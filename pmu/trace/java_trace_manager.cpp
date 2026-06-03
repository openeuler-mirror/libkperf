/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MEretHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-04-27
 * Description: Java trace session manager implementation delegating to JavaBackend
 ******************************************************************************/

#include "java_trace_manager.h"
#include "pcerr.h"
#include <cstring>

int JavaTraceManager::Open(int pd, int pid, const char *includeRules)
{
    auto *backend = new JavaBackendImpl();
    int ret = JavaBackendOpen(backend, pid, includeRules);
    if (ret != 0) {
        delete backend;
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java backend open failed, ret=" + std::to_string(ret));
        return -1;
    }

    JavaSession session;
    session.backend = backend;
    session.pid = pid;
    sessions_[pd] = session;
    return 0;
}

int JavaTraceManager::Enable(int pd)
{
    auto it = sessions_.find(pd);
    if (it == sessions_.end()) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java trace session not found for pd=" + std::to_string(pd));
        return -1;
    }

    int ret = JavaBackendEnable(it->second.backend);
    if (ret != 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java backend enable failed, ret=" + std::to_string(ret));
        return -1;
    }
    return 0;
}

int JavaTraceManager::Disable(int pd)
{
    auto it = sessions_.find(pd);
    if (it == sessions_.end()) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java trace session not found for pd=" + std::to_string(pd));
        return -1;
    }

    int ret = JavaBackendDisable(it->second.backend);
    if (ret != 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java backend disable failed, ret=" + std::to_string(ret));
        return -1;
    }
    return 0;
}

int JavaTraceManager::Read(int pd, struct UTraceData **outData, int *outLen)
{
    auto it = sessions_.find(pd);
    if (it == sessions_.end()) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java trace session not found for pd=" + std::to_string(pd));
        return -1;
    }

    UTraceData *data = nullptr;
    size_t count = 0;
    int ret = JavaBackendRead(it->second.backend, &data, &count);
    if (ret != 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_JAVA_PROCESS_FAILED, "Java backend read failed, ret=" + std::to_string(ret));
        return -1;
    }
    *outData = data;
    *outLen = static_cast<int>(count);
    return 0;
}

void JavaTraceManager::FreeData(struct UTraceData *data)
{
    JavaBackendDataFree(data);
}

void JavaTraceManager::Close(int pd)
{
    auto it = sessions_.find(pd);
    if (it == sessions_.end()) {
        return;
    }
    JavaBackendClose(it->second.backend);
    delete it->second.backend;
    sessions_.erase(it);
}
