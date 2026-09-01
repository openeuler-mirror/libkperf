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
 * Description: Writes trace events (pc, tid, timestamp, etc.) into the shared memory ring buffer
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
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

final class SharedEventSink implements AutoCloseable {

    // Shared memory ABI v2. Strings live in dictionaries and compact event slots contain ids
    static final long MAGIC = 0x5554524356415731L; // UTRCVAW1
    static final int VERSION = 2;

    static final int HEADER_SIZE = 128;
    static final int HEADER_MAGIC = 0;
    static final int HEADER_VERSION = 8;
    static final int HEADER_ACTIVE = 12;
    static final int HEADER_SLOT_COUNT = 16;
    static final int HEADER_SLOT_SIZE = 20;
    static final int HEADER_WRITE_SEQ = 24;
    static final int HEADER_DROPPED = 32;
    static final int HEADER_METHOD_CAPACITY = 40;
    static final int HEADER_METHOD_ENTRY_SIZE = 44;
    static final int HEADER_THREAD_CAPACITY = 48;
    static final int HEADER_THREAD_ENTRY_SIZE = 52;
    static final int HEADER_METHOD_COUNT = 56;
    static final int HEADER_THREAD_COUNT = 60;
    static final int HEADER_EVENT_OFFSET = 64;

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

    static final int SLOT_SIZE = 64;
    static final int SLOT_SEQ = 0;
    static final int SLOT_ADDR = 8;
    static final int SLOT_METHOD_ID = 16;
    static final int SLOT_COMM_ID = 20;
    static final int SLOT_TID = 24;
    static final int SLOT_CPU = 28;
    static final int SLOT_TIMESTAMP = 32;
    static final int SLOT_GPTR = 40;
    static final int SLOT_IS_RET = 48;

    private static final int DEFAULT_SLOT_COUNT = 2097152;
    private static final int MAX_SLOT_COUNT = 67108864;
    private static final int SLOTS_PER_SEGMENT = 2097152;
    private static final int SEGMENT_SHIFT = 21;
    private static final int SEGMENT_SLOT_MASK = SLOTS_PER_SEGMENT - 1;
    private static final long CLOSE_WAIT_NANOS = 1000000000L;
    private static final long WRITER_CLOSED = Long.MIN_VALUE;
    private static final long WRITER_COUNT_MASK = Long.MAX_VALUE;
    private static final byte[] EMPTY_BYTES = new byte[0];

    private final MappedByteBuffer headerBuffer;
    private final MappedByteBuffer methodBuffer;
    private final MappedByteBuffer threadBuffer;
    private final MappedByteBuffer[] slotBuffers;
    private final int slotCount;
    private final int slotMask;
    private final long eventOffset;
    private final AtomicInteger nextThreadId = new AtomicInteger(0);
    private final AtomicInteger threadCount = new AtomicInteger(0);
    private final AtomicLong nextSequence = new AtomicLong(1L);
    private final AtomicLong publishedSequence = new AtomicLong(0L);
    private final AtomicLong droppedEvents = new AtomicLong(0L);
    private final AtomicLong writerState = new AtomicLong(0L);
    private final AtomicBoolean publishing = new AtomicBoolean(false);
    private volatile boolean closed;

    SharedEventSink(String shmPath, int slotCount) throws IOException {
        this.slotCount = clampSlotCount(slotCount);
        this.slotMask = isPowerOfTwo(this.slotCount) ? this.slotCount - 1 : -1;
        long methodBytes = (long) METHOD_CAPACITY * METHOD_ENTRY_SIZE;
        long threadBytes = (long) THREAD_CAPACITY * THREAD_ENTRY_SIZE;
        this.eventOffset = HEADER_SIZE + methodBytes + threadBytes;

        Path p = Paths.get(shmPath).toAbsolutePath().normalize();
        long size = eventOffset + (long) this.slotCount * SLOT_SIZE;
        int segmentCount = (this.slotCount + SLOTS_PER_SEGMENT - 1) / SLOTS_PER_SEGMENT;

        try (RandomAccessFile raf = new RandomAccessFile(p.toFile(), "rw");
             FileChannel channel = raf.getChannel()) {
            raf.setLength(size);
            this.headerBuffer = channel.map(FileChannel.MapMode.READ_WRITE, 0, HEADER_SIZE);
            this.headerBuffer.order(ByteOrder.nativeOrder());
            this.methodBuffer = channel.map(FileChannel.MapMode.READ_WRITE, HEADER_SIZE, methodBytes);
            this.methodBuffer.order(ByteOrder.nativeOrder());
            this.threadBuffer = channel.map(FileChannel.MapMode.READ_WRITE,
                    HEADER_SIZE + methodBytes, threadBytes);
            this.threadBuffer.order(ByteOrder.nativeOrder());

            MappedByteBuffer[] segments = new MappedByteBuffer[segmentCount];
            for (int i = 0; i < segmentCount; i++) {
                int firstSlot = i * SLOTS_PER_SEGMENT;
                int slotsInSegment = Math.min(SLOTS_PER_SEGMENT, this.slotCount - firstSlot);
                long segmentOffset = eventOffset + (long) firstSlot * SLOT_SIZE;
                long segmentSize = (long) slotsInSegment * SLOT_SIZE;
                MappedByteBuffer segment = channel.map(FileChannel.MapMode.READ_WRITE, segmentOffset, segmentSize);
                segment.order(ByteOrder.nativeOrder());
                segments[i] = segment;
            }
            this.slotBuffers = segments;
        }
        initHeader();
    }

