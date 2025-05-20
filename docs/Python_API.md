### kperf.open

kperf.open(collector_type: kperf.PmuTaskType, pmu_attr: kperf.PmuAttr)
初始化Pmu事件

* class PmuTaskType

  * COUTING          PMU计数模式
  * SAMPLING         PMU采样模式
  * SPE_SAMPLING     SPE采样模式
* class PmuAttr

  * evtList
    采集的事件列表，事件列表可以通过perf list查询
  * pidList
    采集的进程id列表
  * cpuList
    指定的使用cpu核采集列表，默认采集所有逻辑核
  * evtAttr
    事件分组列表，和evtList搭配使用，同组事件需要使用相同数字表示，不同组事件使用不同的数字代表，如果数字为-1，则不参与事件分组
  * sampleRate
    采样频率，可通过/proc/sys/kernel/perf_event_max_sample_rate调整最大的采样频率
  * useFreq
    是否启用采样频率，如果设置，将可以使用采样频率
  * excludeUser
    排除对用户态数据的采集
  * excludeKernel
    排除对内核态数据的采集
  * symbolMode
    符号采集模式
    NO_SYMBOL_RESOLVE = 0 不支持符号采集
    RESOLVE_ELF = 1   仅支持ELF数据采集，解析function，不解析行号
    RESOLVE_ELF_DWARF = 2 既支持ELF数据采集，也支持行号解析
  * callStack
    是否采集调用栈，默认不采集，只取栈顶数据
  * dataFilter
    * SPE_FILTER_NONE = 0
    * TS_ENABLE = 1 << 0  使能通用计时器
    * PA_ENABLE = 1 << 1  采集物理地址
    * PCT_ENABLE = 1 << 2 采集物理地址的事件戳替代虚拟地址
    * JITTER = 1 << 16    采样时使用共振防止抖动
    * BRANCH_FILTER = 1 << 32  只采集分支数据
    * LOAD_FILTER = 1 << 33    只采集已加载数据
    * STORE_FILTER = 1 << 34   只采集存储数据
    * SPE_DATA_ALL = TS_ENABLE | PA_ENABLE | PCT_ENABLE | JITTER | BRANCH_FILTER | LOAD_FILTER | STORE_FILTER
  * evFilter
    * SPE_EVENT_NONE = 0
    * SPE_EVENT_RETIRED = 0x2        # instruction retired
    * SPE_EVENT_L1DMISS = 0x8        # L1D refill
    * SPE_EVENT_TLB_WALK = 0x20      # TLB refill
    * SPE_EVENT_MISPREDICTED = 0x80  # mispredict
  * minLatency 仅收集该latency或者更高的样本数据
  * includeNewFork
    是否支持子线程拆分，仅在COUTING模式中支持
  * branchSampleFilter
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
      branch_sample_type对应的比特值，使用方式
      branchSampleMode = kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_ANY | kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_USER
      pmu_attr = kperf.PmuAttr(sampleRate=1000, useFreq=True, pidList=pidList, evtList=evtList, branchSampleFilter=branchSampleMode)
      仅支持SAMPLING模式, 其中KPERF_SAMPLE_BRANCH_USER， KPERF_SAMPLE_BRANCH_KERNEL， KPERF_SAMPLE_BRANCH_HV使用时必须搭配KPERF_SAMPLE_BRANCH_ANY或者KPERF_SAMPLE_BRANCH_ANY之后的值一起使用
* 返回值是int值
  fd > 0 成功初始化
  fd == -1 初始化失败，可通过 kperf.error()查看错误信息
  以下是kperf.open的一个示例:

```python
# python代码示例
import time
import kperf
evtList = ["cycles", "branch-misses"]
pmu_attr = kperf.PmuAttr(evtList=evtList)
pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
if pd == -1:
    print(kperf.error())
    exit(1)
```

### kperf.enable

kperf.enable(pd: int) 该接口用于开启某个pd的采样能力
pd为kperf.open返回值
返回值为int
如果返回值>0，则使能异常
如果返回值=0, 则使能正常
### kperf.disable

