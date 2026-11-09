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
 * Author: Zou
 * Create: 2026-10-20
 * Description: Defines virtual-thread phase instrumentation target methods and their types
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

public final class VTPhaseInstrumentation {

    public static final int TYPE_NONE = 0;
    public static final int TYPE_RUN = 1;
    public static final int TYPE_DEQUEUE = 2;
    public static final int TYPE_SOCKET_READ = 5;
    public static final int TYPE_NIO_SOCKET_READ = 6;
    public static final int TYPE_NIO_SOCKET_WRITE = 7;
    public static final int TYPE_PARK = 8;
    public static final int TYPE_PARK_NANOS = 9;
    public static final int TYPE_UNPARK = 10;

    static final String VT_PHASE_COLLECTOR_OWNER = "com/libkperf/tracex/runtime/VTPhaseCollector";

    static final String THREAD_OWNER = "java/lang/Thread";
    static final String THREAD_CURRENT_THREAD = "currentThread";
    static final String THREAD_CURRENT_THREAD_DESC = "()Ljava/lang/Thread;";
    static final String THREAD_ID = "threadId";
    static final String THREAD_ID_DESC = "()J";
    static final String THREAD_IS_VIRTUAL = "isVirtual";
    static final String THREAD_IS_VIRTUAL_DESC = "()Z";
    static final String THREAD_CURRENT_CARRIER = "currentCarrierThread";
    static final String THREAD_CURRENT_CARRIER_DESC = "()Ljava/lang/Thread;";
    static final String THREAD_GET_NAME = "getName";
    static final String THREAD_GET_NAME_DESC = "()Ljava/lang/String;";

    static final String CARRIER_THREAD = "jdk/internal/misc/CarrierThread";

    static final String VIRTUAL_THREAD = "java/lang/VirtualThread";
    private static final String FJP_WORK_QUEUE = "java/util/concurrent/ForkJoinPool$WorkQueue";
    private static final String SOCKET_CHANNEL = "sun/nio/ch/SocketChannelImpl";
    private static final String NIO_SOCKET_IMPL = "sun/nio/ch/NioSocketImpl";

    private static final String DESC_RUN_CONTINUATION = "()V";
    private static final String DESC_TOP_LEVEL_EXEC =
            "(Ljava/util/concurrent/ForkJoinTask;Ljava/util/concurrent/ForkJoinPool$WorkQueue;)V";
    private static final String DESC_SOCKET_READ = "(Ljava/nio/ByteBuffer;)I";
    private static final String DESC_NIO_SOCKET_READ = "([BII)I";
    private static final String DESC_NIO_SOCKET_WRITE = "([BII)I";
    private static final String DESC_PARK = "()V";
    private static final String DESC_PARK_NANOS = "(J)V";
    private static final String DESC_UNPARK = "()V";

    static final String SOCKET_FD_FIELD = "fdVal";
    static final String SOCKET_FD_FIELD_DESC = "I";
    static final String SOCKET_CHANNEL_OWNER = SOCKET_CHANNEL;

    static final String NIO_SOCKET_OWNER = NIO_SOCKET_IMPL;
    static final String NIO_FD_FIELD = "fd";
    static final String NIO_FD_FIELD_DESC = "Ljava/io/FileDescriptor;";
    static final String NIO_FDVAL_METHOD = "fdVal";
    static final String NIO_FDVAL_DESC = "(Ljava/io/FileDescriptor;)I";

    private VTPhaseInstrumentation() {
    }

    public static boolean isTargetClass(String classNameInternal) {
        return VIRTUAL_THREAD.equals(classNameInternal)
                || FJP_WORK_QUEUE.equals(classNameInternal)
                || SOCKET_CHANNEL.equals(classNameInternal)
                || NIO_SOCKET_IMPL.equals(classNameInternal);
    }

    public static String[] targetClassInternalNames() {
        return new String[] { VIRTUAL_THREAD, FJP_WORK_QUEUE, SOCKET_CHANNEL, NIO_SOCKET_IMPL };
    }

    public static int getInstrumentationType(String owner, String name, String desc) {
        if (VIRTUAL_THREAD.equals(owner)
                && "runContinuation".equals(name)
                && DESC_RUN_CONTINUATION.equals(desc)) {
            return TYPE_RUN;
        }
        if (FJP_WORK_QUEUE.equals(owner)
                && "topLevelExec".equals(name)
                && DESC_TOP_LEVEL_EXEC.equals(desc)) {
            return TYPE_DEQUEUE;
        }
        if (SOCKET_CHANNEL.equals(owner)
                && "read".equals(name)
                && DESC_SOCKET_READ.equals(desc)) {
            return TYPE_SOCKET_READ;
        }
        if (NIO_SOCKET_IMPL.equals(owner)
                && "read".equals(name)
                && DESC_NIO_SOCKET_READ.equals(desc)) {
            return TYPE_NIO_SOCKET_READ;
        }
        if (NIO_SOCKET_IMPL.equals(owner)
                && "implWrite".equals(name)
                && DESC_NIO_SOCKET_WRITE.equals(desc)) {
            return TYPE_NIO_SOCKET_WRITE;
        }
        if (VIRTUAL_THREAD.equals(owner)
                && "park".equals(name)
                && DESC_PARK.equals(desc)) {
            return TYPE_PARK;
        }
        if (VIRTUAL_THREAD.equals(owner)
                && "parkNanos".equals(name)
                && DESC_PARK_NANOS.equals(desc)) {
            return TYPE_PARK_NANOS;
        }
        if (VIRTUAL_THREAD.equals(owner)
                && "unpark".equals(name)
                && DESC_UNPARK.equals(desc)) {
            return TYPE_UNPARK;
        }
        return TYPE_NONE;
    }
}
