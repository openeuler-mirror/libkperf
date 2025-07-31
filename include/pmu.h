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
 * Description: declarations and definitions of interfaces and data structures exposed by perf.so
 ******************************************************************************/
#ifndef PMU_DATA_STRUCT_H
#define PMU_DATA_STRUCT_H
#include <unistd.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#pragma GCC visibility push(default)

enum PmuTaskType {
    COUNTING = 0, // pmu counting task
    SAMPLING = 1, // pmu sampling task
    SPE_SAMPLING = 2, // spe sampling task
    MAX_TASK_TYPE
};

enum PmuEventType {
    CORE_EVENT,
    UNCORE_EVENT,
    TRACE_EVENT,
    ALL_EVENT
};

enum SpeFilter {
    SPE_FILTER_NONE = 0,
    TS_ENABLE = 1UL << 0,       // enable timestamping with value of generic timer
    PA_ENABLE = 1UL << 1,       // collect physical address (as well as VA) of loads/stores
    PCT_ENABLE = 1UL << 2,      // collect physical timestamp instead of virtual timestamp
    JITTER = 1UL << 16,         // use jitter to avoid resonance when sampling
    BRANCH_FILTER = 1UL << 32,  // collect branches only
    LOAD_FILTER = 1UL << 33,    // collect loads only
    STORE_FILTER = 1UL << 34,   // collect stores only
    SPE_DATA_ALL = TS_ENABLE | PA_ENABLE | PCT_ENABLE | JITTER | BRANCH_FILTER | LOAD_FILTER | STORE_FILTER
};

enum SpeEventFilter {
    SPE_EVENT_NONE = 0,
    SPE_EVENT_RETIRED = 0x2,        // instruction retired
    SPE_EVENT_L1DMISS = 0x8,        // L1D refill
    SPE_EVENT_TLB_WALK = 0x20,      // TLB refill
    SPE_EVENT_MISPREDICTED = 0x80,  // mispredict
};

enum SymbolMode {
    // <stack> in PmuData will be set to NULL.
    NO_SYMBOL_RESOLVE = 0,
    // Resolve elf only. Fields except lineNum and fileName in Symbol will be valid. 
    RESOLVE_ELF = 1,
    // Resolve elf and dwarf. All fields in Symbol will be valid.
    RESOLVE_ELF_DWARF = 2
};

enum BranchSampleFilter {
    KPERF_NO_BRANCH_SAMPLE          = 0,
    /**
     * The first part of the value is the privilege level,which is a combination of 
     * one of the values listed below. If the user does not set privilege level explicitly,
     * the kernel will use the event's privilege level.Event and branch privilege levels do
     * not have to match.
     */
    KPERF_SAMPLE_BRANCH_USER        = 1U << 0,
    KPERF_SAMPLE_BRANCH_KERNEL      = 1U << 1,
    KPERF_SAMPLE_BRANCH_HV          = 1U << 2,
    // In addition to privilege value , at least one or more of the following bits must be set.
    KPERF_SAMPLE_BRANCH_ANY         = 1U << 3,
    KPERF_SAMPLE_BRANCH_ANY_CALL    = 1U << 4,
    KPERF_SAMPLE_BRANCH_ANY_RETURN  = 1U << 5,
    KPERF_SAMPLE_BRANCH_IND_CALL    = 1U << 6,
    KPERF_SAMPLE_BRANCH_ABORT_TX    = 1U << 7,
    KPERF_SAMPLE_BRANCH_IN_TX       = 1U << 8,
    KPERF_SAMPLE_BRANCH_NO_TX       = 1U << 9,
    KPERF_SAMPLE_BRANCH_COND        = 1U << 10,
    KPERF_SAMPLE_BRANCH_CALL_STACK  = 1U << 11,
    KPERF_SAMPLE_BRANCH_IND_JUMP    = 1U << 12,
    KPERF_SAMPLE_BRANCH_CALL        = 1U << 13,
    KPERF_SAMPLE_BRANCH_NO_FLAGES   = 1U << 14,
    KPERF_SAMPLE_BRANCH_NO_CYCLES   = 1U << 15,
    KPERF_SAMPLE_BRANCH_TYPE_SAVE   = 1U << 16,
};

