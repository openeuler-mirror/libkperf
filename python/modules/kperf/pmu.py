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

class PmuDeviceMetric:
    # Perchannel metric.
    # Collect ddr read bandwidth for each channel.
    # Unit: Bytes/s
    PMU_DDR_READ_BW = 0
    # Perchannel metric.
    # Collect ddr write bandwidth for each channel.
    # Unit: Bytes/s
    PMU_DDR_WRITE_BW = 1
    # Percore metric.
    # Collect L3 access bytes for each cpu core.
    # Unit: Bytes
    PMU_L3_TRAFFIC = 2
    # Percore metric.
    # Collect L3 miss count for each cpu core.
    # Unit: count
    PMU_L3_MISS = 3
    # Percore metric.
    # Collect L3 total reference count, including miss and hit count.
    # Unit: count
    PMU_L3_REF = 4
    # Percluster metric.
    # Collect L3 total latency for each cluster node.
    # Unit: cycles
    PMU_L3_LAT = 5
    # Collect pcie rx bandwidth.
    # Perpcie metric.
    # Collect pcie rx bandwidth for pcie device.
    # Unit: Bytes/us
    PMU_PCIE_RX_MRD_BW = 6
    PMU_PCIE_RX_MWR_BW = 7
    # Perpcie metric.
    # Collect pcie tx bandwidth for pcie device.
    # Unit: Bytes/us
    PMU_PCIE_TX_MRD_BW = 8
    PMU_PCIE_TX_MWR_BW = 9
    # Perpcie metric.
    # Collect pcie rx latency for pcie device.
    # Unit: ns
    PMU_PCIE_RX_MRD_LAT = 10
    PMU_PCIE_RX_MWR_LAT = 11
    # Perpcie metric.
    # Collect pcie tx latency for pcie device.
    # Unit: ns
    PMU_PCIE_TX_MRD_LAT = 12
    # Perpcie metric.
    # Collect smmu address transaction.
    # Unit: count
    PMU_SMMU_TRAN = 13
    # Pernuma metric.
    # Collect rate of cross-numa operations received by HHA.
    PMU_HHA_CROSS_NUMA = 14
    # Pernuma metric.
    # Collect rate of cross-socket operations received by HHA.
    PMU_HHA_CROSS_SOCKET = 15

class PmuDeviceAttr(_libkperf.PmuDeviceAttr):
    """
    struct PmuDeviceAttr {
        enum PmuDeviceMetric metric;

        // Used for PMU_PCIE_XXX_BW and PMU_SMMU_XXX to collect a specifi pcie device.
        // The string of bdf is something like '7a:01.0'.
        char *bdf;
        // Used for PMU_PCIE_XXX_LAT to collect latency data.
        // Only one port supported.
        char *port;
    };
    """
    def __init__(self, metric, bdf=None, port=None):
        super(PmuDeviceAttr, self).__init__(
            metric=metric,
            bdf=bdf,
            port=port
        )

class PmuBdfType:
    PMU_BDF_TYPE_PCIE = 0   # pcie collect metric.
    PMU_BDF_TYPE_SMMU = 1   # smmu collect metric.

class PmuMetricMode:
    PMU_METRIC_INVALID = 0
    PMU_METRIC_CORE = 1
    PMU_METRIC_NUMA = 2
    PMU_METRIC_CLUSTER = 3
    PMU_METRIC_BDF = 4
    PMU_METRIC_CHANNEL = 5

class ImplPmuDeviceData(_libkperf.ImplPmuDeviceData):
    pass

