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
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.IllegalClassFormatException;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class TraceClassFileTransformer implements ClassFileTransformer {
    public static final String TRACE_RUNTIME_OWNER = "com/libkperf/tracex/runtime/TraceRuntime";
    private static final Set<ClassKey> INSTRUMENTED = ConcurrentHashMap.newKeySet();
    private static final Map<ClassKey, byte[]> ORIGINAL = new ConcurrentHashMap<ClassKey, byte[]>();
    private static final ThreadLocal<Boolean> REENTRANT = new ThreadLocal<Boolean>();

    private final TraceConfig config;

    public TraceClassFileTransformer(TraceConfig config) {
        this.config = config;
    }

    public boolean isStructuralExcluded(String className) {
        if (className == null || className.length() == 0) return true;
        if (className.endsWith("package-info") || className.indexOf("$$Lambda$") >= 0) return true;
        return false;
    }

    public boolean shouldTransformInternalName(String className) {
        return !isStructuralExcluded(className) && config.shouldTransformClass(className);
    }

    @Override
    public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,
                            ProtectionDomain protectionDomain, byte[] classfileBuffer) throws IllegalClassFormatException {
        if (className == null || classfileBuffer == null) return null;
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
            if (classBeingRedefined != null) {
                ORIGINAL.putIfAbsent(key, Arrays.copyOf(classfileBuffer, classfileBuffer.length));
            }
            ClassReader cr = new ClassReader(classfileBuffer);
            ClassWriter cw = new ClassWriter(cr, ClassWriter.COMPUTE_MAXS);
            TraceClassVisitor cv = new TraceClassVisitor(cw, config, className);
            cr.accept(cv, ClassReader.EXPAND_FRAMES);
            if (cv.instrumentedMethodCount() == 0) return null;
            INSTRUMENTED.add(key);
            return cw.toByteArray();
        } catch (Throwable t) {
            ORIGINAL.remove(key);
            System.err.println("[trace_agent] transform failed for " + className + ": " + t);
            return null;
        } finally {
            REENTRANT.remove();
        }
    }

    public static void restoreAll(Instrumentation inst) throws Exception {
        if (inst == null || !inst.isRetransformClassesSupported()) {
            INSTRUMENTED.clear();
            ORIGINAL.clear();
            return;
        }

        if (INSTRUMENTED.isEmpty()) {
            ORIGINAL.clear();
            return;
        }

        final Map<ClassKey, byte[]> snapshot = new HashMap<ClassKey, byte[]>(ORIGINAL);
        final Set<ClassKey> instrumentedSnapshot = ConcurrentHashMap.newKeySet();
        instrumentedSnapshot.addAll(INSTRUMENTED);

        ClassFileTransformer restoreTransformer = new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,
                                    ProtectionDomain protectionDomain, byte[] classfileBuffer) {
                if (loader == null || className == null || classBeingRedefined == null) {
                    return null;
                }
                ClassKey key = new ClassKey(loader, className);
                byte[] original = snapshot.get(key);
                if (original != null) {
                    return original;
                }
                if (instrumentedSnapshot.contains(key)) {
                    byte[] fromClasspath = readFromClasspath(loader, className);
                    if (fromClasspath != null) {
                        return fromClasspath;
                    }
                }
                return null;
            }
        };

        inst.addTransformer(restoreTransformer, true);
        try {
            List<Class<?>> classes = new ArrayList<Class<?>>();
            for (Class<?> clazz : inst.getAllLoadedClasses()) {
                ClassLoader loader = clazz.getClassLoader();
                if (loader == null || !inst.isModifiableClass(clazz)) {
                    continue;
                }
                String internalName = clazz.getName().replace('.', '/');
                ClassKey key = new ClassKey(loader, internalName);
                if (instrumentedSnapshot.contains(key)) {
                    classes.add(clazz);
                }
            }
            if (!classes.isEmpty()) {
                inst.retransformClasses(classes.toArray(new Class<?>[classes.size()]));
            }
        } finally {
            inst.removeTransformer(restoreTransformer);
            INSTRUMENTED.clear();
            ORIGINAL.clear();
        }
    }

    private static byte[] readFromClasspath(ClassLoader loader, String className) {
        if (loader == null || className == null) return null;
        try {
            java.io.InputStream in = loader.getResourceAsStream(className + ".class");
            if (in == null) return null;
            try {
                java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0) bos.write(buf, 0, n);
                return bos.toByteArray();
            } finally {
                try { in.close(); } catch (Exception ignored) {}
            }
        } catch (Throwable t) {
            return null;
        }
    }

    private static final class ClassKey {
        private final ClassLoader loader;
        private final String name;
        ClassKey(ClassLoader loader, String name) { this.loader = loader; this.name = name; }
        @Override public boolean equals(Object o) {
            if (this == o) return true;
            if (!(o instanceof ClassKey)) return false;
            ClassKey k = (ClassKey) o;
            return loader == k.loader && name.equals(k.name);
        }
        @Override public int hashCode() { return System.identityHashCode(loader) * 31 + name.hashCode(); }
    }
}
