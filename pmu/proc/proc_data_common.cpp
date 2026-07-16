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
 * Description: shared utility functions for proc data parsing/converting/freeing
 ******************************************************************************/
#include "proc_data_common.h"

#include <cstring>
#include <sstream>

#include "proc_data_types.h"

using namespace std;

vector<string> SplitLine(const string &line, char delimiter)
{
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (getline(iss, token, delimiter)) {
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != string::npos && end != string::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}

string Trim(const string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

char* AllocStr(const string &s)
{
    char *p = new char[s.size() + 1];
    memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

unsigned long long SafeStoull(const string &s, unsigned long long def)
{
    try { return stoull(s); } catch (...) { return def; }
}

int SafeStoi(const string &s, int def)
{
    try { return stoi(s); } catch (...) { return def; }
}

long long SafeStoll(const string &s, long long def)
{
    try { return stoll(s); } catch (...) { return def; }
}

unsigned long long SafeStoullHex(const string &s, unsigned long long def)
{
    try { return stoull(s, nullptr, 16); } catch (...) { return def; }
}

double SafeStod(const string &s, double def)
{
    try { return stod(s); } catch (...) { return def; }
}

unsigned long long SafeGetUll(const vector<string>& v, size_t i)
{
    return i < v.size() ? SafeStoull(v[i]) : 0;
}

int SafeGetInt(const vector<string>& v, size_t i)
{
    return i < v.size() ? SafeStoi(v[i]) : 0;
}

long long SafeGetLL(const vector<string>& v, size_t i)
{
    return i < v.size() ? SafeStoll(v[i]) : 0;
}

double SafeGetDouble(const vector<string>& v, size_t i)
{
    return i < v.size() ? SafeStod(v[i]) : 0.0;
}

ProcField* ConvertFields(const vector<pair<string, string>> &fields)
{
    if (fields.empty()) {
        return nullptr;
    }
    ProcField* result = new ProcField[fields.size()];
    for (size_t i = 0; i < fields.size(); i++) {
        result[i].key = AllocStr(fields[i].first);
        result[i].value = AllocStr(fields[i].second);
    }
    return result;
}

void FreeFields(ProcField* fields, unsigned numFields)
{
    if (!fields) {
        return;
    }
    for (unsigned i = 0; i < numFields; i++) {
        delete[] fields[i].key;
        delete[] fields[i].value;
    }
    delete[] fields;
}
