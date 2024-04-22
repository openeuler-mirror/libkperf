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
#include "core.h"
#include "pcerr.h"
#include "pmu.h"

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

static void GetEventName(const string& devName, vector<const char*>& eventList)
{
    DIR* dir;
    struct dirent* entry;
    auto path = SYS_DEVICES + devName + EVENT_DIR;
    if ((dir = opendir(path.c_str())) == nullptr) {
        New(LIBPERF_ERR_FOLDER_PATH_INACCESSIBLE, "Could not open \"/sys/devices/\"");
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
        eventList.push_back(eventNameCopy);
    }
    closedir(dir);
}

static void GetTraceSubFolder(const string& devName, vector<const char*>& eventList)
{
    DIR* dir;
    struct dirent* entry;
    auto path = TRACE_FOLDER + devName;
    if ((dir = opendir(path.c_str())) == nullptr) {
        New(LIBPERF_ERR_FOLDER_PATH_INACCESSIBLE, "Could not open " + path);
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
            eventList.push_back(eventNameCopy);
        }
    }
    closedir(dir);
}

const char** QueryCoreEvent(unsigned *numEvt)
{
    static vector<const char*> eventList;
    auto coreEventMap = KUNPENG_PMU::CORE_EVENT_MAP.at(GetCpuType());
    for (auto& pair : coreEventMap) {
        auto eventName = pair.first;
        char* eventNameCopy = new char[eventName.length() + 1];
        strcpy(eventNameCopy, eventName.c_str());
        eventList.push_back(eventNameCopy);
    }
    *numEvt = eventList.size();
    return eventList.data();
}

const char** QueryUncoreEvent(unsigned *numEvt)
{
    DIR* dir;
    struct dirent* entry;
    dir = opendir(SYS_DEVICES.c_str());
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
    DIR* dir;
    struct dirent* entry;
    dir = opendir(TRACE_FOLDER.c_str());
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

const char** QueryAllEvent(unsigned *numEvt)
{
    unsigned coreNum;
    unsigned uncoreNum;
    unsigned traceNum;
    auto coreList = QueryCoreEvent(&coreNum);
    auto uncoreList = QueryUncoreEvent(&uncoreNum);
    auto traceList = QueryTraceEvent(&traceNum);
    *numEvt = coreNum + uncoreNum + traceNum;
    const char** combinedList = new const char* [*numEvt];
    memcpy(combinedList, coreList, coreNum * sizeof(char*));
    memcpy(combinedList + coreNum, uncoreList, uncoreNum * sizeof(char*));
    memcpy(combinedList + coreNum + uncoreNum, traceList, traceNum * sizeof(char*));
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

void PmuEventListFree(const char** eventList, unsigned *numEvt)
{
    lock_guard<mutex> lg(pmuEventListMtx);
    for (unsigned i = 0; i < *numEvt; i++) {
        delete[] eventList[i];
    }
    uncoreEventList.clear();
    traceEventList.clear();
    New(SUCCESS);
}