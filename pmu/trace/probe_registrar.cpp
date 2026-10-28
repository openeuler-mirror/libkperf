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
#include "probe_alias_manager.h"
#include "pcerr.h"
#include <sstream>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/random.h>

namespace {
// tracefs parses commands, not an ordinary byte stream. Submit each line in
// one write; retry only EINTR, never resubmit a partially consumed command.
int WriteProbeCommand(int fd, const std::string &command)
{
    ssize_t n;
    do {
        n = write(fd, command.data(), command.size());
    } while (n < 0 && errno == EINTR);
    return n < 0 ? errno : (n == static_cast<ssize_t>(command.size()) ? 0 : EIO);
}
}

void ProbeRegistrar::ConvertToProbeEvents(
    int pd, const std::unordered_map<std::string, std::vector<ProbePoints>> &module2ProbePoints)
{
    // One private namespace per backend, including repeated sessions in one
    // process and processes in different PID namespaces.
    const std::string groupName = GenerateGroupName();
    if (groupName.empty()) {
        return;
    }
    size_t size = 0;
    for (const auto &kv : module2ProbePoints) {
        for (const auto &probePoints : kv.second) {
            size += 1 + probePoints.retOffsets.size();
        }
    }
    pd2ProbeEvents_[pd].reserve(size);

    for (const auto &kv : module2ProbePoints) {
        const std::string &binaryPath = kv.first;

        for (const auto &probePoints : kv.second) {
            if (probePoints.retOffsets.empty()) {
                continue;
            }
            std::string entryAlias =
                ProbeAliasManager::GetInstance().GetEntryAlias(pd, probePoints.symbolName, probePoints.entryOffset);
            pd2ProbeEvents_[pd].emplace_back(ProbeEvent{groupName, entryAlias, binaryPath, probePoints.entryOffset, false});
            for (size_t i = 0; i < probePoints.retOffsets.size(); ++i) {
                std::string retAlias =
                    ProbeAliasManager::GetInstance().GetRetAlias(pd, entryAlias, i, probePoints.retOffsets[i]);
                pd2ProbeEvents_[pd].emplace_back(
                    ProbeEvent{groupName, retAlias, binaryPath, probePoints.retOffsets[i], false});
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

    for (auto &probeEvent : pd2ProbeEvents_[pd]) {
        if (probeEvent.installedByUs) {
            continue;
        }
        const std::string probeStr = ConvertToProbeStr(probeEvent, fetchG);
        const int err = WriteProbeCommand(fd, probeStr);
        if (err != 0) {
            // EEXIST is not ownership. Never reuse or delete a foreign event.
            const std::string message = "Failed to install " + probeEvent.groupName + "/" +
                probeEvent.eventName + ": " + std::strerror(err);
            close(fd);
            UninstallProbes(pd);
            // Preserve the installation error if rollback also failed.
            pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, message);
            return false;
        }
        probeEvent.installedByUs = true;
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

    std::string firstError;
    for (auto &probeEvent : it->second) {
        if (!probeEvent.installedByUs) {
            continue;
        }
        const std::string command = "-:" + probeEvent.groupName + "/" + probeEvent.eventName + "\n";
        const int err = WriteProbeCommand(fd, command);
        if (err == 0 || err == ENOENT) {
            probeEvent.installedByUs = false;
        } else if (firstError.empty()) {
            firstError = "Failed to uninstall " + probeEvent.groupName + "/" +
                probeEvent.eventName + ": " + std::strerror(err);
        }
        // A busy event must not prevent cleanup of the other owned events.
    }
    if (!firstError.empty()) {
        pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, firstError);
    }

    close(fd);
}

std::string ProbeRegistrar::ConvertToProbeStr(const ProbeEvent &probeEvent, bool fetchG) const
{
    std::stringstream ss;

    ss << "p:" << probeEvent.groupName << "/" << probeEvent.eventName << " " << probeEvent.binaryPath << ":0x"
       << std::hex << probeEvent.offset;

    if (fetchG) {
#if defined(__x86_64__)
        ss << " g=%r14";
#elif defined(__aarch64__)
        ss << " g=%x28";
#endif
    }
    ss << "\n";

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

std::string ProbeRegistrar::GenerateGroupName() const
{
    uint64_t nonce[2];
    size_t done = 0;
    while (done < sizeof(nonce)) {
        const ssize_t n = getrandom(reinterpret_cast<char *>(nonce) + done, sizeof(nonce) - done, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            pcerr::New(LIBPERF_ERR_UTRACE_PROBE_REGISTER_FAILED, "Failed to generate private uprobe group");
            return "";
        }
        done += static_cast<size_t>(n);
    }
    static std::atomic<uint64_t> sequence{0};
    std::ostringstream group;
    group << "kp_" << std::hex << getpid() << "_" << std::setfill('0')
          << std::setw(16) << nonce[0] << std::setw(16) << nonce[1]
          << "_" << sequence.fetch_add(1, std::memory_order_relaxed);
    return group.str();
}
