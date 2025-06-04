### kperf.PmuOpen

func PmuOpen(collectType C.enum_PmuTaskType, attr PmuAttr) (int, error)
初始化Pmu事件

* var collecType C.enum_PmuTaskType

  * COUNT   PMU计数模式
  * SAMPLE  PMU采样模式
  * SPE     SPE采样模式
* type PmuAttr struct

  * EvtList []string
    采集的事件列表，事件列表可以通过perf list查询
  * PidList []int
    采集的进程id列表
  * CpuList []int
    指定的使用cpu核采集列表，默认采集所有逻辑核
  * EvtAttr []int
    事件分组列表，和evtList搭配使用，同组事件需要使用相同数字表示，不同组事件使用不同的数字代表，如果数字为-1，则不参与事件分组
  * SampleRate uint32
    采样频率，可通过/proc/sys/kernel/perf_event_max_sample_rate调整最大的采样频率
  * UseFreq bool
    是否启用采样频率，如果设置，将可以使用采样频率
  * ExcludeUser bool
    排除对用户态数据的采集
  * ExcludeKernel bool
    排除对内核态数据的采集
  * SymbolMode C.enum_SymbolMode
    符号采集模式
    ELF 仅支持ELF数据采集，解析function，不解析行号
    ELF_DWARF 既支持ELF数据采集，也支持行号解析
  * CallStack
    是否采集调用栈，默认不采集，只取栈顶数据
  * DataFilter C.enum_SpeFilter
    * TS_ENABLE           使能通用计时器
    * PA_ENABLE           采集物理地址
    * PCT_ENABLE          采集物理地址的事件戳替代虚拟地址
    * JITTER              采样时使用共振防止抖动
    * BRANCH_FILTER       只采集分支数据
    * LOAD_FILTER         只采集已加载数据
    * STORE_FILTER        只采集存储数据
    * SPE_DATA_ALL        以上所有Filter的组合
  * EvFilter C.enum_SpeEventFilter
    * SPE_EVENT_RETIRED       instruction retired
    * SPE_EVENT_L1DMISS       L1D refill
    * SPE_EVENT_TLB_WALK      TLB refill
    * SPE_EVENT_MISPREDICTED  mispredict
  * MinLatency 仅收集该latency或者更高的样本数据
  * IncludeNewFork bool
    是否支持子线程拆分，仅在COUNTING模式中支持
  * BranchSampleFilter bool
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
* 返回值是int,error, 如果error不等于nil，则返回的int值为对应采集任务ID

```go
import "libkperf/kperf"
import "fmt"

func main() {
    attr := kperf.PmuAttr{EvtList:[]string{"cycles", "branch-misses"}}
    pd, err := kperf.PmuOpen(kperf.COUNT, attr)
	if err != nil {
		fmt.Printf("kperf pmuopen counting failed, expect err is nil, but is %v", err)
        return
	}
}
```

### kperf.PmuEnable

func PmuEnable(fd int) error
该接口用于开启某个pd的采样能力

* 返回值为error
* error != nil，则使能异常
* error == nil，则使能正常

### kperf.PmuDisable

func PmuDisable(fd int) error

* 返回值为error
* error != nil，则关闭采集异常
* error == nil，则关闭采集正常

```go
kperf.PmuEnable(pd)
time.Sleep(time.Second)
kperf.PmuDisable(pd)
```

### kperf.Read

func PmuRead(fd int) (PmuDataVo, error)

* type PmuDataVo struct
  * GoData []PmuData
* type PmuData struct
  * Evt string 	事件
  * Ts uint64		Pmu采样时间戳
  * Pid int       进程ID
  * Tid int		线程ID
  * Cpu int	CPUID
  * Comm string	运行指令名称
  * Period uint64 采样间隔
  * Count uint64	计数
  * CountPercent float64  计数比值，使能时间/运行时间
  * CpuTopo CpuTopology
    * CoreId 系统核ID
    * NumaId numa ID
    * SocketId socket ID
  * Symbols []sym.Symbol
    * Addr uint64 地址
    * Module string 模块名称
    * SymbolName string 符号名
    * MangleName string mangle后的符号名
    * LineNum uint32 行号
    * Offset uint64 地址偏移
    * CodeMapEndAddr uint64 结束地址
    * CodeMapAddr uint64 初始地址
  * BranchRecords
    * FromAddr uint64 起始地址
    * ToAddr   uint64 跳转地址
    * Cycles   uint64 执行指令数
  * SpeExt SpeDataExt
    * Pa uint64     物理地址
    * Va uint64	   虚拟地址
    * Event uint64  混合事件的比特位
    * Lat uint16    调度操作到执行操作的周期数

