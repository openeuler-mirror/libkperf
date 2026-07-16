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
 * Description: proc data conversion functions
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

void ProcDataManager::ConvertStat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<StatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.stat = new ProcStatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.stat[j].cpu_name = AllocStr(vec[j].cpu_name);
        out.stat[j].user = vec[j].user;
        out.stat[j].nice = vec[j].nice;
        out.stat[j].system = vec[j].system;
        out.stat[j].idle = vec[j].idle;
        out.stat[j].iowait = vec[j].iowait;
        out.stat[j].irq = vec[j].irq;
        out.stat[j].softirq = vec[j].softirq;
        out.stat[j].steal = vec[j].steal;
        out.stat[j].guest = vec[j].guest;
        out.stat[j].guest_nice = vec[j].guest_nice;
        out.stat[j].lineType = vec[j].lineType;
        out.stat[j].ctxt = vec[j].ctxt;
        out.stat[j].btime = vec[j].btime;
        out.stat[j].processes = vec[j].processes;
        out.stat[j].procs_running = vec[j].procs_running;
        out.stat[j].procs_blocked = vec[j].procs_blocked;
        out.stat[j].intr_total = vec[j].intr_total;
        out.stat[j].numIntrPerIrq = vec[j].intr_per_irq.size();
        if (!vec[j].intr_per_irq.empty()) {
            out.stat[j].intr_per_irq = new unsigned long long[vec[j].intr_per_irq.size()];
            for (size_t k = 0; k < vec[j].intr_per_irq.size(); k++) {
                out.stat[j].intr_per_irq[k] = vec[j].intr_per_irq[k];
            }
        } else {
            out.stat[j].intr_per_irq = nullptr;
        }
        out.stat[j].softirq_total = vec[j].softirq_total;
        out.stat[j].numSoftirqPerType = vec[j].softirq_per_type.size();
        if (!vec[j].softirq_per_type.empty()) {
            out.stat[j].softirq_per_type = new unsigned long long[vec[j].softirq_per_type.size()];
            for (size_t k = 0; k < vec[j].softirq_per_type.size(); k++) {
                out.stat[j].softirq_per_type[k] = vec[j].softirq_per_type[k];
            }
        } else {
            out.stat[j].softirq_per_type = nullptr;
        }
    }
}

void ProcDataManager::ConvertCpuinfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<CpuinfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.cpuinfo = new ProcCpuinfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.cpuinfo[j].numFields = vec[j].fields.size();
        out.cpuinfo[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertMeminfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<MeminfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.meminfo = new ProcMeminfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.meminfo[j].key = AllocStr(vec[j].key);
        out.meminfo[j].value = vec[j].value;
        out.meminfo[j].unit = AllocStr(vec[j].unit);
    }
}

void ProcDataManager::ConvertLoadavg(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<LoadavgEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.loadavg = new ProcLoadavgEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.loadavg[j].load1 = vec[j].load1;
        out.loadavg[j].load5 = vec[j].load5;
        out.loadavg[j].load15 = vec[j].load15;
        out.loadavg[j].running_procs = vec[j].running_procs;
        out.loadavg[j].total_procs = vec[j].total_procs;
        out.loadavg[j].last_pid = vec[j].last_pid;
    }
}

void ProcDataManager::ConvertVmstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<VmstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.vmstat = new ProcVmstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.vmstat[j].key = AllocStr(vec[j].key);
        out.vmstat[j].value = vec[j].value;
    }
}

void ProcDataManager::ConvertNetDev(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetDevEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.net_dev = new ProcNetDevEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.net_dev[j].iface = AllocStr(vec[j].iface);
        out.net_dev[j].rx_bytes = vec[j].rx_bytes;
        out.net_dev[j].rx_packets = vec[j].rx_packets;
        out.net_dev[j].rx_errs = vec[j].rx_errs;
        out.net_dev[j].rx_drop = vec[j].rx_drop;
        out.net_dev[j].rx_fifo = vec[j].rx_fifo;
        out.net_dev[j].rx_frame = vec[j].rx_frame;
        out.net_dev[j].rx_compressed = vec[j].rx_compressed;
        out.net_dev[j].rx_multicast = vec[j].rx_multicast;
        out.net_dev[j].tx_bytes = vec[j].tx_bytes;
        out.net_dev[j].tx_packets = vec[j].tx_packets;
        out.net_dev[j].tx_errs = vec[j].tx_errs;
        out.net_dev[j].tx_drop = vec[j].tx_drop;
        out.net_dev[j].tx_fifo = vec[j].tx_fifo;
        out.net_dev[j].tx_colls = vec[j].tx_colls;
        out.net_dev[j].tx_carrier = vec[j].tx_carrier;
        out.net_dev[j].tx_compressed = vec[j].tx_compressed;
    }
}

