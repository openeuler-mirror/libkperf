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
 * Author: 
 * Create: 2025-03-29
 * Description: functions for collect metric for L3, DDR, SMMU, PCIE and so on.
 ******************************************************************************/
#include "pmu.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <numa.h>
#include "uncore.h"
#include "pcerrc.h"
#include "pcerr.h"

using namespace std;
using namespace pcerr;

namespace KUNPENG_PMU {
    struct UncoreDeviceConfig {
        string devicePrefix;
        string subDeviceName;
        vector<string> events;
        string extraConfig;
        string bdfParameter;
        unsigned splitPosition;
    };

    static const unordered_map<PmuDeviceMetric, UncoreDeviceConfig> DEVICE_CONFIGS = {
        {PmuDeviceMetric::PMU_DDR_READ_BW, {"hisi_sccl", "ddrc", {"0x84"}, "", "", 1}},
        {PmuDeviceMetric::PMU_DDR_WRITE_BW, {"hisi_sccl", "ddrc", {"0x83"}, "", "", 1}},
        {PmuDeviceMetric::PMU_L3_TRAFFIC, {"armv8_pmu", "", {"0x0032"}, "", "", 0}},
        {PmuDeviceMetric::PMU_L3_MISS, {"armv8_pmu", "", {"0x0033"}, "", "", 0}},
        {PmuDeviceMetric::PMU_L3_LAT, {"hisi_sccl", "l3c", {"0x80"}, "", "", 1}},
        {PmuDeviceMetric::PMU_PCIE_RX_MRD_BW, {"hisi_pcie", "core", {"0x0804", "0x10804"}, "", "bdf=", 1}},
        {PmuDeviceMetric::PMU_PCIE_RX_MWR_BW, {"hisi_pcie", "core", {"0x0104", "0x10104"}, "", "bdf=", 1}},
        {PmuDeviceMetric::PMU_PCIE_TX_MRD_BW, {"hisi_pcie", "core", {"0x0405", "0x10405"}, "", "bdf=", 1}},
        {PmuDeviceMetric::PMU_PCIE_TX_MWR_BW, {"hisi_pcie", "core", {"0x0105", "0x10105"}, "", "bdf=", 1}},
        {PmuDeviceMetric::PMU_SMMU_TRAN, {"smmuv3_pmcg", "", {"0x1"}, "filter_enable=1", "filter_stream_id=", 2}}};

    struct InnerDeviceData {
        enum PmuDeviceMetric metric;
        const char *evtName;
        uint64_t count;
        union {
            unsigned coreId;
            unsigned numaId;
            char *bdf;
        };
    };

    using MetricMap = unordered_map<PmuDeviceMetric, vector<InnerDeviceData>>;
    unordered_map<PmuDeviceData*, vector<PmuDeviceData>> deviceDataMap;

    string ExtractEvtStr(const string fieldName, const string &evtName)
    {
        string fieldEq = fieldName + "=";
        auto pos = evtName.find(fieldEq);
        if (pos != string::npos) {
            size_t start = pos + fieldEq.size();
            size_t end = evtName.find(",", start);
            if (end == string::npos) {
                end = evtName.find("/", start);
            }
            return evtName.substr(start, end - start);
        }
        return "";
    }

    static bool GetDeviceName(const string &evt, string &devName, string &evtName)
    {
        auto findSlash = evt.find('/');
        if (findSlash == string::npos) {
            evtName = evt;
            return true;
        }
        devName = evt.substr(0, findSlash);
        evtName = evt.substr(devName.size() + 1, evt.size() - 1 - (devName.size() + 1));
        return true;
    }

    static uint64_t DDRBw(const uint64_t rawCount)
    {
        return 32 * rawCount;
    }

    static uint64_t L3Bw(const uint64_t rawCount)
    {
        return 64 * rawCount;
    }

    int DefaultAggregate(const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        for (auto &data : rawData) {
            PmuDeviceData outData;
            outData.metric = data.metric;
            outData.count = data.count;
            outData.coreId = data.coreId;
            devData.push_back(outData);
        }

        return SUCCESS;
    }