struct EvtAttr {
    int groupId;
};

struct PmuAttr {
    // Event list.
    // Refer 'perf list' for details about event names.
    // Calling PmuEventList will also return the available event names.
    // Both event names like 'cycles' or event ids like 'r11' are allowed.
    // For uncore events, event names should be of the form '<device>/<event>/'.
    // For tracepoints, event names should be of the form '<system>:<event>'.
    // For spe sampling, this field should be NULL.
    char** evtList;
    // Length of event list.
    unsigned numEvt;
    // Pid list.
    // For multi-threaded programs, all threads will be monitored regardless whether threads are created before or after PmuOpen.
    // For multi-process programs, only processes created after PmuOpen are monitored.
    // For short-lived programs, PmuOpen may fail and return error code.
    // To collect system, set pidList to NULL and cpu cores will be monitored according to the field <cpuList>.
    int* pidList;
    // Length of pid list.
    unsigned numPid;
    // Core id list.
    // If both <cpuList> and <pidList> are NULL, all processes on all cores will be monitored.
    // If <cpuList> is NULL and <pidList> is not NULL, specified processes on all cores will be monitored.
    // if both <cpuList> and <pidList> are not NULL, specified processes on specified cores will be monitored.
    int* cpuList;
    // Length of core id list
    unsigned numCpu;

    // event group id 
    // if not use event group function, this field will be nullptr.
    // if use event group function. please confirm the event group id with eveList is one by one.
    // the same group id is the a event group. 
    // Note: if the group id value is -1, it indicates that the event is not grouped.
    struct EvtAttr *evtAttr;
    // Length of evtAttr list.
    // when numEvt > numGroup, the other events will not set to any group.
    unsigned numGroup;

    union {
        // Sample period, only available for SAMPLING and SPE_SAMPLING.
        unsigned period;
        // Sample frequency, only available for SAMPLING.
        unsigned freq;
    };
    // Use sample frequency or not.
    // If set to 1, the previous union will be used as sample frequency,
    // otherwise, it will be used as sample period.
    unsigned useFreq : 1;
    // Don't count user.
    unsigned excludeUser : 1;
    // Don't count kernel.
    unsigned excludeKernel : 1;
    // This indicates how to analyze symbols of samples.
    // Refer to comments of SymbolMode.
    enum SymbolMode symbolMode;
    // This indicates whether to collect whole callchains or only top frame.
    unsigned callStack : 1;
    // This indicates whether the blocked sample mode is enabled.
    // In this mode, both on CPU and off CPU data is collected.
    unsigned blockedSample : 1;

    // SPE related fields.

    // Spe data filter. Refer to comments of SpeFilter.
    enum SpeFilter dataFilter;
    // Spe event filter. Refer to comments of SpeEventFilter.
    enum SpeEventFilter evFilter;
    // Collect only samples with latency or higher.
    unsigned long minLatency;
    // In count mode, enable it you can get the new child thread count, default is disabled.
    unsigned includeNewFork : 1;
    // if the filtering mode is set, the branch_sample_stack data is collectd in sampling mode.By default,the filtering mode dose not take effect.
    unsigned long branchSampleFilter;
    // cgroup name
    // if not use cgroup function, this field will be nullptr.
    // if use cgroup function. use the cgroup name in the cgroupNamelist to apply all events in the Event list
    char** cgroupNameList;
    unsigned numCgroup;

    // enable user access counting for current process
    unsigned enableUserAccess : 1;
};

enum PmuTraceType {
    TRACE_SYS_CALL,
};

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

struct CpuTopology {
    int coreId;
    int numaId;
    int socketId;
};

