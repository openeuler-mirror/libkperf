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
#include "cpu_freq.h"
#include "pmu.h"
#include "pcerr.h"

using namespace pcerr;

CpuFreqManager* CpuFreqManager::instance = nullptr;
std::mutex CpuFreqManager::singleMutex;
std::mutex CpuFreqManager::initMutex;
std::vector<PmuCpuFreqDetail> CpuFreqManager::freqDetailList;
bool CpuFreqManager::hasInit = false;

PmuCpuFreqDetail* PmuReadCpuFreqDetail(unsigned* cpuNum) {
    auto& ds = CpuFreqManager::GetCpuFreqDetail();
    *cpuNum = ds.size();
    return ds.data();
}

int PmuOpenCpuFreqSampling(unsigned period) {
    return CpuFreqManager::GetInstance()->InitCpuFreqSampling(period);
}

void PmuCloseCpuFreqSampling() {
    CpuFreqManager::Clear();
}

void CpuFreqManager::Clear() {
    std::lock_guard<std::mutex> lock(singleMutex);
    if (instance == nullptr) {
        return;
    }
    delete instance;
    instance = nullptr;
}

CpuFreqManager* CpuFreqManager::GetInstance() {
    if (instance == nullptr) {
        std::lock_guard<std::mutex> lock(singleMutex);
        if(instance == nullptr) {
            instance = new CpuFreqManager();
        }
    }
    return instance;
}

int CpuFreqManager::CheckCpuFreqIsExist() {
    for(int cpuId = 0; cpuId < MAX_CPU_NUM; cpuId++) {
        int64_t freq = PmuGetCpuFreq(cpuId);
        if (freq == -1 ) {
            return -1;
        }
    }
    return 0;
}

int CpuFreqManager::CheckSleepPeriod(unsigned period) {
    if (period == 0 || period > 10000) {
        New(LIBPERF_ERR_INVALID_CPU_FREQ_PERIOD, "invalid period, the period must be less than 10000ms and greater than 0ms");
        return LIBPERF_ERR_INVALID_CPU_FREQ_PERIOD;
    }
    return SUCCESS;
}

int CpuFreqManager::InitCpuFreqSampling(unsigned period) {
    this->isEnable = true;
    if (hasInit) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(initMutex);

    if (CheckSleepPeriod(period) != 0) {
        return -1;
    }

    if (CheckCpuFreqIsExist() != 0) {
        return -1;
    }

    this->sleepPeriod = static_cast<double>(period) / 1000;
    this->cpuFreqThread = std::thread([this]() {
        while (!isEnd) {
            if (!isEnable) {
                continue;
            }
            std::lock_guard<std::mutex> lock(mapMutex);
            for (int cpu = 0; cpu < MAX_CPU_NUM; cpu++) {
                 int64_t freq = PmuGetCpuFreq(cpu);
                 if (freq == -1) {
                    continue;
                 }
                 if (this->freqListMap.find(cpu) != this->freqListMap.end()) {
                    this->freqListMap[cpu].push_back(freq);
                 } else {
                    std::vector<int64_t> freqList = {freq};
                    this->freqListMap.insert({cpu, freqList});
                 }
            }
            sleep(this->sleepPeriod);
        }
    });
    hasInit = true;
    return 0;
}

void CpuFreqManager::CalFreqDetail()  {
    isEnable = false;
    std::lock_guard<std::mutex> lock(mapMutex);

    if(!this->freqListMap.empty()) {
        uint64_t maxFreq, minFreq, sumFreq;
        for (int cpuId = 0; cpuId < MAX_CPU_NUM; cpuId++) {
            std::vector<int64_t> freqList;
            minFreq = 0;
            maxFreq = 0;
            sumFreq = 0;
            if (this->freqListMap.find(cpuId) != this->freqListMap.end()) {
                minFreq = UINT64_MAX;
                freqList = freqListMap[cpuId];
            }
            for (const auto& curFreq: freqList) {
                minFreq = minFreq > curFreq ? curFreq : minFreq;
                maxFreq = maxFreq > curFreq ? maxFreq : curFreq;
                sumFreq += curFreq;
            }
            uint64_t avgFreq = sumFreq / freqList.size();
            PmuCpuFreqDetail detail = {.cpuId=cpuId, .minFreq=minFreq, .maxFreq=maxFreq, .avgFreq=avgFreq};
            freqDetailList.push_back(detail);
        }
        freqListMap.clear();
    } else {
        GetCurFreqDetail();
    }

    isEnable = true;
}

void CpuFreqManager::GetCurFreqDetail() {
    for(int cpuId = 0; cpuId < MAX_CPU_NUM; cpuId++) {
        uint64_t freq = PmuGetCpuFreq(cpuId);
        if (freq == -1) {
            freq = 0;
        }
        PmuCpuFreqDetail detail = {.cpuId=cpuId, .minFreq=freq, .maxFreq=freq, .avgFreq=freq};
        freqDetailList.push_back(detail);
    }
}

std::vector<PmuCpuFreqDetail>& CpuFreqManager::GetCpuFreqDetail() {
    std::lock_guard<std::mutex> lock(initMutex);
    freqDetailList.clear();
    if (!hasInit) {
        CpuFreqManager::GetCurFreqDetail();
    } else {
        CpuFreqManager::GetInstance()->CalFreqDetail();
    }
    return freqDetailList;
}