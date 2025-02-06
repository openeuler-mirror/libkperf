Details
============
### Counting
libkperf提供Counting模式，类似于perf stat功能。
例如，如下perf命令:
```
perf stat -e cycles,branch-misses
```
该命令是对系统采集cycles和branch-misses这两个事件的计数。

对于libkperf，可以这样来设置PmuAttr：

```c++
// c++代码示例
char *evtList[2];
evtList[0] = "cycles";
evtList[1] = "branch-misses";
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = 2;
int pd = PmuOpen(COUNTING, &attr);
```

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
通过调用```PmuOpen```初始化了采集任务，并获得了任务的标识符pd。
然后，可以利用pd来启动采集：
```c++
// c++代码示例
PmuEnable(pd);
sleep(any_duration);
PmuDisable(pd);
```

```python
# python代码示例
kperf.enable(pd)
time.sleep(1)
kperf.disable(pd)
```
不论是否停止了采集，都可以通过```PmuRead```来读取采集数据：
```c++
// c++代码示例
PmuData *data = NULL;
int len = PmuRead(pd, &data);
```
```PmuRead```会返回采集数据的长度。
                                              
```python
# python代码示例
pmu_data = kperf.read(pd)
for data in pmu_data.iter:
    print(f"cpu {data.cpu} count {data.count} evt {data.evt}")
```
```kperf.read```会返回采集数据链表,可以通过遍历的方式读取。

如果是对系统采集，那么PmuData的长度等于core的数量乘以事件的数量，PmuData的数据类似如下：
```
cpu 0 count 123     evt cycles
cpu 1 count 1242354 evt cycles
cpu 2 count 7897234 evt cycles
...
cpu 0 count 423423  evt branch-misses
cpu 1 count 124235  evt branch-misses
cpu 2 count 789723  evt branch-misses
...
```
如果是对进程采集，那么PmuData的长度等于进程内线程的数量乘以事件的数量，PmuData的数据类似如下：
```
pid 4156 tid 4156 count 123     evt cycles
pid 4156 tid 4157 count 534123  evt cycles
pid 4156 tid 4158 count 1241244 evt cycles
...
pid 4156 tid 4156 count 12414 evt branch-misses
pid 4156 tid 4157 count 5123  evt branch-misses
pid 4156 tid 4158 count 64574 evt branch-misses
...
```

### Sampling
libkperf提供Sampling模式，类似于perf record的如下命令：
```
perf record -e cycles,branch-misses
```
该命令是对系统采样cycles和branch-misses这两个事件。

设置PmuAttr的方式和Counting一样，在调用PmuOpen的时候，把任务类型设置为SAMPLING，并且设置采样频率：
```c++
// c++代码示例
attr.freq = 1000; // 采样频率是1000HZ
attr.useFreq = 1;
int pd = PmuOpen(SAMPLING, &attr);
```

```python
# python代码示例
import kperf
import time
evtList = ["branch-misses", "cycles"]
pmu_attr = kperf.PmuAttr(
        evtList=evtList,
        sampleRate=1000, # 采样频率是1000HZ
        symbolMode=kperf.SymbolMode.RESOLVE_ELF
    )
pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
```

