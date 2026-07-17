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
 * Description: Global runtime state: manages enable/disable flags and coordinates agent components
 ******************************************************************************/
package com.libkperf.tracex.runtime;

import com.libkperf.tracex.agent.TraceLog;

import java.util.concurrent.atomic.AtomicBoolean;

public final class TraceRuntime {

    private static final AtomicBoolean ENABLED = new AtomicBoolean(false);

    private static volatile SharedEventSink sink;

    // prevent self-recursion at runtime
    private static final ThreadLocal<Integer> RUNTIME_DEPTH = new ThreadLocal<Integer>() {
        @Override
        protected Integer initialValue() {
            return 0;
        }
    };

    // method call depth
    private static final ThreadLocal<Integer> CALL_DEPTH = new ThreadLocal<Integer>() {
        @Override
        protected Integer initialValue() {
            return 0;
        }
    };

    private TraceRuntime() {
    }

    public static synchronized void reconfigure(String shmPath, int slotCount) throws Exception {
        ENABLED.set(false);

        SharedEventSink old = sink;
        sink = null;

        if (old != null) {
            try {
                old.close();
            } catch (Throwable t) {
                TraceLog.warn("[trace-runtime] close old sink failed: " + t, t);
            }
        }

        SharedEventSink next = new SharedEventSink(shmPath, slotCount);
        sink = next;

        TraceLog.info("[trace-runtime] reconfigured" + ", shmPath=" + shmPath +
                            ", slotCount=" + slotCount + ", active=" + next.isActive());

        ENABLED.set(true);
    }

    public static synchronized void stop() {
        ENABLED.set(false);

        SharedEventSink old = sink;
        sink = null;

        if (old != null) {
            try {
                old.close();
            } catch (Throwable t) {
                TraceLog.warn("[trace-runtime] stop close sink failed: " + t, t);
            }
        }

        TraceLog.info("[trace-runtime] disabled");
    }

    public static void setEnabled(boolean enabled) {
        ENABLED.set(enabled);
    }

    public static Context enter(String classNameInternal, String methodName, String descriptor) {
        try {
            if (!ENABLED.get()) {
                return Context.SKIPPED;
            }
            int runtimeDepth = RUNTIME_DEPTH.get();
            if (runtimeDepth > 0) {
                return Context.SKIPPED;
            }
            SharedEventSink s = sink;
            if (s == null) {
                return Context.SKIPPED;
            }

            RUNTIME_DEPTH.set(runtimeDepth + 1);
            try {
                int depth = CALL_DEPTH.get();
                String module = classNameInternal.replace('/', '.');
                String func = methodName + descriptor;
                long addr = fnv1a64(module + "!" + func) & 0x0000FFFFFFFFFFFFL;
                long ts = NativeThreadInfo.currentTimeNanosSafe();
                String comm = currentThreadName();
                int tid = NativeThreadInfo.currentTidSafe();
                int cpu = NativeThreadInfo.currentCpuSafe();
                long gPtr = 0L;
                if (!s.record(addr, comm, tid, cpu, ts, gPtr, module, func, 0)) {
                    return Context.SKIPPED;
                }
                CALL_DEPTH.set(depth + 1);
                return new Context(s, addr, gPtr, module, func, comm, tid, cpu, depth, false);
            } finally {
                RUNTIME_DEPTH.set(runtimeDepth);
            }
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable ignored) {
            return Context.SKIPPED;
        }
    }

    public static void exit(Context context) {
        try {
            if (context == null || context.skipped) {
                return;
            }
            CALL_DEPTH.set(Math.max(0, CALL_DEPTH.get() - 1));
            if (!ENABLED.get()) {
                return;
            }
            int runtimeDepth = RUNTIME_DEPTH.get();
            if (runtimeDepth > 0) {
                return;
            }
            SharedEventSink s = context.sink;
            if (s == null) {
                return;
            }
            RUNTIME_DEPTH.set(runtimeDepth + 1);
            try {
                long ts = NativeThreadInfo.currentTimeNanosSafe();
                int cpu = NativeThreadInfo.currentCpuSafe();
                s.record(context.addr, context.comm, context.tid, cpu, ts,
                         context.gPtr, context.module, context.func, 1);
            } finally {
                RUNTIME_DEPTH.set(runtimeDepth);
            }
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable ignored) {
        }
    }

    private static String currentThreadName() {
        String n = Thread.currentThread().getName();
        return (n == null || n.isEmpty()) ? "unknown" : n;
    }

    private static long fnv1a64(String value) {
        long h = 0xcbf29ce484222325L;

        for (int i = 0; i < value.length(); i++) {
            h ^= value.charAt(i);
            h *= 0x100000001b3L;
        }

        return h;
    }

    public static final class Context {
        static final Context SKIPPED = new Context(null, 0L, 0L, "", "", "", 0, -1, 0, true);

        final SharedEventSink sink;
        final long addr;
        final long gPtr;
        final String module;
        final String func;
        final String comm;
        final int tid;
        final int cpu;
        final int depth;
        final boolean skipped;

        Context(SharedEventSink sink, long addr, long gPtr, String module, String func,
                String comm, int tid, int cpu, int depth, boolean skipped) {
            this.sink = sink;
            this.addr = addr;
            this.gPtr = gPtr;
            this.module = module;
            this.func = func;
            this.comm = comm;
            this.tid = tid;
            this.cpu = cpu;
            this.depth = depth;
            this.skipped = skipped;
        }
    }
}