kperf.disable(pd: int) 该接口用于关闭某个pd的采样
pd为kperf.open返回值
返回值为int
如果返回值>0，则关闭采集异常
如果返回值=0, 则关闭采集正常
一般kperf.enable和kperf.disable搭配使用，以下为示例,相当于采集1s的数据:

```
kperf.enable(pd)
time.sleep(1)
kperf.disable(pd)
```
### kperf.read

kperf.read(pd: int) 读取pd采样的数据
pd为kperf.open返回值
返回值为PmuData
* class PmuData
  * len 数据长度
  * iter 返回Iterator[ImplPmuData]
  * free 将当前PmuData数据清理
* class ImplPmuData
  * stack: Stack=None
    * symbol
      * addr 地址
      * module 模块名称
      * symbolName 符号名
      * mangleName mangle后的符号名
      * lineNum 行号
      * offset 地址偏移
      * codeMapEndAddr 结束地址
      * codeMapAddr 初始地址
      * count 个数
    * next 下一个stack
    * prev  前一个stack
    * count 计数
  * evt: 事件
  * ts: Pmu采集时间戳
  * pid: 进程ID
  * tid: 线程ID
  * cpu: cpu核
  * cpuTopo:
    * coreId 系统核ID
    * numaId numaID
    * socketId  socketId
  * comm: 执行指令名称
  * period: 采样间隔
  * count: 计数
  * countPercent: 计数比值，使能时间/运行时间
  * ext: SPE独特数据
    * pa  物理地址
    * va  虚拟地址
    * event  事件ID
    * branchRecords brbe数据
        * iter brbe数据迭代器
        * len  brbe数据长度
  * rawData: tracepointer数据指针，搭配kperf.get_field和Kperf.get_field_exp使用

以下为kperf.read示例

```python
# python代码示例
pmu_data = kperf.read(pd)
for data in pmu_data.iter:
    print(f"cpu {data.cpu} count {data.count} evt {data.evt}")
```
### kperf.close

kperf.close(pd: int) 该接口用于清理该pd所有的对应数据，并移除该pd
### kperf.dump

dump(pmuData: PmuData, filepath: str, dump_dwf: int)
* pmuData
  由kperf.read返回的PmuData数据
* filePath
  数据落盘写入路径
* dump_dwf
  是否写入dwarf数据

### kperf.get_field

get_field(pmu_data: ImplPmuData, field_name: str, value: c_void_p)

获取tracepointer format某个字段数据，format数据可通过/sys/kernel/tracing/events/或者/sys/kernel/debug/tracing/events/进行查找
* pmu_data: ImplePmuData
  详细见kperf.read返回数据说明
* field_name
  字段名称
* value
  字段属性指针

以下为kperf.get_field示例

```python
import kperf
import time
from ctypes import *

evtList = ["sched:sched_switch"]
pmu_attr = kperf.PmuAttr(
    evtList=evtList
)
pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
kperf.enable(pd)
time.sleep(1) # 采集时间为1s
kperf.disable(pd)
pmu_data = kperf.read(pd)
for data in pmu_data.iter:
    next_comm = create_string_buffer(128) #该长度可适当减少，但最好设置比最终获取的长度大，否则最终将无法获取对应结果。
    kperf.get_field(data, "next_comm", next_comm)
    next_comm = next_comm.value.decode("utf-8")

    prev_pid = c_uint(0)
    kperf.get_field(data, "prev_pid", pointer(prev_pid))

    print(f"next_comm={next_comm};prev_pid={prev_pid.value}")
```
### kperf.get_field_exp

get_field_exp(pmu_data: ImplPmuData, field_name: str) 
获取某个字段属性说明
* pmu_data: ImplePmuData
  详细见kperf.read返回数据说明
* field_name
  字段名称
  返回数据
* class SampleRawField
  * fieldName 属性名称
  * fieldStr 符号单位
  * offset 偏移字节数
  * size 数据大小
  * isSigned 有无符号

