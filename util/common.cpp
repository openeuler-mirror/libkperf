/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Provide common file operation functions and system resource management functions.
 ******************************************************************************/

#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/resource.h>
#include <climits>
#include "pcerrc.h"
#include "common.h"

bool IsValidIp(unsigned long ip) {
    return (ip != PERF_CONTEXT_HV && ip != PERF_CONTEXT_KERNEL && ip != PERF_CONTEXT_USER
            && ip != PERF_CONTEXT_GUEST && ip != PERF_CONTEXT_GUEST_KERNEL
            && ip != PERF_CONTEXT_GUEST_USER && ip != PERF_CONTEXT_MAX);
}

std::string GetRealPath(const std::string filePath)
{
    char resolvedPath[PATH_MAX];
    if (realpath(filePath.data(), resolvedPath) == nullptr) {
        return std::string{};
    }
    if (access(resolvedPath, R_OK) != 0) {
        return std::string{};
    }
    return resolvedPath;
}

bool IsValidPath(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }
    return true;
}

int RaiseNumFd(unsigned long numFd)
{
    unsigned long extra = 50;
    unsigned long setNumFd = extra + numFd;
    struct rlimit currentlim;
    if (getrlimit(RLIMIT_NOFILE, &currentlim) == -1) {
        return LIBPERF_ERR_RAISE_FD;
    }
    if (currentlim.rlim_cur > setNumFd) {
        return SUCCESS;
    }
    if (currentlim.rlim_max < numFd) {
        return LIBPERF_ERR_TOO_MANY_FD;
    }
    struct rlimit rlim {
            .rlim_cur = currentlim.rlim_max, .rlim_max = currentlim.rlim_max,
    };
    if (setNumFd < currentlim.rlim_max) {
        rlim.rlim_cur = setNumFd;
    }
    if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        return LIBPERF_ERR_RAISE_FD;
    } else {
        return SUCCESS;
    }
}