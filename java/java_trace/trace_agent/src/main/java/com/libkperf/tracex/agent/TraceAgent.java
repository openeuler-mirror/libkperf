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

    private static final int RETRANSFORM_BATCH_SIZE = 64;

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
            try {
                TraceClassFileTransformer.restoreAll(inst);
            } catch (Throwable t) {
                TraceLog.warn("[trace_agent] restoreAll warning: " + t, t);
            }
            return;
        }
        try {
            start(config, inst);
        } catch (Throwable t) {
            TraceRuntime.setEnabled(false);
            removeActiveTransformer();
            TraceLog.warn("[trace_agent] start failed, skip instrumentation: " + t, t);
        }
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

    private static void retransformSafely(Instrumentation inst, List<Class<?>> candidates) {
        if (candidates == null || candidates.isEmpty()) {
            return;
        }
        int ok = 0;
        int failed = 0;
        for (int from = 0; from < candidates.size(); from += RETRANSFORM_BATCH_SIZE) {
            int to = Math.min(from + RETRANSFORM_BATCH_SIZE, candidates.size());
            List<Class<?>> batch = candidates.subList(from, to);
            try {
                inst.retransformClasses(batch.toArray(new Class<?>[0]));
                ok += batch.size();
            } catch (Throwable batchError) {
                TraceLog.warn("[trace_agent] retransform batch failed, fallback to one-by-one"
                        + ", from=" + from + ", to=" + to + ", error=" + batchError, batchError);
                for (Class<?> c : batch) {
                    try {
                        inst.retransformClasses(c);
                        ok++;
                    } catch (Throwable oneError) {
                        failed++;
                        TraceLog.warn("[trace_agent] skip bad class: " + Util.safeClassName(c)
                                + ", error=" + oneError, oneError);
                    }
                }
            }
        }
        TraceLog.info("[trace_agent] retransform done, ok=" + ok + ", failed=" + failed);
    }

    private static synchronized void start(TraceConfig config, Instrumentation inst) throws Exception {
        if (!config.valid) {
            TraceLog.info("[trace_agent] invalid trace filter config, skip instrumentation");
            return;
        }
        if (!inst.isRetransformClassesSupported()) {
            throw new IllegalStateException("JVM does not support class retransformation");
        }

        removeActiveTransformer();

        NativeThreadInfo.load(config.nativeLibPath);
        TraceRuntime.reconfigure(config.shmPath, config.slotCount);
        TraceRuntime.setEnabled(true);

        TraceClassFileTransformer transformer = new TraceClassFileTransformer(config);
        if (config.contextDepth > 0) {
            // Context expansion traces callers/callees around explicitly matched methods
            CallGraphIndex index = CallGraphIndex.build(inst, transformer, config);
            Set<MethodId> context = index.expandContext(config);
            config.addContextMethods(context);
            TraceLog.info("[trace_agent] context methods=" + context.size()
                + ", depth=" + config.contextDepth + ", max=" + config.contextMaxMethods);
        }

        inst.addTransformer(transformer, true);
        activeTransformer = transformer;
        activeInstrumentation = inst;

        List<Class<?>> candidates = collectCandidates(inst, transformer);
        TraceLog.info("[trace_agent] retransform candidates=" + candidates.size()
            + ", requiredIncludeRules=" + config.requiredIncludeRules.size()
            + ", configIncludeRules=" + config.includeRules.size()
            + ", excludeRules=" + config.excludeRules.size()
            + ", includeAll=" + config.includeAll);
        if (!candidates.isEmpty()) {
            retransformSafely(inst, candidates);
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
                String internal = Util.internalName(c);
                if (transformer.shouldTransformInternalName(internal)) {
                    out.add(c);
                }
            } catch (Throwable t) {
                TraceLog.warn("[trace_agent] skip candidate " + c + ": " + t, t);
            }
        }
        return out;
    }
}
