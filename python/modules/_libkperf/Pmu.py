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
        int groupId;
    };
    """
    _fields_ = [('groupId', ctypes.c_int)]

    def __init__(self, groupId=0, *args, **kw):
        super(CtypesEvtAttr, self).__init__(*args, **kw)
        self.groupId = ctypes.c_int(groupId)

class EvtAttr:
    __slots__ = ['__c_evt_attr']

    def __init__(self, groupId=0):
        self.__c_evt_attr = CtypesEvtAttr(groupId)

    @property
    def c_evt_attr(self):
        return self.__c_evt_attr
    
    @property
    def groupId(self):
        return int(self.c_evt_attr.groupId)
    
    @groupId.setter
    def groupId(self, groupId):
        self.c_evt_attr.groupId = ctypes.c_int(groupId)

    @classmethod
    def from_c_evt_attr(cls, c_evt_attr):
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
        unsigned numCpu;                // length of cpu id list

        struct EvtAttr *evtAttr;        // events group id info
        unsigned numGroup               // length of evtAttr

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
        ('numGroup',      ctypes.c_uint),
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
        ('cgroupNameList', ctypes.POINTER(ctypes.c_char_p)),
        ('numCgroup',     ctypes.c_uint),
        ('enableUserAccess', ctypes.c_uint, 1)
    ]

    def __init__(self,
                 evtList=None,
                 pidList=None,
                 cpuList=None,
                 evtAttr=None,
                 sampleRate=0,
                 useFreq=False,
                 excludeUser=False,
                 excludeKernel=False,
                 symbolMode=0,
                 callStack=False,
                 blockedSample=False,
                 dataFilter=0,
                 evFilter=0,
                 minLatency=0,
                 includeNewFork=False,
                 branchSampleFilter=0,
                 cgroupNameList=None,
                 numCgroup=0,
                 enableUserAccess=False,
                 *args, **kw):
        super(CtypesPmuAttr, self).__init__(*args, **kw)

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
            numGroup = len(evtAttr)
            self.evtAttr = (CtypesEvtAttr * numGroup)(*[CtypesEvtAttr(evt) for evt in evtAttr])
            self.numGroup = ctypes.c_uint(numGroup)
        else:
            self.evtAttr = None
            self.numGroup = ctypes.c_uint(0)
        
        if cgroupNameList:
            numCgroup = len(cgroupNameList)
            self.cgroupNameList = (ctypes.c_char_p * numCgroup)(*[cgroupName.encode(UTF_8) for cgroupName in cgroupNameList])
            self.numCgroup = ctypes.c_uint(numCgroup)
        else:
            self.cgroupNameList = None
            self.numCgroup = ctypes.c_uint(0)


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
        self.enableUserAccess = enableUserAccess


class PmuAttr(object):
    __slots__ = ['__c_pmu_attr']

    def __init__(self,
                 evtList=None,
                 pidList=None,
                 cpuList=None,
                 evtAttr=None,
                 sampleRate=0,
                 useFreq=False,
                 excludeUser=False,
                 excludeKernel=False,
                 symbolMode=0,
                 callStack=False,
                 blockedSample=False,
                 dataFilter=0,
                 evFilter=0,
                 minLatency=0,
                 includeNewFork=False,
                 branchSampleFilter=0,
                 cgroupNameList=None,
                 enableUserAccess=False):

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
            cgroupNameList=cgroupNameList,
            enableUserAccess=enableUserAccess
        )

    @property
    def enableUserAccess(self):
        return bool(self.c_pmu_attr.enableUserAccess)

    @enableUserAccess.setter
    def enableUserAccess(self, enableUserAccess):
        self.c_pmu_attr.enableUserAccess = int(enableUserAccess)

    @property
    def c_pmu_attr(self):
        return self.__c_pmu_attr

    @property
    def numEvt(self):
        return self.c_pmu_attr.numEvt

    @property
    def evtList(self):
        return [self.c_pmu_attr.evtList[i].decode(UTF_8) for i in range(self.numEvt)]

    @evtList.setter
    def evtList(self, evtList):
        if evtList:
            numEvt = len(evtList)
            self.c_pmu_attr.evtList = (ctypes.c_char_p * numEvt)(*[evt.encode(UTF_8) for evt in evtList])
            self.c_pmu_attr.numEvt = ctypes.c_uint(numEvt)
        else:
            self.c_pmu_attr.evtList = None
            self.c_pmu_attr.numEvt = ctypes.c_uint(0)

    @property
    def numCgroup(self):
        return self.c_pmu_attr.numCgroup

    @property
    def cgroupNameList(self):
        return [self.c_pmu_attr.cgroupNameList[i].decode(UTF_8) for i in range(self.numCgroup)]

    @cgroupNameList.setter
    def cgroupNameList(self, cgroupNameList):
        if cgroupNameList:
            numCgroup = len(cgroupNameList)
            self.c_pmu_attr.cgroupNameList = (ctypes.c_char_p * numCgroup)(*[cgroupName.encode(UTF_8) for cgroupName in cgroupNameList])
            self.c_pmu_attr.numCgroup = ctypes.c_uint(numCgroup)
        else:
            self.c_pmu_attr.cgroupNameList = None
            self.c_pmu_attr.numCgroup = ctypes.c_uint(0)

    @property
    def numPid(self):
        return self.c_pmu_attr.numPid

    @property
    def pidList(self):
        return [self.c_pmu_attr.pidList[i] for i in range(self.numPid)]

    @pidList.setter
    def pidList(self, pidList):
        if pidList:
            numPid = len(pidList)
            self.c_pmu_attr.pidList = (ctypes.c_int * numPid)(*[pid for pid in pidList])
            self.c_pmu_attr.numPid = ctypes.c_uint(numPid)
        else:
            self.c_pmu_attr.pidList = None
            self.c_pmu_attr.numPid = ctypes.c_uint(0)

    @property
    def numGroup(self):
        return self.c_pmu_attr.numGroup

    @property
    def evtAttr(self):
        return [self.c_pmu_attr.evtAttr[i] for i in range(self.numGroup)]

    @evtAttr.setter
    def evtAttr(self, evtAttr):
        if evtAttr:
            numGroup = len(evtAttr)
            self.c_pmu_attr.evtAttr = (CtypesEvtAttr * numGroup)(*[CtypesEvtAttr(evt) for evt in evtAttr])
            self.c_pmu_attr.numGroup = ctypes.c_uint(numGroup)
        else:
            self.c_pmu_attr.evtAttr = None
            self.c_pmu_attr.numGroup = ctypes.c_uint(0)

    @property
    def numCpu(self):
        return self.c_pmu_attr.numCpu

    @property
    def cpuList(self):
        return [self.c_pmu_attr.cpuList[i] for i in range(self.numCpu)]

    @cpuList.setter
    def cpuList(self, cpuList):
        if cpuList:
            numCpu = len(cpuList)
            self.c_pmu_attr.cpuList = (ctypes.c_int * numCpu)(*[cpu for cpu in cpuList])
            self.c_pmu_attr.numCpu = ctypes.c_uint(numCpu)
        else:
            self.c_pmu_attr.cpuList = None
            self.c_pmu_attr.numCpu = ctypes.c_uint(0)

    @property
    def sampleRate(self):
        if not self.useFreq:
            return self.c_pmu_attr.sampleRate.period
        else:
            return self.c_pmu_attr.sampleRate.freq

    @sampleRate.setter
    def sampleRate(self, sampleRate):
        if not self.useFreq:
            self.c_pmu_attr.sampleRate.period = ctypes.c_uint(sampleRate)
        else:
            self.c_pmu_attr.sampleRate.freq = ctypes.c_uint(sampleRate)

    @property
    def useFreq(self):
        return bool(self.c_pmu_attr.useFreq)

    @useFreq.setter
    def useFreq(self, useFreq):
        self.c_pmu_attr.useFreq = int(useFreq)

    @property
    def excludeUser(self):
        return bool(self.c_pmu_attr.excludeUser)

    @excludeUser.setter
    def excludeUser(self, excludeUser):
        self.c_pmu_attr.excludeUser = int(excludeUser)

    @property
    def excludeKernel(self):
        return bool(self.c_pmu_attr.excludeKernel)

    @excludeKernel.setter
    def excludeKernel(self, excludeKernel):
        self.c_pmu_attr.excludeKernel = int(excludeKernel)

    @property
    def symbolMode(self):
        return self.c_pmu_attr.symbolMode

    @symbolMode.setter
    def symbolMode(self, symbolMode):
        self.c_pmu_attr.symbolMode = ctypes.c_uint(symbolMode)

    @property
    def callStack(self):
        return bool(self.c_pmu_attr.callStack)

    @callStack.setter
    def callStack(self, callStack):
        self.c_pmu_attr.callStack = int(callStack)

    @property
    def blockedSample(self):
        return bool(self.c_pmu_attr.blockedSample)
    
    @blockedSample.setter
    def blockedSample(self, blockedSample):
        self.c_pmu_attr.blockedSample = int(blockedSample)

    @property
    def dataFilter(self):
        return self.c_pmu_attr.dataFilter

    @dataFilter.setter
    def dataFilter(self, dataFilter):
        self.c_pmu_attr.dataFilter = ctypes.c_uint64(dataFilter)

    @property
    def evFilter(self):
        return self.c_pmu_attr.evFilter

    @evFilter.setter
    def evFilter(self, evFilter):
        self.c_pmu_attr.evFilter = ctypes.c_uint(evFilter)

    @property
    def minLatency(self):
        return self.c_pmu_attr.minLatency

    @minLatency.setter
    def minLatency(self, minLatency):
        self.c_pmu_attr.minLatency = ctypes.c_ulong(minLatency)

    @property
    def includeNewFork(self):
        return bool(self.c_pmu_attr.includeNewFork)

    @includeNewFork.setter
    def includeNewFork(self, includeNewFork):
        self.c_pmu_attr.includeNewFork = int(includeNewFork)
    
    @property
    def branchSampleFilter(self):
        return self.c_pmu_attr.branchSampleFilter

    @branchSampleFilter.setter
    def branchSampleFilter(self, branchSampleFilter):
        self.c_pmu_attr.branchSampleFilter = ctypes.c_ulong(branchSampleFilter)

    @classmethod
    def from_c_pmu_data(cls, c_pmu_attr):
        pmu_attr = cls()
        pmu_attr.__c_pmu_attr = c_pmu_attr
        return pmu_attr

class CtypesPmuDeviceAttr(ctypes.Structure):
    """
    struct PmuDeviceAttr {
        enum PmuDeviceMetric metric;
        char *bdf;
        char *port;
    };
    """
    _fields_ = [
        ('metric', ctypes.c_int),
        ('bdf', ctypes.c_char_p),
        ('port', ctypes.c_char_p)
    ]
    
    def __init__(self,
                metric=0,
                bdf=None,
                port=None,
                *args, **kw):
        super(CtypesPmuDeviceAttr, self).__init__(*args, **kw)

        self.metric = ctypes.c_int(metric)
        if bdf:
            self.bdf = ctypes.c_char_p(bdf.encode(UTF_8))
        else:
            self.bdf = None
        if port:
            self.port = ctypes.c_char_p(port.encode(UTF_8))
        else:
            self.port = None

class PmuDeviceAttr(object):
    __slots__ = ['__c_pmu_device_attr']

    def __init__(self,
                 metric=0,
                 bdf=None,
                 port=None):
        self.__c_pmu_device_attr = CtypesPmuDeviceAttr(
            metric=metric,
            bdf=bdf,
            port=port
        )

    @property
    def c_pmu_device_attr(self):
        return self.__c_pmu_device_attr

    @property
    def metric(self):
        return self.c_pmu_device_attr.metric

    @metric.setter
    def metric(self, metric):
        self.c_pmu_device_attr.metric = ctypes.c_int(metric)

    @property
    def bdf(self):
        if self.c_pmu_device_attr.bdf:
            return self.c_pmu_device_attr.bdf.decode(UTF_8)
        return None

    @bdf.setter
    def bdf(self, bdf):
        if bdf:
            self.c_pmu_device_attr.bdf = ctypes.c_char_p(bdf.encode(UTF_8))
        else:
            self.c_pmu_device_attr.bdf = None

    @property
    def port(self):
        if self.c_pmu_device_attr.port:
            return self.c_pmu_device_attr.port.decode(UTF_8)
        return None

    @port.setter
    def port(self, port):
        if port:
            self.c_pmu_device_attr.port = ctypes.c_char_p(port.encode(UTF_8))
        else:
            self.c_pmu_device_attr.port = None

    @classmethod
    def from_c_pmu_device_attr(cls, c_pmu_device_attr):
        pmu_device_attr = cls()
        pmu_device_attr.__c_pmu_device_attr = c_pmu_device_attr
        return pmu_device_attr

class DdrDataStructure(ctypes.Structure):
    _fields_ = [
        ('channelId', ctypes.c_uint),
        ('ddrNumaId', ctypes.c_uint),
        ('socketId', ctypes.c_uint)
    ]

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
            char *port;
            struct {
                unsigned channelId;
                unsigned ddrNumaId;
                unsigned socketId;
            };
        };
    };
    """
    class _Union(ctypes.Union):
        _fields_ = [
            ('coreId', ctypes.c_uint),
            ('numaId', ctypes.c_uint),
            ('clusterId', ctypes.c_uint),
            ('bdf', ctypes.c_char_p),
            ('port', ctypes.c_char_p),
            ('_structure', DdrDataStructure)
        ]
    
    _fields_ = [
        ('metric', ctypes.c_int),
        ('count', ctypes.c_double),
        ('mode', ctypes.c_int),
        ('_union', _Union)
    ]
    
    @property
    def coreId(self):
        if self.mode == 1:  # PMU_METRIC_CORE
            return self._union.coreId
        return 0
    
    @property
    def numaId(self):
        if self.mode == 2:  # PMU_METRIC_NUMA
            return self._union.numaId
        return 0
    
    @property
    def clusterId(self):
        if self.mode == 3:  # PMU_METRIC_CLUSTER
            return self._union.clusterId
        return 0
    
    @property
    def bdf(self):
        if self.mode == 4 and self._union.bdf:  # PMU_METRIC_BDF
            return self._union.bdf.decode(UTF_8)
        return ""

    @property
    def port(self):
        if self.mode == 4 and self._union.port:  # PMU_METRIC_BDF
            return self._union.port.decode(UTF_8)
        return ""

    @property
    def channelId(self):
        if self.mode == 5 and self._union._structure.channelId:  # PMU_METRIC_CHANNEL
            return self._union._structure.channelId
        return 0

    @property
    def ddrNumaId(self):
        if self.mode == 5 and self._union._structure.ddrNumaId:  # PMU_METRIC_CHANNEL
            return self._union._structure.ddrNumaId
        return 0

    @property
    def socketId(self):
        if self.mode == 5 and self._union._structure.socketId:  # PMU_METRIC_CHANNEL
            return self._union._structure.socketId
        return 0