void ProcDataManager::ConvertDiskstats(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<DiskstatsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.diskstats = new ProcDiskstatsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.diskstats[j].major = vec[j].major;
        out.diskstats[j].minor = vec[j].minor;
        out.diskstats[j].device = AllocStr(vec[j].device);
        out.diskstats[j].reads_completed = vec[j].reads_completed;
        out.diskstats[j].reads_merged = vec[j].reads_merged;
        out.diskstats[j].sectors_read = vec[j].sectors_read;
        out.diskstats[j].ms_reading = vec[j].ms_reading;
        out.diskstats[j].writes_completed = vec[j].writes_completed;
        out.diskstats[j].writes_merged = vec[j].writes_merged;
        out.diskstats[j].sectors_written = vec[j].sectors_written;
        out.diskstats[j].ms_writing = vec[j].ms_writing;
        out.diskstats[j].ios_in_progress = vec[j].ios_in_progress;
        out.diskstats[j].ms_ios = vec[j].ms_ios;
        out.diskstats[j].weighted_ms_ios = vec[j].weighted_ms_ios;
        out.diskstats[j].discards_completed = vec[j].discards_completed;
        out.diskstats[j].discards_merged = vec[j].discards_merged;
        out.diskstats[j].sectors_discarded = vec[j].sectors_discarded;
        out.diskstats[j].ms_discarding = vec[j].ms_discarding;
        out.diskstats[j].flush_completed = vec[j].flush_completed;
        out.diskstats[j].ms_flushing = vec[j].ms_flushing;
    }
}

void ProcDataManager::ConvertUptime(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<UptimeEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.uptime = new ProcUptimeEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.uptime[j].total = vec[j].total;
        out.uptime[j].idle = vec[j].idle;
    }
}

void ProcDataManager::ConvertMounts(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<MountsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.mounts = new ProcMountsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.mounts[j].device = AllocStr(vec[j].device);
        out.mounts[j].mount_point = AllocStr(vec[j].mount_point);
        out.mounts[j].fs_type = AllocStr(vec[j].fs_type);
        out.mounts[j].options = AllocStr(vec[j].options);
        out.mounts[j].dump = vec[j].dump;
        out.mounts[j].pass_val = vec[j].pass_val;
    }
}

void ProcDataManager::ConvertSoftirqs(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SoftirqsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.softirqs = new ProcSoftirqsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.softirqs[j].type = AllocStr(vec[j].type);
        out.softirqs[j].numCpus = vec[j].per_cpu.size();
        out.softirqs[j].per_cpu = new unsigned long long[vec[j].per_cpu.size()];
        for (size_t k = 0; k < vec[j].per_cpu.size(); k++)
            out.softirqs[j].per_cpu[k] = vec[j].per_cpu[k];
    }
}

void ProcDataManager::ConvertSlabinfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SlabinfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.slabinfo = new ProcSlabinfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.slabinfo[j].name = AllocStr(vec[j].name);
        out.slabinfo[j].active_objs = vec[j].active_objs;
        out.slabinfo[j].num_objs = vec[j].num_objs;
        out.slabinfo[j].objsize = vec[j].objsize;
        out.slabinfo[j].objperslab = vec[j].objperslab;
        out.slabinfo[j].pagesperslab = vec[j].pagesperslab;
        out.slabinfo[j].limit = vec[j].limit;
        out.slabinfo[j].batchcount = vec[j].batchcount;
        out.slabinfo[j].sharedfactor = vec[j].sharedfactor;
        out.slabinfo[j].active_slabs = vec[j].active_slabs;
        out.slabinfo[j].num_slabs = vec[j].num_slabs;
        out.slabinfo[j].sharedavail = vec[j].sharedavail;
    }
}

