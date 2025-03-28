"""
Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
libkperf licensed under the Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
    http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BA
SIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
PURPOSE.
See the Mulan PSL v2 for more details.
Author: Victor Jin
Create: 2024-05-16
Description: kperf pmu module
"""
from typing import List, Iterator
from ctypes import *

import _libkperf
import ksym

class PmuTaskType:
    COUNTING = 0        # pmu counting task
    SAMPLING = 1        # pmu sampling task
    SPE_SAMPLING = 2    # spe sampling task
    MAX_TASK_TYPE = 3


class PmuEventType:
    CORE_EVENT = 0
    UNCORE_EVENT = 1
    TRACE_EVENT = 2
    ALL_EVENT = 3


class PmuTraceType:
    TRACE_SYS_CALL = 0

class SpeFilter:
    SPE_FILTER_NONE = 0
    TS_ENABLE = 1 << 0       # enable timestamping with value of generic timer
    PA_ENABLE = 1 << 1       # collect physical address (as well as VA) of loads/stores
    PCT_ENABLE = 1 << 2      # collect physical timestamp instead of virtual timestamp
    JITTER = 1 << 16         # use jitter to avoid resonance when sampling
    BRANCH_FILTER = 1 << 32  # collect branches only
    LOAD_FILTER = 1 << 33    # collect loads only
    STORE_FILTER = 1 << 34   # collect stores only
    SPE_DATA_ALL = TS_ENABLE | PA_ENABLE | PCT_ENABLE | JITTER | BRANCH_FILTER | LOAD_FILTER | STORE_FILTER


class SpeEventFilter:
    SPE_EVENT_NONE = 0
    SPE_EVENT_RETIRED = 0x2        # instruction retired
    SPE_EVENT_L1DMISS = 0x8        # L1D refill
    SPE_EVENT_TLB_WALK = 0x20      # TLB refill
    SPE_EVENT_MISPREDICTED = 0x80  # mispredict


class SpeEvent:
    SPE_EV_EXCEPT       = 1 << 0
    SPE_EV_RETIRED      = 1 << 1
    SPE_EV_L1D_ACCESS   = 1 << 2
    SPE_EV_L1D_REFILL   = 1 << 3
    SPE_EV_TLB_ACCESS   = 1 << 4
    SPE_EV_TLB_WALK     = 1 << 5
    SPE_EV_NOT_TAKEN    = 1 << 6
    SPE_EV_MISPRED      = 1 << 7
    SPE_EV_LLC_ACCESS   = 1 << 8
    SPE_EV_LLC_MISS     = 1 << 9
    SPE_EV_REMOTE_ACCESS= 1 << 10
    SPE_EV_ALIGNMENT    = 1 << 11
    SPE_EV_PARTIAL_PRED = 1 << 17
    SPE_EV_EMPTY_PRED   = 1 << 18


class BranchSampleFilter:
    KPERF_NO_BRANCH_SAMPLE         = 0
    """
    The first part of the value is the privilege level,which is a combination of 
    one of the values listed below. If the user does not set privilege level explicitly,
    the kernel will use the event's privilege level.Event and branch privilege levels do
    not have to match.
    """
    KPERF_SAMPLE_BRANCH_USER        = 1 << 0
    KPERF_SAMPLE_BRANCH_KERNEL      = 1 << 1
    KPERF_SAMPLE_BRANCH_HV          = 1 << 2
    # In addition to privilege value , at least one or more of the following bits must be set.
    KPERF_SAMPLE_BRANCH_ANY         = 1 << 3
    KPERF_SAMPLE_BRANCH_ANY_CALL    = 1 << 4
    KPERF_SAMPLE_BRANCH_ANY_RETURN  = 1 << 5
    KPERF_SAMPLE_BRANCH_IND_CALL    = 1 << 6
    KPERF_SAMPLE_BRANCH_ABORT_TX    = 1 << 7
    KPERF_SAMPLE_BRANCH_IN_TX       = 1 << 8
    KPERF_SAMPLE_BRANCH_NO_TX       = 1 << 9
    KPERF_SAMPLE_BRANCH_COND        = 1 << 10
    KPERF_SAMPLE_BRANCH_CALL_STACK  = 1 << 11
    KPERF_SAMPLE_BRANCH_IND_JUMP    = 1 << 12
    KPERF_SAMPLE_BRANCH_CALL        = 1 << 13
    KPERF_SAMPLE_BRANCH_NO_FLAGES   = 1 << 14
    KPERF_SAMPLE_BRANCH_NO_CYCLES   = 1 << 15
    KPERF_SAMPLE_BRANCH_TYPE_SAVE   = 1 << 16


class SymbolMode:
    NO_SYMBOL_RESOLVE = 0  # <stack> in PmuData will be set to NULL.
    RESOLVE_ELF = 1        # Resolve elf only. Fields except lineNum and fileName in Symbol will be valid.
    RESOLVE_ELF_DWARF = 2  # Resolve elf and dwarf. All fields in Symbol will be valid.


