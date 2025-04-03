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
#include <string>
#include <numa.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/perf_event.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <fstream>
#include "common.h"
#include "uncore.h"
#include "cpu_map.h"
#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"
#include "pcerr.h"

using namespace std;
using namespace pcerr;

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

    static const std::unordered_map<PmuDeviceMetric, std::string> MetricToString = {
        {PmuDeviceMetric::PMU_DDR_READ_BW, "PMU_DDR_READ_BW"},
        {PmuDeviceMetric::PMU_DDR_WRITE_BW, "PMU_DDR_WRITE_BW"},
        {PmuDeviceMetric::PMU_L3_TRAFFIC, "PMU_L3_TRAFFIC"},
        {PmuDeviceMetric::PMU_L3_MISS, "PMU_L3_MISS"},
        {PmuDeviceMetric::PMU_L3_REF, "PMU_L3_REF"},
        {PmuDeviceMetric::PMU_L3_LAT, "PMU_L3_LAT"},
        {PmuDeviceMetric::PMU_PA2RING_ALL_BW, "PMU_PA2RING_ALL_BW"},
        {PmuDeviceMetric::PMU_RING2PA_ALL_BW, "PMU_RING2PA_ALL_BW"},
        {PmuDeviceMetric::PMU_PCIE_RX_MRD_BW, "PMU_PCIE_RX_MRD_BW"},
        {PmuDeviceMetric::PMU_PCIE_RX_MWR_BW, "PMU_PCIE_RX_MWR_BW"},
        {PmuDeviceMetric::PMU_PCIE_TX_MRD_BW, "PMU_PCIE_TX_MRD_BW"},
        {PmuDeviceMetric::PMU_PCIE_TX_MWR_BW, "PMU_PCIE_TX_MWR_BW"},
        {PmuDeviceMetric::PMU_SMMU_TRAN, "PMU_SMMU_TRAN"}
    };

    static std::string GetMetricString(PmuDeviceMetric metric)
    {
        auto it = MetricToString.find(metric);
        if (it != MetricToString.end()) {
            return it->second;
        }
        return "<Unknown Metrix>";
    }

    using PMU_METRIC_PAIR = std::pair<PmuDeviceMetric, UncoreDeviceConfig>;
    using UNCORE_METRIC_MAP = std::unordered_map<int, const unordered_map<PmuDeviceMetric, UncoreDeviceConfig>&>;
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
                {"0x80"},
                "",
                "",
                0
            }
        };

        PMU_METRIC_PAIR PA2RING_ALL_BW = {
            PmuDeviceMetric::PMU_PA2RING_ALL_BW,
            {
                "hisi_sicl",
                "_pa",
                {"0x60", "0x61", "0x62", "0x63"},
                "",
                "",
                1
            }
        };

        PMU_METRIC_PAIR RING2PA_ALL_BW = {
            PmuDeviceMetric::PMU_RING2PA_ALL_BW,
            {
                "hisi_sicl",
                "_pa",
                {"0x40", "0x41", "0x42", "0x43"},
                "",
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

    static const unordered_map<PmuDeviceMetric, UncoreDeviceConfig> HIP_A_UNCORE_METRIC_MAP {
        METRIC_CONFIG::DDR_READ_BW_A,
        METRIC_CONFIG::DDR_WRITE_BW_A,
        METRIC_CONFIG::L3_TRAFFIC,
        METRIC_CONFIG::L3_MISS,
        METRIC_CONFIG::L3_REF,
        METRIC_CONFIG::SMMU_TRAN,
    };

    static const unordered_map<PmuDeviceMetric, UncoreDeviceConfig> HIP_B_UNCORE_METRIC_MAP {
        METRIC_CONFIG::DDR_READ_BW_B,
        METRIC_CONFIG::DDR_WRITE_BW_B,
        METRIC_CONFIG::L3_TRAFFIC,
        METRIC_CONFIG::L3_MISS,
        METRIC_CONFIG::L3_REF,
        METRIC_CONFIG::L3_LAT,
        METRIC_CONFIG::PA2RING_ALL_BW,
        METRIC_CONFIG::RING2PA_ALL_BW,
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

    static const unordered_map<PmuDeviceMetric, UncoreDeviceConfig> GetDeviceMtricConfig()
    {
        return UNCORE_METRIC_CONFIG_MAP.at(GetCpuType());
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

    // build map: hisi_pcieX_coreX -> <bus, bdfmin, bdfmax>
    static int QueryPcieBdfRanges(unordered_map<string, std::tuple<string, int, int>>& pciePmuBdfRang)
    {
        if (!pciePmuBdfRang.empty()) {
            return SUCCESS;
        }
        if (!ExistPath(SYS_DEVICES) || !IsDirectory(SYS_DEVICES)) {
            cerr << "PCI devices directory not found: " << SYS_DEVICES << endl;
            New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF, "PCI devices directory not found: " + SYS_DEVICES);
            return LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF;
        }
        vector<string> entries = ListDirectoryEntries(SYS_DEVICES);
        for (const auto& entry : entries) {
            if (entry.find("pcie") == string::npos) {
                continue;
            }
            string bdfBusPath = SYS_DEVICES + "/" + entry + "/bus";
            if (!ExistPath(bdfBusPath)) {
                New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING, "pcie pmu bus file is empty");
                return LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING;
            }
            string bdfMinPath = SYS_DEVICES + "/" + entry + "/bdf_min";
            string bdfMaxPath = SYS_DEVICES + "/" + entry + "/bdf_max";

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
                pciePmuBdfRang[entry] = std::make_tuple(bdfBusStr.substr(strlen("0x")), -1, -1);
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
            pciePmuBdfRang[entry] = std::make_tuple(bdfBusStr.substr(strlen("0x")), bdfMin, bdfMax);
        }
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
        return SUCCESS;
    }

    static const char** PmuDevicePcieBdfList(unsigned *numBdf)
    {
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
                        pcieBdfList.emplace_back(strdup(bdfList[i].c_str()));
                        bdfToPcieMap[bdfList[i]] = pciePmu.first;
                    }
                } else {
                    uint16_t bdfValue = 0;
                    if (ConvertBdfStringToValue(bdfList[i], bdfValue) != SUCCESS) {
                        continue;
                    }
                    if (bus == pcieBus && bdfValue >= bdfmin && bdfValue <= bdfMax) {
                        pcieBdfList.emplace_back(strdup(bdfList[i].c_str()));
                        bdfToPcieMap[bdfList[i]] = pciePmu.first;
                    }
                }
            }
        }

        *numBdf = pcieBdfList.size();
        return pcieBdfList.data();
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
        return classifiedDevices;
    }

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
        return SUCCESS;
    }

    const char** PmuDeviceSmmuBdfList(unsigned *numBdf)
    {
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
                    smmuBdfList.push_back(strdup(bdfList[i].c_str()));
                    bdfToSmmuPmuMap[bdfList[i]] = smmuPmuName;
                }
            }
        }
        *numBdf = smmuBdfList.size();
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
        return SUCCESS;
    }

    static vector<string> GenerateEventStrings(std::unordered_map<string, vector<string>> classifiedDevices,
        const vector<string>& events, const string& extraConfig, const string& bdfParameter, const string& bdf)
    {
        vector<string> eventList;
        if (!bdf.empty()) {
            for (const auto& evt : events) {
                string device = "";
                if (bdfParameter == "bdf=") {
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
                if (!extraConfig.empty()) eventString += "," + extraConfig;
                if (!bdfParameter.empty() && !bdf.empty()) {
                    stringstream bdfValue;
                    uint16_t userBdf = 0;
                    ConvertBdfStringToValue(bdf, userBdf);
                    bdfValue << "0x" << hex << userBdf;
                    eventString += "," + bdfParameter + bdfValue.str();
                }
                eventString += "/";
                eventList.push_back(eventString);
            }
            return eventList;
        }
        for (const auto& [key, devices] : classifiedDevices) {
            for (const auto& evt : events) {
                for (const auto& device : devices) {
                    string eventString = device + "/config=" + evt + "/";
                    eventList.push_back(eventString);
                }
            }
        }
        return eventList;
    }

    static vector<string> GenerateEventList(struct PmuDeviceAttr& deviceAttr)
    {
        const auto& deviceConfig = GetDeviceMtricConfig();
        const auto& config = deviceConfig.at(deviceAttr.metric);
        string bdf = "";
        if (deviceAttr.bdf != nullptr) {
            bdf = deviceAttr.bdf;
        }
        int err = QueryUncoreRawDevices();
        if (err != SUCCESS) {
            return {};
        }
        auto classifiedDevices = ClassifyDevicesByPrefix(config.devicePrefix, config.subDeviceName, config.splitPosition);
        return GenerateEventStrings(classifiedDevices, config.events, config.extraConfig, config.bdfParameter, bdf);
    }

    static int CheckDeviceMetricEnum(struct PmuDeviceAttr *attr, unsigned len)
    {
        const auto& metricConfig = GetDeviceMtricConfig();
        for (int i = 0; i < len; ++i) {
            if (metricConfig.find(attr[i].metric) == metricConfig.end()) {
                New(LIBPERF_ERR_INVALID_PMU_DEVICES_METRIC, "For this platform this metric " +
                    GetMetricString(attr[i].metric) + " is invalid value for PmuDeviceMetric!");
                return LIBPERF_ERR_INVALID_PMU_DEVICES_METRIC;
            }
        }
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

    static int CheckBdf(struct PmuDeviceAttr *attr, unsigned len)
    {
        for (int i = 0; i < len; ++i) {
            if (attr[i].metric >= PmuDeviceMetric::PMU_PCIE_RX_MRD_BW && attr[i].bdf == nullptr) {
                New(LIBPERF_ERR_INVALID_PMU_DEVICES_BDF, "When collecting pcie or smmu metric, bdf value can not is nullptr!");
                return LIBPERF_ERR_INVALID_PMU_DEVICES_BDF;
            }
            if (attr[i].metric >= PmuDeviceMetric::PMU_PCIE_RX_MRD_BW && attr[i].metric <= PmuDeviceMetric::PMU_PCIE_TX_MWR_BW
                && !CheckPcieBdf(attr[i].bdf)) {
                New(LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF, "this bdf not support pcie metric counting."
                    " Plese use PmuDeviceBdfList to query.");
                return LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF;
            }
            if (attr[i].metric == PmuDeviceMetric::PMU_SMMU_TRAN && !CheckSmmuBdf(attr[i].bdf)) {
                New(LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF, "this bdf not support smmu metric counting."
                " Plese use PmuDeviceBdfList to query.");
                return LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF;
            }
        }
        return SUCCESS;
    }

    static int CheckPmuDeviceAttr(struct PmuDeviceAttr *attr, unsigned len)
    {
        int err = CheckDeviceMetricEnum(attr, len);
        if (err != SUCCESS) {
            return err;
        }

        err = CheckBdf(attr, len);
        if (err != SUCCESS) {
            return err;
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
        // Sort data by bdf.
        unordered_map<string, vector<InnerDeviceData>> devDataByBdf;
        for (auto &data : rawData) {
            devDataByBdf[data.bdf].push_back(data);
        }

        // Sum all transactions from different smmu devices for each pcie device.
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
        if (perpcieMetric.find(devAttr.metric) != perpcieMetric.end()) {
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
            if (perpcieMetric.find(devAttr.metric) != pernumaMetric.end()) {
                devData.bdf = devAttr.bdf;
            }
            metricMap[devData.metric].push_back(devData);
        }
        return SUCCESS;
    }
}

using namespace KUNPENG_PMU;

const char** PmuDeviceBdfList(enum PmuBdfType bdfType, unsigned *numBdf)
{
    if (bdfType == PmuBdfType::PMU_BDF_TYPE_PCIE) {
        return PmuDevicePcieBdfList(numBdf);
    }

    if (bdfType == PmuBdfType::PMU_BDF_TYPE_SMMU) {
        return PmuDeviceSmmuBdfList(numBdf);
    }
}

int PmuDeviceOpen(struct PmuDeviceAttr *attr, unsigned len)
{
    if (CheckPmuDeviceAttr(attr, len) != SUCCESS) {
        return -1;
    }
    vector<string> configEvtList;
    for (int i = 0; i < len; ++i) {
        vector<string> temp = GenerateEventList(attr[i]);
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
}

int PmuGetDevMetric(struct PmuData *pmuData, unsigned len,
                    struct PmuDeviceAttr *attr, unsigned attrLen,
                    struct PmuDeviceData **data)
{
    // Filter pmuData by metric and generate InnerDeviceData, 
    // which contains event name, core id, numa id and bdf.
    // InnerDeviceData will be used to aggregate data by core id, numa id or bdf.
    MetricMap metricMap;
    for (unsigned i = 0; i < attrLen; ++i) {
        int ret = GetDevMetric(pmuData, len, attr[i], metricMap);
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
        }
        else {
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
    deviceDataMap[dataPtr] = move(devData);
    *data = dataPtr;
    New(SUCCESS);
    return retLen;
}

void DevDataFree(struct PmuDeviceData *data)
{
    SetWarn(SUCCESS);
    if (deviceDataMap.find(data) != deviceDataMap.end()) {
        deviceDataMap.erase(data);
    }
    New(SUCCESS);
}

int PmuGetCpuFreq(unsigned core)
{
    stringstream cpuPath;
    cpuPath << SYS_CPU_INFO_PATH << core << "/cpufreq/scaling_cur_freq";

    if (!ExistPath(cpuPath.str())) {
        return -1;
    }
    std::string curFreqStr = ReadFileContent(cpuPath.str());
    int cpuFreq = 0;
    try {
        cpuFreq = stoi(curFreqStr);
    } catch (std::exception& e) {
        return -1;
    }
    return cpuFreq;
}
