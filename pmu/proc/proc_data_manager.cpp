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
 * Description: core proc data management
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

string ProcDataManager::GetFilePath(ProcSource source, int pid)
{
    auto isSysDir = PROC_SOURCE_IS_SYS_DIR.find(source);
    if (isSysDir != PROC_SOURCE_IS_SYS_DIR.end()) {
        return "sys_dir";
    }

    auto it = PROC_SOURCE_PATH.find(source);
    if (it == PROC_SOURCE_PATH.end()) {
        return "";
    }

    auto needPid = PROC_SOURCE_NEED_PID.find(source);
    if (needPid != PROC_SOURCE_NEED_PID.end() && needPid->second) {
        if (pid <= 0) {
            return "";
        }
        string path = it->second;
        size_t pos = path.find("%d");
        if (pos != string::npos) {
            path.replace(pos, 2, to_string(pid));
        }
        return path;
    }
    return it->second;
}

int ProcDataManager::ParseSysDir(const vector<string> &filePaths, int pid, ProcDataInternal &result)
{
    vector<SysDirEntryInternal> entries;
    for (const auto &filePath : filePaths) {
        if (!ExistPath(filePath)) continue;
        string content = ReadFileAllContent(filePath);
        string trimmed = Trim(content);
        if (trimmed.empty()) continue;
        SysDirEntryInternal entry;
        size_t lastSlash = filePath.rfind('/');
        entry.name = (lastSlash != string::npos) ? filePath.substr(lastSlash + 1) : filePath;
        entry.path = filePath;
        entry.value = trimmed;
        entries.push_back(move(entry));
    }
    if (entries.empty()) return LIBPERF_ERR_PROC_PARSE_FAILED;
    result.entries = new vector<SysDirEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<SysDirEntryInternal>*>(p); };
    return SUCCESS;
}


int ProcDataManager::ReadFdDirContent(const string &fdDirPath, string &content)
{
    vector<string> dirEntries = ListDirectoryEntries(fdDirPath);
    for (const auto &e : dirEntries) content += e + "\n";
    return SUCCESS;
}

int ProcDataManager::ReadTaskStatContent(const string &taskDirPath, string &content, string &actualPaths)
{
    vector<string> taskDirs = ListDirectoryEntries(taskDirPath);
    for (const auto &tid : taskDirs) {
        string statPath = taskDirPath + "/" + tid + "/stat";
        if (!ExistPath(statPath)) {
            continue;
        }
        string statContent = ReadFileAllContent(statPath);
        if (statContent.empty()) {
            continue;
        }
        content += statContent;
        if (content.back() != '\n') {
            content += "\n";
        }
        if (!actualPaths.empty()) {
            actualPaths += "\n";
        }
        actualPaths += statPath;
    }
    return SUCCESS;
}

int ProcDataManager::ReadSpecialContent(ProcSource source, const string &filePath, string &content,
                                        string &actualPaths)
{
    if (!ExistPath(filePath)) {
        return LIBPERF_ERR_PROC_FILE_NOT_FOUND;
    }
    if (source == PROC_PID_FD && IsDirectory(filePath)) {
        return ReadFdDirContent(filePath, content);
    }
    if (source == PROC_PID_TASK_STAT && IsDirectory(filePath)) {
        return ReadTaskStatContent(filePath, content, actualPaths);
    }
    content = ReadFileAllContent(filePath);
    return SUCCESS;
}

int ProcDataManager::ParseProcFile(ProcSource source, int pid, const string &filePath, ProcDataInternal &result)
{
    auto isSysDir = PROC_SOURCE_IS_SYS_DIR.find(source);
    if (isSysDir != PROC_SOURCE_IS_SYS_DIR.end()) {
        auto filesIt = SYS_DIR_FILES_MAP.find(source);
        if (filesIt == SYS_DIR_FILES_MAP.end()) {
            return LIBPERF_ERR_PROC_SOURCE_INVALID;
        }
        result.source = source;
        result.pid = pid;
        return ParseSysDir(*(filesIt->second), pid, result);
    }

    string content;
    string actualPaths;
    int readRet = ReadSpecialContent(source, filePath, content, actualPaths);
    if (readRet != SUCCESS) {
        return readRet;
    }
    // 目录型 source（如 PID_TASK_STAT）实际读取多个子文件，用实际路径覆盖入参目录路径。
    if (!actualPaths.empty()) {
        result.filePath = actualPaths;
    }

    if (content.empty() && source != PROC_PID_FD && source != PROC_PID_STACK
        && source != PROC_PID_WCHAN && source != PROC_PID_COMM && source != PROC_PID_TASK_STAT) {
        return LIBPERF_ERR_PROC_READ_FAILED;
    }

    result.source = source;
    result.pid = pid;

    const auto &parsers = GetProcFileParsers();
    auto it = parsers.find(source);
    if (it == parsers.end()) {
        return LIBPERF_ERR_PROC_SOURCE_INVALID;
    }
    return it->second(content, pid, result);
}

