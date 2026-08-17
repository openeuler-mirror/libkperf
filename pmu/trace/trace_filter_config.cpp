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
 * Description: Implementation of trace config file analysis
 ******************************************************************************/

#include "trace_filter_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <fnmatch.h>
#include <limits>
#include "common.h"

namespace {
static constexpr int K_NUMBER_BASE_DECIMAL = 10;
static constexpr const char *K_NATIVE_SAMPLE_PERIOD_KEY = "native_sample_period";
static constexpr const char *K_KERNEL_FTRACE_BUFFER_SIZE_KB_KEY = "kernel_ftrace_buffer_size_kb";
static constexpr const char *K_DIGITS = "0123456789";
static constexpr uint32_t K_MAX_FTRACE_BUFFER_SIZE_KB = 16U * 1024U;

static bool TryParseSamplePeriod(const std::string &value, uint32_t *period)
{
    if (period == nullptr || value.empty() || value.find_first_not_of(K_DIGITS) != std::string::npos) {
        return false;
    }
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, nullptr, K_NUMBER_BASE_DECIMAL);
    } catch (const std::exception &) {
        return false;
    }
    if (parsed == 0 || parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *period = static_cast<uint32_t>(parsed);
    return true;
}

static bool TryParseFtraceBufferSizeKb(const std::string &value, uint32_t *size)
{
    uint32_t parsed = 0;
    if (!TryParseSamplePeriod(value, &parsed) || parsed > K_MAX_FTRACE_BUFFER_SIZE_KB) {
        return false;
    }
    *size = parsed;
    return true;
}

static void AppendRuleStatus(std::string &effective, std::string &ineffective,
    const std::vector<std::string> &rules, const std::vector<bool> &matched, const char *type)
{
    for (size_t i = 0; i < rules.size(); ++i) {
        std::string &target = i < matched.size() && matched[i] ? effective : ineffective;
        if (!target.empty()) {
            target += ", ";
        }
        target += type;
        target += ':';
        target += rules[i];
    }
}

static std::string TargetVisibleModulePath(const std::string &module)
{
    const std::string procPrefix = "/proc/";
    if (module.compare(0, procPrefix.size(), procPrefix) != 0) {
        return module;
    }
    size_t pidEnd = module.find('/', procPrefix.size());
    if (pidEnd == std::string::npos || pidEnd == procPrefix.size()) {
        return module;
    }
    std::string pidText = module.substr(procPrefix.size(), pidEnd - procPrefix.size());
    if (pidText.find_first_not_of(K_DIGITS) != std::string::npos ||
        module.compare(pidEnd, 6, "/root/") != 0) {
        return module;
    }
    return module.substr(pidEnd + 5);
}
} // namespace

std::string StripTraceConfigComment(const std::string &line)
{
    size_t end = line.size();
    size_t hash = line.find('#');
    if (hash != std::string::npos) {
        end = std::min(end, hash);
    }
    size_t slashes = line.find("//");
    if (slashes != std::string::npos) {
        end = std::min(end, slashes);
    }
    return line.substr(0, end);
}

UTraceSymbolFilterConfig LoadUTraceSymbolFilterConfig(const std::string &path)
{
    UTraceSymbolFilterConfig out;
    if (path.empty()) {
        return out;
    }

    FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return out;
    }

    std::vector<std::string> *rules = nullptr;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
        std::string s = Trim(StripTraceConfigComment(buf));
        if (s.empty()) {
            continue;
        }
        size_t equalPos = s.find('=');
        if (equalPos != std::string::npos) {
            std::string key = Trim(s.substr(0, equalPos));
            std::string value = Trim(s.substr(equalPos + 1));
            if (key == K_KERNEL_FTRACE_BUFFER_SIZE_KB_KEY &&
                !TryParseFtraceBufferSizeKb(value, &out.kernelFtraceBufferSizeKb)) {
                out.valid = false;
                out.error = "Invalid kernel_ftrace_buffer_size_kb: " + value + "; expected an integer in [1, 16384]";
                break;
            }
            if (key == K_NATIVE_SAMPLE_PERIOD_KEY && !TryParseSamplePeriod(value, &out.nativeSamplePeriod)) {
                out.valid = false;
                out.error = "Invalid native_sample_period: " + value + "; expected an integer in [1, 4294967295]";
                break;
            }
            continue;
        }
        if (s.front() == '[' && s.back() == ']') {
            std::string section = s.substr(1, s.size() - 2);
            std::transform(section.begin(), section.end(), section.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (section == "kernel_include") {
                rules = &out.kernelIncludes;
            } else if (section == "kernel_exclude") {
                rules = &out.kernelExcludes;
            } else if (section == "native_include") {
                rules = &out.nativeIncludes;
            } else if (section == "native_exclude") {
                rules = &out.nativeExcludes;
            } else if (section == "java_include") {
                rules = &out.javaIncludes;
            } else {
                rules = nullptr;
            }
            continue;
        }
        if (rules != nullptr) {
            rules->emplace_back(s);
        }
    }
    std::fclose(fp);
    return out;
}

bool IsTraceSymbolAllowed(const UTraceSymbolFilterConfig &config, TraceSymbolDomain domain,
                          const std::string &module, const std::string &symbol)
{
    const std::vector<std::string> &excludes =
        domain == TraceSymbolDomain::KERNEL ? config.kernelExcludes : config.nativeExcludes;
    auto matches = [&module, &symbol](const std::string &pattern) {
        return MatchesTraceSymbolRule(pattern, module, symbol);
    };
    return !std::any_of(excludes.begin(), excludes.end(), matches);
}

bool MatchesTraceSymbolRule(const std::string &pattern, const std::string &module, const std::string &symbol)
{
    std::string qualified = module + "::" + symbol;
    if (fnmatch(pattern.c_str(), symbol.c_str(), 0) == 0 || fnmatch(pattern.c_str(), qualified.c_str(), 0) == 0) {
        return true;
    }
    std::string targetQualified = TargetVisibleModulePath(module) + "::" + symbol;
    return targetQualified != qualified && fnmatch(pattern.c_str(), targetQualified.c_str(), 0) == 0;
}

std::string FormatTraceFilterRuleStatus(const std::vector<std::string> &includes,
    const std::vector<std::string> &excludes, const std::vector<bool> &includeMatched,
    const std::vector<bool> &excludeMatched)
{
    std::string effective;
    std::string ineffective;
    AppendRuleStatus(effective, ineffective, includes, includeMatched, "include");
    AppendRuleStatus(effective, ineffective, excludes, excludeMatched, "exclude");
    return "matched=[" + effective + "], unmatched=[" + ineffective + "]";
}
