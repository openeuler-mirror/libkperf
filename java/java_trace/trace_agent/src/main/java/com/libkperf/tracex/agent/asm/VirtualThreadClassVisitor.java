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
 * Description: Instruments HotSpot virtual-thread mount/unmount transitions
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.commons.AdviceAdapter;

/**
 * Example:
 * private void mount() {
 *     // JDK implementation
 * }
 *
 * private void unmount() {
 *     // JDK implementation
 * }
 *
 * After transformation:
 *
 * private void mount() {
 *     // JDK implementation
 *     TraceRuntime.virtualThreadMount(this);
 * }
 * 
 * private void unmount() {
 *     TraceRuntime.virtualThreadUnmount(this);
 *     // JDK implementation
 * }
 */
final class VirtualThreadClassVisitor extends ClassVisitor {
    private static final String METHOD_DESCRIPTOR = "()V";
    private static final String THREAD_DESCRIPTOR = "(Ljava/lang/Thread;)V";
    private final HookMode hookMode;
    private int instrumented;

    enum HookMode {
        NONE,
        MOUNT_UNMOUNT,
        RUN_CONTINUATION
    }

    VirtualThreadClassVisitor(ClassVisitor visitor, HookMode hookMode) {
        super(Opcodes.ASM9, visitor);
        this.hookMode = hookMode;
    }

    static HookMode detectHookMode(ClassReader reader) {
        StructureDetector detector = new StructureDetector();
        reader.accept(detector, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        if (detector.supportsMountUnmount()) {
            return HookMode.MOUNT_UNMOUNT;
        }
        if (detector.supportsRunContinuation()) {
            return HookMode.RUN_CONTINUATION;
        }
        return HookMode.NONE;
    }

    @Override
    public MethodVisitor visitMethod(int access, String name, String descriptor, String signature, String[] exceptions) {
        MethodVisitor next = super.visitMethod(access, name, descriptor, signature, exceptions);
        if (next == null || !isInstrumentable(access, descriptor)) {
            return next;
        }

        final boolean mount;
        final boolean runContinuation;
        if (hookMode == HookMode.MOUNT_UNMOUNT && ("mount".equals(name) || "unmount".equals(name))) {
            mount = "mount".equals(name);
            runContinuation = false;
        } else if (hookMode == HookMode.RUN_CONTINUATION && "runContinuation".equals(name)) {
            mount = false;
            runContinuation = true;
        } else {
            return next;
        }

        instrumented++;
        return new AdviceAdapter(api, next, access, name, descriptor) {
            @Override
            protected void onMethodEnter() {
                // runContinuation() is only used when precise mount/unmount hooks are unavailable.
                if (runContinuation) {
                    emitTransition("virtualThreadMount");
                } else if (!mount) {
                    // enter unmount() and this VT no longer belongs to the current carrier
                    emitTransition("virtualThreadUnmount");
                }
            }

            @Override
            protected void onMethodExit(int opcode) {
                if (runContinuation) {
                    emitTransition("virtualThreadUnmount");
                } else if (mount && opcode == RETURN) {
                    // after mount() and this VT has been successfully bound to the carrier
                    emitTransition("virtualThreadMount");
                }
            }

            private void emitTransition(String runtimeMethod) {
                loadThis();
                // TraceRuntime.virtualThreadMount(this);
                visitMethodInsn(INVOKESTATIC, TraceClassFileTransformer.TRACE_RUNTIME_OWNER,
                        runtimeMethod, THREAD_DESCRIPTOR, false);
            }
        };
    }

    int instrumentedMethodCount() {
        return instrumented;
    }

    private static boolean isInstrumentable(int access, String descriptor) {
        return (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) == 0
                && METHOD_DESCRIPTOR.equals(descriptor);
    }

    private static final class StructureDetector extends ClassVisitor {
        private boolean hasMount;
        private boolean hasUnmount;
        private boolean hasRunContinuation;

        StructureDetector() {
            super(Opcodes.ASM9);
        }

        @Override
        public MethodVisitor visitMethod(int access, String name, String descriptor,
                                         String signature, String[] exceptions) {
            if (!isInstrumentable(access, descriptor)) {
                return null;
            }
            if ("mount".equals(name)) {
                hasMount = true;
            } else if ("unmount".equals(name)) {
                hasUnmount = true;
            } else if ("runContinuation".equals(name)) {
                hasRunContinuation = true;
            }
            return null;
        }

        boolean supportsMountUnmount() {
            return hasMount && hasUnmount;
        }

        boolean supportsRunContinuation() {
            return hasRunContinuation;
        }
    }
}