void ProcDataManager::ConvertSchedstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SchedstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.schedstat = new ProcSchedstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.schedstat[j].cpu_id = vec[j].cpu_id;
        out.schedstat[j].yld_count = vec[j].yld_count;
        out.schedstat[j].sched_count = vec[j].sched_count;
        out.schedstat[j].sched_goidle = vec[j].sched_goidle;
        out.schedstat[j].ttwu_count = vec[j].ttwu_count;
        out.schedstat[j].ttwu_local = vec[j].ttwu_local;
        out.schedstat[j].rq_cpu_time = vec[j].rq_cpu_time;
        out.schedstat[j].run_delay = vec[j].run_delay;
        out.schedstat[j].pcount = vec[j].pcount;
        out.schedstat[j].numDomains = vec[j].domains.size();
        out.schedstat[j].domains = new ProcSchedstatDomainEntry[vec[j].domains.size()];
        for (size_t d = 0; d < vec[j].domains.size(); d++) {
            out.schedstat[j].domains[d].domain_id = vec[j].domains[d].domain_id;
            out.schedstat[j].domains[d].mask = AllocStr(vec[j].domains[d].mask);
            out.schedstat[j].domains[d].numValues = vec[j].domains[d].values.size();
            out.schedstat[j].domains[d].values = new unsigned long long[vec[j].domains[d].values.size()];
            for (size_t v = 0; v < vec[j].domains[d].values.size(); v++)
                out.schedstat[j].domains[d].values[v] = vec[j].domains[d].values[v];
        }
    }
}

void ProcDataManager::ConvertInterrupts(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<InterruptsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.interrupts = new ProcInterruptsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.interrupts[j].irq = AllocStr(vec[j].irq);
        out.interrupts[j].numCpus = vec[j].per_cpu.size();
        out.interrupts[j].per_cpu = new unsigned long long[vec[j].per_cpu.size()];
        for (size_t k = 0; k < vec[j].per_cpu.size(); k++)
            out.interrupts[j].per_cpu[k] = vec[j].per_cpu[k];
        out.interrupts[j].description = AllocStr(vec[j].description);
    }
}

void ProcDataManager::ConvertIrqAffinity(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<IrqAffinityEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.irq_affinity = new ProcIrqAffinityEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.irq_affinity[j].affinity = AllocStr(vec[j].affinity);
}

void ProcDataManager::ConvertLocks(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<LocksEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.locks = new ProcLocksEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.locks[j].numFields = vec[j].fields.size();
        out.locks[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertZoneinfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<ZoneinfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.zoneinfo = new ProcZoneinfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.zoneinfo[j].node = vec[j].node;
        out.zoneinfo[j].zone = AllocStr(vec[j].zone);
        // Zone 级别 pages 统计
        out.zoneinfo[j].pages_free = vec[j].pages_free;
        out.zoneinfo[j].pages_min = vec[j].pages_min;
        out.zoneinfo[j].pages_low = vec[j].pages_low;
        out.zoneinfo[j].pages_high = vec[j].pages_high;
        out.zoneinfo[j].pages_spanned = vec[j].pages_spanned;
        out.zoneinfo[j].pages_present = vec[j].pages_present;
        out.zoneinfo[j].pages_managed = vec[j].pages_managed;
        out.zoneinfo[j].pages_cma = vec[j].pages_cma;
        out.zoneinfo[j].protection = AllocStr(vec[j].protection);
        out.zoneinfo[j].node_unreclaimable = AllocStr(vec[j].node_unreclaimable);
        out.zoneinfo[j].start_pfn = AllocStr(vec[j].start_pfn);
        // Node 级别统计
        out.zoneinfo[j].numNodeStats = vec[j].nodeStats.size();
        out.zoneinfo[j].nodeStats = ConvertFields(vec[j].nodeStats);
        // Zone 级别统计
        out.zoneinfo[j].numStats = vec[j].stats.size();
        out.zoneinfo[j].stats = ConvertFields(vec[j].stats);
        // pagesets
        out.zoneinfo[j].numPagesets = vec[j].pagesets.size();
        out.zoneinfo[j].pagesets = new ProcZoneinfoPageset[vec[j].pagesets.size()];
        for (size_t k = 0; k < vec[j].pagesets.size(); k++) {
            out.zoneinfo[j].pagesets[k].cpu_id = vec[j].pagesets[k].cpu_id;
            out.zoneinfo[j].pagesets[k].count = vec[j].pagesets[k].count;
            out.zoneinfo[j].pagesets[k].high = vec[j].pagesets[k].high;
            out.zoneinfo[j].pagesets[k].batch = vec[j].pagesets[k].batch;
            out.zoneinfo[j].pagesets[k].vm_stats_threshold = vec[j].pagesets[k].vm_stats_threshold;
        }
    }
}

