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
 * Create: 2025-05-07
 * Description: sample cpu freq.
 ******************************************************************************/
#ifndef LIBKPERF_CPU_FREQ_H
#define LIBKPERF_CPU_FREQ_H

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <map>

#include "cpu_map.h"

class CpuFreqManager {
public:
    CpuFreqManager(): isEnable(false), isEnd(false), sleepPeriod(0.1) {};
    ~CpuFreqManager() {
        std::lock_guard<std::mutex> lock(initMutex);
        if(!hasInit) {
            return;
        }
        isEnable = false;
        isEnd = true;
        cpuFreqThread.join();
        hasInit = false;
    }
    static void Clear();
    static CpuFreqManager* GetInstance();
    static std::vector<PmuCpuFreqDetail>& GetCpuFreqDetail();
    static void GetCurFreqDetail();

    int InitCpuFreqSampling(unsigned period);
    void CalFreqDetail();

private:
    static CpuFreqManager* instance;
    static std::mutex singleMutex;
    static std::mutex initMutex;
    static std::vector<PmuCpuFreqDetail> freqDetailList;
    static bool hasInit;
    
    std::mutex mapMutex;
    std::thread cpuFreqThread;
    volatile bool isEnable;
    volatile bool isEnd;
    double sleepPeriod;
    std::map<int, std::vector<int64_t>> freqListMap;

    int CheckCpuFreqIsExist();
    static int CheckSleepPeriod(unsigned period);
};


#endif //LIBKPERF_CPU_FREQ_H