class ImplPmuDeviceData:
    __slots__ = ['__c_pmu_device_data']

    def __init__(self,
                 metric=0,
                 count=0,
                 mode=0):
        self.__c_pmu_device_data = CtypesPmuDeviceData()
        self.__c_pmu_device_data.metric = ctypes.c_int(metric)
        self.__c_pmu_device_data.count = ctypes.c_double(count)
        self.__c_pmu_device_data.mode = ctypes.c_int(mode)
    
    @property
    def c_pmu_device_data(self):
        return self.__c_pmu_device_data
    
    @property
    def metric(self):
        return self.c_pmu_device_data.metric
    
    @property
    def count(self):
        return self.c_pmu_device_data.count
    
    @property
    def mode(self):
        return self.c_pmu_device_data.mode
    
    @property
    def coreId(self):
        if self.mode == 1:  # PMU_METRIC_CORE
            return self.c_pmu_device_data._union.coreId
        return 0
    
    @property
    def numaId(self):
        if self.mode == 2:  # PMU_METRIC_NUMA
            return self.c_pmu_device_data._union.numaId
        return 0
    
    @property
    def clusterId(self):
        if self.mode == 3:  # PMU_METRIC_CLUSTER
            return self.c_pmu_device_data._union.clusterId
        return 0
    
    @property
    def bdf(self):
        if self.mode == 4 and self.c_pmu_device_data._union.bdf:  # PMU_METRIC_BDF
            return self.c_pmu_device_data._union.bdf.decode(UTF_8)
        return ""

    @property
    def port(self):
        if self.mode == 4 and self.c_pmu_device_data._union.port:  # PMU_METRIC_BDF
            return self.c_pmu_device_data._union.port.decode(UTF_8)
        return ""

    @property
    def channelId(self):
        if self.mode == 5 and self.c_pmu_device_data._union._structure.channelId:  # PMU_METRIC_CHANNEL
            return self.c_pmu_device_data._union._structure.channelId
        return 0

    @property
    def ddrNumaId(self):
        if self.mode == 5 and self.c_pmu_device_data._union._structure.ddrNumaId:  # PMU_METRIC_CHANNEL
            return self.c_pmu_device_data._union._structure.ddrNumaId
        return 0

    @property
    def socketId(self):
        if self.mode == 5 and self.c_pmu_device_data._union._structure.socketId:  # PMU_METRIC_CHANNEL
            return self.c_pmu_device_data._union._structure.socketId
        return 0

    @classmethod
    def from_c_pmu_device_data(cls, c_pmu_device_data):
        pmu_device_data = cls()
        pmu_device_data.__c_pmu_device_data = c_pmu_device_data
        return pmu_device_data


