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
 * Author: Mr.Li
 * Create: 2024-08-05
 * Description: start a thread to observe new fork thread.
 ******************************************************************************/
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <vector>
#include <sys/ioctl.h>

#include "log.h"
#include "dummy_event.h"

static const int PAGE_SIZE = sysconf(_SC_PAGESIZE);
static const int MAP_LEN = 69632;

namespace KUNPENG_PMU {

    ProcessForkStrategy forkStrategy;

    DummyEvent::~DummyEvent()
    {
        dummyFlag = false;
        if (dummyThread.joinable()) {
            dummyThread.join();
        }
        for (auto it = dummyMap.begin(); it != dummyMap.end(); it++) {
            close(it->second.first);
            munmap(it->second.second, MAP_LEN);
        }
        if (consumeThread.joinable()) {
            consumeThread.join();
        }
    }

    void DummyEvent::ObserverForkThread()
    {
        dummyThread = std::thread([this]() {
            for (const auto& pid: ppids) {
                this->InitDummy(pid);
            }
            while (dummyFlag) {
                this->HandleDummyData();
            }
        });

        consumeThread = std::thread([this]() {
            while (dummyFlag) {
                if (forkPidQueue.empty()) {
                    continue;
                }
                auto& pid = forkPidQueue.front();
                for (const auto& evtList: evtLists) {
                    DummyContext ctx = {evtList, static_cast<pid_t>(pid)};
                    forkStrategy.DoHandler(ctx);
                }
                std::lock_guard<std::mutex> lg(dummyMutex);
                forkPidQueue.pop();
            }
        });
    }

    void DummyEvent::InitDummy(pid_t pid)
    {
        struct perf_event_attr attr = {0};
        attr.size = sizeof(attr);
        attr.type = PERF_TYPE_SOFTWARE;
        attr.config = PERF_COUNT_SW_DUMMY;
        attr.exclude_kernel = 1;
        attr.disabled = 1;
        attr.sample_period = 1;
        attr.sample_type = PERF_SAMPLE_TIME;
        attr.sample_id_all = 1;
        attr.read_format = PERF_FORMAT_ID;
        attr.task = 1;
        attr.exclude_guest = 1;
        auto fd = PerfEventOpen(&attr, pid, -1, -1, 0);
        if (fd == -1) {
            ERR_PRINT("Failed open dummy event fd because of %s\n", strerror(errno));
            return;
        }

        if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
            ERR_PRINT("Failed enable dummy event fd\n");
            return;
        }

        auto fdMap = mmap(nullptr, MAP_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (fdMap == MAP_FAILED) {
            ERR_PRINT("Failed mmap dummy fd\n");
            return;
        }

        dummyMap.insert({pid, std::make_pair(fd, fdMap)});
    }

    void DummyEvent::HandleDummyData()
    {
        if (dummyMap.empty()) {
            dummyFlag = false;
        }
        for (const auto& it: dummyMap) {
            this->ParseDummyData(it.second.second, it.first);
        }
        this->ClearExitProcess();
    }

    void DummyEvent::ParseDummyData(void* page, pid_t pid)
    {
        auto* mapPage = (struct perf_event_mmap_page*) page;
        uint8_t* ringBuf = (uint8_t*) (mapPage) + PAGE_SIZE;
        uint64_t dataHead = mapPage->data_head;
        uint64_t dataTail = mapPage->data_tail;
        std::vector<pid_t> childPidList;
        while (dataTail < dataHead) {
            uint64_t off = dataTail % mapPage->data_size;
            auto* header = (struct perf_event_header*) (ringBuf + off);
            if (header->type == PERF_RECORD_FORK) {
                auto sample = (KUNPENG_PMU::PerfRecordFork*) header;
                std::lock_guard<std::mutex> lg(dummyMutex);
                forkPidQueue.push(sample->tid);
            }
            if (header->type == PERF_RECORD_EXIT) {
                auto sample = (KUNPENG_PMU::PerfRecordFork*) header;
                if (sample->pid == sample->tid && sample->pid == pid) {
                    exitPids.push_back(pid);
                }
            }
            dataTail += header->size;
        }
        mapPage->data_tail = mapPage->data_head;
    }

    void DummyEvent::ClearExitProcess()
    {
        if (exitPids.empty()) {
            return;
        }
        for (const auto& pid: exitPids) {
            if (dummyMap.find(pid) == dummyMap.end()) {
                continue;
            }
            std::pair<pid_t, void*> pair = dummyMap.at(pid);
            dummyMap.erase(pid);
            close(pair.first);
            munmap(pair.second, MAP_LEN);
        }
        exitPids.clear();
    }
}