const map<ProcSource, ProcDataManager::ParserFunc> &ProcDataManager::GetProcFileParsers()
{
    static const map<ProcSource, ParserFunc> parsers = [this]() {
        map<ProcSource, ParserFunc> m;
        FillSysProcFileParsers(m);
        FillPidProcFileParsers(m);
        m[PROC_IRQ_AFFINITY] = [](const string &c, int p, ProcDataInternal &r) {
            vector<IrqAffinityEntryInternal> entries;
            IrqAffinityEntryInternal e;
            e.affinity = Trim(c);
            entries.push_back(move(e));
            r.entries = new vector<IrqAffinityEntryInternal>(move(entries));
            return SUCCESS;
        };
        return m;
    }();
    return parsers;
}

void ProcDataManager::FillSysProcFileParsers(map<ProcSource, ParserFunc> &m)
{
    m[PROC_STAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParseStat(c, p, r); };
    m[PROC_CPUINFO] = [this](const string &c, int p, ProcDataInternal &r) { return ParseCpuinfo(c, p, r); };
    m[PROC_MEMINFO] = [this](const string &c, int p, ProcDataInternal &r) { return ParseMeminfo(c, p, r); };
    m[PROC_LOADAVG] = [this](const string &c, int p, ProcDataInternal &r) { return ParseLoadavg(c, p, r); };
    m[PROC_VMSTAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParseVmstat(c, p, r); };
    m[PROC_NET_DEV] = [this](const string &c, int p, ProcDataInternal &r) { return ParseNetDev(c, p, r); };
    m[PROC_DISKSTATS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseDiskstats(c, p, r); };
    m[PROC_UPTIME] = [this](const string &c, int p, ProcDataInternal &r) { return ParseUptime(c, p, r); };
    m[PROC_MOUNTS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseMounts(c, p, r); };
    m[PROC_SOFTIRQS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseSoftirqs(c, p, r); };
    m[PROC_SLABINFO] = [this](const string &c, int p, ProcDataInternal &r) { return ParseSlabinfo(c, p, r); };
    m[PROC_SCHEDSTAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParseSchedstat(c, p, r); };
    m[PROC_INTERRUPTS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseInterrupts(c, p, r); };
    m[PROC_LOCKS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseLocks(c, p, r); };
    m[PROC_ZONEINFO] = [this](const string &c, int p, ProcDataInternal &r) { return ParseZoneinfo(c, p, r); };
    m[PROC_BUDDYINFO] = [this](const string &c, int p, ProcDataInternal &r) { return ParseBuddyinfo(c, p, r); };
    m[PROC_NET_SOCKSTAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParseNetSockstat(c, p, r); };
    m[PROC_NET_NETSTAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParseNetNetstat(c, p, r); };
    m[PROC_NET_ARP] = [this](const string &c, int p, ProcDataInternal &r) { return ParseNetArp(c, p, r); };
    m[PROC_VERSION] = [this](const string &c, int p, ProcDataInternal &r) { return ParseVersion(c, p, r); };
    m[PROC_MODULES] = [this](const string &c, int p, ProcDataInternal &r) { return ParseModules(c, p, r); };
    m[PROC_FILESYSTEMS] = [this](const string &c, int p, ProcDataInternal &r) { return ParseFilesystems(c, p, r); };
    m[PROC_SCSI] = [this](const string &c, int p, ProcDataInternal &r) { return ParseScsi(c, p, r); };
    m[PROC_PRESSURE_CPU] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePressure(c, p, r); };
    m[PROC_PRESSURE_IO] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePressure(c, p, r); };
}

