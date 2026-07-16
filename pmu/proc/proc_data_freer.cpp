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
 * Description: proc data free functions
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

void ProcDataManager::FreeStat(struct ProcData &d)
{
    if (d.stat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.stat[j].cpu_name;
            delete[] d.stat[j].intr_per_irq;
            delete[] d.stat[j].softirq_per_type;
        }
        delete[] d.stat;
    }
}

void ProcDataManager::FreeCpuinfo(struct ProcData &d)
{
    if (d.cpuinfo) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.cpuinfo[j].fields, d.cpuinfo[j].numFields);
        }
        delete[] d.cpuinfo;
    }
}

void ProcDataManager::FreeMeminfo(struct ProcData &d)
{
    if (d.meminfo) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.meminfo[j].key;
            delete[] d.meminfo[j].unit;
        }
        delete[] d.meminfo;
    }
}

void ProcDataManager::FreeLoadavg(struct ProcData &d)
{
    if (d.loadavg) {
        delete[] d.loadavg;
    }
}

void ProcDataManager::FreeVmstat(struct ProcData &d)
{
    if (d.vmstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.vmstat[j].key;
        }
        delete[] d.vmstat;
    }
}

void ProcDataManager::FreeNetDev(struct ProcData &d)
{
    if (d.net_dev) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.net_dev[j].iface;
        }
        delete[] d.net_dev;
    }
}

void ProcDataManager::FreeDiskstats(struct ProcData &d)
{
    if (d.diskstats) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.diskstats[j].device;
        }
        delete[] d.diskstats;
    }
}

void ProcDataManager::FreeUptime(struct ProcData &d)
{
    if (d.uptime) {
        delete[] d.uptime;
    }
}

void ProcDataManager::FreeMounts(struct ProcData &d)
{
    if (d.mounts) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.mounts[j].device;
            delete[] d.mounts[j].mount_point;
            delete[] d.mounts[j].fs_type;
            delete[] d.mounts[j].options;
        }
        delete[] d.mounts;
    }
}

void ProcDataManager::FreeSoftirqs(struct ProcData &d)
{
    if (d.softirqs) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.softirqs[j].type;
            delete[] d.softirqs[j].per_cpu;
        }
        delete[] d.softirqs;
    }
}

void ProcDataManager::FreeSlabinfo(struct ProcData &d)
{
    if (d.slabinfo) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.slabinfo[j].name;
        }
        delete[] d.slabinfo;
    }
}

void ProcDataManager::FreeSchedstat(struct ProcData &d)
{
    if (d.schedstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            for (unsigned k = 0; k < d.schedstat[j].numDomains; k++) {
                delete[] d.schedstat[j].domains[k].mask;
                delete[] d.schedstat[j].domains[k].values;
            }
            delete[] d.schedstat[j].domains;
        }
        delete[] d.schedstat;
    }
}

void ProcDataManager::FreeInterrupts(struct ProcData &d)
{
    if (d.interrupts) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.interrupts[j].irq;
            delete[] d.interrupts[j].per_cpu;
            delete[] d.interrupts[j].description;
        }
        delete[] d.interrupts;
    }
}

void ProcDataManager::FreeIrqAffinity(struct ProcData &d)
{
    if (d.irq_affinity) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.irq_affinity[j].affinity;
        }
        delete[] d.irq_affinity;
    }
}

void ProcDataManager::FreeLocks(struct ProcData &d)
{
    if (d.locks) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.locks[j].fields, d.locks[j].numFields);
        }
        delete[] d.locks;
    }
}

void ProcDataManager::FreeZoneinfo(struct ProcData &d)
{
    if (d.zoneinfo) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.zoneinfo[j].zone;
            delete[] d.zoneinfo[j].protection;
            delete[] d.zoneinfo[j].node_unreclaimable;
            delete[] d.zoneinfo[j].start_pfn;
            FreeFields(d.zoneinfo[j].nodeStats, d.zoneinfo[j].numNodeStats);
            FreeFields(d.zoneinfo[j].stats, d.zoneinfo[j].numStats);
            delete[] d.zoneinfo[j].pagesets;
        }
        delete[] d.zoneinfo;
    }
}

void ProcDataManager::FreeBuddyinfo(struct ProcData &d)
{
    if (d.buddyinfo) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.buddyinfo[j].zone;
            delete[] d.buddyinfo[j].zone_name;
            delete[] d.buddyinfo[j].orders;
        }
        delete[] d.buddyinfo;
    }
}

void ProcDataManager::FreeNetSockstat(struct ProcData &d)
{
    if (d.net_sockstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.net_sockstat[j].protocol;
            FreeFields(d.net_sockstat[j].fields, d.net_sockstat[j].numFields);
        }
        delete[] d.net_sockstat;
    }
}

void ProcDataManager::FreeNetNetstat(struct ProcData &d)
{
    if (d.net_netstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.net_netstat[j].category;
            FreeFields(d.net_netstat[j].fields, d.net_netstat[j].numFields);
        }
        delete[] d.net_netstat;
    }
}

void ProcDataManager::FreeNetArp(struct ProcData &d)
{
    if (d.net_arp) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.net_arp[j].ip_address;
            delete[] d.net_arp[j].hw_type;
            delete[] d.net_arp[j].flags;
            delete[] d.net_arp[j].hw_address;
            delete[] d.net_arp[j].mask;
            delete[] d.net_arp[j].device;
        }
        delete[] d.net_arp;
    }
}

