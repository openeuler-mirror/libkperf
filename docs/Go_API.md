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
    是否支持子线程拆分，仅在COUTING模式中支持
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

* 返回值时int,error, 如果error不等于nil，则返回的int值为对应采集任务ID

```go
import "libkperf/kperf"
import "fmt"

func main() {
    attr := kperf.PmuAttr{EvtList:[]string{"cycles", "branch-misses"}}
    pd, err := kperf.PmuOpen(kperf.COUNT, attr)
	if err != nil {
		fmt.Printf("kperf pmuopen couting failed, expect err is nil, but is %v", err)
        return
	}
}
```

### kperf.PmuEnable

func PmuEnable(fd int) error
该接口用于开启某个pd的采样能力

返回值为error
error != nil，则使能异常
error == nil, 则使能正常

### kperf.PmuDisable

func PmuDisable(fd int) error

返回值为error
error != nil，则关闭采集异常
error == nil, 则关闭采集正常

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
	* Cpu uint32	CPUID					  
	* Comm string	运行指令名称					 
	* Period uint64 采样间隔                      
	* Count uint64	计数				 
	* CountPercent float64  计数比值，使能时间/运行时间
	* CpuTopo CpuTopolopy 
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
    * BranchRecords []BranchSampleRecord 
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

以下是kperf.PmuTraceOpen:

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

返回值为error
error != nil，则使能异常
error == nil, 则使能正常

### kperf.PmuTraceDisable

func PmuTraceDisable(taskId int) error

返回值为error
error != nil，则关闭采集异常
error == nil, 则关闭采集正常

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
    * Pid int 进程id
    * Tid int 线程id
    * Cpu uint32 cpu号
    * Comm string 执行指令名称

```go
traceList, err := kperf.PmuTraceRead(taskId)
if err != nil {
    fmt.Printf("pmu trace read failed, expect err is nil, but is %v\n", err)
    return
}

for _, v := range traceList.GoTraceData {
    fmt.Printf("funcName: %v, elapsedTime: %v ms pid: %v tid: %v, cpu: %v comm: %v\n", v.FuncName, v.ElapsedTime, v.Pid, v.Tid, v.Cpu, v.Comm)
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


