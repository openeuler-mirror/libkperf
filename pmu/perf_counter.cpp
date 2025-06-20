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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: implementations for reading performance counters and initializing counting logic in
 * the KUNPENG_PMU namespace.
 ******************************************************************************/
#include <climits>
#include <poll.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstring>
#include <sys/ioctl.h>
#include <iostream>
#include <linux/perf_event.h>
#include "pmu.h"
#include "linked_list.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "pcerr.h"
#include "log.h"
#include "perf_counter.h"

using namespace std;

static constexpr int MAX_ATTR_SIZE = 120;
/**
 * Read pmu counter and deal with pmu multiplexing
 * Right now we do not implement grouping logic, thus we ignore the
 * PERF_FORMAT_ID section for now
 */
int KUNPENG_PMU::PerfCounter::Read(vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps,
    std::vector<PmuDataExt*> &extPool, std::vector<PmuSwitchData> &swtichData)
{
    struct ReadFormat perfCountValue;

    /**
     * If some how the file descriptor is less than 0,
     * we make the count to be 0 and return
     */
    if (__glibc_unlikely(this->fd < 0)) {
        this->count = 0;
        return UNKNOWN_ERROR;
    }
    read(this->fd, &perfCountValue, sizeof(perfCountValue));
    if (perfCountValue.value < count || perfCountValue.timeEnabled < enabled || perfCountValue.timeRunning < running) {
        return LIBPERF_ERR_COUNT_OVERFLOW;
    }

    // Calculate the diff of count from last read.
    // In case of multiplexing, we follow the linux documentation for calculating the estimated
    // counting value (https://perf.wiki.kernel.org/index.php/Tutorial)
    double percent = 0.0;
    uint64_t increCount;
    if ((perfCountValue.value == count) || (perfCountValue.timeRunning == running)) {
        percent = -1;
        increCount = 0;   
    } else {
        percent = static_cast<double>(perfCountValue.timeEnabled - enabled) / static_cast<double>(perfCountValue.timeRunning - running);
        increCount = static_cast<uint64_t>((perfCountValue.value - count)* percent);
    }
    
    this->count = perfCountValue.value;
    this->enabled = perfCountValue.timeEnabled;
    this->running = perfCountValue.timeRunning;

    data.emplace_back(PmuData{0});
    auto& current = data.back();
    current.count = increCount;
    current.countPercent = 1.0 / percent;
    current.cpu = this->cpu;
    current.tid = this->pid;
    auto findProc = procMap.find(current.tid);
    if (findProc != procMap.end()) {
        current.pid = findProc->second->pid;
    }
    if(this->evt->cgroupName.size() != 0) {
        current.cgroupName = this->evt->cgroupName.c_str();
    }
    return SUCCESS;
}

/**
 * Initialize counting
 */
int KUNPENG_PMU::PerfCounter::Init(const bool groupEnable, const int groupFd, const int resetOutputFd)
{
    return this->MapPerfAttr(groupEnable, groupFd);
}

int KUNPENG_PMU::PerfCounter::MapPerfAttr(const bool groupEnable, const int groupFd)
{
    /**
     * For now, we only implemented the logic for CORE type events. Support for UNCORE PMU events will be
     * added soon
     */
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(struct perf_event_attr);
    attr.type = this->evt->type;
    attr.config = this->evt->config;
    attr.config1 = this->evt->config1;
    attr.config2 = this->evt->config2;

    /**
     * We want to set the disabled and inherit bit to collect child processes
     */
    attr.disabled = 1;
    attr.inherit = 1;

    // support cgroup feature
    unsigned flags = 0;
    if (this->GetCgroupFd() != -1) {
        flags = PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC;
        this->pid = this->GetCgroupFd();
    }

    /**
     * For now we set the format id bit to implement grouping logic in the future
     */
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING | PERF_FORMAT_ID;
    if (groupEnable) {
        /*
        * when creating an event group, typically the group leader is initialized with disabled bit set to 1,
        * and any child events are initialized with disabled bit set to 0. Despite disabled bit being set to 0,
        * the child events will not start counting until the group leader is enabled.
        */
        attr.disabled = 0;
        this->fd = PerfEventOpen(&attr, this->pid, this->cpu, groupFd, flags);
    } else {
#ifdef IS_X86
        if (this->evt->pmuType == KUNPENG_PMU::UNCORE_TYPE && !StartWith(this->evt->name, "cpu/")) {
            this->fd = PerfEventOpen(&attr, -1, this->cpu, groupFd, flags);
#else
        if (this->evt->pmuType == KUNPENG_PMU::UNCORE_TYPE && !StartWith(this->evt->name, "armv8_")) {
            this->fd = PerfEventOpen(&attr, -1, this->cpu, groupFd, 0);
#endif
        } else {
            this->fd = PerfEventOpen(&attr, this->pid, this->cpu, groupFd, flags);
        }
    }
    this->groupFd = groupFd;
    DBG_PRINT("type: %d cpu: %d config: %llx config1: %llx config2: %llx myfd: %d groupfd: %d\n",
        attr.type, cpu, attr.config, attr.config1, attr.config2, this->fd, groupFd);
    if (__glibc_unlikely(this->fd < 0)) {
        return MapErrno(errno);
    }
    return SUCCESS;
}

/**
 * Enable
 */
int KUNPENG_PMU::PerfCounter::Enable()
{
    if (groupFd != -1) {
        // Only group leader should use ioctl to enable, disable or reset,
        // otherwise each event in the group will be collected for different durations.
        return SUCCESS;
    }
    int err = PerfEvt::Enable();
    if (err != SUCCESS) {
        return err;
    }
    this->count = 0;
    this->enabled = 0;
    this->running = 0;
    return SUCCESS;
}

int KUNPENG_PMU::PerfCounter::Disable()
{
    if (groupFd != -1) {
        return SUCCESS;
    }
    return PerfEvt::Disable();
}

int KUNPENG_PMU::PerfCounter::Reset()
{
    if (groupFd != -1) {
        return SUCCESS;
    }
    return PerfEvt::Reset();
}