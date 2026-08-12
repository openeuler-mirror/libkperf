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
            delete[] d.stat[j].cpuName;
            delete[] d.stat[j].intrPerIrq;
            delete[] d.stat[j].softirqPerType;
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
    if (d.netDev) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.netDev[j].iface;
        }
        delete[] d.netDev;
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
            delete[] d.mounts[j].mountPoint;
            delete[] d.mounts[j].fsType;
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
            delete[] d.softirqs[j].perCpu;
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
            delete[] d.interrupts[j].perCpu;
            delete[] d.interrupts[j].description;
        }
        delete[] d.interrupts;
    }
}

void ProcDataManager::FreeIrqAffinity(struct ProcData &d)
{
    if (d.irqAffinity) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.irqAffinity[j].affinity;
        }
        delete[] d.irqAffinity;
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
            delete[] d.zoneinfo[j].nodeUnreclaimable;
            delete[] d.zoneinfo[j].startPfn;
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
            delete[] d.buddyinfo[j].zoneName;
            delete[] d.buddyinfo[j].orders;
        }
        delete[] d.buddyinfo;
    }
}

void ProcDataManager::FreeNetSockstat(struct ProcData &d)
{
    if (d.netSockstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.netSockstat[j].protocol;
            FreeFields(d.netSockstat[j].fields, d.netSockstat[j].numFields);
        }
        delete[] d.netSockstat;
    }
}

void ProcDataManager::FreeNetNetstat(struct ProcData &d)
{
    if (d.netNetstat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.netNetstat[j].category;
            FreeFields(d.netNetstat[j].fields, d.netNetstat[j].numFields);
        }
        delete[] d.netNetstat;
    }
}

void ProcDataManager::FreeNetArp(struct ProcData &d)
{
    if (d.netArp) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.netArp[j].ipAddress;
            delete[] d.netArp[j].hwType;
            delete[] d.netArp[j].flags;
            delete[] d.netArp[j].hwAddress;
            delete[] d.netArp[j].mask;
            delete[] d.netArp[j].device;
        }
        delete[] d.netArp;
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
            delete[] d.modules[j].usedBy;
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
            delete[] d.filesystems[j].fsType;
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
            delete[] d.scsi[j].ansiScsiRevision;
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
    if (d.sysDir) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.sysDir[j].name;
            delete[] d.sysDir[j].path;
            delete[] d.sysDir[j].value;
        }
        delete[] d.sysDir;
    }
}

void ProcDataManager::FreePidStat(struct ProcData &d)
{
    if (d.pidStat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidStat[j].comm;
        }
        delete[] d.pidStat;
    }
}

void ProcDataManager::FreePidStatm(struct ProcData &d)
{
    if (d.pidStatm) {
        delete[] d.pidStatm;
    }
}

void ProcDataManager::FreePidStatus(struct ProcData &d)
{
    if (d.pidStatus) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.pidStatus[j].fields, d.pidStatus[j].numFields);
        }
        delete[] d.pidStatus;
    }
}

void ProcDataManager::FreePidIo(struct ProcData &d)
{
    if (d.pidIo) {
        delete[] d.pidIo;
    }
}

void ProcDataManager::FreePidSmapsRollup(struct ProcData &d)
{
    if (d.pidSmapsRollup) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            FreeFields(d.pidSmapsRollup[j].fields, d.pidSmapsRollup[j].numFields);
        }
        delete[] d.pidSmapsRollup;
    }
}

void ProcDataManager::FreePidFd(struct ProcData &d)
{
    if (d.pidFd) {
        delete[] d.pidFd;
    }
}

void ProcDataManager::FreePidNumaMaps(struct ProcData &d)
{
    if (d.pidNumaMaps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidNumaMaps[j].address;
            FreeFields(d.pidNumaMaps[j].fields, d.pidNumaMaps[j].numFields);
        }
        delete[] d.pidNumaMaps;
    }
}

void ProcDataManager::FreePidSmaps(struct ProcData &d)
{
    if (d.pidSmaps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidSmaps[j].mapping;
            FreeFields(d.pidSmaps[j].fields, d.pidSmaps[j].numFields);
        }
        delete[] d.pidSmaps;
    }
}

void ProcDataManager::FreePidEnviron(struct ProcData &d)
{
    if (d.pidEnviron) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidEnviron[j].name;
            delete[] d.pidEnviron[j].value;
        }
        delete[] d.pidEnviron;
    }
}

void ProcDataManager::FreePidCmdline(struct ProcData &d)
{
    if (d.pidCmdline) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidCmdline[j].cmdline;
        }
        delete[] d.pidCmdline;
    }
}

void ProcDataManager::FreePidLimits(struct ProcData &d)
{
    if (d.pidLimits) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidLimits[j].limit;
            delete[] d.pidLimits[j].soft;
            delete[] d.pidLimits[j].hard;
            delete[] d.pidLimits[j].units;
        }
        delete[] d.pidLimits;
    }
}

void ProcDataManager::FreePidStack(struct ProcData &d)
{
    if (d.pidStack) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidStack[j].address;
            delete[] d.pidStack[j].symbol;
        }
        delete[] d.pidStack;
    }
}

void ProcDataManager::FreePidWchan(struct ProcData &d)
{
    if (d.pidWchan) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidWchan[j].wchan;
        }
        delete[] d.pidWchan;
    }
}

void ProcDataManager::FreePidMaps(struct ProcData &d)
{
    if (d.pidMaps) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidMaps[j].start;
            delete[] d.pidMaps[j].end;
            delete[] d.pidMaps[j].perms;
            delete[] d.pidMaps[j].offset;
            delete[] d.pidMaps[j].dev;
            delete[] d.pidMaps[j].inode;
            delete[] d.pidMaps[j].pathname;
        }
        delete[] d.pidMaps;
    }
}

void ProcDataManager::FreePidComm(struct ProcData &d)
{
    if (d.pidComm) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidComm[j].comm;
        }
        delete[] d.pidComm;
    }
}

void ProcDataManager::FreePidTaskStat(struct ProcData &d)
{
    if (d.pidTaskStat) {
        for (unsigned j = 0; j < d.numEntries; j++) {
            delete[] d.pidTaskStat[j].comm;
        }
        delete[] d.pidTaskStat;
    }
}
