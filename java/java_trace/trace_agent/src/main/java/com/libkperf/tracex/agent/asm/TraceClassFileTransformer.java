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
 * Description: ClassFileTransformer that selects and retransforms classes matching filter rules
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import com.libkperf.tracex.agent.TraceConfig;
import com.libkperf.tracex.agent.Util;
import com.libkperf.tracex.runtime.TraceLog;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.IllegalClassFormatException;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class TraceClassFileTransformer implements ClassFileTransformer {
    public static final String TRACE_RUNTIME_OWNER = "com/libkperf/tracex/runtime/TraceRuntime";
    private static final String TRACE_RUNTIME_PACKAGE = "com/libkperf/tracex/runtime/";
    private static final String VIRTUAL_THREAD_CLASS = "java/lang/VirtualThread";
    // Dynamic attach invokes agentmain through these reflection classes. If
    // they are retransformed, a later stop/restore attach can execute tracing
    // callbacks while the runtime is being disabled and fail before agentmain
    // is entered. Keep the control path permanently unmodified.
    private static final String REFLECT_METHOD_CLASS = "java/lang/reflect/Method";
    private static final String DIRECT_METHOD_HANDLE_ACCESSOR_CLASS =
            "jdk/internal/reflect/DirectMethodHandleAccessor";
    private static final int RESTORE_BATCH_SIZE = 64;

    // Classes whose current definitions contain tracing instrumentation.
    private static final Set<ClassKey> INSTRUMENTED = ConcurrentHashMap.newKeySet();

    private final TraceConfig config;
    private volatile boolean virtualThreadCarrierHooksEmitted;

    public TraceClassFileTransformer(TraceConfig config) {
        this.config = config;
    }

    public boolean isStructuralExcluded(String className) {
        if (className == null || className.length() == 0) {
            return true;
        }
        if (className.startsWith(TRACE_RUNTIME_PACKAGE)) {
            return true;
        }
        if (REFLECT_METHOD_CLASS.equals(className)
                || DIRECT_METHOD_HANDLE_ACCESSOR_CLASS.equals(className)) {
            return true;
        }
        if (className.endsWith("package-info") || className.indexOf("$$Lambda$") >= 0) {
            return true;
        }
        return false;
    }

    public boolean shouldTransformInternalName(String className) {
        return VIRTUAL_THREAD_CLASS.equals(className)
                || (!isStructuralExcluded(className) && config.shouldTransformClass(className));
    }

    @Override
    public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,
                            ProtectionDomain protectionDomain, byte[] classfileBuffer) throws IllegalClassFormatException {
        try {
            if (className == null || classfileBuffer == null) {
                return null;
            }
            if (!shouldTransformInternalName(className)) {
                return null;
            }
            ClassKey key = new ClassKey(loader, className);

            ClassReader cr = new ClassReader(classfileBuffer);
            int instrumentedMethodCount;
            if (VIRTUAL_THREAD_CLASS.equals(className)) {
                // Pass 1: verify that runContinuation contains both carrier-side call sites.
                if (!TraceMethodVisitor.supportsVirtualThreadCarrierCallSites(cr)) {
                    TraceLog.warn("[trace-java-agent] virtual-thread carrier hooks unavailable"
                            + ", runContinuation mount/unmount call sites not found", null);
                    return null;
                }
            }

            ClassWriter cw = new SafeClassWriter(cr, ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS, loader);
            if (VIRTUAL_THREAD_CLASS.equals(className)) {
                // Pass 2: instrument the verified call sites, never mount/unmount method bodies.
                TraceMethodVisitor.VirtualThreadInstrumentation vt =
                        TraceMethodVisitor.instrumentVirtualThread(cr, cw);
                if (vt.instrumentedMethodCount() != 1 || vt.mountCallSiteCount() == 0 || vt.unmountCallSiteCount() == 0) {
                    TraceLog.warn("[trace-java-agent] virtual-thread carrier hooks unavailable"
                            + ", runContinuationMethods=" + vt.instrumentedMethodCount()
                            + ", mountCallSites=" + vt.mountCallSiteCount()
                            + ", unmountCallSites=" + vt.unmountCallSiteCount(), null);
                    return null;
                }
                instrumentedMethodCount = vt.instrumentedMethodCount();
            } else {
                TraceClassVisitor cv = new TraceClassVisitor(cw, config, className);
                cr.accept(cv, ClassReader.EXPAND_FRAMES);
                if (cv.instrumentedMethodCount() == 0) {
                    return null;
                }
                instrumentedMethodCount = cv.instrumentedMethodCount();
            }
            byte[] transformed = cw.toByteArray();
            INSTRUMENTED.add(key);
            if (VIRTUAL_THREAD_CLASS.equals(className)) {
                virtualThreadCarrierHooksEmitted = true;
            }
            TraceLog.info("[trace-java-agent] instrumented class=" + className.replace('/', '.')
                    + ", methodCount=" + instrumentedMethodCount);
            return transformed;
        } catch (Throwable t) {
            TraceLog.warn("[trace-java-agent] transform failed for " + className + ": " + t, t);
            return null;
        }
    }

    public boolean hasVirtualThreadCarrierHooks() {
        return virtualThreadCarrierHooksEmitted;
    }

    public void discardFailedRetransformations(Iterable<Class<?>> classes) {
        if (classes == null) {
            return;
        }
        for (Class<?> clazz : classes) {
            if (clazz == null) {
                continue;
            }
            ClassKey key = new ClassKey(clazz.getClassLoader(), Util.internalName(clazz));
            INSTRUMENTED.remove(key);
            if (VIRTUAL_THREAD_CLASS.equals(key.name)) {
                virtualThreadCarrierHooksEmitted = false;
            }
        }
    }

    public static boolean hasInstrumentedClasses() {
        return !INSTRUMENTED.isEmpty();
    }

    public static void restoreAll(Instrumentation inst) {
        if (inst == null || !inst.isRetransformClassesSupported()) {
            return;
        }

        if (INSTRUMENTED.isEmpty()) {
            return;
        }

        final Set<ClassKey> instrumentedSnapshot = ConcurrentHashMap.newKeySet();
        instrumentedSnapshot.addAll(INSTRUMENTED);

        RestoreResult result = new RestoreResult();
        try {
            List<Class<?>> restoreCandidates = new ArrayList<Class<?>>();
            for (Class<?> clazz : inst.getAllLoadedClasses()) {
                ClassKey key = null;
                try {
                    if (clazz == null) {
                        continue;
                    }
                    String internalName = Util.internalName(clazz);
                    key = new ClassKey(clazz.getClassLoader(), internalName);
                    if (!instrumentedSnapshot.contains(key)) {
                        continue;
                    }
                    if (!inst.isModifiableClass(clazz)) {
                        result.failed++;
                        TraceLog.warn("[trace-java-agent] restore skip unmodifiable class: "
                                + Util.safeClassName(clazz), null);
                        continue;
                    }
                    restoreCandidates.add(clazz);
                } catch (Throwable t) {
                    result.failed++;
                    TraceLog.warn("[trace-java-agent] restore skip bad class: " + Util.safeClassName(clazz) + ", error=" + t, t);
                    if (key != null) {
                        INSTRUMENTED.add(key);
                    }
                }
            }
            restoreInBatches(inst, restoreCandidates, result);
        } catch (Throwable t) {
            result.failed++;
            TraceLog.warn("[trace-java-agent] restoreAll outer warning: " + t, t);
        } finally {
            if (result.failed == 0) {
                INSTRUMENTED.clear();
            }
        }
    }

    private static void restoreInBatches(Instrumentation inst, List<Class<?>> classes, RestoreResult result) {
        for (int from = 0; from < classes.size(); from += RESTORE_BATCH_SIZE) {
            int to = Math.min(from + RESTORE_BATCH_SIZE, classes.size());
            restoreBatch(inst, classes.subList(from, to), result);
        }
    }

    private static void restoreBatch(Instrumentation inst, List<Class<?>> classes, RestoreResult result) {
        if (classes.isEmpty()) {
            return;
        }
        try {
            inst.retransformClasses(classes.toArray(new Class<?>[0]));
        } catch (Throwable t) {
            if (classes.size() == 1) {
                Class<?> clazz = classes.get(0);
                result.failed++;
                TraceLog.warn("[trace-java-agent] restore skip bad class: " + Util.safeClassName(clazz) + ", error=" + t, t);
                return;
            }
            int middle = classes.size() / 2;
            restoreBatch(inst, classes.subList(0, middle), result);
            restoreBatch(inst, classes.subList(middle, classes.size()), result);
            return;
        }
        for (Class<?> clazz : classes) {
            ClassKey key = new ClassKey(clazz.getClassLoader(), Util.internalName(clazz));
            INSTRUMENTED.remove(key);
        }
        result.ok += classes.size();
    }

    private static final class RestoreResult {
        private int ok;
        private int failed;
    }

    private static final class SafeClassWriter extends ClassWriter {
        private final ClassLoader loader;

        SafeClassWriter(ClassReader cr, int flags, ClassLoader loader) {
            super(cr, flags);
            this.loader = loader;
        }

        @Override
        protected String getCommonSuperClass(String type1, String type2) {
            try {
                Class<?> class1 = loadClass(type1);
                Class<?> class2 = loadClass(type2);
                if (class1.isAssignableFrom(class2)) {
                    return type1;
                }
                if (class2.isAssignableFrom(class1)) {
                    return type2;
                }
                if (class1.isInterface() || class2.isInterface()) {
                    return "java/lang/Object";
                }
                do {
                    class1 = class1.getSuperclass();
                } while (class1 != null && !class1.isAssignableFrom(class2));
                return class1 == null ? "java/lang/Object" : class1.getName().replace('.', '/');
            } catch (Throwable ignored) {
                return "java/lang/Object";
            }
        }

        private Class<?> loadClass(String internalName) throws ClassNotFoundException {
            return Class.forName(internalName.replace('/', '.'), false, loader);
        }
    }

    private static final class ClassKey {
        private final ClassLoader loader;
        private final String name;
        ClassKey(ClassLoader loader, String name) { this.loader = loader; this.name = name; }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof ClassKey)) {
                return false;
            }
            ClassKey k = (ClassKey) o;
            return loader == k.loader && name.equals(k.name);
        }

        @Override
        public int hashCode() {
            return System.identityHashCode(loader) * 31 + name.hashCode();
        }
    }
}