enum SPE_EVENTS {
    SPE_EV_EXCEPT       = 1 << 0,
    SPE_EV_RETIRED      = 1 << 1,
    SPE_EV_L1D_ACCESS   = 1 << 2,
    SPE_EV_L1D_REFILL   = 1 << 3,
    SPE_EV_TLB_ACCESS   = 1 << 4,
    SPE_EV_TLB_WALK     = 1 << 5,
    SPE_EV_NOT_TAKEN    = 1 << 6,
    SPE_EV_MISPRED      = 1 << 7,
    SPE_EV_LLC_ACCESS   = 1 << 8,
    SPE_EV_LLC_MISS     = 1 << 9,
    SPE_EV_REMOTE_ACCESS= 1 << 10,
    SPE_EV_ALIGNMENT    = 1 << 11,
    SPE_EV_PARTIAL_PRED = 1 << 17,
    SPE_EV_EMPTY_PRED   = 1 << 18,
};

struct BranchSampleRecord {
    unsigned long fromAddr;
    unsigned long toAddr;
    unsigned long cycles;
    uint8_t misPred;
    uint8_t predicted;
};

struct PmuDataExt {
    union {
        struct {
            unsigned long pa;    // physical address
            unsigned long va;    // virtual address
            unsigned long event; // event id, which is a bit map of mixed events, event bit is defined in SPE_EVENTS.
            unsigned short lat; // latency, Number of cycles between the time when an operation is dispatched and the time when the operation is executed.
        };

        struct {
            unsigned long nr;                  // number of branchRecords
            struct BranchSampleRecord *branchRecords; // branch pointer array
        };
    };
};

struct SampleRawData {
    char *data;
};

struct SampleRawField {
    char* fieldName; //the field name of this field.
    char* fieldStr; //the field line.
    unsigned offset; //the data offset.
    unsigned size; //the field size.
    unsigned isSigned; //is signed or is unsigned
};

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
    uint64_t period;                // sample period
    uint64_t count;                 // event count. Only available for Counting.
    double countPercent;            // event count Percent. when count = 0, countPercent = -1; Only available for Counting.
    struct PmuDataExt *ext;         // extension. Only available for Spe.
    struct SampleRawData *rawData;  // trace pointer collect data.
    const char* cgroupName;         // trace data from which cgroup 
};

struct PmuTraceData {
    const char *funcs;              // system call function
    int64_t startTs;                // start time stamp. unit: ns
    double elapsedTime;             // elapsed time
    pid_t pid;                      // process id
    int tid;                        // thread id
    int cpu;                   // cpu id
    const char *comm;               // process command
};

struct PmuCpuFreqDetail {
    int cpuId;        // cpu core id
    uint64_t minFreq; // minimum frequency of core
    uint64_t maxFreq; // maximum frequency of core
    uint64_t avgFreq; // average frequency of core
};

/**
 * @brief
 * Initialize the collection target.
 * On success, a task id is returned which is the unique identity for the task.
 * On error, -1 is returned.
 * Refer to comments of PmuAttr for details about settings.
 * @param collectType task type
 * @param attr settings of the current task
 * @return task id
 */
int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);

/**
 * @brief
 * Query all available event from system.
 * @param eventType type of event chosen by user
 * @param numEvt length of event list
 * @return event list
 */
const char** PmuEventList(enum PmuEventType eventType, unsigned *numEvt);

/**
 * @brief
 * Enable counting or sampling of task <pd>.
 * On success, 0 is returned.
 * On error, -1 is returned.
 * @param pd task id
 * @return error code
 */
int PmuEnable(int pd);

/**
 * @brief
 * Disable counting or sampling of task <pd>.
 * On success, 0 is returned.
 * On error, -1 is returned.
 * @param pd task id
 * @return error code
 */
int PmuDisable(int pd);

/**
 * @brief
 * Collect <milliseconds> milliseconds. If <milliseconds> is equal to - 1 and the PID list is not empty, the collection
 * is performed until all processes are complete.
 * @param milliseconds
 * @param interval internal collect period. Unit: millisecond. Must be larger than or equal to 100.
 * @return int
 */
int PmuCollect(int pd, int milliseconds, unsigned interval);

