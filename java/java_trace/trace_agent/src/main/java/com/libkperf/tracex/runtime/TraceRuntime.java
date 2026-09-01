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

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public final class TraceRuntime {

    private static final int INITIAL_METHOD_CAPACITY = 256;
    private static final Map<String, Integer> METHOD_IDS = new HashMap<String, Integer>();
    private static volatile MethodMetadata[] methodTable = new MethodMetadata[INITIAL_METHOD_CAPACITY];

    private static volatile boolean enabled;
    private static volatile SharedEventSink sink;

    private static final ThreadLocal<ThreadState> THREAD_STATE = new ThreadLocal<ThreadState>() {
        @Override
        protected ThreadState initialValue() {
            return new ThreadState();
        }
    };

    private TraceRuntime() {
    }

    public static int registerMethod(String module, String func) {
        long addr = fnv1a64(module + "!" + func) & 0x0000FFFFFFFFFFFFL;
        return registerMethod(addr, module, func);
    }

    public static synchronized int registerMethod(long addr, String module, String func) {
        String key = module + '\u0000' + func;
        Integer existing = METHOD_IDS.get(key);
        if (existing != null) {
            return existing.intValue();
        }

        int id = METHOD_IDS.size();
        if (id >= SharedEventSink.METHOD_CAPACITY) {
            throw new IllegalStateException("trace method dictionary is full: " + id);
        }
        MethodMetadata[] table = methodTable;
        if (id >= table.length) {
            table = Arrays.copyOf(table, table.length << 1);
        }
        MethodMetadata metadata = new MethodMetadata(id, addr, SharedEventSink.utf8(module), SharedEventSink.utf8(func));
        table[id] = metadata;
        methodTable = table;
        METHOD_IDS.put(key, Integer.valueOf(id));

        SharedEventSink current = sink;
        if (current != null && !current.registerMethod(metadata.id, metadata.addr, metadata.module, metadata.func)) {
            throw new IllegalStateException("failed to publish trace method metadata: " + id);
        }
        return id;
    }

    public static synchronized void reconfigure(String shmPath, int slotCount) throws Exception {
        enabled = false;

        SharedEventSink old = sink;
        sink = null;

        if (old != null) {
            try {
                old.close();
            } catch (Throwable t) {
                TraceLog.warn("[trace-java-runtime] close old sink failed: " + t, t);
            }
        }

        SharedEventSink next = new SharedEventSink(shmPath, slotCount);
        MethodMetadata[] table = methodTable;
        for (MethodMetadata metadata : table) {
            if (metadata == null) {
                continue;
            }
            if (!next.registerMethod(metadata.id, metadata.addr, metadata.module, metadata.func)) {
                next.close();
                throw new IllegalStateException("failed to initialize trace method dictionary: " + metadata.id);
            }
        }
        sink = next;

        TraceLog.info("[trace-java-runtime] reconfigured" + ", shmPath=" + shmPath +
                            ", slotCount=" + slotCount + ", active=" + next.isActive());

        enabled = true;
    }

    public static synchronized void stop() {
        enabled = false;

        SharedEventSink old = sink;
        sink = null;

        if (old != null) {
            try {
                old.close();
            } catch (Throwable t) {
                TraceLog.warn("[trace-java-runtime] stop close sink failed: " + t, t);
            }
        }

        TraceLog.info("[trace-java-runtime] disabled");
    }

    public static void setEnabled(boolean value) {
        enabled = value;
    }

    public static Context enter(String classNameInternal, String methodName, String descriptor) {
        try {
            String module = classNameInternal.replace('/', '.');
            String func = methodName + descriptor;
            return enter(registerMethod(module, func));
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable ignored) {
            return Context.SKIPPED;
        }
    }

    public static Context enter(long addr, String module, String func) {
        try {
            return enter(registerMethod(addr, module, func));
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable ignored) {
            return Context.SKIPPED;
        }
    }

    public static Context enter(int methodId) {
        try {
            MethodMetadata[] table = methodTable;
            if (!enabled || methodId < 0 || methodId >= table.length) {
                return Context.SKIPPED;
            }
            MethodMetadata method = table[methodId];
            if (method == null) {
                return Context.SKIPPED;
            }
            ThreadState state = THREAD_STATE.get();
            if (state.runtimeDepth > 0) {
                return Context.SKIPPED;
            }
            SharedEventSink s = sink;
            if (s == null) {
                return Context.SKIPPED;
            }

            state.runtimeDepth++;
            try {
                int depth = state.callDepth;
                long ts = NativeThreadInfo.currentTimeNanosSafe();
                int tid = state.tid();
                int commId = state.commId(s, tid);
                int cpu = NativeThreadInfo.currentCpuSafe();
                long gPtr = 0L;
                if (!s.record(method.addr, method.id, commId, tid, cpu, ts, gPtr, 0)) {
                    return Context.SKIPPED;
                }
                state.callDepth = depth + 1;
                return new Context(state, s, method, commId, tid, cpu, depth, false);
            } finally {
                state.runtimeDepth--;
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
            ThreadState state = context.state;
            state.callDepth = Math.max(0, state.callDepth - 1);
            if (!enabled || state.runtimeDepth > 0) {
                return;
            }
            SharedEventSink s = context.sink;
            if (s == null) {
                return;
            }
            state.runtimeDepth++;
            try {
                long ts = NativeThreadInfo.currentTimeNanosSafe();
                int cpu = NativeThreadInfo.currentCpuSafe();
                MethodMetadata method = context.method;
                s.record(method.addr, method.id, context.commId, context.tid, cpu, ts, context.gPtr, 1);
            } finally {
                state.runtimeDepth--;
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

    private static final class MethodMetadata {
        final int id;
        final long addr;
        final byte[] module;
        final byte[] func;

        MethodMetadata(int id, long addr, byte[] module, byte[] func) {
            this.id = id;
            this.addr = addr;
            this.module = module;
            this.func = func;
        }
    }

    private static final class ThreadState {
        int runtimeDepth;
        int callDepth;
        private int cachedTid;
        private byte[] cachedComm;
        private SharedEventSink registeredSink;
        private int registeredCommId = -1;

        int tid() {
            if (cachedTid == 0) {
                cachedTid = NativeThreadInfo.currentTidSafe();
            }
            return cachedTid;
        }

        byte[] comm() {
            if (cachedComm == null) {
                cachedComm = SharedEventSink.utf8(currentThreadName());
            }
            return cachedComm;
        }

        int commId(SharedEventSink sink, int tid) {
            if (registeredSink != sink) {
                int id = sink.registerThread(tid, comm());
                if (id < 0) {
                    return -1;
                }
                registeredCommId = id;
                registeredSink = sink;
            }
            return registeredCommId;
        }
    }

    public static final class Context {
        static final Context SKIPPED = new Context(null, null, null, -1, 0, -1, 0, true);

        private final ThreadState state;
        final SharedEventSink sink;
        private final MethodMetadata method;
        final int commId;
        final long gPtr;
        final int tid;
        final int cpu;
        final int depth;
        final boolean skipped;

        private Context(ThreadState state, SharedEventSink sink, MethodMetadata method,
                int commId, int tid, int cpu, int depth, boolean skipped) {
            this.state = state;
            this.sink = sink;
            this.method = method;
            this.commId = commId;
            this.gPtr = 0L;
            this.tid = tid;
            this.cpu = cpu;
            this.depth = depth;
            this.skipped = skipped;
        }
    }
}
