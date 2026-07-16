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
    char *cpu_name;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    enum ProcStatLineType lineType;
    unsigned long long ctxt, btime, processes, procs_running, procs_blocked;
    unsigned long long intr_total, numIntrPerIrq;
    unsigned long long *intr_per_irq;
    unsigned long long softirq_total, numSoftirqPerType;
    unsigned long long *softirq_per_type;
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
    unsigned running_procs, total_procs;
    int last_pid;
};

struct ProcVmstatEntry {
    char *key;
    unsigned long long value;
};

struct ProcNetDevEntry {
    char *iface;
    unsigned long long rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
    unsigned long long tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
};

struct ProcDiskstatsEntry {
    int major, minor;
    char *device;
    unsigned long long reads_completed, reads_merged, sectors_read, ms_reading;
    unsigned long long writes_completed, writes_merged, sectors_written, ms_writing;
    unsigned long long ios_in_progress, ms_ios, weighted_ms_ios;
    unsigned long long discards_completed, discards_merged, sectors_discarded, ms_discarding;
    unsigned long long flush_completed, ms_flushing;
};

struct ProcUptimeEntry {
    double total, idle;
};

struct ProcMountsEntry {
    char *device, *mount_point, *fs_type, *options;
    int dump, pass_val;
};

struct ProcSoftirqsEntry {
    char *type;
    unsigned numCpus;
    unsigned long long *per_cpu;
};

struct ProcSlabinfoEntry {
    char *name;
    unsigned long long active_objs, num_objs, objsize, objperslab, pagesperslab;
    unsigned long long limit, batchcount, sharedfactor;
    unsigned long long active_slabs, num_slabs, sharedavail;
};

struct ProcSchedstatDomainEntry {
    int domain_id;
    char *mask;
    unsigned numValues;
    unsigned long long *values;
};

struct ProcSchedstatEntry {
    int cpu_id;
    unsigned long long yld_count, sched_count, sched_goidle;
    unsigned long long ttwu_count, ttwu_local, rq_cpu_time, run_delay, pcount;
    unsigned numDomains;
    struct ProcSchedstatDomainEntry *domains;
};

struct ProcInterruptsEntry {
    char *irq;
    unsigned numCpus;
    unsigned long long *per_cpu;
    char *description;
};

struct ProcIrqAffinityEntry {
    char *affinity;
};

struct ProcLockStatEntry {
    char *class_name;
    unsigned long long con, bwt, adt, adt_max, adt_min;
    unsigned long long wct, wwt, wwt_max, wwt_min;
    unsigned long long wst, wst_max, wst_min;
    unsigned long long rwt, rwt_max, rwt_min;
    unsigned long long rpt, rpt_max, rpt_min;
};

struct ProcLocksEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcZoneinfoPageset {
    int cpu_id;
    unsigned long long count, high, batch, vm_stats_threshold;
};

struct ProcZoneinfoEntry {
    int node;
    char *zone;
    // Zone 级别 pages 统计
    unsigned long long pages_free;
    unsigned long long pages_min, pages_low, pages_high;
    unsigned long long pages_spanned, pages_present, pages_managed, pages_cma;
    char *protection;
    char *node_unreclaimable;
    char *start_pfn;
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
    char *zone, *zone_name;
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
    char *ip_address;
    char *hw_type;
    char *flags;
    char *hw_address;
    char *mask;
    char *device;
};

struct ProcVersionEntry {
    char *version;
};

struct ProcModulesEntry {
    char *name;
    unsigned long long size;
    int used_count;
    char *used_by, *state, *address, *taint;
};

struct ProcFilesystemsEntry {
    int nodev;
    char *fs_type;
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
    char *ansi_scsi_revision;
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

struct ProcPidStatmEntry {
    unsigned long long size, resident, shared, text, lib, data, dt;
};

struct ProcPidStatusEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidIoEntry {
    unsigned long long rchar, wchar, syscr, syscw, read_bytes, write_bytes, cancelled_write_bytes;
};

struct ProcPidSmapsRollupEntry {
    unsigned numFields;
    struct ProcField *fields;
};

struct ProcPidFdEntry {
    unsigned fd_count;
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
        struct ProcNetDevEntry *net_dev;
        struct ProcDiskstatsEntry *diskstats;
        struct ProcUptimeEntry *uptime;
        struct ProcMountsEntry *mounts;
        struct ProcSoftirqsEntry *softirqs;
        struct ProcSlabinfoEntry *slabinfo;
        struct ProcSchedstatEntry *schedstat;
        struct ProcInterruptsEntry *interrupts;
        struct ProcIrqAffinityEntry *irq_affinity;
        struct ProcLockStatEntry *lock_stat;
        struct ProcLocksEntry *locks;
        struct ProcZoneinfoEntry *zoneinfo;
        struct ProcBuddyinfoEntry *buddyinfo;
        struct ProcNetSockstatEntry *net_sockstat;
        struct ProcNetNetstatEntry *net_netstat;
        struct ProcNetArpEntry *net_arp;
        struct ProcVersionEntry *version;
        struct ProcModulesEntry *modules;
        struct ProcFilesystemsEntry *filesystems;
        struct ProcScsiEntry *scsi;
        struct ProcPressureEntry *pressure;
        struct ProcSysDirEntry *sys_dir;
        struct ProcPidStatEntry *pid_stat;
        struct ProcPidStatmEntry *pid_statm;
        struct ProcPidStatusEntry *pid_status;
        struct ProcPidIoEntry *pid_io;
        struct ProcPidSmapsRollupEntry *pid_smaps_rollup;
        struct ProcPidFdEntry *pid_fd;
        struct ProcPidNumaMapsEntry *pid_numa_maps;
        struct ProcPidSmapsEntry *pid_smaps;
        struct ProcPidEnvironEntry *pid_environ;
        struct ProcPidCmdlineEntry *pid_cmdline;
        struct ProcPidLimitsEntry *pid_limits;
        struct ProcPidStackEntry *pid_stack;
        struct ProcPidWchanEntry *pid_wchan;
        struct ProcPidMapsEntry *pid_maps;
        struct ProcPidCommEntry *pid_comm;
        struct ProcPidTaskStatEntry *pid_task_stat;
    };
};

#ifdef __cplusplus
}
#endif

#endif