/**
 * @brief
 * Similar to <PmuCollect>, and <PmuCollectV> accepts multiple pds.
 * @param milliseconds
 * @return int
 */
int PmuCollectV(int *pd, unsigned len, int milliseconds);

/**
 * @brief stop a sampling task in asynchronous mode
 * @param pd pmu descriptor.
 */
void PmuStop(int pd);

/**
 * @brief
 * Collect data.
 * Pmu data are collected starting from the last PmuEnable or PmuRead.
 * That is to say, for COUNTING, counts of all pmu event are reset to zero in PmuRead.
 * For SAMPLING and SPE_SAMPLING, samples collected are started from the last PmuEnable or PmuRead.
 * On success, length of data array is returned.
 * If <pmuData> is NULL and the error code is 0, no data is available in the current collection time.
 * If <pmuData> is NULL and the error code is not 0, an error occurs in the collection process and data cannot be read.
 * @param pd task id
 * @param pmuData pmu data which is a pointer to an array
 * @return length of pmu data
 */
int PmuRead(int pd, struct PmuData** pmuData);

/**
* @brief 
* When symbol mode is NO_SYMBOL_RESOLVE, you can use this resolve PmuData Symbol after PmuRead function
* @param pmuData the data from PmuRead
* @return 0 indicates resolve success, otherwise return error code
*/
int ResolvePmuDataSymbol(struct PmuData* pmuData);

/**
 * @brief
 * Append data list <fromData> to another data list <*toData>.
 * The pointer of data list <*toData> will be refreshed after this function is called.
 * On success, length of <*toData> is returned.
 * On error, -1 is returned.
 * @param fromData data list which will be copied to <*toData>
 * @param toData pointer to target data list. If data list <*toData> is NULL, a new list will be created.
 * @return length of <toData>
 */
int PmuAppendData(struct PmuData *fromData, struct PmuData **toData);

/**
 * @brief
 * Dump pmu data to a specific file.
 * If file exists, then data will be appended to file.
 * If file does not exist, then file will be created.
 * Dump format: comm pid tid cpu period evt count addr symbolName offset module fileName lineNum
 * @param pmuData data list.
 * @param len data length.
 * @param filepath path of the output file.
 * @param dumpDwf if 0, source file and line number of symbols will not be dumped, otherwise, they will be dumped to file.
*/
int PmuDumpData(struct PmuData *pmuData, unsigned len, char *filepath, int dumpDwf);

/**
 * @brief
 * Close task with id <pd>.
 * After PmuClose is called, all pmu data related to the task become invalid.
 * @param pd task id
 */
void PmuClose(int pd);

/**
 * @brief Free PmuData pointer.
 * @param pmuData
 */
void PmuDataFree(struct PmuData* pmuData);

/**
 * @brief Get the pointer trace event raw field.
 * @param rawData the raw data.
 * @param fieldName the filed name of one field.
 * @param value  the pointer of value.
 * @param vSize  the memory size of value.
 * @return 0 success other failed.
 */
int PmuGetField(struct SampleRawData *rawData, const char *fieldName, void *value, uint32_t vSize);


/**
 * @brief Get the SampleRawField explation.
 * @param rawData
 * @param fieldName
 * @return
 */
struct SampleRawField *PmuGetFieldExp(struct SampleRawData *rawData, const char *fieldName);

