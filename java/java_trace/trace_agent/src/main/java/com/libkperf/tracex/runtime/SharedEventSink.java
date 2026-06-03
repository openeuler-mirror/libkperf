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
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;

final class SharedEventSink implements AutoCloseable {

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

    private final MappedByteBuffer buffer;
    private final int slotCount;
    private volatile boolean closed;

    SharedEventSink(String shmPath, int slotCount) throws IOException {
        this.slotCount = slotCount > 0 ? slotCount : 4096;

        Path p = Paths.get(shmPath).toAbsolutePath().normalize();
        long size = HEADER_SIZE + (long) this.slotCount * SLOT_SIZE;

        try (RandomAccessFile raf = new RandomAccessFile(p.toFile(), "rw");
             FileChannel channel = raf.getChannel()) {
            raf.setLength(size);
            this.buffer = channel.map(FileChannel.MapMode.READ_WRITE, 0, size);
            this.buffer.order(ByteOrder.nativeOrder());
        }

        initHeader();
    }

    private void initHeader() {
        buffer.putLong(HEADER_MAGIC, MAGIC);
        buffer.putInt(HEADER_VERSION, VERSION);
        buffer.putInt(HEADER_ACTIVE, 1);
        buffer.putInt(HEADER_SLOT_COUNT, slotCount);
        buffer.putInt(HEADER_SLOT_SIZE, SLOT_SIZE);
        buffer.putLong(HEADER_WRITE_SEQ, 0L);
        buffer.putLong(HEADER_DROPPED, 0L);
    }

    synchronized boolean isActive() {
        if (closed) {
            return false;
        }
        return buffer.getInt(HEADER_ACTIVE) != 0;
    }

    synchronized void record(long addr, String comm, int tid, int cpu, long timestamp,
                             long gPtr, String module, String func, int isRet) {
        if (!isActive()) {
            return;
        }

        long seq = buffer.getLong(HEADER_WRITE_SEQ) + 1;
        int slotIndex = (int) ((seq - 1) % slotCount);
        int base = HEADER_SIZE + slotIndex * SLOT_SIZE;

        buffer.putLong(base + SLOT_ADDR, addr);
        buffer.putInt(base + SLOT_TID, tid);
        buffer.putInt(base + SLOT_CPU, cpu);
        buffer.putLong(base + SLOT_TIMESTAMP, timestamp);
        buffer.putLong(base + SLOT_GPTR, gPtr);
        buffer.putInt(base + SLOT_IS_RET, isRet);

        writeString(base + SLOT_COMM, SLOT_COMM_LEN, comm);
        writeString(base + SLOT_MODULE, SLOT_MODULE_LEN, module);
        writeString(base + SLOT_FUNC, SLOT_FUNC_LEN, func);

        buffer.putLong(base + SLOT_SEQ, seq);
        buffer.putLong(HEADER_WRITE_SEQ, seq);
    }

    private void writeString(int offset, int cap, String value) {
        ByteBuffer dup = buffer.duplicate();
        dup.position(offset);

        byte[] bytes = value == null
                ? new byte[0]
                : value.getBytes(StandardCharsets.UTF_8);

        int len = Math.min(bytes.length, cap - 1);
        dup.put(bytes, 0, len);

        for (int i = len; i < cap; i++) {
            dup.put((byte) 0);
        }
    }

    @Override
    public synchronized void close() {
        if (closed) {
            return;
        }
        closed = true;

        try {
            buffer.putInt(HEADER_ACTIVE, 0);
        } catch (Throwable ignored) {
        }

        try {
            buffer.force();
        } catch (Throwable ignored) {
        }
    }
}