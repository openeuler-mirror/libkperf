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

import org.objectweb.asm.Label;
import org.objectweb.asm.Handle;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Type;
import org.objectweb.asm.commons.AdviceAdapter;

import java.util.ArrayList;
import java.util.List;

/**
 * Example:
 * public int add(int a, int b)
 * {
 *     return a + b;
 * }
 * 
 * After transformation:
 * public int add(int a, int b)
 * {
 *     TraceRuntime.Context context = TraceRuntime.enter("com/example/Foo", "add", "(II)I");
 *     try {
 *         int result = a + b;
 *         TraceRuntime.exit(context);
 *         return result;
 *     } catch (Throwable t) {
 *         TraceRuntime.exit(context);
 *         throw t;
 *     }
 * }
 */
public final class TraceMethodVisitor extends AdviceAdapter {
    private static final Type CONTEXT_TYPE = Type.getObjectType("com/libkperf/tracex/runtime/TraceRuntime$Context");
    private static final Type THROWABLE_TYPE = Type.getType(Throwable.class);
    private static final Type ERROR_TYPE = Type.getType(Error.class);
    private final String methodName;
    private final String methodDesc;
    private final String owner;
    private final List<TryRange> tryRanges = new ArrayList<TryRange>();
    private int contextLocal = -1;
    private Label currentTryStart;
    private boolean currentTryHasCode;
    private boolean injectingProbe;

    public TraceMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor, String owner) {
        super(api, mv, access, name, descriptor);
        this.methodName = name;
        this.owner = owner;
        this.methodDesc = descriptor;
    }

    @Override
    protected void onMethodEnter() {
        // TraceRuntime.Context context = TraceRuntime.enter(owner, methodName, methodDesc)
        injectingProbe = true;
        visitLdcInsn(owner);
        visitLdcInsn(methodName);
        visitLdcInsn(methodDesc);
        visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER, "enter",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Lcom/libkperf/tracex/runtime/TraceRuntime$Context;", false);
        contextLocal = newLocal(CONTEXT_TYPE);
        storeLocal(contextLocal);
        injectingProbe = false;
        beginTryRange();
    }

    @Override
    protected void onMethodExit(int opcode) {
    }

    // exception handling
    @Override
    public void visitInsn(int opcode) {
        if (isReturnOpcode(opcode)) {
            closeTryRange();
            emitExit();
            super.visitInsn(opcode);
            beginTryRange();
            return;
        }
        markProtectedCode();
        super.visitInsn(opcode);
    }

    @Override
    public void visitIntInsn(int opcode, int operand) {
        markProtectedCode();
        super.visitIntInsn(opcode, operand);
    }

    @Override
    public void visitVarInsn(int opcode, int var) {
        markProtectedCode();
        super.visitVarInsn(opcode, var);
    }

    @Override
    public void visitTypeInsn(int opcode, String type) {
        markProtectedCode();
        super.visitTypeInsn(opcode, type);
    }

    @Override
    public void visitFieldInsn(int opcode, String owner, String name, String descriptor) {
        markProtectedCode();
        super.visitFieldInsn(opcode, owner, name, descriptor);
    }

    @Override
    public void visitMethodInsn(int opcode, String owner, String name, String descriptor, boolean isInterface) {
        markProtectedCode();
        super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
    }

    @Override
    public void visitInvokeDynamicInsn(String name, String descriptor, Handle bootstrapMethodHandle,
                                       Object... bootstrapMethodArguments) {
        markProtectedCode();
        super.visitInvokeDynamicInsn(name, descriptor, bootstrapMethodHandle, bootstrapMethodArguments);
    }

    @Override
    public void visitJumpInsn(int opcode, Label label) {
        markProtectedCode();
        super.visitJumpInsn(opcode, label);
    }

    @Override
    public void visitLdcInsn(Object value) {
        markProtectedCode();
        super.visitLdcInsn(value);
    }

    @Override
    public void visitIincInsn(int var, int increment) {
        markProtectedCode();
        super.visitIincInsn(var, increment);
    }

    @Override
    public void visitTableSwitchInsn(int min, int max, Label dflt, Label... labels) {
        markProtectedCode();
        super.visitTableSwitchInsn(min, max, dflt, labels);
    }

    @Override
    public void visitLookupSwitchInsn(Label dflt, int[] keys, Label[] labels) {
        markProtectedCode();
        super.visitLookupSwitchInsn(dflt, keys, labels);
    }

    @Override
    public void visitMultiANewArrayInsn(String descriptor, int numDimensions) {
        markProtectedCode();
        super.visitMultiANewArrayInsn(descriptor, numDimensions);
    }

    @Override
    public void visitMaxs(int maxStack, int maxLocals) {
        closeTryRange();
        if (contextLocal >= 0 && !tryRanges.isEmpty()) {
            Label handler = new Label();
            for (TryRange range : tryRanges) {
                visitTryCatchBlock(range.start, range.end, handler, null);
            }
            visitLabel(handler);
            int throwableLocal = newLocal(THROWABLE_TYPE);
            storeLocal(throwableLocal);
            Label skipExit = new Label();
            loadLocal(throwableLocal);
            instanceOf(ERROR_TYPE);
            ifZCmp(NE, skipExit);
            emitExit();
            visitLabel(skipExit);
            loadLocal(throwableLocal);
            // Preserve exception semantics, including JVM monitor release for ACC_SYNCHRONIZED methods.
            injectingProbe = true;
            super.visitInsn(ATHROW);
            injectingProbe = false;
        }
        super.visitMaxs(maxStack, maxLocals);
    }

    private void beginTryRange() {
        currentTryStart = new Label();
        currentTryHasCode = false;
        visitLabel(currentTryStart);
    }

    private void closeTryRange() {
        if (currentTryStart == null) {
            return;
        }
        Label end = new Label();
        visitLabel(end);
        if (currentTryHasCode) {
            tryRanges.add(new TryRange(currentTryStart, end));
        }
        currentTryStart = null;
        currentTryHasCode = false;
    }

    private void markProtectedCode() {
        if (!injectingProbe && currentTryStart != null) {
            currentTryHasCode = true;
        }
    }

    private void emitExit() {
        if (contextLocal < 0) {
            return;
        }
        injectingProbe = true;
        loadLocal(contextLocal);
        visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER, "exit",
            "(Lcom/libkperf/tracex/runtime/TraceRuntime$Context;)V", false);
        injectingProbe = false;
    }

    private static boolean isReturnOpcode(int opcode) {
        return opcode == RETURN || opcode == IRETURN || opcode == LRETURN || opcode == FRETURN
            || opcode == DRETURN || opcode == ARETURN;
    }

    private static final class TryRange {
        private final Label start;
        private final Label end;

        TryRange(Label start, Label end) {
            this.start = start;
            this.end = end;
        }
    }
}
