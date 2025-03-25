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
 * Author: Mr.Ye
 * Create: 2024-04-03
 * Description: uncore event configuration query
 ******************************************************************************/
#include <fstream>
#include <sstream>
#include <dirent.h>
#include "common.h"
#include "log.h"
#include "pcerr.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "uncore.h"

using namespace std;
using namespace KUNPENG_PMU;

static std::unordered_map<std::string, std::unordered_map<string, uint64_t>> unCoreRawFieldsValues;

static int GetDeviceType(const string &devName)
{
    string typePath = "/sys/devices/" + devName + "/type";
    std::string realPath = GetRealPath(typePath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream typeIn(realPath);
    if (!typeIn.is_open()) {
        return -1;
    }
    string typeStr;
    typeIn >> typeStr;

    return stoi(typeStr);
}

static int GetCpuMask(const string &devName)
{
    string maskPath = "/sys/devices/" + devName + "/cpumask";
    std::string realPath = GetRealPath(maskPath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream maskIn(realPath);
    if (!maskIn.is_open()) {
        return -1;
    }
    // Cpumask is a comma-separated list of integers,
    // but now make it simple for ddrc event.
    string maskStr;
    maskIn >> maskStr;

    return stoi(maskStr);
}

static int64_t GetUncoreEventConfig(const char* pmuName)
{
    int64_t config;
    string strName(pmuName);
    auto findSlash = strName.find('/');
    string devName = strName.substr(0, findSlash);
    string evtName = strName.substr(devName.size() + 1, strName.size() - 1 - (devName.size() + 1));
    string evtPath = "/sys/devices/" + devName + "/events/" + evtName;
    std::string realPath = GetRealPath(evtPath);
    if (!IsValidPath(realPath)) {
        return -1;
    }
    ifstream evtIn(realPath);
    if (!evtIn.is_open()) {
        return -1;
    }
    string configStr;
    evtIn >> configStr;
    auto findEq = configStr.find('=');
    if (findEq == string::npos) {
        return -1;
    }
    auto subStr = configStr.substr(findEq + 1, configStr.size() - findEq);
    std::istringstream iss(subStr);
    iss >> std::hex >> config;

    return config;
}

int FillUncoreFields(const char* pmuName, PmuEvt *evt)
{
    string strName(pmuName);
    auto findSlash = strName.find('/');
    string devName = strName.substr(0, findSlash);
    string evtName = strName.substr(devName.size() + 1, strName.size() - 1 - (devName.size() + 1));
    int devType = GetDeviceType(devName);
    if (devType == -1) {
        return UNKNOWN_ERROR;
    }
    evt->type = devType;
    int cpuMask = GetCpuMask(devName);
    if (cpuMask == -1) {
        return UNKNOWN_ERROR;
    }

    evt->cpumask = cpuMask;
    evt->name = pmuName;
    return SUCCESS;
}

// Read the config params bitfiled from /sys/devices/<devName>/format
static std::unordered_map<std::string, UncoreConfigBitFiled> ReadConfigFormatFiles(const string &devName)
{
    string formatPath = "/sys/devices/" + devName + "/format";
    std::string realPath = GetRealPath(formatPath);
    if (!IsValidPath(realPath)) {
        return {};
    }
    DIR* dir = opendir(realPath.c_str());
    if (!dir) {
        DBG_PRINT("Error: Unable to open directrory: %s\n.", realPath);
        return {};
    }

    struct dirent* entry;
    unordered_map<string, UncoreConfigBitFiled> supportConfigParams;
    while ((entry = readdir(dir)) != nullptr) {
        string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") {
            continue;
        }

        ifstream ifs(realPath + "/" + fileName);
        if (!ifs.is_open()) {
            continue;
        }

        string line;
        getline(ifs, line);
        if (line.empty()) {
            continue;
        }

        // Parse the config params bitfiled, example: "config1:0-31" 或 "config1:33"
        auto colonPos = line.find(':');
        if (colonPos == string::npos) {
            continue;
        }

        string paramName = fileName;
        string fieldName = line.substr(0, colonPos);
        string bitFiledInfo = line.substr(colonPos + 1);

        unsigned startBit = 0;
        unsigned endBit = 0;
        auto hyphenPos = bitFiledInfo.find('-');
        if (hyphenPos == string::npos) {
            // single bit param, example: "config1:33"
            startBit = stoi(bitFiledInfo);
            endBit = startBit;
        } else {
            // double bit param, example: "config1:0-31"
            startBit = stoi(bitFiledInfo.substr(0, hyphenPos));
            endBit = stoi(bitFiledInfo.substr(hyphenPos + 1));
        }

        supportConfigParams[paramName] = UncoreConfigBitFiled{fieldName, startBit, endBit};
        // adapt config event to use "config" as param name. ex:config=0x1x
        if (paramName == "event") {
            supportConfigParams["config"] = UncoreConfigBitFiled{fieldName, startBit, endBit};
        }
    }

    closedir(dir);
    
    return supportConfigParams;
}


static std::pair<string, string> ReadSupportEvtValue(const string &devName, const string &evtName)
{
    string evtPath = "/sys/devices/" + devName + "/events/" + evtName;
    std::string realPath = GetRealPath(evtPath);
    if (!IsValidPath(realPath)) {
        return {};
    }
    ifstream ifs(realPath);
    if (!ifs.is_open()) {
        DBG_PRINT("Error: Unable to open evtpath file: %s\n.", realPath);
        return {};
    }

    // ex: key: event value:0x01
    std::pair<string, string> evtConfigRegs;
    string line;
    while (getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        auto equalPos = line.find('=');
        if (equalPos == string::npos) {
            continue;
        }
        string key = line.substr(0, equalPos);
        if (key == "config") {
            key = "event";
        }
        string value = line.substr(equalPos + 1);
        evtConfigRegs = make_pair(key, value);
    }
    
    return evtConfigRegs;
}

// parse event and config params string.
// ex: event=0x01,filter_enable=1,filter_stream_id=0x7d split like: "event=0x01", "filter_enable=1", "filter_stream_id=0x7d"
static std::unordered_map<string, string> ParseEventConfig(const string &eventStr, const string &devName)
{
    std::unordered_map<string, string> parseParams;

    for (size_t pos = 0; pos < eventStr.size(); ) {
        size_t nextPos = eventStr.find(',', pos);
        if (nextPos == string::npos) {
            // if not find ',' in eventStr, handle the last param
            nextPos = eventStr.size();
        }

        std::string paramStr = eventStr.substr(pos, nextPos - pos);
        auto equalPos = paramStr.find('=');
        if (equalPos != string::npos) {
            string evtName = paramStr.substr(0, equalPos);
            string configValue = paramStr.substr(equalPos + 1);
            parseParams[evtName] = configValue;
        } else {
            // config value is empty, ex: transaction
            string evtName = paramStr;
            auto evtConfigPair = ReadSupportEvtValue(devName, evtName);
            if (evtConfigPair.first.empty() || evtConfigPair.second.empty()) {
                std::cerr << "Error: event Name is error" << std::endl;
                return {};
            }
            parseParams[evtConfigPair.first] = evtConfigPair.second;
        }
        pos = nextPos + 1;
    }

    return parseParams;
}

static bool ValidConfigValueAndGenerateFields(const std::unordered_map<string, UncoreConfigBitFiled> &supportConfigParams,
                                       const std::unordered_map<string, string> &parseParams, std::unordered_map<string, uint64_t> &fields)
{
    for (const auto& [key, value] : parseParams) {
        auto findConfig = supportConfigParams.find(key);
        if (findConfig == supportConfigParams.end()) {
            DBG_PRINT("Error: Invalid config param %s\n", key.c_str());
            return false;
        }
        auto& configBitFiled = findConfig->second;
        unsigned bitWidth = configBitFiled.endBit - configBitFiled.startBit + 1;
        uint64_t maxValue = (1ULL << bitWidth) - 1;

        uint64_t configValue = 0;
        try {
            configValue = stoull(value, nullptr, 0);
        } catch (const std::invalid_argument& e) {
            DBG_PRINT("Error: Invalid config value %s\n", value.c_str());
            return false;
        }

        if (configValue < 0 || configValue > maxValue) {
            DBG_PRINT("Error: config value: %s is too big or negative number.\n", value.c_str());
            return false;
        }

        uint64_t shiftedValue = configValue << configBitFiled.startBit;
        
        fields[configBitFiled.configName] |= shiftedValue;
    }

    return true;
}

bool CheckUncoreRawEvent(const char *pmuName)
{
    string strName = pmuName;
    auto firstFindSlash = strName.find('/');
    auto lastFindSlash = strName.rfind('/');
    if (firstFindSlash == string::npos || lastFindSlash == string::npos || firstFindSlash >= lastFindSlash) {
        return false;
    }
    unsigned numEvt;
    auto eventList = PmuEventList(UNCORE_EVENT, &numEvt);
    if (eventList == nullptr) {
        return false;
    }
    string devName = strName.substr(0, firstFindSlash);
    // check if front part of pmuName in Uncore Pmu Event List
    bool findDev = false;
    for (int j = 0; j < numEvt; ++j) {
        if (strstr(eventList[j], devName.c_str()) != nullptr) {
            findDev = true;
            break;
        }
    }
    if (!findDev) {
        return false;
    }

    // check if "config=, params= " at back part of pmuName
    auto supportConfigParams = ReadConfigFormatFiles(devName);
    if (supportConfigParams.empty()) {
        return false;
    }

    std::string eventStr = strName.substr(firstFindSlash + 1, lastFindSlash - 1 - firstFindSlash);
    auto parseParams = ParseEventConfig(eventStr, devName);
    if (parseParams.empty()) {
        return false;
    }

    std::unordered_map<string, uint64_t> fieldsValues;
    if (!ValidConfigValueAndGenerateFields(supportConfigParams, parseParams, fieldsValues)) {
        return false;
    }
    unCoreRawFieldsValues[strName] = fieldsValues;

    return true;
}

struct PmuEvt* GetUncoreEvent(const char* pmuName, int collectType)
{
    int64_t config = GetUncoreEventConfig(pmuName);
    if (config == -1) {
        return nullptr;
    }
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config;
    pmuEvtPtr->name = pmuName;
    pmuEvtPtr->pmuType = UNCORE_TYPE;
    pmuEvtPtr->collectType = collectType;

    // Fill fields for uncore devices.
    auto err = FillUncoreFields(pmuName, pmuEvtPtr);
    if (err != SUCCESS) {
        return nullptr;
    }
    return pmuEvtPtr;
}

struct PmuEvt* GetUncoreRawEvent(const char* pmuName, int collectType)
{
    if (unCoreRawFieldsValues.empty()) {
        return nullptr;
    }
    auto fieldsValues = unCoreRawFieldsValues[(std::string)pmuName];
    auto* pmuEvtPtr = new PmuEvt {0};
    if (fieldsValues.find("config") == fieldsValues.end()) {
        pmuEvtPtr->config = 0;
    } else {
        pmuEvtPtr->config = fieldsValues.at("config");
    }
    if (fieldsValues.find("config1") == fieldsValues.end()) {
        pmuEvtPtr->config1 = 0;
    } else {
        pmuEvtPtr->config1 = fieldsValues.at("config1");
    }
    if (fieldsValues.find("config2") == fieldsValues.end()) {
        pmuEvtPtr->config2 = 0;
    } else {
        pmuEvtPtr->config2 = fieldsValues.at("config2");
    }

    pmuEvtPtr->name = pmuName;
    pmuEvtPtr->pmuType = UNCORE_RAW_TYPE;
    pmuEvtPtr->collectType = collectType;

    // Fill fields for uncore devices.
    auto err = FillUncoreFields(pmuName, pmuEvtPtr);
    if (err != SUCCESS) {
        return nullptr;
    }
    return pmuEvtPtr;
}