class PmuDeviceData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer = None, len=0):
        self.__pointer =pointer
        self.__len =len
        self.__iter = (ImplPmuDeviceData.from_c_pmu_device_data(self.__pointer[i]) for i in range(self.__len))
    
    def __del__(self):
        self.free()
    
    def __len__(self):
        return self.__len
    
    def __getitem__(self, index):
        if index < 0 or index >= self.__len:
            raise IndexError("index out of range")
        return ImplPmuDeviceData.from_c_pmu_device_data(self.__pointer[index])
    
    @property
    def len(self):
        return self.__len
    
    @property
    def iter(self):
        return self.__iter
    
    def free(self):
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
                 funcs=None,
                 pidList=None,
                 cpuList=None,
                *args, **kw):
        super(CtypesPmuTraceAttr, self).__init__(*args, **kw)

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


class PmuTraceAttr(object):
    __slots__ = ['__c_pmu_trace_attr']

    def __init__(self,
                 funcs=None,
                 pidList=None,
                 cpuList=None):
        self.__c_pmu_trace_attr = CtypesPmuTraceAttr(
            funcs=funcs,
            pidList=pidList,
            cpuList=cpuList
        )
    
    @property
    def c_pmu_trace_attr(self):
        return self.__c_pmu_trace_attr
    
    @property
    def numFuncs(self):
        return self.c_pmu_trace_attr.numFuncs
    
    @property
    def funcs(self):
        return [self.c_pmu_trace_attr.funcs[i].decode(UTF_8) for i in range(self.numFuncs)]
    
    @funcs.setter
    def funcs(self, funcs):
        if funcs:
            numFuncs = len(funcs)
            self.c_pmu_trace_attr.funcs = (ctypes.c_char_p * numFuncs)(*[func.encode(UTF_8) for func in funcs])
            self.c_pmu_trace_attr.numFuncs = ctypes.c_uint(numFuncs)
        else:
            self.c_pmu_trace_attr.funcs = None
            self.c_pmu_trace_attr.numFuncs = ctypes.c_uint(0)
    
    @property
    def numPid(self):
        return self.c_pmu_trace_attr.numPid
    
    @property
    def pidList(self):
        return [self.c_pmu_trace_attr.pidList[i] for i in range(self.numPid)]
    
    @pidList.setter
    def pidList(self, pidList):
        if pidList:
            numPid = len(pidList)
            self.c_pmu_trace_attr.pidList = (ctypes.c_int * numPid)(*[pid for pid in pidList])
            self.c_pmu_trace_attr.numPid = ctypes.c_uint(numPid)
        else:
            self.c_pmu_trace_attr.pidList = None
            self.c_pmu_trace_attr.numPid = ctypes.c_uint(0)
    
    @property
    def numCpu(self):
        return self.c_pmu_trace_attr.numCpu
    
    @property
    def cpuList(self):
        return [self.c_pmu_trace_attr.cpuList[i] for i in range(self.numCpu)]
    
    @cpuList.setter
    def cpuList(self, cpuList):
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
                 coreId=0,
                 numaId=0,
                 socketId=0,
                 *args, **kw):
        super(CtypesCpuTopology, self).__init__(*args, **kw)
        self.coreId =   ctypes.c_int(coreId)
        self.numaId =   ctypes.c_int(numaId)
        self.socketId = ctypes.c_int(socketId)


