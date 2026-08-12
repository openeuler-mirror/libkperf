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
 * Create: 2026-06-16
 * Description: /proc data source types and API declarations
 ******************************************************************************/
#ifndef PROC_DATA_TYPES_H
#define PROC_DATA_TYPES_H

#include <unistd.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ProcSource {
    PROC_STAT,
    PROC_CPUINFO,
    PROC_MEMINFO,
    PROC_LOADAVG,
    PROC_VMSTAT,
    PROC_NET_DEV,
    PROC_DISKSTATS,
    PROC_UPTIME,
    PROC_MOUNTS,
    PROC_SOFTIRQS,
    PROC_SLABINFO,
    PROC_SCHEDSTAT,
    PROC_INTERRUPTS,
    PROC_IRQ_AFFINITY,
    PROC_LOCKS,
    PROC_ZONEINFO,
    PROC_BUDDYINFO,
    PROC_NET_SOCKSTAT,
    PROC_NET_NETSTAT,
    PROC_NET_ARP,
    PROC_VERSION,
    PROC_MODULES,
    PROC_FILESYSTEMS,
    PROC_SCSI,
    PROC_PRESSURE_CPU,
    PROC_PRESSURE_IO,
    PROC_SYS_KERNEL,
    PROC_SYS_FS,
    PROC_SYS_VM,
    PROC_SYS_NET_IPV4,
    PROC_SYS_NET_CORE,
    PROC_PID_STAT,
    PROC_PID_STATM,
    PROC_PID_STATUS,
    PROC_PID_IO,
    PROC_PID_SMAPS_ROLLUP,
    PROC_PID_FD,
    PROC_PID_NUMA_MAPS,
    PROC_PID_SMAPS,
    PROC_PID_ENVIRON,
    PROC_PID_CMDLINE,
    PROC_PID_LIMITS,
    PROC_PID_STACK,
    PROC_PID_WCHAN,
    PROC_PID_MAPS,
    PROC_PID_COMM,
    PROC_PID_TASK_STAT,
    MAX_PROC_SOURCE
};

struct ProcField {
    char *key;
    char *value;
};

enum ProcStatLineType {
    PROC_STAT_LINE_CPU = 0,
    PROC_STAT_LINE_INTR,
    PROC_STAT_LINE_CTXT,
    PROC_STAT_LINE_BTIME,
    PROC_STAT_LINE_PROCESSES,
    PROC_STAT_LINE_PROCS_RUNNING,
    PROC_STAT_LINE_PROCS_BLOCKED,
    PROC_STAT_LINE_SOFTIRQ
};

struct ProcStatEntry {
    char *cpuName;
    unsigned long long user, nice, system, idle, ioWait, irq, softirq, steal, guest, guestNice;
    enum ProcStatLineType lineType;
    unsigned long long ctxt, btime, processes, procsRunning, procsBlocked;
    unsigned long long intrTotal, numIntrPerIrq;
    unsigned long long *intrPerIrq;
    unsigned long long softirqTotal, numSoftirqPerType;
    unsigned long long *softirqPerType;
};

struct ProcCpuinfoEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcMeminfoEntry {
    char *key;
    unsigned long long value;
    char *unit;
};

struct ProcLoadavgEntry {
    double load1, load5, load15;
    unsigned runningProcs, totalProcs;
    int lastPid;
};

struct ProcVmstatEntry {
    char *key;
    unsigned long long value;
};

struct ProcNetDevEntry {
    char *iface;
    unsigned long long rxBytes, rxPackets, rxErrs, rxDrop, rxFifo, rxFrame, rxCompressed, rxMulticast;
    unsigned long long txBytes, txPackets, txErrs, txDrop, txFifo, txColls, txCarrier, txCompressed;
};

struct ProcDiskstatsEntry {
    int major, minor;
    char *device;
    unsigned long long readsCompleted, readsMerged, sectorsRead, msReading;
    unsigned long long writesCompleted, writesMerged, sectorsWritten, msWriting;
    unsigned long long iosInProgress, msIos, weightedMsIos;
    unsigned long long discardsCompleted, discardsMerged, sectorsDiscarded, msDiscarding;
    unsigned long long flushCompleted, msFlushing;
};

