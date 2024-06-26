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
#include "common.h"
#include "pcerr.h"
#include "pfm_event.h"
#include "pmu_event.h"
#include "uncore.h"

using namespace std;
using namespace KUNPENG_PMU;

static const string CONFIG_EQUAL = "config=";

static int ParseConfig(const char* pmuName) {
    string strName = pmuName;
    auto findSlash = strName.find('/');
    string devName = strName.substr(0, findSlash);
    string evtName = strName.substr(devName.size() + 1, strName.size() - 1 - (devName.size() + 1));
    try {
        size_t pos = evtName.find('=');
        string config = evtName.substr(pos + 1);
        if (config.find("0x") != string::npos) {
            // hexadecimal config
            return stoi(config, nullptr, 16);
        } else {
            // decimal config
            return stoi(config);
        }
    } catch (const invalid_argument& e) {
        return -1;
    }
}

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

bool CheckUncoreRawEvent(const char *pmuName)
{
    string strName = pmuName;
    auto findSlash = strName.find('/');
    if (findSlash == string::npos) {
        return false;
    }
    unsigned numEvt;
    auto eventList = PmuEventList(UNCORE_EVENT, &numEvt);
    if (eventList == nullptr) {
        return false;
    }
    string devName = strName.substr(0, findSlash);
    string evtName = strName.substr(devName.size() + 1, strName.size() - 1 - (devName.size() + 1));
    // check if "config=" at back part of pmuName
    if (strstr(evtName.c_str(), CONFIG_EQUAL.c_str()) == nullptr) {
        return false;
    }
    // check if config invalid
    if (ParseConfig(pmuName) == -1) {
        return false;
    }
    // check if front part of pmuName in Uncore Pmu Event List
    for (int j = 0; j < numEvt; ++j) {
        if (strstr(eventList[j], devName.c_str()) == nullptr) {
            return true;
        }
    }
    return false;
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
    int64_t config = ParseConfig(pmuName);
    if (config == -1) {
        return nullptr;
    }
    auto* pmuEvtPtr = new PmuEvt {0};
    pmuEvtPtr->config = config;
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