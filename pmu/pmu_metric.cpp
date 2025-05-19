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
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <numa.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/perf_event.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>
#include <fstream>
#include <mutex>
#include "common.h"
#include "uncore.h"
#include "cpu_map.h"
#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"
#include "pcerr.h"

using namespace std;
using namespace pcerr;

static unsigned maxCpuNum = 0;
static vector<unsigned> coreArray;

static std::mutex pmuBdfListMtx;
static std::mutex pmuCoreListMtx;
static std::mutex pmuDeviceDataMtx;

static const string SYS_DEVICES = "/sys/devices/";
static const string SYS_BUS_PCI_DEVICES = "/sys/bus/pci/devices";
static const string SYS_IOMMU_DEVICES = "/sys/class/iommu";
static const string SYS_CPU_INFO_PATH = "/sys/devices/system/cpu/cpu";
static vector<const char*> supportedDevicePrefixes = {"hisi", "smmuv3", "hns3", "armv8"};
// all uncore raw pmu device name list
static vector<string> uncoreRawDeviceList;
// valid watch pcie bdf list
static vector<const char*> pcieBdfList;
// key: valid bdf value:hisi_pcieX_coreX eg: 01:02.0 <-> hisi_pcie0_core0
static unordered_map<string, string> bdfToPcieMap;
// valid watch smmu bdf list
static vector<const char*> smmuBdfList;
// key: valid smmu bdf value:smmuv3_pmcg_<phys_addr_page> eg: 04:00:0 <-> smmuv3_pmcg_148020
static unordered_map<string, string> bdfToSmmuPmuMap;

namespace KUNPENG_PMU {
    struct UncoreDeviceConfig {
        string devicePrefix;
        string subDeviceName;
        vector<string> events;
        string extraConfig;
        string bdfParameter;
        unsigned splitPosition;
    };

    static const std::map<PmuDeviceMetric, std::string> MetricToString = {
        {PmuDeviceMetric::PMU_DDR_READ_BW, "PMU_DDR_READ_BW"},
        {PmuDeviceMetric::PMU_DDR_WRITE_BW, "PMU_DDR_WRITE_BW"},
        {PmuDeviceMetric::PMU_L3_TRAFFIC, "PMU_L3_TRAFFIC"},
        {PmuDeviceMetric::PMU_L3_MISS, "PMU_L3_MISS"},
        {PmuDeviceMetric::PMU_L3_REF, "PMU_L3_REF"},
        {PmuDeviceMetric::PMU_L3_LAT, "PMU_L3_LAT"},
        {PmuDeviceMetric::PMU_PCIE_RX_MRD_BW, "PMU_PCIE_RX_MRD_BW"},
        {PmuDeviceMetric::PMU_PCIE_RX_MWR_BW, "PMU_PCIE_RX_MWR_BW"},
        {PmuDeviceMetric::PMU_PCIE_TX_MRD_BW, "PMU_PCIE_TX_MRD_BW"},
        {PmuDeviceMetric::PMU_PCIE_TX_MWR_BW, "PMU_PCIE_TX_MWR_BW"},
        {PmuDeviceMetric::PMU_SMMU_TRAN, "PMU_SMMU_TRAN"}
    };

    set<PmuDeviceMetric> percoreMetric = {PMU_L3_TRAFFIC, PMU_L3_MISS, PMU_L3_REF};
    set<PmuDeviceMetric> pernumaMetric = {PMU_L3_LAT};
    set<PmuDeviceMetric> perClusterMetric = {PMU_L3_LAT};
    set<PmuDeviceMetric> perChannelMetric = {PMU_DDR_READ_BW, PMU_DDR_WRITE_BW};
    set<PmuDeviceMetric> perpcieMetric = {PMU_PCIE_RX_MRD_BW,
                                                    PMU_PCIE_RX_MWR_BW,
                                                    PMU_PCIE_TX_MRD_BW,
                                                    PMU_PCIE_TX_MWR_BW,
                                                    PMU_SMMU_TRAN};

    static bool IsBdfMetric(PmuDeviceMetric metric)
    {
        return perpcieMetric.find(metric) != perpcieMetric.end();
    }

    static std::string GetMetricString(PmuDeviceMetric metric)
    {
        auto it = MetricToString.find(metric);
        if (it != MetricToString.end()) {
            return it->second;
        }
        return "<Unknown Metric>";
    }

    using PMU_METRIC_PAIR = std::pair<PmuDeviceMetric, UncoreDeviceConfig>;
    using UNCORE_METRIC_MAP = std::unordered_map<int, const map<PmuDeviceMetric, UncoreDeviceConfig>&>;
    namespace METRIC_CONFIG {
        PMU_METRIC_PAIR DDR_READ_BW_A = {
            PmuDeviceMetric::PMU_DDR_READ_BW,
            {
                "hisi_sccl",
                "ddrc",
                {"0x1"},
                "",
                "",
                1
            }
        };

        PMU_METRIC_PAIR DDR_WRITE_BW_A = {
            PmuDeviceMetric::PMU_DDR_WRITE_BW,
            {
                "hisi_sccl",
                "ddrc",
                {"0x0"},
                "",
                "",
                1
            }
        };

        PMU_METRIC_PAIR DDR_READ_BW_B = {
            PmuDeviceMetric::PMU_DDR_READ_BW,
            {
                "hisi_sccl",
                "ddrc",
                {"0x84"},
                "",
                "",
                1
            }
        };

        PMU_METRIC_PAIR DDR_WRITE_BW_B = {
            PmuDeviceMetric::PMU_DDR_WRITE_BW,
            {
                "hisi_sccl",
                "ddrc",
                {"0x83"},
                "",
                "",
                1
            }
        };

        PMU_METRIC_PAIR L3_TRAFFIC = {
            PmuDeviceMetric::PMU_L3_TRAFFIC,
            {
                "armv8_pmu",
                "",
                {"0x0032"},
                "",
                "",
                0
            }
        };

        PMU_METRIC_PAIR L3_MISS = {
            PmuDeviceMetric::PMU_L3_MISS,
            {
                "armv8_pmu",
                "",
                {"0x0033"},
                "",
                "",
                0
            }
        };

        PMU_METRIC_PAIR L3_REF = {
            PmuDeviceMetric::PMU_L3_REF,
            {
                "armv8_pmu",
                "",
                {"0x0032"},
                "",
                "",
                0
            }
        };

        PMU_METRIC_PAIR L3_LAT = {
            PmuDeviceMetric::PMU_L3_LAT,
            {
                "hisi_sccl",
                "l3c",
                {"0x80", "0xb8", "0xce"},
                "tt_core=0xff",
                "",
                1
            }
        };

        PMU_METRIC_PAIR PCIE_RX_MRD_BW = {
            PmuDeviceMetric::PMU_PCIE_RX_MRD_BW,
            {
                "hisi_pcie",
                "core",
                {"0x0804", "0x10804"},
                "",
                "bdf=",
                1
            }
        };

        PMU_METRIC_PAIR PCIE_RX_MWR_BW = {
            PmuDeviceMetric::PMU_PCIE_RX_MWR_BW,
            {
                "hisi_pcie",
                "core",
                {"0x0104", "0x10104"},
                "",
                "bdf=",
                1
            }
        };

        PMU_METRIC_PAIR PCIE_TX_MRD_BW = {
            PmuDeviceMetric::PMU_PCIE_TX_MRD_BW,
            {
                "hisi_pcie",
                "core",
                {"0x0405", "0x10405"},
                "",
                "bdf=",
                1
            }
        };

