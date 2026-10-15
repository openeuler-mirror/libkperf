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
 * Description: Java Agent entry point that instruments classes for method tracing via premain/agentmain
 ******************************************************************************/
package com.libkperf.tracex.agent;

import com.libkperf.tracex.agent.asm.TraceClassFileTransformer;
import com.libkperf.tracex.runtime.NativeThreadInfo;
import com.libkperf.tracex.runtime.TraceLog;
import com.libkperf.tracex.runtime.TraceRuntime;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.instrument.Instrumentation;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.URI;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.jar.JarOutputStream;

public final class TraceAgent {
    private TraceAgent() {}

    private static volatile TraceClassFileTransformer activeTransformer;
    private static volatile Instrumentation activeInstrumentation;
    private static volatile JarFile bootstrapRuntimeJar;
    private static volatile boolean bootstrapRuntimeReady;

    private static final int RETRANSFORM_BATCH_SIZE = 64;
    private static final String RUNTIME_ENTRY_PREFIX = "com/libkperf/tracex/runtime/";
    private static final String AGENT_BUILD_MARKER = "attach-safe-v2";

    public static void premain(String agentArgs, Instrumentation inst) throws Exception {
        agentmain(agentArgs, inst);
    }

    public static void agentmain(String agentArgs, Instrumentation inst) throws Exception {
        String requestedAction = requestedAction(agentArgs);
        if ("stop".equalsIgnoreCase(requestedAction) || "restore".equalsIgnoreCase(requestedAction)) {
            runControlAction(requestedAction, inst);
            return;
        }

        installBootstrapRuntime(inst);
        TraceConfig config = TraceConfig.parse(agentArgs);
        TraceLog.info("[trace-java-agent] build=" + AGENT_BUILD_MARKER + ", action=start");
        try {
            start(config, inst);
        } catch (Throwable t) {
            TraceRuntime.stop();
            try {
                removeActiveTransformer();
            } catch (Throwable removeError) {
                t.addSuppressed(removeError);
            }
            TraceLog.warn("[trace-java-agent] start failed: " + t, t);
            rethrow(t);
        }
    }

    private static String requestedAction(String agentArgs) {
        if (agentArgs == null || agentArgs.length() == 0) {
            return "start";
        }
        int separator = agentArgs.indexOf(';');
        String first = separator < 0 ? agentArgs : agentArgs.substring(0, separator);
        int equals = first.indexOf('=');
        if (equals <= 0 || !"action".equalsIgnoreCase(first.substring(0, equals))) {
            return "start";
        }
        return first.substring(equals + 1);
    }

    private static void runControlAction(String action, Instrumentation inst) {
        try {
            TraceLog.info("[trace-java-agent] build=" + AGENT_BUILD_MARKER + ", action=" + action);
            TraceRuntime.stop();
            if ("stop".equalsIgnoreCase(action)) {
                removeActiveTransformer();
                return;
            }
            if (removeActiveTransformer()) {
                try {
                    TraceClassFileTransformer.restoreAll(inst);
                } catch (Throwable t) {
                    TraceLog.warn("[trace-java-agent] restoreAll warning: " + t, t);
                }
            } else {
                TraceLog.warn("[trace-java-agent] restore deferred: active transformer removal failed", null);
            }
        } catch (ThreadDeath t) {
            throw t;
        } catch (VirtualMachineError e) {
            throw e;
        } catch (Throwable t) {
            try {
                TraceLog.warn("[trace-java-agent] control action failed: " + t, t);
            } catch (Throwable ignored) {
            }
        }
    }

    private static void rethrow(Throwable t) throws Exception {
        if (t instanceof Exception) {
            throw (Exception) t;
        }
        if (t instanceof Error) {
            throw (Error) t;
        }
        throw new RuntimeException(t);
    }

    private static synchronized void installBootstrapRuntime(Instrumentation inst) throws Exception {
        if (bootstrapRuntimeReady) {
            return;
        }
        if (bootstrapRuntimeJar != null) {
            addBootstrapRuntimeReadEdges(inst);
            bootstrapRuntimeReady = true;
            return;
        }
        ensureRuntimeNotLoadedByApplicationLoader(inst);

        URI location = TraceAgent.class.getProtectionDomain().getCodeSource().getLocation().toURI();
        File agentJarFile = new File(location);
        if (!agentJarFile.isFile()) {
            throw new IllegalStateException("trace agent must be loaded from a jar: " + agentJarFile);
        }

        File runtimeJarFile = createBootstrapRuntimeJar(agentJarFile);
        JarFile runtimeJar = null;
        boolean appended = false;
        try {
            runtimeJar = new JarFile(runtimeJarFile, false);
            inst.appendToBootstrapClassLoaderSearch(runtimeJar);
            appended = true;
            bootstrapRuntimeJar = runtimeJar;
            if (!runtimeJarFile.delete()) {
                runtimeJarFile.deleteOnExit();
            }
            addBootstrapRuntimeReadEdges(inst);
            bootstrapRuntimeReady = true;
        } catch (Throwable t) {
            if (!appended && runtimeJar != null) {
                try {
                    runtimeJar.close();
                } catch (Throwable ignored) {
                }
            }
            if (appended || !runtimeJarFile.delete()) {
                runtimeJarFile.deleteOnExit();
            }
            throw t;
        }
    }