class CpuTopology:
    __slots__ = ['__c_cpu_topo']

    def __init__(self,
                 coreId=0,
                 numaId=0,
                 socketId=0):
        self.__c_cpu_topo = CtypesCpuTopology(
            coreId=coreId,
            numaId=numaId,
            socketId=socketId
        )

    @property
    def c_cpu_topo(self):
        return self.__c_cpu_topo

    @property
    def coreId(self):
        return self.c_cpu_topo.coreId

    @coreId.setter
    def coreId(self, coreId):
        self.c_cpu_topo.coreId = ctypes.c_int(coreId)

    @property
    def numaId(self):
        return self.c_cpu_topo.numaId

    @numaId.setter
    def numaId(self, numaId):
        self.c_cpu_topo.numaId = ctypes.c_int(numaId)

    @property
    def socketId(self):
        return self.c_cpu_topo.socketId

    @socketId.setter
    def socketId(self, socketId):
        self.c_cpu_topo.socketId = ctypes.c_int(socketId)

    @classmethod
    def from_c_cpu_topo(cls, c_cpu_topo):
        cpu_topo = cls()
        cpu_topo.__c_cpu_topo = c_cpu_topo
        return cpu_topo


class CtypesSampleRawData(ctypes.Structure):
    _fields_ = [
        ('data', ctypes.c_char_p)
    ]

    def __init__(self, data='', *args, **kw):
        super(CtypesSampleRawData, self).__init__(*args, **kw)
        self.data = ctypes.c_char_p(data.encode(UTF_8))


class SampleRawData:
    __slots__ = ['__c_sample_rawdata']

    def __init__(self, data=''):
        self.__c_sample_rawdata = CtypesSampleRawData(data)

    @property
    def c_pmu_data_rawData(self):
        return self.__c_sample_rawdata

    @property
    def data(self):
        return self.__c_sample_rawdata.data.decode(UTF_8)

    @classmethod
    def from_sample_raw_data(cls, c_sample_raw_data):
        sample_raw_data = cls()
        sample_raw_data.__c_sample_rawdata = c_sample_raw_data
        return sample_raw_data


class CytpesBranchSampleRecord(ctypes.Structure):
    _fields_ = [
        ("fromAddr",    ctypes.c_ulong),
        ("toAddr",      ctypes.c_ulong),
        ("cycles",      ctypes.c_ulong),
        ("misPred",     ctypes.c_ubyte),
        ("predicted",   ctypes.c_ubyte),
    ]


class CtypesBranchRecords(ctypes.Structure):
    _fields_ = [
        ("nr", ctypes.c_ulong),
        ("branchRecords", ctypes.POINTER(CytpesBranchSampleRecord))
    ]


class ImplBranchRecords():
    __slots__ = ['__c_branch_record']

    def __init__(self,
                 fromAddr=0,
                 toAddr=0,
                 cycles=0,
                 misPred=0,
                 predicted=0):
        self.__c_branch_record = CytpesBranchSampleRecord(
            fromAddr=fromAddr,
            toAddr=toAddr,
            cycles=cycles,
            misPred=misPred,
            predicted=predicted,
        )

    @property
    def c_branch_record(self):
        return self.__c_branch_record

    @property
    def fromAddr(self):
        return self.c_branch_record.fromAddr
    
    @property
    def toAddr(self):
        return self.c_branch_record.toAddr
    
    @property
    def cycles(self):
        return self.c_branch_record.cycles

    @property
    def misPred(self):
        return self.c_branch_record.misPred
    
    @property
    def predicted(self):
        return self.c_branch_record.predicted
    
    @classmethod
    def from_c_branch_record(cls, c_branch_record):
        branch_record = cls()
        branch_record.__c_branch_record = c_branch_record
        return branch_record


class BranchRecords():
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer = None, nr=0):
        self.__pointer = pointer
        self.__len = nr
        self.__iter = (ImplBranchRecords.from_c_branch_record(self.__pointer[i]) for i in range(self.__len))
    
    @property
    def len(self):
        return self.__len

    @property
    def iter(self):
        return self.__iter
    
class CytpesSpeDataExt(ctypes.Structure):
    _fields_ = [
        ('pa',    ctypes.c_ulong),
        ('va',    ctypes.c_ulong),
        ('event', ctypes.c_ulong),
        ('lat',   ctypes.c_ushort),
    ]
    def __init__(self,
                 pa=0,
                 va=0,
                 event=0,
                 lat=0,
                 *args, **kw):
        super(CytpesSpeDataExt, self).__init__(*args, **kw)
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
    def c_pmu_data_ext(self):
        return self.__c_pmu_data_ext

    @property
    def pa(self):
        return self.c_pmu_data_ext.ext.speDataExt.pa

    @property
    def va(self):
        return self.c_pmu_data_ext.ext.speDataExt.va

    @property
    def event(self):
        return self.c_pmu_data_ext.ext.speDataExt.event

    @property
    def lat(self):
        return self.c_pmu_data_ext.ext.speDataExt.lat

    @property
    def branchRecords(self):
        if self.__c_pmu_data_ext.ext.branchRecords.branchRecords:
            return BranchRecords(self.__c_pmu_data_ext.ext.branchRecords.branchRecords, self.__c_pmu_data_ext.ext.branchRecords.nr)
        return None

    @classmethod
    def from_pmu_data_ext(cls, c_pmu_data_ext):
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
                 field_name='',
                 field_str='',
                 offset=0,
                 size=0,
                 is_signed=0,
                 *args, **kw):
        super(CtypesSampleRawField, self).__init__(*args, **kw)
        self.fieldName = ctypes.c_char_p(field_name.encode(UTF_8))
        self.fieldStr = ctypes.c_char_p(field_str.encode(UTF_8))
        self.offset = ctypes.c_uint(offset)
        self.size = ctypes.c_uint(size)
        self.isSigned = ctypes.c_uint(is_signed)

