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
 * Description: Virtual thread phase collector: records lifecycle phases (run, dequeue,
 * park, unpark, socket io) into the shared memory ring buffer via SharedEventSink.
 ******************************************************************************/
package com.libkperf.tracex.runtime;

public final class VTPhaseCollector {

    private static final String TAG = "[trace-vt-phase] ";

    public static final int PHASE_RUN = 0;
    public static final int PHASE_DEQUEUE = 1;
    public static final int PHASE_UNPARK_RETURN = 3;
    public static final int PHASE_SOCKET_READ = 5;
    public static final int PHASE_SOCKET_WRITE = 6;
    public static final int PHASE_PARK_ENTER = 7;
    public static final int PHASE_PARK_RETURN = 8;

    private static volatile boolean enabled;

    private VTPhaseCollector() {
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
        emit(PHASE_RUN, vtId, carrierId, NativeThreadInfo.currentTidSafe());
    }

    public static void onDequeue(long carrierId) {
        emit(PHASE_DEQUEUE, 0L, carrierId, NativeThreadInfo.currentTidSafe());
    }

    public static void onUnparkReturn(long vtId) {
        emit(PHASE_UNPARK_RETURN, vtId, 0L, NativeThreadInfo.currentTidSafe());
    }

    public static void onParkEnter(long vtId, long carrierId, String carrierName) {
        int osTid = NativeThreadInfo.currentTidSafe();
        registerCarrier(osTid, carrierName);
        emit(PHASE_PARK_ENTER, vtId, carrierId, osTid);
    }

    public static void onParkReturn(long vtId, long carrierId, String carrierName) {
        int osTid = NativeThreadInfo.currentTidSafe();
        registerCarrier(osTid, carrierName);
        emit(PHASE_PARK_RETURN, vtId, carrierId, osTid);
    }

    private static void registerCarrier(int tid, String name) {
        if (!enabled || tid <= 0) {
            return;
        }
        try {
            SharedEventSink sink = TraceRuntime.sink();
            if (sink != null && sink.isActive()) {
                sink.registerCarrierThread(tid, SharedEventSink.utf8(name));
            }
        } catch (ThreadDeath t) {
            throw t;
        } catch (Throwable t) {
            TraceLog.debug(TAG + "registerCarrier failed: tid=" + tid + ", ex=" + t, t);
        }
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
            long ts = NativeThreadInfo.currentTimeNanosSafe();
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

    private static void emit(int phase, long vtId, long carrierId, int osTid) {
        if (!enabled) {
            TraceLog.debug(TAG + "emit skipped (disabled): phase=" + phaseName(phase)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId);
            return;
        }
        try {
            SharedEventSink sink = TraceRuntime.sink();
            if (sink == null || !sink.isActive()) {
                TraceLog.debug(TAG + "emit skipped (sink unavailable): phase=" + phaseName(phase)
                                   + ", vtId=" + vtId + ", carrierId=" + carrierId
                                   + ", sink=" + (sink == null ? "null" : "inactive"));
                return;
            }
            long ts = System.nanoTime();
            int cpu = NativeThreadInfo.currentCpuSafe();
            boolean recorded = sink.recordVtPhase(carrierId, vtId, phase, cpu, ts, osTid);
            TraceLog.debug(TAG + "emit: phase=" + phaseName(phase)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId
                               + ", cpu=" + cpu + ", ts=" + ts + ", recorded=" + recorded);
        } catch (ThreadDeath t) {
            throw t;
        } catch (Throwable t) {
            TraceLog.debug(TAG + "emit failed: phase=" + phaseName(phase)
                               + ", vtId=" + vtId + ", carrierId=" + carrierId
                               + ", ex=" + t, t);
        }
    }

    private static String phaseName(int phase) {
        switch (phase) {
            case PHASE_RUN:
                return "RUN";
            case PHASE_DEQUEUE:
                return "DEQUEUE";
            case PHASE_UNPARK_RETURN:
                return "UNPARK_RETURN";
            case PHASE_SOCKET_READ:
                return "SOCKET_READ";
            case PHASE_SOCKET_WRITE:
                return "SOCKET_WRITE";
            case PHASE_PARK_ENTER:
                return "PARK_ENTER";
            case PHASE_PARK_RETURN:
                return "PARK_RETURN";
            default:
                return "UNKNOWN(" + phase + ")";
        }
    }
}
