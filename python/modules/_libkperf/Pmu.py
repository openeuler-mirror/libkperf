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
Create: 2024-05-10
Description: ctype python Pmu module
"""
import ctypes

from typing import List, Any, Iterator
from .Config import UTF_8, kperf_so
from .Symbol import CtypesStack, Stack


class SampleRateUnion(ctypes.Union):
    _fields_ = [
        ('period', ctypes.c_uint),
        ('freq',   ctypes.c_uint)
    ]

class CtypesEvtAttr(ctypes.Structure):
    """
    struct EvtAttr {
        int group_id;
    };
    """
    _fields_ = [('group_id', ctypes.c_int)]

    def __init__(self, group_id: int=0, *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.group_id = ctypes.c_int(group_id)

class EvtAttr:
    __slots__ = ['__c_evt_attr']

    def __init__(self, group_id: int=0) -> None:
        self.__c_evt_attr = CtypesEvtAttr(group_id)

    @property
    def c_evt_attr(self) -> CtypesEvtAttr:
        return self.__c_evt_attr
    
    @property
    def group_id(self) -> int:
        return int(self.c_evt_attr.group_id)
    
    @group_id.setter
    def group_id(self, group_id: int) -> None:
        self.c_evt_attr.group_id = ctypes.c_int(group_id)

    @classmethod
    def from_c_evt_attr(cls, c_evt_attr: CtypesEvtAttr) -> 'EvtAttr':
        evt_attr = cls()
        evt_attr.__c_evt_attr = c_evt_attr
        return evt_attr


class CtypesPmuAttr(ctypes.Structure):
    """
    struct PmuAttr {
        char** evtList;                 // event list
        unsigned numEvt;                // length of event list
        int* pidList;                   // pid list
        unsigned numPid;                // length of pid list
        int* cpuList;                   // cpu id list
        int numCpu;                // length of cpu id list

        struct EvtAttr *evtAttr;        // events group id info

        union {
            unsigned period;            // sample period
            unsigned freq;              // sample frequency
        };
        unsigned useFreq : 1;
        unsigned excludeUser : 1;     // don't count user
        unsigned excludeKernel : 1;   //  don't count kernel
        enum SymbolMode symbolMode;     // refer to comments of SymbolMode
        unsigned callStack : 1;   //  collect complete call stack
        unsigned blockedSample : 1;   //  enable blocked sample
        // SPE related fields.
        enum SpeFilter dataFilter;      // spe data filter
        enum SpeEventFilter evFilter;   // spe event filter
        unsigned long minLatency;       // collect only samples with latency or higher
        unsigned includeNewFork : 1;  // include new fork thread
    };
    """

    _fields_ = [
        ('evtList',       ctypes.POINTER(ctypes.c_char_p)),
        ('numEvt',        ctypes.c_uint),
        ('pidList',       ctypes.POINTER(ctypes.c_int)),
        ('numPid',        ctypes.c_uint),
        ('cpuList',       ctypes.POINTER(ctypes.c_int)),
        ('numCpu',        ctypes.c_uint),
        ('evtAttr',       ctypes.POINTER(CtypesEvtAttr)),
        ('sampleRate',    SampleRateUnion),
        ('useFreq',       ctypes.c_uint, 1),
        ('excludeUser',   ctypes.c_uint, 1),
        ('excludeKernel', ctypes.c_uint, 1),
        ('symbolMode',    ctypes.c_uint),
        ('callStack',     ctypes.c_uint, 1),
        ('blockedSample',     ctypes.c_uint, 1),
        ('dataFilter',    ctypes.c_uint64),  # The enumeration for dataFilter will use 64 bits
        ('evFilter',      ctypes.c_uint),
        ('minLatency',    ctypes.c_ulong),
        ('includeNewFork', ctypes.c_uint, 1),
        ('branchSampleFilter', ctypes.c_ulong),
    ]

    def __init__(self,
                 evtList: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None,
                 evtAttr: List[int]=None,
                 sampleRate: int=0,
                 useFreq: bool=False,
                 excludeUser: bool=False,
                 excludeKernel: bool=False,
                 symbolMode: int=0,
                 callStack: bool=False,
                 blockedSample: bool=False,
                 dataFilter: int=0,
                 evFilter: int=0,
                 minLatency: int=0,
                 includeNewFork: bool=False,
                 branchSampleFilter: int=0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)

        if evtList:
            numEvt = len(evtList)
            self.evtList = (ctypes.c_char_p * numEvt)(*[evt.encode(UTF_8) for evt in evtList])
            self.numEvt = ctypes.c_uint(numEvt)
        else:
            self.evtList = None
            self.numEvt = ctypes.c_uint(0)

        if pidList:
            numPid = len(pidList)
            self.pidList = (ctypes.c_int * numPid)(*pidList)
            self.numPid = ctypes.c_uint(numPid)
        else:
            self.pidList = None
            self.numPid = ctypes.c_uint(0)

        if cpuList:
            numCpu = len(cpuList)
            self.cpuList = (ctypes.c_int * numCpu)(*cpuList)
            self.numCpu = ctypes.c_uint(numCpu)
        else:
            self.cpuList = None
            self.numCpu = ctypes.c_uint(0)

        if not useFreq:
            self.sampleRate.period = ctypes.c_uint(sampleRate)
        else:
            self.sampleRate.freq = ctypes.c_uint(sampleRate)

        if evtAttr:
            numEvtAttr = len(evtAttr)
            self.evtAttr = (CtypesEvtAttr * numEvtAttr)(*[CtypesEvtAttr(evt) for evt in evtAttr])
        else:
            self.evtAttr = None

        self.symbolMode = ctypes.c_uint(symbolMode)
        self.dataFilter = ctypes.c_uint64(dataFilter)
        self.evFilter = ctypes.c_uint(evFilter)
        self.minLatency = ctypes.c_ulong(minLatency)
        self.branchSampleFilter = ctypes.c_ulong(branchSampleFilter)

        self.useFreq = useFreq
        self.excludeUser = excludeUser
        self.excludeKernel = excludeKernel
        self.callStack = callStack
        self.blockedSample = blockedSample
        self.includeNewFork = includeNewFork


class PmuAttr:
    __slots__ = ['__c_pmu_attr']

    def __init__(self,
                 evtList: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None,
                 evtAttr: List[CtypesEvtAttr]=None,
                 sampleRate: int=0,
                 useFreq: bool=False,
                 excludeUser: bool=False,
                 excludeKernel: bool=False,
                 symbolMode: int=0,
                 callStack: bool=False,
                 blockedSample: bool=False,
                 dataFilter: int=0,
                 evFilter: int=0,
                 minLatency: int=0,
                 includeNewFork: bool=False,
                 branchSampleFilter: int=0) -> None:
        self.__c_pmu_attr = CtypesPmuAttr(
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

    @property
    def c_pmu_attr(self) -> CtypesPmuAttr:
        return self.__c_pmu_attr

    @property
    def numEvt(self) -> int:
        return self.c_pmu_attr.numEvt

    @property
    def evtList(self) -> List[str]:
        return [self.c_pmu_attr.evtList[i].decode(UTF_8) for i in range(self.numEvt)]

    @evtList.setter
    def evtList(self, evtList: List[str]) -> None:
        if evtList:
            numEvt = len(evtList)
            self.c_pmu_attr.evtList = (ctypes.c_char_p * numEvt)(*[evt.encode(UTF_8) for evt in evtList])
            self.c_pmu_attr.numEvt = ctypes.c_uint(numEvt)
        else:
            self.c_pmu_attr.evtList = None
            self.c_pmu_attr.numEvt = ctypes.c_uint(0)

    @property
    def numPid(self) -> int:
        return self.c_pmu_attr.numPid

    @property
    def pidList(self) -> List[int]:
        return [self.c_pmu_attr.pidList[i] for i in range(self.numPid)]

    @pidList.setter
    def pidList(self, pidList: List[int]) -> None:
        if pidList:
            numPid = len(pidList)
            self.c_pmu_attr.pidList = (ctypes.c_int * numPid)(*[pid for pid in pidList])
            self.c_pmu_attr.numPid = ctypes.c_uint(numPid)
        else:
            self.c_pmu_attr.pidList = None
            self.c_pmu_attr.numPid = ctypes.c_uint(0)

    @property
    def evtAttr(self) -> List[CtypesEvtAttr]:
        return [self.c_pmu_attr.evtAttr[i] for i in range(len(self.c_pmu_attr.evtAttr))]

    @evtAttr.setter
    def evtAttr(self, evtAttr: List[CtypesEvtAttr]) -> None:
        if evtAttr:
            numEvtAttr = len(evtAttr)
            self.c_pmu_attr.evtAttr = (CtypesEvtAttr * numEvtAttr)(*[CtypesEvtAttr(evt) for evt in evtAttr])
        else:
            self.c_pmu_attr.evtAttr = None
    
    @property
    def numCpu(self) -> int:
        return self.c_pmu_attr.numCpu

    @property
    def cpuList(self) -> List[int]:
        return [self.c_pmu_attr.cpuList[i] for i in range(self.numCpu)]

    @cpuList.setter
    def cpuList(self, cpuList: List[int]) -> None:
        if cpuList:
            numCpu = len(cpuList)
            self.c_pmu_attr.cpuList = (ctypes.c_int * numCpu)(*[cpu for cpu in cpuList])
            self.c_pmu_attr.numCpu = ctypes.c_uint(numCpu)
        else:
            self.c_pmu_attr.cpuList = None
            self.c_pmu_attr.numCpu = ctypes.c_uint(0)

    @property
    def sampleRate(self) -> int:
        if not self.useFreq:
            return self.c_pmu_attr.sampleRate.period
        else:
            return self.c_pmu_attr.sampleRate.freq

    @sampleRate.setter
    def sampleRate(self, sampleRate: int) -> None:
        if not self.useFreq:
            self.c_pmu_attr.sampleRate.period = ctypes.c_uint(sampleRate)
        else:
            self.c_pmu_attr.sampleRate.freq = ctypes.c_uint(sampleRate)

    @property
    def useFreq(self) -> bool:
        return bool(self.c_pmu_attr.useFreq)

    @useFreq.setter
    def useFreq(self, useFreq: bool) -> None:
        self.c_pmu_attr.useFreq = int(useFreq)

    @property
    def excludeUser(self) -> bool:
        return bool(self.c_pmu_attr.excludeUser)

    @excludeUser.setter
    def excludeUser(self, excludeUser: bool) -> None:
        self.c_pmu_attr.excludeUser = int(excludeUser)

    @property
    def excludeKernel(self) -> bool:
        return bool(self.c_pmu_attr.excludeKernel)

    @excludeKernel.setter
    def excludeKernel(self, excludeKernel: bool) -> None:
        self.c_pmu_attr.excludeKernel = int(excludeKernel)

    @property
    def symbolMode(self) -> int:
        return self.c_pmu_attr.symbolMode

    @symbolMode.setter
    def symbolMode(self, symbolMode: int) -> None:
        self.c_pmu_attr.symbolMode = ctypes.c_uint(symbolMode)

    @property
    def callStack(self) -> bool:
        return bool(self.c_pmu_attr.callStack)

    @callStack.setter
    def callStack(self, callStack: bool) -> None:
        self.c_pmu_attr.callStack = int(callStack)

    @property
    def blockedSample(self) -> bool:
        return bool(self.c_pmu_attr.blockedSample)
    
    @blockedSample.setter
    def blockedSample(self, blockedSample: bool) -> None:
        self.c_pmu_attr.blockedSample = int(blockedSample)

    @property
    def dataFilter(self) -> int:
        return self.c_pmu_attr.dataFilter

    @dataFilter.setter
    def dataFilter(self, dataFilter: int) -> None:
        self.c_pmu_attr.dataFilter = ctypes.c_uint64(dataFilter)

    @property
    def evFilter(self) -> int:
        return self.c_pmu_attr.evFilter

    @evFilter.setter
    def evFilter(self, evFilter: int) -> None:
        self.c_pmu_attr.evFilter = ctypes.c_uint(evFilter)

    @property
    def minLatency(self) -> int:
        return self.c_pmu_attr.minLatency

    @minLatency.setter
    def minLatency(self, minLatency: int) -> None:
        self.c_pmu_attr.minLatency = ctypes.c_ulong(minLatency)

    @property
    def includeNewFork(self) -> bool:
        return bool(self.c_pmu_attr.includeNewFork)

    @includeNewFork.setter
    def includeNewFork(self, includeNewFork: bool) -> None:
        self.c_pmu_attr.includeNewFork = int(includeNewFork)
    
    @property
    def branchSampleFilter(self) -> int:
        return self.c_pmu_attr.branchSampleFilter

    @branchSampleFilter.setter
    def branchSampleFilter(self, branchSampleFilter: int) -> None:
        self.c_pmu_attr.branchSampleFilter = ctypes.c_ulong(branchSampleFilter)

    @classmethod
    def from_c_pmu_data(cls, c_pmu_attr: CtypesPmuAttr) -> 'PmuAttr':
        pmu_attr = cls()
        pmu_attr.__c_pmu_attr = c_pmu_attr
        return pmu_attr

class CtypesPmuDeviceAttr(ctypes.Structure):
    """
    struct PmuDeviceAttr {
        enum PmuDeviceMetric metric;
        char *bdf;
    };
    """
    _fields_ = [
        ('metric', ctypes.c_int),
        ('bdf', ctypes.c_char_p)
    ]
    
    def __init__(self,
                metric: int = 0,
                bdf: str = None,
                *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)

        self.metric = ctypes.c_int(metric)
        if bdf:
            self.bdf = ctypes.c_char_p(bdf.encode(UTF_8))
        else:
            self.bdf = None


class PmuDeviceAttr:
    __slots__ = ['__c_pmu_device_attr']

    def __init__(self,
                 metric: int = 0,
                 bdf: str = None) -> None:
        self.__c_pmu_device_attr = CtypesPmuDeviceAttr(
            metric=metric,
            bdf=bdf
        )

    @property
    def c_pmu_device_attr(self) -> CtypesPmuDeviceAttr:
        return self.__c_pmu_device_attr

    @property
    def metric(self) -> int:
        return self.c_pmu_device_attr.metric

    @metric.setter
    def metric(self, metric: int) -> None:
        self.c_pmu_device_attr.metric = ctypes.c_int(metric)

    @property
    def bdf(self) -> str:
        if self.c_pmu_device_attr.bdf:
            return self.c_pmu_device_attr.bdf.decode(UTF_8)
        return None

    @bdf.setter
    def bdf(self, bdf: str) -> None:
        if bdf:
            self.c_pmu_device_attr.bdf = ctypes.c_char_p(bdf.encode(UTF_8))
        else:
            self.c_pmu_device_attr.bdf = None

    @classmethod
    def from_c_pmu_device_attr(cls, c_pmu_device_attr: CtypesPmuDeviceAttr) -> 'PmuDeviceAttr':
        pmu_device_attr = cls()
        pmu_device_attr.__c_pmu_device_attr = c_pmu_device_attr
        return pmu_device_attr


class CtypesPmuDeviceData(ctypes.Structure):
    """
    struct PmuDeviceData {
        enum PmuDeviceMetric metric;
        double count;
        enum PmuMetricMode mode;
        union {
            unsigned coreId;
            unsigned numaId;
            unsigned clusterId;
            char *bdf;
        };
    };
    """
    class _Union(ctypes.Union):
        _fields_ = [
            ('coreId', ctypes.c_uint),
            ('numaId', ctypes.c_uint),
            ('clusterId', ctypes.c_uint),
            ('bdf', ctypes.c_char_p)
        ]
    
    _fields_ = [
        ('metric', ctypes.c_int),
        ('count', ctypes.c_double),
        ('mode', ctypes.c_int),
        ('_union', _Union)
    ]
    
    @property
    def coreId(self) -> int:
        if self.mode == 1:  # PMU_METRIC_CORE
            return self._union.coreId
        return 0
    
    @property
    def numaId(self) -> int:
        if self.mode == 2:  # PMU_METRIC_NUMA
            return self._union.numaId
        return 0
    
    @property
    def clusterId(self) -> int:
        if self.mode == 3:  # PMU_METRIC_CLUSTER
            return self._union.clusterId
        return 0
    
    @property
    def bdf(self) -> str:
        if self.mode == 4 and self._union.bdf:  # PMU_METRIC_BDF
            return self._union.bdf.decode(UTF_8)
        return ""


class ImplPmuDeviceData:
    __slots__ = ['__c_pmu_device_data']

    def __init__(self,
                 metric: int = 0,
                 count: float = 0,
                 mode: int = 0) -> None:
        self.__c_pmu_device_data = CtypesPmuDeviceData()
        self.__c_pmu_device_data.metric = ctypes.c_int(metric)
        self.__c_pmu_device_data.count = ctypes.c_double(count)
        self.__c_pmu_device_data.mode = ctypes.c_int(mode)
    
    @property
    def c_pmu_device_data(self) -> CtypesPmuDeviceData:
        return self.__c_pmu_device_data
    
    @property
    def metric(self) -> int:
        return self.c_pmu_device_data.metric
    
    @property
    def count(self) -> float:
        return self.c_pmu_device_data.count
    
    @property
    def mode(self) -> int:
        return self.c_pmu_device_data.mode
    
    @property
    def coreId(self) -> int:
        if self.mode == 1:  # PMU_METRIC_CORE
            return self.c_pmu_device_data._union.coreId
        return 0
    
    @property
    def numaId(self) -> int:
        if self.mode == 2:  # PMU_METRIC_NUMA
            return self.c_pmu_device_data._union.numaId
        return 0
    
    @property
    def clusterId(self) -> int:
        if self.mode == 3:  # PMU_METRIC_CLUSTER
            return self.c_pmu_device_data._union.clusterId
        return 0
    
    @property
    def bdf(self) -> str:
        if self.mode == 4 and self.c_pmu_device_data._union.bdf:  # PMU_METRIC_BDF
            return self.c_pmu_device_data._union.bdf.decode(UTF_8)
        return ""
    
    @classmethod
    def from_c_pmu_device_data(cls, c_pmu_device_data: CtypesPmuDeviceData) -> 'ImplPmuDeviceData':
        pmu_device_data = cls()
        pmu_device_data.__c_pmu_device_data = c_pmu_device_data
        return pmu_device_data


class PmuDeviceData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer: ctypes.POINTER(CtypesPmuDeviceData) = None, len: int = 0) -> None:
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuDeviceData.from_c_pmu_device_data(self.__pointer[i]) for i in range(self.__len))
    
    def __del__(self) -> None:
        self.free()
    
    def __len__(self) -> int:
        return self.__len
    
    def __getitem__(self, index):
        if index < 0 or index >= self.__len:
            raise IndexError("index out of range")
        return ImplPmuDeviceData.from_c_pmu_device_data(self.__pointer[index])
    
    @property
    def len(self) -> int:
        return self.__len
    
    @property
    def iter(self) -> Iterator[ImplPmuDeviceData]:
        return self.__iter
    
    def free(self) -> None:
        if self.__pointer is not None:
            DevDataFree(self.__pointer)
            self.__pointer = None


class CtypesPmuTraceAttr(ctypes.Structure):
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
    _fields_ = [
        ('funcs',       ctypes.POINTER(ctypes.c_char_p)),
        ('numFuncs',    ctypes.c_uint),
        ('pidList',     ctypes.POINTER(ctypes.c_int)),
        ('numPid',      ctypes.c_uint),
        ('cpuList',     ctypes.POINTER(ctypes.c_int)),
        ('numCpu',      ctypes.c_uint),
    ]

    def __init__(self,
                 funcs: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None,
                *args: Any, **kw:Any) -> None:
        super().__init__(*args, **kw)

        if funcs:
            numFuncs = len(funcs)
            self.funcs = (ctypes.c_char_p * numFuncs)(*[func.encode(UTF_8) for func in funcs])
            self.numFuncs = ctypes.c_uint(numFuncs)
        else:
            self.funcs = None
            self.numFuncs = ctypes.c_uint(0)
        
        if pidList:
            numPid = len(pidList)
            self.pidList = (ctypes.c_int * numPid)(*pidList)
            self.numPid = ctypes.c_uint(numPid)
        else:
            self.pidList = None
            self.numPid = ctypes.c_uint(0)
        
        if cpuList:
            numCpu = len(cpuList)
            self.cpuList = (ctypes.c_int * numCpu)(*cpuList)
            self.numCpu = ctypes.c_uint(numCpu)
        else:
            self.cpuList = None
            self.numCpu = ctypes.c_uint(0)


class PmuTraceAttr:
    __slots__ = ['__c_pmu_trace_attr']

    def __init__(self,
                 funcs: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None) -> None:
        self.__c_pmu_trace_attr = CtypesPmuTraceAttr(
            funcs=funcs,
            pidList=pidList,
            cpuList=cpuList
        )
    
    @property
    def c_pmu_trace_attr(self) -> CtypesPmuTraceAttr:
        return self.__c_pmu_trace_attr
    
    @property
    def numFuncs(self) -> int:
        return self.c_pmu_trace_attr.numFuncs
    
    @property
    def funcs(self) -> List[str]:
        return [self.c_pmu_trace_attr.funcs[i].decode(UTF_8) for i in range(self.numFuncs)]
    
    @funcs.setter
    def funcs(self, funcs: List[str]) -> None:
        if funcs:
            numFuncs = len(funcs)
            self.c_pmu_trace_attr.funcs = (ctypes.c_char_p * numFuncs)(*[func.encode(UTF_8) for func in funcs])
            self.c_pmu_trace_attr.numFuncs = ctypes.c_uint(numFuncs)
        else:
            self.c_pmu_trace_attr.funcs = None
            self.c_pmu_trace_attr.numFuncs = ctypes.c_uint(0)
    
    @property
    def numPid(self) -> int:
        return self.c_pmu_trace_attr.numPid
    
    @property
    def pidList(self) -> List[int]:
        return [self.c_pmu_trace_attr.pidList[i] for i in range(self.numPid)]
    
    @pidList.setter
    def pidList(self, pidList: List[int]) -> None:
        if pidList:
            numPid = len(pidList)
            self.c_pmu_trace_attr.pidList = (ctypes.c_int * numPid)(*[pid for pid in pidList])
            self.c_pmu_trace_attr.numPid = ctypes.c_uint(numPid)
        else:
            self.c_pmu_trace_attr.pidList = None
            self.c_pmu_trace_attr.numPid = ctypes.c_uint(0)
    
    @property
    def numCpu(self) -> int:
        return self.c_pmu_trace_attr.numCpu
    
    @property
    def cpuList(self) -> List[int]:
        return [self.c_pmu_trace_attr.cpuList[i] for i in range(self.numCpu)]
    
    @cpuList.setter
    def cpuList(self, cpuList: List[int]) -> None:
        if cpuList:
            numCpu = len(cpuList)
            self.c_pmu_trace_attr.cpuList = (ctypes.c_int * numCpu)(*[cpu for cpu in cpuList])
            self.c_pmu_trace_attr.numCpu = ctypes.c_uint(numCpu)
        else:
            self.c_pmu_trace_attr.cpuList = None
            self.c_pmu_trace_attr.numCpu = ctypes.c_uint(0)

class CtypesCpuTopology(ctypes.Structure):
    """
    struct CpuTopology {
        int coreId;
        int numaId;
        int socketId;
    };
    """

    _fields_ = [
        ('coreId',   ctypes.c_int),
        ('numaId',   ctypes.c_int),
        ('socketId', ctypes.c_int)
    ]

    def __init__(self,
                 coreId: int=0,
                 numaId: int=0,
                 socketId: int=0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.coreId =   ctypes.c_int(coreId)
        self.numaId =   ctypes.c_int(numaId)
        self.socketId = ctypes.c_int(socketId)


class CpuTopology:
    __slots__ = ['__c_cpu_topo']

    def __init__(self,
                 coreId: int=0,
                 numaId: int=0,
                 socketId: int=0) -> None:
        self.__c_cpu_topo = CtypesCpuTopology(
            coreId=coreId,
            numaId=numaId,
            socketId=socketId
        )

    @property
    def c_cpu_topo(self) -> CtypesCpuTopology:
        return self.__c_cpu_topo

    @property
    def coreId(self) -> int:
        return self.c_cpu_topo.coreId

    @coreId.setter
    def coreId(self, coreId: int) -> None:
        self.c_cpu_topo.coreId = ctypes.c_int(coreId)

    @property
    def numaId(self) -> int:
        return self.c_cpu_topo.numaId

    @numaId.setter
    def numaId(self, numaId: int) -> None:
        self.c_cpu_topo.numaId = ctypes.c_int(numaId)

    @property
    def socketId(self) -> int:
        return self.c_cpu_topo.socketId

    @socketId.setter
    def socketId(self, socketId: int) -> None:
        self.c_cpu_topo.socketId = ctypes.c_int(socketId)

    @classmethod
    def from_c_cpu_topo(cls, c_cpu_topo: CtypesCpuTopology) -> 'CpuTopology':
        cpu_topo = cls()
        cpu_topo.__c_cpu_topo = c_cpu_topo
        return cpu_topo


class CtypesSampleRawData(ctypes.Structure):
    _fields_ = [
        ('data', ctypes.c_char_p)
    ]

    def __init__(self, data: str='', *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.data = ctypes.c_char_p(data.encode(UTF_8))


class SampleRawData:
    __slots__ = ['__c_sample_rawdata']

    def __init__(self, data: str='') -> None:
        self.__c_sample_rawdata = CtypesSampleRawData(data)

    @property
    def c_pmu_data_rawData(self) -> CtypesSampleRawData:
        return self.__c_sample_rawdata

    @property
    def data(self) -> str:
        return self.__c_sample_rawdata.data.decode(UTF_8)

    @classmethod
    def from_sample_raw_data(cls, c_sample_raw_data: CtypesSampleRawData) -> 'SampleRawData':
        sample_raw_data = cls()
        sample_raw_data.__c_sample_rawdata = c_sample_raw_data
        return sample_raw_data


class CytpesBranchSampleRecord(ctypes.Structure):
    _fields_ = [
        ("fromAddr",    ctypes.c_ulong),
        ("toAddr",      ctypes.c_ulong),
        ("cycles",      ctypes.c_ulong),
    ]


class CtypesBranchRecords(ctypes.Structure):
    _fields_ = [
        ("nr", ctypes.c_ulong),
        ("branchRecords", ctypes.POINTER(CytpesBranchSampleRecord))
    ]


class ImplBranchRecords():
    __slots__ = ['__c_branch_record']

    def __init__(self,
                 fromAddr:  int=0,
                 toAddr:    int=0,
                 cycles:    int=0) -> None:
        self.__c_branch_record = CytpesBranchSampleRecord(
            fromAddr=fromAddr,
            toAddr=toAddr,
            cycles=cycles
        )

    @property
    def c_branch_record(self) -> CytpesBranchSampleRecord:
        return self.__c_branch_record

    @property
    def fromAddr(self) -> int:
        return self.c_branch_record.fromAddr
    
    @property
    def toAddr(self) -> int:
        return self.c_branch_record.toAddr
    
    @property
    def cycles(self) -> int:
        return self.c_branch_record.cycles
    
    @classmethod
    def from_c_branch_record(cls, c_branch_record: CytpesBranchSampleRecord) -> 'ImplBranchRecords':
        branch_record = cls()
        branch_record.__c_branch_record = c_branch_record
        return branch_record


class BranchRecords():
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer: ctypes.POINTER(CytpesBranchSampleRecord) = None, nr: int=0) -> None:
        self.__pointer = pointer
        self.__len = nr
        self.__iter = (ImplBranchRecords.from_c_branch_record(self.__pointer[i]) for i in range(self.__len))
    
    @property
    def len(self) -> int:
        return self.__len

    @property
    def iter(self) -> Iterator[ImplBranchRecords]:
        return self.__iter
    
class CytpesSpeDataExt(ctypes.Structure):
    _fields_ = [
        ('pa',    ctypes.c_ulong),
        ('va',    ctypes.c_ulong),
        ('event', ctypes.c_ulong),
        ('lat',   ctypes.c_ushort),
    ]
    def __init__(self,
                 pa: int=0,
                 va: int=0,
                 event: int=0,
                 lat: int=0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.pa = ctypes.c_ulong(pa)
        self.va = ctypes.c_ulong(va)
        self.event = ctypes.c_ulong(event)
        self.lat = ctypes.c_ushort(lat)

class PmuDataExtUnion(ctypes.Union):
    _fields_ = [
        ("speDataExt", CytpesSpeDataExt),
        ("branchRecords", CtypesBranchRecords)
    ]

class CtypesPmuDataExt(ctypes.Structure):
    """
    struct PmuDataExt {
        unsigned long pa;               // physical address
        unsigned long va;               // virtual address
        unsigned long event;            // event id
    };
    """

    _fields_ = [
        ('ext',   PmuDataExtUnion),
    ]

class PmuDataExt:
    __slots__ = ['__c_pmu_data_ext']

    @property
    def c_pmu_data_ext(self) -> CtypesPmuDataExt:
        return self.__c_pmu_data_ext

    @property
    def pa(self) -> int:
        return self.c_pmu_data_ext.ext.speDataExt.pa

    @property
    def va(self) -> int:
        return self.c_pmu_data_ext.ext.speDataExt.va

    @property
    def event(self) -> int:
        return self.c_pmu_data_ext.ext.speDataExt.event

    @property
    def lat(self) -> int:
        return self.c_pmu_data_ext.ext.speDataExt.lat

    @property
    def branchRecords(self) -> BranchRecords:
        if self.__c_pmu_data_ext.ext.branchRecords.branchRecords:
            return BranchRecords(self.__c_pmu_data_ext.ext.branchRecords.branchRecords, self.__c_pmu_data_ext.ext.branchRecords.nr)
        return None

    @classmethod
    def from_pmu_data_ext(cls, c_pmu_data_ext: CtypesPmuDataExt) -> 'PmuDataExt':
        pmu_data_ext = cls()
        pmu_data_ext.__c_pmu_data_ext = c_pmu_data_ext
        return pmu_data_ext


class CtypesSampleRawField(ctypes.Structure):
    """
    struct SampleRawField {
        char* fieldName;    // the field name of this field.
        char* fieldStr;     // the field line.
        unsigned offset;    // the data offset.
        unsigned size;      // the field size.
        unsigned isSigned;  // is signed or is unsigned
    };
    """
    _fields_ = [
        ('fieldName', ctypes.c_char_p),
        ('fieldStr',  ctypes.c_char_p),
        ('offset',    ctypes.c_uint),
        ('size',      ctypes.c_uint),
        ("isSigned",  ctypes.c_uint),
    ]

    def __init__(self,
                 field_name: str='',
                 field_str: str='',
                 offset: int=0,
                 size: int=0,
                 is_signed: int=0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.fieldName = ctypes.c_char_p(field_name.encode(UTF_8))
        self.fieldStr = ctypes.c_char_p(field_str.encode(UTF_8))
        self.offset = ctypes.c_uint(offset)
        self.size = ctypes.c_uint(size)
        self.isSigned = ctypes.c_uint(is_signed)

class SampleRawField:

    __slots__ = ['__c_sample_raw_field']

    def __init__(self,
                 field_name: str='',
                 field_str: str='',
                 offset: int=0,
                 size: int=0,
                 is_signed: int=0) -> None:
        self.__c_sample_raw_field = CtypesSampleRawField(field_name, field_str, offset, size, is_signed)

    @property
    def c_sample_raw_field(self) -> CtypesSampleRawField:
        return self.__c_sample_raw_field

    @property
    def field_name(self) -> str:
        return self.__c_sample_raw_field.fieldName.decode(UTF_8)

    @property
    def field_str(self) -> str:
        return self.__c_sample_raw_field.fieldStr.decode(UTF_8)

    @property
    def size(self) -> int:
        return self.__c_sample_raw_field.size

    @property
    def offset(self) -> int:
        return self.__c_sample_raw_field.offset

    @property
    def is_signed(self) -> bool:
        return bool(self.__c_sample_raw_field.isSigned)

    @classmethod
    def from_sample_raw_field(cls, __c_sample_raw_field: CtypesSampleRawField):
        sample_raw_data = cls()
        sample_raw_data.__c_sample_raw_field = __c_sample_raw_field
        return sample_raw_data


class CtypesPmuData(ctypes.Structure):
    """
    struct PmuData {
        struct Stack* stack;            // call stack
        const char *evt;                // event name
        int64_t ts;                     // time stamp. unit: ns
        pid_t pid;                      // process id
        int tid;                        // thread id
        unsigned cpu;                   // cpu id
        struct CpuTopology *cpuTopo;    // cpu topology
        const char *comm;               // process command
        uint64_t period;                     // number of Samples
        uint64_t count;                 // event count. Only available for Counting.
        double countPercent;              // event count percent. when count = 0, countPercent = -1; Only available for Counting.
        struct PmuDataExt *ext;         // extension. Only available for Spe.
    };
    """

    _fields_ = [
        ('stack',   ctypes.POINTER(CtypesStack)),
        ('evt',     ctypes.c_char_p),
        ('ts',      ctypes.c_int64),
        ('pid',     ctypes.c_int),
        ('tid',     ctypes.c_int),
        ('cpu',     ctypes.c_int),
        ('cpuTopo', ctypes.POINTER(CtypesCpuTopology)),
        ('comm',    ctypes.c_char_p),
        ('period',  ctypes.c_uint64),
        ('count',   ctypes.c_uint64),
        ('countPercent', ctypes.c_double),
        ('ext',     ctypes.POINTER(CtypesPmuDataExt)),
        ('rawData', ctypes.POINTER(CtypesSampleRawData))
    ]

    def __init__(self,
                 stack: CtypesStack=None,
                 evt: str='',
                 ts: int=0,
                 pid: int=0,
                 tid: int=0,
                 cpu: int=0,
                 cpuTopo: CtypesCpuTopology=None,
                 comm: str='',
                 period: int=0,
                 count: int=0,
                 countPercent: float=0.0,
                 ext: CtypesPmuDataExt=None,
                 rawData: CtypesSampleRawData=None,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)

        self.stack = stack
        self.evt = ctypes.c_char_p(evt.encode(UTF_8))
        self.ts = ctypes.c_int64(ts)
        self.pid = ctypes.c_int(pid)
        self.tid = ctypes.c_int(tid)
        self.cpu = ctypes.c_int(cpu)
        self.cpuTopo = cpuTopo
        self.comm = ctypes.c_char_p(comm.encode(UTF_8))
        self.period = ctypes.c_uint64(period)
        self.count = ctypes.c_uint64(count)
        self.countPercent = ctypes.c_double(countPercent)
        self.ext = ext
        self.rawData = rawData


class ImplPmuData:
    __slots__ = ['__c_pmu_data']

    def __init__(self,
                 stack: Stack=None,
                 evt: str='',
                 ts: int=0,
                 pid: int=0,
                 tid: int=0,
                 cpu: int=0,
                 cpuTopo: CpuTopology=None,
                 comm: str='',
                 period: int=0,
                 count: int=0,
                 countPercent: float=0.0,
                 ext: PmuDataExt=None,
                 rawData: SampleRawData=None) -> None:
        self.__c_pmu_data = CtypesPmuData(
            stack=stack.c_stack if stack else None,
            evt=evt,
            ts=ts,
            pid=pid,
            tid=tid,
            cpu=cpu,
            cpuTopo=cpuTopo.c_cpu_topo if cpuTopo else None,
            comm=comm,
            period=period,
            count=count,
            countPercent=countPercent,
            ext=ext.c_pmu_data_ext if ext else None,
            rawData=rawData.c_pmu_data_rawData if rawData else None
        )

    @property
    def c_pmu_data(self) -> CtypesPmuData:
        return self.__c_pmu_data

    @property
    def stack(self) -> Stack:
        return Stack.from_c_stack(self.c_pmu_data.stack.contents) if self.c_pmu_data.stack else None

    @stack.setter
    def stack(self, stack: Stack) -> None:
        self.c_pmu_data.stack = stack.c_stack if stack else None

    @property
    def evt(self) -> str:
        return self.c_pmu_data.evt.decode(UTF_8)

    @evt.setter
    def evt(self, evt: str) -> None:
        self.c_pmu_data.evt = ctypes.c_char_p(evt.encode(UTF_8))

    @property
    def ts(self) -> int:
        return self.c_pmu_data.ts

    @ts.setter
    def ts(self, ts: int) -> None:
        self.c_pmu_data.ts = ctypes.c_int64(ts)

    @property
    def pid(self) -> int:
        return self.c_pmu_data.pid

    @pid.setter
    def pid(self, pid: int) -> None:
        self.c_pmu_data.pid = ctypes.c_int(pid)

    @property
    def tid(self) -> int:
        return self.c_pmu_data.tid

    @tid.setter
    def tid(self, tid: int) -> None:
        self.c_pmu_data.tid = ctypes.c_int(tid)

    @property
    def cpu(self) -> int:
        return self.c_pmu_data.cpu

    @cpu.setter
    def cpu(self, cpu: int) -> None:
        self.c_pmu_data.cpu = ctypes.c_int(cpu)

    @property
    def cpuTopo(self) -> CpuTopology:
        return CpuTopology.from_c_cpu_topo(self.c_pmu_data.cpuTopo.contents) if self.c_pmu_data.cpuTopo else None

    @cpuTopo.setter
    def cpuTopo(self, cpuTopo: CpuTopology) -> None:
        self.c_pmu_data.cpuTopo = cpuTopo.c_cpu_topo if cpuTopo else None

    @property
    def comm(self) -> str:
        return self.c_pmu_data.comm.decode(UTF_8)

    @comm.setter
    def comm(self, comm: str) -> None:
        self.c_pmu_data.comm = ctypes.c_char_p(comm.encode(UTF_8))

    @property
    def period(self) -> int:
        return self.c_pmu_data.period

    @period.setter
    def period(self, period: int) -> None:
        self.c_pmu_data.period = ctypes.c_uint64(period)

    @property
    def count(self) -> int:
        return self.c_pmu_data.count

    @count.setter
    def count(self, count: int) -> None:
        self.c_pmu_data.count = ctypes.c_uint64(count)

    @property
    def countPercent(self) -> float:
        return self.c_pmu_data.countPercent
    
    @countPercent.setter
    def countPercent(self, countPercent: float) -> None:
        self.c_pmu_data.countPercent = ctypes.c_double(countPercent)
        
    @property
    def ext(self) -> PmuDataExt:
        return PmuDataExt.from_pmu_data_ext(self.c_pmu_data.ext.contents) if self.c_pmu_data.ext else None

    @property
    def rawData(self) -> SampleRawData:
        return SampleRawData.from_sample_raw_data(self.c_pmu_data.rawData) if self.c_pmu_data.rawData else None

    @ext.setter
    def ext(self, ext: PmuDataExt) -> None:
        self.c_pmu_data.ext = ext.c_pmu_data_ext if ext else None

    @classmethod
    def from_c_pmu_data(cls, c_pmu_data: CtypesPmuData) -> 'ImplPmuData':
        pmu_data = cls()
        pmu_data.__c_pmu_data = c_pmu_data
        return pmu_data


class PmuData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer: ctypes.POINTER(CtypesPmuData) = None, len: int = 0) -> None:
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuData.from_c_pmu_data(self.__pointer[i]) for i in range(self.__len))

    def __del__(self) -> None:
        self.free()

    def __len__(self) -> int:
        return self.__len
    
    def __iter__(self):
        return self.__iter
    
    def pointer(self) -> ctypes.POINTER(CtypesPmuData):
        return self.__pointer
    
    @property
    def len(self) -> int:
        return self.__len

    @property
    def iter(self) -> Iterator[ImplPmuData]:
        return self.__iter

    def free(self) -> None:
        if self.__pointer is not None:
            PmuDataFree(self.__pointer)
            self.__pointer = None

class CtypesPmuTraceData(ctypes.Structure):
    """
    struct PmuTraceData {
        const char *funcs;              // system call function
        int64_t startTs;               // start time stamp. unit: ns
        double elapsedTime;             // elapsed time
        pid_t pid;                      // process id
        int tid;                        // thread id
        int cpu;                   // cpu id
        const char *comm;               // process command
    };
    """
    _fields_ = [
        ('funcs', ctypes.c_char_p),
        ('startTs', ctypes.c_int64),
        ('elapsedTime', ctypes.c_double),
        ('pid', ctypes.c_int),
        ('tid', ctypes.c_int),
        ('cpu', ctypes.c_int),
        ('comm', ctypes.c_char_p)
    ]

    def __init__(self,
                 funcs: str = '',
                 startTs: int = 0,
                 elapsedTime: float = 0.0,
                 pid: int = 0,
                 tid: int = 0,
                 cpu: int = 0,
                 comm: str = '',
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)

        self.funcs = ctypes.c_char_p(funcs.encode(UTF_8))
        self.startTs = ctypes.c_int64(startTs)
        self.elapsedTime = ctypes.c_double(elapsedTime)
        self.pid = ctypes.c_int(pid)
        self.tid = ctypes.c_int(tid)
        self.cpu = ctypes.c_int(cpu)
        self.comm = ctypes.c_char_p(comm.encode(UTF_8))

class ImplPmuTraceData:
    __slots__ = ['__c_pmu_trace_data']
    def __init__(self,
                 funcs: str = '',
                 startTs: int = 0,
                 elapsedTime: float = 0.0,
                 pid: int = 0,
                 tid: int = 0,
                 cpu: int = 0,
                 comm: str = '',
                 *args: Any, **kw: Any) -> None:
        self.__c_pmu_trace_data = CtypesPmuTraceData(
            funcs=funcs,
            startTs=startTs,
            elapsedTime=elapsedTime,
            pid=pid,
            tid=tid,
            cpu=cpu,
            comm=comm
        )
    
    @property
    def c_pmu_trace_data(self) -> CtypesPmuTraceData:
        return self.__c_pmu_trace_data
    
    @property
    def funcs(self) -> str:
        return self.__c_pmu_trace_data.funcs.decode(UTF_8)
    
    @funcs.setter
    def funcs(self, funcs: str) -> None:
        self.__c_pmu_trace_data.funcs = ctypes.c_char_p(funcs.encode(UTF_8))

    @property
    def startTs(self) -> int:
        return self.__c_pmu_trace_data.startTs
    
    @startTs.setter
    def startTs(self, startTs: int) -> None:
        self.__c_pmu_trace_data.startTs = ctypes.c_int64(startTs)
    
    @property
    def elapsedTime(self) -> float:
        return self.__c_pmu_trace_data.elapsedTime
    
    @elapsedTime.setter
    def elapsedTime(self, elapsedTime: float) -> None:
        self.__c_pmu_trace_data.elapsedTime = ctypes.c_double(elapsedTime)
    
    @property
    def pid(self) -> int:
        return self.__c_pmu_trace_data.pid
    
    @pid.setter
    def pid(self, pid: int) -> None:
        self.__c_pmu_trace_data.pid = ctypes.c_int(pid)
    
    @property
    def tid(self) -> int:
        return self.__c_pmu_trace_data.tid

    @tid.setter
    def tid(self, tid: int) -> None:
        self.__c_pmu_trace_data.tid = ctypes.c_int(tid)
    
    @property
    def cpu(self) -> int:
        return self.__c_pmu_trace_data.cpu
    
    @cpu.setter
    def cpu(self, cpu: int) -> None:
        self.__c_pmu_trace_data.cpu = ctypes.c_int(cpu)
    
    @property
    def comm(self) -> str:
        return self.__c_pmu_trace_data.comm.decode(UTF_8)
    
    @comm.setter
    def comm(self, comm: str) -> None:
        self.__c_pmu_trace_data.comm = ctypes.c_char_p(comm.encode(UTF_8))
    
    @classmethod
    def from_c_pmu_trace_data(cls, c_pmu_trace_data: CtypesPmuTraceData) -> 'ImplPmuTraceData':
        pmu_trace_data = cls()
        pmu_trace_data.__c_pmu_trace_data = c_pmu_trace_data
        return pmu_trace_data

class PmuTraceData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer: ctypes.POINTER(CtypesPmuTraceData) = None, len: int = 0) -> None:
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuTraceData.from_c_pmu_trace_data(self.__pointer[i]) for i in range(self.__len))
    
    def __del__(self) -> None:
        self.free()
    
    @property
    def len(self) -> int:
        return self.__len
    
    @property
    def iter(self) -> Iterator[ImplPmuTraceData]:
        return self.__iter
    
    def free(self) -> None:
        if self.__pointer is not None:
            PmuTraceDataFree(self.__pointer)
            self.__pointer = None

def PmuOpen(collectType: int, pmuAttr: PmuAttr) -> int:
    """
    int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);
    """
    c_PmuOpen = kperf_so.PmuOpen
    c_PmuOpen.argtypes = [ctypes.c_int, ctypes.POINTER(CtypesPmuAttr)]
    c_PmuOpen.restype = ctypes.c_int

    c_collectType = ctypes.c_int(collectType)

    return c_PmuOpen(c_collectType, ctypes.byref(pmuAttr.c_pmu_attr))


def PmuEventListFree() -> None:
    """
    void PmuEventListFree();
    """
    c_PmuEventListFree = kperf_so.PmuEventListFree
    c_PmuEventListFree.argtypes = []
    c_PmuEventListFree.restype = None

    c_PmuEventListFree()


def PmuEventList(eventType: int) -> Iterator[str]:
    """
    const char** PmuEventList(enum PmuEventType eventType, unsigned *numEvt);
    """
    c_PmuEventList = kperf_so.PmuEventList
    c_PmuEventList.argtypes = [ctypes.c_int]
    c_PmuEventList.restype = ctypes.POINTER(ctypes.c_char_p)

    c_eventType = ctypes.c_int(eventType)
    c_numEvt = ctypes.c_uint()

    eventList = c_PmuEventList(c_eventType, ctypes.byref(c_numEvt))
    return (eventList[i].decode(UTF_8) for i in range(c_numEvt.value))


def PmuEnable(pd: int) -> int:
    """
    int PmuEnable(int pd);
    """
    c_PmuEnable = kperf_so.PmuEnable
    c_PmuEnable.argtypes = [ctypes.c_int]
    c_PmuEnable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuEnable(c_pd)


def PmuDisable(pd: int) -> int:
    """
    int PmuDisable(int pd);
    """
    c_PmuDisable = kperf_so.PmuDisable
    c_PmuDisable.argtypes = [ctypes.c_int]
    c_PmuDisable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuDisable(c_pd)


def PmuCollect(pd: int, milliseconds: int, interval: int) -> int:
    """
    int PmuCollect(int pd, int milliseconds, unsigned interval);
    """
    c_PmuCollect = kperf_so.PmuCollect
    c_PmuCollect.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
    c_PmuCollect.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)
    c_milliseconds = ctypes.c_int(milliseconds)
    c_interval = ctypes.c_uint(interval)

    return c_PmuCollect(c_pd, c_milliseconds, c_interval)


def PmuStop(pd: int) -> None:
    """
    void PmuStop(int pd);
    """
    c_PmuStop = kperf_so.PmuStop
    c_PmuStop.argtypes = [ctypes.c_int]
    c_PmuStop.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuStop(c_pd)


def PmuDataFree(pmuData: ctypes.POINTER(CtypesPmuData)) -> None:
    """
    void PmuDataFree(struct PmuData* pmuData);
    """
    c_PmuDataFree = kperf_so.PmuDataFree
    c_PmuDataFree.argtypes = [ctypes.POINTER(CtypesPmuData)]
    c_PmuDataFree.restype = None
    c_PmuDataFree(pmuData)


def PmuRead(pd: int) -> PmuData:
    """
    int PmuRead(int pd, struct PmuData** pmuData);
    """
    c_PmuRead = kperf_so.PmuRead
    c_PmuRead.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.POINTER(CtypesPmuData))]
    c_PmuRead.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)
    c_data_pointer = ctypes.pointer(CtypesPmuData())

    c_data_len = c_PmuRead(c_pd, ctypes.byref(c_data_pointer))
    return PmuData(c_data_pointer, c_data_len)


def PmuAppendData(fromData: ctypes.POINTER(CtypesPmuData),
                  toData: ctypes.POINTER(ctypes.POINTER(CtypesPmuData))) -> int:
    """
    int PmuAppendData(struct PmuData *fromData, struct PmuData **toData);
    """
    c_PmuAppendData = kperf_so.PmuAppendData
    c_PmuAppendData.argtypes = [ctypes.POINTER(CtypesPmuData), ctypes.POINTER(ctypes.POINTER(CtypesPmuData))]
    c_PmuAppendData.restype = ctypes.c_int

    return c_PmuAppendData(fromData, toData)


def PmuClose(pd: int) -> None:
    """
    void PmuClose(int pd);
    """
    c_PmuClose = kperf_so.PmuClose
    c_PmuClose.argtypes = [ctypes.c_int]
    c_PmuClose.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuClose(c_pd)


def PmuDumpData(pmuData: PmuData, filepath: str, dumpDwf: int) -> None:
    """
    int PmuDumpData(struct PmuData *pmuData, unsigned len, char *filepath, int dumpDwf);
    """
    c_PmuDumpData = kperf_so.PmuDumpData
    c_PmuDumpData.argtypes = [ctypes.POINTER(CtypesPmuData), ctypes.c_uint, ctypes.c_char_p, ctypes]
    c_PmuDumpData.restype = ctypes.c_int

    c_len = ctypes.c_uint(pmuData.len)
    c_filepath = ctypes.c_char_p(filepath.encode(UTF_8))
    c_dumpDwf = ctypes.c_int(dumpDwf)

    c_PmuDumpData(pmuData.pointer, c_len, c_filepath, c_dumpDwf)


def PmuGetField(rawData: ctypes.POINTER(CtypesSampleRawData), field_name: str, value: ctypes.c_void_p,
                          vSize: int) -> int:
    """
    int PmuGetField(struct SampleRawData *rawData, const char *fieldName, void *value, uint32_t vSize);
    """
    c_PmuGetField = kperf_so.PmuGetField
    c_PmuGetField.argtypes = [ctypes.POINTER(CtypesSampleRawData), ctypes.c_char_p, ctypes.c_void_p,
                                        ctypes.c_uint]
    c_PmuGetField.restype = ctypes.c_int
    return c_PmuGetField(rawData, field_name.encode(UTF_8), value, vSize)


def PmuGetFieldExp(rawData: ctypes.POINTER(CtypesSampleRawData), field_name: str) -> SampleRawField:
    """
    SampleRawField *PmuGetFieldExp(struct SampleRawData *rawData, const char *fieldName);
    """
    c_PmuGetFieldExp = kperf_so.PmuGetFieldExp
    c_PmuGetFieldExp.argtypes = [ctypes.POINTER(CtypesSampleRawData), ctypes.c_char_p]
    c_PmuGetFieldExp.restype = ctypes.POINTER(CtypesSampleRawField)
    pointer_field = c_PmuGetFieldExp(rawData, field_name.encode(UTF_8))
    if not pointer_field:
        return None
    return SampleRawField.from_sample_raw_field(pointer_field.contents)


def PmuDeviceBdfListFree() -> None:
    """
    void PmuDeviceBdfListFree()
    """
    c_PmuDeviceBdfListFree = kperf_so.PmuDeviceBdfListFree
    c_PmuDeviceBdfListFree.argtypes = []
    c_PmuDeviceBdfListFree.restype = None

    c_PmuDeviceBdfListFree()

def PmuDeviceBdfList(bdf_type: int) -> Iterator[str]:
    """
    const char** PmuDeviceBdfList(enum PmuBdfType bdfType, unsigned *numBdf);
    """
    c_PmuDeviceBdfList = kperf_so.PmuDeviceBdfList
    c_PmuDeviceBdfList.argtypes = [ctypes.c_int]
    c_PmuDeviceBdfList.restype = ctypes.POINTER(ctypes.c_char_p)

    c_bdf_type = ctypes.c_int(bdf_type)
    c_num_bdf = ctypes.c_uint()

    c_bdf_list = c_PmuDeviceBdfList(c_bdf_type, ctypes.byref(c_num_bdf))

    return [c_bdf_list[i].decode(UTF_8) for i in range(c_num_bdf.value)]


def PmuDeviceOpen(device_attr: List[PmuDeviceAttr]) -> int:
    """
    int PmuDeviceOpen(struct PmuDeviceAttr *deviceAttr, unsigned len);
    """
    c_PmuDeviceOpen = kperf_so.PmuDeviceOpen
    c_PmuDeviceOpen.argtypes = [ctypes.POINTER(CtypesPmuDeviceAttr), ctypes.c_uint]
    c_PmuDeviceOpen.restype = ctypes.c_int
    c_num_device = len(device_attr)
    c_device_attr = (CtypesPmuDeviceAttr * c_num_device)(*[attr.c_pmu_device_attr for attr in device_attr])
    return c_PmuDeviceOpen(c_device_attr, c_num_device)


def PmuGetDevMetric(pmu_data: PmuData, device_attr: List[PmuDeviceAttr]) -> PmuDeviceData:
    """
    int PmuGetDevMetric(struct PmuData *pmuData, unsigned pmuLen, struct PmuDeviceAttr *deviceAttr, unsigned len,
                        struct PmuDeviceData *devicedata);
    """
    c_PmuGetDevMetric = kperf_so.PmuGetDevMetric
    c_PmuGetDevMetric.argtypes = [ctypes.POINTER(CtypesPmuData), ctypes.c_uint, 
                                 ctypes.POINTER(CtypesPmuDeviceAttr), ctypes.c_uint, 
                                 ctypes.POINTER(ctypes.POINTER(CtypesPmuDeviceData))]
    c_PmuGetDevMetric.restype = ctypes.c_int
    if not pmu_data or not device_attr:
        return PmuDeviceData()
    
    num_device = len(device_attr)
    c_device_attr = (CtypesPmuDeviceAttr * num_device)(*[attr.c_pmu_device_attr for attr in device_attr])
    c_device_data = ctypes.POINTER(CtypesPmuDeviceData)()
    
    res = c_PmuGetDevMetric(pmu_data.pointer(), len(pmu_data), c_device_attr, num_device, ctypes.byref(c_device_data))
    if res <= 0:
        return PmuDeviceData()
    
    return PmuDeviceData(c_device_data, res)

def DevDataFree(dev_data: ctypes.POINTER(CtypesPmuDeviceData)) -> None:
    """
    void DevDataFree(struct PmuDeviceData *devData);
    """
    c_DevDataFree = kperf_so.DevDataFree
    c_DevDataFree.argtypes = [ctypes.POINTER(CtypesPmuDeviceData)]
    c_DevDataFree.restype = None

    c_DevDataFree(dev_data)

def PmuGetCpuFreq(core: int) -> int:
    """
    Get CPU frequency of a specific CPU core.
    
    Args:
        core: Index of the CPU core
        
    Returns:
        On success, core frequency (Hz) is returned.
        On error, -1 is returned.
    """
    c_PmuGetCpuFreq = kperf_so.PmuGetCpuFreq
    c_PmuGetCpuFreq.argtypes = [ctypes.c_uint]
    c_PmuGetCpuFreq.restype = ctypes.c_longlong
    return c_PmuGetCpuFreq(core)

def PmuGetClusterCore(clusterId: int) -> List[int]:
    """
    Get CPU core list of a specific cluster.
    
    int PmuGetClusterCore(unsigned clusterId, unsigned **coreList);
    """
    c_PmuGetClusterCore = kperf_so.PmuGetClusterCore
    c_PmuGetClusterCore.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint))]
    c_PmuGetClusterCore.restype = ctypes.c_int

    c_clusterId = ctypes.c_uint(clusterId)
    c_core_list = ctypes.POINTER(ctypes.c_uint)()
    c_num_core = ctypes.c_int()
    c_num_core = c_PmuGetClusterCore(c_clusterId, ctypes.byref(c_core_list))
    if c_num_core == -1:
        return []
    
    return [c_core_list[i] for i in range(c_num_core)]

def PmuGetNumaCore(numaId: int) -> List[int]:
    """
    Get CPU core list of a specific NUMA node.
    
    int PmuGetNumaCore(unsigned nodeId, unsigned **coreList);
    """
    c_PmuGetNumaCore = kperf_so.PmuGetNumaCore
    c_PmuGetNumaCore.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint))]
    c_PmuGetNumaCore.restype = ctypes.c_int

    c_numaId = ctypes.c_uint(numaId)
    c_core_list = ctypes.POINTER(ctypes.c_uint)()
    c_num_core = ctypes.c_int()
    c_num_core = c_PmuGetNumaCore(c_numaId, ctypes.byref(c_core_list))
    if c_num_core == -1:
        return []
    return [c_core_list[i] for i in range(c_num_core)]


def PmuTraceOpen(traceType: int, pmuTraceAttr: PmuTraceAttr) -> int:
    """
    int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);
    """
    c_PmuTraceOpen = kperf_so.PmuTraceOpen
    c_PmuTraceOpen.argtypes = [ctypes.c_int, ctypes.POINTER(CtypesPmuTraceAttr)]
    c_PmuTraceOpen.restype = ctypes.c_int

    c_traceType = ctypes.c_int(traceType)

    return c_PmuTraceOpen(c_traceType, ctypes.byref(pmuTraceAttr.c_pmu_trace_attr))

def PmuTraceEnable(pd: int) -> int:
    """
    int PmuTraceEnable(int pd);
    """
    c_PmuTraceEnable = kperf_so.PmuTraceEnable
    c_PmuTraceEnable.argtypes = [ctypes.c_int]
    c_PmuTraceEnable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuTraceEnable(c_pd)

def PmuTraceDisable(pd: int) -> int:
    """
    int PmuTraceDisable(int pd);
    """
    c_PmuTraceDisable = kperf_so.PmuTraceDisable
    c_PmuTraceDisable.argtypes = [ctypes.c_int]
    c_PmuTraceDisable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuTraceDisable(c_pd)

def PmuTraceRead(pd: int) -> PmuTraceData:
    """
    int PmuTraceRead(int pd, struct PmuTraceData** pmuTraceData);
    """
    c_PmuTraceRead = kperf_so.PmuTraceRead
    c_PmuTraceRead.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.POINTER(CtypesPmuTraceData))]
    c_PmuTraceRead.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)
    c_data_pointer = ctypes.pointer(CtypesPmuTraceData())

    c_data_len = c_PmuTraceRead(c_pd, ctypes.byref(c_data_pointer))
    return PmuTraceData(c_data_pointer, c_data_len)

def PmuTraceClose(pd: int) -> None:
    """
    void PmuTraceClose(int pd);
    """
    c_PmuTraceClose = kperf_so.PmuTraceClose
    c_PmuTraceClose.argtypes = [ctypes.c_int]
    c_PmuTraceClose.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuTraceClose(c_pd)

def PmuTraceDataFree(pmuTraceData: ctypes.POINTER(CtypesPmuTraceData)) -> None:
    """
    void PmuTraceDataFree(struct PmuTraceData* pmuTraceData);
    """
    c_PmuTraceDataFree = kperf_so.PmuTraceDataFree
    c_PmuTraceDataFree.argtypes = [ctypes.POINTER(CtypesPmuTraceData)]
    c_PmuTraceDataFree.restype = None
    c_PmuTraceDataFree(pmuTraceData)

def PmuSysCallFuncList() -> Iterator[str]:
    """
    char **PmuSysCallFuncList(unsigned *numFunc);
    """
    c_PmuSysCallFuncList = kperf_so.PmuSysCallFuncList
    c_PmuSysCallFuncList.argtypes = []
    c_PmuSysCallFuncList.restype = ctypes.POINTER(ctypes.c_char_p)
    
    c_num_func = ctypes.c_uint()
    c_func_list = c_PmuSysCallFuncList(ctypes.byref(c_num_func))

    return (c_func_list[i].decode(UTF_8) for i in range(c_num_func.value))

def PmuSysCallFuncListFree() -> None:
    """
    void PmuSysCallFuncListFree();
    """
    c_PmuSysCallFuncListFree = kperf_so.PmuSysCallFuncListFree
    c_PmuSysCallFuncListFree.argtypes = []
    c_PmuSysCallFuncListFree.restype = None

    c_PmuSysCallFuncListFree()

class CtypesPmuCpuFreqDetail(ctypes.Structure):
    """
    struct PmuCpuFreqDetail {
    int cpuId;        // cpu core id
    uint64_t minFreq; // minimum frequency of core
    uint64_t maxFreq; // maximum frequency of core
    uint64_t avgFreq; // average frequency of core
    }
    """
    _fields_ = [
        ('cpuId',   ctypes.c_int),
        ('minFreq', ctypes.c_uint64),
        ('maxFreq', ctypes.c_uint64),
        ('avgFreq', ctypes.c_uint64),
    ]

    def __init__(self,
                 cpuId: int = 0,
                 minFreq: int = 0,
                 maxFreq: int = 0,
                 avgFreq: int = 0,
                 *args:Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.cpuId = ctypes.c_int(cpuId)
        self.minFreq = ctypes.c_uint64(minFreq)
        self.maxFreq = ctypes.c_uint64(maxFreq)
        self.avgFreq = ctypes.c_uint64(avgFreq)


class ImplPmuCpuFreqDetail:
    __slots__ = ['__c_pmu_cpu_freq_detail']
    def __init__(self,
                 cpuId: int = 0,
                 minFreq: int = 0,
                 maxFreq: int = 0,
                 avgFreq: int = 0,
                 *args:Any, **kw: Any) -> None:
        self.__c_pmu_cpu_freq_detail = CtypesPmuCpuFreqDetail(
            cpuId=cpuId,
            minFreq=minFreq,
            maxFreq=maxFreq,
            avgFreq=avgFreq
        )
    
    @property
    def c_pmu_cpu_freq_detail(self) -> CtypesPmuCpuFreqDetail:
        return self.__c_pmu_cpu_freq_detail
    
    @property
    def cpuId(self) -> int:
        return self.__c_pmu_cpu_freq_detail.cpuId
    
    @cpuId.setter
    def cpuId(self, cpuId: int) -> None:
        self.__c_pmu_cpu_freq_detail.cpuId = ctypes.c_int(cpuId)
    
    @property
    def minFreq(self) -> int:
        return self.__c_pmu_cpu_freq_detail.minFreq
    
    @minFreq.setter
    def minFreq(self, minFreq: int) -> None:
        self.__c_pmu_cpu_freq_detail.minFreq = ctypes.c_uint64(minFreq)
    
    @property
    def maxFreq(self) -> int:
        return self.__c_pmu_cpu_freq_detail.maxFreq
    
    @maxFreq.setter
    def maxFreq(self, maxFreq: int) -> None:
        self.__c_pmu_cpu_freq_detail.maxFreq = ctypes.c_uint64(maxFreq)
    
    @property
    def avgFreq(self) -> int:
        return self.__c_pmu_cpu_freq_detail.avgFreq
    
    @avgFreq.setter
    def avgFreq(self, avgFreq: int) -> None:
        self.__c_pmu_cpu_freq_detail.avgFreq = ctypes.c_uint64(avgFreq)
    
    @classmethod
    def from_c_pmu_cpu_freq_detail(cls, c_pmu_cpu_freq_detail: CtypesPmuCpuFreqDetail) -> 'ImplPmuCpuFreqDetail':
        freq_detail = cls()
        freq_detail.__c_pmu_cpu_freq_detail = c_pmu_cpu_freq_detail
        return freq_detail


class PmuCpuFreqDetail:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer: ctypes.POINTER(CtypesPmuCpuFreqDetail) = None, len: int = 0) -> None:
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuCpuFreqDetail.from_c_pmu_cpu_freq_detail(self.__pointer[i]) for i in range(self.__len))
    
    @property
    def len(self) -> int:
        return self.__len

    @property
    def iter(self) -> Iterator[ImplPmuCpuFreqDetail]:
        return self.__iter


def PmuReadCpuFreqDetail() -> PmuCpuFreqDetail:
    """
    struct PmuCpuFreqDetail* PmuReadCpuFreqDetail(unsigned* cpuNum);
    """
    c_PmuGetCpuFreqDetail = kperf_so.PmuReadCpuFreqDetail
    c_PmuGetCpuFreqDetail.argtypes = []
    c_PmuGetCpuFreqDetail.restype  = ctypes.POINTER(CtypesPmuCpuFreqDetail)
    c_cpu_len = ctypes.c_uint(0)
    c_freq_detail_pointer = c_PmuGetCpuFreqDetail(ctypes.byref(c_cpu_len))

    return PmuCpuFreqDetail(c_freq_detail_pointer, c_cpu_len.value)

def PmuOpenCpuFreqSampling(period: int) -> None:
    """
    int PmuOpenCpuFreqSampling(unsigned period);
    """
    c_PmuOpenCpuFreqSampling = kperf_so.PmuOpenCpuFreqSampling

    c_period = ctypes.c_uint(period)
    return c_PmuOpenCpuFreqSampling(c_period)

def PmuCloseCpuFreqSampling() -> None:
    """
    void PmuCloseCpuFreqSampling();
    """
    c_PmuCloseCpuFreqSampling = kperf_so.PmuCloseCpuFreqSampling
    c_PmuCloseCpuFreqSampling()


__all__ = [
    'CtypesEvtAttr',
    'EvtAttr',
    'CtypesPmuAttr',
    'PmuAttr',
    'CpuTopology',
    'PmuDataExt',
    'SampleRawField',
    'SampleRawData',
    'CtypesPmuData',
    'ImplPmuData',
    'PmuData',
    'PmuOpen',
    'PmuEventList',
    'PmuEnable',
    'PmuDisable',
    'PmuStop',
    'PmuRead',
    'PmuClose',
    'PmuDumpData',
    'PmuGetField',
    'PmuGetFieldExp',
    'PmuDeviceAttr',
    'ImplPmuDeviceData',
    'PmuDeviceData',
    'PmuDeviceBdfList',
    'PmuDeviceBdfListFree',
    'PmuDeviceOpen',
    'PmuGetDevMetric',
    'PmuGetCpuFreq',
    'PmuGetClusterCore',
    'PmuGetNumaCore',
    'CtypesPmuTraceAttr',
    'PmuTraceAttr',
    'CtypesPmuTraceData',
    'ImplPmuTraceData',
    'PmuTraceData',
    'PmuTraceOpen',
    'PmuTraceEnable',
    'PmuTraceDisable',
    'PmuTraceRead',
    'PmuTraceClose',
    'PmuTraceDataFree',
    'PmuSysCallFuncList',
    'PmuSysCallFuncListFree',
    'PmuOpenCpuFreqSampling',
    'PmuReadCpuFreqDetail',
    'PmuCloseCpuFreqSampling',
    'PmuCpuFreqDetail',
]
