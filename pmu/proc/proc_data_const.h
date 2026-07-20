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
 * Description: inline constants for /proc data source mapping and configuration
 ******************************************************************************/
#ifndef PROC_DATA_CONST_H
#define PROC_DATA_CONST_H

#include <string>
#include <vector>
#include <map>
#include "proc_data_types.h"

extern const std::map<ProcSource, std::string> PROC_SOURCE_PATH;
extern const std::map<ProcSource, bool> PROC_SOURCE_NEED_PID;
extern const std::map<ProcSource, bool> PROC_SOURCE_IS_SYS_DIR;
extern const std::vector<std::string> SYS_KERNEL_FILES;
extern const std::vector<std::string> SYS_FS_FILES;
extern const std::vector<std::string> SYS_VM_FILES;
extern const std::vector<std::string> SYS_NET_IPV4_FILES;
extern const std::vector<std::string> SYS_NET_CORE_FILES;
extern const std::map<ProcSource, const std::vector<std::string>*> SYS_DIR_FILES_MAP;
extern const std::vector<ProcSource> SYSTEM_SOURCES;
extern const std::vector<ProcSource> PID_SOURCES;

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
