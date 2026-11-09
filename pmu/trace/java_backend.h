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
#include <sys/types.h>
#include <unordered_map>
#include <vector>

struct JavaBackendTarget {
    std::string filterConfigPath;
    std::string agentJarPath;
    std::string nativeLibPath;
    std::string assetDirHost;
    std::string shmPath;
    std::string logPath;
    uid_t fsUid{};
    gid_t fsGid{};
    bool credentialsValid{false};
};

struct JavaBackendImpl {
    int pid{};
    std::string includeRules;
    // ABI v4 ring configuration: one ring per target CPU, sized by slotsPerRing.
    unsigned ringCount{0};
    unsigned slotsPerRing{65536};
    unsigned consumerThreads{1};
    size_t descOffset{0};
    size_t eventOffset{0};
    size_t ringDataBytes{0};
    std::string filterConfigPath;
    std::string shmName;
    std::string shmPath;
    JavaBackendTarget target;
    int shmFd{-1};
    size_t shmSize{0};
    void *mapped{nullptr};
    // Per-ring consumer cursors, sized from the header ring count.
    std::vector<uint64_t> ringReadSeq;
    bool enableVtPhase{false};
    // Avoid repeating the same empty-read diagnostic when UTraceRead is polled.
    uint64_t lastEmptyReadTotalSeq{0};
    // java records contain the tid visible in the target's pid namespace, translate it to the host TID
    std::unordered_map<int, int> hostTidByNamespaceTid;
    bool runtimeStopped = false;
    bool runtimeRestored = false;
    bool runtimePrepared = false;
};

int JavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules, bool enableVtPhase);
int JavaBackendPrepare(JavaBackendImpl *impl);
int JavaBackendEnable(JavaBackendImpl *impl);
int JavaBackendDisable(JavaBackendImpl *impl);
int JavaBackendRead(JavaBackendImpl *impl, struct UTraceData **out_data, size_t *out_count);
void JavaBackendDataFree(struct UTraceData *data);
void JavaBackendClose(JavaBackendImpl *impl);
