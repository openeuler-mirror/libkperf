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
 * Description: Java trace backend: shared memory management and JVM agent injection
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct JavaBackendTarget {
    std::string filterConfigPath;
    std::string agentJarPath;
    std::string nativeLibPath;
    std::string assetDirHost;
    std::string shmPath;
    std::string logPath;
};

struct JavaBackendImpl {
    int pid{};
    std::string includeRules;
    unsigned slotCount{524288};
    std::string filterConfigPath;
    std::string shmName;
    std::string shmPath;
    JavaBackendTarget target;
    int shmFd{-1};
    size_t shmSize{0};
    void *mapped{nullptr};
    uint64_t readSeq{0};
    bool runtimeStopped = false;
    bool runtimeRestored = false;
};

int JavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules);
int JavaBackendEnable(JavaBackendImpl *impl);
int JavaBackendDisable(JavaBackendImpl *impl);
int JavaBackendRead(JavaBackendImpl *impl, struct UTraceData **out_data, size_t *out_count);
void JavaBackendDataFree(struct UTraceData *data);
void JavaBackendClose(JavaBackendImpl *impl);