void ProcDataManager::FillPidProcFileParsers(map<ProcSource, ParserFunc> &m)
{
    m[PROC_PID_STAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidStat(c, p, r); };
    m[PROC_PID_STATM] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidStatm(c, p, r); };
    m[PROC_PID_STATUS] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidStatus(c, p, r); };
    m[PROC_PID_IO] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidIo(c, p, r); };
    m[PROC_PID_SMAPS_ROLLUP] = [this](const string &c, int p, ProcDataInternal &r)
            { return ParsePidSmapsRollup(c, p, r); };
    m[PROC_PID_FD] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidFd(c, p, r); };
    m[PROC_PID_NUMA_MAPS] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidNumaMaps(c, p, r); };
    m[PROC_PID_SMAPS] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidSmaps(c, p, r); };
    m[PROC_PID_ENVIRON] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidEnviron(c, p, r); };
    m[PROC_PID_CMDLINE] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidCmdline(c, p, r); };
    m[PROC_PID_LIMITS] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidLimits(c, p, r); };
    m[PROC_PID_STACK] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidStack(c, p, r); };
    m[PROC_PID_WCHAN] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidWchan(c, p, r); };
    m[PROC_PID_MAPS] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidMaps(c, p, r); };
    m[PROC_PID_COMM] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidComm(c, p, r); };
    m[PROC_PID_TASK_STAT] = [this](const string &c, int p, ProcDataInternal &r) { return ParsePidTaskStat(c, p, r); };
}

int ProcDataManager::ConvertToCStruct(const vector<ProcDataInternal> &internalData, struct ProcData **data,
                                            unsigned *numData)
{
    if (internalData.empty()) {
        *data = nullptr;
        *numData = 0;
        return SUCCESS;
    }
    *numData = internalData.size();
    *data = new(std::nothrow) ProcData[internalData.size()]();
    if (*data == nullptr) {
        *numData = 0;
        pcerr::New(LIBPERF_ERR_PROC_DATA_NULL);
        return LIBPERF_ERR_PROC_DATA_NULL;
    }
    const auto &convFns = GetConvertFns();
    for (size_t i = 0; i < internalData.size(); i++) {
        (*data)[i].source = internalData[i].source;
        (*data)[i].pid = internalData[i].pid;
        (*data)[i].filePath = AllocStr(internalData[i].filePath);
        auto it = convFns.find(internalData[i].source);
        if (it != convFns.end()) {
            (this->*(it->second))(internalData[i], (*data)[i]);
        }
    }
    return SUCCESS;
}

const map<ProcSource, ProcDataManager::ConvFn> &ProcDataManager::GetConvertFns()
{
    static const map<ProcSource, ConvFn> convFns = [this]() {
        map<ProcSource, ConvFn> m;
        FillSysConvertFns(m);
        FillPidConvertFns(m);
        return m;
    }();
    return convFns;
}

void ProcDataManager::FillSysConvertFns(map<ProcSource, ConvFn> &m)
{
    m[PROC_STAT] = &ProcDataManager::ConvertStat;
    m[PROC_CPUINFO] = &ProcDataManager::ConvertCpuinfo;
    m[PROC_MEMINFO] = &ProcDataManager::ConvertMeminfo;
    m[PROC_LOADAVG] = &ProcDataManager::ConvertLoadavg;
    m[PROC_VMSTAT] = &ProcDataManager::ConvertVmstat;
    m[PROC_NET_DEV] = &ProcDataManager::ConvertNetDev;
    m[PROC_DISKSTATS] = &ProcDataManager::ConvertDiskstats;
    m[PROC_UPTIME] = &ProcDataManager::ConvertUptime;
    m[PROC_MOUNTS] = &ProcDataManager::ConvertMounts;
    m[PROC_SOFTIRQS] = &ProcDataManager::ConvertSoftirqs;
    m[PROC_SLABINFO] = &ProcDataManager::ConvertSlabinfo;
    m[PROC_SCHEDSTAT] = &ProcDataManager::ConvertSchedstat;
    m[PROC_INTERRUPTS] = &ProcDataManager::ConvertInterrupts;
    m[PROC_IRQ_AFFINITY] = &ProcDataManager::ConvertIrqAffinity;
    m[PROC_LOCKS] = &ProcDataManager::ConvertLocks;
    m[PROC_ZONEINFO] = &ProcDataManager::ConvertZoneinfo;
    m[PROC_BUDDYINFO] = &ProcDataManager::ConvertBuddyinfo;
    m[PROC_NET_SOCKSTAT] = &ProcDataManager::ConvertNetSockstat;
    m[PROC_NET_NETSTAT] = &ProcDataManager::ConvertNetNetstat;
    m[PROC_NET_ARP] = &ProcDataManager::ConvertNetArp;
    m[PROC_VERSION] = &ProcDataManager::ConvertVersion;
    m[PROC_MODULES] = &ProcDataManager::ConvertModules;
    m[PROC_FILESYSTEMS] = &ProcDataManager::ConvertFilesystems;
    m[PROC_SCSI] = &ProcDataManager::ConvertScsi;
    m[PROC_PRESSURE_CPU] = &ProcDataManager::ConvertPressure;
    m[PROC_PRESSURE_IO] = &ProcDataManager::ConvertPressure;
    m[PROC_SYS_KERNEL] = &ProcDataManager::ConvertSysDir;
    m[PROC_SYS_FS] = &ProcDataManager::ConvertSysDir;
    m[PROC_SYS_VM] = &ProcDataManager::ConvertSysDir;
    m[PROC_SYS_NET_IPV4] = &ProcDataManager::ConvertSysDir;
    m[PROC_SYS_NET_CORE] = &ProcDataManager::ConvertSysDir;
}