    int AggregateByNuma(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        map<unsigned, PmuDeviceData> devDataByNuma;
        for (auto &data : rawData) {
            auto findData = devDataByNuma.find(data.numaId);
            if (findData == devDataByNuma.end()) {
                PmuDeviceData outData;
                outData.metric = data.metric;
                outData.count = data.count;
                outData.numaId = data.numaId;
                devDataByNuma[data.numaId] = outData;
            } else {
                findData->second.count += data.count;
            }
        }

        for (auto &data : devDataByNuma) {
            devData.push_back(data.second);
        }

        return SUCCESS;
    }

    int PcieBWAggregate(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        auto findConfig = DEVICE_CONFIGS.find(metric);
        if (findConfig == DEVICE_CONFIGS.end()) {
            return SUCCESS;
        }
        auto &evts = findConfig->second.events;
        if (evts.size() != 2) {
            return SUCCESS;
        }
        string packLenEvt = evts[0];
        string latEvt = evts[1];

        unordered_map<string, map<string, InnerDeviceData>> devDataByBdf;
        for (auto &data : rawData) {
            string devName;
            string evtName;
            if (!GetDeviceName(data.evtName, devName, evtName)) {
                continue;
            }
            auto evtConfig = ExtractEvtStr("config", evtName);
            devDataByBdf[data.bdf][evtConfig] = data;
        }

        for (auto &data : devDataByBdf) {
            auto findLenData = data.second.find(packLenEvt);
            auto findLatData = data.second.find(latEvt);
            if (findLenData == data.second.end() || findLatData == data.second.end()) {
                continue;
            }
            int bw = 4 * findLenData->second.count / findLatData->second.count;
            PmuDeviceData outData;
            outData.metric = metric;
            outData.count = bw;
            outData.bdf = findLenData->second.bdf;
            devData.push_back(outData);
        }
        return SUCCESS;
    }

    int SmmuTransAggregate(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        unordered_map<string, vector<InnerDeviceData>> devDataByBdf;
        for (auto &data : rawData) {
            devDataByBdf[data.bdf].push_back(data);
        }

        for (auto &data : devDataByBdf) {
            PmuDeviceData outData;
            outData.metric = metric;
            outData.bdf = data.second[0].bdf;
            outData.count = 0;
            for (auto bdfData : data.second) {
                outData.count += bdfData.count;
            }
            devData.push_back(outData);
        }
        return SUCCESS;
    }

    typedef bool (*IsDevEventCb)(const string &devName, const string &evtName, const PmuDeviceAttr &devAttr);
    typedef uint64_t (*ComputeMetricCb)(const uint64_t rawCount);
    typedef int (*AggregateMetricCb)(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData);

    unordered_map<PmuDeviceMetric, ComputeMetricCb> computeMetricMap = {{PMU_DDR_READ_BW, DDRBw},
                                                                        {PMU_DDR_WRITE_BW, DDRBw},
                                                                        {PMU_L3_TRAFFIC, L3Bw}};
    unordered_set<PmuDeviceMetric> percoreMetric = {PMU_L3_TRAFFIC, PMU_L3_MISS, PMU_L3_REF};
    unordered_set<PmuDeviceMetric> pernumaMetric = {PMU_DDR_READ_BW, PMU_DDR_WRITE_BW, PMU_L3_LAT};
    unordered_set<PmuDeviceMetric> perpcieMetric = {PMU_PCIE_RX_MRD_BW,
                                                    PMU_PCIE_RX_MWR_BW,
                                                    PMU_PCIE_TX_MRD_BW,
                                                    PMU_PCIE_TX_MWR_BW,
                                                    PMU_SMMU_TRAN};
    unordered_map<PmuDeviceMetric, AggregateMetricCb> aggregateMap = {
        {PMU_DDR_READ_BW, AggregateByNuma},
        {PMU_DDR_WRITE_BW, AggregateByNuma},
        {PMU_L3_LAT, AggregateByNuma},
        {PMU_PCIE_RX_MRD_BW, PcieBWAggregate},
        {PMU_PCIE_RX_MWR_BW, PcieBWAggregate},
        {PMU_PCIE_TX_MRD_BW, PcieBWAggregate},
        {PMU_PCIE_TX_MWR_BW, PcieBWAggregate},
        {PMU_SMMU_TRAN, SmmuTransAggregate},
    };

