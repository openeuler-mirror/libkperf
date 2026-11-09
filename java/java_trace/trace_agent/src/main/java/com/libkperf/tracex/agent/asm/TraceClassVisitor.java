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
 * Description: ASM ClassVisitor that chains trace (TraceMethodVisitor) and virtual-thread phase
 * (VTPhaseMethodVisitor) instrumentation for matching methods
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import com.libkperf.tracex.agent.TraceConfig;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

public final class TraceClassVisitor extends ClassVisitor {
    private static final int SKIP_ACCESS = Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE | Opcodes.ACC_SYNTHETIC;

    private final TraceConfig config;
    private final String owner;
    private final boolean virtualThreadMode;
    private int instrumented;

    public TraceClassVisitor(ClassVisitor cv, TraceConfig config, String owner) {
        this(cv, config, owner, false);
    }

    public TraceClassVisitor(ClassVisitor cv, TraceConfig config, String owner, boolean virtualThreadMode) {
        super(Opcodes.ASM9, cv);
        this.config = config;
        this.owner = owner;
        this.virtualThreadMode = virtualThreadMode;
    }

    @Override
    public MethodVisitor visitMethod(int access, String name, String desc,
                                     String signature, String[] exceptions) {
        MethodVisitor next = super.visitMethod(access, name, desc, signature, exceptions);
        if (next == null || (access & SKIP_ACCESS) != 0 || isSpecialMethod(name)) {
            return next;
        }
        boolean isRunContinuation = virtualThreadMode
                && VirtualThreadMethodVisitor.isRunContinuation(access, name, desc);
        // Layered chain: TraceMethodVisitor emits enter/exit probes only when the
        // method matches the trace config; VirtualThreadMethodVisitor wraps the
        // result to inject carrier-side hooks around the mount/unmount call sites.
        MethodVisitor mv = next;
        if (config.shouldTransformMethod(owner, name, desc)) {
            mv = new TraceMethodVisitor(api, mv, access, name, desc, owner);
        }
        if (isRunContinuation) {
            mv = new VirtualThreadMethodVisitor(api, mv);
        }

        int vtType = config.enableVtPhase
        ? VTPhaseInstrumentation.getInstrumentationType(owner, name, desc)
        : VTPhaseInstrumentation.TYPE_NONE;
        if (vtType != VTPhaseInstrumentation.TYPE_NONE) {
            mv = new VTPhaseMethodVisitor(api, mv, access, name, desc, vtType);
        }

        if (mv != next) {
            instrumented++;
        }
        return mv;
    }

    private static boolean isSpecialMethod(String name) {
        return "<init>".equals(name) || "<clinit>".equals(name);
    }

    public int instrumentedMethodCount() {
        return instrumented;
    }
}