class SampleRawField:

    __slots__ = ['__c_sample_raw_field']

    def __init__(self,
                 field_name='',
                 field_str='',
                 offset=0,
                 size=0,
                 is_signed=0):
        self.__c_sample_raw_field = CtypesSampleRawField(field_name, field_str, offset, size, is_signed)

    @property
    def c_sample_raw_field(self):
        return self.__c_sample_raw_field

    @property
    def field_name(self):
        return self.__c_sample_raw_field.fieldName.decode(UTF_8)

    @property
    def field_str(self):
        return self.__c_sample_raw_field.fieldStr.decode(UTF_8)

    @property
    def size(self):
        return self.__c_sample_raw_field.size

    @property
    def offset(self):
        return self.__c_sample_raw_field.offset

    @property
    def is_signed(self):
        return bool(self.__c_sample_raw_field.isSigned)

    @classmethod
    def from_sample_raw_field(cls, __c_sample_raw_field):
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
        int cpu;                        // cpu id
        int groupId;                    // id for group event
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
        ('groupId', ctypes.c_int),
        ('cpuTopo', ctypes.POINTER(CtypesCpuTopology)),
        ('comm',    ctypes.c_char_p),
        ('period',  ctypes.c_uint64),
        ('count',   ctypes.c_uint64),
        ('countPercent', ctypes.c_double),
        ('ext',     ctypes.POINTER(CtypesPmuDataExt)),
        ('rawData', ctypes.POINTER(CtypesSampleRawData)),
        ('cgroupName', ctypes.c_char_p),
    ]

    def __init__(self,
                 stack=None,
                 evt='',
                 ts=0,
                 pid=0,
                 tid=0,
                 cpu=0,
                 groupId=0,
                 cpuTopo=None,
                 comm='',
                 period=0,
                 count=0,
                 countPercent=0.0,
                 ext=None,
                 rawData=None,
                 cgroupName='',
                 *args, **kw):
        super(CtypesPmuData, self).__init__(*args, **kw)

        self.stack = stack
        self.evt = ctypes.c_char_p(evt.encode(UTF_8))
        self.ts = ctypes.c_int64(ts)
        self.pid = ctypes.c_int(pid)
        self.tid = ctypes.c_int(tid)
        self.cpu = ctypes.c_int(cpu)
        self.groupId = ctypes.c_int(groupId)
        self.cpuTopo = cpuTopo
        self.comm = ctypes.c_char_p(comm.encode(UTF_8))
        self.period = ctypes.c_uint64(period)
        self.count = ctypes.c_uint64(count)
        self.countPercent = ctypes.c_double(countPercent)
        self.ext = ext
        self.rawData = rawData
        self.cgroupName = ctypes.c_char_p(cgroupName.encode(UTF_8))


class ImplPmuData:
    __slots__ = ['__c_pmu_data']

    def __init__(self,
                 stack=None,
                 evt='',
                 ts=0,
                 pid=0,
                 tid=0,
                 cpu=0,
                 groupId=0,
                 cpuTopo=None,
                 comm='',
                 period=0,
                 count=0,
                 countPercent=0.0,
                 ext=None,
                 rawData=None,
                 cgroupName=''):
        self.__c_pmu_data = CtypesPmuData(
            stack=stack.c_stack if stack else None,
            evt=evt,
            ts=ts,
            pid=pid,
            tid=tid,
            cpu=cpu,
            groupId=groupId,
            cpuTopo=cpuTopo.c_cpu_topo if cpuTopo else None,
            comm=comm,
            period=period,
            count=count,
            countPercent=countPercent,
            ext=ext.c_pmu_data_ext if ext else None,
            rawData=rawData.c_pmu_data_rawData if rawData else None,
            cgroupName=cgroupName
        )

    @property
    def c_pmu_data(self):
        return self.__c_pmu_data

    @property
    def stack(self):
        return Stack.from_c_stack(self.c_pmu_data.stack.contents) if self.c_pmu_data.stack else None

    @stack.setter
    def stack(self, stack):
        self.c_pmu_data.stack = stack.c_stack if stack else None

    @property
    def evt(self):
        return self.c_pmu_data.evt.decode(UTF_8)

    @evt.setter
    def evt(self, evt):
        self.c_pmu_data.evt = ctypes.c_char_p(evt.encode(UTF_8))

    @property
    def ts(self):
        return self.c_pmu_data.ts

    @ts.setter
    def ts(self, ts):
        self.c_pmu_data.ts = ctypes.c_int64(ts)

    @property
    def pid(self):
        return self.c_pmu_data.pid

    @pid.setter
    def pid(self, pid):
        self.c_pmu_data.pid = ctypes.c_int(pid)

    @property
    def tid(self):
        return self.c_pmu_data.tid

    @tid.setter
    def tid(self, tid):
        self.c_pmu_data.tid = ctypes.c_int(tid)

    @property
    def cpu(self):
        return self.c_pmu_data.cpu

    @cpu.setter
    def cpu(self, cpu):
        self.c_pmu_data.cpu = ctypes.c_int(cpu)

    @property
    def groupId(self):
        return self.c_pmu_data.groupId

    @groupId.setter
    def groupId(self, groupId):
        self.c_pmu_data.groupId = ctypes.c_int(groupId)

    @property
    def cpuTopo(self):
        return CpuTopology.from_c_cpu_topo(self.c_pmu_data.cpuTopo.contents) if self.c_pmu_data.cpuTopo else None

    @cpuTopo.setter
    def cpuTopo(self, cpuTopo):
        self.c_pmu_data.cpuTopo = cpuTopo.c_cpu_topo if cpuTopo else None

    @property
    def comm(self):
        return self.c_pmu_data.comm.decode(UTF_8)

    @comm.setter
    def comm(self, comm):
        self.c_pmu_data.comm = ctypes.c_char_p(comm.encode(UTF_8))

    @property
    def period(self):
        return self.c_pmu_data.period

    @period.setter
    def period(self, period):
        self.c_pmu_data.period = ctypes.c_uint64(period)

    @property
    def count(self):
        return self.c_pmu_data.count

    @count.setter
    def count(self, count):
        self.c_pmu_data.count = ctypes.c_uint64(count)

    @property
    def countPercent(self):
        return self.c_pmu_data.countPercent
    
    @countPercent.setter
    def countPercent(self, countPercent):
        self.c_pmu_data.countPercent = ctypes.c_double(countPercent)
        
    @property
    def ext(self):
        return PmuDataExt.from_pmu_data_ext(self.c_pmu_data.ext.contents) if self.c_pmu_data.ext else None

    @property
    def rawData(self):
        return SampleRawData.from_sample_raw_data(self.c_pmu_data.rawData) if self.c_pmu_data.rawData else None

    @ext.setter
    def ext(self, ext):
        self.c_pmu_data.ext = ext.c_pmu_data_ext if ext else None
    
    @property
    def cgroupName(self):
        return self.c_pmu_data.cgroupName.decode(UTF_8)

    @cgroupName.setter
    def cgroupName(self, cgroupName):
        self.c_pmu_data.cgroupName = ctypes.c_char_p(cgroupName.encode(UTF_8))

    @classmethod
    def from_c_pmu_data(cls, c_pmu_data):
        pmu_data = cls()
        pmu_data.__c_pmu_data = c_pmu_data
        return pmu_data