void ProcDataManager::ConvertBuddyinfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<BuddyinfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.buddyinfo = new ProcBuddyinfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.buddyinfo[j].node = vec[j].node;
        out.buddyinfo[j].zone = AllocStr(vec[j].zone);
        out.buddyinfo[j].zone_name = AllocStr(vec[j].zone_name);
        out.buddyinfo[j].numOrders = vec[j].orders.size();
        out.buddyinfo[j].orders = new unsigned long long[vec[j].orders.size()];
        for (size_t k = 0; k < vec[j].orders.size(); k++)
            out.buddyinfo[j].orders[k] = vec[j].orders[k];
    }
}

void ProcDataManager::ConvertNetSockstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetSockstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.net_sockstat = new ProcNetSockstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.net_sockstat[j].protocol = AllocStr(vec[j].protocol);
        out.net_sockstat[j].numFields = vec[j].fields.size();
        out.net_sockstat[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertNetNetstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetNetstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.net_netstat = new ProcNetNetstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.net_netstat[j].category = AllocStr(vec[j].category);
        out.net_netstat[j].numFields = vec[j].fields.size();
        out.net_netstat[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertNetArp(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetArpEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.net_arp = new ProcNetArpEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.net_arp[j].ip_address = AllocStr(vec[j].ip_address);
        out.net_arp[j].hw_type = AllocStr(vec[j].hw_type);
        out.net_arp[j].flags = AllocStr(vec[j].flags);
        out.net_arp[j].hw_address = AllocStr(vec[j].hw_address);
        out.net_arp[j].mask = AllocStr(vec[j].mask);
        out.net_arp[j].device = AllocStr(vec[j].device);
    }
}

void ProcDataManager::ConvertVersion(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<VersionEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.version = new ProcVersionEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.version[j].version = AllocStr(vec[j].version);
}

void ProcDataManager::ConvertModules(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<ModulesEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.modules = new ProcModulesEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.modules[j].name = AllocStr(vec[j].name);
        out.modules[j].size = vec[j].size;
        out.modules[j].used_count = vec[j].used_count;
        out.modules[j].used_by = AllocStr(vec[j].used_by);
        out.modules[j].state = AllocStr(vec[j].state);
        out.modules[j].address = AllocStr(vec[j].address);
        out.modules[j].taint = AllocStr(vec[j].taint);
    }
}

void ProcDataManager::ConvertFilesystems(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<FilesystemsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.filesystems = new ProcFilesystemsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.filesystems[j].nodev = vec[j].nodev;
        out.filesystems[j].fs_type = AllocStr(vec[j].fs_type);
    }
}

void ProcDataManager::ConvertScsi(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<ScsiEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.scsi = new ProcScsiEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.scsi[j].host = AllocStr(vec[j].host);
        out.scsi[j].channel = AllocStr(vec[j].channel);
        out.scsi[j].id = AllocStr(vec[j].id);
        out.scsi[j].lun = AllocStr(vec[j].lun);
        out.scsi[j].vendor = AllocStr(vec[j].vendor);
        out.scsi[j].model = AllocStr(vec[j].model);
        out.scsi[j].rev = AllocStr(vec[j].rev);
        out.scsi[j].type = AllocStr(vec[j].type);
        out.scsi[j].ansi_scsi_revision = AllocStr(vec[j].ansi_scsi_revision);
    }
}

