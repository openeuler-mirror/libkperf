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
 * Create: 2026-01-31
 * Description: Interfaces for managing probe aliases and maintaining mappings between aliases and symbol bindings
 ******************************************************************************/

#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

class ProbeAliasManager {
public:
    static ProbeAliasManager& GetInstance() {
        static ProbeAliasManager instance;
        return instance;
    }

    ProbeAliasManager(const ProbeAliasManager&) = delete;
    ProbeAliasManager& operator=(const ProbeAliasManager&) = delete;

    struct Binding {
        std::shared_ptr<std::string> originalSymRef;
        bool isRet;
        uint64_t offset;
    };

    std::string GetEntryAlias(int pd, const std::string& symbol, uint64_t offset);

    std::string GetRetAlias(int pd, const std::string& entryAlias, size_t retIndex, uint64_t offset);

    void Erase(int pd);

    const Binding& GetBinding(const std::string& alias) const;

private:
    ProbeAliasManager() = default;
    ~ProbeAliasManager() = default;

    std::string RegisterAlias(int pd, std::string&& alias, Binding&& binding);

    std::unordered_map<int, std::vector<std::string>> pd2Aliases_;

    std::unordered_map<std::string, Binding> alias2Binding_;

    uint64_t counter_ = 0;
};