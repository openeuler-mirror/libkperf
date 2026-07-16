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
 * Create: 2026-06-09
 * Description: Static constants for /proc data source mapping and configuration
 ******************************************************************************/
#ifndef PROC_DATA_CONST_H
#define PROC_DATA_CONST_H

#include <string>
#include <vector>
#include <unordered_map>
#include "proc_data_types.h"

using namespace std;

static const unordered_map<ProcSource, string> PROC_SOURCE_PATH = {
    {PROC_STAT, "/proc/stat"},
    {PROC_CPUINFO, "/proc/cpuinfo"},
    {PROC_MEMINFO, "/proc/meminfo"},
    {PROC_LOADAVG, "/proc/loadavg"},
    {PROC_VMSTAT, "/proc/vmstat"},
    {PROC_NET_DEV, "/proc/net/dev"},
    {PROC_DISKSTATS, "/proc/diskstats"},
    {PROC_UPTIME, "/proc/uptime"},
    {PROC_MOUNTS, "/proc/mounts"},
    {PROC_SOFTIRQS, "/proc/softirqs"},
    {PROC_SLABINFO, "/proc/slabinfo"},
    {PROC_SCHEDSTAT, "/proc/schedstat"},
    {PROC_INTERRUPTS, "/proc/interrupts"},
    {PROC_IRQ_AFFINITY, "/proc/irq/default_smp_affinity"},
    {PROC_LOCKS, "/proc/locks"},
    {PROC_ZONEINFO, "/proc/zoneinfo"},
    {PROC_BUDDYINFO, "/proc/buddyinfo"},
    {PROC_NET_SOCKSTAT, "/proc/net/sockstat"},
    {PROC_NET_NETSTAT, "/proc/net/netstat"},
    {PROC_NET_ARP, "/proc/net/arp"},
    {PROC_VERSION, "/proc/version"},
    {PROC_MODULES, "/proc/modules"},
    {PROC_FILESYSTEMS, "/proc/filesystems"},
    {PROC_SCSI, "/proc/scsi/scsi"},
    {PROC_PRESSURE_CPU, "/proc/pressure/cpu"},
    {PROC_PRESSURE_IO, "/proc/pressure/io"},
    {PROC_PID_STAT, "/proc/%d/stat"},
    {PROC_PID_STATM, "/proc/%d/statm"},
    {PROC_PID_STATUS, "/proc/%d/status"},
    {PROC_PID_IO, "/proc/%d/io"},
    {PROC_PID_SMAPS_ROLLUP, "/proc/%d/smaps_rollup"},
    {PROC_PID_FD, "/proc/%d/fd"},
    {PROC_PID_NUMA_MAPS, "/proc/%d/numa_maps"},
    {PROC_PID_SMAPS, "/proc/%d/smaps"},
    {PROC_PID_ENVIRON, "/proc/%d/environ"},
    {PROC_PID_CMDLINE, "/proc/%d/cmdline"},
    {PROC_PID_LIMITS, "/proc/%d/limits"},
    {PROC_PID_STACK, "/proc/%d/stack"},
    {PROC_PID_WCHAN, "/proc/%d/wchan"},
    {PROC_PID_MAPS, "/proc/%d/maps"},
    {PROC_PID_COMM, "/proc/%d/comm"},
    {PROC_PID_TASK_STAT, "/proc/%d/task"}
};

static const unordered_map<ProcSource, bool> PROC_SOURCE_NEED_PID = {
    {PROC_PID_STAT, true},
    {PROC_PID_STATM, true},
    {PROC_PID_STATUS, true},
    {PROC_PID_IO, true},
    {PROC_PID_SMAPS_ROLLUP, true},
    {PROC_PID_FD, true},
    {PROC_PID_NUMA_MAPS, true},
    {PROC_PID_SMAPS, true},
    {PROC_PID_ENVIRON, true},
    {PROC_PID_CMDLINE, true},
    {PROC_PID_LIMITS, true},
    {PROC_PID_STACK, true},
    {PROC_PID_WCHAN, true},
    {PROC_PID_MAPS, true},
    {PROC_PID_COMM, true},
    {PROC_PID_TASK_STAT, true}
};

static const unordered_map<ProcSource, bool> PROC_SOURCE_IS_SYS_DIR = {
    {PROC_SYS_KERNEL, true},
    {PROC_SYS_FS, true},
    {PROC_SYS_VM, true},
    {PROC_SYS_NET_IPV4, true},
    {PROC_SYS_NET_CORE, true}
};

static const vector<string> SYS_KERNEL_FILES = {
    "/proc/sys/kernel/perf_event_paranoid",
    "/proc/sys/kernel/pid_max",
    "/proc/sys/kernel/threads-max",
    "/proc/sys/kernel/kptr_restrict",
    "/proc/sys/kernel/lock_stat",
    "/proc/sys/kernel/tainted",
    "/proc/sys/kernel/sched_cluster",
    "/proc/sys/kernel/sched_schedstats",
    "/proc/sys/kernel/sched_latency_ns",
    "/proc/sys/kernel/sched_min_granularity_ns",
    "/proc/sys/kernel/sched_wakeup_granularity_ns",
    "/proc/sys/kernel/sched_tunable_scaling",
    "/proc/sys/kernel/sched_migration_cost_ns",
    "/proc/sys/kernel/sched_autogroup_enabled",
    "/proc/sys/kernel/sched_child_runs_first",
    "/proc/sys/kernel/sched_rt_period_us",
    "/proc/sys/kernel/sched_rt_runtime_us",
    "/proc/sys/kernel/sched_util_low_pct",
    "/proc/sys/kernel/sched_soft_runtime_ratio",
    "/proc/sys/kernel/numa_balancing"
};

