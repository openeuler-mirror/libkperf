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
 * Description: pid-level proc data parsing functions
 ******************************************************************************/
#include "proc_data_manager.h"

#include <cstring>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include "proc_data_const.h"
#include "proc_data_common.h"
#include "common.h"
#include "pcerr.h"
#include "pcerrc.h"

using namespace std;
using namespace pcerr;
using namespace KUNPENG_PMU;

int ProcDataManager::ParsePidStat(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    if (trimmed.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    vector<PidStatEntryInternal> entries;
    PidStatEntryInternal entry{};
    size_t commStart = trimmed.find('(');
    size_t commEnd = trimmed.rfind(')');
    if (commStart != string::npos && commEnd != string::npos && commEnd > commStart) {
        entry.pid = SafeStoi(Trim(trimmed.substr(0, commStart)));
        entry.comm = trimmed.substr(commStart + 1, commEnd - commStart - 1);
        string afterComm = Trim(trimmed.substr(commEnd + 1));
        vector<string> parts = SplitLine(afterComm, ' ');
        if (!parts.empty() && !parts[0].empty()) {
            entry.state = parts[0][0];
        }
        FillPidStatFields(entry, parts);
    }
    entries.push_back(move(entry));
    result.entries = new vector<PidStatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidStatEntryInternal>*>(p); };
    return SUCCESS;
}

template <typename Entry>
void ProcDataManager::FillPidStatFields(Entry &entry, const vector<string> &parts)
{
    FillPidStatProcFields(entry, parts);
    FillPidStatMemFields(entry, parts);
    FillPidStatSigFields(entry, parts);
}

template <typename Entry>
void ProcDataManager::FillPidStatProcFields(Entry &entry, const vector<string> &parts)
{
    entry.ppid = SafeGetInt(parts, PidStatField::PPID);
    entry.pgrp = SafeGetInt(parts, PidStatField::PGRP);
    entry.session = SafeGetInt(parts, PidStatField::SESSION);
    entry.tty_nr = SafeGetInt(parts, PidStatField::TTY_NR);
    entry.tpgid = SafeGetInt(parts, PidStatField::TPGID);
    entry.flags = SafeGetUll(parts, PidStatField::PIDSTAT_FLAGS);
    entry.minflt = SafeGetUll(parts, PidStatField::MINFLT);
    entry.cminflt = SafeGetUll(parts, PidStatField::CMINFLT);
    entry.majflt = SafeGetUll(parts, PidStatField::MAJFLT);
    entry.cmajflt = SafeGetUll(parts, PidStatField::CMAJFLT);
    entry.utime = SafeGetUll(parts, PidStatField::UTIME);
    entry.stime = SafeGetUll(parts, PidStatField::STIME);
    entry.cutime = SafeGetLL(parts, PidStatField::CUTIME);
    entry.cstime = SafeGetLL(parts, PidStatField::CSTIME);
    entry.priority = SafeGetInt(parts, PidStatField::PRIORITY);
    entry.nice_val = SafeGetInt(parts, PidStatField::NICE_VAL);
}

template <typename Entry>
void ProcDataManager::FillPidStatMemFields(Entry &entry, const vector<string> &parts)
{
    entry.num_threads = SafeGetInt(parts, PidStatField::NUM_THREADS);
    entry.itrealvalue = SafeGetLL(parts, PidStatField::ITREALVALUE);
    entry.starttime = SafeGetUll(parts, PidStatField::STARTTIME);
    entry.vsize = SafeGetUll(parts, PidStatField::VSIZE);
    entry.rss = SafeGetLL(parts, PidStatField::RSS);
    entry.rsslim = SafeGetUll(parts, PidStatField::RSSLIM);
    entry.startcode = SafeGetUll(parts, PidStatField::STARTCODE);
    entry.endcode = SafeGetUll(parts, PidStatField::ENDCODE);
    entry.startstack = SafeGetUll(parts, PidStatField::STARTSTACK);
    entry.kstkesp = SafeGetUll(parts, PidStatField::KSTKESP);
    entry.kstkeip = SafeGetUll(parts, PidStatField::KSTKEIP);
    entry.signal = SafeGetUll(parts, PidStatField::SIGNAL);
    entry.blocked = SafeGetUll(parts, PidStatField::BLOCKED);
    entry.sigignore = SafeGetUll(parts, PidStatField::SIGIGNORE);
    entry.sigcatch = SafeGetUll(parts, PidStatField::SIGCATCH);
    entry.wchan = SafeGetUll(parts, PidStatField::WCHAN);
}