class PmuDeviceData(_libkperf.PmuDeviceData):
    """
    struct PmuDeviceData {
        enum PmuDeviceMetric metric;
        // The metric value. The meaning of value depends on metric type.
        // Refer to comments of PmuDeviceMetric for detailed info.
        double count;
        enum PmuMetricMode mode;
        // Field of union depends on the above <mode>.
        union {
            // for percore metric
            unsigned coreId;
            // for pernuma metric
            unsigned numaId;
            // for perpcie metric
            char *bdf;
            char *port;
            // for perchannel metric of ddr
            struct {
                unsigned channelId;
                unsigned ddrNumaId;
                unsigned socketId;
            };
        };
    };
    """
    pass

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
        branchSampleFilter: if the filter mode is set, branch_sample_stack data is collected in sampling mode
        cgroupNameList: cgroup name list, can not assigned with pidList.
        enableUserAccess: In count mode, enable read the register directly to collect data
        enableBpf: In count mode, enable bpf to collect data.
    """
    def __init__(self,
                 evtList = None, 
                 pidList = None,
                 cpuList = None,
                 evtAttr = None,
                 sampleRate = 0,
                 useFreq = False,
                 excludeUser = False,
                 excludeKernel = False,
                 symbolMode = 0,
                 callStack = False,
                 blockedSample = False,
                 dataFilter = 0,
                 evFilter = 0,
                 minLatency = 0,
                 includeNewFork = False,
                 branchSampleFilter = 0,
                 cgroupNameList = None,
                 enableUserAccess = False,
                 enableBpf = False):
        super(PmuAttr, self).__init__(
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
            enableUserAccess=enableUserAccess,
            enableBpf=enableBpf,
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
                 funcs = None,
                 pidList = None,
                 cpuList = None):
        super(PmuTraceAttr, self).__init__(
            funcs=funcs,
            pidList=pidList,
            cpuList=cpuList
        )

class ImplPmuTraceData(_libkperf.ImplPmuTraceData):
    pass

class PmuTraceData(_libkperf.PmuTraceData):
    pass

def open(collect_type, pmu_attr):
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


def event_list(event_type):
    """
    Query all available event from system.
    :param event_type: type of event chosen by user
    :return: event list
    """
    return _libkperf.PmuEventList(int(event_type))


def enable(pd):
    """
    Enable counting or sampling of task <pd>.
    On success, 0 is returned.
    On error, -1 is returned.
    :param pd: task id
    :return: error code
    """
    return _libkperf.PmuEnable(pd)


def disable(pd):
    """
    Disable counting or sampling of task <pd>.
    On success, 0 is returned.
    On error, -1 is returned.
    :param pd: task id
    :return: error code
    """
    return _libkperf.PmuDisable(pd)


def read(pd):
    """
    Collect data.
    Pmu data are collected starting from the last PmuEnable or PmuRead.
    That is to say, for COUNTING, counts of all pmu event are reset to zero in PmuRead.
    For SAMPLING and SPE_SAMPLING, samples collected are started from the last PmuEnable or PmuRead.
    :param pd: task id
    :return: pmu data
    """
    return _libkperf.PmuRead(pd)

def resolvePmuDataSymbol(pmuData):
    """
    when kperf symbol mode is NO_SYMBOL_RESOLVE during PmuRead(), this function can be used to resolve stack symbols
    :param: pmuData
    :return: pmu data
    """
    return _libkperf.ResolvePmuDataSymbol(pmuData.pointer())


def stop(pd):
    """
    stop a sampling task in asynchronous mode
    :param pd: task id.
    """
    return _libkperf.PmuStop(pd)


def close(pd):
    """
    Close task with id <pd>.
    After PmuClose is called, all pmu data related to the task become invalid.
    :param pd: task id
    """
    return _libkperf.PmuClose(pd)


def exit(pd):
    """
    Close task with id <pd>.
    After PmuExit is called, the parsing symbol phase will be halted.
    :param pd: task id
    """
    return _libkperf.PmuExit(pd)


def dump(pmuData, filepath, dump_dwf):
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


def get_field(pmu_data, field_name, value):
    """
    get field value of trace pointer named field_name
    :param pmu_data: _libkperf.ImplPmuData
    :param field_name: field name
    :param value: pointer
    :return: 0 success other failed
    """
    return _libkperf.PmuGetField(pmu_data.rawData.c_pmu_data_rawData, field_name, value, sizeof(value))


def get_field_exp(pmu_data, field_name):
    """
    get the field detail of trace pointer event
    :param pmu_data: the _libkperf.ImplPmuData
    :param field_name: field name
    :return:SampleRawField
    """
    return _libkperf.PmuGetFieldExp(pmu_data.rawData.c_pmu_data_rawData, field_name)

def device_bdf_list(bdf_type):
    """
    Query all available BDF (Bus:Device.Function) list from system.
    :param bdf_type: type of bdf chosen by user
    :return: valid bdf list
    """
    return _libkperf.PmuDeviceBdfList(int(bdf_type))

def device_open(device_attr):
    """
    A high level interface for initializing PMU events for devices,
    such as L3 cache, DDRC, PCIe, and SMMU, to collect metrics like bandwidth, latency, and others.
    This interface is an alternative option for initializing events besides PmuOpen.
    :param device_attr: List of PmuDeviceAttr objects containing metrics to collect
    :return: Task Id, similar with returned value of PmuOpen
    """
    return _libkperf.PmuDeviceOpen(device_attr)

def get_device_metric(pmu_data, device_attr):
    """
    Get device performance metric data from pmu data
    :param pmu_data: raw data collected by pmu
    :param device_attr: list of device metric attributes to retrieve
    :return: list of device performance metric data
    """
    return _libkperf.PmuGetDevMetric(pmu_data, device_attr)


def get_cpu_freq(core):
    """
    Get cpu frequency
    :param core: cpu core id
    :return: cpu frequency
    """
    return _libkperf.PmuGetCpuFreq(core)


def get_cluster_core(clusterId):
    """
    Get the list of core in a cluster
    :param cluster: cluster id
    :return: the list of core ids in the cluster
    """
    return _libkperf.PmuGetClusterCore(clusterId)

def get_numa_core(numaId):
    """
    Get the list of core in a numa node
    :param numaId: numa node id
    :return: the list of core ids in the numa node
    """
    return _libkperf.PmuGetNumaCore(numaId)

def trace_open(trace_type, pmu_trace_attr):
    """
    int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);
    """
    return _libkperf.PmuTraceOpen(int(trace_type), pmu_trace_attr)

def trace_enable(pd):
    """
    int PmuTraceEnable(int pd);
    """
    return _libkperf.PmuTraceEnable(pd)

def trace_disable(pd):
    """
    int PmuTraceDisable(int pd);
    """
    return _libkperf.PmuTraceDisable(pd)

def trace_read(pd):
    """
    int PmuTraceRead(int pd, struct PmuTraceData **traceData);
    """
    return _libkperf.PmuTraceRead(pd)

def trace_close(pd):
    """
    void PmuTraceClose(int pd);
    """
    return _libkperf.PmuTraceClose(pd)

def sys_call_func_list():
    """
    get the system call function list
    :return: system call function list
    """
    return _libkperf.PmuSysCallFuncList()

class CpuFreqDetail(_libkperf.PmuCpuFreqDetail):
    pass

def open_cpu_freq_sampling(period):
    return _libkperf.PmuOpenCpuFreqSampling(period)

def close_cpu_freq_sampling():
    return _libkperf.PmuCloseCpuFreqSampling()

def read_cpu_freq_detail():
    return _libkperf.PmuReadCpuFreqDetail()

def begin_write(path, pattr, addIdHdr):
    return _libkperf.PmuBeginWrite(path, pattr, addIdHdr)

def write_data(file, data):
    return _libkperf.PmuWriteData(file, data)

def end_write(file):
    return _libkperf.PmuEndWrite(file)

__all__ = [
    'PmuTaskType',
    'PmuEventType',
    'PmuTraceType',
    'SpeFilter',
    'SpeEventFilter',
    'SpeEvent',
    'SymbolMode',
    'PmuAttr',
    'PmuDeviceMetric',
    'PmuDeviceAttr',
    'PmuBdfType',
    'PmuMetricMode',
    'ImplPmuDeviceData',
    'PmuDeviceData',
    'device_bdf_list',
    'device_open',
    'get_device_metric',
    'get_cpu_freq',
    'get_cluster_core',
    'get_numa_core',
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
    'exit',
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
    'CpuFreqDetail',
    'open_cpu_freq_sampling',
    'close_cpu_freq_sampling',
    'read_cpu_freq_detail',
    'resolvePmuDataSymbol',
    'begin_write',
    'write_data',
    'end_write'
]
