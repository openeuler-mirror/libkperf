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
 * Create: 2024-07-04
 * Description: Provides the capability of parsing pointer events.
 ******************************************************************************/

#include "trace_pointer_parser.h"

#define offset(type, v) (&(((type *)0)->v))

using namespace TracePointerParser;
using namespace pcerr;

const char *POINTER_FIELD_STR = "field:";
const char *POINTER_OFFSET_REGEX = "%*[^0-9]%i%*[;] %*[^0-9]%i%*[;]";

static std::unordered_map<std::string, std::unordered_map<string, Field>> efMap; //the key is event name, value is field and ths field name map.
static std::unordered_map<char *, std::string> dEvtMap; //The key is the data pointer, value is event name.

bool TracePointerParser::PointerPasser::IsNeedFormat(std::ifstream &file, const std::string &evtName) {
    auto colonId = evtName.find(':');
    if (colonId == string::npos) {
        return false;
    }
    string eventName = evtName.substr(colonId + 1);
    string systemName = evtName.substr(0, colonId);
    string formatPath = "/sys/kernel/tracing/events/" + systemName + "/" + eventName + "/format";
    string realPath = GetRealPath(formatPath);
    if (realPath.empty()) {
        return false;
    }
    if (!IsValidPath(realPath)) {
        return false;
    }
    file.open(realPath);
    if (!file.is_open()) {
        return false;
    }
    return true;
}

void ParseFormatFile(ifstream &file, const std::string &evtName) {
    // parse the format file.
    string line;
    std::unordered_map<string, Field> fnMap;

    while (getline(file, line)) {
        if (line.find(POINTER_FIELD_STR) == string::npos) {
            continue;
        }
        auto sealId = line.find(';');
        if (sealId == string::npos) {
            continue;
        }
        Field field;
        if (!sscanf(line.substr(sealId + 1).c_str(), POINTER_OFFSET_REGEX, &field.offset, &field.size)) {
            continue;
        }
        field.fieldStr = line.substr(strlen(POINTER_FIELD_STR) + 1, sealId - strlen(POINTER_FIELD_STR) - 1);
        auto spaceId = field.fieldStr.find_last_of(' ');
        auto braceId = field.fieldStr.find('[');
        field.fieldName =
                braceId == string::npos ? field.fieldStr.substr(spaceId + 1) : field.fieldStr.substr(spaceId + 1,
                                                                                                     braceId - spaceId -
                                                                                                     1);
        fnMap.insert({field.fieldName, field});
    }
    // add the evtName and its fields map.
    efMap.insert({evtName, fnMap});
}

void TracePointerParser::PointerPasser::ParserRawFormatData(struct PmuData *pd, KUNPENG_PMU::PerfRawSample *sample,
                                                            union KUNPENG_PMU::PerfEvent *event,
                                                            const std::string &evtName) {
    ifstream file;
    if (!IsNeedFormat(file, evtName)) {
        pd->rawData = nullptr;
        return;
    }
    // Get the perf sample raw data.
    auto ipsOffset = (unsigned long) offset(struct PerfRawSample, ips); // ips offset of PerfRawSample.
    auto traceOffset = ipsOffset + sample->nr * (sizeof(unsigned long)); // TraceRawData offset of sample.array.
    TraceRawData *trace = (TraceRawData *) ((char *) &event->sample.array + traceOffset);

    if (trace->size == 0) {
        pd->rawData = nullptr;
        return;
    }

    // copy the perf sample raw data to our rawData->data.
    pd->rawData = (SampleRawData *) malloc(sizeof(struct SampleRawData));
    if (!pd->rawData) {
        return;
    }
    pd->rawData->data = (char *) malloc(trace->size);
    if (!pd->rawData->data) {
        free(pd->rawData);
        pd->rawData = nullptr;
        return;
    }
    memcpy(pd->rawData->data, trace->data, trace->size);
    dEvtMap.insert({pd->rawData->data, evtName});

    if (efMap.find(evtName) != efMap.end()) {
        return;
    }
    ParseFormatFile(file, evtName);
}

template<typename T>
int CheckFieldArgs(char *data, const string &fieldName, T *value, uint32_t vSize) {
    if (data == nullptr) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "data cannot be nullptr.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }
    if (fieldName.empty()) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "fieldName cannot be empty.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }

    if (value == nullptr) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "value cannot be nullptr.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }

    if (vSize == 0) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "vSize cannot be zero.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }
    auto evtIt = dEvtMap.find(data);
    if (evtIt == dEvtMap.end()) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS,
            "The args data maybe have changed, can't find the event name which it si associated with.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }
    auto fieldIt = efMap.find(evtIt->second);
    if (fieldIt == efMap.end()) {
        New(LIBPERF_ERR_FIND_FIELD_LOSS, "unknown error, can't find the filed map.");
        return LIBPERF_ERR_FIND_FIELD_LOSS;
    }
    if (fieldIt->second.find(fieldName) == fieldIt->second.end()) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS, "invalid fieldName, can't find it in format data.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;;
    }
    return SUCCESS;
}

template<typename T>
int TracePointerParser::PointerPasser::ParseField(char *data, const std::string &fieldName, T *value, uint32_t vSize) {
    int rt = CheckFieldArgs(data, fieldName, value, vSize);
    if (rt != SUCCESS) {
        return rt;
    }
    Field field = efMap.at(dEvtMap.at(data)).at(fieldName);
    if (field.IsDataLoc()) {
        int dataLoc;
        memcpy(&dataLoc, data + field.offset, field.size);
        char *dataName = data + (unsigned short) (dataLoc & 0xffff);
        if (strlen(dataName) > vSize) {
            New(LIBPERF_ERR_INVALID_FIELD_ARGS,
                "__dta_loc data requires " + std::to_string(strlen(dataName)) +
                " bytes of memory, the value needs larger memory.");
            return LIBPERF_ERR_INVALID_FIELD_ARGS;
        }
        memcpy(value, dataName, strlen(dataName) + 1);
        return SUCCESS;
    }
    if (field.size > vSize) {
        New(LIBPERF_ERR_INVALID_FIELD_ARGS,
            "value requires " + std::to_string(field.size) +
            " bytes of memory, the value needs larger memory.");
        return LIBPERF_ERR_INVALID_FIELD_ARGS;
    }
    memcpy(value, data + field.offset, field.size);
    New(SUCCESS);
    return SUCCESS;
}

int TracePointerParser::PointerPasser::ParsePointer(char *data, const std::string &fieldName, void *value,
                                                    uint32_t vSize) {
    return ParseField(data, fieldName, value, vSize);
}

void TracePointerParser::PointerPasser::FreePointerData(char *data) {
    if (data == nullptr) {
        return;
    }
    if (dEvtMap.find(data) != dEvtMap.end()) {
        dEvtMap.erase(data);
    }
    free(data);
    data = nullptr;
}
