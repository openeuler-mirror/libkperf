### int PmuOpen(enum PmuTaskType collectType, struct PmuAttr *attr);
初始化Pmu事件的采集任务，获取任务的标识符pd

* enum PmuTaskType
  * COUNTING         PMU计数模式
  * SAMPLING         PMU采样模式
  * SPE_SAMPLING     SPE采样模式

* struct PmuAttr
  * char** evtList
    采集的事件列表，事件列表可以通过perf list查询
  * unsigned numEvt
    采集事件的个数
  * int* pidList
    采集的进程id列表
  * unsigned numPid
    采集进程id的个数
  * int* cpuList
    指定的使用cpu核采集列表，默认采集所有逻辑核
  * unsigned numCpu
    采集cpu核的个数
  * struct EvtAttr *evtAttr
    事件分组列表，和evtList搭配使用，同组事件需要使用相同数字表示，不同组事件使用不同的数字代表，如果数字为-1，则不参与事件分组
  * union
    unsigned period  采样周期，仅支持SAMPLING和SPE_SAMPLING模式
    unsigned freq    采样频率，仅支持SAMPLING模式
  * unsigned useFreq
    是否启用采样频率，如果设置，将可以使用采样频率
  * unsigned excludeUser
    排除对用户态数据的采集
  * unsigned excludeKernel
    排除对内核态数据的采集
  * enum SymbolMode symbolMode
    符号采集模式
    * NO_SYMBOL_RESOLVE = 0 不支持符号采集
    * RESOLVE_ELF = 1   仅支持ELF数据采集，解析function，不解析行号
    * RESOLVE_ELF_DWARF = 2 既支持ELF数据采集，也支持行号解析
  * unsigned callStack
    是否采集调用栈，默认不采集，只取栈顶数据
  * unsigned blockedSample
    是否执行blocked Sample采样
  * enum SpeFilter dataFilter
    * SPE_FILTER_NONE = 0
    * TS_ENABLE = 1 << 0  使能通用计时器
    * PA_ENABLE = 1 << 1  采集物理地址
    * PCT_ENABLE = 1 << 2 采集物理地址的事件戳替代虚拟地址
    * JITTER = 1 << 16    采样时使用共振防止抖动
    * BRANCH_FILTER = 1 << 32  只采集分支数据
    * LOAD_FILTER = 1 << 33    只采集已加载数据
    * STORE_FILTER = 1 << 34   只采集存储数据
    * SPE_DATA_ALL = TS_ENABLE | PA_ENABLE | PCT_ENABLE | JITTER | BRANCH_FILTER | LOAD_FILTER | STORE_FILTER
  * enum SpeEventFilter evFilter
    * SPE_EVENT_NONE = 0
    * SPE_EVENT_RETIRED = 0x2        # instruction retired
    * SPE_EVENT_L1DMISS = 0x8        # L1D refill
    * SPE_EVENT_TLB_WALK = 0x20      # TLB refill
    * SPE_EVENT_MISPREDICTED = 0x80  # mispredict
  * unsigned long minLatency 
    仅收集该latency或者更高的样本数据
  * unsigned includeNewFork
    是否支持子线程拆分，仅在COUNTING模式中支持
  * unsigned long branchSampleFilter
    * KPERF_NO_BRANCH_SAMPLE         = 0     不采集branch sample stack数据
    * KPERF_SAMPLE_BRANCH_USER        = 1 << 0  分支目标位于用户空间
    * KPERF_SAMPLE_BRANCH_KERNEL      = 1 << 1  分支目标位于内核空间
    * KPERF_SAMPLE_BRANCH_HV          = 1 << 2  分支目标位于虚拟机管理程序中
    * KPERF_SAMPLE_BRANCH_ANY         = 1 << 3  任意分支目标
    * KPERF_SAMPLE_BRANCH_ANY_CALL    = 1 << 4  任意调用分支（包括直接调用，间接调用和远程调用）
    * KPERF_SAMPLE_BRANCH_ANY_RETURN  = 1 << 5  任意返回分支 
    * KPERF_SAMPLE_BRANCH_IND_CALL    = 1 << 6  间接调用分支
    * KPERF_SAMPLE_BRANCH_ABORT_TX    = 1 << 7  事物性内存中止
    * KPERF_SAMPLE_BRANCH_IN_TX       = 1 << 8  事物内存分支
    * KPERF_SAMPLE_BRANCH_NO_TX       = 1 << 9  分支不在事物性内存事物中
    * KPERF_SAMPLE_BRANCH_COND        = 1 << 10 条件分支  
    * KPERF_SAMPLE_BRANCH_CALL_STACK  = 1 << 11 调用栈分支
    * KPERF_SAMPLE_BRANCH_IND_JUMP    = 1 << 12 跳跃分支
    * KPERF_SAMPLE_BRANCH_CALL        = 1 << 13 调用分支
    * KPERF_SAMPLE_BRANCH_NO_FLAGES   = 1 << 14 无标志
    * KPERF_SAMPLE_BRANCH_NO_CYCLES   = 1 << 15 无循环
    * KPERF_SAMPLE_BRANCH_TYPE_SAVE   = 1 << 16 存储分支类型
      branchSampleFilter对应的比特值，使用方式：
      attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_USER | KPERF_SAMPLE_BRANCH_ANY;
      仅支持SAMPLING模式, 其中KPERF_SAMPLE_BRANCH_USER， KPERF_SAMPLE_BRANCH_KERNEL， KPERF_SAMPLE_BRANCH_HV使用时必须搭配KPERF_SAMPLE_BRANCH_ANY或者KPERF_SAMPLE_BRANCH_ANY之后的值一起使用
  * char** cgroupNameList;
    采集cgroup的名称列表
  * unsigned numCgroup;
    采集cgroup的个数

