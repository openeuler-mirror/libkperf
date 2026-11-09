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
 * Author: Zou
 * Create: 2026-10-20
 * Description: ASM AdviceAdapter that injects VTPhaseCollector calls at the virtual-thread phase
 * instrumentation points: runContinuation entry, topLevelExec dequeue, poll exit, wakeup around unpark
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Type;
import org.objectweb.asm.commons.AdviceAdapter;

public final class VTPhaseMethodVisitor extends AdviceAdapter {

    private static final String LOCKSUPPORT_OWNER = "java/util/concurrent/locks/LockSupport";
    private static final String LOCKSUPPORT_UNPARK = "unpark";
    private static final String LOCKSUPPORT_UNPARK_DESC = "(Ljava/lang/Thread;)V";

    private final int instrumentationType;
    private final int taskLocal;

    private int savedThreadLocal = -1;
    private int socketBytesLocal = -1;
    private boolean injecting;

    VTPhaseMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor,
                         int instrumentationType) {
        super(api, mv, access, name, descriptor);
        this.instrumentationType = instrumentationType;
        this.taskLocal = 1;
    }

    @Override
    protected void onMethodEnter() {
        switch (instrumentationType) {
            case VTPhaseInstrumentation.TYPE_RUN:
                emitOnRun();
                break;
            case VTPhaseInstrumentation.TYPE_DEQUEUE:
                emitOnDequeue();
                break;
            case VTPhaseInstrumentation.TYPE_PARK:
            case VTPhaseInstrumentation.TYPE_PARK_NANOS:
                emitOnParkEnter();
                break;
            default:
                break;
        }
    }

    @Override
    protected void onMethodExit(int opcode) {
        if (instrumentationType == VTPhaseInstrumentation.TYPE_POLL_RETURN
                && opcode == RETURN) {
            emitOnPollReturn();
        } else if (instrumentationType == VTPhaseInstrumentation.TYPE_SOCKET_READ
                && opcode == IRETURN) {
            emitOnSocketReadExit();
        } else if (instrumentationType == VTPhaseInstrumentation.TYPE_NIO_SOCKET_READ
                && opcode == IRETURN) {
            emitOnNioSocketIoExit("onSocketRead");
        } else if (instrumentationType == VTPhaseInstrumentation.TYPE_NIO_SOCKET_WRITE
                && opcode == IRETURN) {
            emitOnNioSocketIoExit("onSocketWrite");
        } else if ((instrumentationType == VTPhaseInstrumentation.TYPE_PARK
                || instrumentationType == VTPhaseInstrumentation.TYPE_PARK_NANOS)
                && opcode == RETURN) {
            emitOnParkReturn();
        } else if (instrumentationType == VTPhaseInstrumentation.TYPE_UNPARK
                && opcode == RETURN) {
            emitOnUnparkReturn();
        }
    }

    @Override
    public void visitVarInsn(int opcode, int var) {
        super.visitVarInsn(opcode, var);
        if (instrumentationType == VTPhaseInstrumentation.TYPE_DEQUEUE
                && opcode == ASTORE && var == taskLocal && !injecting) {
            emitOnDequeue();
        }
    }

    @Override
    public void visitMethodInsn(int opcode, String owner, String name, String descriptor, boolean isInterface) {
        if (instrumentationType == VTPhaseInstrumentation.TYPE_WAKEUP
                && opcode == INVOKESTATIC
                && LOCKSUPPORT_OWNER.equals(owner)
                && LOCKSUPPORT_UNPARK.equals(name)
                && LOCKSUPPORT_UNPARK_DESC.equals(descriptor)
                && !injecting) {
            emitWakeupAroundUnpark();
            return;
        }
        super.visitMethodInsn(opcode, owner, name, descriptor, isInterface);
    }

    private void emitCarrierThreadGuard(Label skip) {
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitTypeInsn(INSTANCEOF, VTPhaseInstrumentation.CARRIER_THREAD);
        ifZCmp(EQ, skip);
    }

    private void emitVirtualThreadGuard(Label skip) {
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_IS_VIRTUAL,
                VTPhaseInstrumentation.THREAD_IS_VIRTUAL_DESC, false);
        ifZCmp(EQ, skip);
    }

    private void emitCurrentThreadThreadId() {
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD,
                VTPhaseInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_ID, VTPhaseInstrumentation.THREAD_ID_DESC, false);
    }

    private void emitOnRun() {
        injecting = true;
        Label skip = new Label();
        emitCarrierThreadGuard(skip);
        loadThis();
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_ID, VTPhaseInstrumentation.THREAD_ID_DESC, false);
        emitCurrentThreadThreadId();
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onRun", "(JJ)V", false);
        visitLabel(skip);
        injecting = false;
    }

    private void emitOnDequeue() {
        injecting = true;
        Label skip = new Label();
        emitCarrierThreadGuard(skip);
        visitVarInsn(ALOAD, taskLocal);
        ifNull(skip);
        emitCurrentThreadThreadId();
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onDequeue", "(J)V", false);
        visitLabel(skip);
        injecting = false;
    }

    private void emitThisThreadId(String collectorMethod) {
        injecting = true;
        loadThis();
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_ID, VTPhaseInstrumentation.THREAD_ID_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                collectorMethod, "(J)V", false);
        injecting = false;
    }

    private void emitOnParkEnter() {
        emitThisThreadId("onParkEnter");
    }

    private void emitOnParkReturn() {
        emitThisThreadId("onParkReturn");
    }

    private void emitOnUnparkReturn() {
        emitThisThreadId("onUnparkReturn");
    }

    private void emitOnPollReturn() {
        injecting = true;
        emitCurrentThreadThreadId();
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onPollReturn", "(J)V", false);
        injecting = false;
    }

    private void emitWakeupAroundUnpark() {
        injecting = true;
        if (savedThreadLocal < 0) {
            savedThreadLocal = newLocal(Type.getType("Ljava/lang/Thread;"));
        }
        dup();
        storeLocal(savedThreadLocal);
        dup();
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_ID, VTPhaseInstrumentation.THREAD_ID_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onCreateRegistration", "(J)V", false);
        super.visitMethodInsn(INVOKESTATIC, LOCKSUPPORT_OWNER, LOCKSUPPORT_UNPARK,
                LOCKSUPPORT_UNPARK_DESC, false);
        loadLocal(savedThreadLocal);
        visitMethodInsn(INVOKEVIRTUAL, VTPhaseInstrumentation.THREAD_OWNER,
                VTPhaseInstrumentation.THREAD_ID, VTPhaseInstrumentation.THREAD_ID_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onUnparkReturn", "(J)V", false);
        injecting = false;
    }

    private void emitOnSocketReadExit() {
        injecting = true;
        if (socketBytesLocal < 0) {
            socketBytesLocal = newLocal(Type.INT_TYPE);
        }
        Label skip = new Label();
        // The int return value (bytes read) is on top of the stack at exit; keep it for
        // the real IRETURN while also stashing a copy into a local for the collector arg.
        visitInsn(DUP);
        visitVarInsn(ISTORE, socketBytesLocal);
        emitVirtualThreadGuard(skip);
        // args: (long vtId, int fd, int bytes)
        emitCurrentThreadThreadId();
        loadThis();
        visitFieldInsn(GETFIELD, VTPhaseInstrumentation.SOCKET_CHANNEL_OWNER,
                VTPhaseInstrumentation.SOCKET_FD_FIELD, VTPhaseInstrumentation.SOCKET_FD_FIELD_DESC);
        visitVarInsn(ILOAD, socketBytesLocal);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                "onSocketRead", "(JII)V", false);
        visitLabel(skip);
        injecting = false;
    }

    private void emitOnNioSocketIoExit(String collectorMethod) {
        injecting = true;
        if (socketBytesLocal < 0) {
            socketBytesLocal = newLocal(Type.INT_TYPE);
        }
        Label skip = new Label();
        // int return value (bytes read/written) sits on top of the operand stack at IRETURN.
        visitInsn(DUP);
        visitVarInsn(ISTORE, socketBytesLocal);
        emitVirtualThreadGuard(skip);
        // args: (long vtId, int fd, int bytes). NioSocketImpl keeps fd as a FileDescriptor
        // field and exposes the raw int via its private static fdVal(FileDescriptor).
        emitCurrentThreadThreadId();
        loadThis();
        visitFieldInsn(GETFIELD, VTPhaseInstrumentation.NIO_SOCKET_OWNER,
                VTPhaseInstrumentation.NIO_FD_FIELD, VTPhaseInstrumentation.NIO_FD_FIELD_DESC);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.NIO_SOCKET_OWNER,
                VTPhaseInstrumentation.NIO_FDVAL_METHOD, VTPhaseInstrumentation.NIO_FDVAL_DESC, false);
        visitVarInsn(ILOAD, socketBytesLocal);
        visitMethodInsn(INVOKESTATIC, VTPhaseInstrumentation.VT_PHASE_COLLECTOR_OWNER,
                collectorMethod, "(JII)V", false);
        visitLabel(skip);
        injecting = false;
    }
}
