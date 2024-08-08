/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Used for code debugging.
 ******************************************************************************/
#ifndef PERF_LOG_H
#define PERF_LOG_H

#include <cstring>
#include <cstdlib>

#ifdef NDEBUG
#define DBG_PRINT(...)
#define ERR_PRINT(...)
#else
#define DBG_PRINT(...) if (g_perfDebug) fprintf(stdout, __VA_ARGS__)
#define ERR_PRINT(...) if (g_perfDebug) fprintf(stderr, __VA_ARGS__)

static int g_perfDebug = 0;

__attribute__((constructor)) static void InitLog()
{
    auto debugEnv = getenv("PERF_DEBUG");
    if (debugEnv != nullptr && strcmp(debugEnv, "1") == 0) {
        g_perfDebug = 1;
    }
}
#endif

#endif