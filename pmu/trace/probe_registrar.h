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
 * Description: Interfaces for managing probe events and registering/unregistering uprobes
 ******************************************************************************/

#pragma once

#include "elf_scanner.h"

struct ProbeEvent {
    std::string groupName;
    std::string eventName;
    std::string binaryPath;
    uint64_t offset;
};

class ProbeRegistrar {
public:
    static ProbeRegistrar &GetInstance()
    {
        static ProbeRegistrar instance;
        return instance;
    }

    ProbeRegistrar(const ProbeRegistrar &) = delete;
    ProbeRegistrar &operator=(const ProbeRegistrar &) = delete;

    void ConvertToProbeEvents(
        int pd, const std::unordered_map<std::string, std::vector<ProbePoints>> &module2ProbePoints);

    void EraseProbeEvents(int pd);

    bool InstallProbes(int pd, bool fetchG);

    void UninstallProbes(int pd);

    const std::vector<ProbeEvent> &GetProbeEvents(int pd) const
    {
        return pd2ProbeEvents_.at(pd);
    }

private:
    ProbeRegistrar() = default;
    ~ProbeRegistrar() = default;

    std::unordered_map<int, std::vector<ProbeEvent>> pd2ProbeEvents_;

    std::string ConvertToProbeStr(const ProbeEvent &probeEvent, bool fetchG) const;

    const std::string &GetUprobeFilePath() const;

    std::string GenerateGroupName(const std::string &binaryPath) const;

    std::string SanitizeSymbol(const std::string &rawName) const;

    std::string SanitizeStr(const std::string &input, size_t maxLen) const;
};