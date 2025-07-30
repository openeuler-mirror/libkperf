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
 * Description: Provide common file operation functions and system resource management functions.
 ******************************************************************************/

#ifndef LIBKPROF_COMMON_H
#define LIBKPROF_COMMON_H
#include <linux/perf_event.h>
#include <string>
#include <vector>
#include <cstdint>

#ifdef __x86_64__
#define IS_X86 1
#elif defined(__aarch64__)
#define IS_ARM 1
#elif defined(__riscv) && (__riscv_xlen == 64)
#define IS_RISCV64 1
#else
#error "Only the x86_64, aarch64, and riscv64 architectures are supported."
#endif

#define CGROUP2_SUPER_MAGIC 0x63677270

const std::string TRACE_EVENT_PATH = "/sys/kernel/tracing/events/";
const std::string TRACE_DEBUG_EVENT_PATH = "/sys/kernel/debug/tracing/events/";
const std::string PERF_EVENT_PARANOID_PATH = "/proc/sys/kernel/perf_event_paranoid";
const std::string SYS_DEVICE_PATH = "/sys/bus/event_source/devices/";

inline bool IsValidIp(unsigned long ip)
{
    return (ip != PERF_CONTEXT_HV && ip != PERF_CONTEXT_KERNEL && ip != PERF_CONTEXT_USER
        && ip != PERF_CONTEXT_GUEST && ip != PERF_CONTEXT_GUEST_KERNEL
        && ip != PERF_CONTEXT_GUEST_USER && ip != PERF_CONTEXT_MAX);
}
std::string GetRealPath(const std::string filePath);
bool IsValidPath(const std::string& filePath);
bool IsDirectory(const std::string& path);
bool FileExists(const std::string& path);
std::vector<std::string> ListDirectoryEntries(const std::string& dirPath);
std::string ReadFileContent(const std::string& filePath);
std::vector<std::string> SplitStringByDelimiter(const std::string& str, char delimiter);
int RaiseNumFd(uint64_t numFd);
bool ExistPath(const std::string& filePath);
std::string GetTraceEventDir();
bool StartWith(const std::string& str, const std::string& prefix);
int CheckCgroupV2();
bool ConvertStrToInt(const std::string& intValStr, int32_t& val);
int GetParanoidVal();

#endif  // LIBKPROF_COMMON_H