        PMU_METRIC_PAIR PCIE_TX_MWR_BW = {
            PmuDeviceMetric::PMU_PCIE_TX_MWR_BW,
            {
                "hisi_pcie",
                "core",
                {"0x0105", "0x10105"},
                "",
                "bdf=",
                1
            }
        };

        PMU_METRIC_PAIR SMMU_TRAN = {
            PmuDeviceMetric::PMU_SMMU_TRAN,
            {
                "smmuv3_pmcg",
                "",
                {"0x1"},
                "filter_enable=1",
                "filter_stream_id=",
                2
            }
        };
    }

    static const map<PmuDeviceMetric, UncoreDeviceConfig> HIP_A_UNCORE_METRIC_MAP {
        METRIC_CONFIG::DDR_READ_BW_A,
        METRIC_CONFIG::DDR_WRITE_BW_A,
        METRIC_CONFIG::L3_TRAFFIC,
        METRIC_CONFIG::L3_MISS,
        METRIC_CONFIG::L3_REF,
        METRIC_CONFIG::SMMU_TRAN,
    };

    static const map<PmuDeviceMetric, UncoreDeviceConfig> HIP_B_UNCORE_METRIC_MAP {
        METRIC_CONFIG::DDR_READ_BW_B,
        METRIC_CONFIG::DDR_WRITE_BW_B,
        METRIC_CONFIG::L3_TRAFFIC,
        METRIC_CONFIG::L3_MISS,
        METRIC_CONFIG::L3_REF,
        METRIC_CONFIG::L3_LAT,
        METRIC_CONFIG::PCIE_RX_MRD_BW,
        METRIC_CONFIG::PCIE_RX_MWR_BW,
        METRIC_CONFIG::PCIE_TX_MRD_BW,
        METRIC_CONFIG::PCIE_TX_MWR_BW,
        METRIC_CONFIG::SMMU_TRAN,
    };

    const UNCORE_METRIC_MAP UNCORE_METRIC_CONFIG_MAP = {
        {CHIP_TYPE::HIPA, HIP_A_UNCORE_METRIC_MAP},
        {CHIP_TYPE::HIPB, HIP_B_UNCORE_METRIC_MAP},
    };

    static const map<PmuDeviceMetric, UncoreDeviceConfig> GetDeviceMtricConfig()
    {
        CHIP_TYPE chipType = GetCpuType();
        if (UNCORE_METRIC_CONFIG_MAP.find(chipType) == UNCORE_METRIC_CONFIG_MAP.end()) {
            return {}; 
        }
        return UNCORE_METRIC_CONFIG_MAP.at(chipType);
    }

    static int QueryUncoreRawDevices()
    {
        if (!uncoreRawDeviceList.empty()) {
            return SUCCESS;
        }
        if (!ExistPath(SYS_DEVICES) || !IsDirectory(SYS_DEVICES)) {
            New(LIBPERF_ERR_QUERY_EVENT_LIST_FAILED, "Query uncore evtlist falied!");
            return LIBPERF_ERR_QUERY_EVENT_LIST_FAILED;
        }
        vector<string> entries = ListDirectoryEntries(SYS_DEVICES);
        for (const auto& entry : entries) {
            for (auto devPrefix : supportedDevicePrefixes) {
                if (entry.find(devPrefix) == 0) {
                    uncoreRawDeviceList.emplace_back(entry);
                    break;
                }
            }
        }
        New(SUCCESS);
        return SUCCESS;
    }

    // build map: bdf number -> smmuv3.0x<phy_addr> eg: 04:00:0 <-> smmuv3.0x0000000148000000
    static int QueryBdfToSmmuNameMap(unordered_map<string, string>& bdfToSmmuMap)
    {
        if (!bdfToSmmuMap.empty()) {
            return SUCCESS;
        }
        if (!ExistPath(SYS_IOMMU_DEVICES) || !IsDirectory(SYS_IOMMU_DEVICES)) {
            cerr << "Directory does not exist or is not a directory: " << SYS_IOMMU_DEVICES << endl;
            New(LIBPERF_ERR_INVALID_IOSMMU_DIR, "Directory does not exist or is not a directory: " + SYS_IOMMU_DEVICES);
            return LIBPERF_ERR_INVALID_IOSMMU_DIR;
        }
        vector<string> entries = ListDirectoryEntries(SYS_IOMMU_DEVICES);
        if (entries.empty()) {
            New(LIBPERF_ERR_INVALID_IOSMMU_DIR, "No bdf device found in the directory: " + SYS_IOMMU_DEVICES);
            return LIBPERF_ERR_INVALID_IOSMMU_DIR;
        }
        for (const auto& entry : entries) {
            string devicesPath = SYS_IOMMU_DEVICES + "/" + entry + "/devices";
            if (!ExistPath(devicesPath) || !IsDirectory(devicesPath)) {
                New(LIBPERF_ERR_INVALID_IOSMMU_DIR, "Directory does not exist or is not a directory: " + devicesPath);
                return LIBPERF_ERR_INVALID_IOSMMU_DIR;
            }
            vector<string> bdfEntries = ListDirectoryEntries(devicesPath);
            for (const auto& bdfEntry : bdfEntries) {
                if (IsDirectory(devicesPath + "/" + bdfEntry)) {
                    string bdfStr = bdfEntry;
                    if (bdfStr.find("0000:") != string::npos) {
                        string bdfValue = bdfStr.substr(bdfStr.find("0000:") + strlen("0000:"));
                        bdfToSmmuMap[bdfValue] = entry;
                    }
                }
            }
        }
        New(SUCCESS);
        return SUCCESS;
    }

    static int ConvertBdfStringToValue(const string& bdfStr, uint16_t& bdfValue)
    {
        vector<string> busDeviceFunction = SplitStringByDelimiter(bdfStr, ':');
        vector<string> deviceFunction = SplitStringByDelimiter(busDeviceFunction[1], '.');
        if (busDeviceFunction.size() != 2 || deviceFunction.size() != 2) {
            New(LIBPERF_ERR_INVALID_BDF_VALUE, "bdf value is invalid, shoubld be like 00:00.0");
            return LIBPERF_ERR_INVALID_BDF_VALUE;
        }
        uint16_t bus = 0;
        uint16_t device = 0;
        uint16_t function = 0;
        try {
            bus = static_cast<uint16_t>(stoul(busDeviceFunction[0], nullptr, 16));
            device = static_cast<uint16_t>(stoul(deviceFunction[0], nullptr, 16));
            function = static_cast<uint16_t>(stoul(deviceFunction[1], nullptr, 16));
        } catch (const std::invalid_argument& e) {
            New(LIBPERF_ERR_INVALID_BDF_VALUE, "bdf value is invalid, shoubld be like 00:00.0");
            return LIBPERF_ERR_INVALID_BDF_VALUE;
        }
        if (bus > 0xFF || device > 0x1F || function > 0x7) {
            New(LIBPERF_ERR_INVALID_BDF_VALUE, "bdf value is invalid, shoubld be like 00:00.0");
            return LIBPERF_ERR_INVALID_BDF_VALUE;
        }
        bdfValue = (static_cast<uint16_t>(bus) << 8) | (static_cast<uint16_t>(device) << 3) | function;
        return SUCCESS;
    }