enum PmuDeviceMetric {
    // Perchannel metric.
    // Collect ddr read bandwidth for each channel.
    // Unit: Bytes
    PMU_DDR_READ_BW,
    // Perchannel metric.
    // Collect ddr write bandwidth for each channel.
    // Unit: Bytes
    PMU_DDR_WRITE_BW,
    // Percore metric.
    // Collect L3 access bytes for each cpu core.
    // Unit: Bytes
    PMU_L3_TRAFFIC,
    // Percore metric.
    // Collect L3 miss count for each cpu core.
    // Unit: count
    PMU_L3_MISS,
    // Percore metric.
    // Collect L3 total reference count, including miss and hit count.
    // Unit: count
    PMU_L3_REF,
    // Percluster metric.
    // Collect L3 total latency for each cluster node.
    // Unit: ns 
    PMU_L3_LAT,
    // Collect pcie rx bandwidth.
    // Perpcie metric.
    // Collect pcie rx bandwidth for pcie device.
    // Unit: Bytes/us
    PMU_PCIE_RX_MRD_BW,
    PMU_PCIE_RX_MWR_BW,
    // Perpcie metric.
    // Collect pcie tx bandwidth for pcie device.
    // Unit: Bytes/us
    PMU_PCIE_TX_MRD_BW,
    PMU_PCIE_TX_MWR_BW,
    // Perpcie metric.
    // Collect pcie rx latency for pcie device.
    // Unit: ns
    PMU_PCIE_RX_MRD_LAT,
    PMU_PCIE_RX_MWR_LAT,
    // Perpcie metric.
    // Collect pcie tx latency for pcie device.
    // Unit: ns
    PMU_PCIE_TX_MRD_LAT,
    // Perpcie metric.
    // Collect smmu address transaction.
    // Unit: count
    PMU_SMMU_TRAN,
    // Pernuma metric.
    // Collect rate of cross-numa operations received by HHA.
    PMU_HHA_CROSS_NUMA,
    // Pernuma metric.
    // Collect rate of cross-socket operations received by HHA.
    PMU_HHA_CROSS_SOCKET
};

struct PmuDeviceAttr {
    enum PmuDeviceMetric metric;

    // Used for PMU_PCIE_XXX_BW and PMU_SMMU_XXX to collect a specifi pcie device.
    // The string of bdf is something like '7a:01.0'.
    char *bdf;
    // Used for PMU_PCIE_XXX_LAT to collect latency data.
    // Only one port supported.
    char *port;
};

enum PmuBdfType {
    PMU_BDF_TYPE_PCIE,  // pcie collect metric.
    PMU_BDF_TYPE_SMMU   // smmu collect metric.
};

enum PmuMetricMode {
    PMU_METRIC_INVALID,
    PMU_METRIC_CORE,
    PMU_METRIC_NUMA,
    PMU_METRIC_CLUSTER,
    PMU_METRIC_BDF,
    PMU_METRIC_CHANNEL
};

/**
 * @brief
 * Query all available bdf list from system.
 * @param bdfType type of bdf chosen by user
 * @param numBdf length of bdf list
 * @return bdf list
 */
const char** PmuDeviceBdfList(enum PmuBdfType bdfType, unsigned *numBdf);

/**
 * @brief
 * A high level interface for initializing pmu events for devices,
 * such as L3 cache, DDRC, PCIe, and SMMU, to collect metrics like bandwidth, latency, and others.
 * This interface is an alternative option for initializing events besides PmuOpen.
 * @param attr Array of metrics to collect
 * @param len Length of array
 * @return Task Id, similar with returned value of PmuOpen
 */
int PmuDeviceOpen(struct PmuDeviceAttr *attr, unsigned len);

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
        // for percluster metric
        unsigned clusterId;
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

/**
 * @brief
 * Query device metrics from pmuData and metric array.
 * @param pmuData pmuData read from PmuRead
 * @param len length of pmuData
 * @param attr metric array to query
 * @param attrLen length of metric array
 * @param data output metric data array, the length of array is the returned value
 * @return On success, length of metric data array is returned.
 * On fail, -1 is returned and use Perror to get error message.
 */
int PmuGetDevMetric(struct PmuData *pmuData, unsigned len,
                    struct PmuDeviceAttr *attr, unsigned attrLen,
                    struct PmuDeviceData **data);

/**
 * @brief Free PmuDeviceData pointer.
 * @param data
 */
void DevDataFree(struct PmuDeviceData *data);

/**
 * @brief Get core list of a cluster.
 * @param clusterId cluster id
 * @param coreList core id list, malloced by this method.
 * @return length of core id list
 */
int PmuGetClusterCore(unsigned clusterId, unsigned **coreList);

