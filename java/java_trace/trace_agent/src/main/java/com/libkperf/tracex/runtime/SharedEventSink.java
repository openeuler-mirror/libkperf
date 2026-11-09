/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-04-27
 * Description: Writes trace events (pc, tid, timestamp, etc.) into shared memory ring buffers.
 * The sink owns one ring per available CPU, mapped eagerly at startup. Threads bind at their
 * first event to the ring of the CPU they are running on and keep that ring for life, so the
 * claim CAS is only contended by threads sharing one CPU bucket. There is no global sequence,
 * drain thread, or registration lock anywhere on the hot path.
 ******************************************************************************/
package com.libkperf.tracex.runtime;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

final class SharedEventSink implements AutoCloseable {

    // Shared memory ABI v4. Strings live in dictionaries, events live in per-ring slot buffers
    // and every ring publishes its own monotonic sequence through a cache-line padded descriptor.
    static final long MAGIC = 0x5554524356415731L; // UTRCVAW1
    static final int VERSION = 4;

    static final int HEADER_SIZE = 128;
    static final int HEADER_MAGIC = 0;
    static final int HEADER_VERSION = 8;
    static final int HEADER_ACTIVE = 12;
    // Offsets 16 and 20 are reserved (removed ring mode / group size fields).
    static final int HEADER_RING_COUNT = 24;
    static final int HEADER_SLOTS_PER_RING = 28;
    static final int HEADER_DROPPED = 32;
    static final int HEADER_METHOD_CAPACITY = 40;
    static final int HEADER_METHOD_ENTRY_SIZE = 44;
    static final int HEADER_THREAD_CAPACITY = 48;
    static final int HEADER_THREAD_ENTRY_SIZE = 52;
    static final int HEADER_METHOD_COUNT = 56;
    static final int HEADER_THREAD_COUNT = 60;
    static final int HEADER_EVENT_OFFSET = 64;
    static final int HEADER_DESC_OFFSET = 72;

    static final int METHOD_CAPACITY = 131072;
    static final int METHOD_ENTRY_SIZE = 448;
    static final int METHOD_SEQ = 0;
    static final int METHOD_ADDR = 8;
    static final int METHOD_MODULE = 16;
    static final int METHOD_MODULE_LEN = 160;
    static final int METHOD_FUNC = 176;
    static final int METHOD_FUNC_LEN = 256;

    static final int THREAD_CAPACITY = 65536;
    static final int THREAD_ENTRY_SIZE = 48;
    static final int THREAD_SEQ = 0;
    static final int THREAD_TID = 4;
    static final int THREAD_COMM = 8;
    static final int THREAD_COMM_LEN = 32;

    // Ring descriptor table: one cache-line padded 128-byte entry per ring.
    static final int DESC_SIZE = 128;
    static final int DESC_WRITE_SEQ = 0;
    static final int DESC_DROPPED = 8;
    static final int DESC_TID = 16;
    static final int DESC_RING_IDX = 20;

    // Record layout: SLOT_SEQ is the publish marker (first field, written last) and
    // SLOT_DATA_TYPE (second field) selects which per-type field group decodes the rest.
    static final int SLOT_SIZE = 64;
    static final int SLOT_SEQ = 0;
    static final int SLOT_DATA_TYPE = 8;

    // METHOD record field group (DATA_TYPE_METHOD).
    static final int SLOT_METHOD_IS_RET = 12;
    static final int SLOT_METHOD_ADDR = 16;
    static final int SLOT_METHOD_METHOD_ID = 24;
    static final int SLOT_METHOD_COMM_ID = 28;
    static final int SLOT_METHOD_TID = 32;
    static final int SLOT_METHOD_CPU = 36;
    static final int SLOT_METHOD_TIMESTAMP = 40;
    static final int SLOT_METHOD_GPTR = 48;