class PmuData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer = None, len=0):
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuData.from_c_pmu_data(self.__pointer[i]) for i in range(self.__len))

    def __del__(self):
        self.free()

    def __len__(self):
        return self.__len
    
    def __iter__(self):
        return self.__iter
    
    def pointer(self):
        return self.__pointer
    
    @property
    def len(self):
        return self.__len

    @property
    def iter(self):
        return self.__iter

    def free(self):
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
                 funcs= '',
                 startTs=0,
                 elapsedTime=0.0,
                 pid=0,
                 tid=0,
                 cpu=0,
                 comm= '',
                 *args, **kw):
        super(CtypesPmuTraceData, self).__init__(*args, **kw)

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
                 funcs= '',
                 startTs=0,
                 elapsedTime=0.0,
                 pid=0,
                 tid=0,
                 cpu=0,
                 comm= '',
                 *args, **kw):
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
    def c_pmu_trace_data(self):
        return self.__c_pmu_trace_data
    
    @property
    def funcs(self):
        return self.__c_pmu_trace_data.funcs.decode(UTF_8)
    
    @funcs.setter
    def funcs(self, funcs):
        self.__c_pmu_trace_data.funcs = ctypes.c_char_p(funcs.encode(UTF_8))

    @property
    def startTs(self):
        return self.__c_pmu_trace_data.startTs
    
    @startTs.setter
    def startTs(self, startTs):
        self.__c_pmu_trace_data.startTs = ctypes.c_int64(startTs)
    
    @property
    def elapsedTime(self):
        return self.__c_pmu_trace_data.elapsedTime
    
    @elapsedTime.setter
    def elapsedTime(self, elapsedTime):
        self.__c_pmu_trace_data.elapsedTime = ctypes.c_double(elapsedTime)
    
    @property
    def pid(self):
        return self.__c_pmu_trace_data.pid
    
    @pid.setter
    def pid(self, pid):
        self.__c_pmu_trace_data.pid = ctypes.c_int(pid)
    
    @property
    def tid(self):
        return self.__c_pmu_trace_data.tid

    @tid.setter
    def tid(self, tid):
        self.__c_pmu_trace_data.tid = ctypes.c_int(tid)
    
    @property
    def cpu(self):
        return self.__c_pmu_trace_data.cpu
    
    @cpu.setter
    def cpu(self, cpu):
        self.__c_pmu_trace_data.cpu = ctypes.c_int(cpu)
    
    @property
    def comm(self):
        return self.__c_pmu_trace_data.comm.decode(UTF_8)
    
    @comm.setter
    def comm(self, comm):
        self.__c_pmu_trace_data.comm = ctypes.c_char_p(comm.encode(UTF_8))
    
    @classmethod
    def from_c_pmu_trace_data(cls, c_pmu_trace_data):
        pmu_trace_data = cls()
        pmu_trace_data.__c_pmu_trace_data = c_pmu_trace_data
        return pmu_trace_data

class PmuTraceData:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer = None, len=0):
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuTraceData.from_c_pmu_trace_data(self.__pointer[i]) for i in range(self.__len))
    
    def __del__(self):
        self.free()
    
    @property
    def len(self):
        return self.__len
    
    @property
    def iter(self):
        return self.__iter
    
    def free(self):
        if self.__pointer is not None:
            PmuTraceDataFree(self.__pointer)
            self.__pointer = None

def PmuOpen(collectType, pmuAttr):
    """
    int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);
    """
    c_PmuOpen = kperf_so.PmuOpen
    c_PmuOpen.argtypes = [ctypes.c_int, ctypes.POINTER(CtypesPmuAttr)]
    c_PmuOpen.restype = ctypes.c_int

    c_collectType = ctypes.c_int(collectType)

    return c_PmuOpen(c_collectType, ctypes.byref(pmuAttr.c_pmu_attr))


def PmuEventListFree():
    """
    void PmuEventListFree();
    """
    c_PmuEventListFree = kperf_so.PmuEventListFree
    c_PmuEventListFree.argtypes = []
    c_PmuEventListFree.restype = None

    c_PmuEventListFree()


def PmuEventList(eventType):
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


def PmuEnable(pd):
    """
    int PmuEnable(int pd);
    """
    c_PmuEnable = kperf_so.PmuEnable
    c_PmuEnable.argtypes = [ctypes.c_int]
    c_PmuEnable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuEnable(c_pd)


def PmuDisable(pd):
    """
    int PmuDisable(int pd);
    """
    c_PmuDisable = kperf_so.PmuDisable
    c_PmuDisable.argtypes = [ctypes.c_int]
    c_PmuDisable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuDisable(c_pd)


def PmuCollect(pd, milliseconds, interval):
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


def PmuStop(pd):
    """
    void PmuStop(int pd);
    """
    c_PmuStop = kperf_so.PmuStop
    c_PmuStop.argtypes = [ctypes.c_int]
    c_PmuStop.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuStop(c_pd)


