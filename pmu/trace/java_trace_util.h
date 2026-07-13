/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-06-12
 * Description: Java trace utility functions for symbol parsing, config loading, command building and UTraceData
 * process
 ******************************************************************************/

#pragma once

#include "java_backend.h"
#include "pmu.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

struct SplitTraceAttr {
    std::vector<std::string> javaModules;
    std::vector<std::string> javaSymbols;
    std::vector<SymbolSource> javaSymSrc;

    std::vector<std::string> nativeModules;
    std::vector<std::string> nativeSymbols;
    std::vector<SymbolSource> nativeSymSrc;
};

struct JavaTraceLocalConfig {
    uint32_t slotCount;
    bool valid;
};

template <typename... Args>
std::string MakeLogMessage(const Args &...args)
{
    std::ostringstream stream;
    using Expander = int[];
    (void)Expander{0, ((void)(stream << args), 0)...};
    return stream.str();
}

std::string StripJavaClassName(const std::string &s);
SplitTraceAttr SplitSymbolsByRegex(const UTraceAttr *attr);
UTraceAttr MakeSubAttr(const UTraceAttr *src, std::vector<SymbolSource> &symSrc);
std::string BuildJavaSymSrc(const UTraceAttr *attr);

std::string FilterConfigPath();
JavaTraceLocalConfig LoadLocalConfig(const std::string &path);
std::string JavaTraceLogPath();
void JavaTraceLog(const std::string &message);
bool JavaTracePrepareTargetFiles(JavaBackendImpl &impl);
void JavaTraceFlushTargetLog(const JavaBackendImpl &impl);
void JavaTraceCleanupTargetAssets(JavaBackendImpl *impl);
std::string TimestampSuffix();
std::string BuildEnableCommand(const JavaBackendImpl &impl);
std::string BuildActionCommand(const JavaBackendImpl &impl, const char *action);
int RunCommand(const std::string &cmd);

char *TraceDupCString(const char *s);
char *TraceDupString(const std::string &s);
UTraceData DeepCopyTraceData(const UTraceData &src);
void FreeTraceDataFields(UTraceData &data);
