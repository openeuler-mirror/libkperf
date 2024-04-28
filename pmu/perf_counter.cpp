/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
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
int KUNPENG_PMU::PerfCounter::Read(vector<PmuData> &data, std::vector<PerfSampleIps> &sampleIps)
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

    /**
     * In case of multiplexing, we follow the linux documentation for calculating the estimated
     * counting value (https://perf.wiki.kernel.org/index.php/Tutorial)
    */

    /**
     * For now we assume PMU register was reset before each collection, so we assign the counting value to the
     * count section in data. We will implement the aggregating logic soon
     */
    this->count = perfCountValue.value * static_cast<double>(perfCountValue.timeEnabled) /
	    		static_cast<double>(perfCountValue.timeRunning);
    data.emplace_back(PmuData{0});
    auto& current = data.back();
    current.count = this->count;
    current.cpu = static_cast<unsigned>(this->cpu);
    current.tid = this->pid;
    auto findProc = procMap.find(current.tid);
    if (findProc != procMap.end()) {
        current.pid = findProc->second->pid;
    }
    return SUCCESS;
}

/**
 * Initialize counting
 */
int KUNPENG_PMU::PerfCounter::Init()
{
    return this->MapPerfAttr();
}

int KUNPENG_PMU::PerfCounter::MapPerfAttr()
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

    /**
     * We want to set the disabled and inherit bit to collect child processes
     */
    attr.disabled = 1;
    attr.inherit = 1;

    /**
     * For now we set the format id bit to implement grouping logic in the future
     */
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING | PERF_FORMAT_ID;
    if (this->evt->pmuType == KUNPENG_PMU::UNCORE_TYPE) {
        this->fd = PerfEventOpen(&attr, -1, this->cpu, -1, 0);
    } else {
        this->fd = PerfEventOpen(&attr, this->pid, this->cpu, -1, 0);
    }
    DBG_PRINT("type: %d cpu: %d config: %X\n", attr.type, cpu, attr.config);
    if (__glibc_unlikely(this->fd < 0)) {
        return MapErrno(errno);
    }
    return SUCCESS;
}
