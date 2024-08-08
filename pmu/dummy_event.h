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
#include "pcerr.h"
#include "evt_list.h"

namespace KUNPENG_PMU {

    struct DummyContext {
        std::shared_ptr<EvtList> evtList;
        pid_t pid;
    };

    class DummyEventStrategy {
    public:
        virtual void DoHandler(DummyContext& ctx) = 0;
    };

    class ProcessForkStrategy : public DummyEventStrategy {
    public:
        void DoHandler(DummyContext& ctx)
        {
            ctx.evtList->AddNewProcess(ctx.pid);
        }
    };

    class DummyEvent {
    public:
        DummyEvent(std::vector<std::shared_ptr<EvtList>>& evtLists, std::vector<pid_t>& ppids) :
                evtLists(evtLists),
                ppids(ppids),
                dummyFlag(true),
                errNo(SUCCESS) {};

        ~DummyEvent();

        /**
         * @brief start a thread to observe fork thread.
         */
        void ObserverForkThread();

    private:
        std::thread dummyThread;
        std::atomic<bool> dummyFlag;
        std::vector<std::shared_ptr<EvtList>>& evtLists;
        std::vector<pid_t> ppids;
        std::vector<pid_t> exitPids;
        std::unordered_map<pid_t, std::pair<int, void*>> dummyMap;
        int errNo;

        void InitDummy(pid_t pid);
        void ParseDummyData(void* page, pid_t pid);
        void HandleDummyData();
        void ClearExitProcess();
    };

}
#endif // LIBKPROF_DUMMY_EVENT_H