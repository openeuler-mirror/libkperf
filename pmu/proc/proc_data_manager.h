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
 * Description: ProcDataManager class for collecting and parsing /proc data
 ******************************************************************************/
#ifndef PROC_DATA_H
#define PROC_DATA_H

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <map>
#include "proc_data_types.h"

namespace KUNPENG_PMU {

struct StatEntryInternal {
    std::string cpu_name;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    ProcStatLineType lineType = PROC_STAT_LINE_CPU;
    unsigned long long ctxt = 0, btime = 0, processes = 0, procs_running = 0, procs_blocked = 0;
    unsigned long long intr_total = 0;
    std::vector<unsigned long long> intr_per_irq;
    unsigned long long softirq_total = 0;
    std::vector<unsigned long long> softirq_per_type;
};

struct CpuinfoEntryInternal {
    std::vector<std::pair<std::string, std::string>> fields;
};

struct MeminfoEntryInternal {
    std::string key;
    unsigned long long value;
    std::string unit;
};

struct LoadavgEntryInternal {
    double load1, load5, load15;
    unsigned running_procs, total_procs;
    int last_pid;
};

struct VmstatEntryInternal {
    std::string key;
    unsigned long long value;
};

struct NetDevEntryInternal {
    std::string iface;
    unsigned long long rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
    unsigned long long tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
};

struct DiskstatsEntryInternal {
    int major, minor;
    std::string device;
    unsigned long long reads_completed, reads_merged, sectors_read, ms_reading;
    unsigned long long writes_completed, writes_merged, sectors_written, ms_writing;
    unsigned long long ios_in_progress, ms_ios, weighted_ms_ios;
    unsigned long long discards_completed, discards_merged, sectors_discarded, ms_discarding;
    unsigned long long flush_completed, ms_flushing;
};

struct UptimeEntryInternal {
    double total, idle;
};

struct MountsEntryInternal {
    std::string device, mount_point, fs_type, options;
    int dump, pass_val;
};

struct SoftirqsEntryInternal {
    std::string type;
    std::vector<unsigned long long> per_cpu;
};

struct SlabinfoEntryInternal {
    std::string name;
    unsigned long long active_objs, num_objs, objsize, objperslab, pagesperslab;
    unsigned long long limit, batchcount, sharedfactor;
    unsigned long long active_slabs, num_slabs, sharedavail;
};

struct SchedstatDomainInternal {
    int domain_id;
    std::string mask;
    std::vector<unsigned long long> values;
};

struct SchedstatEntryInternal {
    int cpu_id;
    unsigned long long yld_count, sched_count, sched_goidle;
    unsigned long long ttwu_count, ttwu_local, rq_cpu_time, run_delay, pcount;
    std::vector<SchedstatDomainInternal> domains;
};

struct InterruptsEntryInternal {
    std::string irq;
    std::vector<unsigned long long> per_cpu;
    std::string description;
};

struct IrqAffinityEntryInternal {
    std::string affinity;
};

struct LocksEntryInternal {
    std::vector<std::pair<std::string, std::string>> fields;
};

struct ZoneinfoPagesetInternal {
    int cpu_id;
    unsigned long long count, high, batch, vm_stats_threshold;
};

struct ZoneinfoEntryInternal {
    int node;
    std::string zone;
    // Zone 级别 pages 统计
    unsigned long long pages_free = 0;
    unsigned long long pages_min = 0, pages_low = 0, pages_high = 0;
    unsigned long long pages_spanned = 0, pages_present = 0, pages_managed = 0, pages_cma = 0;
    std::string protection;
    std::string node_unreclaimable;
    std::string start_pfn;
    // Node 级别统计 (per-node stats)
    std::vector<std::pair<std::string, std::string>> nodeStats;
    // Zone 级别统计 (nr_zone_* 等)
    std::vector<std::pair<std::string, std::string>> stats;
    // pagesets
    std::vector<ZoneinfoPagesetInternal> pagesets;
};

struct BuddyinfoEntryInternal {
    int node;
    std::string zone, zone_name;
    std::vector<unsigned long long> orders;
};

struct NetSockstatEntryInternal {
    std::string protocol;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct NetNetstatEntryInternal {
    std::string category;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct NetArpEntryInternal {
    std::string ip_address;
    std::string hw_type;
    std::string flags;
    std::string hw_address;
    std::string mask;
    std::string device;
};

struct VersionEntryInternal {
    std::string version;
};

struct ModulesEntryInternal {
    std::string name;
    unsigned long long size;
    int used_count;
    std::string used_by, state, address, taint;
};

struct FilesystemsEntryInternal {
    int nodev;
    std::string fs_type;
};

struct ScsiEntryInternal {
    std::string host;
    std::string channel;
    std::string id;
    std::string lun;
    std::string vendor;
    std::string model;
    std::string rev;
    std::string type;
    std::string ansi_scsi_revision;
};

struct PressureEntryInternal {
    std::string type;
    double avg10, avg60, avg300;
    unsigned long long total;
};

struct SysDirEntryInternal {
    std::string name, path, value;
};

struct PidStatEntryInternal {
    int pid;
    std::string comm;
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned long long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    long long cutime, cstime;
    int priority, nice_val, num_threads;
    long long itrealvalue;
    unsigned long long starttime, vsize, rsslim;
    long long rss;
    unsigned long long startcode, endcode, startstack, kstkesp, kstkeip;
    unsigned long long signal, blocked, sigignore, sigcatch, wchan, nswap, cnswap;
    int exit_signal, processor;
    unsigned rt_priority, policy;
    unsigned long long delayacct_blkio_ticks, guest_time;
    long long cguest_time;
    unsigned long long start_data, end_data, start_brk, arg_start, arg_end, env_start, env_end;
    int exit_code;
};

struct PidStatmEntryInternal {
    unsigned long long size, resident, shared, text, lib, data, dt;
};

struct PidStatusEntryInternal {
    std::vector<std::pair<std::string, std::string>> fields;
};

struct PidIoEntryInternal {
    unsigned long long rchar, wchar, syscr, syscw, read_bytes, write_bytes, cancelled_write_bytes;
};

struct PidSmapsRollupEntryInternal {
    std::vector<std::pair<std::string, std::string>> fields;
};

struct PidFdEntryInternal {
    unsigned fd_count;
};

struct PidNumaMapsEntryInternal {
    std::string address;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct PidSmapsEntryInternal {
    std::string mapping;
    std::vector<std::pair<std::string, std::string>> fields;
};

struct PidEnvironEntryInternal {
    std::string name, value;
};

struct PidCmdlineEntryInternal {
    std::string cmdline;
};

struct PidLimitsEntryInternal {
    std::string limit, soft, hard, units;
};

struct PidStackEntryInternal {
    std::string address, symbol;
};

struct PidWchanEntryInternal {
    std::string wchan;
};

struct PidMapsEntryInternal {
    std::string start, end, perms, offset, dev, inode, pathname;
};

struct PidCommEntryInternal {
    std::string comm;
};

struct PidTaskStatEntryInternal {
    int pid;
    std::string comm;
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned long long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    long long cutime, cstime;
    int priority, nice_val, num_threads;
    long long itrealvalue;
    unsigned long long starttime, vsize, rsslim;
    long long rss;
    unsigned long long startcode, endcode, startstack, kstkesp, kstkeip;
    unsigned long long signal, blocked, sigignore, sigcatch, wchan, nswap, cnswap;
    int exit_signal, processor;
    unsigned rt_priority, policy;
    unsigned long long delayacct_blkio_ticks, guest_time;
    long long cguest_time;
    unsigned long long start_data, end_data, start_brk, arg_start, arg_end, env_start, env_end;
    int exit_code;
};

struct ProcDataInternal {
    ProcSource source;
    int pid;
    std::string filePath;
    void *entries;
    void (*destroy)(void*);

    ProcDataInternal() : source(PROC_STAT), pid(0), filePath(), entries(nullptr), destroy(nullptr) {}

    ~ProcDataInternal()
    {
        if (entries && destroy) {
            destroy(entries);
        }
    }

    ProcDataInternal(ProcDataInternal &&other) noexcept
        : source(other.source), pid(other.pid), filePath(std::move(other.filePath)),
          entries(other.entries), destroy(other.destroy) {
        other.entries = nullptr;
        other.destroy = nullptr;
    }

    ProcDataInternal& operator=(ProcDataInternal &&other) noexcept
    {
        if (this != &other) {
            if (entries && destroy) {
                destroy(entries);
            }
            source = other.source;
            pid = other.pid;
            filePath = std::move(other.filePath);
            entries = other.entries;
            destroy = other.destroy;
            other.entries = nullptr;
            other.destroy = nullptr;
        }
        return *this;
    }

    ProcDataInternal(const ProcDataInternal&) = delete;
    ProcDataInternal& operator=(const ProcDataInternal&) = delete;
};

class ProcDataManager {
public:
    static ProcDataManager* GetInstance()
    {
        static ProcDataManager instance;
        return &instance;
    }

    int ReadProcData(int pid, struct ProcData **data, unsigned *numData);
    void FreeProcData(struct ProcData *data, unsigned numData);

private:
    ProcDataManager() = default;
    ~ProcDataManager() = default;
    ProcDataManager(const ProcDataManager&) = delete;
    ProcDataManager& operator=(const ProcDataManager&) = delete;

    using ParserFunc = std::function<int(const std::string&, int, ProcDataInternal&)>;

    std::string GetFilePath(ProcSource source, int pid);
    int ParseProcFile(ProcSource source, int pid, const std::string &filePath, ProcDataInternal &result);
    // 读取特殊 source 的内容（sys_dir / PID_FD 目录 / PID_TASK_STAT 目录 / 普通文件）。
    int ReadSpecialContent(ProcSource source, const std::string &filePath, std::string &content,
                           std::string &actualPaths);
    int ConvertToCStruct(const std::vector<ProcDataInternal> &internalData, struct ProcData **data, unsigned *numData);

    // Per-source converters: fill out.xxx and out.numEntries from src.entries.
    void ConvertStat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertCpuinfo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertMeminfo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertLoadavg(const ProcDataInternal &src, struct ProcData &out);
    void ConvertVmstat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertNetDev(const ProcDataInternal &src, struct ProcData &out);
    void ConvertDiskstats(const ProcDataInternal &src, struct ProcData &out);
    void ConvertUptime(const ProcDataInternal &src, struct ProcData &out);
    void ConvertMounts(const ProcDataInternal &src, struct ProcData &out);
    void ConvertSoftirqs(const ProcDataInternal &src, struct ProcData &out);
    void ConvertSlabinfo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertSchedstat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertInterrupts(const ProcDataInternal &src, struct ProcData &out);
    void ConvertIrqAffinity(const ProcDataInternal &src, struct ProcData &out);
    void ConvertLocks(const ProcDataInternal &src, struct ProcData &out);
    void ConvertZoneinfo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertBuddyinfo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertNetSockstat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertNetNetstat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertNetArp(const ProcDataInternal &src, struct ProcData &out);
    void ConvertVersion(const ProcDataInternal &src, struct ProcData &out);
    void ConvertModules(const ProcDataInternal &src, struct ProcData &out);
    void ConvertFilesystems(const ProcDataInternal &src, struct ProcData &out);
    void ConvertScsi(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPressure(const ProcDataInternal &src, struct ProcData &out);
    void ConvertSysDir(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidStat(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidStatm(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidStatus(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidIo(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidSmapsRollup(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidFd(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidNumaMaps(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidSmaps(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidEnviron(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidCmdline(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidLimits(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidStack(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidWchan(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidMaps(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidComm(const ProcDataInternal &src, struct ProcData &out);
    void ConvertPidTaskStat(const ProcDataInternal &src, struct ProcData &out);

    // Per-source freers: release the type-specific array on ProcData.
    void FreeStat(struct ProcData &d);
    void FreeCpuinfo(struct ProcData &d);
    void FreeMeminfo(struct ProcData &d);
    void FreeLoadavg(struct ProcData &d);
    void FreeVmstat(struct ProcData &d);
    void FreeNetDev(struct ProcData &d);
    void FreeDiskstats(struct ProcData &d);
    void FreeUptime(struct ProcData &d);
    void FreeMounts(struct ProcData &d);
    void FreeSoftirqs(struct ProcData &d);
    void FreeSlabinfo(struct ProcData &d);
    void FreeSchedstat(struct ProcData &d);
    void FreeInterrupts(struct ProcData &d);
    void FreeIrqAffinity(struct ProcData &d);
    void FreeLocks(struct ProcData &d);
    void FreeZoneinfo(struct ProcData &d);
    void FreeBuddyinfo(struct ProcData &d);
    void FreeNetSockstat(struct ProcData &d);
    void FreeNetNetstat(struct ProcData &d);
    void FreeNetArp(struct ProcData &d);
    void FreeVersion(struct ProcData &d);
    void FreeModules(struct ProcData &d);
    void FreeFilesystems(struct ProcData &d);
    void FreeScsi(struct ProcData &d);
    void FreePressure(struct ProcData &d);
    void FreeSysDir(struct ProcData &d);
    void FreePidStat(struct ProcData &d);
    void FreePidStatm(struct ProcData &d);
    void FreePidStatus(struct ProcData &d);
    void FreePidIo(struct ProcData &d);
    void FreePidSmapsRollup(struct ProcData &d);
    void FreePidFd(struct ProcData &d);
    void FreePidNumaMaps(struct ProcData &d);
    void FreePidSmaps(struct ProcData &d);
    void FreePidEnviron(struct ProcData &d);
    void FreePidCmdline(struct ProcData &d);
    void FreePidLimits(struct ProcData &d);
    void FreePidStack(struct ProcData &d);
    void FreePidWchan(struct ProcData &d);
    void FreePidMaps(struct ProcData &d);
    void FreePidComm(struct ProcData &d);
    void FreePidTaskStat(struct ProcData &d);

    int ParseSysDir(const std::vector<std::string> &filePaths, int pid, ProcDataInternal &result);

    int ParseStat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseCpuinfo(const std::string &content, int pid, ProcDataInternal &result);
    int ParseMeminfo(const std::string &content, int pid, ProcDataInternal &result);
    int ParseLoadavg(const std::string &content, int pid, ProcDataInternal &result);
    int ParseVmstat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseNetDev(const std::string &content, int pid, ProcDataInternal &result);
    int ParseDiskstats(const std::string &content, int pid, ProcDataInternal &result);
    int ParseUptime(const std::string &content, int pid, ProcDataInternal &result);
    int ParseMounts(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidStat(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidStatm(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidStatus(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidIo(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidSmapsRollup(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidFd(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidNumaMaps(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidSmaps(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidEnviron(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidCmdline(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidLimits(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidStack(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidWchan(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidMaps(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidComm(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePidTaskStat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseSoftirqs(const std::string &content, int pid, ProcDataInternal &result);
    int ParseSlabinfo(const std::string &content, int pid, ProcDataInternal &result);
    int ParseSchedstat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseInterrupts(const std::string &content, int pid, ProcDataInternal &result);
    int ParseLocks(const std::string &content, int pid, ProcDataInternal &result);
    int ParseZoneinfo(const std::string &content, int pid, ProcDataInternal &result);
    int ParseBuddyinfo(const std::string &content, int pid, ProcDataInternal &result);
    // Zoneinfo 辅助函数
    void HandleZoneField(ZoneinfoEntryInternal &entry, const std::string &key, const std::string &value);
    void HandlePagesetField(ZoneinfoPagesetInternal &pageset, const std::string &key, const std::string &value);
    void HandleZoneinfoNodeStats(ZoneinfoEntryInternal &entry, const std::string &line);
    void HandleZoneinfoNewZone(ZoneinfoEntryInternal &entry, const std::string &line);
    bool HandleZoneinfoZoneStart(std::vector<ZoneinfoEntryInternal> &entries, ZoneinfoEntryInternal &entry,
                                 bool &hasEntry, ZoneinfoPagesetInternal &currentPageset,
                                 bool &hasPageset, int &state, const std::string &line);
    bool HandleZoneinfoStateSwitch(ZoneinfoEntryInternal &entry, int &state, bool &hasPageset, const std::string &line);
    void FinalizeZoneinfoEntry(std::vector<ZoneinfoEntryInternal> &entries, ZoneinfoEntryInternal &entry,
                               bool hasEntry, ZoneinfoPagesetInternal &currentPageset, bool hasPageset);
    void HandleZoneinfoZoneStats(ZoneinfoEntryInternal &entry, const std::string &line);
    void HandleZoneinfoPagesets(ZoneinfoEntryInternal &entry, ZoneinfoPagesetInternal &currentPageset,
                                bool &hasPageset, const std::string &line);
    int ParseNetSockstat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseNetNetstat(const std::string &content, int pid, ProcDataInternal &result);
    int ParseNetArp(const std::string &content, int pid, ProcDataInternal &result);
    int ParseVersion(const std::string &content, int pid, ProcDataInternal &result);
    int ParseModules(const std::string &content, int pid, ProcDataInternal &result);
    int ParseFilesystems(const std::string &content, int pid, ProcDataInternal &result);
    int ParseScsi(const std::string &content, int pid, ProcDataInternal &result);
    int ParsePressure(const std::string &content, int pid, ProcDataInternal &result);

    // ---- 辅助函数：降低函数行数/复杂度，按数据源分组 ----
    using StatHandler = std::function<void(StatEntryInternal&, const std::vector<std::string>&)>;
    const std::map<std::string, StatHandler> &GetStatHandlers();
    void FillStatCpuEntry(StatEntryInternal &entry, const std::vector<std::string> &values);
    void FillSchedstatCpuEntry(SchedstatEntryInternal &entry, const std::vector<std::string> &parts);
    void FillSchedstatDomain(SchedstatEntryInternal *currentCpu, const std::vector<std::string> &parts);
    void FillScsiHostInfo(ScsiEntryInternal &entry, const std::string &line);
    void FillScsiVendorInfo(ScsiEntryInternal &entry, const std::string &line);
    void FillScsiTypeInfo(ScsiEntryInternal &entry, const std::string &line);
    void FillPressureEntry(PressureEntryInternal &entry, const std::vector<std::string> &parts);
    bool IsSmapsMappingLine(const std::string &line);
    void FillPidNumaMapsEntry(PidNumaMapsEntryInternal &entry, const std::string &line);
    int ReadFdDirContent(const std::string &fdDirPath, std::string &content);
    int ReadTaskStatContent(const std::string &taskDirPath, std::string &content, std::string &actualPaths);
    template <typename Entry>
    void FillPidStatFields(Entry &entry, const std::vector<std::string> &parts);
    template <typename Entry>
    void FillPidStatProcFields(Entry &entry, const std::vector<std::string> &parts);
    template <typename Entry>
    void FillPidStatMemFields(Entry &entry, const std::vector<std::string> &parts);
    template <typename Entry>
    void FillPidStatSigFields(Entry &entry, const std::vector<std::string> &parts);
    std::string BuildLimitName(const std::vector<std::string> &parts, size_t nameEnd);
    void FillPidLimitsEntry(PidLimitsEntryInternal &entry, const std::vector<std::string> &parts, bool hasUnits);
    void FillPidMapsEntry(PidMapsEntryInternal &entry, const std::vector<std::string> &parts);
    // ParseProcFile 辅助：返回 source->parser 映射
    const std::map<ProcSource, ParserFunc> &GetProcFileParsers();
    void FillSysProcFileParsers(std::map<ProcSource, ParserFunc> &m);
    void FillPidProcFileParsers(std::map<ProcSource, ParserFunc> &m);
    // ConvertToCStruct 辅助：返回 source->converter 映射
    using ConvFn = void(ProcDataManager::*)(const ProcDataInternal&, struct ProcData&);
    const std::map<ProcSource, ConvFn> &GetConvertFns();
    void FillSysConvertFns(std::map<ProcSource, ConvFn> &m);
    void FillPidConvertFns(std::map<ProcSource, ConvFn> &m);
    // FreeProcData 辅助：返回 source->freer 映射
    using FreeFn = void(ProcDataManager::*)(struct ProcData&);
    const std::map<ProcSource, FreeFn> &GetFreeFns();
    void FillSysFreeFns(std::map<ProcSource, FreeFn> &m);
    void FillPidFreeFns(std::map<ProcSource, FreeFn> &m);
    // ConvertPidStat / ConvertPidTaskStat 共用字段拷贝（结构字段一致，用模板）
    template <typename SrcEntry, typename DstEntry>
    void CopyPidStatEntry(const SrcEntry &src, DstEntry &dst);
    template <typename SrcEntry, typename DstEntry>
    void CopyPidStatProcFields(const SrcEntry &src, DstEntry &dst);
    template <typename SrcEntry, typename DstEntry>
    void CopyPidStatMemFields(const SrcEntry &src, DstEntry &dst);
    template <typename SrcEntry, typename DstEntry>
    void CopyPidStatSigFields(const SrcEntry &src, DstEntry &dst);
};

}  // namespace KUNPENG_PMU

#endif  // PROC_DATA_H
