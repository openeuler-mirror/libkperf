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
 * Author: Mr.Zhang
 * Create: 2024-04-03
 * Description: functions for managing and interacting with performance events using system calls and inline assembly
 ******************************************************************************/
#include <climits>
#include <unistd.h>
#include <iostream>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <linux/perf_event.h>
#include "evt.h"

enum class HEAD_SIZE {
    HEAD_SIZE_ONE = 1,
    HEAD_SIZE_TWO = 2,
    HEAD_SIZE_FOUR = 4,
    HEAD_SIZE_EIGHT = 8,
};

static constexpr double CUT_OFF_PERCENT = 0.05;
int KUNPENG_PMU::PerfEventOpen(struct perf_event_attr *attr, pid_t pid, int cpu, int groupFd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, groupFd, flags);
}

bool KUNPENG_PMU::PerfEvt::Enable()
{
    return (ioctl(this->fd, PERF_EVENT_IOC_ENABLE, 0) == 0);
}

bool KUNPENG_PMU::PerfEvt::Reset()
{
    return (ioctl(this->fd, PERF_EVENT_IOC_RESET, 0) == 0);
}

bool KUNPENG_PMU::PerfEvt::Disable()
{
    return ioctl(this->fd, PERF_EVENT_IOC_DISABLE, 0);
}

bool KUNPENG_PMU::PerfEvt::Close()
{
    close(this->fd);
    return true;
}

bool KUNPENG_PMU::PerfEvt::Start()
{
    this->Reset();
    return this->Enable();
}

bool KUNPENG_PMU::PerfEvt::Pause()
{
    return this->Disable();
}

__u64 KUNPENG_PMU::ReadOnce(__u64 *head)
{
    union {
        typeof(*head) val;
        char charHead[1];
    } pointerUnion = {.charHead = {0}};

    switch (static_cast<HEAD_SIZE>(sizeof(*head))) {
        case HEAD_SIZE::HEAD_SIZE_ONE:
            asm volatile("ldarb %w0, %1"
                    : "=r"(*(__u8 __attribute__((__may_alias__)) *)pointerUnion.charHead)
                    : "Q"(*head)
                    : "memory");
            break;
        case HEAD_SIZE::HEAD_SIZE_TWO:
            asm volatile("ldarh %w0, %1"
                    : "=r"(*(__u16 __attribute__((__may_alias__)) *)pointerUnion.charHead)
                    : "Q"(*head)
                    : "memory");
            break;
        case HEAD_SIZE::HEAD_SIZE_FOUR:
            asm volatile("ldar %w0, %1"
                    : "=r"(*(__u32 __attribute__((__may_alias__)) *)pointerUnion.charHead)
                    : "Q"(*head)
                    : "memory");
            break;
        case HEAD_SIZE::HEAD_SIZE_EIGHT:
            asm volatile("ldar %0, %1"
                    : "=r"(*(__u64 __attribute__((__may_alias__)) *)pointerUnion.charHead)
                    : "Q"(*head)
                    : "memory");
            break;
        default:
            break;
    }
    return pointerUnion.val;
}
