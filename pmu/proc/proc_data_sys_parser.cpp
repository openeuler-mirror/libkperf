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
 * Description: system-level proc data parsing functions
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

int ProcDataManager::ParseStat(const string &content, int pid, ProcDataInternal &result)
{
    const auto &handlers = GetStatHandlers();
    vector<StatEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        if (line.empty()) {
            continue;
        }
        size_t spacePos = line.find(' ');
        if (spacePos == string::npos) {
            continue;
        }
        string name = Trim(line.substr(0, spacePos));
        string rest = Trim(line.substr(spacePos + 1));
        vector<string> values = SplitLine(rest, ' ');
        StatEntryInternal entry{};
        entry.cpu_name = name;
        if (name.rfind("cpu", 0) == 0) {
            FillStatCpuEntry(entry, values);
        } else {
            auto it = handlers.find(name);
            if (it == handlers.end()) {
                continue;
            }
            it->second(entry, values);
        }
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<StatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<StatEntryInternal>*>(p); };
    return SUCCESS;
}

const map<string, ProcDataManager::StatHandler> &ProcDataManager::GetStatHandlers()
{
    static const map<string, StatHandler> handlers = {
        {"intr", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_INTR;
            e.intr_total = SafeGetUll(v, 0);
            for (size_t k = 1; k < v.size(); k++) e.intr_per_irq.push_back(SafeStoull(v[k]));
        }},
        {"ctxt", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_CTXT;
            e.ctxt = SafeGetUll(v, 0);
        }},
        {"btime", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_BTIME;
            e.btime = SafeGetUll(v, 0);
        }},
        {"processes", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_PROCESSES;
            e.processes = SafeGetUll(v, 0);
        }},
        {"procs_running", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_PROCS_RUNNING;
            e.procs_running = SafeGetUll(v, 0);
        }},
        {"procs_blocked", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_PROCS_BLOCKED;
            e.procs_blocked = SafeGetUll(v, 0);
        }},
        {"softirq", [](StatEntryInternal& e, const vector<string>& v) {
            e.lineType = PROC_STAT_LINE_SOFTIRQ;
            e.softirq_total = SafeGetUll(v, 0);
            for (size_t k = 1; k < v.size(); k++) e.softirq_per_type.push_back(SafeStoull(v[k]));
        }},
    };
    return handlers;
}

void ProcDataManager::FillStatCpuEntry(StatEntryInternal &entry, const vector<string> &values)
{
    entry.lineType = PROC_STAT_LINE_CPU;
    entry.user = SafeGetUll(values, StatField::USER);
    entry.nice = SafeGetUll(values, StatField::NICE);
    entry.system = SafeGetUll(values, StatField::SYSTEM);
    entry.idle = SafeGetUll(values, StatField::STAT_IDLE);
    entry.iowait = SafeGetUll(values, StatField::IOWAIT);
    entry.irq = SafeGetUll(values, StatField::IRQ);
    entry.softirq = SafeGetUll(values, StatField::SOFTIRQ);
    entry.steal = SafeGetUll(values, StatField::STEAL);
    entry.guest = SafeGetUll(values, StatField::GUEST);
    entry.guest_nice = SafeGetUll(values, StatField::GUEST_NICE);
}