template <typename Entry>
void ProcDataManager::FillPidStatSigFields(Entry &entry, const vector<string> &parts)
{
    entry.nswap = SafeGetUll(parts, PidStatField::NSWAP);
    entry.cnswap = SafeGetUll(parts, PidStatField::CNSWAP);
    entry.exit_signal = SafeGetInt(parts, PidStatField::EXIT_SIGNAL);
    entry.processor = SafeGetInt(parts, PidStatField::PROCESSOR);
    entry.rt_priority = SafeGetUll(parts, PidStatField::RT_PRIORITY);
    entry.policy = SafeGetUll(parts, PidStatField::POLICY);
    entry.delayacct_blkio_ticks = SafeGetUll(parts, PidStatField::DELAYACCT_BLKIO_TICKS);
    entry.guest_time = SafeGetUll(parts, PidStatField::GUEST_TIME);
    entry.cguest_time = SafeGetLL(parts, PidStatField::CGUEST_TIME);
    entry.start_data = SafeGetUll(parts, PidStatField::START_DATA);
    entry.end_data = SafeGetUll(parts, PidStatField::END_DATA);
    entry.start_brk = SafeGetUll(parts, PidStatField::START_BRK);
    entry.arg_start = SafeGetUll(parts, PidStatField::ARG_START);
    entry.arg_end = SafeGetUll(parts, PidStatField::ARG_END);
    entry.env_start = SafeGetUll(parts, PidStatField::ENV_START);
    entry.env_end = SafeGetUll(parts, PidStatField::ENV_END);
    entry.exit_code = SafeGetInt(parts, PidStatField::EXIT_CODE);
}

int ProcDataManager::ParsePidStatm(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    if (trimmed.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    vector<PidStatmEntryInternal> entries;
    PidStatmEntryInternal entry{};
    vector<string> parts = SplitLine(trimmed, ' ');
        // parts field indices (/proc file format)
    entry.size = SafeGetUll(parts, PidStatmField::SIZE);
    entry.resident = SafeGetUll(parts, PidStatmField::RESIDENT);
    entry.shared = SafeGetUll(parts, PidStatmField::SHARED);
    entry.text = SafeGetUll(parts, PidStatmField::TEXT);
    entry.lib = SafeGetUll(parts, PidStatmField::LIB);
    entry.data = SafeGetUll(parts, PidStatmField::DATA);
    entry.dt = SafeGetUll(parts, PidStatmField::DT);
    entries.push_back(move(entry));
    result.entries = new vector<PidStatmEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidStatmEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidStatus(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidStatusEntryInternal> entries;
    PidStatusEntryInternal entry;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        size_t colonPos = line.find(':');
        if (colonPos != string::npos) {
            string key = Trim(line.substr(0, colonPos));
            string value = Trim(line.substr(colonPos + 1));
            if (!key.empty()) {
                entry.fields.emplace_back(key, value);
            }
        }
    }
    if (entry.fields.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    entries.push_back(move(entry));
    result.entries = new vector<PidStatusEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidStatusEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidIo(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidIoEntryInternal> entries;
    PidIoEntryInternal entry{};
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        size_t colonPos = line.find(':');
        if (colonPos == string::npos) {
            continue;
        }
        string key = Trim(line.substr(0, colonPos));
        string value = Trim(line.substr(colonPos + 1));
        if (key == "rchar") {
            entry.rchar = SafeStoull(value);
        } else if (key == "wchar") {
            entry.wchar = SafeStoull(value);
        } else if (key == "syscr") {
            entry.syscr = SafeStoull(value);
        } else if (key == "syscw") {
            entry.syscw = SafeStoull(value);
        } else if (key == "read_bytes") {
            entry.read_bytes = SafeStoull(value);
        } else if (key == "write_bytes") {
            entry.write_bytes = SafeStoull(value);
        } else if (key == "cancelled_write_bytes") {
            entry.cancelled_write_bytes = SafeStoull(value);
        }
    }
    entries.push_back(move(entry));
    result.entries = new vector<PidIoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidIoEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidSmapsRollup(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidSmapsRollupEntryInternal> entries;
    PidSmapsRollupEntryInternal entry;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.find("[rollup]") != string::npos) {
            entry.fields.emplace_back("rollup", trimmed);
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos != string::npos) {
            entry.fields.emplace_back(Trim(trimmed.substr(0, colonPos)), Trim(trimmed.substr(colonPos + 1)));
        }
    }
    if (entry.fields.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    entries.push_back(move(entry));
    result.entries = new vector<PidSmapsRollupEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidSmapsRollupEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidFd(const string &content, int pid, ProcDataInternal &result)
{
    istringstream iss(content);
    string line;
    int count = 0;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (!trimmed.empty() && trimmed != "." && trimmed != "..") {
            count++;
        }
    }
    vector<PidFdEntryInternal> entries;
    PidFdEntryInternal entry{};
    entry.fd_count = count;
    entries.push_back(move(entry));
    result.entries = new vector<PidFdEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidFdEntryInternal>*>(p); };
    return SUCCESS;
}

void ProcDataManager::FillPidNumaMapsEntry(PidNumaMapsEntryInternal &entry, const string &line)
{
    size_t spacePos = line.find(' ');
    if (spacePos == string::npos) {
        entry.address = line;
        return;
    }
    entry.address = Trim(line.substr(0, spacePos));
    string rest = Trim(line.substr(spacePos + 1));
    vector<string> parts = SplitLine(rest, ' ');
    for (const auto &part : parts) {
        size_t eqPos = part.find('=');
        if (eqPos != string::npos) {
            entry.fields.emplace_back(part.substr(0, eqPos), part.substr(eqPos + 1));
        } else {
            entry.fields.emplace_back("flag", part);
        }
    }
}

int ProcDataManager::ParsePidNumaMaps(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidNumaMapsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        PidNumaMapsEntryInternal entry{};
        FillPidNumaMapsEntry(entry, trimmed);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidNumaMapsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidNumaMapsEntryInternal>*>(p); };
    return SUCCESS;
}