* 返回值 > 0   初始化成功
  返回值 = -1 初始化失败，可通过Perror()查看错误信息

### const char** PmuEventList(enum PmuEventType eventType, unsigned *numEvt);
查找所有的事件列表

* enum PmuEventType:
  * CORE_EVENT    获取core事件列表
  * UNCORE_EVENT  获取uncore事件列表
  * TRACE_EVEN    获取tracepointer事件列表
  * ALL_EVENT     获取所有的事件列表

* 返回事件列表，可通过for循环遍历该单元

### int PmuEnable(int pd);
开启某个pd的采集能力，pd为PmuOpen的返回值
* 返回值 = 0  使能正常
  返回值 = -1 使能异常

### int PmuDisable(int pd);
关闭某个pd的采集，pd为PmuOpen的返回值
* 返回值 = 0  关闭采集正常
  返回值 = -1 关闭采集异常

### int PmuRead(int pd, struct PmuData** pmuData);
读取pd采样的数据，pd为PmuOpen的返回值

* struct PmuData 
  * struct Stack stack
    * struct Symbol symbol
      * unsigned long addr 地址
      * char* module 模块名称
      * char* symbolName 符号名
      * char* mangleName mangle后的符号名
      * char* fileName 文件名称
      * unsigned int lineNum 行号
      * unsigned long offset 地址偏移
      * unsigned long codeMapEndAddr 结束地址
      * unsigned long codeMapAddr 初始地址
      * __u64 count 个数
    * Stack next 下一个stack
    * Stack prev  前一个stack
    * __u64 count 计数
  * const char *evt: 事件名称
  * int64_t ts: Pmu采集时间戳
  * pid_t pid: 进程ID
  * int tid: 线程ID
  * int cpu: cpuID
  * int groupId: 事件分组的Id
  * struct CpuTopology cpuTopo:
    * int coreId 系统核ID
    * int numaId numaID
    * int socketId  socketId
  * const char *comm: 执行指令名称
  * uint64_t period: 采样间隔
  * uint64_t count: 计数
  * double countPercent: 计数比值，使能时间/运行时间
  * struct PmuDataExt ext: SPE特有数据
    * union
      * struct
        * unsigned long pa  物理地址
        * unsigned long va  虚拟地址
        * unsigned long event  事件ID
        * unsigned short branchRecords brbe数据
      * struct
        * unsigned long nr  branchRecords的数量
        * struct BranchSampleRecord *branchRecords  branch指针数组
  * struct SampleRawData rawData: tracepointer数据指针
  * const char* cgroupName: cgroup名称

* 返回值 = 0 且 pmuData为空   采集时间内无可用数据
  返回值 != 0 且 pmuData为空  采集时发生错误，无法读取数据

### void PmuClose(int pd);
清理该pd所有的对应数据，并移除该pd

### void PmuDataFree(struct PmuData* pmuData);
清理PmuData

### int ResolvePmuDataSymbol(struct PmuData* pmuData);
当SymbolMode不设置或者设置为0时，可通过该接口解析read返回的PmuData数据中的符号

### int PmuDumpData(struct PmuData *pmuData, unsigned len, char *filepath, int dumpDwf);
* pmuData
  由PmuRead返回的PmuData数据
* len
  PmuData数据的长度
* filePath
  数据落盘写入路径
* dump_dwf
  是否写入dwarf数据

### int PmuTraceOpen(enum PmuTraceType traceType, struct PmuTraceAttr *traceAttr);
* PmuTraceType traceType
  * TRACE_SYS_CALL 采集系统调用函数事件
* PmuTraceAttr traceAttr
  * const char **funcs 采集的系统调用函数列表，可以查看/usr/include/asm-generic/unistd.h文件，默认为空，表示采集所有系统调用函数
  * unsigned numFuncs funcs的个数
  * int* pidList 采集的进程id列表
  * unsigned numPid 采集进程id的个数
  * int* cpuList 指定的使用cpu核采集列表，默认采集所有逻辑核
  * unsigned numCpu 采集cpu核的个数
  * CpuList []int采集的cpu列表，默认为空，表示采集所有cpu
* 返回值 > 0   采集初始化成功
  返回值 = -1 采集初始化失败

### int PmuTraceEnable(int pd);
该接口用于开启某个pd的采样能力

* 返回值 = 0 使能正常
  返回值 = -1 使能异常

