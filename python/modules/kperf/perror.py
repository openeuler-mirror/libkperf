"""
Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
libkperf licensed under the Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
    http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
PURPOSE.
See the Mulan PSL v2 for more details.
Author: Victor Jin
Create: 2024-05-16
Description: kperf perror module
"""
import _libkperf


class Error:
    # default code
    SUCCESS = 0
    COMMON_ERR_NOMEM = 1  # not enough memory

    # libsym 100-1000
    LIBSYM_ERR_BASE = 100
    LIBSYM_ERR_KALLSYMS_INVALID = LIBSYM_ERR_BASE
    LIBSYM_ERR_DWARF_FORMAT_FAILED = 101
    LIBSYM_ERR_ELFIN_FOMAT_FAILED = 102
    LIBSYM_ERR_OPEN_FILE_FAILED = 103
    LIBSYM_ERR_NOT_FIND_PID = 104
    LIBSYM_ERR_MAP_ADDR_MODULE_FAILED = 105
    LIBSYM_ERR_MAP_KERNAL_ADDR_FAILED = 106
    LIBSYM_ERR_PARAM_PID_INVALID = 107
    LIBSYM_ERR_STRCPY_OPERATE_FAILED = 108
    LIBSYM_ERR_SNPRINF_OPERATE_FAILED = 109
    LIBSYM_ERR_MAP_CODE_KERNEL_NOT_SUPPORT = 110
    LIBSYM_ERR_MAP_CODE_FIND_ELF_FAILED = 111
    LIBSYM_ERR_CMD_OPERATE_FAILED = 112
    LIBSYM_ERR_FILE_NOT_RGE = 113
    LIBSYM_ERR_START_SMALLER_END = 114
    LIBSYM_ERR_STOUL_OPERATE_FAILED = 115
    LIBSYM_ERR_FILE_INVALID = 116
    # libperf = 1000-3000
    LIBPERF_ERR_NO_AVAIL_PD = 1000
    LIBPERF_ERR_CHIP_TYPE_INVALID = 1001
    LIBPERF_ERR_FAIL_LISTEN_PROC = 1002
    LIBPERF_ERR_INVALID_CPULIST = 1003
    LIBPERF_ERR_INVALID_PIDLIST = 1004
    LIBPERF_ERR_INVALID_EVTLIST = 1005
    LIBPERF_ERR_INVALID_PD = 1006
    LIBPERF_ERR_INVALID_EVENT = 1007
    LIBPERF_ERR_SPE_UNAVAIL = 1008
    LIBPERF_ERR_FAIL_GET_CPU = 1009
    LIBPERF_ERR_FAIL_GET_PROC = 1010
    LIBPERF_ERR_NO_PERMISSION = 1011
    LIBPERF_ERR_DEVICE_BUSY = 1012
    LIBPERF_ERR_DEVICE_INVAL = 1013
    LIBPERF_ERR_FAIL_MMAP = 1014
    LIBPERF_ERR_FAIL_RESOLVE_MODULE = 1015
    LIBPERF_ERR_KERNEL_NOT_SUPPORT = 1016
    LIBPERF_ERR_INVALID_METRIC_TYPE = 1017
    LIBPERF_ERR_INVALID_PID = 1018
    LIBPERF_ERR_INVALID_TASK_TYPE = 1019
    LIBPERF_ERR_INVALID_TIME = 1020
    LIBPERF_ERR_NO_PROC = 1021
    LIBPERF_ERR_TOO_MANY_FD = 1022
    LIBPERF_ERR_RAISE_FD = 1023
    LIBPERF_ERR_INVALID_PMU_DATA = 1024
    LIBPERF_ERR_FAILED_PMU_ENABLE = 1025
    LIBPERF_ERR_FAILED_PMU_DISABLE = 1026
    LIBPERF_ERR_FAILED_PMU_RESET = 1027
    LIBPERF_ERR_NOT_OPENED = 1028
    LIBPERF_ERR_QUERY_EVENT_TYPE_INVALID = 1029
    LIBPERF_ERR_QUERY_EVENT_LIST_FAILED = 1030
    LIBPERF_ERR_PATH_INACCESSIBLE = 1031
    LIBPERF_ERR_INVALID_SAMPLE_RATE = 1032
    LIBPERF_ERR_INVALID_FIELD_ARGS = 1033
    LIBPERF_ERR_FIND_FIELD_LOSS = 1034
    LIBPERF_ERR_COUNT_OVERFLOW = 1035
    LIBPERF_ERR_INVALID_GROUP_SPE = 1036
    LIBPERF_ERR_INVALID_GROUP_ALL_UNCORE = 1037
    LIBPERF_ERR_INVALID_TRACE_TYPE = 1038
    LIBPERF_ERR_INVALID_SYSCALL_FUN = 1039
    LIBPERF_ERR_QUERY_SYSCALL_LIST_FAILED = 1040
    LIBPERF_ERR_OPEN_SYSCALL_HEADER_FAILED = 1041
    LIBPERF_ERR_INVALID_BRANCH_SAMPLE_FILTER = 1042
    LIBPERF_ERR_BRANCH_JUST_SUPPORT_SAMPLING = 1043
    LIBPERF_ERR_RESET_FD = 1044
    LIBPERF_ERR_SET_FD_RDONLY_NONBLOCK = 1045
    LIBPERF_ERR_NOT_SUPPORT_CONFIG_EVENT = 1046
    LIBPERF_ERR_INVALID_BLOCKED_SAMPLE = 1047
    LIBPERF_ERR_NOT_SUPPORT_GROUP_EVENT = 1048
    LIBPERF_ERR_NOT_SUPPORT_SYSTEM_SAMPLE = 1049
    LIBPERF_ERR_INVALID_PMU_DEVICES_METRIC = 1050
    LIBPERF_ERR_INVALID_PMU_DEVICES_BDF = 1051
    LIBPERF_ERR_OPEN_INVALID_FILE = 1052
    LIBPERF_ERR_INVALID_BDF_VALUE = 1053
    LIBPERF_ERR_NOT_SOUUPUT_PCIE_BDF = 1054
    LIBPERF_ERR_NOT_SOUUPUT_SMMU_BDF = 1055
    LIBPERF_ERR_INVALID_IOSMMU_DIR = 1056
    LIBPERF_ERR_INVALID_SMMU_NAME = 1057
    LIBPERF_ERR_NOT_SOUUPUT_PCIE_COUNTING = 1058
    LIBPERF_ERR_OPEN_PCI_FILE_FAILD = 1059
    LIBPERF_ERR_INVALID_MTRIC_PARAM = 1060
    LIBPERF_ERR_PMU_DEVICES_NULL = 1061
    LIBPERF_ERR_CPUFREQ_NOT_CONFIG = 1062

    UNKNOWN_ERROR = 9999

    # warning code
    LIBPERF_WARN_CTXID_LOST = 1000
    LIBPERF_WARN_FAIL_GET_PROC = 1001
    LIBPERF_WARN_INVALID_GROUP_HAS_UNCORE = 1002
    LIBPERF_WARN_PCIE_BIOS_NOT_NEWEST = 1003
    LIBPERF_WARN_INVALID_SMMU_BDF = 1004

def errorno() -> int:
    """
    Obtaining error codes
    """
    return _libkperf.Perrorno()


def error()-> str:
    """
    Obtaining Error Information
    """
    return _libkperf.Perror()


def get_warn() -> int:
    """
    Get warning codes
    """
    return _libkperf.GetWarn()


def get_warn_msg()-> str:
    """
    Get warning message
    """
    return _libkperf.GetWarnMsg()


__all__ = [
    'Error',
    'errorno',
    'error',
    'get_warn',
    'get_warn_msg'
]
