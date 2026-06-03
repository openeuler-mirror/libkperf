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
 * Description: ASM AdviceAdapter that injects TraceRuntime enter/exit probes into method bodies
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;
import org.objectweb.asm.commons.AdviceAdapter;

public final class TraceMethodVisitor extends AdviceAdapter {
    private static final Type CONTEXT_TYPE = Type.getObjectType("com/libkperf/tracex/runtime/TraceRuntime$Context");
    private final String methodName;
    private final String methodDesc;
    private final String owner;
    private int contextLocal = -1;

    public TraceMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor, String owner) {
        super(api, mv, access, name, descriptor);
        this.methodName = name;
        this.methodDesc = descriptor;
        this.owner = owner;
    }

    @Override
    protected void onMethodEnter() {
        visitLdcInsn(owner);
        visitLdcInsn(methodName);
        visitLdcInsn(methodDesc);
        visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER, "enter",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Lcom/libkperf/tracex/runtime/TraceRuntime$Context;", false);
        contextLocal = newLocal(CONTEXT_TYPE);
        storeLocal(contextLocal);
    }

    @Override
    protected void onMethodExit(int opcode) {
        if (contextLocal < 0) return;
        loadLocal(contextLocal);
        visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER, "exit",
            "(Lcom/libkperf/tracex/runtime/TraceRuntime$Context;)V", false);
    }
}
