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
 * Description: ASM AdviceAdapter that injects VTStageCollector calls at the virtual-thread stage
 * instrumentation points: runContinuation entry, topLevelExec dequeue, socket read/write exit,
 * park/parkNanos enter and exit, unpark exit
 ******************************************************************************/
package com.libkperf.tracex.agent.asm;

import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Type;
import org.objectweb.asm.commons.AdviceAdapter;

public final class VTStageMethodVisitor extends AdviceAdapter {

    private final int instrumentationType;
    private final int taskLocal;

    private int carrierThreadLocal = -1;
    private int socketBytesLocal = -1;
    private boolean injecting;

    VTStageMethodVisitor(int api, MethodVisitor mv, int access, String name, String descriptor,
                         int instrumentationType) {
        super(api, mv, access, name, descriptor);
        this.instrumentationType = instrumentationType;
        this.taskLocal = 1;
    }

    @Override
    protected void onMethodEnter() {
        switch (instrumentationType) {
            case VTStageInstrumentation.TYPE_RUN:
                emitOnRun();
                break;
            case VTStageInstrumentation.TYPE_DEQUEUE:
                emitOnDequeue();
                break;
            case VTStageInstrumentation.TYPE_PARK:
            case VTStageInstrumentation.TYPE_PARK_NANOS:
                emitOnParkEnter();
                break;
            default:
                break;
        }
    }

    @Override
    protected void onMethodExit(int opcode) {
        if (instrumentationType == VTStageInstrumentation.TYPE_SOCKET_READ
                && opcode == IRETURN) {
            emitOnSocketReadExit();
        } else if (instrumentationType == VTStageInstrumentation.TYPE_NIO_SOCKET_READ
                && opcode == IRETURN) {
            emitOnNioSocketIoExit("onSocketRead");
        } else if (instrumentationType == VTStageInstrumentation.TYPE_NIO_SOCKET_WRITE
                && opcode == IRETURN) {
            emitOnNioSocketIoExit("onSocketWrite");
        } else if ((instrumentationType == VTStageInstrumentation.TYPE_PARK
                || instrumentationType == VTStageInstrumentation.TYPE_PARK_NANOS)
                && opcode == RETURN) {
            emitOnParkReturn();
        } else if (instrumentationType == VTStageInstrumentation.TYPE_UNPARK
                && opcode == RETURN) {
            emitOnUnparkReturn();
        }
    }

    @Override
    public void visitVarInsn(int opcode, int var) {
        super.visitVarInsn(opcode, var);
        if (instrumentationType == VTStageInstrumentation.TYPE_DEQUEUE
                && opcode == ASTORE && var == taskLocal && !injecting) {
            emitOnDequeue();
        }
    }

    private void emitCarrierThreadGuard(Label skip) {
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_CURRENT_THREAD,
                VTStageInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitTypeInsn(INSTANCEOF, VTStageInstrumentation.CARRIER_THREAD);
        ifZCmp(EQ, skip);
    }

    private void emitVirtualThreadGuard(Label skip) {
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_CURRENT_THREAD,
                VTStageInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_IS_VIRTUAL,
                VTStageInstrumentation.THREAD_IS_VIRTUAL_DESC, false);
        ifZCmp(EQ, skip);
    }

    private void emitCurrentThreadThreadId() {
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_CURRENT_THREAD,
                VTStageInstrumentation.THREAD_CURRENT_THREAD_DESC, false);
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_ID, VTStageInstrumentation.THREAD_ID_DESC, false);
    }

    private void emitOnRun() {
        injecting = true;
        Label skip = new Label();
        emitCarrierThreadGuard(skip);
        loadThis();
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_ID, VTStageInstrumentation.THREAD_ID_DESC, false);
        emitCurrentThreadThreadId();
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
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
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
                "onDequeue", "(J)V", false);
        visitLabel(skip);
        injecting = false;
    }

    private void emitThisThreadId(String collectorMethod) {
        injecting = true;
        loadThis();
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_ID, VTStageInstrumentation.THREAD_ID_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
                collectorMethod, "(J)V", false);
        injecting = false;
    }

    private void emitOnParkEnter() {
        emitParkCallback("onParkEnter");
    }

    private void emitOnParkReturn() {
        emitParkCallback("onParkReturn");
    }

    // args: (long vtId, long carrierId, String carrierName)
    private void emitParkCallback(String collectorMethod) {
        injecting = true;
        if (carrierThreadLocal < 0) {
            carrierThreadLocal = newLocal(Type.getType("Ljava/lang/Thread;"));
        }
        loadThis();
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_ID, VTStageInstrumentation.THREAD_ID_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_CURRENT_CARRIER,
                VTStageInstrumentation.THREAD_CURRENT_CARRIER_DESC, false);
        dup();
        visitVarInsn(ASTORE, carrierThreadLocal);
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_ID, VTStageInstrumentation.THREAD_ID_DESC, false);
        visitVarInsn(ALOAD, carrierThreadLocal);
        visitMethodInsn(INVOKEVIRTUAL, VTStageInstrumentation.THREAD_OWNER,
                VTStageInstrumentation.THREAD_GET_NAME, VTStageInstrumentation.THREAD_GET_NAME_DESC, false);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
                collectorMethod, "(JJLjava/lang/String;)V", false);
        injecting = false;
    }

    private void emitOnUnparkReturn() {
        emitThisThreadId("onUnparkReturn");
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
        visitFieldInsn(GETFIELD, VTStageInstrumentation.SOCKET_CHANNEL_OWNER,
                VTStageInstrumentation.SOCKET_FD_FIELD, VTStageInstrumentation.SOCKET_FD_FIELD_DESC);
        visitVarInsn(ILOAD, socketBytesLocal);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
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
        visitFieldInsn(GETFIELD, VTStageInstrumentation.NIO_SOCKET_OWNER,
                VTStageInstrumentation.NIO_FD_FIELD, VTStageInstrumentation.NIO_FD_FIELD_DESC);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.NIO_SOCKET_OWNER,
                VTStageInstrumentation.NIO_FDVAL_METHOD, VTStageInstrumentation.NIO_FDVAL_DESC, false);
        visitVarInsn(ILOAD, socketBytesLocal);
        visitMethodInsn(INVOKESTATIC, VTStageInstrumentation.VT_STAGE_COLLECTOR_OWNER,
                collectorMethod, "(JII)V", false);
        visitLabel(skip);
        injecting = false;
    }
}