```go
//go 代码示例
dataVo, err := kperf.PmuRead(fd)
if err != nil {
    fmt.Printf("kperf pmuread failed, expect err is nil, but is %v\n", err)
    return
}

for _, o := range dataVo.GoData {
    fmt.Printf("cpu %v count %v evt %v\n", o.Cpu, o.Count, o.Evt)
}
```

### kperf.PmuClose

func PmuClose(fd int) 接口用于清理该pd所有的对应数据，并移除该pd

### kperf.PmuDumpData

func PmuDumpData(dataVo PmuDataVo, filePath string, dumpDwf bool) error

* dataVo 由kperf.PmuRead读取返回的数据
* filePath 数据转储的路径
* dumpDwf 是否写入dwarf数据

### kperf.PmuEventList

func PmuEventList(eventType C.enum_PmuEventType) []string
查找所有的事件列表

* eventType
  * CORE_EVENT   获取core事件列表
  * UNCORE_EVENT 获取uncore事件列表
  * TRACE_EVENT  获取tracepointer事件列表
  * ALL_EVENT    获取所有的事件列表
* 返回数据为事件列表

```go
import "libkperf/kperf"
import "fmt"

func main() {
    evtList := kperf.PmuEventList(kperf.CORE_EVENT)
	if len(evtList) == 0 {
		fmt.Printf("core event can't be empty\n")
        return
	}
	for _, v := range evtList {
		fmt.Printf("%v\n", v)
	}
}
```

### kperf.PmuTraceOpen

func PmuTraceOpen(traceType C.enum_PmuTraceType, traceAttr PmuTraceAttr) (int, error) 初始化采集系统调用函数能力

* traceType C.enum_PmuTraceType
  * TRACE_SYS_CALL 采集系统调用函数事件
* traceAttr PmuTraceAttr
  * Funcs []string 采集的系统调用函数列表，可以查看/usr/include/asm-generic/unistd.h文件，默认为空，表示采集所有系统调用函数
  * PidList []int采集的进程列表，默认为空，表示采集所有进程
  * CpuList []int采集的cpu列表，默认为空，表示采集所有cpu
* 返回值是int和error，如果error！=nil则采集初始化失败，error==nil则采集初始化成功

以下是kperf.PmuTraceOpen用法:

```go
import "libkperf/kperf"
import "fmt"

func main() {
    traceAttr := kperf.PmuTraceAttr{Funcs:[]string{"read", "write"}}
	taskId, err := kperf.PmuTraceOpen(kperf.TRACE_SYS_CALL, traceAttr)
	if err != nil {
		fmt.Printf("pmu trace open failed, expect err is nil, but is %v\n", err)
        return
	}
}

```

### Kperf.PmuTraceEnable

func PmuTraceEnable(taskId int) error
该接口用于开启某个pd的采样能力

* 返回值为error
* error != nil，则使能异常
* error == nil，则使能正常

### kperf.PmuTraceDisable

func PmuTraceDisable(taskId int) error

* 返回值为error
* error != nil，则关闭采集异常
* error == nil，则关闭采集正常

```go
kperf.PmuTraceEnable(pd)
time.Sleep(time.Second)
kperf.PmuTraceDisable(pd)
```

### kperf.PmuTraceRead

func PmuTraceRead(taskId int) (PmuTraceDataVo, error)

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

```go
traceList, err := kperf.PmuTraceRead(taskId)
if err != nil {
    fmt.Printf("pmu trace read failed, expect err is nil, but is %v\n", err)
    return
}

for _, v := range traceList.GoTraceData {
    fmt.Printf("funcName: %v, elapsedTime: %v ms startTs: %v pid: %v tid: %v, cpu: %v comm: %v\n", v.FuncName, v.ElapsedTime, v.StartTs, v.Pid, v.Tid, v.Cpu, v.Comm)
}
```