/**
 * @brief Get core list of a numa node.
 * @param clusterId numa id
 * @param coreList core id list, malloced by this method.
 * @return length of core id list
 */
int PmuGetNumaCore(unsigned nodeId, unsigned **coreList);

/**
 * @brief
 * Initialize the trace collection target.
 * On success, a trace collect task id is returned which is the unique identity for the task.
 * On error, -1 is returned.
 * Refer to comments of PmuTraceAttr for details about settings.
 * @param PmuTraceType task type
 * @param PmuTraceAttr settings of the current trace collect task
 * @return trace collect task id
 */
int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);

/**
 * @brief
 * Enable trace collection of task <pd>.
 * On success, 0 is returned.
 * On error, -1 is returned.
 * @param pd trace collect task id
 * @return error code
 */
int PmuTraceEnable(int pd);

/**
 * @brief
 * Disable trace collection of task <pd>.
 * On success, 0 is returned.
 * On error, -1 is returned.
 * @param pd trace collect task id
 * @return error code
 */
int PmuTraceDisable(int pd);

/**
 * @brief
 * Collect data.
 * Pmu trace data are collected starting from the last PmuTraceEnable or PmuTraceRead.
 * On success, length of data array is returned.
 * If <PmuTraceData> is NULL and the error code is 0, no data is available in the current collection time.
 * If <PmuTraceData> is NULL and the error code is not 0, an error occurs in the collection process and data cannot be read.
 * @param pd trace collect task id
 * @param PmuTraceData pmu trace data which is a pointer to an array
 * @return length of pmu trace data
 */
int PmuTraceRead(int pd, struct PmuTraceData** pmuData);

/**
 * @brief
 * Close task with id <pd>.
 * After PmuTraceClose is called, all pmu trace data related to the task become invalid.
 * @param pd trace collect task id
 */
void PmuTraceClose(int pd);

/**
 * @brief Free PmuTraceData pointer.
 * @param pmuTraceData
 */
void PmuTraceDataFree(struct PmuTraceData* pmuTraceData);

/**
 * @brief
 * Query all available system call function from system.
 * @param numFunc length of system call function list
 * @return system call function list
 */
const char** PmuSysCallFuncList(unsigned *numFunc);

/**
 * @brief
 * Get cpu frequency of cpu core.
 * @param core Index of core
 * @return On success, core frequency(Hz) is returned.
 * On error, -1 is returned and call Perrorno to get error.
 */
int64_t PmuGetCpuFreq(unsigned core);

/**
 * @brief get the maximum frequency,minimum frequency,and average frequency of each core
 * @param cpuNum
 * @return PmuCpuFreqDetail array of pointers
 */
struct PmuCpuFreqDetail* PmuReadCpuFreqDetail(unsigned* cpuNum);

/**
 * @brief open cpu core freq sampling
 * @param time period unit ms
 * @return -1 or 0
 */
int PmuOpenCpuFreqSampling(unsigned period);

/**
 * @brief close cpu freq sampling
 */
void PmuCloseCpuFreqSampling();

typedef void* PmuFile;

/**
 * @brief Begin to write PmuData list to perf.data file.
 *        It is a simplified perf.data only include basic fields for perf sample,
 *        including id, tid, pid, addr and branch stack.
 *        It also includes sample like mmap, mmap2, comm, fork.
 * @param path path of perf.data
 * @param pattr PmuAttr of collection task
 * @return a handle of file to write. If error, return NULL and check Perrorno.
 */
PmuFile PmuBeginWrite(const char *path, const struct PmuAttr *pattr);

/**
 * @brief Write PmuData list to file.
 * @param file file handle
 * @param data PmuData list
 * @param len length of data
 * @return On success, return SUCCESS. on error, return error code.
 */
int PmuWriteData(PmuFile file, struct PmuData *data, int len);

/**
 * @brief End to write file.
 * @param file file handle
 */
void PmuEndWrite(PmuFile file);

#pragma GCC visibility pop
#ifdef __cplusplus
}
#endif
#endif
