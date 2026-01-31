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
 * Description: Implementation of probe alias management, handling alias registration, binding retrieval, and cleanup
 ******************************************************************************/

#include "probe_alias_manager.h"
#include <stdexcept>

std::string ProbeAliasManager::GetEntryAlias(int pd, const std::string& symbol, uint64_t offset)
{
    auto symbolPtr = std::make_shared<std::string>(symbol);
    Binding binding{std::move(symbolPtr), false, offset};
    std::string alias = "s" + std::to_string(counter_++);
    return RegisterAlias(pd, std::move(alias), std::move(binding));
}

std::string ProbeAliasManager::GetRetAlias(int pd, const std::string& entryAlias, size_t retIndex, uint64_t offset)
{
    auto it = alias2Binding_.find(entryAlias);
    if (it == alias2Binding_.end()) {
        return "";
    }

    auto symbolPtr = it->second.originalSymRef;
    Binding binding{std::move(symbolPtr), true, offset};
    std::string alias = entryAlias + "_r" + std::to_string(retIndex);
    return RegisterAlias(pd, std::move(alias), std::move(binding));
}

void ProbeAliasManager::Erase(int pd)
{
    auto it = pd2Aliases_.find(pd);
    if (it == pd2Aliases_.end()) {
        return;
    }

    for (const std::string& alias : it->second) {
        alias2Binding_.erase(alias);
    }
    pd2Aliases_.erase(it);
}

const ProbeAliasManager::Binding& ProbeAliasManager::GetBinding(const std::string& alias) const
{
    auto it = alias2Binding_.find(alias);
    if (it == alias2Binding_.end()) {
        throw std::out_of_range("ProbeAliasManager::GetBinding: alias '" + alias + "' not found");
    }
    return it->second;
}

std::string ProbeAliasManager::RegisterAlias(int pd, std::string&& alias, ProbeAliasManager::Binding&& binding)
{
    std::string result(alias);
    alias2Binding_.emplace(std::move(alias), std::move(binding));
    pd2Aliases_[pd].push_back(result);
    return result;
}