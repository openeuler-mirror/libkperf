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

import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public final class TraceRuntime {

    private static final int INITIAL_METHOD_CAPACITY = 256;
    private static final byte[] UNKNOWN_CARRIER_COMM = SharedEventSink.utf8("carrier");
    private static final Map<String, Integer> METHOD_IDS = new HashMap<String, Integer>();
    private static volatile MethodMetadata[] methodTable = new MethodMetadata[INITIAL_METHOD_CAPACITY];
    private static final int VTHREAD_CARRIER_METHOD_ID = registerMethod("[java-vthread]", "carrier");

    private static volatile boolean enabled;
    private static volatile SharedEventSink sink;
    private static volatile boolean virtualThreadCarrierHooksEnabled;
    private static volatile boolean virtualThreadFallbackLogged;

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
        virtualThreadCarrierHooksEnabled = false;
        virtualThreadFallbackLogged = false;

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
                            ", slotCount=" + slotCount + ", active=" + next.isActive() +
                            ", virtualThreads=" + VirtualThreadSupport.isSupported() +
                            ", carrierTid=" + NativeThreadInfo.isNativeTidAvailable());

        enabled = true;
    }

    public static synchronized void stop() {
        enabled = false;
        virtualThreadCarrierHooksEnabled = false;

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

    /**
     * Resolves callback dependencies before java/lang/VirtualThread is retransformed.
     * This must run while the agent transformer is not installed.
     */
    public static void preloadVirtualThreadHelpers() {
        VirtualThreadSupport.isSupported();
        SharedEventSink.preloadHelpers();
        MethodMetadata ignored = methodTable[VTHREAD_CARRIER_METHOD_ID];
        if (ignored != null) {
            ignored.module.hashCode();
        }
    }

    public static void configureVirtualThreadCarrierHooks(boolean installed) {
        virtualThreadCarrierHooksEnabled = installed;
        if (installed) {
            TraceLog.info("[trace-java-runtime] virtual-thread carrier call-site hooks enabled");
            return;
        }
        if (VirtualThreadSupport.isSupported() && !virtualThreadFallbackLogged) {
            virtualThreadFallbackLogged = true;
            TraceLog.warn("[trace-java-runtime] virtual-thread carrier hooks unavailable"
                    + ", using degraded METHOD_ENTER mount inference", null);
        }
    }

    public static void virtualThreadBeforeMount(Thread virtualThread) {
        recordCarrierVirtualThreadTransition(virtualThread, SharedEventSink.EVENT_VTHREAD_MOUNT);
    }

    public static void virtualThreadBeforeUnmount(Thread virtualThread) {
        recordCarrierVirtualThreadTransition(virtualThread, SharedEventSink.EVENT_VTHREAD_UNMOUNT);
    }

    private static void recordCarrierVirtualThreadTransition(Thread virtualThread, int eventType) {
        try {
            if (!enabled || virtualThread == null) {
                return;
            }
            SharedEventSink s = sink;
            if (s == null) {
                return;
            }
            // A callback can only be reached from successfully transformed call-site bytecode.
            virtualThreadCarrierHooksEnabled = true;
            MethodMetadata method = methodTable[VTHREAD_CARRIER_METHOD_ID];
            if (method == null) {
                return;
            }
            int tid = NativeThreadInfo.currentTidSafe();
            // Before mount and before unmount Thread.currentThread() is the carrier.
            // The virtual-thread identity is carried separately in contextId/gPtr.
            byte[] comm = SharedEventSink.utf8(currentThreadName());
            int commId = s.registerCarrierThread(tid, comm);
            long timestamp = NativeThreadInfo.currentTimeNanosSafe();
            int cpu = NativeThreadInfo.currentCpuSafe();
            long contextId = VirtualThreadSupport.contextId(virtualThread);
            s.record(method.addr, method.id, commId, tid, cpu, timestamp, contextId, eventType);
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable ignored) {
        }
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
                int tid = state.tid();
                int commId = state.commId(s, tid);
                if (!virtualThreadCarrierHooksEnabled && state.needsMountEvent(s, tid)) {
                    state.updateMountState(s, tid, SharedEventSink.EVENT_VTHREAD_MOUNT_DEGRADED,
                            recordVirtualThreadTransition(state, s, tid,
                                    SharedEventSink.EVENT_VTHREAD_MOUNT_DEGRADED));
                }
                long ts = NativeThreadInfo.currentTimeNanosSafe();
                int cpu = NativeThreadInfo.currentCpuSafe();
                long gPtr = state.contextId();
                if (!s.record(method.addr, method.id, commId, tid, cpu, ts, gPtr, SharedEventSink.EVENT_METHOD_ENTER)) {
                    return Context.SKIPPED;
                }
                state.callDepth = depth + 1;
                return new Context(state, s, method, gPtr, false);
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
                int tid = state.tid();
                int commId = state.commId(s, tid);
                int cpu = NativeThreadInfo.currentCpuSafe();
                MethodMetadata method = context.method;
                s.record(method.addr, method.id, commId, tid, cpu, ts, context.gPtr,
                        SharedEventSink.EVENT_METHOD_EXIT);
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

    private static boolean recordVirtualThreadTransition(ThreadState state, SharedEventSink s,
            int tid, int eventType) {
        MethodMetadata method = methodTable[VTHREAD_CARRIER_METHOD_ID];
        if (method == null) {
            return false;
        }
        long timestamp = NativeThreadInfo.currentTimeNanosSafe();
        int commId = state.commId(s, tid);
        int cpu = NativeThreadInfo.currentCpuSafe();
        return s.record(method.addr, method.id, commId, tid, cpu, timestamp, state.contextId(), eventType);
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
        private final boolean virtualThread;
        private final long contextId;
        private byte[] cachedComm;
        private SharedEventSink registeredSink;
        private int registeredTid;
        private SharedEventSink mountedSink;
        private int mountedTid;
        private int registeredCommId = -1;

        ThreadState() {
            Thread thread = Thread.currentThread();
            virtualThread = VirtualThreadSupport.isVirtual(thread);
            // platform thread: 0
            // virtual thread: high-bit tagged + Java thread ID
            contextId = virtualThread ? VirtualThreadSupport.contextId(thread) : 0L;
        }

        int tid() {
            if (virtualThread) {
                return NativeThreadInfo.currentTidSafe();
            }
            if (cachedTid == 0) {
                cachedTid = NativeThreadInfo.currentTidSafe();
            }
            return cachedTid;
        }

        long contextId() {
            return virtualThread && !virtualThreadCarrierHooksEnabled
                    ? VirtualThreadSupport.markDegraded(contextId)
                    : contextId;
        }

        boolean needsMountEvent(SharedEventSink sink, int tid) {
            return virtualThread && (mountedSink != sink || mountedTid != tid);
        }

        void updateMountState(SharedEventSink sink, int tid, int eventType, boolean recorded) {
            if (eventType == SharedEventSink.EVENT_VTHREAD_MOUNT
                    || eventType == SharedEventSink.EVENT_VTHREAD_MOUNT_DEGRADED) {
                if (recorded) {
                    mountedSink = sink;
                    mountedTid = tid;
                }
                return;
            }
            if (eventType == SharedEventSink.EVENT_VTHREAD_UNMOUNT) {
                mountedSink = null;
                mountedTid = 0;
            }
        }

        byte[] comm() {
            if (cachedComm == null) {
                cachedComm = SharedEventSink.utf8(currentThreadName());
            }
            return cachedComm;
        }

        int commId(SharedEventSink sink, int tid) {
            if (registeredSink != sink || registeredTid != tid) {
                int id = virtualThread
                        ? sink.registerCarrierThread(tid, UNKNOWN_CARRIER_COMM)
                        : sink.registerThread(tid, comm());
                if (id < 0) {
                    return -1;
                }
                registeredCommId = id;
                registeredSink = sink;
                registeredTid = tid;
            }
            return registeredCommId;
        }
    }

    private static final class VirtualThreadSupport {
        // Native user-space pointers do not use the high half of the 64-bit address space.
        // Marking Java virtual-thread IDs keeps them distinct from the Go g pointer.
        private static final long CONTEXT_ID_FLAG = Long.MIN_VALUE;
        private static final long CONTEXT_DEGRADED_FLAG = 1L << 62;
        private static final Method IS_VIRTUAL = findIsVirtual();

        private VirtualThreadSupport() {
        }

        static boolean isSupported() {
            return IS_VIRTUAL != null;
        }

        static boolean isVirtual(Thread thread) {
            if (IS_VIRTUAL == null) {
                return false;
            }
            try {
                return Boolean.TRUE.equals(IS_VIRTUAL.invoke(thread));
            } catch (ReflectiveOperationException | RuntimeException ignored) {
                return false;
            }
        }

        static long contextId(Thread thread) {
            long id = thread.getId() & Long.MAX_VALUE;
            return CONTEXT_ID_FLAG | (id == 0L ? 1L : id);
        }

        static long markDegraded(long contextId) {
            return contextId | CONTEXT_DEGRADED_FLAG;
        }

        private static Method findIsVirtual() {
            try {
                return Thread.class.getMethod("isVirtual");
            } catch (NoSuchMethodException | SecurityException ignored) {
                return null;
            }
        }
    }

    public static final class Context {
        static final Context SKIPPED = new Context(null, null, null, 0L, true);

        private final ThreadState state;
        final SharedEventSink sink;
        private final MethodMetadata method;
        final long gPtr;
        final boolean skipped;

        private Context(ThreadState state, SharedEventSink sink, MethodMetadata method, long gPtr, boolean skipped) {
            this.state = state;
            this.sink = sink;
            this.method = method;
            this.gPtr = gPtr;
            this.skipped = skipped;
        }
    }
}