```python
field = kperf.get_field_exp(data, "name")
print("field_str={} field_name={} size={} offset={} isSigned={}"
        .format(field.field_name, field.field_str, field.size, field.offset, field.is_signed))
```
### kperf.event_list

event_list(event_type: PmuEventType)
查找所有的事件列表
* class PmuEventType:
  * CORE_EVENT = 0  获取core事件列表
  * UNCORE_EVENT = 1 获取uncore事件列表
  * TRACE_EVENT = 2 获取tracepointer事件列表
  * ALL_EVENT = 3 获取所有的事件列表
* 返回数据 Iterator[str]，可通过for循环遍历该单元

以下为kperf.event_list示例

```python
# python代码示例
for evt in kperf.event_list(kperf.PmuEventType.CORE_EVENT):
    print(f"event name: {evt}")
```
### kperf.trace_open

kperf.trace_open(trace_type: kperf.PmuTraceType, pmu_trace_attr: kperf.PmuTraceAttr) # 初始化采集系统调用函数能力
* class PmuTraceType:
  * TRACE_SYS_CALL = 0  采集系统调用函数事件
* class PmuTraceAttr:
  * funcs：采集的系统调用函数列表，可以查看/usr/include/asm-generic/unistd.h文件，默认为空，表示采集所有系统调用函数
  * pidList：采集的进程列表，默认为空，表示采集所有进程
  * cpuList：采集的cpu列表，默认为空，表示采集所有cpu
* 返回值是int类型的, pd > 0 表示打开成功， pd == -1 初始化失败，可通过kperf.error()查看错误信息，调用样例类似kperf.open()

以下是kperf.trace_open的一个示例:

```python
# python代码示例
import time
import kperf
funcs = ["read", "write"]
pmu_trace_attr = kperf.PmuTraceAttr(funcs=funcs)
pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_attr)
if pd == -1:
    print(kperf.error())
    exit(1)
```
### kperf.trace_enable、kperf.trace_disable

调用逻辑类似kperf.enable、kperf.disable，用于配置采集启动和结束的时刻，两个调用之间的时间即是采集的时间段
### kperf.trace_read

kperf.trace_read(pd: int)
pd为kperf.trace_open返回值
返回值是PmuTraceData
* class PmuTraceData:
  * len: 数据长度
  * iter: 返回iterator[lmplPmuTraceData]
  * free: 释放当前PmuTraceData数据
* class lmplPmuTraceData:
  * funcs: 系统调用函数名
  * startTs: 耗时起始时刻
  * elapsedTime: 耗时时间
  * pid: 进程id
  * tid: 线程id
  * cpu: cpu号
  * comm: 执行指令名称
    以下为kperf.trace_read示例

```python
pmu_trace_data = kperf.trace_read(pd)
for pmu_trace in pmu_trace_data.iter:
    print("funcs: %s, elapsedTime: %d, pid: %d, tid: %d, cpu: %d, comm: %s" % (pmu_trace.funcs, pmu_trace.elapsedTime, pmu_trace.pid, pmu_trace.tid, pmu_trace.cpu, pmu_trace.comm))
```
### kperf.trace_close

kperf.trace_close(pd: int): 该接口用于清理该pd所有对应的数据，并移除该pd
### kperf.sys_call_func_list

kperf.sys_call_func_list(): 查找所有的系统调用函数列表
* 返回数据 iterator[str], 可通过for循环遍历该单元

以下为kperf.sys_call_func_list示例

```python
# python代码示例
for func_name in kperf.sys_call_func_list():
    print(f"syscall function name: {func_name}")
```


### kperf.device_open

