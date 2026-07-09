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
import com.libkperf.tracex.agent.TraceLog;
import com.libkperf.tracex.agent.Util;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.IllegalClassFormatException;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class TraceClassFileTransformer implements ClassFileTransformer {
    public static final String TRACE_RUNTIME_OWNER = "com/libkperf/tracex/runtime/TraceRuntime";

    // prevent the same class from being instrumented repeatedly
    private static final Set<ClassKey> INSTRUMENTED = ConcurrentHashMap.newKeySet();

    // save the original class bytecode for subsequent restoration
    private static final Map<ClassKey, byte[]> ORIGINAL = new ConcurrentHashMap<ClassKey, byte[]>();

    // prevent transformer re-entry
    private static final ThreadLocal<Boolean> REENTRANT = new ThreadLocal<Boolean>();

    private final TraceConfig config;

    public TraceClassFileTransformer(TraceConfig config) {
        this.config = config;
    }

    public boolean isStructuralExcluded(String className) {
        if (className == null || className.length() == 0) {
            return true;
        }
        if (className.endsWith("package-info") || className.indexOf("$$Lambda$") >= 0) {
            return true;
        }
        return false;
    }

    public boolean shouldTransformInternalName(String className) {
        return !isStructuralExcluded(className) && config.shouldTransformClass(className);
    }

    @Override
    public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,
                            ProtectionDomain protectionDomain, byte[] classfileBuffer) throws IllegalClassFormatException {
        if (className == null || classfileBuffer == null) {
            return null;
        }
        if (loader == null) {
            return null;
        }
        if (!shouldTransformInternalName(className)) {
            return null;
        }
        if (Boolean.TRUE.equals(REENTRANT.get())) {
            return null;
        }
        ClassKey key = new ClassKey(loader, className);
        if (INSTRUMENTED.contains(key)) {
            return null;
        }
        REENTRANT.set(Boolean.TRUE);
        try {
            ClassReader cr = new ClassReader(classfileBuffer);
            ClassWriter cw = new SafeClassWriter(cr, ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS, loader);
            TraceClassVisitor cv = new TraceClassVisitor(cw, config, className);
            cr.accept(cv, ClassReader.EXPAND_FRAMES);
            if (cv.instrumentedMethodCount() == 0) {
                return null;
            }
            byte[] transformed = cw.toByteArray();
            ORIGINAL.putIfAbsent(key, Arrays.copyOf(classfileBuffer, classfileBuffer.length));
            if (!INSTRUMENTED.add(key)) {
                return null;
            }
            return transformed;
        } catch (Throwable t) {
            TraceLog.warn("[trace_agent] transform failed for " + className + ": " + t, t);
            return null;
        } finally {
            REENTRANT.remove();
        }
    }

    public static void restoreAll(Instrumentation inst) {
        if (inst == null || !inst.isRetransformClassesSupported()) {
            return;
        }

        if (INSTRUMENTED.isEmpty()) {
            ORIGINAL.clear();
            return;
        }

        final Map<ClassKey, byte[]> restoreBytes = new HashMap<ClassKey, byte[]>(ORIGINAL);
        final Set<ClassKey> instrumentedSnapshot = ConcurrentHashMap.newKeySet();
        instrumentedSnapshot.addAll(INSTRUMENTED);

        ClassFileTransformer restoreTransformer = new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader,
                                    String className,
                                    Class<?> classBeingRedefined,
                                    ProtectionDomain protectionDomain,
                                    byte[] classfileBuffer) {
                if (className == null) {
                    return null;
                }

                ClassKey key = new ClassKey(loader, className);
                byte[] original = restoreBytes.get(key);
                if (original != null) {
                    return original;
                }

                return null;
            }
        };

        inst.addTransformer(restoreTransformer, true);

        int ok = 0;
        int failed = 0;

        try {
            for (Class<?> clazz : inst.getAllLoadedClasses()) {
                ClassKey key = null;
                try {
                    if (clazz == null) {
                        continue;
                    }
                    if (!inst.isModifiableClass(clazz)) {
                        continue;
                    }
                    String internalName = Util.internalName(clazz);
                    key = new ClassKey(clazz.getClassLoader(), internalName);
                    if (!instrumentedSnapshot.contains(key)) {
                        continue;
                    }
                    if (!restoreBytes.containsKey(key)) {
                        byte[] fromClasspath = Util.readClassBytes(clazz.getClassLoader(), internalName);
                        if (fromClasspath != null) {
                            restoreBytes.put(key, fromClasspath);
                        }
                    }
                    if (!restoreBytes.containsKey(key)) {
                        failed++;
                        TraceLog.info("[trace_agent] restore skip no original bytes: " + Util.safeClassName(clazz));
                        continue;
                    }
                    inst.retransformClasses(clazz);
                    INSTRUMENTED.remove(key);
                    ORIGINAL.remove(key);
                    ok++;
                } catch (Throwable t) {
                    failed++;
                    TraceLog.warn("[trace_agent] restore skip bad class: " + Util.safeClassName(clazz) + ", error=" + t, t);
                    if (key != null) {
                        INSTRUMENTED.add(key);
                    }
                }
            }
        } catch (Throwable t) {
            TraceLog.warn("[trace_agent] restoreAll outer warning: " + t, t);
        } finally {
            try {
                inst.removeTransformer(restoreTransformer);
            } catch (Throwable ignored) {
            }
            if (failed == 0) {
                INSTRUMENTED.clear();
                ORIGINAL.clear();
            }
            TraceLog.info("[trace_agent] restore done, ok=" + ok + ", failed=" + failed);
        }
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
