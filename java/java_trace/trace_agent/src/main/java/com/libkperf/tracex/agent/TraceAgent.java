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
import com.libkperf.tracex.runtime.TraceRuntime;

import java.lang.instrument.Instrumentation;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

public final class TraceAgent {
    private TraceAgent() {}

    private static volatile TraceClassFileTransformer activeTransformer;
    private static volatile Instrumentation activeInstrumentation;

    public static void premain(String agentArgs, Instrumentation inst) throws Exception {
        agentmain(agentArgs, inst);
    }

    public static void agentmain(String agentArgs, Instrumentation inst) throws Exception {
        TraceConfig config = TraceConfig.parse(agentArgs);
        if (config.isStopAction()) {
            TraceRuntime.setEnabled(false);
            removeActiveTransformer();
            return;
        }
        if (config.isRestoreAction()) {
            TraceRuntime.setEnabled(false);
            removeActiveTransformer();
            TraceClassFileTransformer.restoreAll(inst);
            return;
        }
        start(config, inst);
    }

    private static synchronized void removeActiveTransformer() {
        TraceClassFileTransformer t = activeTransformer;
        Instrumentation inst = activeInstrumentation;
        if (t != null && inst != null) {
            try {
                inst.removeTransformer(t);
            } catch (Throwable ignored) {}
            activeTransformer = null;
            activeInstrumentation = null;
        }
    }

    private static synchronized void start(TraceConfig config, Instrumentation inst) throws Exception {
        if (!inst.isRetransformClassesSupported()) {
            throw new IllegalStateException("JVM does not support class retransformation");
        }

        removeActiveTransformer();

        NativeThreadInfo.load(config.nativeLibPath);
        TraceRuntime.reconfigure(config.shmPath, config.slotCount);
        TraceRuntime.setEnabled(true);

        TraceClassFileTransformer transformer = new TraceClassFileTransformer(config);
        if (config.contextDepth > 0) {
            CallGraphIndex index = CallGraphIndex.build(inst, transformer, config);
            Set<MethodId> context = index.expandContext(config);
            config.addContextMethods(context);
            System.err.println("[trace_agent] context methods=" + context.size()
                + ", depth=" + config.contextDepth + ", max=" + config.contextMaxMethods);
        }

        inst.addTransformer(transformer, true);
        activeTransformer = transformer;
        activeInstrumentation = inst;

        List<Class<?>> candidates = collectCandidates(inst, transformer);
        System.err.println("[trace_agent] retransform candidates=" + candidates.size()
            + ", requiredIncludeRules=" + config.requiredIncludeRules.size()
            + ", configIncludeRules=" + config.includeRules.size()
            + ", excludeRules=" + config.excludeRules.size()
            + ", includeAll=" + config.includeAll);
        if (!candidates.isEmpty()) {
            try {
                inst.retransformClasses(candidates.toArray(new Class<?>[0]));
            } catch (Throwable t) {
                System.err.println("[trace_agent] retransform failed: " + t);
            }
        }
    }
    
    // Collects all classes that are candidates for retransformation.
    private static List<Class<?>> collectCandidates(Instrumentation inst, TraceClassFileTransformer transformer) {
        List<Class<?>> out = new ArrayList<Class<?>>();
        for (Class<?> c : inst.getAllLoadedClasses()) {
            try {
                if (c == null || c.getClassLoader() == null || !inst.isModifiableClass(c)) {
                    continue;
                }
                int m = c.getModifiers();
                if (Modifier.isInterface(m) || c.isAnnotation() || c.isArray() || c.isPrimitive()) {
                    continue;
                }
                String internal = c.getName().replace('.', '/');
                if (transformer.shouldTransformInternalName(internal)) {
                    out.add(c);
                }
            } catch (Throwable t) {
                System.err.println("[trace_agent] skip candidate " + c + ": " + t);
            }
        }
        return out;
    }
}