kperf.device_open(dev_attr: List[PmuDeviceAttr]) 初始化采集uncore事件指标的能力
* class PmuDeviceAttr:
  * metic: 指定需要采集的指标
    * PMU_DDR_READ_BW 采集每个numa的ddrc的读带宽，单位：Bytes
    * PMU_DDR_WRITE_BW 采集每个numa的ddrc的写带宽，单位：Bytes
    * PMU_L3_TRAFFIC 采集每个core的L3的访问字节数，单位：Bytes
    * PMU_L3_MISS 采集每个core的L3的miss数量，单位：count
    * PMU_L3_REF 采集每个core的L3的总访问数量，单位：count
    * PMU_L3_LAT 采集每个numa的L3的总时延，单位：cycles
    * PMU_PCIE_RX_MRD_BW 采集pcie设备的rx方向上的读带宽，单位：Bytes/ns
    * PMU_PCIE_RX_MWR_BW 采集pcie设备的rx方向上的写带宽，单位：Bytes/ns
    * PMU_PCIE_TX_MRD_BW 采集pcie设备的tx方向上的读带宽，单位：Bytes/ns
    * PMU_PCIE_TX_MWR_BW 采集pcie设备的tx方向上的读带宽，单位：Bytes/ns
    * PMU_SMMU_TRAN 采集指定smmu设备的地址转换次数，单位：count
  * bdf: 指定需要采集设备的bdf号，只对pcie和smmu指标有效
* 返回值是int类型，pd > 0表示初始化成功，pd == -1初始化失败，可通过kperf.error()查看错误信息，以下是一个kperf.device_open的示例

```python
# python代码示例
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_TRAFFIC)
    ]
    pd = kperf.device_open(dev_attr)
    if pd == -1:
        print(kperf.error())
        exit(1)
```

### kperf.get_device_metric

kperf.get_device_metric(pmu_data: PmuData, device_attr: List[PmuDeviceAttr]) 对原始read接口的数据，按照device_attr中给定的指标进行数据聚合接口，返回值是PmuDeviceData

* class PmuData: read接口返回的原始数据
* clase PmuDeviceAttr: 指定需要聚合的指标参数
* class PmuDeviceData:
  * len: 数据长度
  * iter: 返回iterator[ImplPmuDeviceData]
  * free: 释放当前PmuDeviceData
* class DdrDataStructure:
  * channelId: ddr数据的channel编号
  * ddrNumaId: ddr数据的numa编号
  * socketId:  ddr数据的socket编号
* class ImplPmuDeviceData:
  * metric: 采集的指标
  * count：指标的计数值
  * mode: 指标的采集类型，按core、按numa、按channel还是按bdf号
  * union：
    * coreId: 数据的core编号
    * numaId: 数据的numa编号
    * bdf: 数据的bdf编号
    * DdrDataStructure: ddr相关的统计数据


### kperf.device_bdf_list

kperf.device_bdf_list(bdf_type: PmuBdfType): 查找当前系统pcie指标中有效的bdf列表和smmu指标中的有效bdf列表

* calss PmuBdfType:
  PMU_BDF_TYPE_PCIE: pice指标类型
  PMU_BDF_TYPE_SMMU: smmu指标类型
* 返回数据iterator[str],可通过for循环遍历该单元
以下是kperf.device_bdf_list示例

```python
# python代码示例
    bdf_type = kperf.PmuBdfType.PMU_BDF_TYPE_SMMU
    bdf_list = kperf.device_bdf_list(bdf_type)
    print(bdf_list)
```

### kperf.get_cpu_freq

kperf.get_cpu_freq(core: int): 查询当前系统指定core的实时CPU频率
* core: CPU的core编号
* 返回值：指定core的实时频率，单位: Hz
```python
# python代码示例
    core = 6
    cpu6_freq = kperf.get_cpu_freq(core)
```

### kperf.get_cluster_core

kperf.get_cluster_core(clusterId: int): 查询指定clusterId下对应的core列表
* clusterId: CPU的clusterId编号
* 返回值：当前clusterId下对应的core列表,出现错误则列表为空
```python
# python代码示例
    clusterId = 1
    cluster_cores = kperf.get_cluster_core(clusterId)
```

### kperf.get_numa_core

kperf.get_numa_core(numaId: int): 查询指定numaId下对应的core列表
* numaId: CPU的numaId编号
* 返回值：当前numaId下对应的core列表,出现错误则列表为空
```python
# python代码示例
    numaId = 1
    numa_cores = kperf.get_numa_core(numaId)
```