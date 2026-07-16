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
 * Author: Salt
 * Create: 2026-06-08
 * Description: internal shared utilities for proc data modules
 ******************************************************************************/
#ifndef PROC_DATA_COMMON_H
#define PROC_DATA_COMMON_H

#include <string>
#include <vector>
#include <utility>
#include "proc_data_types.h"

std::vector<std::string> SplitLine(const std::string &line, char delimiter);
std::string Trim(const std::string &s);
char* AllocStr(const std::string &s);
unsigned long long SafeStoull(const std::string &s, unsigned long long def = 0);
int SafeStoi(const std::string &s, int def = 0);
long long SafeStoll(const std::string &s, long long def = 0);
unsigned long long SafeStoullHex(const std::string &s, unsigned long long def = 0);
double SafeStod(const std::string &s, double def = 0.0);
unsigned long long SafeGetUll(const std::vector<std::string>& v, size_t i);
int SafeGetInt(const std::vector<std::string>& v, size_t i);
long long SafeGetLL(const std::vector<std::string>& v, size_t i);
double SafeGetDouble(const std::vector<std::string>& v, size_t i);
ProcField* ConvertFields(const std::vector<std::pair<std::string, std::string>> &fields);
void FreeFields(ProcField* fields, unsigned numFields);

#endif
