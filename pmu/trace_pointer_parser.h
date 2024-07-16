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
#ifndef LIBKPERF_TRACE_POINTER_PARSER_H
#define LIBKPERF_TRACE_POINTER_PARSER_H

#include <iostream>
#include <memory>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <fstream>

#include "pcerr.h"
#include "common.h"
#include "pmu_event.h"

using namespace std;

namespace KUNPENG_PMU {

    struct Field {
        unsigned offset; //the data offset.
        unsigned size; //the field size.
        unsigned isSigned; //signed and unsigned
        std::string fieldName; //the field name of this field.
        std::string fieldStr; //the field line.

        bool IsDataLoc() const {
            return fieldStr.find("__data_loc") != string::npos;
        }

        bool operator<(const Field &t) const {
            return this->fieldStr < t.fieldStr;
        }
    };

    class PointerPasser {
    public:
        /**
         * @brief determine whether the event is a pointer event.
         */
        static bool IsNeedFormat(ifstream &file, const std::string &evtName);

        /**
         * @brief if this event is pointer event, should parse it.
         */
        static void
        ParserRawFormatData(struct PmuData *pd, PerfRawSample *sample, union PerfEvent *event, const string &evtName);

        /**
         * @brief the method of parsing field.
         */
        template<typename T>
        static int ParseField(char *data, const string &fieldName, T *value, uint32_t vSize);

        /**
         * @brief the method of parsing field.
         */
        static int ParsePointer(char *data, const string &fieldName, void *value, uint32_t vSize);

        /**
         * @brief free the data.
         */
        static void FreePointerData(char *data);

        /**
         * @brief get the field named fieldName of this event.
         * @return
         */
        static SampleRawField *GetSampleRawField(char *data, const string &fieldName);

        /**
         * @brief clear the field
         */
        static void FreeRawFieldMap();
    };
}


#endif //LIBKPERF_TRACE_POINTER_PARSER_H