int ProcDataManager::ParseCpuinfo(const string &content, int pid, ProcDataInternal &result)
{
    vector<CpuinfoEntryInternal> entries;
    istringstream iss(content);
    string line;
    CpuinfoEntryInternal currentEntry;
    bool hasEntry = false;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            if (hasEntry) {
                entries.push_back(move(currentEntry));
                currentEntry = CpuinfoEntryInternal();
                hasEntry = false;
            }
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos != string::npos) {
            currentEntry.fields.emplace_back(Trim(trimmed.substr(0, colonPos)), Trim(trimmed.substr(colonPos + 1)));
            hasEntry = true;
        }
    }
    if (hasEntry) {
        entries.push_back(move(currentEntry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<CpuinfoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<CpuinfoEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseMeminfo(const string &content, int pid, ProcDataInternal &result)
{
    vector<MeminfoEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        size_t colonPos = line.find(':');
        if (colonPos == string::npos) {
            continue;
        }
        string key = Trim(line.substr(0, colonPos));
        string rest = Trim(line.substr(colonPos + 1));
        MeminfoEntryInternal entry{};
        entry.key = key;
        vector<string> parts = SplitLine(rest, ' ');
        entry.value = SafeGetUll(parts, 0);
        entry.unit = parts.size() > 1 ? parts[1] : "";
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<MeminfoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<MeminfoEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseLoadavg(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    if (trimmed.empty()) return LIBPERF_ERR_PROC_PARSE_FAILED;
    vector<LoadavgEntryInternal> entries;
    LoadavgEntryInternal entry{};
    vector<string> parts = SplitLine(trimmed, ' ');
        // parts field indices (/proc file format)
    entry.load1 = SafeGetDouble(parts, LoadavgField::LOAD1);
    entry.load5 = SafeGetDouble(parts, LoadavgField::LOAD5);
    entry.load15 = SafeGetDouble(parts, LoadavgField::LOAD15);
    if (parts.size() > 3) {
        size_t slashPos = parts[LoadavgField::RUNNING_TOTAL_PROCS].find('/');
        if (slashPos != string::npos) {
            entry.running_procs = SafeStoi(parts[LoadavgField::RUNNING_TOTAL_PROCS].substr(0, slashPos));
            entry.total_procs = SafeStoi(parts[LoadavgField::RUNNING_TOTAL_PROCS].substr(slashPos + 1));
        }
    }
    if (parts.size() > 4) {
        entry.last_pid = SafeStoi(parts[LoadavgField::LAST_PID]);
    }
    entries.push_back(move(entry));
    result.entries = new vector<LoadavgEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<LoadavgEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseVmstat(const string &content, int pid, ProcDataInternal &result)
{
    vector<VmstatEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        vector<string> parts = SplitLine(line, ' ');
        // parts field indices (/proc file format)
        if (parts.size() >= 2) {
            VmstatEntryInternal entry{};
            entry.key = parts[VmstatField::KEY];
            entry.value = SafeStoull(parts[VmstatField::VALUE]);
            entries.push_back(move(entry));
        }
    }
    if (entries.empty()) return LIBPERF_ERR_PROC_PARSE_FAILED;
    result.entries = new vector<VmstatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<VmstatEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseNetDev(const string &content, int pid, ProcDataInternal &result)
{
    vector<NetDevEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        size_t colonPos = line.find(':');
        if (colonPos == string::npos) {
            continue;
        }
        string iface = Trim(line.substr(0, colonPos));
        string values = Trim(line.substr(colonPos + 1));
        vector<string> parts = SplitLine(values, ' ');
        // parts field indices (/proc file format)
        NetDevEntryInternal entry{};
        entry.iface = iface;
        entry.rx_bytes = SafeGetUll(parts, NetDevField::RX_BYTES);
        entry.rx_packets = SafeGetUll(parts, NetDevField::RX_PACKETS);
        entry.rx_errs = SafeGetUll(parts, NetDevField::RX_ERRS);
        entry.rx_drop = SafeGetUll(parts, NetDevField::RX_DROP);
        entry.rx_fifo = SafeGetUll(parts, NetDevField::RX_FIFO);
        entry.rx_frame = SafeGetUll(parts, NetDevField::RX_FRAME);
        entry.rx_compressed = SafeGetUll(parts, NetDevField::RX_COMPRESSED);
        entry.rx_multicast = SafeGetUll(parts, NetDevField::RX_MULTICAST);
        entry.tx_bytes = SafeGetUll(parts, NetDevField::TX_BYTES);
        entry.tx_packets = SafeGetUll(parts, NetDevField::TX_PACKETS);
        entry.tx_errs = SafeGetUll(parts, NetDevField::TX_ERRS);
        entry.tx_drop = SafeGetUll(parts, NetDevField::TX_DROP);
        entry.tx_fifo = SafeGetUll(parts, NetDevField::TX_FIFO);
        entry.tx_colls = SafeGetUll(parts, NetDevField::TX_COLLS);
        entry.tx_carrier = SafeGetUll(parts, NetDevField::TX_CARRIER);
        entry.tx_compressed = SafeGetUll(parts, NetDevField::TX_COMPRESSED);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<NetDevEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<NetDevEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseDiskstats(const string &content, int pid, ProcDataInternal &result)
{
    vector<DiskstatsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        vector<string> parts = SplitLine(line, ' ');
        // parts field indices (/proc file format)
        if (parts.size() < 3) {
            continue;
        }
        DiskstatsEntryInternal entry{};
        entry.major = SafeStoi(parts[DiskstatsField::MAJOR]);
        entry.minor = SafeStoi(parts[DiskstatsField::MINOR]);
        entry.device = parts[DiskstatsField::DISK_DEVICE];
        entry.reads_completed = SafeGetUll(parts, DiskstatsField::READS_COMPLETED);
        entry.reads_merged = SafeGetUll(parts, DiskstatsField::READS_MERGED);
        entry.sectors_read = SafeGetUll(parts, DiskstatsField::SECTORS_READ);
        entry.ms_reading = SafeGetUll(parts, DiskstatsField::MS_READING);
        entry.writes_completed = SafeGetUll(parts, DiskstatsField::WRITES_COMPLETED);
        entry.writes_merged = SafeGetUll(parts, DiskstatsField::WRITES_MERGED);
        entry.sectors_written = SafeGetUll(parts, DiskstatsField::SECTORS_WRITTEN);
        entry.ms_writing = SafeGetUll(parts, DiskstatsField::MS_WRITING);
        entry.ios_in_progress = SafeGetUll(parts, DiskstatsField::IOS_IN_PROGRESS);
        entry.ms_ios = SafeGetUll(parts, DiskstatsField::MS_IOS);
        entry.weighted_ms_ios = SafeGetUll(parts, DiskstatsField::WEIGHTED_MS_IOS);
        entry.discards_completed = SafeGetUll(parts, DiskstatsField::DISCARDS_COMPLETED);
        entry.discards_merged = SafeGetUll(parts, DiskstatsField::DISCARDS_MERGED);
        entry.sectors_discarded = SafeGetUll(parts, DiskstatsField::SECTORS_DISCARDED);
        entry.ms_discarding = SafeGetUll(parts, DiskstatsField::MS_DISCARDING);
        entry.flush_completed = SafeGetUll(parts, DiskstatsField::FLUSH_COMPLETED);
        entry.ms_flushing = SafeGetUll(parts, DiskstatsField::MS_FLUSHING);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<DiskstatsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<DiskstatsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseUptime(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    if (trimmed.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    vector<UptimeEntryInternal> entries;
    UptimeEntryInternal entry{};
    vector<string> parts = SplitLine(trimmed, ' ');
        // parts field indices (/proc file format)
    entry.total = SafeGetDouble(parts, UptimeField::TOTAL);
    entry.idle = SafeGetDouble(parts, UptimeField::UPTIME_IDLE);
    entries.push_back(move(entry));
    result.entries = new vector<UptimeEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<UptimeEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseMounts(const string &content, int pid, ProcDataInternal &result)
{
    vector<MountsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        // parts field indices (/proc file format)
        if (parts.size() < 6) {
            continue;
        }
        MountsEntryInternal entry{};
        entry.device = parts[MountsField::MOUNT_DEVICE];
        entry.mount_point = parts[MountsField::MOUNT_POINT];
        entry.fs_type = parts[MountsField::FS_TYPE];
        entry.options = parts[MountsField::OPTIONS];
        entry.dump = SafeStoi(parts[MountsField::DUMP]);
        entry.pass_val = SafeStoi(parts[MountsField::PASS]);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<MountsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<MountsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseSoftirqs(const string &content, int pid, ProcDataInternal &result)
{
    vector<SoftirqsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos == string::npos) {
            continue;
        }
        SoftirqsEntryInternal entry{};
        entry.type = Trim(trimmed.substr(0, colonPos));
        string values = Trim(trimmed.substr(colonPos + 1));
        vector<string> parts = SplitLine(values, ' ');
        for (const auto &p : parts) entry.per_cpu.push_back(SafeStoull(p));
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<SoftirqsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<SoftirqsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseSlabinfo(const string &content, int pid, ProcDataInternal &result)
{
    vector<SlabinfoEntryInternal> entries;
    istringstream iss(content);
    string line;
    bool headerParsed = false;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (!headerParsed && trimmed.find("slabinfo") == 0) {
            headerParsed = true;
            continue;
        }
        if (trimmed[0] == '#') {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        vector<string> values;
        for (const auto &p : parts) {
            if (p != ":" && p != "tunables" && p != "slabdata") {
                values.push_back(p);
            }
        }
        if (values.size() < 3) {
            continue;
        }
        SlabinfoEntryInternal entry{};
        entry.name = values[SlabinfoField::NAME];
        entry.active_objs = SafeGetUll(values, SlabinfoField::ACTIVE_OBJS);
        entry.num_objs = SafeGetUll(values, SlabinfoField::NUM_OBJS);
        entry.objsize = SafeGetUll(values, SlabinfoField::OBJSIZE);
        entry.objperslab = SafeGetUll(values, SlabinfoField::OBJPERSLAB);
        entry.pagesperslab = SafeGetUll(values, SlabinfoField::PAGESPERSLAB);
        entry.limit = SafeGetUll(values, SlabinfoField::LIMIT);
        entry.batchcount = SafeGetUll(values, SlabinfoField::BATCHCOUNT);
        entry.sharedfactor = SafeGetUll(values, SlabinfoField::SHAREDFACTOR);
        entry.active_slabs = SafeGetUll(values, SlabinfoField::ACTIVE_SLABS);
        entry.num_slabs = SafeGetUll(values, SlabinfoField::NUM_SLABS);
        entry.sharedavail = SafeGetUll(values, SlabinfoField::SHAREDAVAIL);
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<SlabinfoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<SlabinfoEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseSchedstat(const string &content, int pid, ProcDataInternal &result)
{
    vector<SchedstatEntryInternal> entries;
    istringstream iss(content);
    string line;
    SchedstatEntryInternal *currentCpu = nullptr;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.empty()) {
            continue;
        }
        if (parts[0] == "version" || parts[0] == "timestamp") {
            continue;
        }
        if (parts[0] == "eas" || parts[0] == "cpufreq") {
            continue;
        }
        if (parts[0].find("domain") == 0) {
            FillSchedstatDomain(currentCpu, parts);
            continue;
        }
        if (parts[0].find("cpu") == 0) {
            SchedstatEntryInternal entry{};
            string cpuStr = parts[0];
            size_t cpuNumPos = cpuStr.find_first_of("0123456789");
            entry.cpu_id = cpuNumPos != string::npos ? SafeStoi(cpuStr.substr(cpuNumPos)) : 0;
            FillSchedstatCpuEntry(entry, parts);
            entries.push_back(move(entry));
            currentCpu = &entries.back();
        }
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<SchedstatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<SchedstatEntryInternal>*>(p); };
    return SUCCESS;
}

void ProcDataManager::FillSchedstatCpuEntry(SchedstatEntryInternal &entry, const vector<string> &parts)
{
    entry.yld_count = SafeGetUll(parts, SchedstatField::YLD_COUNT);
    entry.sched_count = SafeGetUll(parts, SchedstatField::SCHED_COUNT);
    entry.sched_goidle = SafeGetUll(parts, SchedstatField::SCHED_GOIDLE);
    entry.ttwu_count = SafeGetUll(parts, SchedstatField::TTWU_COUNT);
    entry.ttwu_local = SafeGetUll(parts, SchedstatField::TTWU_LOCAL);
    entry.rq_cpu_time = SafeGetUll(parts, SchedstatField::RQ_CPU_TIME);
    entry.run_delay = SafeGetUll(parts, SchedstatField::RUN_DELAY);
    entry.pcount = SafeGetUll(parts, SchedstatField::PCOUNT);
}

void ProcDataManager::FillSchedstatDomain(SchedstatEntryInternal *currentCpu, const vector<string> &parts)
{
    if (!currentCpu) {
        return;
    }
    SchedstatDomainInternal domain{};
    string domStr = parts[0];
    size_t numPos = domStr.find_first_of("0123456789");
    domain.domain_id = numPos != string::npos ? SafeStoi(domStr.substr(numPos)) : 0;
    if (parts.size() > 1) {
        domain.mask = parts[SchedstatField::YLD_COUNT];
    }
    for (size_t k = 2; k < parts.size(); k++) {
        domain.values.push_back(SafeStoull(parts[k]));
    }
    currentCpu->domains.push_back(move(domain));
}

int ProcDataManager::ParseInterrupts(const string &content, int pid, ProcDataInternal &result)
{
    vector<InterruptsEntryInternal> entries;
    istringstream iss(content);
    string line;
    vector<string> cpuNames;
    bool firstLine = true;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (firstLine) {
            cpuNames = SplitLine(trimmed, ' ');
            firstLine = false;
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.empty()) {
            continue;
        }
        InterruptsEntryInternal entry{};
        string irqName = parts[0];
        size_t colonPos = irqName.find(':');
        if (colonPos != string::npos) {
            entry.irq = irqName.substr(0, colonPos);
        } else {
            entry.irq = irqName;
        }
        size_t valueStart = 1;
        for (size_t k = 0; k < cpuNames.size() && (k + valueStart) < parts.size(); k++) {
            entry.per_cpu.push_back(SafeStoull(parts[k + valueStart]));
        }
        string desc;
        for (size_t k = valueStart + cpuNames.size(); k < parts.size(); k++) {
            if (!desc.empty()) {
                desc += " ";
            }
            desc += parts[k];
        }
        entry.description = desc;
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<InterruptsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<InterruptsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseLocks(const string &content, int pid, ProcDataInternal &result)
{
    vector<LocksEntryInternal> entries;
    istringstream iss(content);
    string line;
    LocksEntryInternal currentEntry;
    bool hasEntry = false;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            if (hasEntry) {
                entries.push_back(move(currentEntry));
                currentEntry = LocksEntryInternal();
                hasEntry = false;
            }
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos != string::npos) {
            currentEntry.fields.emplace_back(Trim(trimmed.substr(0, colonPos)), Trim(trimmed.substr(colonPos + 1)));
            hasEntry = true;
        }
    }
    if (hasEntry) {
        entries.push_back(move(currentEntry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<LocksEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<LocksEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseZoneinfo(const string &content, int pid, ProcDataInternal &result)
{
    const int NONE = 0;
    const int NODE_STATS = 1;
    const int ZONE_STATS = 2;
    const int PAGESETS = 3;
    vector<ZoneinfoEntryInternal> entries;
    istringstream iss(content);
    string line;
    ZoneinfoEntryInternal entry;
    bool hasEntry = false;
    int state = NONE;
    ZoneinfoPagesetInternal currentPageset;
    bool hasPageset = false;

    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (HandleZoneinfoZoneStart(entries, entry, hasEntry, currentPageset, hasPageset, state, trimmed)) {
            continue;
        }
        if (!hasEntry) {
            continue;
        }
        if (HandleZoneinfoStateSwitch(entry, state, hasPageset, trimmed)) {
            continue;
        }
        switch (state) {
            case NODE_STATS:
                HandleZoneinfoNodeStats(entry, trimmed);
                break;
            case ZONE_STATS:
                HandleZoneinfoZoneStats(entry, trimmed);
                break;
            case PAGESETS:
                HandleZoneinfoPagesets(entry, currentPageset, hasPageset, trimmed);
                break;
            default:
                break;
        }
    }
    FinalizeZoneinfoEntry(entries, entry, hasEntry, currentPageset, hasPageset);
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<ZoneinfoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<ZoneinfoEntryInternal>*>(p); };
    return SUCCESS;
}

bool ProcDataManager::HandleZoneinfoZoneStart(vector<ZoneinfoEntryInternal> &entries, ZoneinfoEntryInternal &entry,
                                              bool &hasEntry, ZoneinfoPagesetInternal &currentPageset,
                                              bool &hasPageset, int &state, const string &line)
{
    if (line.find("Node") != 0 || line.find("zone") == string::npos) {
        return false;
    }
    if (hasEntry) {
        if (hasPageset) {
            entry.pagesets.push_back(move(currentPageset));
        }
        entries.push_back(move(entry));
        entry = ZoneinfoEntryInternal();
    }
    state = 0; // NONE
    hasPageset = false;
    HandleZoneinfoNewZone(entry, line);
    hasEntry = true;
    return true;
}

bool ProcDataManager::HandleZoneinfoStateSwitch(ZoneinfoEntryInternal &entry, int &state,
                                                bool &hasPageset, const string &line)
{
    if (line == "per-node stats") {
        state = 1; // NODE_STATS
        return true;
    }
    if (line == "pagesets") {
        state = 3; // PAGESETS
        hasPageset = false;
        return true;
    }
    if (line.find("pages free") == 0) {
        state = 2; // ZONE_STATS
        vector<string> parts = SplitLine(line, ' ');
        if (!parts.empty()) {
            entry.pages_free = SafeStoull(parts.back());
        }
        return true;
    }
    return false;
}

void ProcDataManager::FinalizeZoneinfoEntry(vector<ZoneinfoEntryInternal> &entries, ZoneinfoEntryInternal &entry,
                                            bool hasEntry, ZoneinfoPagesetInternal &currentPageset, bool hasPageset)
{
    if (!hasEntry) {
        return;
    }
    if (hasPageset) {
        entry.pagesets.push_back(move(currentPageset));
    }
    entries.push_back(move(entry));
}

void ProcDataManager::HandleZoneinfoNewZone(ZoneinfoEntryInternal &entry, const string &line)
{
    size_t commaPos = line.find(',');
    size_t zonePos = line.find("zone");
    if (commaPos != string::npos) {
        entry.node = SafeStoi(Trim(line.substr(4, commaPos - 4)));
    }
    if (zonePos != string::npos) {
        entry.zone = Trim(line.substr(zonePos + 4));
    }
}

void ProcDataManager::HandleZoneinfoNodeStats(ZoneinfoEntryInternal &entry, const string &line)
{
    // per-node stats 区域：key value（空格分隔）
    size_t spacePos = line.find(' ');
    if (spacePos == string::npos) {
        return;
    }
    string key = Trim(line.substr(0, spacePos));
    string value = Trim(line.substr(spacePos + 1));
    entry.nodeStats.emplace_back(key, value);
}

void ProcDataManager::HandleZoneinfoZoneStats(ZoneinfoEntryInternal &entry, const string &line)
{
    // zone 区域：key: value（冒号分隔）
    size_t colonPos = line.find(':');
    if (colonPos == string::npos) {
        return;
    }
    string key = Trim(line.substr(0, colonPos));
    string value = Trim(line.substr(colonPos + 1));
    HandleZoneField(entry, key, value);
}

void ProcDataManager::HandleZoneinfoPagesets(ZoneinfoEntryInternal &entry, ZoneinfoPagesetInternal &currentPageset,
                                             bool &hasPageset, const string &line)
{
    // pagesets 区域：key: value（冒号分隔）
    size_t colonPos = line.find(':');
    if (colonPos == string::npos) {
        return;
    }
    string key = Trim(line.substr(0, colonPos));
    string value = Trim(line.substr(colonPos + 1));
    if (key == "cpu") {
        if (hasPageset) {
            entry.pagesets.push_back(move(currentPageset));
        }
        currentPageset = ZoneinfoPagesetInternal();
        currentPageset.cpu_id = SafeStoi(value);
        hasPageset = true;
        return;
    }
    if (hasPageset) {
        HandlePagesetField(currentPageset, key, value);
    }
}

void ProcDataManager::HandleZoneField(ZoneinfoEntryInternal &entry, const string &key, const string &value)
{
    // 数值字段
    if (key == "min") {
        entry.pages_min = SafeStoull(value);
        return;
    }
    if (key == "low") {
        entry.pages_low = SafeStoull(value);
        return;
    }
    if (key == "high") {
        entry.pages_high = SafeStoull(value);
        return;
    }
    if (key == "spanned") {
        entry.pages_spanned = SafeStoull(value);
        return;
    }
    if (key == "present") {
        entry.pages_present = SafeStoull(value);
        return;
    }
    if (key == "managed") {
        entry.pages_managed = SafeStoull(value);
        return;
    }
    if (key == "cma") {
        entry.pages_cma = SafeStoull(value);
        return;
    }
    // 字符串字段
    if (key == "protection") {
        entry.protection = value;
        return;
    }
    if (key == "node_unreclaimable") {
        entry.node_unreclaimable = value;
        return;
    }
    if (key == "start_pfn") {
        entry.start_pfn = value;
        return;
    }
    // 其他字段存入 stats
    entry.stats.emplace_back(key, value);
}

void ProcDataManager::HandlePagesetField(ZoneinfoPagesetInternal &pageset, const string &key, const string &value)
{
    if (key == "count") {
        pageset.count = SafeStoull(value);
        return;
    }
    if (key == "high") {
        pageset.high = SafeStoull(value);
        return;
    }
    if (key == "batch") {
        pageset.batch = SafeStoull(value);
        return;
    }
    if (key == "vm stats threshold") {
        pageset.vm_stats_threshold = SafeStoull(value);
        return;
    }
}

int ProcDataManager::ParseBuddyinfo(const string &content, int pid, ProcDataInternal &result)
{
    vector<BuddyinfoEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.size() < 4) {
            continue;
        }
        BuddyinfoEntryInternal entry{};
        entry.node = SafeGetInt(parts, 0);
        entry.zone = parts[1];
        entry.zone_name = parts[3];
        for (size_t k = 4; k < parts.size(); k++) entry.orders.push_back(SafeStoull(parts[k]));
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<BuddyinfoEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<BuddyinfoEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseNetSockstat(const string &content, int pid, ProcDataInternal &result)
{
    vector<NetSockstatEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        size_t colonPos = trimmed.find(':');
        if (colonPos == string::npos) {
            continue;
        }
        NetSockstatEntryInternal entry{};
        entry.protocol = Trim(trimmed.substr(0, colonPos));
        string rest = Trim(trimmed.substr(colonPos + 1));
        vector<string> parts = SplitLine(rest, ' ');
        for (size_t k = 0; k + 1 < parts.size(); k += 2) {
            entry.fields.emplace_back(parts[k], parts[k + 1]);
        }
        if (parts.size() % 2 != 0 && !parts.empty()) {
            entry.fields.emplace_back(parts.back(), "");
        }
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<NetSockstatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<NetSockstatEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseNetNetstat(const string &content, int pid, ProcDataInternal &result)
{
    vector<NetNetstatEntryInternal> entries;
    istringstream iss(content);
    string line;
    vector<string> headers;
    string headerCategory;
    bool expectValues = false;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.empty()) {
            continue;
        }
        string category = parts[0];
        if (!category.empty() && category.back() == ':') {
            category = category.substr(0, category.size() - 1);
        }
        if (!expectValues) {
            headerCategory = category;
            headers.clear();
            for (size_t k = 1; k < parts.size(); k++) headers.push_back(parts[k]);
            expectValues = true;
        } else {
            NetNetstatEntryInternal entry{};
            entry.category = headerCategory;
            for (size_t k = 1; k < parts.size() && (k - 1) < headers.size(); k++) {
                entry.fields.emplace_back(headers[k - 1], parts[k]);
            }
            entries.push_back(move(entry));
            headers.clear();
            headerCategory.clear();
            expectValues = false;
        }
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<NetNetstatEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<NetNetstatEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseNetArp(const string &content, int pid, ProcDataInternal &result)
{
    vector<NetArpEntryInternal> entries;
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
        // parts field indices (/proc file format)
        if (parts.size() < 6) {
            continue;
        }
        NetArpEntryInternal entry{};
        entry.ip_address = parts[NetArpField::IP_ADDRESS];
        entry.hw_type = parts[NetArpField::HW_TYPE];
        entry.flags = parts[NetArpField::ARP_FLAGS];
        entry.hw_address = parts[NetArpField::HW_ADDRESS];
        entry.mask = parts[NetArpField::MASK];
        entry.device = parts[NetArpField::ARP_DEVICE];
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<NetArpEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<NetArpEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseVersion(const string &content, int pid, ProcDataInternal &result)
{
    string trimmed = Trim(content);
    if (trimmed.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    vector<VersionEntryInternal> entries;
    VersionEntryInternal entry{};
    entry.version = trimmed;
    entries.push_back(move(entry));
    result.entries = new vector<VersionEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<VersionEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseModules(const string &content, int pid, ProcDataInternal &result)
{
    vector<ModulesEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        vector<string> parts = SplitLine(trimmed, ' ');
        if (parts.empty()) {
            continue;
        }
        ModulesEntryInternal entry{};
        entry.name = parts[0];
        entry.size = SafeGetUll(parts, 1);
        entry.used_count = SafeGetInt(parts, 2);
        entry.used_by = parts.size() > 3 ? parts[3] : "";
        entry.state = parts.size() > 4 ? parts[4] : "";
        entry.address = parts.size() > 5 ? parts[5] : "";
        entry.taint = parts.size() > 6 ? parts[6] : "";
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<ModulesEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<ModulesEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseFilesystems(const string &content, int pid, ProcDataInternal &result)
{
    vector<FilesystemsEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        FilesystemsEntryInternal entry{};
        if (trimmed.find("nodev") == 0 && trimmed.size() > 5 && (trimmed[5] == '\t' || trimmed[5] == ' ')) {
            entry.nodev = 1;
            entry.fs_type = Trim(trimmed.substr(5));
        } else {
            entry.nodev = 0;
            entry.fs_type = trimmed;
        }
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<FilesystemsEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<FilesystemsEntryInternal>*>(p); };
    return SUCCESS;
}

int ProcDataManager::ParseScsi(const string &content, int pid, ProcDataInternal &result)
{
    vector<ScsiEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty() || trimmed.find("Attached devices") == 0) {
            continue;
        }
        if (trimmed.find("Host:") != 0) {
            continue;
        }
        ScsiEntryInternal entry{};
        FillScsiHostInfo(entry, trimmed);
        if (!getline(iss, line)) {
            break;
        }
        FillScsiVendorInfo(entry, Trim(line));
        if (!getline(iss, line)) {
            break;
        }
        FillScsiTypeInfo(entry, Trim(line));
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<ScsiEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<ScsiEntryInternal>*>(p); };
    return SUCCESS;
}

void ProcDataManager::FillScsiHostInfo(ScsiEntryInternal &entry, const string &line)
{
    size_t pos = line.find("Host:");
    if (pos != string::npos) {
        entry.host = Trim(line.substr(pos + 5));
    }
    pos = entry.host.find("Channel:");
    if (pos != string::npos) {
        entry.channel = Trim(entry.host.substr(pos + 8));
        entry.host = Trim(entry.host.substr(0, pos));
    }
    pos = entry.channel.find("Id:");
    if (pos != string::npos) {
        entry.id = Trim(entry.channel.substr(pos + 3));
        entry.channel = Trim(entry.channel.substr(0, pos));
    }
    pos = entry.id.find("Lun:");
    if (pos != string::npos) {
        entry.lun = Trim(entry.id.substr(pos + 4));
        entry.id = Trim(entry.id.substr(0, pos));
    }
}

void ProcDataManager::FillScsiVendorInfo(ScsiEntryInternal &entry, const string &line)
{
    size_t pos = line.find("Vendor:");
    if (pos != string::npos) {
        entry.vendor = Trim(line.substr(pos + 7));
    }
    pos = entry.vendor.find("Model:");
    if (pos != string::npos) {
        entry.model = Trim(entry.vendor.substr(pos + 6));
        entry.vendor = Trim(entry.vendor.substr(0, pos));
    }
    pos = entry.model.find("Rev:");
    if (pos != string::npos) {
        entry.rev = Trim(entry.model.substr(pos + 4));
        entry.model = Trim(entry.model.substr(0, pos));
    }
}

void ProcDataManager::FillScsiTypeInfo(ScsiEntryInternal &entry, const string &line)
{
    size_t pos = line.find("Type:");
    if (pos != string::npos) {
        entry.type = Trim(line.substr(pos + 5));
    }
    pos = entry.type.find("ANSI");
    if (pos != string::npos) {
        entry.ansi_scsi_revision = Trim(entry.type.substr(pos));
        size_t revPos = entry.ansi_scsi_revision.find(':');
        if (revPos != string::npos) {
            entry.ansi_scsi_revision = Trim(entry.ansi_scsi_revision.substr(revPos + 1));
            entry.type = Trim(entry.type.substr(0, pos));
        }
    }
}

void ProcDataManager::FillPressureEntry(PressureEntryInternal &entry, const vector<string> &parts)
{
    if (parts.empty()) {
        return;
    }
    entry.type = parts[0];
    for (size_t k = 1; k < parts.size(); k++) {
        size_t eqPos = parts[k].find('=');
        if (eqPos == string::npos) continue;
        string key = parts[k].substr(0, eqPos);
        string value = parts[k].substr(eqPos + 1);
        if (key == "avg10") entry.avg10 = SafeStod(value);
        else if (key == "avg60") entry.avg60 = SafeStod(value);
        else if (key == "avg300") entry.avg300 = SafeStod(value);
        else if (key == "total") entry.total = SafeStoull(value);
    }
}

int ProcDataManager::ParsePressure(const string &content, int pid, ProcDataInternal &result)
{
    vector<PressureEntryInternal> entries;
    istringstream iss(content);
    string line;
    while (getline(iss, line)) {
        string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        PressureEntryInternal entry{};
        FillPressureEntry(entry, SplitLine(trimmed, ' '));
        entries.push_back(move(entry));
    }
    if (entries.empty()) {
        return LIBPERF_ERR_PROC_PARSE_FAILED;
    }
    result.entries = new vector<PressureEntryInternal>(move(entries));
    result.destroy = [](void *p) { delete static_cast<vector<PressureEntryInternal>*>(p); };
    return SUCCESS;
}
