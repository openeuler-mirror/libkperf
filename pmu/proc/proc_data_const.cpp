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
 * Create: 2026-07-17
 * Description: definitions of /proc data source mapping constants
 ******************************************************************************/
#include "proc_data_const.h"

const std::map<ProcSource, std::string> PROC_SOURCE_PATH = {
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

const std::map<ProcSource, bool> PROC_SOURCE_NEED_PID = {
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

const std::map<ProcSource, bool> PROC_SOURCE_IS_SYS_DIR = {
    {PROC_SYS_KERNEL, true},
    {PROC_SYS_FS, true},
    {PROC_SYS_VM, true},
    {PROC_SYS_NET_IPV4, true},
    {PROC_SYS_NET_CORE, true}
};

const std::vector<std::string> SYS_KERNEL_FILES = {
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

const std::vector<std::string> SYS_FS_FILES = {
    "/proc/sys/fs/aio-max-nr",
    "/proc/sys/fs/aio-nr",
    "/proc/sys/fs/file-max",
    "/proc/sys/fs/file-nr",
    "/proc/sys/fs/nr_open"
};

const std::vector<std::string> SYS_VM_FILES = {
    "/proc/sys/vm/nr_hugepages",
    "/proc/sys/vm/oom_kill_allocating_task",
    "/proc/sys/vm/oom_dump_tasks",
    "/proc/sys/vm/watermark_scale_factor"
};

const std::vector<std::string> SYS_NET_IPV4_FILES = {
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

const std::vector<std::string> SYS_NET_CORE_FILES = {
    "/proc/sys/net/core/netdev_max_backlog",
    "/proc/sys/net/core/netdev_budget",
    "/proc/sys/net/core/somaxconn",
    "/proc/sys/net/core/rmem_default",
    "/proc/sys/net/core/rmem_max",
    "/proc/sys/net/core/wmem_default",
    "/proc/sys/net/core/wmem_max"
};

// 必须在所有 SYS_*_FILES 之后定义，因为取了它们的地址。
const std::map<ProcSource, const std::vector<std::string>*> SYS_DIR_FILES_MAP = {
    {PROC_SYS_KERNEL, &SYS_KERNEL_FILES},
    {PROC_SYS_FS, &SYS_FS_FILES},
    {PROC_SYS_VM, &SYS_VM_FILES},
    {PROC_SYS_NET_IPV4, &SYS_NET_IPV4_FILES},
    {PROC_SYS_NET_CORE, &SYS_NET_CORE_FILES}
};

const std::vector<ProcSource> SYSTEM_SOURCES = {
    PROC_STAT, PROC_CPUINFO, PROC_MEMINFO, PROC_LOADAVG, PROC_VMSTAT,
    PROC_NET_DEV, PROC_DISKSTATS, PROC_UPTIME, PROC_MOUNTS,
    PROC_SOFTIRQS, PROC_SLABINFO, PROC_SCHEDSTAT, PROC_INTERRUPTS,
    PROC_IRQ_AFFINITY, PROC_LOCKS, PROC_ZONEINFO, PROC_BUDDYINFO,
    PROC_NET_SOCKSTAT, PROC_NET_NETSTAT, PROC_NET_ARP, PROC_VERSION,
    PROC_MODULES, PROC_FILESYSTEMS, PROC_SCSI, PROC_PRESSURE_CPU, PROC_PRESSURE_IO,
    PROC_SYS_KERNEL, PROC_SYS_FS, PROC_SYS_VM, PROC_SYS_NET_IPV4, PROC_SYS_NET_CORE
};

const std::vector<ProcSource> PID_SOURCES = {
    PROC_PID_STAT, PROC_PID_STATM, PROC_PID_STATUS, PROC_PID_IO,
    PROC_PID_SMAPS_ROLLUP, PROC_PID_FD, PROC_PID_NUMA_MAPS,
    PROC_PID_SMAPS, PROC_PID_ENVIRON, PROC_PID_CMDLINE, PROC_PID_LIMITS,
    PROC_PID_STACK, PROC_PID_WCHAN, PROC_PID_MAPS, PROC_PID_COMM, PROC_PID_TASK_STAT
};