    // convert smmu name to smmu pmu key. example: smmu3.0x0000000100100000 -> 100120
    static int ConvertSmmuToDeviceAddress(const string& smmuDeviceName, string& smmuPmuKey)
    {
        const string prefix = "smmu3.0x";
        const uint64_t PMU_OFFSET = 0x20000;
        size_t pos = smmuDeviceName.find(prefix);
        string hexAddressStr = smmuDeviceName.substr(prefix.length());
        uint64_t physicalBaseAddress = 0;
        try {
            physicalBaseAddress = stoul(hexAddressStr, nullptr, 16);
        } catch (const std::invalid_argument& e) {
            New(LIBPERF_ERR_INVALID_SMMU_NAME, "invalid smmu device name");
            return LIBPERF_ERR_INVALID_SMMU_NAME;
        }
        uint64_t pmuPhysicalAddress = physicalBaseAddress + PMU_OFFSET;
        uint64_t pmuSuffix = pmuPhysicalAddress >> 12;
        std::stringstream result;
        result << hex << uppercase << pmuSuffix;
        smmuPmuKey = result.str();
        return SUCCESS;
    }

    void FindAllSubBdfDevice(std::string bdfPath, std::string bus, unordered_map<string, string>& pcieBdfToBus)
    {
        vector<string> entries = ListDirectoryEntries(bdfPath);
        for (auto& entry : entries) {
            if (entry.find("0000:") != string::npos) {
                string bdfNumber = entry.substr(strlen("0000:"));
                pcieBdfToBus[bdfNumber] = bus;
                FindAllSubBdfDevice(bdfPath + "/" + entry, bus, pcieBdfToBus);
            }
        }
        return;
    }
    
    // build map: EP bdf number -> bus
    static int GeneratePcieBusToBdfMap(unordered_map<string, string>& pcieBdfToBus)
    {
        if (!ExistPath(SYS_DEVICES) || !IsDirectory(SYS_DEVICES)) {
            New(LIBPERF_ERR_QUERY_EVENT_LIST_FAILED, "Query uncore evtlist falied!");
            return LIBPERF_ERR_QUERY_EVENT_LIST_FAILED;
        }
        vector<string> entries = ListDirectoryEntries(SYS_DEVICES);
        for (const auto& entry : entries) {
            if (entry.find("pci0000:") == 0) {
                string bus = entry.substr(strlen("pci0000:"));
                FindAllSubBdfDevice(SYS_DEVICES + "/" + entry, bus, pcieBdfToBus);
            }
        }
        return SUCCESS;
    }

    static std::unordered_map<string, vector<string>> ClassifyDevicesByPrefix(const string& devicePrefix,
        const string& subDeviceName, unsigned splitPos)
    {
        std::unordered_map<string, vector<string>> classifiedDevices;
        for (const auto& device : uncoreRawDeviceList) {
            if (device.find(devicePrefix) != string::npos && device.find(subDeviceName) != string::npos) {
                if (splitPos == 0) {
                    classifiedDevices[device].push_back(device);
                    continue;
                }
                vector<string> splitParts = SplitStringByDelimiter(device, '_');
                if (splitParts.size() > splitPos) {
                    classifiedDevices[splitParts[splitPos]].push_back(device);
                }
            }
        }
        New(SUCCESS);
        return classifiedDevices;
    }

    // build map: hisi_pcieX_coreX -> <bus, bdfmin, bdfmax>
    static int QueryPcieBdfRanges(unordered_map<string, std::tuple<string, int, int>>& pciePmuBdfRang)
    {
        if (!pciePmuBdfRang.empty()) {
            return SUCCESS;
        }
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& pcieConfig = deviceConfig.at(PmuDeviceMetric::PMU_PCIE_RX_MRD_BW);
        int err = QueryUncoreRawDevices();
        if (err != SUCCESS) {
            return err;
        }
        auto classifiedDevices = ClassifyDevicesByPrefix(pcieConfig.devicePrefix, pcieConfig.subDeviceName, pcieConfig.splitPosition);
        if (classifiedDevices.empty()) {
            New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "No pcie pmu device is not exist in the " + SYS_DEVICES);
            return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
        }
        for (const auto& device : classifiedDevices) {
            const auto& pcieNuma = device.first;
            const auto& pciePmus = device.second;
            for (auto& pciePmu : pciePmus) {
                string bdfBusPath = SYS_DEVICES + "/" + pciePmu + "/bus";
                if (!ExistPath(bdfBusPath)) {
                    New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "pcie pmu bus file is empty");
                    return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
                }
                string bdfMinPath = SYS_DEVICES + "/" + pciePmu + "/bdf_min";
                string bdfMaxPath = SYS_DEVICES + "/" + pciePmu + "/bdf_max";

                string bdfBusStr = ReadFileContent(bdfBusPath);
                int bus = 0;
                try {
                    bus = stoul(bdfBusStr, nullptr, 16);
                } catch (const std::exception& e) {
                    New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "pcie pmu bus file is invalid");
                    return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
                }
                if (bus == 0) {
                    continue;
                }
                