void ProcDataManager::ConvertPressure(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PressureEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pressure = new ProcPressureEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pressure[j].type = AllocStr(vec[j].type);
        out.pressure[j].avg10 = vec[j].avg10;
        out.pressure[j].avg60 = vec[j].avg60;
        out.pressure[j].avg300 = vec[j].avg300;
        out.pressure[j].total = vec[j].total;
    }
}

void ProcDataManager::ConvertSysDir(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SysDirEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.sys_dir = new ProcSysDirEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.sys_dir[j].name = AllocStr(vec[j].name);
        out.sys_dir[j].path = AllocStr(vec[j].path);
        out.sys_dir[j].value = AllocStr(vec[j].value);
    }
}

void ProcDataManager::ConvertPidStat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_stat = new ProcPidStatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_stat[j].comm = AllocStr(vec[j].comm);
        CopyPidStatEntry(vec[j], out.pid_stat[j]);
    }
}

template <typename SrcEntry, typename DstEntry>
void ProcDataManager::CopyPidStatEntry(const SrcEntry &src, DstEntry &dst)
{
    CopyPidStatProcFields(src, dst);
    CopyPidStatMemFields(src, dst);
    CopyPidStatSigFields(src, dst);
}

template <typename SrcEntry, typename DstEntry>
void ProcDataManager::CopyPidStatProcFields(const SrcEntry &src, DstEntry &dst)
{
    dst.pid = src.pid;
    dst.state = src.state;
    dst.ppid = src.ppid;
    dst.pgrp = src.pgrp;
    dst.session = src.session;
    dst.tty_nr = src.tty_nr;
    dst.tpgid = src.tpgid;
    dst.flags = src.flags;
    dst.minflt = src.minflt;
    dst.cminflt = src.cminflt;
    dst.majflt = src.majflt;
    dst.cmajflt = src.cmajflt;
    dst.utime = src.utime;
    dst.stime = src.stime;
    dst.cutime = src.cutime;
    dst.cstime = src.cstime;
    dst.priority = src.priority;
    dst.nice_val = src.nice_val;
}

template <typename SrcEntry, typename DstEntry>
void ProcDataManager::CopyPidStatMemFields(const SrcEntry &src, DstEntry &dst)
{
    dst.num_threads = src.num_threads;
    dst.itrealvalue = src.itrealvalue;
    dst.starttime = src.starttime;
    dst.vsize = src.vsize;
    dst.rsslim = src.rsslim;
    dst.rss = src.rss;
    dst.startcode = src.startcode;
    dst.endcode = src.endcode;
    dst.startstack = src.startstack;
    dst.kstkesp = src.kstkesp;
    dst.kstkeip = src.kstkeip;
    dst.signal = src.signal;
    dst.blocked = src.blocked;
    dst.sigignore = src.sigignore;
    dst.sigcatch = src.sigcatch;
    dst.wchan = src.wchan;
}

template <typename SrcEntry, typename DstEntry>
void ProcDataManager::CopyPidStatSigFields(const SrcEntry &src, DstEntry &dst)
{
    dst.nswap = src.nswap;
    dst.cnswap = src.cnswap;
    dst.exit_signal = src.exit_signal;
    dst.processor = src.processor;
    dst.rt_priority = src.rt_priority;
    dst.policy = src.policy;
    dst.delayacct_blkio_ticks = src.delayacct_blkio_ticks;
    dst.guest_time = src.guest_time;
    dst.cguest_time = src.cguest_time;
    dst.start_data = src.start_data;
    dst.end_data = src.end_data;
    dst.start_brk = src.start_brk;
    dst.arg_start = src.arg_start;
    dst.arg_end = src.arg_end;
    dst.env_start = src.env_start;
    dst.env_end = src.env_end;
    dst.exit_code = src.exit_code;
}

