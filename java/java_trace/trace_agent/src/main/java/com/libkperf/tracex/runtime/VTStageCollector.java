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
 * Description: Virtual thread stage collector: records lifecycle stages (run, dequeue,
 * park, unpark, socket io) into the shared memory ring buffer via SharedEventSink.
 ******************************************************************************/
package com.libkperf.tracex.runtime;

public final class VTStageCollector {

    private static final String TAG = "[trace-vt-stage] ";

    public static final int STAGE_RUN = 0;
    public static final int STAGE_DEQUEUE = 1;
    public static final int STAGE_UNPARK_RETURN = 3;
    public static final int STAGE_SOCKET_READ = 5;
    public static final int STAGE_SOCKET_WRITE = 6;
    public static final int STAGE_PARK_ENTER = 7;
    public static final int STAGE_PARK_RETURN = 8;

    private static volatile boolean enabled;

    private VTStageCollector() {
    }

    public static void setEnabled(boolean value) {
        boolean prev = enabled;
        enabled = value;
        if (prev != value) {
            TraceLog.info(TAG + "setEnabled: " + prev + " -> " + value);
        }
    }

    public static boolean isEnabled() {
        return enabled;
    }

    public static void onRun(long vtId, long carrierId) {
        emit(STAGE_RUN, vtId, carrierId, NativeThreadInfo.currentTidSafe(), -1);
    }

    public static void onDequeue(long carrierId) {
        emit(STAGE_DEQUEUE, 0L, carrierId, NativeThreadInfo.currentTidSafe(), -1);
    }

    public static void onUnparkReturn(long vtId) {
        emit(STAGE_UNPARK_RETURN, vtId, 0L, NativeThreadInfo.currentTidSafe(), -1);
    }

    public static void onParkEnter(long vtId, long carrierId, String carrierName) {
        int osTid = NativeThreadInfo.currentTidSafe();
        SharedEventSink sink = enabled ? TraceRuntime.sink() : null;
        int commId = sink != null && sink.isActive() && osTid > 0
                ? sink.registerThread(osTid, SharedEventSink.utf8(carrierName)) : -1;
        emit(STAGE_PARK_ENTER, vtId, carrierId, osTid, commId);
    }

    public static void onParkReturn(long vtId, long carrierId, String carrierName) {
        int osTid = NativeThreadInfo.currentTidSafe();
        SharedEventSink sink = enabled ? TraceRuntime.sink() : null;
        int commId = sink != null && sink.isActive() && osTid > 0
                ? sink.registerThread(osTid, SharedEventSink.utf8(carrierName)) : -1;
        emit(STAGE_PARK_RETURN, vtId, carrierId, osTid, commId);
    }

    public static void onSocketRead(long vtId, int fd, int bytes) {
        if (!enabled) {
            return;
        }
        try {
            SharedEventSink sink = TraceRuntime.sink();
            if (sink == null || !sink.isActive()) {
                TraceLog.debug(TAG + "onSocketRead skipped (sink unavailable): vtId=" + vtId
                                   + ", fd=" + fd + ", bytes=" + bytes);
                return;
            }
            long ts = System.nanoTime();
            int cpu = NativeThreadInfo.currentCpuSafe();
            boolean recorded = sink.recordVtSocketRead(vtId, fd, bytes, cpu, ts,
                                                       NativeThreadInfo.currentTidSafe());
            TraceLog.debug(TAG + "onSocketRead: vtId=" + vtId + ", fd=" + fd
                               + ", bytes=" + bytes + ", cpu=" + cpu + ", ts=" + ts
                               + ", recorded=" + recorded);
        } catch (ThreadDeath t) {
            throw t;
        } catch (Throwable t) {
            TraceLog.debug(TAG + "onSocketRead failed: vtId=" + vtId + ", fd=" + fd
                               + ", bytes=" + bytes + ", ex=" + t, t);
        }
    }

    public static void onSocketWrite(long vtId, int fd, int bytes) {
        if (!enabled) {
            return;
        }
        try {
            SharedEventSink sink = TraceRuntime.sink();
            if (sink == null || !sink.isActive()) {
                TraceLog.debug(TAG + "onSocketWrite skipped (sink unavailable): vtId=" + vtId
                                   + ", fd=" + fd + ", bytes=" + bytes);
                return;
            }
            long ts = System.nanoTime();
            int cpu = NativeThreadInfo.currentCpuSafe();
            boolean recorded = sink.recordVtSocketWrite(vtId, fd, bytes, cpu, ts,
                                                        NativeThreadInfo.currentTidSafe());
            TraceLog.debug(TAG + "onSocketWrite: vtId=" + vtId + ", fd=" + fd
                               + ", bytes=" + bytes + ", cpu=" + cpu + ", ts=" + ts
                               + ", recorded=" + recorded);
        } catch (ThreadDeath t) {
            throw t;
        } catch (Throwable t) {
            TraceLog.debug(TAG + "onSocketWrite failed: vtId=" + vtId + ", fd=" + fd
                               + ", bytes=" + bytes + ", ex=" + t, t);
        }
    }

    private static void emit(int stage, long vtId, long carrierId, int osTid, int commId) {
        if (!enabled) {
            TraceLog.debug(TAG + "emit skipped (disabled): stage=" + stageName(stage)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId);
            return;
        }
        try {
            SharedEventSink sink = TraceRuntime.sink();
            if (sink == null || !sink.isActive()) {
                TraceLog.debug(TAG + "emit skipped (sink unavailable): stage=" + stageName(stage)
                                   + ", vtId=" + vtId + ", carrierId=" + carrierId
                                   + ", sink=" + (sink == null ? "null" : "inactive"));
                return;
            }
            long ts = System.nanoTime();
            int cpu = NativeThreadInfo.currentCpuSafe();
            boolean recorded = sink.recordVTStage(carrierId, vtId, stage, cpu, ts, osTid, commId);
            TraceLog.debug(TAG + "emit: stage=" + stageName(stage)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId
                               + ", cpu=" + cpu + ", ts=" + ts + ", recorded=" + recorded);
        } catch (ThreadDeath t) {
            throw t;
        } catch (Throwable t) {
            TraceLog.debug(TAG + "emit failed: stage=" + stageName(stage)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId
                               + ", ex=" + t, t);
        }
    }

    private static String stageName(int stage) {
        switch (stage) {
            case STAGE_RUN:
                return "RUN";
            case STAGE_DEQUEUE:
                return "DEQUEUE";
            case STAGE_UNPARK_RETURN:
                return "UNPARK_RETURN";
            case STAGE_SOCKET_READ:
                return "SOCKET_READ";
            case STAGE_SOCKET_WRITE:
                return "SOCKET_WRITE";
            case STAGE_PARK_ENTER:
                return "PARK_ENTER";
            case STAGE_PARK_RETURN:
                return "PARK_RETURN";
            default:
                return "UNKNOWN(" + stage + ")";
        }
    }
}
