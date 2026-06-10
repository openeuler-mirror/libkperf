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
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

final class SharedEventSink implements AutoCloseable {

    // Shared memory ABI. The C++ reader in pmu/trace/java_backend.cpp uses the same offsets
    static final long MAGIC = 0x5554524356415731L; // UTRCVAW1
    static final int VERSION = 1;

    static final int HEADER_SIZE = 64;
    static final int HEADER_MAGIC = 0;
    static final int HEADER_VERSION = 8;
    static final int HEADER_ACTIVE = 12;
    static final int HEADER_SLOT_COUNT = 16;
    static final int HEADER_SLOT_SIZE = 20;
    static final int HEADER_WRITE_SEQ = 24;
    static final int HEADER_DROPPED = 32;

    static final int SLOT_SIZE = 512;
    static final int SLOT_SEQ = 0;
    static final int SLOT_ADDR = 8;
    static final int SLOT_TID = 16;
    static final int SLOT_CPU = 20;
    static final int SLOT_TIMESTAMP = 24;
    static final int SLOT_GPTR = 32;
    static final int SLOT_IS_RET = 40;
    static final int SLOT_COMM = 48;
    static final int SLOT_COMM_LEN = 32;
    static final int SLOT_MODULE = 80;
    static final int SLOT_MODULE_LEN = 160;
    static final int SLOT_FUNC = 240;
    static final int SLOT_FUNC_LEN = 256;

    private static final int DEFAULT_SLOT_COUNT = 1048576;
    private static final int MAX_SLOT_COUNT = 67108864;
    private static final int SLOTS_PER_SEGMENT = 1048576;

    private final MappedByteBuffer headerBuffer;
    private final MappedByteBuffer[] slotBuffers;
    private final int slotCount;
    private final AtomicLong nextSequence = new AtomicLong(1L);
    private final AtomicLong publishedSequence = new AtomicLong(0L);
    private final AtomicLong droppedEvents = new AtomicLong(0L);
    private final AtomicBoolean publishing = new AtomicBoolean(false);
    private volatile boolean closed;

    SharedEventSink(String shmPath, int slotCount) throws IOException {
        this.slotCount = clampSlotCount(slotCount);
        Path p = Paths.get(shmPath).toAbsolutePath().normalize();
        long size = HEADER_SIZE + (long) this.slotCount * SLOT_SIZE;
        int segmentCount = (this.slotCount + SLOTS_PER_SEGMENT - 1) / SLOTS_PER_SEGMENT;

        try (RandomAccessFile raf = new RandomAccessFile(p.toFile(), "rw");
             FileChannel channel = raf.getChannel()) {
            raf.setLength(size);
            this.headerBuffer = channel.map(FileChannel.MapMode.READ_WRITE, 0, HEADER_SIZE);
            this.headerBuffer.order(ByteOrder.nativeOrder());

            MappedByteBuffer[] segments = new MappedByteBuffer[segmentCount];
            for (int i = 0; i < segmentCount; i++) {
                int firstSlot = i * SLOTS_PER_SEGMENT;
                int slotsInSegment = Math.min(SLOTS_PER_SEGMENT, this.slotCount - firstSlot);
                long segmentOffset = HEADER_SIZE + (long) firstSlot * SLOT_SIZE;
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
        putIntRelease(HEADER_ACTIVE, 1);
    }

    boolean isActive() {
        if (closed) {
            return false;
        }
        return getIntAcquire(HEADER_ACTIVE) != 0;
    }

    boolean record(long addr, String comm, int tid, int cpu, long timestamp,
                   long gPtr, String module, String func, int isRet) {
        if (!isActive()) {
            return false;
        }

        long seq = claimSequence();
        if (seq <= 0L) {
            return false;
        }

        int slotIndex = (int) ((seq - 1L) % slotCount);
        MappedByteBuffer slotBuffer = slotBuffer(slotIndex);
        int base = slotOffset(slotIndex);

        slotBuffer.putLong(base + SLOT_ADDR, addr);
        slotBuffer.putInt(base + SLOT_TID, tid);
        slotBuffer.putInt(base + SLOT_CPU, cpu);
        slotBuffer.putLong(base + SLOT_TIMESTAMP, timestamp);
        slotBuffer.putLong(base + SLOT_GPTR, gPtr);
        slotBuffer.putInt(base + SLOT_IS_RET, isRet);

        writeString(slotBuffer, base + SLOT_COMM, SLOT_COMM_LEN, comm);
        writeString(slotBuffer, base + SLOT_MODULE, SLOT_MODULE_LEN, module);
        writeString(slotBuffer, base + SLOT_FUNC, SLOT_FUNC_LEN, func);
        // Publish this slot after all slot fields are written.
        putLongRelease(slotBuffer, base + SLOT_SEQ, seq);
        drainPublished();
        return true;
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
                int slotIndex = (int) ((next - 1L) % slotCount);
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

    private MappedByteBuffer slotBuffer(int slotIndex) {
        return slotBuffers[slotIndex / SLOTS_PER_SEGMENT];
    }

    private int slotOffset(int slotIndex) {
        return (slotIndex % SLOTS_PER_SEGMENT) * SLOT_SIZE;
    }

    private void writeString(MappedByteBuffer target, int offset, int cap, String value) {
        byte[] bytes = value == null ? new byte[0] : value.getBytes(StandardCharsets.UTF_8);
        int len = Math.min(bytes.length, cap - 1);
        for (int i = 0; i < len; i++) {
            target.put(offset + i, bytes[i]);
        }
        for (int i = len; i < cap; i++) {
            target.put(offset + i, (byte) 0);
        }
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

    private int getIntAcquire(int offset) {
        int value = headerBuffer.getInt(offset);
        MemoryFence.acquireFence();
        return value;
    }

    private long getLongAcquire(int offset) {
        return getLongAcquire(headerBuffer, offset);
    }

    private long getLongAcquire(MappedByteBuffer target, int offset) {
        long value = target.getLong(offset);
        MemoryFence.acquireFence();
        return value;
    }

    private void putIntRelease(int offset, int value) {
        MemoryFence.releaseFence();
        headerBuffer.putInt(offset, value);
    }

    private void putLongRelease(int offset, long value) {
        putLongRelease(headerBuffer, offset, value);
    }

    private void putLongRelease(MappedByteBuffer target, int offset, long value) {
        MemoryFence.releaseFence();
        target.putLong(offset, value);
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;

        try {
            putIntRelease(HEADER_ACTIVE, 0);
        } catch (Throwable ignored) {
        }

        try {
            headerBuffer.force();
            for (MappedByteBuffer slotBuffer : slotBuffers) {
                slotBuffer.force();
            }
        } catch (Throwable ignored) {
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
            } catch (Throwable ignored) {
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
            } catch (Throwable ignored) {
                return null;
            }
        }
    }
}
