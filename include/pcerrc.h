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
 * Author: Mr.Gan
 * Create: 2024-04-03
 * Description: global error codes of perf.so
 ******************************************************************************/
#ifndef PCERRC_H
#define PCERRC_H
#ifdef __cplusplus
extern "C" {
#endif
// default code
#define SUCCESS 0
#define COMMON_ERR_NOMEM 1  // not enough memory

// libsym 100-1000
#define LIBSYM_ERR_BASE 100
#define LIBSYM_ERR_KALLSYMS_INVALID LIBSYM_ERR_BASE
#define LIBSYM_ERR_DWARF_FORMAT_FAILED 101
#define LIBSYM_ERR_ELFIN_FOMAT_FAILED 102
#define LIBSYM_ERR_OPEN_FILE_FAILED 103
#define LIBSYM_ERR_NOT_FIND_PID 104
#define LIBSYM_ERR_MAP_ADDR_MODULE_FAILED 105
#define LIBSYM_ERR_MAP_KERNAL_ADDR_FAILED 106
#define LIBSYM_ERR_PARAM_PID_INVALID 107
#define LIBSYM_ERR_STRCPY_OPERATE_FAILED 108
#define LIBSYM_ERR_SNPRINF_OPERATE_FAILED 109
#define LIBSYM_ERR_MAP_CODE_KERNEL_NOT_SUPPORT 110
#define LIBSYM_ERR_MAP_CODE_FIND_ELF_FAILED 111
#define LIBSYM_ERR_CMD_OPERATE_FAILED 112
#define LIBSYM_ERR_FILE_NOT_RGE 113
#define LIBSYM_ERR_START_SMALLER_END 114
#define LIBSYM_ERR_STOUL_OPERATE_FAILED 115
#define LIBSYM_ERR_FILE_INVALID 116
// libperf 1000-3000
#define LIBPERF_ERR_NO_AVAIL_PD 1000
#define LIBPERF_ERR_CHIP_TYPE_INVALID 1001
#define LIBPERF_ERR_FAIL_LISTEN_PROC 1002
#define LIBPERF_ERR_INVALID_CPULIST 1003
#define LIBPERF_ERR_INVALID_PIDLIST 1004
#define LIBPERF_ERR_INVALID_EVTLIST 1005
#define LIBPERF_ERR_INVALID_PD 1006
#define LIBPERF_ERR_INVALID_EVENT 1007
#define LIBPERF_ERR_SPE_UNAVAIL 1008
#define LIBPERF_ERR_FAIL_GET_CPU 1009
#define LIBPERF_ERR_FAIL_GET_PROC 1010
#define LIBPERF_ERR_NO_PERMISSION 1011
#define LIBPERF_ERR_DEVICE_BUSY 1012
#define LIBPERF_ERR_DEVICE_INVAL 1013
#define LIBPERF_ERR_FAIL_MMAP 1014
#define LIBPERF_ERR_FAIL_RESOLVE_MODULE 1015
#define LIBPERF_ERR_KERNEL_NOT_SUPPORT 1016
#define LIBPERF_ERR_INVALID_METRIC_TYPE 1017
#define LIBPERF_ERR_INVALID_PID 1018
#define LIBPERF_ERR_INVALID_TASK_TYPE 1019
#define LIBPERF_ERR_INVALID_TIME 1020
#define LIBPERF_ERR_NO_PROC 1021
#define LIBPERF_ERR_TOO_MANY_FD 1022
#define LIBPERF_ERR_RAISE_FD 1023
#define LIBPERF_ERR_INVALID_PMU_DATA 1024
#define LIBPERF_ERR_FAILED_PMU_ENABLE 1025
#define LIBPERF_ERR_FAILED_PMU_DISABLE 1026
#define LIBPERF_ERR_FAILED_PMU_RESET 1027
#define LIBPERF_ERR_NOT_OPENED 1028
#define LIBPERF_ERR_QUERY_EVENT_TYPE_INVALID 1029
#define LIBPERF_ERR_QUERY_EVENT_LIST_FAILED 1030
#define LIBPERF_ERR_PATH_INACCESSIBLE 1031
#define LIBPERF_ERR_INVALID_SAMPLE_RATE 1032

#define UNKNOWN_ERROR 9999

// warnning code
#define LIBPERF_WARN_CTXID_LOST 1000


/**
* @brief Obtaining error codes
*/
int Perrorno();

/**
* @brief Obtaining Error Information
*/
const char* Perror();

/**.
 * @brief Get warnning codes
*/
int GetWarn();

/**
 * @brief Get warning message.
*/
const char* GetWarnMsg();

#ifdef __cplusplus
}
#endif
#endif
