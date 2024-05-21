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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Error code mechanism, used to return error codes and error messages.
 ******************************************************************************/
#include <unordered_map>
#include "pcerrc.h"
#include "pcerr.h"

namespace pcerr {
    static std::unordered_map<int, std::string> defaultMsg = {
            {SUCCESS, "success"},
            {COMMON_ERR_NOMEM, "not enough memory"},
            {LIBPERF_ERR_NO_AVAIL_PD, "no available pd for libperf"},
            {LIBPERF_ERR_CHIP_TYPE_INVALID, "invalid cpu arch"},
            {LIBPERF_ERR_FAIL_LISTEN_PROC, "failed to listen process"},
            {LIBPERF_ERR_INVALID_CPULIST, "invalid cpu list"},
            {LIBPERF_ERR_INVALID_PIDLIST, "invalid pid list"},
            {LIBPERF_ERR_INVALID_EVTLIST, "invalid event list"},
            {LIBPERF_ERR_INVALID_PD, "invalid pd"},
            {LIBPERF_ERR_INVALID_EVENT, "invalid event"},
            {LIBPERF_ERR_SPE_UNAVAIL, "spe unavailable"},
            {LIBPERF_ERR_FAIL_GET_CPU, "failed to get cpu info"},
            {LIBPERF_ERR_FAIL_GET_PROC, "failed to get process info"},
            {LIBPERF_ERR_NO_PERMISSION, "no permission to open pmu device"},
            {LIBPERF_ERR_DEVICE_BUSY, "pmu device is busy"},
            {LIBPERF_ERR_DEVICE_INVAL, "invalid event for pmu device"},
            {LIBPERF_ERR_FAIL_MMAP, "failed to mmap"},
            {LIBPERF_ERR_FAIL_RESOLVE_MODULE, "failed to resolve symbols"},
            {LIBPERF_ERR_INVALID_PID, "failed to find process by pid"},
            {LIBPERF_ERR_INVALID_TASK_TYPE, "invalid task type"},
            {LIBPERF_ERR_INVALID_TIME, "invalid sampling time"},
            {LIBPERF_ERR_NO_PROC, "no such process"},
            {LIBPERF_ERR_KERNEL_NOT_SUPPORT, "current pmu task is not supported by kernel"},
            {LIBPERF_ERR_TOO_MANY_FD, "too many open files"},
            {LIBPERF_ERR_RAISE_FD, "failed to setrlimit or getrlimit"},
            {LIBPERF_ERR_PATH_INACCESSIBLE, "cannot access file path"},
            {LIBPERF_ERR_INVALID_SAMPLE_RATE, "invalid sample rate, please check /proc/sys/kernel/perf_event_max_sample_rate"},
    };

    ProfErrorObj ProfErrorObj::profErrorObj;

    ProfErrorObj& ProfErrorObj::GetInstance()
    {
        return profErrorObj;
    }

    void ProfErrorObj::SetProfError(const ProfError& profError)
    {
        this->profError = profError;
    }

    void New(int code)
    {
        auto findMsg = defaultMsg.find(code);
        if (findMsg != defaultMsg.end()) {
            New(code, findMsg->second);
        } else {
            New(code, "");
        }
    }

    void New(int code, const std::string& msg)
    {
        ProfError profError = std::make_shared<details::CodeStrMsgError>(code, msg);
        ProfErrorObj::GetInstance().SetProfError(profError);
    }

}  // namespace pcerr

int Perrorno()
{
    return pcerr::ProfErrorObj::GetInstance().GetProfError()->Code();
}

const char* Perror()
{
    return pcerr::ProfErrorObj::GetInstance().GetProfError()->Msg();
}