struct ProcUptimeEntry {
    double total, idle;
};

struct ProcMountsEntry {
    char *device, *mountPoint, *fsType, *options;
    int dump, passVal;
};

struct ProcSoftirqsEntry {
    char *type;
    unsigned numCpus;
    unsigned long long *perCpu;
};

struct ProcSlabinfoEntry {
    char *name;
    unsigned long long activeObjs, numObjs, objsize, objperslab, pagesperslab;
    unsigned long long limit, batchcount, sharedfactor;
    unsigned long long activeSlabs, numSlabs, sharedavail;
};

struct ProcSchedstatDomainEntry {
    int domainId;
    char *mask;
    unsigned numValues;
    unsigned long long *values;
};

struct ProcSchedstatEntry {
    int cpuId;
    unsigned long long yldCount, schedCount, schedGoidle;
    unsigned long long ttwuCount, ttwuLocal, rqCpuTime, runDelay, pcount;
    unsigned numDomains;
    struct ProcSchedstatDomainEntry *domains;
};

struct ProcInterruptsEntry {
    char *irq;
    unsigned numCpus;
    unsigned long long *perCpu;
    char *description;
};

struct ProcIrqAffinityEntry {
    char *affinity;
};

struct ProcLocksEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcZoneinfoPageset {
    int cpuId;
    unsigned long long count, high, batch, vmStatsThreshold;
};

struct ProcZoneinfoEntry {
    int node;
    char *zone;
    // Zone 级别 pages 统计
    unsigned long long pagesFree;
    unsigned long long pagesMin, pagesLow, pagesHigh;
    unsigned long long pagesSpanned, pagesPresent, pagesManaged, pagesCma;
    char *protection;
    char *nodeUnreclaimable;
    char *startPfn;
    // Node 级别统计 (per-node stats)
    unsigned numNodeStats;
    struct ProcField *nodeStats;
    // Zone 级别统计 (nr_zone_* 等)
    unsigned numStats;
    struct ProcField *stats;
    // pagesets
    unsigned numPagesets;
    struct ProcZoneinfoPageset *pagesets;
};

struct ProcBuddyinfoEntry {
    int node;
    char *zone, *zoneName;
    unsigned numOrders;
    unsigned long long *orders;
};

