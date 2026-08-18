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
 * Create: 2026-07-23
 * Description: Definition of trace config file analysis
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class TraceSymbolDomain {
    KERNEL,
    NATIVE
};

struct UTraceSymbolFilterConfig {
    uint32_t nativeSamplePeriod = 4000;
    // ftrace buffer size per CPU, in KiB.
    uint32_t kernelFtraceBufferSizeKb = 1024;
    // Include selected functions that execute in interrupt context.
    bool kernelFtraceIrqs = true;
    bool valid = true;
    std::string error;
    std::vector<std::string> kernelIncludes;
    std::vector<std::string> kernelExcludes;
    std::vector<std::string> nativeIncludes;
    std::vector<std::string> nativeExcludes;
    std::vector<std::string> javaIncludes;
};

std::string StripTraceConfigComment(const std::string &line);
UTraceSymbolFilterConfig LoadUTraceSymbolFilterConfig(const std::string &path);
bool IsTraceSymbolAllowed(const UTraceSymbolFilterConfig &config, TraceSymbolDomain domain,
                          const std::string &module, const std::string &symbol);
bool MatchesTraceSymbolRule(const std::string &pattern, const std::string &module,
                            const std::string &symbol);
std::string FormatTraceFilterRuleStatus(const std::vector<std::string> &includes,
                                        const std::vector<std::string> &excludes,
                                        const std::vector<bool> &includeMatched,
                                        const std::vector<bool> &excludeMatched);
