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
 * Description: Native library for fetching OS thread ID via gettid() syscall, exposed to Java via JNI
 ******************************************************************************/
#include <jni.h>
#include <sched.h>
#include <time.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

JNIEXPORT jint JNICALL
Java_com_libkperf_tracex_runtime_NativeThreadInfo_currentTid0(JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
    return (jint) syscall(SYS_gettid);
}

JNIEXPORT jint JNICALL
Java_com_libkperf_tracex_runtime_NativeThreadInfo_currentCpu0(JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
    int cpu = sched_getcpu();
    return cpu < 0 ? -1 : (jint) cpu;
}

JNIEXPORT jlong JNICALL
Java_com_libkperf_tracex_runtime_NativeThreadInfo_currentTimeNanos0(JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (jlong) ts.tv_sec * 1000000000LL + (jlong) ts.tv_nsec;
}