### int PmuTraceDisable(int pd);
该接口用于关闭某个pd的采样能力

* 返回值 = 0 关闭采集正常
  返回值 = -1 关闭采集异常

### int PmuTraceRead(int pd, struct PmuTraceData** pmuData);
* taskId int kperf.PmuOpen返回的taskId
* type PmuTraceDataVo struct

  * GoTraceData []PmuTraceData
* type PmuTraceData struct

  * FuncName string 系统调用函数名
  * ElapsedTime float64 耗时时间
  * StartTs 开始时间戳
  * Pid int 进程id
  * Tid int 线程id
  * Cpu int cpu号
  * Comm string 执行指令名称

### void PmuTraceClose(int pd);
该接口用于清理该pd所有对应的数据，并移除该taskId

### const char** PmuSysCallFuncList(unsigned *numFunc);
查找所有的系统调用函数列表

### void PmuTraceDataFree(struct PmuTraceData* pmuTraceData);
清理pmuTraceData

### int PmuDeviceOpen(struct PmuDeviceAttr *attr, unsigned len);
初始化采集uncore事件指标的能力

* struct PmuDeviceAttr struct:
  * enum PmuDeviceMetric metric: 指定需要采集的指标
    * PMU_DDR_READ_BW 采集每个channel的ddrc的读带宽，单位：Bytes
    * PMU_DDR_WRITE_BW 采集每个channel的ddrc的写带宽，单位：Bytes
    * PMU_L3_TRAFFIC 采集每个core的L3的访问字节数，单位：Bytes
    * PMU_L3_MISS 采集每个core的L3的miss数量，单位：count
    * PMU_L3_REF 采集每个core的L3的总访问数量，单位：count
    * PMU_L3_LAT 采集每个cluster的L3的总时延，单位：cycles
    * PMU_PCIE_RX_MRD_BW 采集pcie设备的rx方向上的读带宽，单位：Bytes/ns
    * PMU_PCIE_RX_MWR_BW 采集pcie设备的rx方向上的写带宽，单位：Bytes/ns
    * PMU_PCIE_TX_MRD_BW 采集pcie设备的tx方向上的读带宽，单位：Bytes/ns
    * PMU_PCIE_TX_MWR_BW 采集pcie设备的tx方向上的读带宽，单位：Bytes/ns
    * PMU_SMMU_TRAN 采集指定smmu设备的地址转换次数，单位：count
  * char *bdf: 指定需要采集设备的bdf号，只对pcie和smmu指标有效
* 与PmuOpen类似，返回task Id
  返回值 > 0    初始化成功
  返回值 = -1  初始化失败，可通过Perror()查看错误信息

### int PmuGetDevMetric(struct PmuData *pmuData, unsigned len,struct PmuDeviceAttr *attr, unsigned attrLen, struct PmuDeviceData **data);
对原始read接口的数据，按照device_attr中给定的指标进行数据聚合，返回值是PmuDeviceData

* struct ImplPmuDeviceData:
  * enum PmuDeviceMetric metric: 采集的指标
  * double count：指标的计数值
  * enum PmuMetricMode mode: 指标的采集类型，包括
      PMU_METRIC_INVALID,
      PMU_METRIC_CORE,
      PMU_METRIC_NUMA,
      PMU_METRIC_CLUSTER,
      PMU_METRIC_BDF,
      PMU_METRIC_CHANNEL
  * union：
    * unsigned coreId: 数据的core编号
    * unsigned numaId: 数据的numa编号
    * unsigned clusterId: 簇ID
    * unsigned bdf: 数据的bdf编号
    * struct: ddr相关的统计数据
      * unsigned channelId: ddr数据的channel编号
      * unsigned ddrNumaId: ddr数据的numa编号
      * unsigned socketId:  ddr数据的socket编号


### int PmuGetNumaCore(unsigned nodeId, unsigned **coreList);
查询指定numaId下对应的core列表，返回列表长度

### int PmuGetClusterCore(unsigned clusterId, unsigned **coreList);
查询指定clusterId下对应的core列表，返回列表长度

### const char** PmuDeviceBdfList(enum PmuBdfType bdfType, unsigned *numBdf);
从系统中查找所有的bdf列表

* enum PmuBdfType
    * PMU_BDF_TYPE_PCIE  PCIE设备对应的bdf
    * PMU_BDF_TYPE_SMMU  SMMU设备对应的bdf

### void DevDataFree(struct PmuDeviceData *data);
清理PmuDeviceData的指针数据

### int64_t PmuGetCpuFreq(unsigned core);
查询当前系统指定core的实时CPU频率
* core: CPU的core编号
* 返回值：指定core的实时频率，单位: Hz

### int PmuOpenCpuFreqSampling(unsigned period);
开启cpu频率采集

### struct PmuCpuFreqDetail* PmuReadCpuFreqDetail(unsigned* cpuNum);
读取开启频率采集到读取时间内的cpu最大频率、最小频率以及平均频率

### void PmuCloseCpuFreqSampling();
关闭cpu频率采集