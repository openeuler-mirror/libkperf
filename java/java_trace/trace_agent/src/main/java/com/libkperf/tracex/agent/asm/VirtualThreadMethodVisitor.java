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
 * Create: 2026-10-19
 * Description: ASM MethodVisitor that injects carrier-side mount/unmount hooks around
 *              VirtualThread runContinuation call sites, delegating to the wrapped mv
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import com.libkperf.tracex.runtime.TraceLog;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

final class VirtualThreadMethodVisitor extends MethodVisitor {
    private static final String VIRTUAL_THREAD_OWNER = "java/lang/VirtualThread";
    private static final String RUN_CONTINUATION_NAME = "runContinuation";
    private static final String METHOD_DESCRIPTOR = "()V";
    private static final String THREAD_DESCRIPTOR = "(Ljava/lang/Thread;)V";

    VirtualThreadMethodVisitor(int api, MethodVisitor mv) {
        super(api, mv);
    }

    static boolean isRunContinuation(int access, String name, String descriptor) {
        return (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE | Opcodes.ACC_STATIC)) == 0
                && RUN_CONTINUATION_NAME.equals(name) && METHOD_DESCRIPTOR.equals(descriptor);
    }

    @Override
    public void visitMethodInsn(int opcode, String callOwner, String methodName,
                                String methodDescriptor, boolean isInterface) {
        if (!isTransitionCall(opcode, callOwner, methodName, methodDescriptor)) {
            super.visitMethodInsn(opcode, callOwner, methodName, methodDescriptor, isInterface);
            return;
        }
        super.visitInsn(Opcodes.DUP);
        if ("mount".equals(methodName)) {
            super.visitMethodInsn(Opcodes.INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER,
                    "virtualThreadBeforeMount", THREAD_DESCRIPTOR, false);
            TraceLog.info("[trace-java-agent] virtual-thread beforeMount hook injected, callSite="
                    + callOwner + "." + methodName + methodDescriptor);
            super.visitMethodInsn(opcode, callOwner, methodName, methodDescriptor, isInterface);
            return;
        }
        super.visitMethodInsn(opcode, callOwner, methodName, methodDescriptor, isInterface);
        super.visitMethodInsn(Opcodes.INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER,
        "virtualThreadAfterUnmount", THREAD_DESCRIPTOR, false);

        TraceLog.info("[trace-java-agent] virtual-thread afterUnmount hook injected, callSite="
                + callOwner + "." + methodName + methodDescriptor);
    }

    private static boolean isTransitionCall(int opcode, String owner, String name, String descriptor) {
        return opcode != Opcodes.INVOKESTATIC
                && VIRTUAL_THREAD_OWNER.equals(owner)
                && METHOD_DESCRIPTOR.equals(descriptor)
                && ("mount".equals(name) || "unmount".equals(name));
    }
}
