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
 * Create: 2026-07-23
 * Description: Trace log function definition
 ******************************************************************************/

#pragma once

#include <string>

// Trace log severity. Higher numeric values are more verbose.
// A message is emitted only when its level is less than or equal to the
// current threshold (see SetTraceLogLevel). Default threshold is INFO, so
// VERBOSE ("debug") diagnostics are suppressed unless the user opts in via trace_filter.
enum class TraceLogLevel {
    ERROR = 0,
    WARNING = 1,
    INFO = 2,
    VERBOSE = 3
};

// Set the runtime log threshold used by TraceLog. Persisted for the process
// lifetime; call once after loading trace_filter.conf. Unknown values fall
// back to INFO.
void SetTraceLogLevel(TraceLogLevel level);
TraceLogLevel GetTraceLogLevel();

// Parse a case-insensitive level name ("error", "warning", "info", "debug").
// Returns true and stores the level on success; returns false otherwise.
bool TraceLogLevelFromString(const std::string &name, TraceLogLevel *level);

// Emit a message at the given severity, subject to the current threshold.
void TraceLog(TraceLogLevel level, const std::string &message);

// Convenience overload: equivalent to TraceLog(TraceLogLevel::INFO, message).
void TraceLog(const std::string &message);
