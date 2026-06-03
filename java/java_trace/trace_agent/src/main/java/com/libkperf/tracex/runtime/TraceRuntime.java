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

import java.util.concurrent.atomic.AtomicBoolean;

public final class TraceRuntime {

    private static final AtomicBoolean ENABLED = new AtomicBoolean(false);

    private static volatile SharedEventSink sink;

    private static final ThreadLocal<Integer> REENTRANT_DEPTH = new ThreadLocal<Integer>() {
        @Override
        protected Integer initialValue() {
            return 0;
        }
    };

    private TraceRuntime() {
    }

    public static synchronized void reconfigure(String shmPath, int slotCount) throws Exception {
        reconfigure(shmPath, slotCount, 0L);
    }

    public static synchronized void reconfigure(String shmPath,
                                                int slotCount,
                                                long durationMs) throws Exception {
        ENABLED.set(false);

        SharedEventSink old = sink;
        sink = null;

        if (old != null) {
            try {
                old.close();
            } catch (Throwable t) {
                System.err.println("[trace-runtime] close old sink failed: " + t);
            }
        }

        SharedEventSink next = new SharedEventSink(shmPath, slotCount);
        sink = next;

        System.err.println("[trace-runtime] reconfigured"
                + ", shmPath=" + shmPath
                + ", slotCount=" + slotCount
                + ", durationMs=" + durationMs
                + ", active=" + next.isActive());

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
                System.err.println("[trace-runtime] stop close sink failed: " + t);
            }
        }

        System.err.println("[trace-runtime] disabled");
    }

    public static void setEnabled(boolean enabled) {
        ENABLED.set(enabled);
    }

    public static Context enter(String classNameInternal, String methodName, String descriptor) {
        try {
            if (!ENABLED.get()) {
                return Context.SKIPPED;
            }

            int depth = REENTRANT_DEPTH.get();
            if (depth > 0) {
                return Context.SKIPPED;
            }

            SharedEventSink s = sink;
            if (s == null || !s.isActive()) {
                return Context.SKIPPED;
            }

            REENTRANT_DEPTH.set(depth + 1);

            String module = classNameInternal.replace('/', '.');
            String func = methodName + descriptor;
            long addr = fnv1a64(module + "!" + func) & 0x0000FFFFFFFFFFFFL;
            long ts = System.nanoTime();
            String comm = currentThreadName();
            int tid = NativeThreadInfo.currentTidSafe();
            int cpu = NativeThreadInfo.currentCpuSafe();
            long gPtr = 0L;

            Context context = new Context(s, addr, gPtr, module, func, comm, tid, cpu, false);
            s.record(addr, comm, tid, cpu, ts, gPtr, module, func, 0);

            return context;
        } catch (Throwable ignored) {
            return Context.SKIPPED;
        }
    }

    public static void exit(Context context) {
        try {
            REENTRANT_DEPTH.set(Math.max(0, REENTRANT_DEPTH.get() - 1));

            if (context == null || context.skipped) {
                return;
            }

            if (!ENABLED.get()) {
                return;
            }

            SharedEventSink s = context.sink;
            if (s == null || !s.isActive()) {
                return;
            }

            long ts = System.nanoTime();
            s.record(
                    context.addr,
                    context.comm,
                    context.tid,
                    context.cpu,
                    ts,
                    context.gPtr,
                    context.module,
                    context.func,
                    1
            );
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
        static final Context SKIPPED =
                new Context(null, 0L, 0L, "", "", "", 0, -1, true);

        final SharedEventSink sink;
        final long addr;
        final long gPtr;
        final String module;
        final String func;
        final String comm;
        final int tid;
        final int cpu;
        final boolean skipped;

        Context(SharedEventSink sink,
                long addr,
                long gPtr,
                String module,
                String func,
                String comm,
                int tid,
                int cpu,
                boolean skipped) {
            this.sink = sink;
            this.addr = addr;
            this.gPtr = gPtr;
            this.module = module;
            this.func = func;
            this.comm = comm;
            this.tid = tid;
            this.cpu = cpu;
            this.skipped = skipped;
        }
    }
}
