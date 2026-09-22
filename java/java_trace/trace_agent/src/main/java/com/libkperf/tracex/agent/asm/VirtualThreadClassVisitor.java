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
 * Create: 2026-09-21
 * Description: Instruments HotSpot virtual-thread mount/unmount call sites
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * Hooks the carrier-side call sites in VirtualThread.runContinuation():
 *
 * <pre>
 * TraceRuntime.virtualThreadBeforeMount(this);
 * mount();
 * ...
 * unmount();
 * TraceRuntime.virtualThreadAfterUnmount(this);
 * </pre>
 *
 * The runtime callbacks therefore execute outside the HotSpot VTMS transition.
 */
final class VirtualThreadClassVisitor extends ClassVisitor {
    private static final String VIRTUAL_THREAD_OWNER = "java/lang/VirtualThread";
    private static final String METHOD_DESCRIPTOR = "()V";
    private static final String THREAD_DESCRIPTOR = "(Ljava/lang/Thread;)V";

    private int instrumentedMethods;
    private int mountCallSites;
    private int unmountCallSites;

    VirtualThreadClassVisitor(ClassVisitor visitor) {
        super(Opcodes.ASM9, visitor);
    }

    static boolean supportsCarrierCallSites(ClassReader reader) {
        CallSiteDetector detector = new CallSiteDetector();
        reader.accept(detector, ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        return detector.mountCallSites > 0 && detector.unmountCallSites > 0;
    }

    @Override
    public MethodVisitor visitMethod(int access, String name, String descriptor,
                                     String signature, String[] exceptions) {
        MethodVisitor next = super.visitMethod(access, name, descriptor, signature, exceptions);
        if (next == null || !isRunContinuation(access, name, descriptor)) {
            return next;
        }

        instrumentedMethods++;
        return new MethodVisitor(api, next) {
            @Override
            public void visitMethodInsn(int opcode, String owner, String methodName,
                                        String methodDescriptor, boolean isInterface) {
                if (isTransitionCall(opcode, owner, methodName, methodDescriptor)) {
                    if ("mount".equals(methodName)) {
                        // Keep the receiver for mount(), and pass the duplicate to the callback.
                        super.visitInsn(Opcodes.DUP);
                        emitTransition("virtualThreadBeforeMount");
                        mountCallSites++;
                        super.visitMethodInsn(opcode, owner, methodName, methodDescriptor, isInterface);
                        return;
                    }

                    // Keep one receiver across unmount(), whose ()V invocation consumes the other.
                    super.visitInsn(Opcodes.DUP);
                    super.visitMethodInsn(opcode, owner, methodName, methodDescriptor, isInterface);
                    emitTransition("virtualThreadAfterUnmount");
                    unmountCallSites++;
                    return;
                }
                super.visitMethodInsn(opcode, owner, methodName, methodDescriptor, isInterface);
            }

            private void emitTransition(String runtimeMethod) {
                super.visitMethodInsn(Opcodes.INVOKESTATIC,
                        TraceClassFileTransformer.TRACE_RUNTIME_OWNER,
                        runtimeMethod, THREAD_DESCRIPTOR, false);
            }
        };
    }

    int instrumentedMethodCount() {
        return instrumentedMethods;
    }

    int mountCallSiteCount() {
        return mountCallSites;
    }

    int unmountCallSiteCount() {
        return unmountCallSites;
    }

    private static boolean isRunContinuation(int access, String name, String descriptor) {
        return (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE | Opcodes.ACC_STATIC)) == 0
                && "runContinuation".equals(name)
                && METHOD_DESCRIPTOR.equals(descriptor);
    }

    private static boolean isTransitionCall(int opcode, String owner, String name, String descriptor) {
        return opcode != Opcodes.INVOKESTATIC
                && VIRTUAL_THREAD_OWNER.equals(owner)
                && METHOD_DESCRIPTOR.equals(descriptor)
                && ("mount".equals(name) || "unmount".equals(name));
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
}