def PmuDataFree(pmuData):
    """
    void PmuDataFree(struct PmuData* pmuData);
    """
    c_PmuDataFree = kperf_so.PmuDataFree
    c_PmuDataFree.argtypes = [ctypes.POINTER(CtypesPmuData)]
    c_PmuDataFree.restype = None
    c_PmuDataFree(pmuData)


def PmuRead(pd):
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

def ResolvePmuDataSymbol(pmuData):
    """
    int ResolvePmuDataSymbol(struct PmuData* pmuData);
    """
    c_ResolvePmuDataSymbol = kperf_so.ResolvePmuDataSymbol
    c_ResolvePmuDataSymbol.argtypes = [ctypes.POINTER(CtypesPmuData)]
    c_ResolvePmuDataSymbol.restype = ctypes.c_int

    return c_ResolvePmuDataSymbol(pmuData)

def PmuAppendData(fromData, toData):
    """
    int PmuAppendData(struct PmuData *fromData, struct PmuData **toData);
    """
    c_PmuAppendData = kperf_so.PmuAppendData
    c_PmuAppendData.argtypes = [ctypes.POINTER(CtypesPmuData), ctypes.POINTER(ctypes.POINTER(CtypesPmuData))]
    c_PmuAppendData.restype = ctypes.c_int

    return c_PmuAppendData(fromData, toData)


def PmuClose(pd):
    """
    void PmuClose(int pd);
    """
    c_PmuClose = kperf_so.PmuClose
    c_PmuClose.argtypes = [ctypes.c_int]
    c_PmuClose.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuClose(c_pd)


def PmuDumpData(pmuData, filepath, dumpDwf):
    """
    int PmuDumpData(struct PmuData *pmuData, unsigned len, char *filepath, int dumpDwf);
    """
    c_PmuDumpData = kperf_so.PmuDumpData
    c_PmuDumpData.argtypes = [ctypes.POINTER(CtypesPmuData), ctypes.c_uint, ctypes.c_char_p, ctypes.c_int]
    c_PmuDumpData.restype = ctypes.c_int

    c_len = ctypes.c_uint(pmuData.len)
    c_filepath = ctypes.c_char_p(filepath.encode(UTF_8))
    c_dumpDwf = ctypes.c_int(dumpDwf)

    c_PmuDumpData(pmuData.pointer(), c_len, c_filepath, c_dumpDwf)


def PmuGetField(rawData, field_name, value, vSize):
    """
    int PmuGetField(struct SampleRawData *rawData, const char *fieldName, void *value, uint32_t vSize);
    """
    c_PmuGetField = kperf_so.PmuGetField
    c_PmuGetField.argtypes = [ctypes.POINTER(CtypesSampleRawData), ctypes.c_char_p, ctypes.c_void_p,
                                        ctypes.c_uint]
    c_PmuGetField.restype = ctypes.c_int
    return c_PmuGetField(rawData, field_name.encode(UTF_8), value, vSize)


def PmuGetFieldExp(rawData, field_name):
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


def PmuDeviceBdfListFree():
    """
    void PmuDeviceBdfListFree()
    """
    c_PmuDeviceBdfListFree = kperf_so.PmuDeviceBdfListFree
    c_PmuDeviceBdfListFree.argtypes = []
    c_PmuDeviceBdfListFree.restype = None

    c_PmuDeviceBdfListFree()

def PmuDeviceBdfList(bdf_type):
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


def PmuDeviceOpen(device_attr):
    """
    int PmuDeviceOpen(struct PmuDeviceAttr *deviceAttr, unsigned len);
    """
    c_PmuDeviceOpen = kperf_so.PmuDeviceOpen
    c_PmuDeviceOpen.argtypes = [ctypes.POINTER(CtypesPmuDeviceAttr), ctypes.c_uint]
    c_PmuDeviceOpen.restype = ctypes.c_int
    c_num_device = len(device_attr)
    c_device_attr = (CtypesPmuDeviceAttr * c_num_device)(*[attr.c_pmu_device_attr for attr in device_attr])
    return c_PmuDeviceOpen(c_device_attr, c_num_device)


def PmuGetDevMetric(pmu_data, device_attr):
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
    if res <=0:
        return PmuDeviceData()
    
    return PmuDeviceData(c_device_data, res)

def DevDataFree(dev_data):
    """
    void DevDataFree(struct PmuDeviceData *devData);
    """
    c_DevDataFree = kperf_so.DevDataFree
    c_DevDataFree.argtypes = [ctypes.POINTER(CtypesPmuDeviceData)]
    c_DevDataFree.restype = None

    c_DevDataFree(dev_data)

def PmuGetCpuFreq(core):
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

def PmuGetClusterCore(clusterId):
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

def PmuGetNumaCore(numaId):
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


def PmuTraceOpen(traceType, pmuTraceAttr):
    """
    int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);
    """
    c_PmuTraceOpen = kperf_so.PmuTraceOpen
    c_PmuTraceOpen.argtypes = [ctypes.c_int, ctypes.POINTER(CtypesPmuTraceAttr)]
    c_PmuTraceOpen.restype = ctypes.c_int

    c_traceType = ctypes.c_int(traceType)

    return c_PmuTraceOpen(c_traceType, ctypes.byref(pmuTraceAttr.c_pmu_trace_attr))

def PmuTraceEnable(pd):
    """
    int PmuTraceEnable(int pd);
    """
    c_PmuTraceEnable = kperf_so.PmuTraceEnable
    c_PmuTraceEnable.argtypes = [ctypes.c_int]
    c_PmuTraceEnable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuTraceEnable(c_pd)

def PmuTraceDisable(pd):
    """
    int PmuTraceDisable(int pd);
    """
    c_PmuTraceDisable = kperf_so.PmuTraceDisable
    c_PmuTraceDisable.argtypes = [ctypes.c_int]
    c_PmuTraceDisable.restype = ctypes.c_int

    c_pd = ctypes.c_int(pd)

    return c_PmuTraceDisable(c_pd)

def PmuTraceRead(pd):
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

def PmuTraceClose(pd):
    """
    void PmuTraceClose(int pd);
    """
    c_PmuTraceClose = kperf_so.PmuTraceClose
    c_PmuTraceClose.argtypes = [ctypes.c_int]
    c_PmuTraceClose.restype = None

    c_pd = ctypes.c_int(pd)

    c_PmuTraceClose(c_pd)