### kperf.PmuTraceClose

func PmuTraceClose(taskId int) 该接口用于清理该pd所有对应的数据，并移除该taskId

### kperf.PmuSysCallFuncList

func PmuSysCallFuncList() []string 查找所有的系统调用函数列表

```go
import "libkperf/kperf"
import "fmt"

func main() {
    syscallList := kperf.PmuSysCallFuncList()
    if syscallList == nil {
        fmt.Printf("sys call list is empty")
    } else {
        for _, funcName := range syscallList {
            fmt.Printf("func name %v\n", funcName)
        }
    }
}
```

### kperf.PmuDeviceBdfList

func PmuDeviceBdfList(bdfType C.enum_PmuBdfType) ([]string, error) 从系统中查找所有的bdf列表
* bdfType C.enum_PmuBdfType
  * PMU_BDF_TYPE_PCIE  PCIE设备对应的bdf
  * PMU_BDF_TYPE_SMMU  SMMU设备对应的bdf
```go
import "libkperf/kperf"
import "fmt"

func main() {
   pcieBdfList, err := kperf.PmuDeviceBdfList(kperf.PMU_BDF_TYPE_PCIE)
   if err != nil {
      fmt.Printf("kperf GetDeviceBdfList failed, expect err is nil, but is %v\n", err)
   } 
   for _, v := range pcieBdfList {
      fmt.Printf("bdf is %v\n", v)
   }
}
```
### kperf.PmuDeviceOpen

func PmuDeviceOpen(attr []PmuDeviceAttr) (int, error) 初始化采集uncore事件指标的能力

* type PmuDeviceAttr struct:
  * Metric: 指定需要采集的指标
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
  * Bdf: 指定需要采集设备的bdf号，只对pcie和smmu指标有效
* 返回值是int和error，pd > 0表示初始化成功，pd == -1初始化失败，可通过kperf.error()查看错误信息，以下是一个kperf.device_open的示例

```go
import "libkperf/kperf"
import "fmt"

func main() {
    deviceAttrs := []kperf.PmuDeviceAttr{kperf.PmuDeviceAttr{Metric: kperf.PMU_L3_LAT}}
	fd, err := kperf.PmuDeviceOpen(deviceAttrs)
	if err != nil {
		fmt.Printf("kperf PmuDeviceOpen failed, expect err is nil, but is %v\n", err)
	}
}
```

### kperf.PmuGetDevMetric

func PmuGetDevMetric(dataVo PmuDataVo, deviceAttr []PmuDeviceAttr) (PmuDeviceDataVo, error)  对原始read接口的数据，按照device_attr中给定的指标进行数据聚合接口，返回值是PmuDeviceData

* type PmuDataVo struct: PmuRead接口返回的原始数据
* []PmuDeviceAttr: 指定需要聚合的指标参数
* typ PmuDeviceDataVo struct:
  * GoDeviceData []PmuDeviceData
* type DdrDataStructure struct {
    ChannelId uint32             ddr数据的channel编号
    DdrNumaId uint32             ddr数据的numa编号
    SocketId uint32              ddr数据的socket编号
  }
* type PmuDeviceData struct:
  * Metric C.enum_PmuDeviceMetric 采集的指标
	* Count float64                 指标的计数值
	* Mode C.enum_PmuMetricMode     指标的采集类型，按core、按numa、按channel还是按bdf号
	* CoreId uint32                 数据的core编号
	* NumaId uint32                 数据的numa编号
	* ClusterId uint32              簇ID
	* Bdf string                    数据的bdf编号
  * DdrDataStructure              ddr相关的统计数据

### kperf.DevDataFree 

func DevDataFree(devVo PmuDeviceDataVo)  清理PmuDeviceData的指针数据

### kperf.PmuGetClusterCore

func PmuGetClusterCore(clusterId uint) ([]uint, error) 查询指定clusterId下对应的core列表

* clusterId CPU的clusterId编号
* 返回值：当前clusterId下对应的core列表,出现错误则列表为空，且error不为空