class PmuAttr(_libkperf.PmuAttr):
    """
    Args:
        evtList: event list. Refer 'perf list' for details about event names.
            Both event names likes 'cycles' or event ids like 'r11' are allowed.
            For uncore events, event names should be of the form '<device>/<event>'
            For tracepoints, event names should be of the form '<system>:<event>'
            For spe sampling, this field should be NULL.
        pidList: pid list.
            For multi-threaded programs, all threads will be monitored regardless whether threads are created before or after PmuOpen.
            For multi-process programs, only processes created after PmuOpen are monitored.
            For short-lived programs, PmuOpen may fail and return error code.
            To collect system, set pidList to NULL and cpu cores will be monitored according to the field <cpuList>.
        cpuList: Core id list.
            If both <cpuList> and <pidList> are NULL, all processes on all cores will be monitored.
            If <cpuList> is NULL and <pidList> is not NULL, specified processes on all cores will be monitored.
            if both <cpuList> and <pidList> are not NULL, specified processes on specified cores will be monitored.
        evtAttr: event group id attributes.
            if not use event group function, this field will be NULL.
            if use event group function. please confirm the event group id with eveList is one by one.
            the same group id is the a event group. 
            Note: if the group id value is -1, it indicates that the event is not grouped.
        sampleRate: sample time enum.
            period enum: Sample period, only available for SAMPLING and SPE_SAMPLING.
            freq enum: Sample frequency, only available for SAMPLING.
        useFreq: use sample frequency or not.
            If set to 1, the previous union will be used as sample frequency,
            otherwise, it will be used as sample period.
        excludeUser: Don't count user.
        excludeKernel: Don't count kernel.
        symbolMode: This indicates how to analyze symbols of samples.
            Refer to  comments of SymbolMode.
        callStack: This indicates whether to collect whole callchains or only top frame.
        blockedSample: This indicates whether to enable blocked sampling.

        # SPE related fields:
        dataFilter: spe data filter. Refer to comments of SpeFilter.
        evFilter: spe event filter. Refer to comments of SpeEventFilter.
        minLatency: collect only samples with latency or higher.
        includeNewFork: In count mode, enable it you can get the new child thread count, default is disabled.
    """
    def __init__(self,
                 evtList: List[str] = None, 
                 pidList: List[int] = None,
                 cpuList: List[int] = None,
                 evtAttr: List[_libkperf.CtypesEvtAttr] = None,
                 sampleRate: int = 0,
                 useFreq: bool = False,
                 excludeUser: bool = False,
                 excludeKernel: bool = False,
                 symbolMode: int = 0,
                 callStack: bool = False,
                 blockedSample: bool = False,
                 dataFilter: int = 0,
                 evFilter: int = 0,
                 minLatency: int = 0,
                 includeNewFork: bool = False,
                 branchSampleFilter: int = 0) -> None:
        super().__init__(
            evtList=evtList,
            pidList=pidList,
            cpuList=cpuList,
            evtAttr=evtAttr,
            sampleRate=sampleRate,
            useFreq=useFreq,
            excludeUser=excludeUser,
            excludeKernel=excludeKernel,
            symbolMode=symbolMode,
            callStack=callStack,
            blockedSample=blockedSample,
            dataFilter=dataFilter,
            evFilter=evFilter,
            minLatency=minLatency,
            includeNewFork=includeNewFork,
            branchSampleFilter=branchSampleFilter,
        )


class CpuTopology(_libkperf.CpuTopology):
    pass


class SampleRawField(_libkperf.SampleRawField):
    pass


class SampleRawData(_libkperf.SampleRawData):
    pass


class PmuDataExt(_libkperf.PmuDataExt):
    pass


class ImplPmuData(_libkperf.ImplPmuData):
    pass


class PmuData(_libkperf.PmuData):
    pass

class PmuTraceAttr(_libkperf.PmuTraceAttr):
    """
    struct PmuTraceAttr {
        // system call function List, if funcs is nullptr, it will collect all the system call function elapsed time.
        const char **funcs;
        // Length of system call function list
        unsigned numFuncs;
        int* pidList;
        unsigned numPid;
        int* cpuList;
        unsigned numCpu;
    };
    """
    def __init__(self,
                 funcs: List[str] = None,
                 pidList: List[int] = None,
                 cpuList: List[int] = None) -> None:
        super().__init__(
            funcs=funcs,
            pidList=pidList,
            cpuList=cpuList
        )

class ImplPmuTraceData(_libkperf.ImplPmuTraceData):
    pass

class PmuTraceData(_libkperf.PmuTraceData):
    pass

def open(collect_type: PmuTaskType, pmu_attr: PmuAttr) -> int:
    """
    Initialize the collection target.
    On success, a task id is returned which is the unique identifier of the task.
    On error, -1 is returned.
    Refer to comments of PmuAttr for details about setting.
    :param collect_type: task type
    :param pmu_attr: settings of the current task
    :return: task id
    """
    return _libkperf.PmuOpen(int(collect_type), pmu_attr)