struct ProcNetSockstatEntry {
    char *protocol;
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcNetNetstatEntry {
    char *category;
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcNetArpEntry {
    char *ipAddress;
    char *hwType;
    char *flags;
    char *hwAddress;
    char *mask;
    char *device;
};

struct ProcVersionEntry {
    char *version;
};

struct ProcModulesEntry {
    char *name;
    unsigned long long size;
    int usedCount;
    char *usedBy, *state, *address, *taint;
};

struct ProcFilesystemsEntry {
    int nodev;
    char *fsType;
};

struct ProcScsiEntry {
    char *host;
    char *channel;
    char *id;
    char *lun;
    char *vendor;
    char *model;
    char *rev;
    char *type;
    char *ansiScsiRevision;
};

struct ProcPressureEntry {
    char *type;
    double avg10, avg60, avg300;
    unsigned long long total;
};

struct ProcSysDirEntry {
    char *name, *path, *value;
};

struct ProcPidStatEntry {
    int pid;
    char *comm;
    char state;
    int ppid, pgrp, session, ttyNr, tpgid;
    unsigned long long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    long long cutime, cstime;
    int priority, niceVal, numThreads;
    long long itrealvalue;
    unsigned long long starttime, vsize, rsslim;
    long long rss;
    unsigned long long startcode, endcode, startstack, kstkesp, kstkeip;
    unsigned long long signal, blocked, sigignore, sigcatch, wchan, nswap, cnswap;
    int exitSignal, processor;
    unsigned rtPriority, policy;
    unsigned long long delayacctBlkioTicks, guestTime;
    long long cguestTime;
    unsigned long long startData, endData, startBrk, argStart, argEnd, envStart, envEnd;
    int exitCode;
};

struct ProcPidStatmEntry {
    unsigned long long size, resident, shared, text, lib, data, dt;
};

struct ProcPidStatusEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidIoEntry {
    unsigned long long rchar, wchar, syscr, syscw, readBytes, writeBytes, cancelledWriteBytes;
};

struct ProcPidSmapsRollupEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidFdEntry {
    unsigned fdCount;
};

struct ProcPidNumaMapsEntry {
    char *address;
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidSmapsEntry {
    char *mapping;
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidEnvironEntry {
    char *name, *value;
};

struct ProcPidCmdlineEntry {
    char *cmdline;
};

struct ProcPidLimitsEntry {
    char *limit, *soft, *hard, *units;
};

struct ProcPidStackEntry {
    char *address, *symbol;
};

struct ProcPidWchanEntry {
    char *wchan;
};

struct ProcPidMapsEntry {
    char *start, *end, *perms, *offset, *dev, *inode, *pathname;
};

struct ProcPidCommEntry {
    char *comm;
};

struct ProcPidTaskStatEntry {
    int pid;
    char *comm;
    char state;
    int ppid, pgrp, session, ttyNr, tpgid;
    unsigned long long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    long long cutime, cstime;
    int priority, niceVal, numThreads;
    long long itrealvalue;
    unsigned long long starttime, vsize, rsslim;
    long long rss;
    unsigned long long startcode, endcode, startstack, kstkesp, kstkeip;
    unsigned long long signal, blocked, sigignore, sigcatch, wchan, nswap, cnswap;
    int exitSignal, processor;
    unsigned rtPriority, policy;
    unsigned long long delayacctBlkioTicks, guestTime;
    long long cguestTime;
    unsigned long long startData, endData, startBrk, argStart, argEnd, envStart, envEnd;
    int exitCode;
};

struct ProcData {
    enum ProcSource source;
    int pid;
    unsigned numEntries;
    char *filePath;
    union {
        struct ProcStatEntry *stat;
        struct ProcCpuinfoEntry *cpuinfo;
        struct ProcMeminfoEntry *meminfo;
        struct ProcLoadavgEntry *loadavg;
        struct ProcVmstatEntry *vmstat;
        struct ProcNetDevEntry *netDev;
        struct ProcDiskstatsEntry *diskstats;
        struct ProcUptimeEntry *uptime;
        struct ProcMountsEntry *mounts;
        struct ProcSoftirqsEntry *softirqs;
        struct ProcSlabinfoEntry *slabinfo;
        struct ProcSchedstatEntry *schedstat;
        struct ProcInterruptsEntry *interrupts;
        struct ProcIrqAffinityEntry *irqAffinity;
        struct ProcLocksEntry *locks;
        struct ProcZoneinfoEntry *zoneinfo;
        struct ProcBuddyinfoEntry *buddyinfo;
        struct ProcNetSockstatEntry *netSockstat;
        struct ProcNetNetstatEntry *netNetstat;
        struct ProcNetArpEntry *netArp;
        struct ProcVersionEntry *version;
        struct ProcModulesEntry *modules;
        struct ProcFilesystemsEntry *filesystems;
        struct ProcScsiEntry *scsi;
        struct ProcPressureEntry *pressure;
        struct ProcSysDirEntry *sysDir;
        struct ProcPidStatEntry *pidStat;
        struct ProcPidStatmEntry *pidStatm;
        struct ProcPidStatusEntry *pidStatus;
        struct ProcPidIoEntry *pidIo;
        struct ProcPidSmapsRollupEntry *pidSmapsRollup;
        struct ProcPidFdEntry *pidFd;
        struct ProcPidNumaMapsEntry *pidNumaMaps;
        struct ProcPidSmapsEntry *pidSmaps;
        struct ProcPidEnvironEntry *pidEnviron;
        struct ProcPidCmdlineEntry *pidCmdline;
        struct ProcPidLimitsEntry *pidLimits;
        struct ProcPidStackEntry *pidStack;
        struct ProcPidWchanEntry *pidWchan;
        struct ProcPidMapsEntry *pidMaps;
        struct ProcPidCommEntry *pidComm;
        struct ProcPidTaskStatEntry *pidTaskStat;
    };
};

#ifdef __cplusplus
}
#endif

#endif