def PmuTraceDataFree(pmuTraceData):
    """
    void PmuTraceDataFree(struct PmuTraceData* pmuTraceData);
    """
    c_PmuTraceDataFree = kperf_so.PmuTraceDataFree
    c_PmuTraceDataFree.argtypes = [ctypes.POINTER(CtypesPmuTraceData)]
    c_PmuTraceDataFree.restype = None
    c_PmuTraceDataFree(pmuTraceData)

def PmuSysCallFuncList():
    """
    char **PmuSysCallFuncList(unsigned *numFunc);
    """
    c_PmuSysCallFuncList = kperf_so.PmuSysCallFuncList
    c_PmuSysCallFuncList.argtypes = []
    c_PmuSysCallFuncList.restype = ctypes.POINTER(ctypes.c_char_p)
    
    c_num_func = ctypes.c_uint()
    c_func_list = c_PmuSysCallFuncList(ctypes.byref(c_num_func))

    return (c_func_list[i].decode(UTF_8) for i in range(c_num_func.value))

def PmuSysCallFuncListFree():
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
                 cpuId=0,
                 minFreq=0,
                 maxFreq=0,
                 avgFreq=0,
                 *args, **kw):
        super(CtypesPmuCpuFreqDetail, self).__init__(*args, **kw)
        self.cpuId = ctypes.c_int(cpuId)
        self.minFreq = ctypes.c_uint64(minFreq)
        self.maxFreq = ctypes.c_uint64(maxFreq)
        self.avgFreq = ctypes.c_uint64(avgFreq)


class ImplPmuCpuFreqDetail:
    __slots__ = ['__c_pmu_cpu_freq_detail']
    def __init__(self,
                 cpuId=0,
                 minFreq=0,
                 maxFreq=0,
                 avgFreq=0,
                 *args, **kw):
        self.__c_pmu_cpu_freq_detail = CtypesPmuCpuFreqDetail(
            cpuId=cpuId,
            minFreq=minFreq,
            maxFreq=maxFreq,
            avgFreq=avgFreq
        )
    
    @property
    def c_pmu_cpu_freq_detail(self):
        return self.__c_pmu_cpu_freq_detail
    
    @property
    def cpuId(self):
        return self.__c_pmu_cpu_freq_detail.cpuId
    
    @cpuId.setter
    def cpuId(self, cpuId):
        self.__c_pmu_cpu_freq_detail.cpuId = ctypes.c_int(cpuId)
    
    @property
    def minFreq(self):
        return self.__c_pmu_cpu_freq_detail.minFreq
    
    @minFreq.setter
    def minFreq(self, minFreq):
        self.__c_pmu_cpu_freq_detail.minFreq = ctypes.c_uint64(minFreq)
    
    @property
    def maxFreq(self):
        return self.__c_pmu_cpu_freq_detail.maxFreq
    
    @maxFreq.setter
    def maxFreq(self, maxFreq):
        self.__c_pmu_cpu_freq_detail.maxFreq = ctypes.c_uint64(maxFreq)
    
    @property
    def avgFreq(self):
        return self.__c_pmu_cpu_freq_detail.avgFreq
    
    @avgFreq.setter
    def avgFreq(self, avgFreq):
        self.__c_pmu_cpu_freq_detail.avgFreq = ctypes.c_uint64(avgFreq)
    
    @classmethod
    def from_c_pmu_cpu_freq_detail(cls, c_pmu_cpu_freq_detail):
        freq_detail = cls()
        freq_detail.__c_pmu_cpu_freq_detail = c_pmu_cpu_freq_detail
        return freq_detail


class PmuCpuFreqDetail:
    __slots__ = ['__pointer', '__iter', '__len']

    def __init__(self, pointer=None, len=0):
        self.__pointer = pointer
        self.__len = len
        self.__iter = (ImplPmuCpuFreqDetail.from_c_pmu_cpu_freq_detail(self.__pointer[i]) for i in range(self.__len))
    
    @property
    def len(self):
        return self.__len

    @property
    def iter(self):
        return self.__iter


def PmuReadCpuFreqDetail():
    """
    struct PmuCpuFreqDetail* PmuReadCpuFreqDetail(unsigned* cpuNum);
    """
    c_PmuGetCpuFreqDetail = kperf_so.PmuReadCpuFreqDetail
    c_PmuGetCpuFreqDetail.argtypes = []
    c_PmuGetCpuFreqDetail.restype  = ctypes.POINTER(CtypesPmuCpuFreqDetail)
    c_cpu_len = ctypes.c_uint(0)
    c_freq_detail_pointer = c_PmuGetCpuFreqDetail(ctypes.byref(c_cpu_len))

    return PmuCpuFreqDetail(c_freq_detail_pointer, c_cpu_len.value)

def PmuOpenCpuFreqSampling(period):
    """
    int PmuOpenCpuFreqSampling(unsigned period);
    """
    c_PmuOpenCpuFreqSampling = kperf_so.PmuOpenCpuFreqSampling

    c_period = ctypes.c_uint(period)
    return c_PmuOpenCpuFreqSampling(c_period)

def PmuCloseCpuFreqSampling():
    """
    void PmuCloseCpuFreqSampling();
    """
    c_PmuCloseCpuFreqSampling = kperf_so.PmuCloseCpuFreqSampling
    c_PmuCloseCpuFreqSampling()

def PmuBeginWrite(path, pattr):
    """
    PmuFile PmuBeginWrite(const char *path, const PmuAttr *pattr)
    """
    c_func = kperf_so.PmuBeginWrite
    c_func.argtypes = [ctypes.c_char_p, ctypes.POINTER(CtypesPmuAttr)]
    c_func.restype = ctypes.c_void_p

    c_filepath = ctypes.c_char_p(path.encode(UTF_8))

    return c_func(c_filepath, ctypes.byref(pattr.c_pmu_attr))

def PmuWriteData(file, data):
    """
    int PmuWriteData(PmuFile file, PmuData *data, int len)
    """
    c_func = kperf_so.PmuWriteData
    c_func.argtypes = [ctypes.c_void_p, ctypes.POINTER(CtypesPmuData), ctypes.c_uint]
    c_func.restype = ctypes.c_int

    c_len = ctypes.c_uint(data.len)
    return c_func(file, data.pointer(), c_len)

def PmuEndWrite(file):
    """
    void PmuEndWrite(PmuFile file)
    """
    c_func = kperf_so.PmuEndWrite
    c_func.argtypes = [ctypes.c_void_p]

    return c_func(file)

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
    'ResolvePmuDataSymbol',
    'PmuBeginWrite',
    'PmuWriteData',
    'PmuEndWrite'
]