    static string BDFtoHex(const char *bdf)
    {
        // TODO: implement
        return "";
    }

    static bool IsMetricEvent(const string &devName, const string &evtName, const PmuDeviceAttr &devAttr)
    {
        auto findDevConfig = DEVICE_CONFIGS.find(devAttr.metric);
        if (findDevConfig == DEVICE_CONFIGS.end()) {
            return false;
        }

        auto &devConfig = findDevConfig->second;
        if (devName.find(devConfig.devicePrefix) == string::npos ||
            devName.find(devConfig.subDeviceName) == string::npos) {
            return false;
        }

        auto configStr = ExtractEvtStr("config", evtName);
        if (configStr == "") {
            return false;
        }

        if (perpcieMetric.find(devAttr.metric) != perpcieMetric.end()) {
            auto bdfStr = ExtractEvtStr("bdf", evtName);
            auto expect = BDFtoHex(devAttr.bdf);
            if (bdfStr != expect) {
                return false;
            }
        }

        for (auto &evt : devConfig.events) {
            if (configStr == evt) {
                return true;
            }
        }

        return false;
    }

    static int GetDevMetric(const PmuData *pmuData, const unsigned len,
                            const PmuDeviceAttr &devAttr, MetricMap &metricMap)
    {
        for (unsigned i = 0; i < len; ++i) {
            string evt = pmuData[i].evt;
            string devName;
            string evtName;
            if (!GetDeviceName(evt, devName, evtName)){
                continue;
            }
            if (!IsMetricEvent(devName, evtName, devAttr)) {
                continue;
            }

            InnerDeviceData devData;
            devData.evtName = pmuData[i].evt;
            devData.metric = devAttr.metric;
            if (computeMetricMap.find(devAttr.metric) != computeMetricMap.end()) {
                devData.count = computeMetricMap[devAttr.metric](pmuData[i].count);
            } else {
                devData.count = pmuData[i].count;
            }
            if (percoreMetric.find(devAttr.metric) != percoreMetric.end()) {
                devData.coreId = pmuData[i].cpu;
            }
            if (pernumaMetric.find(devAttr.metric) != pernumaMetric.end()) {
                devData.numaId = pmuData[i].cpuTopo->numaId;
            }
            if (perpcieMetric.find(devAttr.metric) != pernumaMetric.end()) {
                devData.bdf = devAttr.bdf;
            }
            metricMap[devData.metric].push_back(devData);
        }
        return SUCCESS;
    }
}

using namespace KUNPENG_PMU;

int PmuGetDevMetric(struct PmuData *pmuData, unsigned len,
                    struct PmuDeviceAttr *attr, unsigned attrLen,
                    struct PmuDeviceData **data)
{
    MetricMap metricMap;
    for (unsigned i = 0; i < attrLen; ++i) {
        int ret = GetDevMetric(pmuData, len, attr[i], metricMap);
        if (ret != SUCCESS) {
            New(ret);
            return -1;
        }
    }

    vector<PmuDeviceData> devData;
    for (auto &metricData : metricMap) {
        auto findAggregate = aggregateMap.find(metricData.first);
        int ret;
        if (findAggregate == aggregateMap.end()) {
            ret = DefaultAggregate(metricData.second, devData);
        }
        else {
            ret = findAggregate->second(metricData.first, metricData.second, devData);
        }
        if (ret != SUCCESS) {
            New(ret);
            return -1;
        }
    }

    auto dataPtr = devData.data();
    int retLen = devData.size();
    deviceDataMap[dataPtr] = move(devData);
    *data = dataPtr;
    New(SUCCESS);
    return retLen;
}

void DevDataFree(struct PmuDeviceData *data)
{
    SetWarn(SUCCESS);
    if (deviceDataMap.find(data) != deviceDataMap.end())
    {
        deviceDataMap.erase(data);
    }
    New(SUCCESS);
}