启动采集和读取数据的方式和Counting一致。
如果是对系统采集，PmuData的数据类似如下（长度取决于数据量）：
```
cpu 0 pid 3145 tid 3145 period 12314352
cpu 0 pid 4145 tid 4145 period 12314367
...
cpu 1 pid 23423 tid 23423 period 1231241
...
...
```
如果是对进程采集，PmuData的数据类似如下：
```
cpu 32 pid 7878 tid 7878 period 123144
cpu 32 pid 7878 tid 7879 period 1523342
cpu 32 pid 7878 tid 7879 period 1234342
...
```
每一条记录还包含触发事件的程序地址和符号信息，关于如何获取符号信息，可以参考[获取符号信息](#获取符号信息)这一章节。

### SPE Sampling
libkperf提供SPE采样模式，类似于perf record的如下命令：
```
perf record -e arm_spe_0/load_filter=1/
```
该命令是对系统进行spe采样，关于linux spe采样的详细介绍，可以参考[这里](https://www.man7.org/linux/man-pages/man1/perf-arm-spe.1.html)。

对于libkperf，可以这样设置PmuAttr：
```c++
// c++代码示例
PmuAttr attr = {0};
attr.period = 8192; // 采样周期是8192
attr.dataFilter = LOAD_FILTER; // 设置filter属性为load_filter
```

```python
# python代码示例
import kperf
pmu_attr = kperf.PmuAttr(
    sampleRate = 1000,
    symbolMode = kperf.SymbolMode.RESOLVE_ELF,
    dataFilter = kperf.SpeFilter.SPE_DATA_ALL,
    evFilter = kperf.SpeEventFilter.SPE_EVENT_RETIRED,
    minLatency = 0x40
) 
# 需要root权限才能运行
pd = kperf.open(kperf.PmuTaskType.SPE_SAMPLING, pmu_attr)
```
对于spe采样，不需要设置evtList，而是通过设置dataFilter和evFilter来指定需要采集的事件。dataFilter和evFilter的含义仍然可以参考[perf spe的说明文档](https://www.man7.org/linux/man-pages/man1/perf-arm-spe.1.html)。

采样数据PmuData和Sampling模式差不多，差别是：
- SPE采样的调用栈只有一层，而Sampling可以有多层调用栈。
- SPE的PmuData提供了额外的数据struct PmuDataExt *ext.
PmuDataExt包含spe特有的数据：访存的物理地址、虚拟地址和事件bit。
```c++
struct PmuDataExt {
    unsigned long pa;               // physical address
    unsigned long va;               // virtual address
    unsigned long event;            // event id, which is a bit map of mixed events, event bit is defined in SPE_EVENTS.
};
```
其中，物理地址pa需要在启用PA_ENABLE的情况下才能采集。
event是一个bit map，是多个事件的集合，每一个事件占据一个bit，事件对应的bit参考枚举SPE_EVENTS：
```c++
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
```
### 获取符号信息
结构体PmuData里提供了采样数据的调用栈信息，包含调用栈的地址、符号名称等。
```c++
struct Symbol {
    unsigned long addr;
    char* module;
    char* symbolName;
    char* fileName;
    unsigned int lineNum;
    ...
};

struct Stack {
    struct Symbol* symbol;
    struct Stack* next;
    struct Stack* prev;
    ...
} __attribute__((aligned(64)));
```

Stack是链表结构，每一个元素都是一层调用函数。
```mermaid
graph LR
a(Symbol) --> b(Symbol)
b --> c(Symbol)
c --> d(......)
```

Symbol的字段信息受PmuAttr影响：
- PmuAttr.callStack会决定Stack是完整的调用栈，还是只有一层调用栈（即Stack链表只有一个元素）。
- PmuAttr.symbolMode如果等于NO_SYMBOL_RESOLVE，那么PmuData的stack是空指针。
- PmuAttr.symbolMode如果等于RESOLVE_ELF，那么Symbol的fileName和lineNum没有数据，都等于0，因为没有解析dwarf信息。
- PmuAttr.symbolMode如果等于RESOLVE_ELF_DWARF，那么Symbol的所有信息都有效。

### 采集uncore事件
libkperf支持uncore事件的采集，只有Counting模式支持uncore事件的采集（和perf一致）。
可以像这样设置PmuAttr：
```c++
// c++代码示例
char *evtList[1];
evtList[0] = "hisi_sccl1_ddrc0/flux_rd/";
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = 1;
int pd = PmuOpen(COUNTING, &attr);
```
```python
# python代码示例
import kperf
evtList = ["hisi_sccl1_ddrc0/flux_rd/"]
pmu_attr = kperf.PmuAttr(evtList=evtList)
pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
```

uncore事件的格式为```<device>/<event>/```，上面代码是采集设备hisi_sccl1_ddrc0的flux_rd事件。

也可以把设备索引号省略：
```c++
// c++代码示例
evtList[0] = "hisi_sccl1_ddrc/flux_rd/";
```

```python
# python代码示例
evtList = ["hisi_sccl1_ddrc/flux_rd/"]
```
这里把hisi_sccl1_ddrc0改为了hisi_sccl1_ddrc，这样会采集设备hisi_sccl1_ddrc0、hisi_sccl1_ddrc1、hisi_sccl1_ddrc2...，并且采集数据PmuData是所有设备数据的总和：count = count(hisi_sccl1_ddrc0) + count(hisi_sccl1_ddrc1) + count(hisi_sccl1_ddrc2) + ...

也可以通过```<device>/config=0xxx/```的方式来指定事件名：
```c++
// c++代码示例
evtList[0] = "hisi_sccl1_ddrc0/config=0x1/";
```

```python
# python代码示例
evtList = ["hisi_sccl1_ddrc0/config=0x1/"]
```

这样效果是和指定flux_rd是一样的。

### 采集tracepoint
libkperf支持tracepoint的采集，支持的tracepoint事件可以通过perf list来查看（通常需要root权限）。
可以这样设置PmuAttr：
```c++
// c++代码示例
char *evtList[1];
evtList[0] = "sched:sched_switch";
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = 1;
```

```python
# python代码示例
evtList = ["sched:sched_switch"]
pmu_attr = kperf.PmuAttr(
    evtList=evtList,
    sampleRate=1000,
    symbolMode=kperf.SymbolMode.RESOLVE_ELF # 不需要符号解析，可以不使用该参数
)
```

tracepoint支持Counting和Sampling两种模式，API调用流程和两者相似。
tracepoint能够获取每个事件特有的数据，比如sched:sched_switch包含的数据有：prev_comm, prev_pid, prev_prio, prev_state, next_comm, next_pid, next_prio.
想要查询每个事件包含哪些数据，可以查看/sys/kernel/tracing/events下面的文件内容，比如/sys/kernel/tracing/events/sched/sched_switch/format。

libkperf提供了接口PmuGetField来获取tracepoint的数据。比如对于sched:sched_switch，可以这样调用：
```c++
// c++代码示例
int prev_pid;
PmuGetField(pmuData->rawData, "prev_pid", &prev_pid, sizeof(prev_pid));
char next_comm[16];
PmuGetField(pmuData->rawData, "next_comm", &next_comm, sizeof(next_comm));
```

```python
# python代码示例
import kperf
import time
from ctypes import *

kperf.enable(pd)
time.sleep(3)
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
这里调用者需要提前了解数据的类型，并且指定数据的大小。数据的类型和大小仍然可以从/sys/kernel/tracing/下每个事件的format文件来得知。

### 事件分组
libkperf提供了事件分组的能力，能够让多个事件同时处于采集状态。
该功能类似于perf的如下使用方式：
```
perf stat -e "{cycles,branch-loads,branch-load-misses,iTLB-loads}",inst_retired
```

对于libkperf，可以通过设置PmuAttr的evtAttr字段来设定哪些事件放在一个group内。
比如，可以这样调用：
```c++
// c++代码示例
unsigned numEvt = 5;
char *evtList[numEvt] = {"cycles","branch-loads","branch-load-misses","iTLB-loads","inst_retired"};
// 前四个事件是一个分组
struct EvtAttr groupId[numEvt] = {1,1,1,1,-1};
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = numEvt;
attr.evtAttr = groupId;
```

```python
# python代码示例
import kperf
import time
evtList = ["cycles","branch-loads","branch-load-misses","iTLB-loads","inst_retired"]
# 前四个事件是一个分组
evtAttrList = [1,1,1,1,-1]
pmu_attr = kperf.PmuAttr(evtList=evtList, evtAttr = evtAttrList)
pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
kperf.enable(pd)
time.sleep(1)
kperf.disable(pd)
pmu_data = kperf.read(pd)
pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
for data in pmu_data.iter:
    print(f"cpu {data.cpu} count {data.count} evt {data.evt}")
```
上述代码把前四个事件设定为一个分组，groupId都设定为1，最后一个事件不分组，groupId设定为-1。
事件数组attr.evtList和事件属性数组attr.evtAttr必须一一对应，即长度必须一致。
或者attr.evtAttr也可以是空指针，那么所有事件都不分组。

事件分组的效果可以从PmuData.countPercent来体现。PmuData.countPercent表示事件实际采集时间除以事件期望采集时间。
对于同一组的事件，他们的countPercent是相同的。如果一个组的事件过多，超过了硬件计数器的数目，那么这个组的所有事件都不会被采集，countPercent会等于-1.

### 对进程子线程计数采集
```mermaid
graph TD
a(主线程) --perf stat--> b(创建线程)
b --> c(子线程)
c --end perf--> d(子线程退出)
```
考虑上面的场景：用perf stat对进程采集，之后进程创建了子线程，采集一段事件后，停止perf。
查看采集结果，perf只会显示主线程的采集结果，而无法看到子线程的结果：count = count(main thread) + count(thread). perf把子线程的数据聚合到了主线程上。

libkperf提供了采集子线程的能力。如果想要在上面场景中获取子线程的计数，可以把PmuAttr.incluceNewFork设置为1.
```c++
// c++代码示例
attr.includeNewFork = 1;
```
```python
# python代码示例
pmu_attr = kperf.PmuAttr(evtList=evtList, includeNewFork=True)
```
然后，通过PmuRead获取到的PmuData，便能包含子线程计数信息了。
注意，该功能是针对Counting模式，因为Sampling和SPE Sampling本身就会采集子线程的数据。

### 采集DDRC带宽
基于uncore事件可以计算DDRC的访存带宽，不同硬件平台有不同的计算方式。
鲲鹏芯片上的访存带宽公式可以参考openeuler kernel的tools/perf/pmu-events/arch/arm64/hisilicon/hip09/sys/uncore-ddrc.json：
```json
   {
	"MetricExpr": "flux_wr * 32 / duration_time",
	"BriefDescription": "Average bandwidth of DDRC memory write(Byte/s)",
	"Compat": "0x00000030",
	"MetricGroup": "DDRC",
	"MetricName": "ddrc_bw_write",
	"Unit": "hisi_sccl,ddrc"
   },
   {
	"MetricExpr": "flux_rd * 32 / duration_time",
	"BriefDescription": "Average bandwidth of DDRC memory read(Byte/s)",
	"Compat": "0x00000030",
	"MetricGroup": "DDRC",
	"MetricName": "ddrc_bw_read",
	"Unit": "hisi_sccl,ddrc"
   },
```

根据公式，采集flux_wr和flux_rd事件，用于计算带宽：
```c++
// c++代码示例
   
    vector<char *> evts = {
        "hisi_sccl1_ddrc/flux_rd/",
        "hisi_sccl3_ddrc/flux_rd/",
        "hisi_sccl5_ddrc/flux_rd/",
        "hisi_sccl7_ddrc/flux_rd/",
        "hisi_sccl1_ddrc/flux_wr/",
        "hisi_sccl3_ddrc/flux_wr/",
        "hisi_sccl5_ddrc/flux_wr/",
        "hisi_sccl7_ddrc/flux_wr/"
    }; // 采集hisi_scclX_ddrc设备下的flux_rd和flux_wr，具体设备名称因硬件而异，可以在/sys/devices/下查询。

    PmuAttr attr = {0};
    attr.evtList = evts.data();
    attr.numEvt = evts.size();

    int pd = PmuOpen(COUNTING, &attr);
    if (pd == -1) {
        cout << Perror() << "\n";
        return;
    }

    PmuEnable(pd);
    for (int i=0;i<60;++i) {
        sleep(1);
        PmuData *data = nullptr;
        int len = PmuRead(pd, &data);
        // 有8个uncore事件，所以data的长度等于8.
        // 前4个是4个numa的read带宽，后4个是4个numa的write带宽。
        for (int j=0;j<4;++j) {
            printf("read bandwidth: %f M/s\n", (float)data[j].count*32/1024/1024);
        }
        for (int j=4;j<8;++j) {
            printf("write bandwidth: %f M/s\n", (float)data[j].count*32/1024/1024);
        }
        PmuDataFree(data);
    }
    PmuDisable(pd);
    PmuClose(pd);
```

```python
# python代码示例
    import kperf
    import time
    evtList = [ "hisi_sccl1_ddrc/flux_rd/",
        "hisi_sccl3_ddrc/flux_rd/",
        "hisi_sccl5_ddrc/flux_rd/",
        "hisi_sccl7_ddrc/flux_rd/",
        "hisi_sccl1_ddrc/flux_wr/",
        "hisi_sccl3_ddrc/flux_wr/",
        "hisi_sccl5_ddrc/flux_wr/",
        "hisi_sccl7_ddrc/flux_wr/"] # 采集hisi_scclX_ddrc设备下的flux_rd和flux_wr，具体设备名称因硬件而异，可以在/sys/devices/下查询。
    
    pmu_attr = kperf.PmuAttr(evtList=evtList) 
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(kperf.error())
        exit(1)
    kperf.enable(pd)
    for i in range(60):
        time.sleep(1)
        pmu_data = kperf.read(pd)
        j = 0
        for data in pmu_data.iter:
            bandwidth = data.count*32/1024/1024
            if j < 4:
              print(f"read bandwidth: {bandwidth} M/s\n")
            if j >= 4 and j < 8:
              print(f"write bandwidth: {bandwidth} M/s\n")
            j += 1
    kperf.disable(pd)
    kperf.close(pd)
```

执行上述代码，输出的结果类似如下：
```
read bandwidth: 17.32 M/s
read bandwidth: 5.43 M/s
read bandwidth: 2.83 M/s
read bandwidth: 4.09 M/s
write bandwidth: 4.35 M/s
write bandwidth: 2.29 M/s
write bandwidth: 0.84 M/s
write bandwidth: 0.97 M/s
```

### 采集系统调用函数耗时信息
libkperf基于tracepoint事件采集能力，在原有能力的基础上，重新封装了一组相关的调用API，来提供采集系统调用函数耗时信息的能力，类似于perf trace命令

```
perf trace -e read,write
```

对于libkperf，可以通过设置PmuTraceAttr的funcs字段来需要采集哪些系统调用函数的耗时信息，pidList字段用于设定需要采集耗时的进程，cpuList字段用于设定需要采集哪些cpu上的系统调用耗时信息。三个参数如果任何一个为空，表示采集此字段采集系统上存在的所有信息，比如funcs为空，表示采集所有系统调用耗时信息。
比如，可以这样调用：
```c++
// c++代码示例
unsigned numFunc = 2;
const char *funs1 = "read";
const char *funs2 = "write";
const char *funcs[numFunc] = {funs1,funs2};
PmuTraceAttr traceAttr = {0};
traceAttr.funcs = funcs;
traceAttr.numFuncs = numFunc;
pd = PmuTraceOpen(TRACE_SYS_CALL, &traceAttr);
PmuTraceEnable(pd);
sleep(1);
PmuTraceDisable(pd);
int len = PmuTraceRead(pd, &data);
for(int i = 0; i < len; ++i) {
    print("funcName: %s, elspsedTime: %f ms pid: %d tid: %d cpu: %d comm: %s", data[i].funcs, data[i].elapsedTime, data[i].pid, data[i].tid, data[i].cpu, data[i].comm)
}
PmuTraceClose(pd);
```

```python
# python代码示例
import kperf
import time
funcList = ["read","write"]
pmu_trace_attr = kperf.PmuTraceAttr(funcs=funcList)
pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
kperf.trace_enable(pd)
time.sleep(1)
kperf.trace_disable(pd)
pmu_trace_data = kperf.trace_read(pd)
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} elapsedTime: {data.elapsedTime} ms pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
    
kperf.trace_close(pd)
```
执行上述代码，输出的结果类似如下：
```
funcName: read elapsedTime: 0.00110 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: read elapsedTime: 0.00118 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: read elapsedTime: 0.00125 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: read elapsedTime: 0.00123 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: write elapsedTime: 0.00105 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: write elapsedTime: 0.00107 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
funcName: write elapsedTime: 0.00118 ms pid: 997235 tid: 997235 cpu: 110 comm: taskset
```
支持采集的系统调用函数列表，在查看/sys/kernel/tracing/events/syscalls/下所有系统调用对应的enter和exit文件，去掉相同的前缀就是对应的系统调用函数名称；也可以基于提供的PmuSysCallFuncList函数获取对应的系统调用函数列表。