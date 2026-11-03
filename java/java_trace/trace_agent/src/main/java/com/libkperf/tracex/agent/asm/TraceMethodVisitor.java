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

import com.libkperf.tracex.runtime.TraceRuntime;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.Handle;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
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
 *     TraceRuntime.Context context = TraceRuntime.enter(methodId);
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
    private static final String VIRTUAL_THREAD_OWNER = "java/lang/VirtualThread";
    private static final String METHOD_DESCRIPTOR = "()V";
    private static final String THREAD_DESCRIPTOR = "(Ljava/lang/Thread;)V";
    private static final int VT_PHASE_NONE = 0;
    private final boolean enableTrace;
    private final int vtType;
    private final VirtualThreadInstrumentation virtualThreadInstrumentation;
    private final String moduleName;
    private final String functionName;
    private final int methodId;
    private final List<TryRange> tryRanges = new ArrayList<TryRange>();
    private int contextLocal = -1;
    private Label currentTryStart;
    private boolean currentTryHasCode;
    private boolean injectingProbe;

    public TraceMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor, String owner) {
        this(api, mv, access, name, descriptor, owner, true, VT_PHASE_NONE);
    }

    TraceMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor,
                       String owner, boolean enableTrace, int vtType) {
        this(api, mv, access, name, descriptor, owner, enableTrace, vtType, null);
    }

    private TraceMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor,
                               String owner, boolean enableTrace, int vtType,
                               VirtualThreadInstrumentation virtualThreadInstrumentation) {
        super(api, mv, access, name, descriptor);
        this.enableTrace = enableTrace;
        this.vtType = vtType;
        this.virtualThreadInstrumentation = virtualThreadInstrumentation;
        this.moduleName = owner.replace('/', '.');
        this.functionName = name + descriptor;
        this.methodId = enableTrace ? TraceRuntime.registerMethod(moduleName, functionName) : -1;
    }

    @Override
    protected void onMethodEnter() {
        if (enableTrace) {
            injectingProbe = true;
            visitLdcInsn(methodId);
            visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER, "enter",
                "(I)Lcom/libkperf/tracex/runtime/TraceRuntime$Context;", false);
            contextLocal = newLocal(CONTEXT_TYPE);
            storeLocal(contextLocal);
            injectingProbe = false;
            beginTryRange();
        }
    }

    @Override
    protected void onMethodExit(int opcode) {
    }

    // exception handling
    @Override
    public void visitInsn(int opcode) {
        if (enableTrace && isReturnOpcode(opcode)) {
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
        if (virtualThreadInstrumentation != null && isTransitionCall(opcode, owner, name, descriptor)) {
            if ("mount".equals(name)) {
                super.visitInsn(DUP);
                emitVirtualThreadTransition("virtualThreadBeforeMount");
                virtualThreadInstrumentation.mountCallSites++;
                super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
                return;
            }
            super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
            super.visitVarInsn(ALOAD, 0);
            emitVirtualThreadTransition("virtualThreadAfterUnmount");
            virtualThreadInstrumentation.unmountCallSites++;
            return;
        }
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
        if (enableTrace) {
            closeTryRange();
            if (!tryRanges.isEmpty()) {
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

    private void emitVirtualThreadTransition(String runtimeMethod) {
        super.visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER,
                runtimeMethod, THREAD_DESCRIPTOR, false);
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

    static VirtualThreadInstrumentation instrumentVirtualThread(ClassReader reader, ClassVisitor visitor) {
        final VirtualThreadInstrumentation result = new VirtualThreadInstrumentation();
        reader.accept(new ClassVisitor(Opcodes.ASM9, visitor) {
            @Override
            public MethodVisitor visitMethod(int access, String name, String descriptor,
                                             String signature, String[] exceptions) {
                MethodVisitor next = super.visitMethod(access, name, descriptor, signature, exceptions);
                if (next == null || !isRunContinuation(access, name, descriptor)) {
                    return next;
                }
                result.instrumentedMethods++;
                return new TraceMethodVisitor(api, next, access, name, descriptor,
                        VIRTUAL_THREAD_OWNER, false, VT_PHASE_NONE, result);
            }
        }, ClassReader.EXPAND_FRAMES);
        return result;
    }

    static boolean supportsVirtualThreadCarrierCallSites(ClassReader reader) {
        CallSiteDetector detector = new CallSiteDetector();
        reader.accept(detector, ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        return detector.mountCallSites > 0 && detector.unmountCallSites > 0;
    }

    private static boolean isRunContinuation(int access, String name, String descriptor) {
        return (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE | Opcodes.ACC_STATIC)) == 0
                && "runContinuation".equals(name) && METHOD_DESCRIPTOR.equals(descriptor);
    }

    private static boolean isTransitionCall(int opcode, String owner, String name, String descriptor) {
        return opcode != Opcodes.INVOKESTATIC
                && VIRTUAL_THREAD_OWNER.equals(owner)
                && METHOD_DESCRIPTOR.equals(descriptor)
                && ("mount".equals(name) || "unmount".equals(name));
    }

    private static boolean isReturnOpcode(int opcode) {
        return opcode == RETURN || opcode == IRETURN || opcode == LRETURN || opcode == FRETURN
            || opcode == DRETURN || opcode == ARETURN;
    }

    private static final class CallSiteDetector extends ClassVisitor {
        private int mountCallSites;
        private int unmountCallSites;

        CallSiteDetector() {
            super(Opcodes.ASM9);
        }

        @Override
        public MethodVisitor visitMethod(int access, String name, String descriptor,
                                         String signature, String[] exceptions) {
            if (!isRunContinuation(access, name, descriptor)) {
                return null;
            }
            return new MethodVisitor(api) {
                @Override
                public void visitMethodInsn(int opcode, String owner, String methodName,
                                            String methodDescriptor, boolean isInterface) {
                    if (!isTransitionCall(opcode, owner, methodName, methodDescriptor)) {
                        return;
                    }
                    if ("mount".equals(methodName)) {
                        mountCallSites++;
                    } else {
                        unmountCallSites++;
                    }
                }
            };
        }
    }

    static final class VirtualThreadInstrumentation {
        private int instrumentedMethods;
        private int mountCallSites;
        private int unmountCallSites;

        int instrumentedMethodCount() {
            return instrumentedMethods;
        }

        int mountCallSiteCount() {
            return mountCallSites;
        }

        int unmountCallSiteCount() {
            return unmountCallSites;
        }
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
