/**
 * @copyright Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * @brief
 * @version 24.0
 * @date 2024-04-18
 */
#include <iostream>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <fstream>
#include "core.h"
#include "pcerr.h"
#include "pmu.h"
#include "common.h"

using namespace pcerr;
using namespace std;
using EvtQueryer = function<const char**(unsigned*)>;

static const string SLASH = "/";
static const string COLON = ":";
static const string UNCORE_PREFIX = "hisi";
static const string SYS_DEVICES = "/sys/devices/";
static const string EVENT_DIR = "/events/";
static const string TRACE_FOLDER = "/sys/kernel/tracing/events/";

static std::mutex pmuEventListMtx;

static vector<const char*> uncoreEventList;
static vector<const char*> traceEventList;
static vector<const char*> coreEventList;

static void GetEventName(const string& devName, vector<const char*>& eventList)
{
    DIR* dir;
    struct dirent* entry;
    auto path = SYS_DEVICES + devName + EVENT_DIR;
    dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) { // Check if it is a regular file

            continue;
        }
        string fileName(entry->d_name);
        auto eventName = devName;
        eventName += SLASH + fileName;
        eventName +=  SLASH;
        char* eventNameCopy = new char[eventName.length() + 1];
        strcpy(eventNameCopy, eventName.c_str());
        eventList.emplace_back(eventNameCopy);
    }
    closedir(dir);
}

static void GetTraceSubFolder(const string& devName, vector<const char*>& eventList)
{
    DIR* dir;
    struct dirent* entry;
    auto path = TRACE_FOLDER + devName;
    dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) { // Check if it is a regular folder
            continue;
        }
        string folderName(entry->d_name);
        if (folderName.find('.') == string::npos) {
            auto eventName = devName;
            eventName += COLON + folderName;
            char* eventNameCopy = new char[eventName.length() + 1];
            strcpy(eventNameCopy, eventName.c_str());
            eventList.emplace_back(eventNameCopy);
        }
    }
    closedir(dir);
}

const char** QueryCoreEvent(unsigned *numEvt)
{
    if (!coreEventList.empty()) {
        *numEvt = coreEventList.size();
        return coreEventList.data();
    }
    auto coreEventMap = KUNPENG_PMU::CORE_EVENT_MAP.at(GetCpuType());
    for (auto& pair : coreEventMap) {
        auto eventName = pair.first;
        char* eventNameCopy = new char[eventName.length() + 1];
        strcpy(eventNameCopy, eventName.c_str());
        coreEventList.emplace_back(eventNameCopy);
    }
    DIR* dir;
    struct dirent* entry;
    string path = "/sys/devices/armv8_pmuv3_0/events/";
    dir = opendir(path.c_str());
    if (dir == nullptr) {
        return nullptr;
    }
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            string evtName = entry->d_name;
            char* eventNameCopy = new char[evtName.length() + 1];
            strcpy(eventNameCopy, evtName.c_str());
            coreEventList.emplace_back(eventNameCopy);
        }
    }
    closedir(dir);
    *numEvt = coreEventList.size();
    return coreEventList.data();
}

const char** QueryUncoreEvent(unsigned *numEvt)
{
    if (!uncoreEventList.empty()) {
        *numEvt = uncoreEventList.size();
        return uncoreEventList.data();
    }
    DIR* dir;
    struct dirent* entry;
    dir = opendir(SYS_DEVICES.c_str());
    if (dir == nullptr) {
        return nullptr;
    }
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            string folderName = entry->d_name;
            if (folderName.find(UNCORE_PREFIX) == 0) {
                string devName(entry->d_name);
                GetEventName(devName, uncoreEventList);
            }
        }
    }
    closedir(dir);
    *numEvt = uncoreEventList.size();
    return uncoreEventList.data();
}

const char** QueryTraceEvent(unsigned *numEvt)
{
    if (!traceEventList.empty()) {
        *numEvt = traceEventList.size();
        return traceEventList.data();
    }
    DIR* dir;
    struct dirent* entry;
    dir = opendir(TRACE_FOLDER.c_str());
    if (dir == nullptr) {
        return nullptr;
    }
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            string folderName = entry->d_name;
            if (folderName.find('.') == string::npos) {
                string devName(entry->d_name);
                GetTraceSubFolder(devName, traceEventList);
            }
        }
    }
    closedir(dir);
    *numEvt = traceEventList.size();
    return traceEventList.data();
}

const char** QueryAllEvent(unsigned *numEvt) {
    unsigned coreNum, uncoreNum, traceNum;
    const char** coreList = QueryCoreEvent(&coreNum);
    const char** uncoreList = QueryUncoreEvent(&uncoreNum);
    const char** traceList = QueryTraceEvent(&traceNum);

    unsigned totalSize = 0;
    if (coreList != nullptr) {
        totalSize += coreNum;
    }
    if (uncoreList != nullptr) {
        totalSize += uncoreNum;
    }
    if (traceList != nullptr) {
        totalSize += traceNum;
    }

    const char** combinedList = new const char*[totalSize];
    unsigned index = 0;

    if (coreList != nullptr) {
        memcpy(combinedList, coreList, coreNum * sizeof(const char*));
        index += coreNum;
    }
    if (uncoreList != nullptr) {
        memcpy(combinedList + index, uncoreList, uncoreNum * sizeof(const char*));
        index += uncoreNum;
    }
    if (traceList != nullptr) {
        memcpy(combinedList + index, traceList, traceNum * sizeof(const char*));
    }

    *numEvt = totalSize;
    return combinedList;
}

static const unordered_map<int, EvtQueryer> QueryMap{
        {PmuEventType::CORE_EVENT, QueryCoreEvent},
        {PmuEventType::UNCORE_EVENT, QueryUncoreEvent},
        {PmuEventType::TRACE_EVENT, QueryTraceEvent},
        {PmuEventType::ALL_EVENT, QueryAllEvent},
};

const char** PmuEventList(enum PmuEventType eventType, unsigned *numEvt)
{
    lock_guard<mutex> lg(pmuEventListMtx);
    const char** eventList;
    if (QueryMap.find(eventType) == QueryMap.end()) {
        New(LIBPERF_ERR_QUERY_EVENT_TYPE_INVALID, "Event type is invalid.");
        return nullptr;
    }
    try {
        eventList = QueryMap.at(eventType)(numEvt);
    } catch (...) {
        New(LIBPERF_ERR_QUERY_EVENT_LIST_FAILED, "Query event failed.");
        return nullptr;
    }
    New(SUCCESS);
    return eventList;
}

static void PmuEventListFreeSingle(vector<const char*>& eventList)
{
    for (auto evt : eventList) {
        if (evt != nullptr && evt[0] != '\0')
            delete[] evt;
    }
    eventList.clear();
}

void PmuEventListFree()
{
    lock_guard<mutex> lg(pmuEventListMtx);
    PmuEventListFreeSingle(coreEventList);
    PmuEventListFreeSingle(uncoreEventList);
    PmuEventListFreeSingle(traceEventList);
    New(SUCCESS);
}