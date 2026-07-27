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
        out.stat[j].cpuName = AllocStr(vec[j].cpuName);
        out.stat[j].user = vec[j].user;
        out.stat[j].nice = vec[j].nice;
        out.stat[j].system = vec[j].system;
        out.stat[j].idle = vec[j].idle;
        out.stat[j].ioWait = vec[j].ioWait;
        out.stat[j].irq = vec[j].irq;
        out.stat[j].softirq = vec[j].softirq;
        out.stat[j].steal = vec[j].steal;
        out.stat[j].guest = vec[j].guest;
        out.stat[j].guestNice = vec[j].guestNice;
        out.stat[j].lineType = vec[j].lineType;
        out.stat[j].ctxt = vec[j].ctxt;
        out.stat[j].btime = vec[j].btime;
        out.stat[j].processes = vec[j].processes;
        out.stat[j].procsRunning = vec[j].procsRunning;
        out.stat[j].procsBlocked = vec[j].procsBlocked;
        out.stat[j].intrTotal = vec[j].intrTotal;
        out.stat[j].numIntrPerIrq = vec[j].intrPerIrq.size();
        if (!vec[j].intrPerIrq.empty()) {
            out.stat[j].intrPerIrq = new unsigned long long[vec[j].intrPerIrq.size()];
            for (size_t k = 0; k < vec[j].intrPerIrq.size(); k++) {
                out.stat[j].intrPerIrq[k] = vec[j].intrPerIrq[k];
            }
        } else {
            out.stat[j].intrPerIrq = nullptr;
        }
        out.stat[j].softirqTotal = vec[j].softirqTotal;
        out.stat[j].numSoftirqPerType = vec[j].softirqPerType.size();
        if (!vec[j].softirqPerType.empty()) {
            out.stat[j].softirqPerType = new unsigned long long[vec[j].softirqPerType.size()];
            for (size_t k = 0; k < vec[j].softirqPerType.size(); k++) {
                out.stat[j].softirqPerType[k] = vec[j].softirqPerType[k];
            }
        } else {
            out.stat[j].softirqPerType = nullptr;
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
        out.loadavg[j].runningProcs = vec[j].runningProcs;
        out.loadavg[j].totalProcs = vec[j].totalProcs;
        out.loadavg[j].lastPid = vec[j].lastPid;
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
    out.netDev = new ProcNetDevEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.netDev[j].iface = AllocStr(vec[j].iface);
        out.netDev[j].rxBytes = vec[j].rxBytes;
        out.netDev[j].rxPackets = vec[j].rxPackets;
        out.netDev[j].rxErrs = vec[j].rxErrs;
        out.netDev[j].rxDrop = vec[j].rxDrop;
        out.netDev[j].rxFifo = vec[j].rxFifo;
        out.netDev[j].rxFrame = vec[j].rxFrame;
        out.netDev[j].rxCompressed = vec[j].rxCompressed;
        out.netDev[j].rxMulticast = vec[j].rxMulticast;
        out.netDev[j].txBytes = vec[j].txBytes;
        out.netDev[j].txPackets = vec[j].txPackets;
        out.netDev[j].txErrs = vec[j].txErrs;
        out.netDev[j].txDrop = vec[j].txDrop;
        out.netDev[j].txFifo = vec[j].txFifo;
        out.netDev[j].txColls = vec[j].txColls;
        out.netDev[j].txCarrier = vec[j].txCarrier;
        out.netDev[j].txCompressed = vec[j].txCompressed;
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
        out.diskstats[j].readsCompleted = vec[j].readsCompleted;
        out.diskstats[j].readsMerged = vec[j].readsMerged;
        out.diskstats[j].sectorsRead = vec[j].sectorsRead;
        out.diskstats[j].msReading = vec[j].msReading;
        out.diskstats[j].writesCompleted = vec[j].writesCompleted;
        out.diskstats[j].writesMerged = vec[j].writesMerged;
        out.diskstats[j].sectorsWritten = vec[j].sectorsWritten;
        out.diskstats[j].msWriting = vec[j].msWriting;
        out.diskstats[j].iosInProgress = vec[j].iosInProgress;
        out.diskstats[j].msIos = vec[j].msIos;
        out.diskstats[j].weightedMsIos = vec[j].weightedMsIos;
        out.diskstats[j].discardsCompleted = vec[j].discardsCompleted;
        out.diskstats[j].discardsMerged = vec[j].discardsMerged;
        out.diskstats[j].sectorsDiscarded = vec[j].sectorsDiscarded;
        out.diskstats[j].msDiscarding = vec[j].msDiscarding;
        out.diskstats[j].flushCompleted = vec[j].flushCompleted;
        out.diskstats[j].msFlushing = vec[j].msFlushing;
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
        out.mounts[j].mountPoint = AllocStr(vec[j].mountPoint);
        out.mounts[j].fsType = AllocStr(vec[j].fsType);
        out.mounts[j].options = AllocStr(vec[j].options);
        out.mounts[j].dump = vec[j].dump;
        out.mounts[j].passVal = vec[j].passVal;
    }
}