void ProcDataManager::FillPidConvertFns(map<ProcSource, ConvFn> &m)
{
    m[PROC_PID_STAT] = &ProcDataManager::ConvertPidStat;
    m[PROC_PID_STATM] = &ProcDataManager::ConvertPidStatm;
    m[PROC_PID_STATUS] = &ProcDataManager::ConvertPidStatus;
    m[PROC_PID_IO] = &ProcDataManager::ConvertPidIo;
    m[PROC_PID_SMAPS_ROLLUP] = &ProcDataManager::ConvertPidSmapsRollup;
    m[PROC_PID_FD] = &ProcDataManager::ConvertPidFd;
    m[PROC_PID_NUMA_MAPS] = &ProcDataManager::ConvertPidNumaMaps;
    m[PROC_PID_SMAPS] = &ProcDataManager::ConvertPidSmaps;
    m[PROC_PID_ENVIRON] = &ProcDataManager::ConvertPidEnviron;
    m[PROC_PID_CMDLINE] = &ProcDataManager::ConvertPidCmdline;
    m[PROC_PID_LIMITS] = &ProcDataManager::ConvertPidLimits;
    m[PROC_PID_STACK] = &ProcDataManager::ConvertPidStack;
    m[PROC_PID_WCHAN] = &ProcDataManager::ConvertPidWchan;
    m[PROC_PID_MAPS] = &ProcDataManager::ConvertPidMaps;
    m[PROC_PID_COMM] = &ProcDataManager::ConvertPidComm;
    m[PROC_PID_TASK_STAT] = &ProcDataManager::ConvertPidTaskStat;
}

int ProcDataManager::ReadProcData(int pid, struct ProcData **data, unsigned *numData)
{
    if (!data || !numData) {
        pcerr::New(LIBPERF_ERR_PROC_DATA_NULL);
        return -1;
    }

    vector<ProcDataInternal> results;
    for (const auto &source : SYSTEM_SOURCES) {
        ProcDataInternal result;
        string filePath = GetFilePath(source, 0);
        result.filePath = filePath;
        int ret = ParseProcFile(source, 0, filePath, result);
        if (ret == SUCCESS) {
            results.push_back(move(result));
        }
    }

    if (pid > 0) {
        for (const auto &source : PID_SOURCES) {
            ProcDataInternal result;
            string filePath = GetFilePath(source, pid);
            if (filePath.empty()) {
                continue;
            }
            result.filePath = filePath;
            int ret = ParseProcFile(source, pid, filePath, result);
            if (ret == SUCCESS) {
                results.push_back(move(result));
            }
        }
    }

    if (results.empty()) {
        *data = nullptr;
        *numData = 0;
        return SUCCESS;
    }

    int ret = ConvertToCStruct(results, data, numData);
    if (ret != SUCCESS) {
        *data = nullptr;
        *numData = 0;
        return ret;
    }

    return SUCCESS;
}

void ProcDataManager::FreeProcData(struct ProcData *data, unsigned numData)
{
    if (!data) {
        return;
    }
    const auto &freeFns = GetFreeFns();
    for (unsigned i = 0; i < numData; i++) {
        auto it = freeFns.find(data[i].source);
        if (it != freeFns.end()) {
            (this->*(it->second))(data[i]);
        }
        delete[] data[i].filePath;
    }
    delete[] data;
}

const map<ProcSource, ProcDataManager::FreeFn> &ProcDataManager::GetFreeFns()
{
    static const map<ProcSource, FreeFn> freeFns = [this]() {
        map<ProcSource, FreeFn> m;
        FillSysFreeFns(m);
        FillPidFreeFns(m);
        return m;
    }();
    return freeFns;
}

