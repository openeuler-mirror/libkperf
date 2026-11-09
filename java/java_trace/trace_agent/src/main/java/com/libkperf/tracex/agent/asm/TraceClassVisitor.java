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
 * Description: ASM ClassVisitor that delegates method instrumentation to TraceMethodVisitor
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
        boolean isRunContinuation = virtualThreadMode
                && next != null
                && VirtualThreadMethodVisitor.isRunContinuation(access, name, desc);
        // Layered chain: TraceMethodVisitor emits enter/exit probes only when the
        // method matches the trace config; VirtualThreadMethodVisitor wraps the
        // result to inject carrier-side hooks around the mount/unmount call sites.
        MethodVisitor mv = next;
        if (needTrace(mv, access, name, desc)) {
            mv = new TraceMethodVisitor(api, mv, access, name, desc, owner);
        }
        if (isRunContinuation) {
            mv = new VirtualThreadMethodVisitor(api, mv);
        }
        if (mv != next) {
            instrumented++;
        }
        return mv;
    }

    private boolean needTrace(MethodVisitor mv, int access, String name, String desc) {
        return mv != null && (access & SKIP_ACCESS) == 0
                && !isSpecialMethod(name)
                && config.shouldTransformMethod(owner, name, desc);
    }

    private static boolean isSpecialMethod(String name) {
        return "<init>".equals(name) || "<clinit>".equals(name);
    }

    public int instrumentedMethodCount() {
        return instrumented;
    }
}
