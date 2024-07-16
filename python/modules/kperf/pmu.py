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
    COUNTING = 0
    SAMPLING = 1
    SPE_SAMPLING = 2
    MAX_TASK_TYPE = 3


class PmuEventType:
    CORE_EVENT = 0
    UNCORE_EVENT = 1
    TRACE_EVENT = 2
    ALL_EVENT = 3


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


class SymbolMode:
    NO_SYMBOL_RESOLVE = 0  # <stack> in PmuData will be set to NULL.
    RESOLVE_ELF = 1        # Resolve elf only. Fields except lineNum and fileName in Symbol will be valid.
    RESOLVE_ELF_DWARF = 2  # Resolve elf and dwarf. All fields in Symbol will be valid.


class PmuAttr(_libkperf.PmuAttr):

    def __init__(self,
                 evtList: List[str] = None,
                 pidList: List[int] = None,
                 cpuList: List[int] = None,
                 sampleRate: int = 0,
                 useFreq: bool = False,
                 excludeUser: bool = False,
                 excludeKernel: bool = False,
                 symbolMode: int = 0,
                 callStack: bool = False,
                 dataFilter: int = 0,
                 evFilter: int = 0,
                 minLatency: int = 0) -> None:
        super().__init__(
            evtList=evtList,
            pidList=pidList,
            cpuList=cpuList,
            sampleRate=sampleRate,
            useFreq=useFreq,
            excludeUser=excludeUser,
            excludeKernel=excludeKernel,
            symbolMode=symbolMode,
            callStack=callStack,
            dataFilter=dataFilter,
            evFilter=evFilter,
            minLatency=minLatency
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


def open(collect_type: PmuTaskType, pmu_attr: PmuAttr) -> int:
    return _libkperf.PmuOpen(int(collect_type), pmu_attr)


def event_list(event_type: PmuEventType)-> Iterator[str]:
    return _libkperf.PmuEventList(int(event_type))


def enable(pd: int)-> int:
    return _libkperf.PmuEnable(pd)


def disable(pd: int)-> int:
    return _libkperf.PmuDisable(pd)


def read(pd: int) -> PmuData:
    return _libkperf.PmuRead(pd)


def stop(pd: int) -> None:
    return _libkperf.PmuStop(pd)


def close(pd: int) -> None:
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


__all__ = [
    'PmuTaskType',
    'PmuEventType',
    'SpeFilter',
    'SpeEventFilter',
    'SymbolMode',
    'PmuAttr',
    'CpuTopology',
    'PmuDataExt',
    'SampleRawField',
    'ImplPmuData',
    'PmuData',
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
]