void ProcDataManager::FreeVersion(struct ProcData &d)
{
    if (d.version) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.version[j].version;
        }
        delete[] d.version;
    }
}

void ProcDataManager::FreeModules(struct ProcData &d)
{
    if (d.modules) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.modules[j].name;
            delete[] d.modules[j].used_by;
            delete[] d.modules[j].state;
            delete[] d.modules[j].address;
            delete[] d.modules[j].taint;
        }
        delete[] d.modules;
    }
}

void ProcDataManager::FreeFilesystems(struct ProcData &d)
{
    if (d.filesystems) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.filesystems[j].fs_type;
        }
        delete[] d.filesystems;
    }
}

void ProcDataManager::FreeScsi(struct ProcData &d)
{
    if (d.scsi) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.scsi[j].host;
            delete[] d.scsi[j].channel;
            delete[] d.scsi[j].id;
            delete[] d.scsi[j].lun;
            delete[] d.scsi[j].vendor;
            delete[] d.scsi[j].model;
            delete[] d.scsi[j].rev;
            delete[] d.scsi[j].type;
            delete[] d.scsi[j].ansi_scsi_revision;
        }
        delete[] d.scsi;
    }
}

void ProcDataManager::FreePressure(struct ProcData &d)
{
    if (d.pressure) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pressure[j].type;
        }
        delete[] d.pressure;
    }
}

void ProcDataManager::FreeSysDir(struct ProcData &d)
{
    if (d.sys_dir) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.sys_dir[j].name;
            delete[] d.sys_dir[j].path;
            delete[] d.sys_dir[j].value;
        }
        delete[] d.sys_dir;
    }
}

void ProcDataManager::FreePidStat(struct ProcData &d)
{
    if (d.pid_stat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_stat[j].comm;
        }
        delete[] d.pid_stat;
    }
}

void ProcDataManager::FreePidStatm(struct ProcData &d)
{
    if (d.pid_statm) {
        delete[] d.pid_statm;
    }
}

void ProcDataManager::FreePidStatus(struct ProcData &d)
{
    if (d.pid_status) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.pid_status[j].fields, d.pid_status[j].numFields);
        }
        delete[] d.pid_status;
    }
}

void ProcDataManager::FreePidIo(struct ProcData &d)
{
    if (d.pid_io) {
        delete[] d.pid_io;
    }
}

void ProcDataManager::FreePidSmapsRollup(struct ProcData &d)
{
    if (d.pid_smaps_rollup) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.pid_smaps_rollup[j].fields, d.pid_smaps_rollup[j].numFields);
        }
        delete[] d.pid_smaps_rollup;
    }
}

void ProcDataManager::FreePidFd(struct ProcData &d)
{
    if (d.pid_fd) {
        delete[] d.pid_fd;
    }
}

void ProcDataManager::FreePidNumaMaps(struct ProcData &d)
{
    if (d.pid_numa_maps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_numa_maps[j].address;
            FreeFields(d.pid_numa_maps[j].fields, d.pid_numa_maps[j].numFields);
        }
        delete[] d.pid_numa_maps;
    }
}

void ProcDataManager::FreePidSmaps(struct ProcData &d)
{
    if (d.pid_smaps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_smaps[j].mapping;
            FreeFields(d.pid_smaps[j].fields, d.pid_smaps[j].numFields);
        }
        delete[] d.pid_smaps;
    }
}

void ProcDataManager::FreePidEnviron(struct ProcData &d)
{
    if (d.pid_environ) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_environ[j].name;
            delete[] d.pid_environ[j].value;
        }
        delete[] d.pid_environ;
    }
}

void ProcDataManager::FreePidCmdline(struct ProcData &d)
{
    if (d.pid_cmdline) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_cmdline[j].cmdline;
        }
        delete[] d.pid_cmdline;
    }
}

void ProcDataManager::FreePidLimits(struct ProcData &d)
{
    if (d.pid_limits) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_limits[j].limit;
            delete[] d.pid_limits[j].soft;
            delete[] d.pid_limits[j].hard;
            delete[] d.pid_limits[j].units;
        }
        delete[] d.pid_limits;
    }
}

void ProcDataManager::FreePidStack(struct ProcData &d)
{
    if (d.pid_stack) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_stack[j].address;
            delete[] d.pid_stack[j].symbol;
        }
        delete[] d.pid_stack;
    }
}

void ProcDataManager::FreePidWchan(struct ProcData &d)
{
    if (d.pid_wchan) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_wchan[j].wchan;
        }
        delete[] d.pid_wchan;
    }
}

void ProcDataManager::FreePidMaps(struct ProcData &d)
{
    if (d.pid_maps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_maps[j].start;
            delete[] d.pid_maps[j].end;
            delete[] d.pid_maps[j].perms;
            delete[] d.pid_maps[j].offset;
            delete[] d.pid_maps[j].dev;
            delete[] d.pid_maps[j].inode;
            delete[] d.pid_maps[j].pathname;
        }
        delete[] d.pid_maps;
    }
}

void ProcDataManager::FreePidComm(struct ProcData &d)
{
    if (d.pid_comm) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_comm[j].comm;
        }
        delete[] d.pid_comm;
    }
}

void ProcDataManager::FreePidTaskStat(struct ProcData &d)
{
    if (d.pid_task_stat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pid_task_stat[j].comm;
        }
        delete[] d.pid_task_stat;
    }
}