    private void initHeader() {
        headerBuffer.putLong(HEADER_MAGIC, MAGIC);
        headerBuffer.putInt(HEADER_VERSION, VERSION);
        headerBuffer.putInt(HEADER_ACTIVE, 0);
        headerBuffer.putInt(HEADER_SLOT_COUNT, slotCount);
        headerBuffer.putInt(HEADER_SLOT_SIZE, SLOT_SIZE);
        headerBuffer.putLong(HEADER_WRITE_SEQ, 0L);
        headerBuffer.putLong(HEADER_DROPPED, 0L);
        headerBuffer.putInt(HEADER_METHOD_CAPACITY, METHOD_CAPACITY);
        headerBuffer.putInt(HEADER_METHOD_ENTRY_SIZE, METHOD_ENTRY_SIZE);
        headerBuffer.putInt(HEADER_THREAD_CAPACITY, THREAD_CAPACITY);
        headerBuffer.putInt(HEADER_THREAD_ENTRY_SIZE, THREAD_ENTRY_SIZE);
        headerBuffer.putInt(HEADER_METHOD_COUNT, 0);
        headerBuffer.putInt(HEADER_THREAD_COUNT, 0);
        headerBuffer.putLong(HEADER_EVENT_OFFSET, eventOffset);
        putIntRelease(HEADER_ACTIVE, 1);
    }

    synchronized boolean registerMethod(int id, long addr, byte[] module, byte[] func) {
        if (closed || id < 0 || id >= METHOD_CAPACITY) {
            return false;
        }
        int base = id * METHOD_ENTRY_SIZE;
        methodBuffer.putLong(base + METHOD_ADDR, addr);
        writeBytesAbsolute(methodBuffer, base + METHOD_MODULE, METHOD_MODULE_LEN, module);
        writeBytesAbsolute(methodBuffer, base + METHOD_FUNC, METHOD_FUNC_LEN, func);
        putIntRelease(methodBuffer, base + METHOD_SEQ, id + 1);
        int count = id + 1;
        if (count > headerBuffer.getInt(HEADER_METHOD_COUNT)) {
            putIntRelease(HEADER_METHOD_COUNT, count);
        }
        return true;
    }

