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
#include <queue>
#include <mutex>
#include "pcerrc.h"
#include "pcerr.h"

namespace pcerr {
    static std::unordered_map<int, std::string> defaultMsg = {
            {SUCCESS, "success"},
            {COMMON_ERR_NOMEM, "not enough memory"},
            {COMMON_ERR_WRITE, "failed to write file"},
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
            {LIBPERF_ERR_COUNT_OVERFLOW, "pmu count is overflowed"},
            {LIBPERF_ERR_INVALID_GROUP_SPE, "event group not support SPE_SAMPLING collect mode"},
            {LIBPERF_ERR_INVALID_GROUP_ALL_UNCORE, "event group not support all events is uncore event"},
            {LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER, "invalid value for branchSampleFilter, use the correct accumulated bits value of BranchSampleFilter"},
            {LIBPERF_ERR_BRANCH_JUST_SUPPORT_SAMPLING, "branch filter just support sampling mode"},
            {LIBPERF_ERR_RESET_FD, "failed to reset fd output"},
            {LIBPERF_ERR_SET_FD_RDONLY_NONBLOCK, "failed to set fd readonly and nonbolock"},
            {LIBPERF_ERR_INTERFACE_NOT_SUPPORT_X86, "the current interface does not support x86"},
            {LIBPERF_ERR_INVALID_CGROUP_LIST, "invalid cgroup name list"},
            {LIBPERF_ERR_NOT_SUPPORT_PMU_FILE, "writing perf.data is not supported"},
            {LIBPERF_ERR_INVALID_PMU_FILE, "invalid pmu file handler"},
            {LIBPERF_ERR_OPEN_INVALID_FILE, "failed to open file"},
            {LIBPERF_ERR_INVALID_EVTATTR, "invalid evtAttr list"},
            {LIBPERF_ERR_COUNT_MMAP_IS_NULL, "Count mmap page is null!"},
            {LIBPERF_ERR_ENABLE_USER_ACCESS_FAILED, "Enable user access failed!"},
            {LIBPERF_ERR_ALLOCATE_REGISTER_FAILED, "Allocate register failed!"},
            {LIBPERF_ERR_CHECK_USER_ACCESS, "Check user access failed!"}
    };
    static std::unordered_map<int, std::string> warnMsgs = {
            {LIBPERF_WARN_CTXID_LOST, "Some SPE context packets are not found in the traces."},
            {LIBPERF_WARN_INVALID_GROUP_HAS_UNCORE, "event group has uncore event, cann`t event group, disabling event group"}
    };
    static std::unordered_map<int, std::queue<std::string>> customErrMsgs;
    static int warnCode = SUCCESS;
    static std::string warnMsg = "";
    static int errCode = SUCCESS;
    static std::string errMsg = "";
    static std::mutex errMutex;
    static std::mutex warnMutex;

    static std::string GetCustomMsg(int code) {
        std::string msg;
        auto it = customErrMsgs.find(code);
        if (it != customErrMsgs.end() && !it->second.empty()) {
            msg = it->second.front();
            it->second.pop();
        }
        return msg;
    }

    void New(int code)
    {
        std::string customMsg = GetCustomMsg(code);
        if (!customMsg.empty()) {
            New(code, customMsg);
            return;
        }
        auto findMsg = defaultMsg.find(code);
        if (findMsg != defaultMsg.end()) {
            New(code, findMsg->second);
        } else {
            New(code, "");
        }
    }

    void New(int code, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(errMutex);
        errCode = code;
        errMsg = msg;
    }

    void SetWarn(int warn)
    {
        auto findMsg = warnMsgs.find(warn);
        if (findMsg != warnMsgs.end()) {
            SetWarn(warn, findMsg->second);
        } else {
            SetWarn(warn, "");
        }
    }

    void SetWarn(int code, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(warnMutex);
        warnCode = code;
        warnMsg = msg;
    }

    void SetCustomErr(int code, const std::string &msg)
    {
        auto it = customErrMsgs.find(code);
        if (it != customErrMsgs.end()) {
            it->second.push(msg);
        } else {
            std::queue<std::string> errList;
            errList.push(msg);
            customErrMsgs.insert({code, errList});
        }
        New(code, msg);
    }
}  // namespace pcerr

int Perrorno()
{
    return pcerr::errCode;
}

const char* Perror()
{
    return pcerr::errMsg.c_str();
}

int GetWarn()
{
    return pcerr::warnCode;
}

const char* GetWarnMsg()
{
    return pcerr::warnMsg.c_str();
}