    private static void ensureRuntimeNotLoadedByApplicationLoader(Instrumentation inst) {
        for (Class<?> clazz : inst.getAllLoadedClasses()) {
            String name = clazz.getName();
            if (name.startsWith("com.libkperf.tracex.runtime.") && clazz.getClassLoader() != null) {
                throw new IllegalStateException("trace runtime already loaded outside bootstrap: " + name);
            }
        }
    }

    private static File createBootstrapRuntimeJar(File agentJarFile) throws Exception {
        File runtimeJarFile = File.createTempFile("libkperf-trace-runtime-", ".jar");
        boolean hasRuntime = false;
        try (JarFile agentJar = new JarFile(agentJarFile, false);
             JarOutputStream out = new JarOutputStream(new FileOutputStream(runtimeJarFile))) {
            Enumeration<JarEntry> entries = agentJar.entries();
            byte[] buffer = new byte[8192];
            while (entries.hasMoreElements()) {
                JarEntry entry = entries.nextElement();
                String name = entry.getName();
                if (entry.isDirectory()
                        || !name.startsWith(RUNTIME_ENTRY_PREFIX)
                        || !name.endsWith(".class")) {
                    continue;
                }
                out.putNextEntry(new JarEntry(name));
                try (InputStream in = agentJar.getInputStream(entry)) {
                    int read;
                    while ((read = in.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                    }
                }
                out.closeEntry();
                hasRuntime |= name.equals("com/libkperf/tracex/runtime/TraceRuntime.class");
            }
        } catch (Throwable t) {
            if (!runtimeJarFile.delete()) {
                runtimeJarFile.deleteOnExit();
            }
            throw t;
        }
        if (!hasRuntime) {
            if (!runtimeJarFile.delete()) {
                runtimeJarFile.deleteOnExit();
            }
            throw new IllegalStateException("trace agent jar is missing bootstrap runtime classes");
        }
        return runtimeJarFile;
    }

    private static void addBootstrapRuntimeReadEdges(Instrumentation inst) throws Exception {
        Class<?> moduleClass;
        try {
            moduleClass = Class.forName("java.lang.Module");
        } catch (ClassNotFoundException ignored) {
            return;
        }

        Method getModule = Class.class.getMethod("getModule");
        Method isNamed = moduleClass.getMethod("isNamed");
        Method canRead = moduleClass.getMethod("canRead", moduleClass);
        Method isModifiableModule = Instrumentation.class.getMethod("isModifiableModule", moduleClass);
        Method redefineModule = Instrumentation.class.getMethod("redefineModule", moduleClass,
                Set.class, Map.class, Map.class, Set.class, Map.class);
        Object runtimeModule = getModule.invoke(Class.forName(
                "com.libkperf.tracex.runtime.TraceRuntime", false, null));
        Set<Object> visited = Collections.newSetFromMap(new IdentityHashMap<Object, Boolean>());
        for (Class<?> clazz : inst.getAllLoadedClasses()) {
            Object module = getModule.invoke(clazz);
            if (!visited.add(module)
                    || !((Boolean) isNamed.invoke(module))
                    || ((Boolean) canRead.invoke(module, runtimeModule))
                    || !((Boolean) isModifiableModule.invoke(inst, module))) {
                continue;
            }
            redefineModule.invoke(inst, module, Collections.singleton(runtimeModule),
                    Collections.emptyMap(), Collections.emptyMap(),
                    Collections.emptySet(), Collections.emptyMap());
        }
    }

    private static synchronized boolean removeActiveTransformer() {
        TraceClassFileTransformer t = activeTransformer;
        Instrumentation inst = activeInstrumentation;
        if (t == null && inst == null) {
            return true;
        }
        if (t == null || inst == null) {
            TraceLog.warn("[trace-java-agent] inconsistent active transformer state", null);
            activeTransformer = null;
            activeInstrumentation = null;
            return false;
        }
        try {
            boolean removed = inst.removeTransformer(t);
            if (!removed) {
                TraceLog.warn("[trace-java-agent] active transformer was already absent", null);
            }
        } catch (Throwable error) {
            TraceLog.warn("[trace-java-agent] failed to remove active transformer: " + error, error);
            return false;
        }
        activeTransformer = null;
        activeInstrumentation = null;
        return true;
    }

    private static void retransformSafely(Instrumentation inst, List<Class<?>> candidates,
                                          TraceClassFileTransformer transformer) {
        if (candidates == null || candidates.isEmpty()) {
            return;
        }
        for (int from = 0; from < candidates.size(); from += RETRANSFORM_BATCH_SIZE) {
            int to = Math.min(from + RETRANSFORM_BATCH_SIZE, candidates.size());
            List<Class<?>> batch = candidates.subList(from, to);
            try {
                inst.retransformClasses(batch.toArray(new Class<?>[0]));
            } catch (Throwable batchError) {
                transformer.discardFailedRetransformations(batch);
                TraceLog.warn("[trace-java-agent] retransform batch failed, fallback to one-by-one"
                        + ", from=" + from + ", to=" + to + ", error=" + batchError, batchError);
                for (Class<?> c : batch) {
                    try {
                        inst.retransformClasses(c);
                    } catch (Throwable oneError) {
                        transformer.discardFailedRetransformations(Collections.singletonList(c));
                        TraceLog.warn("[trace-java-agent] skip bad class: " + Util.safeClassName(c) + ", error=" + oneError, oneError);
                    }
                }
            }
        }
    }

    private static synchronized void start(TraceConfig config, Instrumentation inst) throws Exception {
        if (!config.valid) {
            throw new IllegalArgumentException(config.error.length() == 0
                    ? "invalid trace filter config" : config.error);
        }
        if (!inst.isRetransformClassesSupported()) {
            throw new IllegalStateException("JVM does not support class retransformation");
        }

        TraceRuntime.setEnabled(false);
        if (!removeActiveTransformer()) {
            throw new IllegalStateException("failed to remove previous trace transformer");
        }
        TraceClassFileTransformer.restoreAll(inst);
        if (TraceClassFileTransformer.hasInstrumentedClasses()) {
            throw new IllegalStateException("previous instrumentation could not be fully restored");
        }

        NativeThreadInfo.load(config.nativeLibPath);
        TraceRuntime.reconfigure(config.shmPath, config.slotCount);
        TraceRuntime.setEnabled(false);
        TraceRuntime.preloadVirtualThreadHelpers();

        TraceClassFileTransformer transformer = new TraceClassFileTransformer(config);
        if (config.contextDepth > 0) {
            // Context expansion traces callers/callees around explicitly matched methods
            CallGraphIndex index = CallGraphIndex.build(inst, transformer, config);
            Set<MethodId> context = index.expandContext(config);
            config.addContextMethods(context);
            TraceLog.info("[trace-java-agent] context methods=" + context.size()
                + ", depth=" + config.contextDepth + ", max=" + config.contextMaxMethods);
        }

        inst.addTransformer(transformer, true);
        activeTransformer = transformer;
        activeInstrumentation = inst;

        List<Class<?>> candidates = collectCandidates(inst, transformer);
        TraceLog.info("[trace-java-agent] retransform candidates=" + candidates.size()
            + ", requiredIncludeRules=" + config.requiredIncludeRules.size()
            + ", configIncludeRules=" + config.includeRules.size()
            + ", excludeRules=" + config.excludeRules.size());
        retransformSafely(inst, candidates, transformer);
        TraceRuntime.configureVirtualThreadCarrierHooks(transformer.hasVirtualThreadCarrierHooks());
        TraceRuntime.setEnabled(true);
        TraceLog.info("[trace-java] filter: " + config.filterRuleStatus());
    }

    // Collects all classes that are candidates for retransformation.
    private static List<Class<?>> collectCandidates(Instrumentation inst, TraceClassFileTransformer transformer) {
        List<Class<?>> out = new ArrayList<Class<?>>();
        for (Class<?> c : inst.getAllLoadedClasses()) {
            try {
                if (c == null || !inst.isModifiableClass(c)) {
                    continue;
                }
                int m = c.getModifiers();
                if (Modifier.isInterface(m) || c.isAnnotation() || c.isArray() || c.isPrimitive()) {
                    continue;
                }
                String internal = Util.internalName(c);
                if (transformer.shouldTransformInternalName(internal)) {
                    out.add(c);
                }
            } catch (Throwable t) {
                TraceLog.warn("[trace-java-agent] skip candidate " + c + ": " + t, t);
            }
        }
        return out;
    }
}
