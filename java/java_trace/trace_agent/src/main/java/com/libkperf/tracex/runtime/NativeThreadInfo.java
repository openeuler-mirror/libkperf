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
 * Description: JNI bridge for retrieving native thread IDs (tid) and CPU ID from the JVM runtime
 ******************************************************************************/
package com.libkperf.tracex.runtime;

public final class NativeThreadInfo {

    private static volatile boolean loaded = false;
    private static volatile boolean loadTried = false;

    private NativeThreadInfo() {
    }

    public static synchronized void load(String absolutePath) {
        if (loaded || loadTried) {
            return;
        }

        loadTried = true;

        if (absolutePath == null || absolutePath.isEmpty()) {
            System.err.println("[java-trace-agent] native lib path is empty, fallback to Java thread id/time");
            return;
        }

        try {
            System.load(absolutePath);
            loaded = true;
            System.err.println("[java-trace-agent] native thread info loaded: " + absolutePath);
        } catch (Throwable t) {
            loaded = false;
            System.err.println("[java-trace-agent] native thread info load failed: " + absolutePath + ", ex=" + t);
        }
    }

    public static int currentTidSafe() {
        if (!loaded) {
            return (int) Thread.currentThread().getId();
        }

        try {
            return currentTid0();
        } catch (Throwable t) {
            return (int) Thread.currentThread().getId();
        }
    }

    public static int currentCpuSafe() {
        if (!loaded) {
            return -1;
        }

        try {
            return currentCpu0();
        } catch (Throwable t) {
            return -1;
        }
    }

    public static long currentTimeNanosSafe() {
        if (!loaded) {
            return System.nanoTime();
        }

        try {
            return currentTimeNanos0();
        } catch (Throwable t) {
            return System.nanoTime();
        }
    }

    private static native int currentTid0();

    private static native int currentCpu0();

    private static native long currentTimeNanos0();
}