bool ProcDataManager::IsSmapsMappingLine(const string &line)
{
    size_t dashPos = line.find('-');
    if (dashPos == string::npos || dashPos == 0) {
        return false;
    }
    for (size_t k = 0; k < dashPos; k++) {
        if (!isxdigit(line[k])) {
            return false;
        }
    }
    if (dashPos + 1 >= line.size()) {
        return false;
    }
    return isxdigit(line[dashPos + 1]);
}

int ProcDataManager::ParsePidSmaps(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidSmapsEntryInternal> entries;
    istringstream iss(content);
    string line;
    PidSmapsEntryInternal entry;
    bool hasMapping = false;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (IsSmapsMappingLine(trimmed)) {
            if (hasMapping) {
                entries.push_back(move(entry));
                entry = PidSmapsEntryInternal();
            }
            entry.mapping = trimmed;
            hasMapping = true;
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos != string::npos && hasMapping) {
            entry.fields.emplace_back(Trim(trimmed.substr(0, colonPos)), Trim(trimmed.substr(colonPos + 1)));
        }
    }
    if (hasMapping) {
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidSmapsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidSmapsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidEnviron(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidEnvironEntryInternal> entries;
    string normalized;
    for (char c : content) normalized += (c == '\0') ? '\n' : c;
    istringstream iss(normalized);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        PidEnvironEntryInternal entry{};
        size_t eqPos = trimmed.find('=');
        if (eqPos != string::npos) {
            entry.name = trimmed.substr(0, eqPos);
            entry.value = trimmed.substr(eqPos + 1);
        } else {
            entry.name = trimmed;
        }
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidEnvironEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidEnvironEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidCmdline(const string &content, int pid, ProcDataInternal &result)
{
    string normalized;
    for (char c : content) normalized += (c == '\0') ? ' ' : c;
    string trimmed = Trim(normalized);
    vector<PidCmdlineEntryInternal> entries;
    PidCmdlineEntryInternal entry{};
    entry.cmdline = trimmed;
    entries.push_back(move(entry));
    result.entries = new vector<PidCmdlineEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidCmdlineEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidLimits(const string &content, int pid, ProcDataInternal &result)
{
    static const unordered_set<string> knownUnits = {
        "seconds", "bytes", "processes", "files", "locks", "signals", "us", "ms", "kB", "MB"
    };
    vector<PidLimitsEntryInternal> entries;
    istringstream iss(content);
    string line;
    bool firstLine = true;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (firstLine) {
            firstLine = false;
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.size() < 3) {
            continue;
        }
        PidLimitsEntryInternal entry{};
        bool hasUnits = parts.size() >= 4 && knownUnits.count(parts.back());
        FillPidLimitsEntry(entry, parts, hasUnits);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidLimitsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidLimitsEntryInternal>*>(p); };
    return SUCCESS;
}

string ProcDataManager::BuildLimitName(const vector<string> &parts, size_t nameEnd)
{
    string limitName;
    for (size_t k = 0; k < nameEnd; k++) {
        if (!limitName.empty()) {
            limitName += " ";
        }
        limitName += parts[k];
    }
    return limitName;
}

void ProcDataManager::FillPidLimitsEntry(PidLimitsEntryInternal &entry, const vector<string> &parts, bool hasUnits)
{
    if (hasUnits) {
        entry.limit = BuildLimitName(parts, parts.size() - 3);
        entry.soft = parts[parts.size() - 3];
        entry.hard = parts[parts.size() - 2];
        entry.units = parts.back();
    } else {
        entry.limit = BuildLimitName(parts, parts.size() - 2);
        entry.soft = parts[parts.size() - 2];
        entry.hard = parts.back();
        entry.units = "";
    }
}

int ProcDataManager::ParsePidStack(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidStackEntryInternal> entries;
    string trimmedContent = Trim(content);
    if (trimmedContent.empty()) {
        result.entries = new vector<PidStackEntryInternal>(move(entries));
        result.destroy = [](void *p) { delete static_cast<vector<PidStackEntryInternal>*>(p); };
        return SUCCESS;
    }
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        size_t addrStart = trimmed.find("[<");
        size_t addrEnd = trimmed.find(">]");
        if (addrStart == string::npos || addrEnd == string::npos || addrEnd <= addrStart) {
            continue;
        }
        string address = trimmed.substr(addrStart + 2, addrEnd - addrStart - 2);
        string symbol = Trim(trimmed.substr(addrEnd + 2));
        if (symbol.empty()) {
            continue;
        }
        size_t plusPos = symbol.find('+');
        if (plusPos == string::npos) {
            continue;
        }
        PidStackEntryInternal entry{};
        entry.address = address;
        entry.symbol = symbol;
        entries.push_back(move(entry));
    }
    result.entries = new vector<PidStackEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidStackEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidWchan(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    vector<PidWchanEntryInternal> entries;
    PidWchanEntryInternal entry{};
    entry.wchan = trimmed;
    entries.push_back(move(entry));
    result.entries = new vector<PidWchanEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidWchanEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidMaps(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidMapsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        PidMapsEntryInternal entry{};
        vector<string> parts = SplitLine(trimmed, ' ');
        FillPidMapsEntry(entry, parts);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidMapsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidMapsEntryInternal>*>(p); };
    return SUCCESS;
}

void ProcDataManager::FillPidMapsEntry(PidMapsEntryInternal &entry, const vector<string> &parts)
{
    if (parts.size() >= 1) {
        const string &addr = parts[PidMapsField::ADDR_RANGE];
        size_t dashPos = addr.find('-');
        if (dashPos != string::npos) {
            entry.start = addr.substr(0, dashPos);
            entry.end = addr.substr(dashPos + 1);
        } else {
            entry.start = addr;
        }
    }
    if (parts.size() >= 2) {
        entry.perms = parts[PidMapsField::PERMS];
    }
    if (parts.size() >= 3) {
        entry.offset = parts[PidMapsField::OFFSET];
    }
    if (parts.size() >= 4) {
        entry.dev = parts[PidMapsField::DEV];
    }
    if (parts.size() >= 5) {
        entry.inode = parts[PidMapsField::INODE];
    }
    if (parts.size() >= 6) {
        string pathname;
        for (size_t k = 5; k < parts.size(); k++) {
            if (!pathname.empty()) {
                pathname += " ";
            }
            pathname += parts[k];
        }
        entry.pathname = pathname;
    }
}

int ProcDataManager::ParsePidComm(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    vector<PidCommEntryInternal> entries;
    PidCommEntryInternal entry{};
    entry.comm = trimmed;
    entries.push_back(move(entry));
    result.entries = new vector<PidCommEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidCommEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParsePidTaskStat(const string &content, int pid, ProcDataInternal &result)
{
    vector<PidTaskStatEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        PidTaskStatEntryInternal entry{};
        size_t commStart = trimmed.find('(');
        size_t commEnd = trimmed.rfind(')');
        if (commStart == string::npos || commEnd == string::npos || commEnd <= commStart) {
            continue;
        }
        entry.pid = SafeStoi(Trim(trimmed.substr(0, commStart)));
        entry.comm = trimmed.substr(commStart + 1, commEnd - commStart - 1);
        string afterComm = Trim(trimmed.substr(commEnd + 1));
        vector<string> parts = SplitLine(afterComm, ' ');
        if (!parts.empty() && !parts[0].empty()) {
            entry.state = parts[0][0];
        }
        FillPidStatFields(entry, parts);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PidTaskStatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PidTaskStatEntryInternal>*>(p); };
    return SUCCESS;
}