void ProcDataManager::ConvertSoftirqs(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SoftirqsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.softirqs = new ProcSoftirqsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.softirqs[j].type = AllocStr(vec[j].type);
        out.softirqs[j].numCpus = vec[j].perCpu.size();
        out.softirqs[j].perCpu = new unsigned long long[vec[j].perCpu.size()];
        for (size_t k = 0; k < vec[j].perCpu.size(); k++)
            out.softirqs[j].perCpu[k] = vec[j].perCpu[k];
    }
}

void ProcDataManager::ConvertSlabinfo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SlabinfoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.slabinfo = new ProcSlabinfoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.slabinfo[j].name = AllocStr(vec[j].name);
        out.slabinfo[j].activeObjs = vec[j].activeObjs;
        out.slabinfo[j].numObjs = vec[j].numObjs;
        out.slabinfo[j].objsize = vec[j].objsize;
        out.slabinfo[j].objperslab = vec[j].objperslab;
        out.slabinfo[j].pagesperslab = vec[j].pagesperslab;
        out.slabinfo[j].limit = vec[j].limit;
        out.slabinfo[j].batchcount = vec[j].batchcount;
        out.slabinfo[j].sharedfactor = vec[j].sharedfactor;
        out.slabinfo[j].activeSlabs = vec[j].activeSlabs;
        out.slabinfo[j].numSlabs = vec[j].numSlabs;
        out.slabinfo[j].sharedavail = vec[j].sharedavail;
    }
}

void ProcDataManager::ConvertSchedstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<SchedstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.schedstat = new ProcSchedstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.schedstat[j].cpuId = vec[j].cpuId;
        out.schedstat[j].yldCount = vec[j].yldCount;
        out.schedstat[j].schedCount = vec[j].schedCount;
        out.schedstat[j].schedGoidle = vec[j].schedGoidle;
        out.schedstat[j].ttwuCount = vec[j].ttwuCount;
        out.schedstat[j].ttwuLocal = vec[j].ttwuLocal;
        out.schedstat[j].rqCpuTime = vec[j].rqCpuTime;
        out.schedstat[j].runDelay = vec[j].runDelay;
        out.schedstat[j].pcount = vec[j].pcount;
        out.schedstat[j].numDomains = vec[j].domains.size();
        out.schedstat[j].domains = new ProcSchedstatDomainEntry[vec[j].domains.size()];
        for (size_t d = 0; d < vec[j].domains.size(); d++) {
            out.schedstat[j].domains[d].domainId = vec[j].domains[d].domainId;
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
        out.interrupts[j].numCpus = vec[j].perCpu.size();
        out.interrupts[j].perCpu = new unsigned long long[vec[j].perCpu.size()];
        for (size_t k = 0; k < vec[j].perCpu.size(); k++)
            out.interrupts[j].perCpu[k] = vec[j].perCpu[k];
        out.interrupts[j].description = AllocStr(vec[j].description);
    }
}

