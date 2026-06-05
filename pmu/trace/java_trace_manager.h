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
 * Description: Java trace session manager: open/enable/disable/read/close per pd
 ******************************************************************************/

#pragma once

#include "java_backend.h"
#include "pmu.h"
#include <cstddef>
#include <unordered_map>

class JavaTraceManager {
public:
    static JavaTraceManager &GetInstance()
    {
        static JavaTraceManager instance;
        return instance;
    }

    JavaTraceManager(const JavaTraceManager &) = delete;
    JavaTraceManager &operator=(const JavaTraceManager &) = delete;

    int Open(int pd, int pid, const char *includeRules);

    int Enable(int pd);

    int Disable(int pd);

    int Read(int pd, struct UTraceData **outData, int *outLen);
    void FreeData(struct UTraceData *data);

    void Close(int pd);

private:
    JavaTraceManager() = default;
    ~JavaTraceManager() = default;

    struct JavaSession {
        JavaBackendImpl *backend{nullptr};
        int pid{0};
    };

    std::unordered_map<int, JavaSession> sessions_;
};