```go
import "libkperf/kperf"
import "fmt"

func main() {
    clusterId := uint(1)
	coreList, err := kperf.PmuGetClusterCore(clusterId)
	if err != nil {
		fmt.Printf("kperf PmuGetClusterCore failed, expect err is nil, but is %v\n", err)
    return
	}
	for _, v := range coreList {
		fmt.Printf("coreId has:%v\n", v)
	}
}
```

### kperf.PmuGetNumaCore

func PmuGetNumaCore(nodeId uint) ([]uint, error)  查询指定numaId下对应的core列表

* nodeId numa对应的ID
* 返回值为对应numaId下的cpu core列表，出现错误则列表为空，且error不为空

```go
import "libkperf/kperf"
import "fmt"

func main() {
    nodeId := uint(0)
	coreList, err := kperf.PmuGetNumaCore(nodeId)
	if err != nil {
		fmt.Printf("kperf PmuGetNumaCore failed, expect err is nil, but is %v\n", err)
    return
	}
	for _, v := range coreList {
		fmt.Printf("coreId has:%v\n", v)
	}
}
```


### kperf.PmuGetCpuFreq

func PmuGetCpuFreq(core	uint) (int64, error) 查询当前系统指定core的实时CPU频率

* core cpu coreId
* 返回值为int64, 为当前cpu core的实时频率，出现错误频率为-1，且error不为空

```go
import "libkperf/kperf"
import "fmt"

func main() {
    coreId := uint(0)
	freq, err := kperf.PmuGetCpuFreq(coreId)
	if err != nil {
		fmt.Printf("kperf PmuGetCpuFreq failed, expect err is nil, but is %v\n", err)
    return
	}
	fmt.Printf("coreId %v freq is %v\n", coreId, freq)
}
```

### kperf.PmuOpenCpuFreqSampling

func PmuOpenCpuFreqSampling(period uint) (error) 开启cpu频率采集

### kperf.PmuCloseCpuFreqSampling

func PmuCloseCpuFreqSampling() 关闭cpu频率采集

### kperf.PmuReadCpuFreqDetail

func PmuReadCpuFreqDetail() ([]PmuCpuFreqDetail) 读取开启频率采集到读取时间内的cpu最大频率、最小频率以及平均频率
```go
import "libkperf/kperf"
import "fmt"

func main() {
    err := kperf.PmuOpenCpuFreqSampling(100)
    if err != nil {
		  fmt.Printf("kperf PmuOpenCpuFreqSampling failed, expect err is nil, but is %v", err)
	  }

    freqList := kperf.PmuReadCpuFreqDetail()
  	for _, v := range freqList {
	  	fmt.Printf("cpuId=%v, minFreq=%d, maxFreq=%d, avgFreq=%d", v.CpuId, v.MinFreq, v.MaxFreq, v.AvgFreq)
	  }

	  kperf.PmuCloseCpuFreqSampling()
}
```

### kperf.ResolvePmuDataSymbol

func ResolvePmuDataSymbol(dataVo PmuDataVo) error 当SymbolMode不设置或者设置为0时，可通过该接口解析PmuRead返回的PmuData数据中的符号
```go
import "libkperf/kperf"
import "fmt"

func main() {
    attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, CallStack:true, SampleRate: 1000, UseFreq:true}
    fd, err := kperf.PmuOpen(kperf.SAMPLE, attr)
    if err != nil {
      fmt.Printf("kperf pmuopen sample failed, expect err is nil, but is %v", err)
      return
    }

    kperf.PmuEnable(fd)
    time.Sleep(time.Second)
    kperf.PmuDisable(fd)

    dataVo, err := kperf.PmuRead(fd)
    if err != nil {
      fmt.Printf("kperf pmuread failed, expect err is nil, but is %v", err)
      return
    }

    for _, o := range dataVo.GoData {
      if len(o.Symbols) != 0 {
        fmt.Printf("expect symbol data is empty, but is not")
      }
    }

    parseErr := kperf.ResolvePmuDataSymbol(dataVo)
    if parseErr != nil {
      fmt.Printf("kperf ResolvePmuDataSymbol failed, expect err is nil, but is %v", parseErr)
    }

    for _, o := range dataVo.GoData {
      if len(o.Symbols) == 0 {
        fmt.Printf("expect symbol data is not empty, but is empty")
      }
    }
    kperf.PmuDataFree(dataVo)
    kperf.PmuClose(fd)
}
```