def event_list(event_type: PmuEventType)-> Iterator[str]:
    """
    Query all available event from system.
    :param event_type: type of event chosen by user
    :return: event list
    """
    return _libkperf.PmuEventList(int(event_type))


def enable(pd: int)-> int:
    """
    Enable counting or sampling of task <pd>.
    On success, 0 is returned.
    On error, -1 is returned.
    :param pd: task id
    :return: error code
    """
    return _libkperf.PmuEnable(pd)


def disable(pd: int)-> int:
    """
    Disable counting or sampling of task <pd>.
    On success, 0 is returned.
    On error, -1 is returned.
    :param pd: task id
    :return: error code
    """
    return _libkperf.PmuDisable(pd)


def read(pd: int) -> PmuData:
    """
    Collect data.
    Pmu data are collected starting from the last PmuEnable or PmuRead.
    That is to say, for COUNTING, counts of all pmu event are reset to zero in PmuRead.
    For SAMPLING and SPE_SAMPLING, samples collected are started from the last PmuEnable or PmuRead.
    :param pd: task id
    :return: pmu data
    """
    return _libkperf.PmuRead(pd)


def stop(pd: int) -> None:
    """
    stop a sampling task in asynchronous mode
    :param pd: task id.
    """
    return _libkperf.PmuStop(pd)


def close(pd: int) -> None:
    """
    Close task with id <pd>.
    After PmuClose is called, all pmu data related to the task become invalid.
    :param pd: task id
    """
    return _libkperf.PmuClose(pd)


def dump(pmuData: PmuData, filepath: str, dump_dwf: int) -> None:
    """
    /**
    Dump pmu data to a specific file.
    If file exists, then data will be appended to file.
    If file does not exist, then file will be created.
    Dump format: comm pid tid cpu period evt count addr symbolName offset module fileName lineNum
    :param pmuData: data list.
    :param filepath: path of the output file.
    :param dump_dwf: if 0, source file and line number of symbols will not be dumped, otherwise, they will be dumped to file.
    :return: None
    """
    return _libkperf.PmuDumpData(pmuData, filepath, dump_dwf)


def get_field(pmu_data: _libkperf.ImplPmuData, field_name: str, value: c_void_p) -> int:
    """
    get field value of trace pointer named field_name
    :param pmu_data: _libkperf.ImplPmuData
    :param field_name: field name
    :param value: pointer
    :return: 0 success other failed
    """
    return _libkperf.PmuGetField(pmu_data.rawData.c_pmu_data_rawData, field_name, value, sizeof(value))


def get_field_exp(pmu_data: _libkperf.ImplPmuData, field_name: str) -> SampleRawField:
    """
    get the field detail of trace pointer event
    :param pmu_data: the _libkperf.ImplPmuData
    :param field_name: field name
    :return:SampleRawField
    """
    return _libkperf.PmuGetFieldExp(pmu_data.rawData.c_pmu_data_rawData, field_name)

def trace_open(trace_type: PmuTraceType, pmu_trace_attr: PmuTraceAttr) -> int:
    """
    int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);
    """
    return _libkperf.PmuTraceOpen(int(trace_type), pmu_trace_attr)

def trace_enable(pd: int) -> int:
    """
    int PmuTraceEnable(int pd);
    """
    return _libkperf.PmuTraceEnable(pd)

def trace_disable(pd: int) -> int:
    """
    int PmuTraceDisable(int pd);
    """
    return _libkperf.PmuTraceDisable(pd)

def trace_read(pd: int) -> PmuTraceData:
    """
    int PmuTraceRead(int pd, struct PmuTraceData **traceData);
    """
    return _libkperf.PmuTraceRead(pd)

def trace_close(pd: int) -> None:
    """
    void PmuTraceClose(int pd);
    """
    return _libkperf.PmuTraceClose(pd)

def sys_call_func_list() -> Iterator[str]:
    """
    get the system call function list
    :return: system call function list
    """
    return _libkperf.PmuSysCallFuncList()

__all__ = [
    'PmuTaskType',
    'PmuEventType',
    'PmuTraceType',
    'SpeFilter',
    'SpeEventFilter',
    'SymbolMode',
    'PmuAttr',
    'CpuTopology',
    'PmuDataExt',
    'SampleRawField',
    'ImplPmuData',
    'PmuData',
    'PmuTraceAttr',
    'ImplPmuTraceData',
    'PmuTraceData',
    'open',
    'event_list',
    'enable',
    'disable',
    'read',
    'stop',
    'close',
    'dump',
    'get_field',
    'get_field_exp',
    'trace_open',
    'trace_enable',
    'trace_disable',
    'trace_read',
    'trace_close',
    'sys_call_func_list',
    'BranchSampleFilter',
]
