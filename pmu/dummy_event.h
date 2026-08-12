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
#ifndef LIBKPROF_DUMMY_EVENT_H
#define LIBKPROF_DUMMY_EVENT_H

#include <thread>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include "pcerr.h"
#include "pmu_list.h"

namespace KUNPENG_PMU {

    class DummyEvent {
    public:
        DummyEvent(const unsigned &pd, std::vector<pid_t>& ppids, bool excludeGuest, bool excludeHost) :
                dummyFlag(true),
                ppids(ppids),
                pd(pd),
                excludeGuest(excludeGuest),
                excludeHost(excludeHost) {};

        ~DummyEvent();
        /**
         * @brief start a thread to observe fork thread.
         */
        void ObserverForkThread();

    private:
        std::thread dummyThread;
        std::thread consumeThread;
        volatile std::atomic<bool> dummyFlag;
        std::vector<pid_t> ppids;
        unsigned pd;
        bool excludeGuest;
        bool excludeHost;
        std::vector<pid_t> exitPids;
        std::unordered_map<pid_t, std::pair<int, void*>> dummyMap;
        std::condition_variable hasDataCond;

        std::mutex dummyMutex;
        std::queue<int> forkPidQueue;
        std::vector<std::thread> childThreads;

        void ConsumeForkQueue();
        void InitDummy(pid_t pid);
        void ParseDummyData(void* page, pid_t pid);
        void HandleDummyData();
        void ClearExitProcess();
    };

}
#endif // LIBKPROF_DUMMY_EVENT_H