                if (!ExistPath(bdfMinPath) || !ExistPath(bdfMaxPath)) {
                    SetWarn(LIBPERF_WARN_PCIE_BIOS_NOT_NEWEST, "pcie bios possible is the newest version");
                    pciePmuBdfRang[pciePmu] = std::make_tuple(bdfBusStr.substr(strlen("0x")), -1, -1);
                    continue;
                }
                string bdfMinStr = ReadFileContent(bdfMinPath);
                string bdfMaxStr = ReadFileContent(bdfMaxPath);
                if (bdfMinStr.empty() || bdfMaxStr.empty()) {
                    New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "pcie pmu bdfMin or bdfMax file is empty");
                    return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
                }
                int bdfMin = 0;
                int bdfMax = 0;
                try {
                    bdfMin = stoul(bdfMinStr, nullptr, 16);
                    bdfMax = stoul(bdfMaxStr, nullptr, 16);
                } catch (const std::exception& e) {
                    New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "pcie pmu bdfMin or bdfMax file is invalid");
                    return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
                }
                pciePmuBdfRang[pciePmu] = std::make_tuple(bdfBusStr.substr(strlen("0x")), bdfMin, bdfMax);
            }
        }
        New(SUCCESS);
        return SUCCESS;
    }

    static int QueryAllBdfList(vector<string>& bdfList)
    {
        if (!ExistPath(SYS_BUS_PCI_DEVICES) || !IsDirectory(SYS_BUS_PCI_DEVICES)) {
            New(LIBPERF_ERR_OPEN_PCI_FILE_FAILD, SYS_BUS_PCI_DEVICES + " is not exist.");
            return LIBPERF_ERR_OPEN_PCI_FILE_FAILD;
        }
        vector<string> entries = ListDirectoryEntries(SYS_BUS_PCI_DEVICES);
        for (const auto& entry : entries) {
            if (entry.size() > 5 && entry.substr(0, 5) == "0000:") {
                bdfList.push_back(entry.substr(5));
            }
        }
        New(SUCCESS);
        return SUCCESS;
    }

    static const char** PmuDevicePcieBdfList(unsigned *numBdf)
    {
        // fix repeat called List continue increase
        if (!pcieBdfList.empty()) {
            *numBdf = pcieBdfList.size();
            New(SUCCESS);
            return pcieBdfList.data();
        }
        vector<string> bdfList;
        if(QueryAllBdfList(bdfList) != SUCCESS) {
            *numBdf = 0;
            return nullptr;
        }

        unordered_map<string, string> pcieBdfToBus;
        if (GeneratePcieBusToBdfMap(pcieBdfToBus) != SUCCESS) {
            *numBdf = 0;
            return nullptr;
        }
        unordered_map<string, std::tuple<string, int, int>> pciePmuToBdfRang;
        if (QueryPcieBdfRanges(pciePmuToBdfRang) != SUCCESS) {
            *numBdf = 0;
            return nullptr;
        }

        for (int i = 0; i < bdfList.size(); i++) {
            auto it = pcieBdfToBus.find(bdfList[i]);
            if (it == pcieBdfToBus.end()) {
                continue;
            }

            const string bus = it->second;
            for (auto& pciePmu : pciePmuToBdfRang) {
                string pcieBus = std::get<0>(pciePmu.second);
                int bdfmin = std::get<1>(pciePmu.second);
                int bdfMax = std::get<2>(pciePmu.second);
                if (bdfmin == -1) {
                    if (bus == pcieBus) {
                        char* bdfCopy = new char[bdfList[i].size() + 1];
                        strcpy(bdfCopy, bdfList[i].c_str());
                        pcieBdfList.emplace_back(bdfCopy);
                        bdfToPcieMap[bdfList[i]] = pciePmu.first;
                    }
                } else {
                    uint16_t bdfValue = 0;
                    if (ConvertBdfStringToValue(bdfList[i], bdfValue) != SUCCESS) {
                        continue;
                    }
                    if (bus == pcieBus && bdfValue >= bdfmin && bdfValue <= bdfMax) {
                        char* bdfCopy = new char[bdfList[i].size() + 1];
                        strcpy(bdfCopy, bdfList[i].c_str());
                        pcieBdfList.emplace_back(bdfCopy);
                        bdfToPcieMap[bdfList[i]] = pciePmu.first;
                    }
                }
            }
        }

        *numBdf = pcieBdfList.size();
        New(SUCCESS);
        return pcieBdfList.data();
    }

    // convert smmu name to smmu pmu device name: smmu3.0x<phys_addr> -> smmuv3_pmcg_<phys_addr_page>
    // eg: smmu3.0x0000000148000000 <-> smmuv3_pmcg_148020
    static int FindSmmuToSmmuPmu(std::string& smmuName, std::string& smmuPmuName)
    {
        string smmuPmuKey = "";
        int err = ConvertSmmuToDeviceAddress(smmuName, smmuPmuKey);
        if (err != SUCCESS) {
            return err;
        }
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& config = deviceConfig.at(PmuDeviceMetric::PMU_SMMU_TRAN);
        err = QueryUncoreRawDevices();
        if (err != SUCCESS) {
            return err;
        }
        auto classifiedDevices = ClassifyDevicesByPrefix(config.devicePrefix, config.subDeviceName, config.splitPosition);
        const auto smmu = classifiedDevices.find(smmuPmuKey);
        if (smmu == classifiedDevices.end()) {
            SetWarn(LIBPERF_WARN_INVALID_SMMU_BDF, "BDF Value not manage in any SMMU Directory.");
            return LIBPERF_WARN_INVALID_SMMU_BDF;
        }
        smmuPmuName = smmu->second.front();
        New(SUCCESS);
        return SUCCESS;
    }

    const char** PmuDeviceSmmuBdfList(unsigned *numBdf)
    {
        // fix repeat called List continue increase
        if (!smmuBdfList.empty()) {
            *numBdf = smmuBdfList.size();
            New(SUCCESS);
            return smmuBdfList.data();
        }
        vector<string> bdfList;
        if(QueryAllBdfList(bdfList) != SUCCESS) {
            *numBdf = 0;
            return nullptr;
        }
        unordered_map<string, string> bdfToSmmuMap;
        if (QueryBdfToSmmuNameMap(bdfToSmmuMap) != SUCCESS) {
            *numBdf = 0;
            return nullptr;
        }
        for (int i = 0; i < bdfList.size(); i++) {
            const auto& smmuName = bdfToSmmuMap.find(bdfList[i]);
            if (smmuName != bdfToSmmuMap.end()) {
                string smmuPmuName = "";
                if (FindSmmuToSmmuPmu(smmuName->second, smmuPmuName) == SUCCESS) {
                    smmuBdfList.emplace_back(strdup(bdfList[i].c_str()));
                    bdfToSmmuPmuMap[bdfList[i]] = smmuPmuName;
                }
            }
        }
        *numBdf = smmuBdfList.size();
        New(SUCCESS);
        return smmuBdfList.data();
    }

    static int FindSmmuDeviceByBdf(const string& bdf, string& smmuPmuName)
    {
        auto it = bdfToSmmuPmuMap.find(bdf);
        if (it == bdfToSmmuPmuMap.end()) {
            New(LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF, "BDF Value " + bdf + " not found in any SMMU Directory.");
            return LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF;
        }
        smmuPmuName = it->second;
        New(SUCCESS);
        return SUCCESS;
    }

    static int FindPcieDeviceByBdf(const string& bdf, string& pciePmuName)
    {
        auto bdfIt = bdfToPcieMap.find(bdf);
        if (bdfIt == bdfToPcieMap.end()) {
            New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF, "bdf value " + bdf + " is not managed by any PCIe Device.");
            return LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF;
        }
        pciePmuName = bdfIt->second;
        New(SUCCESS);
        return SUCCESS;
    }

    static vector<string> GenerateEventStrings(std::unordered_map<string, vector<string>> classifiedDevices,
        PmuDeviceAttr& deviceAttr, const UncoreDeviceConfig& metricConfig)
    {
        vector<string> eventList;
        if (IsBdfMetric(deviceAttr.metric)) {
            string bdf = deviceAttr.bdf;
            for (const auto& evt : metricConfig.events) {
                string device = "";
                if (metricConfig.bdfParameter == "bdf=") {
                    int err = FindPcieDeviceByBdf(bdf, device);
                    if (err != SUCCESS) {
                        return {};
                    }
                } else {
                    int err = FindSmmuDeviceByBdf(bdf, device);
                    if (err != SUCCESS) {
                        return {};
                    }
                }
                string eventString = device + "/config=" + evt;
                if (!metricConfig.extraConfig.empty()) {
                    eventString += "," + metricConfig.extraConfig;
                }
                if (!metricConfig.bdfParameter.empty() && !bdf.empty()) {
                    stringstream bdfValue;
                    uint16_t userBdf = 0;
                    ConvertBdfStringToValue(bdf, userBdf);
                    bdfValue << "0x" << hex << userBdf;
                    eventString += "," + metricConfig.bdfParameter + bdfValue.str();
                }
                eventString += "/";
                eventList.push_back(eventString);
            }
            New(SUCCESS);
            return eventList;
        }
        for (const auto& pair : classifiedDevices) {
            const auto& devices = pair.second;
            for (const auto& evt : metricConfig.events) {
                for (const auto& device : devices) {
                    string eventString = device + "/config=" + evt;
                    if (!metricConfig.extraConfig.empty()) {
                        eventString += "," + metricConfig.extraConfig;
                    }
                    eventString += "/";
                    eventList.push_back(eventString);
                }
            }
        }
        New(SUCCESS);
        return eventList;
    }

    static vector<string> GenerateEventList(struct PmuDeviceAttr& deviceAttr)
    {
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& config = deviceConfig.at(deviceAttr.metric);
        int err = QueryUncoreRawDevices();
        if (err != SUCCESS) {
            return {};
        }
        auto classifiedDevices = ClassifyDevicesByPrefix(config.devicePrefix, config.subDeviceName, config.splitPosition);
        return GenerateEventStrings(classifiedDevices, deviceAttr, config);
    }

    static int CheckDeviceMetricEnum(PmuDeviceMetric metric)
    {
        const auto& metricConfig = GetDeviceMtricConfig();
        if (metricConfig.empty()) {
            New(LIBPERF_ERR_NOT_SUPPORT_METRIC, "The current platform cpu does not support uncore metric collection.");
            return LIBPERF_ERR_NOT_SUPPORT_METRIC;
        }
        if (metricConfig.find(metric) == metricConfig.end()) {
            New(LIBPERF_ERR_INVALID_PMU_DEVICES_METRIC, "For this platform this metric " +
                GetMetricString(metric) + " is invalid value for PmuDeviceMetric!");
            return LIBPERF_ERR_INVALID_PMU_DEVICES_METRIC;
        }
        New(SUCCESS);
        return SUCCESS;
    }

    static bool CheckPcieBdf(char* bdf)
    {
        unsigned numBdf = 0;
        const char** pcieBdfList = nullptr;
        pcieBdfList = PmuDeviceBdfList(PmuBdfType::PMU_BDF_TYPE_PCIE, &numBdf);
        bool find = false;
        for (int i = 0; i < numBdf; ++i) {
            if (strcmp(pcieBdfList[i], bdf) == 0) {
                find = true;
                break;
            }
        }
        return find;
    }

    static bool CheckSmmuBdf(char* bdf)
    {
        unsigned numBdf = 0;
        const char** smmuBdfList = nullptr;
        smmuBdfList = PmuDeviceBdfList(PmuBdfType::PMU_BDF_TYPE_SMMU, &numBdf);
        bool find = false;
        for (int i = 0; i < numBdf; ++i) {
            if (strcmp(smmuBdfList[i], bdf) == 0) {
                find = true;
            }
        }
        return find;
    }

    static int CheckBdf(struct PmuDeviceAttr& deviceAttr)
    {
        if (IsBdfMetric(deviceAttr.metric) && deviceAttr.bdf == nullptr) {
            New(LIBPERF_ERR_INVALID_PMU_DEVICES_BDF, "When collecting pcie or smmu metric, bdf value can not is nullptr!");
            return LIBPERF_ERR_INVALID_PMU_DEVICES_BDF;
        }
        if (deviceAttr.metric >= PmuDeviceMetric::PMU_PCIE_RX_MRD_BW && deviceAttr.metric <= PmuDeviceMetric::PMU_PCIE_TX_MWR_BW
            && !CheckPcieBdf(deviceAttr.bdf)) {
            New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF, "this bdf not support pcie metric counting."
                " Please use PmuDeviceBdfList to query.");
            return LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF;
        }
        if (deviceAttr.metric == PmuDeviceMetric::PMU_SMMU_TRAN && !CheckSmmuBdf(deviceAttr.bdf)) {
            New(LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF, "this bdf not support smmu metric counting."
            " Please use PmuDeviceBdfList to query.");
            return LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF;
        }
        New(SUCCESS);
        return SUCCESS;
    }

    static int CheckPmuDeviceAttr(struct PmuDeviceAttr *attr, unsigned len)
    {
        if (attr == nullptr || len == 0) {
            New(LIBPERF_ERR_PMU_DEVICES_NULL, "PmuDeviceAttr or len is invalid.");
            return LIBPERF_ERR_PMU_DEVICES_NULL;
        }
        int err = 0;
        for (int i = 0; i < len; ++i) {
            err = CheckDeviceMetricEnum(attr[i].metric);
            if (err != SUCCESS) {
                return err;
            }
        }

        for (int i = 0; i < len; ++i) {
            err = CheckBdf(attr[i]);
            if (err != SUCCESS) {
                return err;
            }
        }
        
        return SUCCESS;
    }

    // remove duplicate device attribute
    static int RemoveDupDeviceAttr(struct PmuDeviceAttr *attr, unsigned len, std::vector<PmuDeviceAttr>& deviceAttr, bool l3ReDup)
    {
        std::unordered_set<std::string> uniqueSet;
        for (int i = 0; i < len; ++i) {
            std::string key = "";
            if (IsBdfMetric(attr[i].metric)) {
                key = std::to_string(attr[i].metric) + "_" + attr[i].bdf;
            } else {
                key = std::to_string(attr[i].metric);
            }

            if (uniqueSet.find(key) == uniqueSet.end()) {
                // when in deviceopen remove the same PMU_L3_TRAFFIC and PMU_L3_REF,
                // but when getDevMetric we need to keep them.
                if (l3ReDup == true &&
                    (attr[i].metric == PmuDeviceMetric::PMU_L3_TRAFFIC || attr[i].metric == PmuDeviceMetric::PMU_L3_REF)) {
                    if (uniqueSet.find(std::to_string(PmuDeviceMetric::PMU_L3_TRAFFIC)) != uniqueSet.end()) {
                        continue;
                    }
                    if (uniqueSet.find(std::to_string(PmuDeviceMetric::PMU_L3_REF)) != uniqueSet.end()) {
                        continue;
                    }
                }
                uniqueSet.insert(key);
                deviceAttr.emplace_back(attr[i]);
            }
        }
        return SUCCESS;
    }

    struct InnerDeviceData {
        enum PmuDeviceMetric metric;
        const char *evtName;
        uint64_t count;
        union {
            unsigned coreId;
            unsigned numaId;
            unsigned clusterId;
            char *bdf;
            struct {
                unsigned channelId;
                unsigned ddrNumaId;
                unsigned socketId;
            };
        };
    };

    using MetricMap = vector<std::pair<PmuDeviceMetric, vector<InnerDeviceData>>>;
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

    static PmuMetricMode GetMetricMode(const PmuDeviceMetric &metric)
    {
        switch(metric) {
            case PMU_DDR_READ_BW:
            case PMU_DDR_WRITE_BW:
                return PMU_METRIC_CHANNEL;
            case PMU_L3_LAT:
                return PMU_METRIC_CLUSTER;
            case PMU_L3_TRAFFIC:
            case PMU_L3_MISS:
            case PMU_L3_REF:
                return PMU_METRIC_CORE;
            case PMU_PCIE_RX_MRD_BW:
            case PMU_PCIE_RX_MWR_BW:
            case PMU_PCIE_TX_MRD_BW:
            case PMU_PCIE_TX_MWR_BW:
            case PMU_SMMU_TRAN:
                return PMU_METRIC_BDF;
        }
        return PMU_METRIC_INVALID;
    }

    int DefaultAggregate(const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        for (auto &data : rawData) {
            PmuDeviceData outData;
            outData.metric = data.metric;
            outData.count = data.count;
            outData.mode = GetMetricMode(data.metric);
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
                outData.mode = GetMetricMode(data.metric);
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

    static int HyperThreadEnabled(bool &enabled)
    {
        std::ifstream siblingFile("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list");
        if (!siblingFile.is_open()) {
            return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
        }
        std::string siblings;
        siblingFile >> siblings;
        enabled = siblings != "0";
        return SUCCESS;
    }

    int AggregateByCluster(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& findConfig = deviceConfig.find(metric);
        if (findConfig == deviceConfig.end()) {
            return SUCCESS;
        }
        auto &evts = findConfig->second.events;
        if (evts.size() != 3) {
            return SUCCESS;
        }
        // Event name for total latency.
        string latEvt = evts[0];
        // Event name for total access count.
        string refEvt = evts[1];
        // Event name for retry_alloc.
        string retryEvt = evts[2];

        // Sort data by cluster, and then sort by event string.
        map<unsigned, unordered_map<string, InnerDeviceData>> devDataByCluster;
        for (auto &data : rawData) {
            string devName;
            string evtName;
            if (!GetDeviceName(data.evtName, devName, evtName)) {
                continue;
            }
            auto evtConfig = ExtractEvtStr("config", evtName);
            auto findData = devDataByCluster.find(data.clusterId);
            if (findData == devDataByCluster.end()) {
                devDataByCluster[data.clusterId][evtConfig] = data;
            } else {
                devDataByCluster[data.clusterId][evtConfig].count += data.count;
            }
        }

        for (auto &data : devDataByCluster) {
            // Get events of total latency and total access count.
            auto findLatData = data.second.find(latEvt);
            auto findRefData = data.second.find(refEvt);
            auto findRetryData = data.second.find(retryEvt);
            if (findLatData == data.second.end() || findRefData == data.second.end() || findRetryData == data.second.end()) {
                continue;
            }
            // Compute avage latency: (latency)/(access count - retry_alloc)
            uint64_t res = findRefData->second.count - findRetryData->second.count;
            double lat = 0.0;
            if (res != 0) {
                lat = (double)(findLatData->second.count) / res;
            } else {
                lat = -1;
            }
            PmuDeviceData outData;
            outData.metric = metric;
            outData.count = lat;
            outData.mode = GetMetricMode(metric);
            outData.clusterId = data.first;
            devData.push_back(outData);
        }
        return SUCCESS;
    }

    static unordered_map<int, vector<int>> DDRC_CHANNEL_MAP = {
        {CHIP_TYPE::HIPA, {0, 1, 2, 3}},
        {CHIP_TYPE::HIPB, {0, 2, 3, 5}}
    };

    static bool getChannelId(const char *evt, const unsigned ddrNumaId, unsigned &channelId)
    {
        string devName;
        string evtName;
        if (!GetDeviceName(evt, devName, evtName)) {
            return false;
        }
        // ddrc channel index. eg: hisi_sccl3_ddrc3_1 --> 3_1
        string ddrcStr = "ddrc";
        size_t ddrcPos = devName.find(ddrcStr);
        size_t channelIndex = ddrcPos + ddrcStr.length();
        string ddrcIndexStr = devName.substr(channelIndex);
        // find index in DDRC_CHANNEL_MAP. eg: 3_1 --> 3, corresponds to channel 2 in HIPB
        size_t separatorPos = ddrcIndexStr.find("_");
        int ddrcIndex = separatorPos != string::npos ? stoi(ddrcIndexStr.substr(0, separatorPos)) : stoi(ddrcIndexStr);

        unsigned channelAddNum = 0;
        if((ddrNumaId & 1) == 1) {  // channel id + 4 in sequence
            channelAddNum = 4;
        }
        CHIP_TYPE chipType = GetCpuType();  //get channel index
        if (DDRC_CHANNEL_MAP.find(chipType) == DDRC_CHANNEL_MAP.end()) {
            return false;
        }
        auto ddrcChannelList = DDRC_CHANNEL_MAP[chipType];
        auto it = find(ddrcChannelList.begin(), ddrcChannelList.end(), ddrcIndex);
        if (it != ddrcChannelList.end()) {
            size_t index = distance(ddrcChannelList.begin(), it);
            channelId = index + channelAddNum;
            return true;
        }
        return false;
    }

    struct channelKeyHash {
        size_t operator()(const tuple<unsigned, unsigned, unsigned>& key) const {
            auto socketIdHash = hash<unsigned>{}(get<0>(key));
            auto channelIdHash = hash<unsigned>{}(get<1>(key));
            auto ddrNumaIdHash = hash<unsigned>{}(get<2>(key));
            return socketIdHash ^ (channelIdHash << 1) ^ (ddrNumaIdHash << 2);
        }
    };

    int AggregateByChannel(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        unordered_map<tuple<unsigned, unsigned, unsigned>, PmuDeviceData, channelKeyHash> devDataByChannel;  //Key: socketId, channelId, ddrNumaId
        for (auto &data : rawData) {
            unsigned channelId;
            if (!getChannelId(data.evtName, data.ddrNumaId, channelId)) {
                continue;
            }
            auto ddrDatakey = make_tuple(data.socketId, channelId, data.ddrNumaId);
            auto findData = devDataByChannel.find(ddrDatakey);
            if (findData == devDataByChannel.end()) {
                PmuDeviceData outData;
                outData.metric = data.metric;
                outData.count = data.count;
                outData.mode = GetMetricMode(data.metric);
                outData.channelId = channelId;
                outData.ddrNumaId = data.ddrNumaId;
                outData.socketId = data.ddrNumaId < 2 ? 0 : 1;  // numa id 0-1 --> socket id 0; numa id 2-3 --> socket id 1
                devDataByChannel[ddrDatakey] = outData;
            } else {
                findData->second.count += data.count;
            }
        }

        vector<pair<tuple<unsigned, unsigned, unsigned>, PmuDeviceData>> sortedVec(devDataByChannel.begin(), devDataByChannel.end());
        sort(sortedVec.begin(), sortedVec.end(), [](
            const pair<tuple<unsigned, unsigned, unsigned>, PmuDeviceData>& a,
            const pair<tuple<unsigned, unsigned, unsigned>, PmuDeviceData>& b) {
            return a.first < b.first;
        });
        for (auto &data : sortedVec) {
            devData.push_back(data.second);
        }

        return SUCCESS;
    }

    int PcieBWAggregate(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& findConfig = deviceConfig.find(metric);
        if (findConfig == deviceConfig.end()) {
            return SUCCESS;
        }
        auto &evts = findConfig->second.events;
        if (evts.size() != 2) {
            return SUCCESS;
        }
        // Event name for total packet length.
        string packLenEvt = evts[0];
        // Event name for total latency.
        string latEvt = evts[1];

        // Sort data by bdf, and then sort by event string.
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
            // Get events of total packet length and total latency.
            auto findLenData = data.second.find(packLenEvt);
            auto findLatData = data.second.find(latEvt);
            if (findLenData == data.second.end() || findLatData == data.second.end()) {
                continue;
            }
            // Compute bandwidth: (packet length)/(latency)
            double bw = 0.0;
            if (findLatData->second.count != 0) {
                bw = (double)(4 * findLenData->second.count) / findLatData->second.count;
            } else {
                bw = -1;
            }
            PmuDeviceData outData;
            outData.metric = metric;
            outData.count = bw;
            outData.mode = GetMetricMode(metric);
            outData.bdf = findLenData->second.bdf;
            devData.push_back(outData);
        }
        return SUCCESS;
    }

    int SmmuTransAggregate(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData)
    {
        // Sort data by bdf.
        unordered_map<string, vector<InnerDeviceData>> devDataByBdf;
        for (auto &data : rawData) {
            devDataByBdf[data.bdf].push_back(data);
        }

        // Sum all transactions from different smmu devices for each pcie device.
        for (auto &data : devDataByBdf) {
            PmuDeviceData outData;
            outData.metric = metric;
            outData.mode = GetMetricMode(metric);
            outData.bdf = data.second[0].bdf;
            outData.count = 0;
            for (auto bdfData : data.second) {
                outData.count += bdfData.count;
            }
            devData.push_back(outData);
        }
        return SUCCESS;
    }

    typedef uint64_t (*ComputeMetricCb)(const uint64_t rawCount);
    typedef int (*AggregateMetricCb)(const PmuDeviceMetric metric, const vector<InnerDeviceData> &rawData, vector<PmuDeviceData> &devData);

    map<PmuDeviceMetric, ComputeMetricCb> computeMetricMap = {{PMU_DDR_READ_BW, DDRBw},
                                                                {PMU_DDR_WRITE_BW, DDRBw},
                                                                {PMU_L3_TRAFFIC, L3Bw}};
    map<PmuDeviceMetric, AggregateMetricCb> aggregateMap = {
        {PMU_DDR_READ_BW, AggregateByChannel},
        {PMU_DDR_WRITE_BW, AggregateByChannel},
        {PMU_L3_LAT, AggregateByCluster},
        {PMU_PCIE_RX_MRD_BW, PcieBWAggregate},
        {PMU_PCIE_RX_MWR_BW, PcieBWAggregate},
        {PMU_PCIE_TX_MRD_BW, PcieBWAggregate},
        {PMU_PCIE_TX_MWR_BW, PcieBWAggregate},
        {PMU_SMMU_TRAN, SmmuTransAggregate},
    };

    static bool IsMetricEvent(const string &devName, const string &evtName, const PmuDeviceAttr &devAttr)
    {
        const auto& deviceConfig = GetDeviceMtricConfig();
        auto findDevConfig = deviceConfig.find(devAttr.metric);
        if (findDevConfig == deviceConfig.end()) {
            return false;
        }

        // Check device name.
        auto &devConfig = findDevConfig->second;
        if (devName.find(devConfig.devicePrefix) == string::npos ||
            devName.find(devConfig.subDeviceName) == string::npos) {
            return false;
        }

        // Extract config string.
        // For example, get '0x12' from 'config=0x12'.
        auto configStr = ExtractEvtStr("config", evtName);
        if (configStr == "") {
            return false;
        }

        // For pcie events, check if event is related with specifi bdf.
        if (IsBdfMetric(devAttr.metric)) {
            auto bdfStr = ExtractEvtStr("bdf", evtName);
            if (bdfStr.empty()) {
                bdfStr = ExtractEvtStr("filter_stream_id", evtName);
            }
            if (bdfStr.empty()) {
                return false;
            }
            uint16_t expectBdf;
            int ret = ConvertBdfStringToValue(devAttr.bdf, expectBdf);
            if (ret != SUCCESS) {
                return false;
            }
            stringstream bdfValue;
            bdfValue << "0x" << hex << expectBdf;
            if (bdfStr != bdfValue.str()) {
                return false;
            }
        }

        // Check if there is at least one event to match.
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
        std::vector<InnerDeviceData> devDataList;
        unsigned clusterWidth = 0;
        if (perClusterMetric.find(devAttr.metric) != perClusterMetric.end()) {
            bool hyperThreadEnabled = false;
            int err = HyperThreadEnabled(hyperThreadEnabled);
            if (err != SUCCESS) {
                New(err);
                return err;
            }
            clusterWidth = hyperThreadEnabled ? 8 : 4;
        }
        for (unsigned i = 0; i < len; ++i) {
            string evt = pmuData[i].evt;
            string devName;
            string evtName;
            // Get device name and event string.
            // For example, 'hisi_pcie0_core0/config=0x0804, bdf=0x70/',
            // devName is 'hisi_pcie0_core0' and evtName is 'config=0x0804, bdf=0x70'
            if (!GetDeviceName(evt, devName, evtName)){
                continue;
            }
            // Check if pmuData is related with current metric.
            if (!IsMetricEvent(devName, evtName, devAttr)) {
                continue;
            }

            InnerDeviceData devData;
            devData.evtName = pmuData[i].evt;
            devData.metric = devAttr.metric;
            if (computeMetricMap.find(devAttr.metric) != computeMetricMap.end()) {
                // Translate pmu count to meaningful value.
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
            if (perClusterMetric.find(devAttr.metric) != perClusterMetric.end()) {
                devData.clusterId = pmuData[i].cpuTopo->coreId / clusterWidth;
            }
            if (perChannelMetric.find(devAttr.metric) != pernumaMetric.end()) {
                devData.ddrNumaId = pmuData[i].cpuTopo->numaId;
                devData.socketId = pmuData[i].cpuTopo->socketId;
            }
            if (IsBdfMetric(devAttr.metric)) {
                devData.bdf = devAttr.bdf;
            }
            devDataList.emplace_back(devData);
        }
        metricMap.emplace_back(std::make_pair(devAttr.metric, move(devDataList)));
        return SUCCESS;
    }

}

using namespace KUNPENG_PMU;

const char** PmuDeviceBdfList(enum PmuBdfType bdfType, unsigned *numBdf)
{
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return nullptr;
#else 
    try {
        lock_guard<mutex> lg(pmuBdfListMtx);
        SetWarn(SUCCESS);
        int err = 0;
        if (bdfType == PmuBdfType::PMU_BDF_TYPE_PCIE) {
            err = CheckDeviceMetricEnum(PmuDeviceMetric::PMU_PCIE_RX_MRD_BW);
            if (err != SUCCESS) {
                *numBdf = 0;
                New(err, "For this platform not support pcie metric counting!");
                return nullptr;
            }
            return PmuDevicePcieBdfList(numBdf);
        }

        if (bdfType == PmuBdfType::PMU_BDF_TYPE_SMMU) {
            err = CheckDeviceMetricEnum(PmuDeviceMetric::PMU_SMMU_TRAN);
            if (err != SUCCESS) {
                *numBdf = 0;
                New(err, "For this platform not support smmu metric counting!");
                return nullptr;
            }
            return PmuDeviceSmmuBdfList(numBdf);
        }
        *numBdf = 0;
        New(LIBPERF_ERR_INVALID_PMU_BDF_TYPE, "bdfType is invalid.");
        return nullptr;
    } catch (exception &ex) {
        *numBdf = 0;
        New(UNKNOWN_ERROR, ex.what());
        return nullptr;
    }
#endif
}

static void PmuBdfListFreeSingle(vector<const char*> &bdfList)
{
    for (auto& bdf : bdfList) {
        if (bdf != NULL && bdf[0] != '\0') {
            delete[] bdf;
        }
    }
    bdfList.clear();
}

void PmuDeviceBdfListFree()
{
    lock_guard<mutex> lg(pmuBdfListMtx);
    PmuBdfListFreeSingle(pcieBdfList);
    PmuBdfListFreeSingle(smmuBdfList);
    New(SUCCESS);
}

int PmuDeviceOpen(struct PmuDeviceAttr *attr, unsigned len)
{
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return -1;
#else
    SetWarn(SUCCESS);
    try {
        if (CheckPmuDeviceAttr(attr, len) != SUCCESS) {
            return -1;
        }
        // Remove duplicate device attributes.
        vector<PmuDeviceAttr> deviceAttr;
        if (RemoveDupDeviceAttr(attr, len, deviceAttr, true) != SUCCESS) {
            return -1;
        }
        vector<string> configEvtList;
        for (int i = 0; i < deviceAttr.size(); ++i) {
            vector<string> temp = GenerateEventList(deviceAttr[i]);
            if (temp.empty()) {
                return -1;
            }
            configEvtList.insert(configEvtList.end(), temp.begin(), temp.end());
        }

        vector<char*> evts;
        for (auto& evt : configEvtList) {
            evts.push_back(const_cast<char*>(evt.c_str()));
        }

        PmuAttr attrConfig = {0};
        attrConfig.evtList = evts.data();
        attrConfig.numEvt = evts.size();
        int pd = PmuOpen(COUNTING, &attrConfig);
        return pd;
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
#endif
}

static int CheckPmuDeviceVar(struct PmuData *pmuData, unsigned len,
                      struct PmuDeviceAttr *attr, unsigned attrLen)
{
    if (pmuData == nullptr || attr == nullptr) {
        New(LIBPERF_ERR_INVALID_MTRIC_PARAM, "PmuData or PmuDeviceAttr is nullptr!");
        return LIBPERF_ERR_INVALID_MTRIC_PARAM;
    }

    if (len == 0 || attrLen == 0) {
        New(LIBPERF_ERR_INVALID_MTRIC_PARAM, "Input array length is 0!");
        return LIBPERF_ERR_INVALID_MTRIC_PARAM;
    }

    int err = CheckPmuDeviceAttr(attr, attrLen);
    if (err!= SUCCESS) {
        return err;
    }
    New(SUCCESS);
    return SUCCESS;
}

int PmuGetDevMetric(struct PmuData *pmuData, unsigned len,
                    struct PmuDeviceAttr *attr, unsigned attrLen,
                    struct PmuDeviceData **data)
{
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return -1;
#else 
    SetWarn(SUCCESS);
    try {
        if (CheckPmuDeviceVar(pmuData, len, attr, attrLen) != SUCCESS) {
            return -1;
        }
        // Remove duplicate device attributes.
        vector<PmuDeviceAttr> deviceAttr;
        if (RemoveDupDeviceAttr(attr, attrLen, deviceAttr, false) != SUCCESS) {
            return -1;
        }
        // Filter pmuData by metric and generate InnerDeviceData, 
        // which contains event name, core id, numa id and bdf.
        // InnerDeviceData will be used to aggregate data by core id, numa id or bdf.
        MetricMap metricMap;
        for (unsigned i = 0; i < deviceAttr.size(); ++i) {
            int ret = GetDevMetric(pmuData, len, deviceAttr[i], metricMap);
            if (ret != SUCCESS) {
                New(ret);
                return -1;
            }
        }

        // Aggregate each metric data by core id, numa id or bdf.
        vector<PmuDeviceData> devData;
        for (auto &metricData : metricMap) {
            auto findAggregate = aggregateMap.find(metricData.first);
            int ret;
            if (findAggregate == aggregateMap.end()) {
                // No aggregation and just copy InnerDeviceData to PmuDeviceData.
                ret = DefaultAggregate(metricData.second, devData);
            } else {
                ret = findAggregate->second(metricData.first, metricData.second, devData);
            }
            if (ret != SUCCESS) {
                New(ret);
                return -1;
            }
        }

        // Store pointer of vector and return to caller.
        auto dataPtr = devData.data();
        int retLen = devData.size();
        // Make relationship between raw pointer and vector, for DevDataFree.
        lock_guard<mutex> lg(pmuDeviceDataMtx);
        deviceDataMap[dataPtr] = move(devData);
        *data = dataPtr;
        New(SUCCESS);
        return retLen;
    } catch (exception& ex) {
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
#endif
}

void DevDataFree(struct PmuDeviceData *data)
{
    SetWarn(SUCCESS);
    lock_guard<mutex> lg(pmuDeviceDataMtx);
    if (deviceDataMap.find(data) != deviceDataMap.end()) {
        deviceDataMap.erase(data);
    }
    New(SUCCESS);
}

int64_t PmuGetCpuFreq(unsigned core)
{
    stringstream cpuPath;
    cpuPath << SYS_CPU_INFO_PATH << core << "/cpufreq/scaling_cur_freq";

    if (!ExistPath(cpuPath.str())) {
        New(LIBPERF_ERR_CPUFREQ_NOT_CONFIG, "Kernel not config cpuFreq Or core exceed cpuNums. Not exist " + cpuPath.str());
        return -1;
    }
    std::string curFreqStr = ReadFileContent(cpuPath.str());
    int64_t cpuFreq = 0;
    try {
        cpuFreq = stoi(curFreqStr);
    } catch (std::exception& e) {
        return -1;
    }
    New(SUCCESS);
    return cpuFreq * 1000;
}

static void InitializeCoreArray()
{
    if (!coreArray.empty()) {
        return;
    }
    maxCpuNum = sysconf(_SC_NPROCESSORS_CONF);
    for (unsigned i = 0; i < maxCpuNum; ++i) {
        coreArray.emplace_back(i);
    }
    return;
}

int PmuGetClusterCore(unsigned clusterId, unsigned **coreList)
{
#ifdef IS_X86
    New(LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86);
    return -1;
#else
    try {
        lock_guard<mutex> lg(pmuCoreListMtx);
        InitializeCoreArray();
        bool hyperThread = false;
        int err = HyperThreadEnabled(hyperThread);
        if (err != SUCCESS) {
            New(err);
            return -1;
        }
        int coreNums = hyperThread ? 8 : 4;
        unsigned startCore = clusterId * coreNums;

        if (startCore >= MAX_CPU_NUM) {
            New(LIBPERF_ERR_CLUSTER_ID_OVERSIZE, "clusterId exceed cpuNums.");
            return -1;
        }

        if (startCore + coreNums > MAX_CPU_NUM) {
            New(LIBPERF_ERR_CLUSTER_ID_OVERSIZE, "clusterId and coreNums exceed cpuNums.");
            return -1;
        }

        *coreList = &coreArray[startCore];

        New(SUCCESS);
        return coreNums;
    }
    catch (exception &ex) {
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
#endif
}

int PmuGetNumaCore(unsigned nodeId, unsigned **coreList)
{
    try {
        lock_guard<mutex> lg(pmuCoreListMtx);
        string nodeListFile = "/sys/devices/system/node/node" + to_string(nodeId) + "/cpulist";
        ifstream in(nodeListFile);
        if (!in.is_open()) {
            New(LIBPERF_ERR_KERNEL_NOT_SUPPORT);
            return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
        }
        std::string cpulist;
        in >> cpulist;
        auto split = SplitStringByDelimiter(cpulist, '-');
        if (split.size() != 2) {
            New(LIBPERF_ERR_KERNEL_NOT_SUPPORT);
            return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
        }
        auto start = stoi(split[0]);
        auto end = stoi(split[1]);
        int coreNums = end - start + 1;
        if (coreNums <= 0) {
            New(LIBPERF_ERR_KERNEL_NOT_SUPPORT);
            return LIBPERF_ERR_KERNEL_NOT_SUPPORT;
        }
        InitializeCoreArray();
        *coreList = &coreArray[start];

        New(SUCCESS);
        return coreNums;
    } catch (exception &ex) {
        New(UNKNOWN_ERROR, ex.what());
        return -1;
    }
}