static const vector<string> SYS_FS_FILES = {
    "/proc/sys/fs/aio-max-nr",
    "/proc/sys/fs/aio-nr",
    "/proc/sys/fs/file-max",
    "/proc/sys/fs/file-nr",
    "/proc/sys/fs/nr_open"
};

static const vector<string> SYS_VM_FILES = {
    "/proc/sys/vm/nr_hugepages",
    "/proc/sys/vm/oom_kill_allocating_task",
    "/proc/sys/vm/oom_dump_tasks",
    "/proc/sys/vm/watermark_scale_factor"
};

static const vector<string> SYS_NET_IPV4_FILES = {
    "/proc/sys/net/ipv4/tcp_tw_reuse",
    "/proc/sys/net/ipv4/tcp_timestamps",
    "/proc/sys/net/ipv4/tcp_sack",
    "/proc/sys/net/ipv4/tcp_window_scaling",
    "/proc/sys/net/ipv4/tcp_congestion_control",
    "/proc/sys/net/ipv4/tcp_rmem",
    "/proc/sys/net/ipv4/tcp_wmem",
    "/proc/sys/net/ipv4/tcp_mem",
    "/proc/sys/net/ipv4/tcp_max_syn_backlog",
    "/proc/sys/net/ipv4/tcp_fin_timeout",
    "/proc/sys/net/ipv4/ip_local_port_range",
    "/proc/sys/net/ipv4/netdev_max_backlog",
    "/proc/sys/net/ipv4/netdev_budget",
    "/proc/sys/net/ipv4/somaxconn",
    "/proc/sys/net/ipv4/rmem_default",
    "/proc/sys/net/ipv4/rmem_max",
    "/proc/sys/net/ipv4/wmem_default",
    "/proc/sys/net/ipv4/wmem_max"
};

static const vector<string> SYS_NET_CORE_FILES = {
    "/proc/sys/net/core/netdev_max_backlog",
    "/proc/sys/net/core/netdev_budget",
    "/proc/sys/net/core/somaxconn",
    "/proc/sys/net/core/rmem_default",
    "/proc/sys/net/core/rmem_max",
    "/proc/sys/net/core/wmem_default",
    "/proc/sys/net/core/wmem_max"
};

static const unordered_map<ProcSource, const vector<string>*> SYS_DIR_FILES_MAP = {
    {PROC_SYS_KERNEL, &SYS_KERNEL_FILES},
    {PROC_SYS_FS, &SYS_FS_FILES},
    {PROC_SYS_VM, &SYS_VM_FILES},
    {PROC_SYS_NET_IPV4, &SYS_NET_IPV4_FILES},
    {PROC_SYS_NET_CORE, &SYS_NET_CORE_FILES}
};

static const vector<ProcSource> SYSTEM_SOURCES = {
    PROC_STAT, PROC_CPUINFO, PROC_MEMINFO, PROC_LOADAVG, PROC_VMSTAT,
    PROC_NET_DEV, PROC_DISKSTATS, PROC_UPTIME, PROC_MOUNTS,
    PROC_SOFTIRQS, PROC_SLABINFO, PROC_SCHEDSTAT, PROC_INTERRUPTS,
    PROC_IRQ_AFFINITY, PROC_LOCKS, PROC_ZONEINFO, PROC_BUDDYINFO,
    PROC_NET_SOCKSTAT, PROC_NET_NETSTAT, PROC_NET_ARP, PROC_VERSION,
    PROC_MODULES, PROC_FILESYSTEMS, PROC_SCSI, PROC_PRESSURE_CPU, PROC_PRESSURE_IO,
    PROC_SYS_KERNEL, PROC_SYS_FS, PROC_SYS_VM, PROC_SYS_NET_IPV4, PROC_SYS_NET_CORE
};

static const vector<ProcSource> PID_SOURCES = {
    PROC_PID_STAT, PROC_PID_STATM, PROC_PID_STATUS, PROC_PID_IO,
    PROC_PID_SMAPS_ROLLUP, PROC_PID_FD, PROC_PID_NUMA_MAPS,
    PROC_PID_SMAPS, PROC_PID_ENVIRON, PROC_PID_CMDLINE, PROC_PID_LIMITS,
    PROC_PID_STACK, PROC_PID_WCHAN, PROC_PID_MAPS, PROC_PID_COMM, PROC_PID_TASK_STAT
};

enum StatField : int {
    USER = 0,
    NICE = 1,
    SYSTEM = 2,
    STAT_IDLE = 3,
    IOWAIT = 4,
    IRQ = 5,
    SOFTIRQ = 6,
    STEAL = 7,
    GUEST = 8,
    GUEST_NICE = 9,
};