    // VT_PHASE record field group (DATA_TYPE_VT_PHASE): only the lifecycle fields are written.
    static final int SLOT_VT_CARRIER_TID = 16;
    static final int SLOT_VT_VT_ID = 24;
    static final int SLOT_VT_PHASE_ID = 32;
    static final int SLOT_VT_CPU = 36;
    static final int SLOT_VT_TIMESTAMP = 40;
    // SOCKET_READ phase reuses the reserved tail of the VT_PHASE field group (bytes 48..63)
    // to carry the socket fd and the byte count returned by SocketChannelImpl.read.
    static final int SLOT_VT_FD = 48;
    static final int SLOT_VT_BYTES = 52;
    // Carrier OS tid (NativeThreadInfo.currentTidSafe) of the thread that emitted the event.
    static final int SLOT_VT_OS_TID = 60;
    // Thread dictionary id of the carrier (see registerCarrierThread); -1 = absent.
    static final int SLOT_VT_COMM_ID = 12;

    // SLOT_DATA_TYPE: coarse record kind, drives the consumer's payload dispatch.
    static final short DATA_TYPE_METHOD = 0;
    static final short DATA_TYPE_VT_PHASE = 1;

    // VT_PHASE phaseId value for SocketChannelImpl.read completion events.
    static final int VT_PHASE_SOCKET_READ_ID = 5;
    // VT_PHASE phaseId value for NioSocketImpl.implWrite completion events.
    static final int VT_PHASE_SOCKET_WRITE_ID = 6;

    // METHOD records carry the enter/exit or virtual-thread transition marker in SLOT_METHOD_IS_RET.
    static final int EVENT_METHOD_ENTER = 0;
    static final int EVENT_METHOD_EXIT = 1;
    static final int EVENT_VTHREAD_MOUNT = 2;
    static final int EVENT_VTHREAD_UNMOUNT = 3;
    // Fallback inference when carrier call-site hooks cannot be installed.
    static final int EVENT_VTHREAD_MOUNT_DEGRADED = 4;
    static final int EVENT_VTHREAD_UNMOUNT_DEGRADED = 5;

    static final int DEFAULT_SLOTS_PER_RING = 65536;
    static final int MAX_SLOTS_PER_RING = 1 << 22;
    private static final long CLOSE_WAIT_NANOS = 1000000000L;
    private static final byte[] EMPTY_BYTES = new byte[0];

    private final MappedByteBuffer headerBuffer;
    private final MappedByteBuffer methodBuffer;
    private final MappedByteBuffer threadBuffer;
    private final MappedByteBuffer descBuffer;
    private final Ring[] rings;
    private final int ringCount;
    private final int slotsPerRing;
    private final int slotMask;
    private final long eventOffset;
    private final AtomicInteger nextThreadId = new AtomicInteger(0);
    private final AtomicInteger threadCount = new AtomicInteger(0);
    private final AtomicInteger methodCount = new AtomicInteger(0);
    // thread meta data --> thread ID in Shared Memory. These maps are caches, not the
    // source of truth. Concurrent first-time registrations of the same key may allocate
    // two dictionary ids, but every entry is fully published and the consumer resolves
    // each commId independently, so duplicate ids are harmless. ConcurrentHashMap keeps
    // the concurrent put/resize safe.
    private final ConcurrentHashMap<ThreadMetadataKey, Integer> threadIds =
            new ConcurrentHashMap<ThreadMetadataKey, Integer>();
    // A virtual thread may migrate between carriers. Keep one metadata id per carrier TID
    // instead of creating a (carrier TID, virtual-thread name) entry for every migration.
    private final ConcurrentHashMap<Integer, Integer> carrierThreadIds =
            new ConcurrentHashMap<Integer, Integer>();
    private final AtomicLong droppedEvents = new AtomicLong(0L);
    private volatile boolean closed;

    private static final class ThreadMetadataKey {
        private final int tid;
        private final byte[] comm;
        private final int hashCode;

        ThreadMetadataKey(int tid, byte[] comm) {
            this.tid = tid;
            byte[] value = comm == null ? EMPTY_BYTES : comm;
            int length = Math.min(value.length, THREAD_COMM_LEN - 1);
            this.comm = Arrays.copyOf(value, length);
            this.hashCode = 31 * tid + Arrays.hashCode(this.comm);
        }