void ProcDataManager::FillSysFreeFns(map<ProcSource, FreeFn> &m)
{
    m[PROC_STAT] = &ProcDataManager::FreeStat;
    m[PROC_CPUINFO] = &ProcDataManager::FreeCpuinfo;
    m[PROC_MEMINFO] = &ProcDataManager::FreeMeminfo;
    m[PROC_LOADAVG] = &ProcDataManager::FreeLoadavg;
    m[PROC_VMSTAT] = &ProcDataManager::FreeVmstat;
    m[PROC_NET_DEV] = &ProcDataManager::FreeNetDev;
    m[PROC_DISKSTATS] = &ProcDataManager::FreeDiskstats;
    m[PROC_UPTIME] = &ProcDataManager::FreeUptime;
    m[PROC_MOUNTS] = &ProcDataManager::FreeMounts;
    m[PROC_SOFTIRQS] = &ProcDataManager::FreeSoftirqs;
    m[PROC_SLABINFO] = &ProcDataManager::FreeSlabinfo;
    m[PROC_SCHEDSTAT] = &ProcDataManager::FreeSchedstat;
    m[PROC_INTERRUPTS] = &ProcDataManager::FreeInterrupts;
    m[PROC_IRQ_AFFINITY] = &ProcDataManager::FreeIrqAffinity;
    m[PROC_LOCKS] = &ProcDataManager::FreeLocks;
    m[PROC_ZONEINFO] = &ProcDataManager::FreeZoneinfo;
    m[PROC_BUDDYINFO] = &ProcDataManager::FreeBuddyinfo;
    m[PROC_NET_SOCKSTAT] = &ProcDataManager::FreeNetSockstat;
    m[PROC_NET_NETSTAT] = &ProcDataManager::FreeNetNetstat;
    m[PROC_NET_ARP] = &ProcDataManager::FreeNetArp;
    m[PROC_VERSION] = &ProcDataManager::FreeVersion;
    m[PROC_MODULES] = &ProcDataManager::FreeModules;
    m[PROC_FILESYSTEMS] = &ProcDataManager::FreeFilesystems;
    m[PROC_SCSI] = &ProcDataManager::FreeScsi;
    m[PROC_PRESSURE_CPU] = &ProcDataManager::FreePressure;
    m[PROC_PRESSURE_IO] = &ProcDataManager::FreePressure;
    m[PROC_SYS_KERNEL] = &ProcDataManager::FreeSysDir;
    m[PROC_SYS_FS] = &ProcDataManager::FreeSysDir;
    m[PROC_SYS_VM] = &ProcDataManager::FreeSysDir;
    m[PROC_SYS_NET_IPV4] = &ProcDataManager::FreeSysDir;
    m[PROC_SYS_NET_CORE] = &ProcDataManager::FreeSysDir;
}

void ProcDataManager::FillPidFreeFns(map<ProcSource, FreeFn> &m)
{
    m[PROC_PID_STAT] = &ProcDataManager::FreePidStat;
    m[PROC_PID_STATM] = &ProcDataManager::FreePidStatm;
    m[PROC_PID_STATUS] = &ProcDataManager::FreePidStatus;
    m[PROC_PID_IO] = &ProcDataManager::FreePidIo;
    m[PROC_PID_SMAPS_ROLLUP] = &ProcDataManager::FreePidSmapsRollup;
    m[PROC_PID_FD] = &ProcDataManager::FreePidFd;
    m[PROC_PID_NUMA_MAPS] = &ProcDataManager::FreePidNumaMaps;
    m[PROC_PID_SMAPS] = &ProcDataManager::FreePidSmaps;
    m[PROC_PID_ENVIRON] = &ProcDataManager::FreePidEnviron;
    m[PROC_PID_CMDLINE] = &ProcDataManager::FreePidCmdline;
    m[PROC_PID_LIMITS] = &ProcDataManager::FreePidLimits;
    m[PROC_PID_STACK] = &ProcDataManager::FreePidStack;
    m[PROC_PID_WCHAN] = &ProcDataManager::FreePidWchan;
    m[PROC_PID_MAPS] = &ProcDataManager::FreePidMaps;
    m[PROC_PID_COMM] = &ProcDataManager::FreePidComm;
    m[PROC_PID_TASK_STAT] = &ProcDataManager::FreePidTaskStat;
}

extern "C" {
int ProcDataRead(int pid, struct ProcData **data, unsigned *numData)
{
    return KUNPENG_PMU::ProcDataManager::GetInstance()->ReadProcData(pid, data, numData);
}

void ProcDataFree(struct ProcData *data, unsigned numData)
{
    KUNPENG_PMU::ProcDataManager::GetInstance()->FreeProcData(data, numData);
}
}