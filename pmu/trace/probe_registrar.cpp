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
 * Author: Xie Jingwei
 * Create: 2026-01-21
 * Description: Implementation of probe event registration and uprobe management
 ******************************************************************************/

#include "probe_registrar.h"
#include "pcerr.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

void ProbeRegistrar::ConvertToProbeEvents(int pd, const std::unordered_map<std::string, std::vector<ProbePoints>> &module2ProbePoints)
{
    size_t size = 0;
    for (const auto &kv : module2ProbePoints) {
        for (const auto &probePoints : kv.second) {
            size += 1 + probePoints.retOffsets.size();
        }
    }
    pd2ProbeEvents_[pd].reserve(size);

    for (const auto &kv : module2ProbePoints) {
        const std::string &binaryPath = kv.first;
        std::string groupName = GenerateGroupName(binaryPath);

        for (const auto &probePoints : kv.second) {
            std::string eventName = SanitizeSymbol(probePoints.symbolName);
            pd2ProbeEvents_[pd].emplace_back(ProbeEvent{groupName, eventName, binaryPath, probePoints.entryOffset});
            for (size_t i = 0; i < probePoints.retOffsets.size(); ++i) {
                pd2ProbeEvents_[pd].emplace_back(ProbeEvent{
                    groupName, eventName + "_ret_" + std::to_string(i), binaryPath, probePoints.retOffsets[i]});
            }
        }
    }
}

void ProbeRegistrar::EraseProbeEvents(int pd)
{
    pd2ProbeEvents_.erase(pd);
}

bool ProbeRegistrar::InstallProbes(int pd, bool fetchG)
{
    if (pd2ProbeEvents_[pd].empty()) {
        pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "No probes to install");
        return false;
    }
    if (GetUprobeFilePath().empty()) {
        pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED,
            "Cannot access uprobe_events file (permission denied or not available)");
        return false;
    }

    int fd = open(GetUprobeFilePath().c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "Failed to open uprobe_events file for writing");
        return false;
    }

    std::stringstream ss;
    for (const auto &probeEvent : pd2ProbeEvents_[pd]) {
        ss << ConvertToProbeStr(probeEvent, fetchG) << "\n";
    }
    std::string allProbeStrs = ss.str();

    ssize_t written = 0;
    const char *p = allProbeStrs.c_str();
    size_t total = allProbeStrs.size();

    while (written < static_cast<ssize_t>(total)) {
        ssize_t n = write(fd, p + written, total - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            pcerr::New(
                LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "Failed to write to uprobe_events file for installation");
            close(fd);
            return false;
        }
        written += n;
    }

    close(fd);
    return true;
}

void ProbeRegistrar::UninstallProbes(int pd)
{
    auto it = pd2ProbeEvents_.find(pd);
    if (it == pd2ProbeEvents_.end() || it->second.empty() || GetUprobeFilePath().empty()) {
        return;
    }

    int fd = open(GetUprobeFilePath().c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) {
        pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "Failed to open uprobe_events file for writing");
        return;
    }

    std::stringstream ss;
    for (const auto &probeEvent : pd2ProbeEvents_[pd]) {
        ss << "-:" << probeEvent.groupName << "/" << probeEvent.eventName << "\n";
    }
    std::string allProbeStrs = ss.str();

    ssize_t written = 0;
    const char *p = allProbeStrs.c_str();
    size_t total = allProbeStrs.size();

    while (written < static_cast<ssize_t>(total)) {
        ssize_t n = write(fd, p + written, total - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            pcerr::New(
                LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "Failed to write to uprobe_events file for uninstallation");
            close(fd);
            return;
        }
        written += n;
    }

    close(fd);
}

std::string ProbeRegistrar::ConvertToProbeStr(const ProbeEvent &probeEvent, bool fetchG) const
{
    std::stringstream ss;
    ss << "p:" << probeEvent.groupName << "/" << probeEvent.eventName << " " << probeEvent.binaryPath << ":0x"
       << std::hex << probeEvent.offset;

    if (fetchG) {
        ss << " g=%x28";
    }
    return ss.str();
}

const std::string &ProbeRegistrar::GetUprobeFilePath() const
{
    static std::string cachedPath = []() {
        const std::vector<std::string> paths = {
            "/sys/kernel/tracing/uprobe_events", "/sys/kernel/debug/tracing/uprobe_events"};
        for (const auto &path : paths) {
            struct stat buffer;
            if (stat(path.c_str(), &buffer) == 0) {
                return path;
            }
        }
        return std::string("");
    }();
    return cachedPath;
}

std::string ProbeRegistrar::GenerateGroupName(const std::string &binaryPath) const
{
    size_t lastSlash = binaryPath.find_last_of('/');
    std::string fileName = (lastSlash == std::string::npos) ? binaryPath : binaryPath.substr(lastSlash + 1);
    return "probe_" + SanitizeStr(fileName, 32);
}

std::string ProbeRegistrar::SanitizeSymbol(const std::string &rawName) const
{
    return SanitizeStr(rawName, 50);
}

std::string ProbeRegistrar::SanitizeStr(const std::string &input, size_t maxLen) const
{
    if (input.empty()) {
        return "unknown";
    }

    std::string result;
    result.reserve(std::min(input.length(), maxLen));

    bool lastIsUnderscore = true;

    for (char c : input) {
        if (result.length() >= maxLen) {
            break;
        }

        if (isalnum(c)) {
            result.push_back(c);
            lastIsUnderscore = false;
        } else if (!lastIsUnderscore) {
            result.push_back('_');
            lastIsUnderscore = true;
        }
    }

    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return result.empty() ? "unknown" : result;
}