void ProcDataManager::ConvertPidStatm(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatmEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_statm = new ProcPidStatmEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_statm[j].size = vec[j].size;
        out.pid_statm[j].resident = vec[j].resident;
        out.pid_statm[j].shared = vec[j].shared;
        out.pid_statm[j].text = vec[j].text;
        out.pid_statm[j].lib = vec[j].lib;
        out.pid_statm[j].data = vec[j].data;
        out.pid_statm[j].dt = vec[j].dt;
    }
}

void ProcDataManager::ConvertPidStatus(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatusEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_status = new ProcPidStatusEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_status[j].numFields = vec[j].fields.size();
        out.pid_status[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidIo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidIoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_io = new ProcPidIoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_io[j].rchar = vec[j].rchar;
        out.pid_io[j].wchar = vec[j].wchar;
        out.pid_io[j].syscr = vec[j].syscr;
        out.pid_io[j].syscw = vec[j].syscw;
        out.pid_io[j].read_bytes = vec[j].read_bytes;
        out.pid_io[j].write_bytes = vec[j].write_bytes;
        out.pid_io[j].cancelled_write_bytes = vec[j].cancelled_write_bytes;
    }
}

void ProcDataManager::ConvertPidSmapsRollup(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidSmapsRollupEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_smaps_rollup = new ProcPidSmapsRollupEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_smaps_rollup[j].numFields = vec[j].fields.size();
        out.pid_smaps_rollup[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidFd(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidFdEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_fd = new ProcPidFdEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pid_fd[j].fd_count = vec[j].fd_count;
}

void ProcDataManager::ConvertPidNumaMaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidNumaMapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_numa_maps = new ProcPidNumaMapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_numa_maps[j].address = AllocStr(vec[j].address);
        out.pid_numa_maps[j].numFields = vec[j].fields.size();
        out.pid_numa_maps[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidSmaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidSmapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_smaps = new ProcPidSmapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_smaps[j].mapping = AllocStr(vec[j].mapping);
        out.pid_smaps[j].numFields = vec[j].fields.size();
        out.pid_smaps[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidEnviron(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidEnvironEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_environ = new ProcPidEnvironEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_environ[j].name = AllocStr(vec[j].name);
        out.pid_environ[j].value = AllocStr(vec[j].value);
    }
}

void ProcDataManager::ConvertPidCmdline(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidCmdlineEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_cmdline = new ProcPidCmdlineEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pid_cmdline[j].cmdline = AllocStr(vec[j].cmdline);
}

void ProcDataManager::ConvertPidLimits(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidLimitsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_limits = new ProcPidLimitsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_limits[j].limit = AllocStr(vec[j].limit);
        out.pid_limits[j].soft = AllocStr(vec[j].soft);
        out.pid_limits[j].hard = AllocStr(vec[j].hard);
        out.pid_limits[j].units = AllocStr(vec[j].units);
    }
}

void ProcDataManager::ConvertPidStack(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStackEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_stack = new ProcPidStackEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_stack[j].address = AllocStr(vec[j].address);
        out.pid_stack[j].symbol = AllocStr(vec[j].symbol);
    }
}

void ProcDataManager::ConvertPidWchan(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidWchanEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_wchan = new ProcPidWchanEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pid_wchan[j].wchan = AllocStr(vec[j].wchan);
}

void ProcDataManager::ConvertPidMaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidMapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_maps = new ProcPidMapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_maps[j].start = AllocStr(vec[j].start);
        out.pid_maps[j].end = AllocStr(vec[j].end);
        out.pid_maps[j].perms = AllocStr(vec[j].perms);
        out.pid_maps[j].offset = AllocStr(vec[j].offset);
        out.pid_maps[j].dev = AllocStr(vec[j].dev);
        out.pid_maps[j].inode = AllocStr(vec[j].inode);
        out.pid_maps[j].pathname = AllocStr(vec[j].pathname);
    }
}

void ProcDataManager::ConvertPidComm(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidCommEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_comm = new ProcPidCommEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pid_comm[j].comm = AllocStr(vec[j].comm);
}

void ProcDataManager::ConvertPidTaskStat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidTaskStatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pid_task_stat = new ProcPidTaskStatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pid_task_stat[j].comm = AllocStr(vec[j].comm);
        CopyPidStatEntry(vec[j], out.pid_task_stat[j]);
    }
}