void ProcDataManager::ConvertIrqAffinity(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<IrqAffinityEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.irqAffinity = new ProcIrqAffinityEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.irqAffinity[j].affinity = AllocStr(vec[j].affinity);
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
        out.zoneinfo[j].pagesFree = vec[j].pagesFree;
        out.zoneinfo[j].pagesMin = vec[j].pagesMin;
        out.zoneinfo[j].pagesLow = vec[j].pagesLow;
        out.zoneinfo[j].pagesHigh = vec[j].pagesHigh;
        out.zoneinfo[j].pagesSpanned = vec[j].pagesSpanned;
        out.zoneinfo[j].pagesPresent = vec[j].pagesPresent;
        out.zoneinfo[j].pagesManaged = vec[j].pagesManaged;
        out.zoneinfo[j].pagesCma = vec[j].pagesCma;
        out.zoneinfo[j].protection = AllocStr(vec[j].protection);
        out.zoneinfo[j].nodeUnreclaimable = AllocStr(vec[j].nodeUnreclaimable);
        out.zoneinfo[j].startPfn = AllocStr(vec[j].startPfn);
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
            out.zoneinfo[j].pagesets[k].cpuId = vec[j].pagesets[k].cpuId;
            out.zoneinfo[j].pagesets[k].count = vec[j].pagesets[k].count;
            out.zoneinfo[j].pagesets[k].high = vec[j].pagesets[k].high;
            out.zoneinfo[j].pagesets[k].batch = vec[j].pagesets[k].batch;
            out.zoneinfo[j].pagesets[k].vmStatsThreshold = vec[j].pagesets[k].vmStatsThreshold;
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
        out.buddyinfo[j].zoneName = AllocStr(vec[j].zoneName);
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
    out.netSockstat = new ProcNetSockstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.netSockstat[j].protocol = AllocStr(vec[j].protocol);
        out.netSockstat[j].numFields = vec[j].fields.size();
        out.netSockstat[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertNetNetstat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetNetstatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.netNetstat = new ProcNetNetstatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.netNetstat[j].category = AllocStr(vec[j].category);
        out.netNetstat[j].numFields = vec[j].fields.size();
        out.netNetstat[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertNetArp(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<NetArpEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.netArp = new ProcNetArpEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.netArp[j].ipAddress = AllocStr(vec[j].ipAddress);
        out.netArp[j].hwType = AllocStr(vec[j].hwType);
        out.netArp[j].flags = AllocStr(vec[j].flags);
        out.netArp[j].hwAddress = AllocStr(vec[j].hwAddress);
        out.netArp[j].mask = AllocStr(vec[j].mask);
        out.netArp[j].device = AllocStr(vec[j].device);
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
        out.modules[j].usedCount = vec[j].usedCount;
        out.modules[j].usedBy = AllocStr(vec[j].usedBy);
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
        out.filesystems[j].fsType = AllocStr(vec[j].fsType);
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
        out.scsi[j].ansiScsiRevision = AllocStr(vec[j].ansiScsiRevision);
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
    out.sysDir = new ProcSysDirEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.sysDir[j].name = AllocStr(vec[j].name);
        out.sysDir[j].path = AllocStr(vec[j].path);
        out.sysDir[j].value = AllocStr(vec[j].value);
    }
}

void ProcDataManager::ConvertPidStat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidStat = new ProcPidStatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidStat[j].comm = AllocStr(vec[j].comm);
        CopyPidStatEntry(vec[j], out.pidStat[j]);
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
    dst.ttyNr = src.ttyNr;
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
    dst.niceVal = src.niceVal;
}

template <typename SrcEntry, typename DstEntry>
void ProcDataManager::CopyPidStatMemFields(const SrcEntry &src, DstEntry &dst)
{
    dst.numThreads = src.numThreads;
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
    dst.exitSignal = src.exitSignal;
    dst.processor = src.processor;
    dst.rtPriority = src.rtPriority;
    dst.policy = src.policy;
    dst.delayacctBlkioTicks = src.delayacctBlkioTicks;
    dst.guestTime = src.guestTime;
    dst.cguestTime = src.cguestTime;
    dst.startData = src.startData;
    dst.endData = src.endData;
    dst.startBrk = src.startBrk;
    dst.argStart = src.argStart;
    dst.argEnd = src.argEnd;
    dst.envStart = src.envStart;
    dst.envEnd = src.envEnd;
    dst.exitCode = src.exitCode;
}

void ProcDataManager::ConvertPidStatm(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatmEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidStatm = new ProcPidStatmEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidStatm[j].size = vec[j].size;
        out.pidStatm[j].resident = vec[j].resident;
        out.pidStatm[j].shared = vec[j].shared;
        out.pidStatm[j].text = vec[j].text;
        out.pidStatm[j].lib = vec[j].lib;
        out.pidStatm[j].data = vec[j].data;
        out.pidStatm[j].dt = vec[j].dt;
    }
}

