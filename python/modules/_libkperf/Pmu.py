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


class CtypesPmuAttr(ctypes.Structure):
    """
    struct PmuAttr {
        char** evtList;                 // event list
        unsigned numEvt;                // length of event list
        int* pidList;                   // pid list
        unsigned numPid;                // length of pid list
        int* cpuList;                   // cpu id list
        unsigned numCpu;                // length of cpu id list

        union {
            unsigned period;            // sample period
            unsigned freq;              // sample frequency
        };
        unsigned useFreq : 1;
        unsigned excludeUser : 1;     // don't count user
        unsigned excludeKernel : 1;   //  don't count kernel
        enum SymbolMode symbolMode;     // refer to comments of SymbolMode
        unsigned callStack : 1;   //  collect complete call stack
        // SPE related fields.
        enum SpeFilter dataFilter;      // spe data filter
        enum SpeEventFilter evFilter;   // spe event filter
        unsigned long minLatency;       // collect only samples with latency or higher
        unsigned includeNewFork;  // include new fork thread
    };
    """

    _fields_ = [
        ('evtList',       ctypes.POINTER(ctypes.c_char_p)),
        ('numEvt',        ctypes.c_uint),
        ('pidList',       ctypes.POINTER(ctypes.c_int)),
        ('numPid',        ctypes.c_uint),
        ('cpuList',       ctypes.POINTER(ctypes.c_int)),
        ('numCpu',        ctypes.c_uint),
        ('sampleRate',    SampleRateUnion),
        ('useFreq',       ctypes.c_bool),
        ('excludeUser',   ctypes.c_bool),
        ('excludeKernel', ctypes.c_bool),
        ('symbolMode',    ctypes.c_uint),
        ('callStack',     ctypes.c_bool),
        ('dataFilter',    ctypes.c_uint64),  # The enumeration for dataFilter will use 64 bits
        ('evFilter',      ctypes.c_uint),
        ('minLatency',    ctypes.c_ulong),
        ('includeNewFork', ctypes.c_bool),
    ]

    def __init__(self,
                 evtList: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None,
                 sampleRate: int=0,
                 useFreq: bool=False,
                 excludeUser: bool=False,
                 excludeKernel: bool=False,
                 symbolMode: int=0,
                 callStack: bool=False,
                 dataFilter: int=0,
                 evFilter: int=0,
                 minLatency: int=0,
                 includeNewFork: bool=False,
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

        self.useFreq = ctypes.c_bool(useFreq)
        self.excludeUser = ctypes.c_bool(excludeUser)
        self.excludeKernel = ctypes.c_bool(excludeKernel)

        self.symbolMode = ctypes.c_uint(symbolMode)
        self.callStack = ctypes.c_bool(callStack)
        self.dataFilter = ctypes.c_uint64(dataFilter)
        self.evFilter = ctypes.c_uint(evFilter)
        self.minLatency = ctypes.c_ulong(minLatency)
        self.includeNewFork = ctypes.c_bool(includeNewFork)


class PmuAttr:
    __slots__ = ['__c_pmu_attr']

    def __init__(self,
                 evtList: List[str]=None,
                 pidList: List[int]=None,
                 cpuList: List[int]=None,
                 sampleRate: int=0,
                 useFreq: bool=False,
                 excludeUser: bool=False,
                 excludeKernel: bool=False,
                 symbolMode: int=0,
                 callStack: bool=False,
                 dataFilter: int=0,
                 evFilter: int=0,
                 minLatency: int=0,
                 inlcludeNewFork: bool=False) -> None:
        self.__c_pmu_attr = CtypesPmuAttr(
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
            minLatency=minLatency,
            includeNewFork=inlcludeNewFork
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
        self.c_pmu_attr.useFreq = ctypes.c_bool(useFreq)

    @property
    def excludeUser(self) -> bool:
        return bool(self.c_pmu_attr.excludeUser)

    @excludeUser.setter
    def excludeUser(self, excludeUser: bool) -> None:
        self.c_pmu_attr.excludeUser = ctypes.c_bool(excludeUser)

    @property
    def excludeKernel(self) -> bool:
        return bool(self.c_pmu_attr.excludeKernel)

    @excludeKernel.setter
    def excludeKernel(self, excludeKernel: bool) -> None:
        self.c_pmu_attr.excludeKernel = ctypes.c_bool(excludeKernel)

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
        self.c_pmu_attr.callStack = ctypes.c_bool(callStack)

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
        self.c_pmu_attr.includeNewFork = ctypes.c_bool(includeNewFork)

    @classmethod
    def from_c_pmu_data(cls, c_pmu_attr: CtypesPmuAttr) -> 'PmuAttr':
        pmu_attr = cls()
        pmu_attr.__c_pmu_attr = c_pmu_attr
        return pmu_attr


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


class CtypesPmuDataExt(ctypes.Structure):
    """
    struct PmuDataExt {
        unsigned long pa;               // physical address
        unsigned long va;               // virtual address
        unsigned long event;            // event id
    };
    """

    _fields_ = [
        ('pa',    ctypes.c_ulong),
        ('va',    ctypes.c_ulong),
        ('event', ctypes.c_ulong)
    ]

    def __init__(self,
                 pa: int=0,
                 va: int=0,
                 event: int=0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.pa = ctypes.c_ulong(pa)
        self.va = ctypes.c_ulong(va)
        self.event = ctypes.c_ulong(event)


class PmuDataExt:
    __slots__ = ['__c_pmu_data_ext']

    def __init__(self,
                 pa: int=0,
                 va: int=0,
                 event: int=0) -> None:
        self.__c_pmu_data_ext = CtypesPmuDataExt(
            pa=pa,
            va=va,
            event=event
        )

    @property
    def c_pmu_data_ext(self) -> CtypesPmuDataExt:
        return self.__c_pmu_data_ext

    @property
    def pa(self) -> int:
        return self.c_pmu_data_ext.pa

    @pa.setter
    def pa(self, pa: int) -> None:
        self.c_pmu_data_ext.pa = ctypes.c_ulong(pa)

    @property
    def va(self) -> int:
        return self.c_pmu_data_ext.va

    @va.setter
    def va(self, va: int) -> None:
        self.c_pmu_data_ext.va = ctypes.c_ulong(va)

    @property
    def event(self) -> int:
        return self.c_pmu_data_ext.event

    @event.setter
    def event(self, event) -> None:
        self.c_pmu_data_ext.event = ctypes.c_ulong(event)

    @classmethod
    def from_pmu_data_ext(cls, c_pmu_data_ext: CtypesPmuDataExt) -> 'PmuDataExt':
        pmu_data_ext = cls()
        pmu_data_ext.__c_pmu_data_ext = c_pmu_data_ext
        return pmu_data_ext


class CtypesSampleRawField(ctypes.Structure):
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
        int64_t ts;                     // time stamp
        pid_t pid;                      // process id
        int tid;                        // thread id
        unsigned cpu;                   // cpu id
        struct CpuTopology *cpuTopo;    // cpu topology
        const char *comm;               // process command
        uint64_t period;                     // number of Samples
        uint64_t count;                 // event count. Only available for Counting.
        struct PmuDataExt *ext;         // extension. Only available for Spe.
    };
    """

    _fields_ = [
        ('stack',   ctypes.POINTER(CtypesStack)),
        ('evt',     ctypes.c_char_p),
        ('ts',      ctypes.c_int64),
        ('pid',     ctypes.c_int),
        ('tid',     ctypes.c_int),
        ('cpu',     ctypes.c_uint),
        ('cpuTopo', ctypes.POINTER(CtypesCpuTopology)),
        ('comm',    ctypes.c_char_p),
        ('period',  ctypes.c_int),
        ('count',   ctypes.c_uint64),
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
                 ext: CtypesPmuDataExt=None,
                 rawData: CtypesSampleRawData=None,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)

        self.stack = stack
        self.evt = ctypes.c_char_p(evt.encode(UTF_8))
        self.ts = ctypes.c_int64(ts)
        self.pid = ctypes.c_int(pid)
        self.tid = ctypes.c_int(tid)
        self.cpu = ctypes.c_uint(cpu)
        self.cpuTopo = cpuTopo
        self.comm = ctypes.c_char_p(comm.encode(UTF_8))
        self.period = ctypes.c_int(period)
        self.count = ctypes.c_uint64(count)
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
        self.c_pmu_data.cpu = ctypes.c_uint(cpu)

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
        self.c_pmu_data.period = ctypes.c_int(period)

    @property
    def count(self) -> int:
        return self.c_pmu_data.count

    @count.setter
    def count(self, count: int) -> None:
        self.c_pmu_data.count = ctypes.c_uint64(count)

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
    int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);
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


__all__ = [
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
    'CtypesPmuAttr',
    'PmuGetField',
    'PmuGetFieldExp',
]