    int registerThread(int tid, byte[] comm) {
        if (!enterWriter()) {
            return -1;
        }
        try {
            int id = nextThreadId.getAndIncrement();
            if (closed || id < 0 || id >= THREAD_CAPACITY) {
                return -1;
            }
            int base = id * THREAD_ENTRY_SIZE;
            threadBuffer.putInt(base + THREAD_TID, tid);
            writeBytesAbsolute(threadBuffer, base + THREAD_COMM, THREAD_COMM_LEN, comm);
            putIntRelease(threadBuffer, base + THREAD_SEQ, id + 1);
            publishThreadCount(id + 1);
            return id;
        } finally {
            writerState.decrementAndGet();
        }
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

    boolean record(long addr, int methodId, int commId, int tid, int cpu,
                   long timestamp, long gPtr, int isRet) {
        if (!enterWriter()) {
            return false;
        }
        try {
            if (!isActive()) {
                return false;
            }
            long seq = claimSequence();
            if (seq <= 0L) {
                return false;
            }

            int slotIndex = slotIndex(seq);
            MappedByteBuffer slotBuffer = slotBuffer(slotIndex);
            int base = slotOffset(slotIndex);

            slotBuffer.putLong(base + SLOT_ADDR, addr);
            slotBuffer.putInt(base + SLOT_METHOD_ID, methodId);
            slotBuffer.putInt(base + SLOT_COMM_ID, commId);
            slotBuffer.putInt(base + SLOT_TID, tid);
            slotBuffer.putInt(base + SLOT_CPU, cpu);
            slotBuffer.putLong(base + SLOT_TIMESTAMP, timestamp);
            slotBuffer.putLong(base + SLOT_GPTR, gPtr);
            slotBuffer.putInt(base + SLOT_IS_RET, isRet);

            // Publish this slot after all slot fields are written.
            putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
            drainPublished();
            return true;
        } finally {
            writerState.decrementAndGet();
        }
    }

    private boolean enterWriter() {
        while (true) {
            long state = writerState.get();
            if ((state & WRITER_CLOSED) != 0L) {
                return false;
            }
            if (writerState.compareAndSet(state, state + 1L)) {
                return true;
            }
        }
    }

    private long claimSequence() {
        while (!closed) {
            long published = publishedSequence.get();
            long next = nextSequence.get();
            if (next - published > slotCount) {
                incrementDropped();
                return -1L;
            }
            if (nextSequence.compareAndSet(next, next + 1L)) {
                return next;
            }
        }
        return -1L;
    }

    private void drainPublished() {
        while (!publishing.compareAndSet(false, true)) {
            if (closed) {
                return;
            }
            Thread.yield();
        }
        try {
            long seq = publishedSequence.get();
            while (!closed) {
                long next = seq + 1L;
                if (next >= nextSequence.get()) {
                    return;
                }
                int slotIndex = slotIndex(next);
                MappedByteBuffer slotBuffer = slotBuffer(slotIndex);
                int base = slotOffset(slotIndex);
                if (getLongAcquire(slotBuffer, base + SLOT_SEQ) != next) {
                    return;
                }
                putLongRelease(HEADER_WRITE_SEQ, next);
                publishedSequence.set(next);
                seq = next;
            }
        } finally {
            publishing.set(false);
        }
    }

    private int slotIndex(long sequence) {
        long zeroBased = sequence - 1L;
        return slotMask >= 0 ? (int) (zeroBased & slotMask) : (int) (zeroBased % slotCount);
    }

    private MappedByteBuffer slotBuffer(int slotIndex) {
        return slotBuffers[slotIndex >>> SEGMENT_SHIFT];
    }

    private int slotOffset(int slotIndex) {
        return (slotIndex & SEGMENT_SLOT_MASK) * SLOT_SIZE;
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

    private static int clampSlotCount(int value) {
        if (value <= 0) {
            return DEFAULT_SLOT_COUNT;
        }
        return Math.min(value, MAX_SLOT_COUNT);
    }

    private static boolean isPowerOfTwo(int value) {
        return (value & (value - 1)) == 0;
    }

    private int getIntAcquire(int offset) {
        int value = headerBuffer.getInt(offset);
        MemoryFence.acquireFence();
        return value;
    }

    private long getLongAcquire(int offset) {
        return getLongAcquire(headerBuffer, offset);
    }

    private long getLongAcquire(ByteBuffer target, int offset) {
        long value = target.getLong(offset);
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
        closeWriterGate();

        try {
            putIntRelease(HEADER_ACTIVE, 0);
        } catch (RuntimeException | LinkageError ignored) {
        }

        long deadline = System.nanoTime() + CLOSE_WAIT_NANOS;
        while (activeWriterCount() != 0L && System.nanoTime() - deadline < 0L) {
            Thread.yield();
        }

        // Never unmap while a writer may still hold a buffer reference.
        // Removing TraceRuntime's sink reference lets GC clean it later.
        if (activeWriterCount() != 0L) {
            return;
        }

        for (MappedByteBuffer slotBuffer : slotBuffers) {
            BufferCleaner.clean(slotBuffer);
        }
        BufferCleaner.clean(threadBuffer);
        BufferCleaner.clean(methodBuffer);
        BufferCleaner.clean(headerBuffer);
    }

    private void closeWriterGate() {
        while (true) {
            long state = writerState.get();
            if ((state & WRITER_CLOSED) != 0L ||
                    writerState.compareAndSet(state, state | WRITER_CLOSED)) {
                return;
            }
        }
    }

    private long activeWriterCount() {
        return writerState.get() & WRITER_COUNT_MASK;
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