void ProcDataManager::ConvertPidStatus(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStatusEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidStatus = new ProcPidStatusEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidStatus[j].numFields = vec[j].fields.size();
        out.pidStatus[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidIo(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidIoEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidIo = new ProcPidIoEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidIo[j].rchar = vec[j].rchar;
        out.pidIo[j].wchar = vec[j].wchar;
        out.pidIo[j].syscr = vec[j].syscr;
        out.pidIo[j].syscw = vec[j].syscw;
        out.pidIo[j].readBytes = vec[j].readBytes;
        out.pidIo[j].writeBytes = vec[j].writeBytes;
        out.pidIo[j].cancelledWriteBytes = vec[j].cancelledWriteBytes;
    }
}

void ProcDataManager::ConvertPidSmapsRollup(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidSmapsRollupEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidSmapsRollup = new ProcPidSmapsRollupEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidSmapsRollup[j].numFields = vec[j].fields.size();
        out.pidSmapsRollup[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidFd(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidFdEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidFd = new ProcPidFdEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pidFd[j].fdCount = vec[j].fdCount;
}

void ProcDataManager::ConvertPidNumaMaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidNumaMapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidNumaMaps = new ProcPidNumaMapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidNumaMaps[j].address = AllocStr(vec[j].address);
        out.pidNumaMaps[j].numFields = vec[j].fields.size();
        out.pidNumaMaps[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidSmaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidSmapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidSmaps = new ProcPidSmapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidSmaps[j].mapping = AllocStr(vec[j].mapping);
        out.pidSmaps[j].numFields = vec[j].fields.size();
        out.pidSmaps[j].fields = ConvertFields(vec[j].fields);
    }
}

void ProcDataManager::ConvertPidEnviron(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidEnvironEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidEnviron = new ProcPidEnvironEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidEnviron[j].name = AllocStr(vec[j].name);
        out.pidEnviron[j].value = AllocStr(vec[j].value);
    }
}

void ProcDataManager::ConvertPidCmdline(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidCmdlineEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidCmdline = new ProcPidCmdlineEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pidCmdline[j].cmdline = AllocStr(vec[j].cmdline);
}

void ProcDataManager::ConvertPidLimits(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidLimitsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidLimits = new ProcPidLimitsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidLimits[j].limit = AllocStr(vec[j].limit);
        out.pidLimits[j].soft = AllocStr(vec[j].soft);
        out.pidLimits[j].hard = AllocStr(vec[j].hard);
        out.pidLimits[j].units = AllocStr(vec[j].units);
    }
}

void ProcDataManager::ConvertPidStack(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidStackEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidStack = new ProcPidStackEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidStack[j].address = AllocStr(vec[j].address);
        out.pidStack[j].symbol = AllocStr(vec[j].symbol);
    }
}

void ProcDataManager::ConvertPidWchan(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidWchanEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidWchan = new ProcPidWchanEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pidWchan[j].wchan = AllocStr(vec[j].wchan);
}

void ProcDataManager::ConvertPidMaps(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidMapsEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidMaps = new ProcPidMapsEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidMaps[j].start = AllocStr(vec[j].start);
        out.pidMaps[j].end = AllocStr(vec[j].end);
        out.pidMaps[j].perms = AllocStr(vec[j].perms);
        out.pidMaps[j].offset = AllocStr(vec[j].offset);
        out.pidMaps[j].dev = AllocStr(vec[j].dev);
        out.pidMaps[j].inode = AllocStr(vec[j].inode);
        out.pidMaps[j].pathname = AllocStr(vec[j].pathname);
    }
}

void ProcDataManager::ConvertPidComm(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidCommEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidComm = new ProcPidCommEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++)
        out.pidComm[j].comm = AllocStr(vec[j].comm);
}

void ProcDataManager::ConvertPidTaskStat(const ProcDataInternal &src, struct ProcData &out)
{
    auto &vec = *static_cast<const vector<PidTaskStatEntryInternal>*>(src.entries);
    out.numEntries = vec.size();
    out.pidTaskStat = new ProcPidTaskStatEntry[vec.size()];
    for (size_t j = 0; j < vec.size(); j++) {
        out.pidTaskStat[j].comm = AllocStr(vec[j].comm);
        CopyPidStatEntry(vec[j], out.pidTaskStat[j]);
    }
}

