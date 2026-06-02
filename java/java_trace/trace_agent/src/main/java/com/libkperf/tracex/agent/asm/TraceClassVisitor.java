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
    private final TraceConfig config;
    private final String owner;
    private int instrumented;

    public TraceClassVisitor(ClassVisitor cv, TraceConfig config, String owner) {
        super(Opcodes.ASM9, cv);
        this.config = config;
        this.owner = owner;
    }

    @Override
    public MethodVisitor visitMethod(int access, String name, String descriptor, String signature, String[] exceptions) {
        MethodVisitor mv = super.visitMethod(access, name, descriptor, signature, exceptions);
        if (mv == null) return null;
        if ((access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0) return mv;
        if ((access & Opcodes.ACC_SYNTHETIC) != 0) return mv;
        if ("<init>".equals(name) || "<clinit>".equals(name)) return mv;
        if (!config.shouldTransformMethod(owner, name, descriptor)) return mv;
        instrumented++;
        return new TraceMethodVisitor(api, mv, access, name, descriptor, owner);
    }

    public int instrumentedMethodCount() {
        return instrumented;
    }
}