        @Override
        public int hashCode() {
            return hashCode;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) {
                return true;
            }
            if (!(other instanceof ThreadMetadataKey)) {
                return false;
            }
            ThreadMetadataKey that = (ThreadMetadataKey) other;
            return tid == that.tid && Arrays.equals(comm, that.comm);
        }
    }

    // One ring buffer plus its descriptor location and ring-local sequence machinery.
    // Threads bound to the same CPU bucket share the claim CAS; nothing here is global.
    private static final class Ring {
        final int index;
        final int descBase;
        final MappedByteBuffer data;
        final AtomicLong claim = new AtomicLong(0L);
        final AtomicLong publish = new AtomicLong(0L);

        Ring(int index, MappedByteBuffer data) {
            this.index = index;
            this.descBase = index * DESC_SIZE;
            this.data = data;
        }
    }

    static void preloadHelpers() {
        // Force resolution/initialization outside a HotSpot VTMS transition.
        new ThreadMetadataKey(0, EMPTY_BYTES).hashCode();
        new AtomicLong(0L).incrementAndGet();
        new AtomicInteger(0).incrementAndGet();
        MemoryFence.acquireFence();
        MemoryFence.releaseFence();
    }

    SharedEventSink(String shmPath, int slotsPerRing) throws IOException {
        this.slotsPerRing = clampSlotsPerRing(slotsPerRing);
        this.slotMask = isPowerOfTwo(this.slotsPerRing) ? this.slotsPerRing - 1 : -1;
        this.ringCount = clampRingCount(Runtime.getRuntime().availableProcessors());
        long ringDataBytes = (long) this.slotsPerRing * SLOT_SIZE;
        long methodBytes = (long) METHOD_CAPACITY * METHOD_ENTRY_SIZE;
        long threadBytes = (long) THREAD_CAPACITY * THREAD_ENTRY_SIZE;
        long descOffset = HEADER_SIZE + methodBytes + threadBytes;
        this.eventOffset = descOffset + (long) this.ringCount * DESC_SIZE;

        Path p = Paths.get(shmPath).toAbsolutePath().normalize();
        long size = eventOffset + (long) this.ringCount * ringDataBytes;
        // FileChannel.truncate cannot grow a file; RandomAccessFile.setLength matches the
        // backend's ftruncate sizing semantics (production files are pre-created at the
        // same size and this is a no-op then).
        try (RandomAccessFile raf = new RandomAccessFile(p.toFile(), "rw")) {
            if (raf.length() != size) {
                raf.setLength(size);
            }
        }

        MappedByteBuffer header = null;
        MappedByteBuffer method = null;
        MappedByteBuffer thread = null;
        MappedByteBuffer desc = null;
        Ring[] mappedRings = null;
        boolean complete = false;
        try (FileChannel channel = FileChannel.open(p, StandardOpenOption.READ, StandardOpenOption.WRITE)) {
            header = channel.map(FileChannel.MapMode.READ_WRITE, 0, HEADER_SIZE);
            header.order(ByteOrder.nativeOrder());
            method = channel.map(FileChannel.MapMode.READ_WRITE, HEADER_SIZE, methodBytes);
            method.order(ByteOrder.nativeOrder());
            thread = channel.map(FileChannel.MapMode.READ_WRITE, HEADER_SIZE + methodBytes, threadBytes);
            thread.order(ByteOrder.nativeOrder());
            desc = channel.map(FileChannel.MapMode.READ_WRITE, descOffset, (long) this.ringCount * DESC_SIZE);
            desc.order(ByteOrder.nativeOrder());
            mappedRings = new Ring[this.ringCount];
            for (int i = 0; i < this.ringCount; i++) {
                MappedByteBuffer buf = channel.map(FileChannel.MapMode.READ_WRITE,
                        eventOffset + (long) i * ringDataBytes, ringDataBytes);
                buf.order(ByteOrder.nativeOrder());
                mappedRings[i] = new Ring(i, buf);
            }
            this.headerBuffer = header;
            this.methodBuffer = method;
            this.threadBuffer = thread;
            this.descBuffer = desc;
            this.rings = mappedRings;
            initHeader(descOffset);
            complete = true;
        } finally {
            if (!complete) {
                if (mappedRings != null) {
                    for (Ring ring : mappedRings) {
                        if (ring != null) {
                            BufferCleaner.clean(ring.data);
                        }
                    }
                }
                if (desc != null) {
                    BufferCleaner.clean(desc);
                }
                if (thread != null) {
                    BufferCleaner.clean(thread);
                }
                if (method != null) {
                    BufferCleaner.clean(method);
                }
                if (header != null) {
                    BufferCleaner.clean(header);
                }
            }
        }
    }

    private void initHeader(long descOffset) {
        headerBuffer.putLong(HEADER_MAGIC, MAGIC);
        headerBuffer.putInt(HEADER_VERSION, VERSION);
        headerBuffer.putInt(HEADER_ACTIVE, 0);
        headerBuffer.putInt(HEADER_RING_COUNT, ringCount);
        headerBuffer.putInt(HEADER_SLOTS_PER_RING, slotsPerRing);
        headerBuffer.putLong(HEADER_DROPPED, 0L);
        headerBuffer.putInt(HEADER_METHOD_CAPACITY, METHOD_CAPACITY);
        headerBuffer.putInt(HEADER_METHOD_ENTRY_SIZE, METHOD_ENTRY_SIZE);
        headerBuffer.putInt(HEADER_THREAD_CAPACITY, THREAD_CAPACITY);
        headerBuffer.putInt(HEADER_THREAD_ENTRY_SIZE, THREAD_ENTRY_SIZE);
        headerBuffer.putInt(HEADER_METHOD_COUNT, 0);
        headerBuffer.putInt(HEADER_THREAD_COUNT, 0);
        headerBuffer.putLong(HEADER_DESC_OFFSET, descOffset);
        headerBuffer.putLong(HEADER_EVENT_OFFSET, eventOffset);
        // Reset every ring publish marker so a reused file never exposes stale sequences.
        for (int i = 0; i < ringCount; i++) {
            descBuffer.putLong(i * DESC_SIZE + DESC_WRITE_SEQ, 0L);
        }
        putIntRelease(HEADER_ACTIVE, 1);
    }

    // Lock-free registration: ids are unique through nextThreadId.getAndIncrement(), so every
    // dictionary slot has exactly one writer and publication needs only a release store.
    boolean registerMethod(int id, long addr, byte[] module, byte[] func) {
        if (closed || id < 0 || id >= METHOD_CAPACITY) {
            return false;
        }
        try {
            int base = id * METHOD_ENTRY_SIZE;
            methodBuffer.putLong(base + METHOD_ADDR, addr);
            writeBytesAbsolute(methodBuffer, base + METHOD_MODULE, METHOD_MODULE_LEN, module);
            writeBytesAbsolute(methodBuffer, base + METHOD_FUNC, METHOD_FUNC_LEN, func);
            putIntRelease(methodBuffer, base + METHOD_SEQ, id + 1);
            publishMethodCount(id + 1);
            return true;
        } catch (RuntimeException | LinkageError e) {
            TraceLog.warn("[trace-java-sink] registerMethod failed: id=" + id + ", " + e, e);
            return false;
        }
    }

    private void publishMethodCount(int value) {
        int current;
        do {
            current = methodCount.get();
            if (value <= current) {
                return;
            }
        } while (!methodCount.compareAndSet(current, value));
        putIntRelease(HEADER_METHOD_COUNT, value);
    }

    int registerThread(int tid, byte[] comm) {
        if (closed) {
            return -1;
        }
        ThreadMetadataKey key = new ThreadMetadataKey(tid, comm);
        Integer existing = threadIds.get(key);
        if (existing != null) {
            return existing.intValue();
        }
        int id = nextThreadId.getAndIncrement();
        if (id < 0 || id >= THREAD_CAPACITY) {
            incrementDropped();
            return -1;
        }
        int base = id * THREAD_ENTRY_SIZE;
        threadBuffer.putInt(base + THREAD_TID, tid);
        writeBytesAbsolute(threadBuffer, base + THREAD_COMM, THREAD_COMM_LEN, comm);
        putIntRelease(threadBuffer, base + THREAD_SEQ, id + 1);
        publishThreadCount(id + 1);
        threadIds.put(key, Integer.valueOf(id));
        return id;
    }

    int registerCarrierThread(int tid, byte[] comm) {
        if (closed) {
            return -1;
        }
        Integer existing = carrierThreadIds.get(Integer.valueOf(tid));
        if (existing != null) {
            return existing.intValue();
        }
        int id = registerThread(tid, comm);
        if (id < 0) {
            return -1;
        }
        carrierThreadIds.put(Integer.valueOf(tid), Integer.valueOf(id));
        return id;
    }

    private void publishThreadCount(int value) {
        int current;
        do {
            current = threadCount.get();
            if (value <= current) {
                return;
            }
        } while (!threadCount.compareAndSet(current, value));
        putIntRelease(HEADER_THREAD_COUNT, value);
    }

    boolean isActive() {
        if (closed) {
            return false;
        }
        return getIntAcquire(HEADER_ACTIVE) != 0;
    }

    // METHOD record: writes DATA_TYPE_METHOD and the METHOD field group.
    boolean recordMethod(long addr, int methodId, int commId, int tid, int cpu,
                         long timestamp, long gPtr, int isRet) {
        if (TraceLog.isDebugEnabled()) {
            TraceLog.debug("[trace-java-sink] recordMethod methodId=" + methodId
                    + ", method=" + readMethodDictionary(methodId)
                    + ", event=" + eventTypeName(isRet) + "(" + isRet + ")"
                    + ", addr=0x" + Long.toHexString(addr)
                    + ", commId=" + commId + ", tid=" + tid + ", cpu=" + cpu
                    + ", timestamp=" + timestamp + ", gPtr=0x" + Long.toHexString(gPtr));
        }
        if (!isActive()) {
            return false;
        }
        Ring ring = ringFor(cpu);
        long seq = ring.claim.incrementAndGet();
        int base = slotIndex(seq) * SLOT_SIZE;
        MappedByteBuffer slotBuffer = ring.data;

        slotBuffer.putShort(base + SLOT_DATA_TYPE, DATA_TYPE_METHOD);
        slotBuffer.putLong(base + SLOT_METHOD_ADDR, addr);
        slotBuffer.putInt(base + SLOT_METHOD_METHOD_ID, methodId);
        slotBuffer.putInt(base + SLOT_METHOD_COMM_ID, commId);
        slotBuffer.putInt(base + SLOT_METHOD_TID, tid);
        slotBuffer.putInt(base + SLOT_METHOD_CPU, cpu);
        slotBuffer.putLong(base + SLOT_METHOD_TIMESTAMP, timestamp);
        slotBuffer.putLong(base + SLOT_METHOD_GPTR, gPtr);
        slotBuffer.putInt(base + SLOT_METHOD_IS_RET, isRet);

        // Publish this slot after all slot fields are written.
        putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
        publishSequence(ring, seq);
        return true;
    }

    private String readMethodDictionary(int methodId) {
        if (methodId < 0 || methodId >= METHOD_CAPACITY) {
            return "unknown(methodId=" + methodId + ")";
        }
        try {
            if (closed) {
                return "unknown(methodId=" + methodId + ")";
            }
            int base = methodId * METHOD_ENTRY_SIZE;
            String module = readDictString(methodBuffer, base + METHOD_MODULE, METHOD_MODULE_LEN);
            String func = readDictString(methodBuffer, base + METHOD_FUNC, METHOD_FUNC_LEN);
            return module + "." + func;
        } catch (Throwable t) {
            return "unknown(methodId=" + methodId + ")";
        }
    }

    private static String readDictString(ByteBuffer source, int offset, int cap) {
        byte[] bytes = new byte[cap];
        for (int i = 0; i < cap; i++) {
            byte b = source.get(offset + i);
            if (b == 0) {
                return new String(bytes, 0, i, StandardCharsets.UTF_8);
            }
            bytes[i] = b;
        }
        return new String(bytes, StandardCharsets.UTF_8);
    }

    private static String eventTypeName(int isRet) {
        switch (isRet) {
            case EVENT_METHOD_ENTER:
                return "METHOD_ENTER";
            case EVENT_METHOD_EXIT:
                return "METHOD_EXIT";
            case EVENT_VTHREAD_MOUNT:
                return "VTHREAD_MOUNT";
            case EVENT_VTHREAD_UNMOUNT:
                return "VTHREAD_UNMOUNT";
            case EVENT_VTHREAD_MOUNT_DEGRADED:
                return "VTHREAD_MOUNT_DEGRADED";
            case EVENT_VTHREAD_UNMOUNT_DEGRADED:
                return "VTHREAD_UNMOUNT_DEGRADED";
            default:
                return "UNKNOWN";
        }
    }

    // VT_PHASE record: writes DATA_TYPE_VT_PHASE and only the VT_PHASE field group,
    // so no method/comm/tid/isRet bytes are produced for lifecycle events.
    boolean recordVtPhase(long carrierTid, long vtId, int phaseId, int cpu, long timestamp,
                          int osTid, int commId) {
        if (!isActive()) {
            return false;
        }
        Ring ring = ringFor(cpu);
        long seq = ring.claim.incrementAndGet();
        int base = slotIndex(seq) * SLOT_SIZE;
        MappedByteBuffer slotBuffer = ring.data;

        slotBuffer.putShort(base + SLOT_DATA_TYPE, DATA_TYPE_VT_PHASE);
        slotBuffer.putInt(base + SLOT_VT_COMM_ID, commId);
        slotBuffer.putLong(base + SLOT_VT_CARRIER_TID, carrierTid);
        slotBuffer.putLong(base + SLOT_VT_VT_ID, vtId);
        slotBuffer.putInt(base + SLOT_VT_PHASE_ID, phaseId);
        slotBuffer.putInt(base + SLOT_VT_CPU, cpu);
        slotBuffer.putLong(base + SLOT_VT_TIMESTAMP, timestamp);
        slotBuffer.putInt(base + SLOT_VT_OS_TID, osTid);

        // Publish this slot after all field groups are written.
        putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
        publishSequence(ring, seq);
        return true;
    }

    boolean recordVtSocketRead(long vtId, int fd, long bytes, int cpu, long timestamp, int osTid) {
        if (!isActive()) {
            return false;
        }
        Ring ring = ringFor(cpu);
        long seq = ring.claim.incrementAndGet();
        int base = slotIndex(seq) * SLOT_SIZE;
        MappedByteBuffer slotBuffer = ring.data;

        slotBuffer.putShort(base + SLOT_DATA_TYPE, DATA_TYPE_VT_PHASE);
        slotBuffer.putInt(base + SLOT_VT_COMM_ID, -1);
        slotBuffer.putLong(base + SLOT_VT_CARRIER_TID, 0L);
        slotBuffer.putLong(base + SLOT_VT_VT_ID, vtId);
        slotBuffer.putInt(base + SLOT_VT_PHASE_ID, VT_PHASE_SOCKET_READ_ID);
        slotBuffer.putInt(base + SLOT_VT_CPU, cpu);
        slotBuffer.putLong(base + SLOT_VT_TIMESTAMP, timestamp);
        slotBuffer.putInt(base + SLOT_VT_FD, fd);
        slotBuffer.putLong(base + SLOT_VT_BYTES, bytes);
        slotBuffer.putInt(base + SLOT_VT_OS_TID, osTid);

        putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
        publishSequence(ring, seq);
        return true;
    }

    boolean recordVtSocketWrite(long vtId, int fd, long bytes, int cpu, long timestamp, int osTid) {
        if (!isActive()) {
            return false;
        }
        Ring ring = ringFor(cpu);
        long seq = ring.claim.incrementAndGet();
        int base = slotIndex(seq) * SLOT_SIZE;
        MappedByteBuffer slotBuffer = ring.data;

        slotBuffer.putShort(base + SLOT_DATA_TYPE, DATA_TYPE_VT_PHASE);
        slotBuffer.putInt(base + SLOT_VT_COMM_ID, -1);
        slotBuffer.putLong(base + SLOT_VT_CARRIER_TID, 0L);
        slotBuffer.putLong(base + SLOT_VT_VT_ID, vtId);
        slotBuffer.putInt(base + SLOT_VT_PHASE_ID, VT_PHASE_SOCKET_WRITE_ID);
        slotBuffer.putInt(base + SLOT_VT_CPU, cpu);
        slotBuffer.putLong(base + SLOT_VT_TIMESTAMP, timestamp);
        slotBuffer.putInt(base + SLOT_VT_FD, fd);
        slotBuffer.putLong(base + SLOT_VT_BYTES, bytes);
        slotBuffer.putInt(base + SLOT_VT_OS_TID, osTid);

        putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
        publishSequence(ring, seq);
        return true;
    }

    // One ring per CPU bucket; threads on the same CPU interleave through the ring-local
    // claim CAS. Without native CPU resolution every event funnels into ring 0 (contention
    // only, correctness still holds).
    private Ring ringFor(int cpu) {
        return cpu >= 0 ? rings[cpu % ringCount] : rings[0];
    }

    private void publishSequence(Ring ring, long seq) {
        // Advance the published watermark to the highest completed sequence.
        // A slow writer that lags behind is skipped by the consumer through slot-seq validation.
        for (;;) {
            long current = ring.publish.get();
            if (current >= seq) {
                return;
            }
            if (ring.publish.compareAndSet(current, seq)) {
                putLongRelease(descBuffer, ring.descBase + DESC_WRITE_SEQ, seq);
                return;
            }
        }
    }

    private int slotIndex(long sequence) {
        long zeroBased = sequence - 1L;
        return slotMask >= 0 ? (int) (zeroBased & slotMask) : (int) (zeroBased % slotsPerRing);
    }

    private void writeBytesAbsolute(ByteBuffer target, int offset, int cap, byte[] value) {
        byte[] bytes = value == null ? EMPTY_BYTES : value;
        int len = Math.min(bytes.length, cap - 1);
        for (int i = 0; i < len; i++) {
            target.put(offset + i, bytes[i]);
        }
        target.put(offset + len, (byte) 0);
    }

    static byte[] utf8(String value) {
        return value == null || value.length() == 0 ? EMPTY_BYTES : value.getBytes(StandardCharsets.UTF_8);
    }

    private void incrementDropped() {
        long dropped = droppedEvents.incrementAndGet();
        putLongRelease(HEADER_DROPPED, dropped);
    }

    private static int clampRingCount(int value) {
        if (value <= 0) {
            return 1;
        }
        return Math.min(value, THREAD_CAPACITY);
    }

    private static int clampSlotsPerRing(int value) {
        if (value <= 0) {
            return DEFAULT_SLOTS_PER_RING;
        }
        return Math.min(value, MAX_SLOTS_PER_RING);
    }

    private static boolean isPowerOfTwo(int value) {
        return (value & (value - 1)) == 0;
    }

    private int getIntAcquire(int offset) {
        int value = headerBuffer.getInt(offset);
        MemoryFence.acquireFence();
        return value;
    }

    private void putIntRelease(int offset, int value) {
        putIntRelease(headerBuffer, offset, value);
    }

    private void putIntRelease(ByteBuffer target, int offset, int value) {
        MemoryFence.releaseFence();
        target.putInt(offset, value);
    }

    private void putLongRelease(int offset, long value) {
        putLongRelease(headerBuffer, offset, value);
    }

    private void putLongRelease(ByteBuffer target, int offset, long value) {
        MemoryFence.releaseFence();
        target.putLong(offset, value);
    }

    @Override
    public synchronized void close() {
        if (closed) {
            return;
        }
        closed = true;
        try {
            putIntRelease(HEADER_ACTIVE, 0);
        } catch (RuntimeException | LinkageError ignored) {
        }

        // Fixed grace period replaces per-writer accounting: a writer that passed its
        // isActive check finishes its stores within a few hundred nanoseconds, so unmapping
        // after 1 s is safe for every interleaving except a multi-second OS preemption.
        long deadline = System.nanoTime() + CLOSE_WAIT_NANOS;
        while (System.nanoTime() - deadline < 0L) {
            Thread.yield();
        }

        for (Ring ring : rings) {
            BufferCleaner.clean(ring.data);
        }
        BufferCleaner.clean(descBuffer);
        BufferCleaner.clean(threadBuffer);
        BufferCleaner.clean(methodBuffer);
        BufferCleaner.clean(headerBuffer);
    }

    private static final class BufferCleaner {
        private BufferCleaner() {
        }

        static void clean(MappedByteBuffer buffer) {
            if (buffer == null || cleanWithUnsafe(buffer)) {
                return;
            }
            cleanWithLegacyCleaner(buffer);
        }

        private static boolean cleanWithUnsafe(ByteBuffer buffer) {
            try {
                Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
                Field field = unsafeClass.getDeclaredField("theUnsafe");
                field.setAccessible(true);
                Object unsafe = field.get(null);
                Method invokeCleaner = unsafeClass.getMethod("invokeCleaner", ByteBuffer.class);
                invokeCleaner.invoke(unsafe, buffer);
                return true;
            } catch (Throwable ignored) {
                return false;
            }
        }

        private static void cleanWithLegacyCleaner(ByteBuffer buffer) {
            try {
                Method cleanerMethod = buffer.getClass().getMethod("cleaner");
                cleanerMethod.setAccessible(true);
                Object cleaner = cleanerMethod.invoke(buffer);
                if (cleaner != null) {
                    Method cleanMethod = cleaner.getClass().getMethod("clean");
                    cleanMethod.invoke(cleaner);
                }
            } catch (Throwable ignored) {
            }
        }
    }

    private static final class MemoryFence {
        private static final MethodHandle ACQUIRE_FENCE = findAcquireFence();
        private static final MethodHandle RELEASE_FENCE = findReleaseFence();

        private MemoryFence() {
        }

        static void acquireFence() {
            MethodHandle fence = ACQUIRE_FENCE;
            if (fence == null) {
                return;
            }
            try {
                fence.invoke();
            } catch (RuntimeException | LinkageError ignored) {
            } catch (Error e) {
                throw e;
            } catch (Throwable ignored) {
            }
        }

        static void releaseFence() {
            MethodHandle fence = RELEASE_FENCE;
            if (fence == null) {
                return;
            }

            try {
                fence.invoke();
            } catch (RuntimeException | LinkageError ignored) {
            } catch (Error e) {
                throw e;
            } catch (Throwable ignored) {
            }
        }

        private static MethodHandle findAcquireFence() {
            MethodHandle fence = findVarHandleFence("acquireFence");
            if (fence != null) {
                return fence;
            }
            return findUnsafeFence("loadFence");
        }

        private static MethodHandle findReleaseFence() {
            MethodHandle fence = findVarHandleFence("releaseFence");
            if (fence != null) {
                return fence;
            }
            return findUnsafeFence("storeFence");
        }

        private static MethodHandle findVarHandleFence(String name) {
            try {
                Class<?> varHandleClass = Class.forName("java.lang.invoke.VarHandle");
                return MethodHandles.publicLookup().findStatic(
                    varHandleClass,
                    name,
                    MethodType.methodType(void.class)
                );
            } catch (ReflectiveOperationException | RuntimeException | LinkageError ignored) {
                return null;
            }
        }

        private static MethodHandle findUnsafeFence(String name) {
            try {
                Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
                Field f = unsafeClass.getDeclaredField("theUnsafe");
                f.setAccessible(true);
                Object unsafe = f.get(null);
                return MethodHandles.lookup()
                    .findVirtual(unsafeClass, name, MethodType.methodType(void.class))
                    .bindTo(unsafe);
            } catch (ReflectiveOperationException | RuntimeException | LinkageError ignored) {
                return null;
            }
        }
    }
}