enum LoadavgField : int {
    LOAD1 = 0,
    LOAD5 = 1,
    LOAD15 = 2,
    RUNNING_TOTAL_PROCS = 3,
    LAST_PID = 4,
};

enum VmstatField : int {
    KEY = 0,
    VALUE = 1,
};

enum NetDevField : int {
    RX_BYTES = 0,
    RX_PACKETS = 1,
    RX_ERRS = 2,
    RX_DROP = 3,
    RX_FIFO = 4,
    RX_FRAME = 5,
    RX_COMPRESSED = 6,
    RX_MULTICAST = 7,
    TX_BYTES = 8,
    TX_PACKETS = 9,
    TX_ERRS = 10,
    TX_DROP = 11,
    TX_FIFO = 12,
    TX_COLLS = 13,
    TX_CARRIER = 14,
    TX_COMPRESSED = 15,
};

enum DiskstatsField : int {
    MAJOR = 0,
    MINOR = 1,
    DISK_DEVICE = 2,
    READS_COMPLETED = 3,
    READS_MERGED = 4,
    SECTORS_READ = 5,
    MS_READING = 6,
    WRITES_COMPLETED = 7,
    WRITES_MERGED = 8,
    SECTORS_WRITTEN = 9,
    MS_WRITING = 10,
    IOS_IN_PROGRESS = 11,
    MS_IOS = 12,
    WEIGHTED_MS_IOS = 13,
    DISCARDS_COMPLETED = 14,
    DISCARDS_MERGED = 15,
    SECTORS_DISCARDED = 16,
    MS_DISCARDING = 17,
    FLUSH_COMPLETED = 18,
    MS_FLUSHING = 19,
};

enum UptimeField : int {
    TOTAL = 0,
    UPTIME_IDLE = 1,
};

enum MountsField : int {
    MOUNT_DEVICE = 0,
    MOUNT_POINT = 1,
    FS_TYPE = 2,
    OPTIONS = 3,
    DUMP = 4,
    PASS = 5,
};

enum SlabinfoField : int {
    NAME = 0,
    ACTIVE_OBJS = 1,
    NUM_OBJS = 2,
    OBJSIZE = 3,
    OBJPERSLAB = 4,
    PAGESPERSLAB = 5,
    LIMIT = 6,
    BATCHCOUNT = 7,
    SHAREDFACTOR = 8,
    ACTIVE_SLABS = 9,
    NUM_SLABS = 10,
    SHAREDAVAIL = 11,
};

enum SchedstatField : int {
    YLD_COUNT = 1,
    SCHED_COUNT = 2,
    SCHED_GOIDLE = 3,
    TTWU_COUNT = 4,
    TTWU_LOCAL = 5,
    RQ_CPU_TIME = 6,
    RUN_DELAY = 7,
    PCOUNT = 8,
};

enum NetArpField : int {
    IP_ADDRESS = 0,
    HW_TYPE = 1,
    ARP_FLAGS = 2,
    HW_ADDRESS = 3,
    MASK = 4,
    ARP_DEVICE = 5,
};

enum PidStatField : int {
    PPID = 1,
    PGRP = 2,
    SESSION = 3,
    TTY_NR = 4,
    TPGID = 5,
    PIDSTAT_FLAGS = 6,
    MINFLT = 7,
    CMINFLT = 8,
    MAJFLT = 9,
    CMAJFLT = 10,
    UTIME = 11,
    STIME = 12,
    CUTIME = 13,
    CSTIME = 14,
    PRIORITY = 15,
    NICE_VAL = 16,
    NUM_THREADS = 17,
    ITREALVALUE = 18,
    STARTTIME = 19,
    VSIZE = 20,
    RSS = 21,
    RSSLIM = 22,
    STARTCODE = 23,
    ENDCODE = 24,
    STARTSTACK = 25,
    KSTKESP = 26,
    KSTKEIP = 27,
    SIGNAL = 28,
    BLOCKED = 29,
    SIGIGNORE = 30,
    SIGCATCH = 31,
    WCHAN = 32,
    NSWAP = 33,
    CNSWAP = 34,
    EXIT_SIGNAL = 35,
    PROCESSOR = 36,
    RT_PRIORITY = 37,
    POLICY = 38,
    DELAYACCT_BLKIO_TICKS = 39,
    GUEST_TIME = 40,
    CGUEST_TIME = 41,
    START_DATA = 42,
    END_DATA = 43,
    START_BRK = 44,
    ARG_START = 45,
    ARG_END = 46,
    ENV_START = 47,
    ENV_END = 48,
    EXIT_CODE = 49,
};

enum PidStatmField : int {
    SIZE = 0,
    RESIDENT = 1,
    SHARED = 2,
    TEXT = 3,
    LIB = 4,
    DATA = 5,
    DT = 6,
};

enum PidMapsField : int {
    ADDR_RANGE = 0,
    PERMS = 1,
    OFFSET = 2,
    DEV = 3,
    INODE = 4,
    PATH = 5,
};

#endif  // PROC_DATA_CONST_H
