# libkperf

### 描述

libkperf是一个轻量级linux性能采集库，它能够让开发者以API的方式执行性能采集，包括pmu采样和符号解析。libkperf把采集数据内存化，使开发者能够在内存中直接处理采集数据，避免了读写perf.data带来的开销。

### 在什么时候可能会需要libkperf
- 想要轻量化采集系统和应用的性能数据。
- 当分析一个高负载的应用，却不想对应用性能有较大影响的时候。
- 不想管理多个性能分析进程(perf)，想获取易于解析的性能数据。
- 想轻量化地获取cpu的热点。
- 想分析cache的miss分布和时延。
- 想要跟踪系统调用函数的耗时。
- 想获取内存读写带宽。
- 想获取网络带宽和时延。

### 支持的CPU架构
- 鲲鹏

### 支持的OS
- openEuler
- OpenCloudOS
- TencentOS
- KylinOS
- CentOS

### Release Notes
v2.1:
- 支持SPE Data datasrc采集
- 支持metric-based profiling

v2.0:
- 支持编译反馈优化，提供生成perf.data格式文件的API。
- Counting模式支持基于BPF的低开销采集。
- Counting模式支持kernel bypass采集。
- 支持cgroup的性能计数和采样采集。

v1.4:
- 支持采集DDRC带宽、PCIe带宽，以及部分L3C和SMMU事件。
- 支持CPU频率采集。
- 提供blocked sample采集模式。
- 提供go的API。

v1.3:
- 支持对系统调用函数的耗时采集。
- 支持历史分支记录的采集（BRBE）。

v1.2:
- 支持tracepoint的采集和跟踪。
- 支持事件分组采集。

v1.1:
- 提供python的API。 

v1.0:
- 支持采集pmu计数、采样和SPE采样功能。
- 支持core和uncore事件的采集。
- 支持符号解析功能。

### 文档

- [详细使用文档](./docs/Details_Usage.md)
- [C/C++ API 说明文档](./docs/C_C++_API.md)
- [Python API 说明文档](./docs/Python_API.md)
- [Go API 说明文档](./docs/Go_API.md)

### 依赖
| 依赖项 | 版本要求 |
| --- | --- |
| GCC / glibc 最低依赖版本 | `gcc-4.8.5` 和 `glibc-2.17` |
| GCC / glibc 最高支持版本 | `gcc-12.2.0` 和 `glibc-2.36` |
| Python 最低依赖版本 | `python-3.6` |

说明：
- 如果编译报错提示没有numa.h文件，需要先安装对应的numactl-devel包。
- 如果编译连接在Found PythonInterp报CMake错误，需要先安装所需的python3-devel包。

### 快速使用
下面的流程将从获取源码开始，完成基础编译，并运行一个最小的C++示例。Python与Go的示例请参考后续的[其他示例](#其他示例)章节。

libkperf的PMU采集通常遵循以下调用顺序：

| API | 作用 |
| --- | --- |
| `PmuOpen` | 输入pid、core id和event，打开PMU设备 |
| `PmuEnable` | 开始采集 |
| `PmuRead` | 读取采集数据 |
| `PmuDisable` | 停止采集 |
| `PmuDatafree` | 释放PmuData |
| `PmuClose` | 关闭PMU设备 |

#### 1. 获取源码

```shell
git clone --recurse-submodules https://atomgit.com/openeuler/libkperf.git
cd libkperf
```

#### 2. 编译并安装基础 C/C++ 库

```shell
bash build.sh install_path=/path/to/install
```

如果不指定`install_path`，默认安装到当前目录下的`output`目录。后续示例中统一使用`/path/to/install`表示用户指定的安装目录，请根据实际环境替换为真实路径。

编译完成后，安装目录下会包含头文件和库文件：

```text
/path/to/install/
├── include
└── lib
```

如果库没有安装到系统默认库路径，运行依赖 libkperf 的程序前需要设置：

```shell
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
```

#### 3. 编译运行示例程序
新建 `example.cpp`：获取进程的PMU计数

```C++
#include <iostream>
#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

int main() {
    int pidList[1];
    pidList[0] = getpid();
    char *evtList[1];
    evtList[0] = "cycles";
    // 初始化事件列表，指定需要计数的事件cycles。
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 1;
    attr.pidList = pidList;
    attr.numPid = 1;
    // 调用PmuOpen，返回pd。pd表示该任务的id。
    int pd = PmuOpen(COUNTING, &attr);
    // 开始采集。
    PmuEnable(pd);
    // 采集1秒。
    sleep(1);
    // 停止采集。
    PmuDisable(pd);
    PmuData *data = NULL;
    // 读取PmuData，它是一个数组，长度是len。
    int len = PmuRead(pd, &data);
    for (int i = 0; i < len; ++i) {
        PmuData *d = &data[i];
        std::cout << "evt=" << d->evt << " count=" << d->count << std::endl;
    }
    // 释放PmuData。
    PmuDataFree(data);
    // 类似fd，当任务结束时调用PmuClose释放资源。
    PmuClose(pd);
}
```

编译并运行：

```shell
g++ -o example example.cpp -I /path/to/install/include -L /path/to/install/lib -lkperf -lsym
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
./example
```

### 编译选项

`build.sh` 支持通过 `option=value` 形式配置编译行为：

```shell
bash build.sh option=value
```

例如：

```shell
bash build.sh install_path=/path/to/install build_type=debug
```

#### 功能选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `install_path` | 当前目录下的 `output` | 指定安装路径，编译产物将安装到该路径下 |
| `build_type` | `Release` | 指定编译类型。设置 `debug`可编译调试版本 |
| `test` | `false` | 是否编译并运行libkperf C/C++测试用例 |
| `asan` | `false` | 是否启用AddressSanitizer编译，启动内存问题检测 |
| `bpf` | `false` | 是否编译counting模式下的BPF采集功能 |
| `elf_llvm` | `false` | ELF解析默认使用elfin-parser。启动该选项后，将使用llvm-symbolizer |
| `utrace` | `false` | 是否编译 `utrace` 模式，构建capstone |

示例：

```shell
bash build.sh iinstall_path=/home/test build_type=debug test=true
bash build.sh install_path=/path/to/install asan=true
bash build.sh install_path=/path/to/install utrace=true
```

#### Python 包

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `python` | `false` | 是否编译 Python 包 |
| `python_exe` | 系统默认 Python | 指定用于安装Python包的解释器 |
| `whl` | `false` | 是否生成 Python `.whl` 安装包 |

**编译并安装 Python 包**：

```shell
bash build.sh install_path=/path/to/install python=true
```

如果环境中有多个 Python 版本：

```shell
bash build.sh install_path=/path/to/install python=true python_exe=$(which python3)
```

生成 `.whl` 安装包：

```shell
bash build.sh install_path=/path/to/install python=true whl=true
```

**卸载 Python 包**：

```shell
python3 -m pip uninstall -y libkperf
```

**注**：如果Python模块运行时报错类似下面内容：

```text
OSError: /usr/lib/python3.9/site-packages/_libkperf/libsym.so: cannot open shared object file: No such file or directory
```

可能是 `setuptools` 版本不适配导致安装失败，可尝试降级：

```shell
python3 -m pip uninstall setuptools
python3 -m pip install setuptools==58
```

**Python用例代码运行**：
```shell
cd python/tests
pytest test_*.py -s -v
```

#### Go 包

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `go` | `false` | 是否编译 Go 包 |

**编译Go包**：

```shell
bash build.sh install_path=/path/to/install go=true
```

执行成功后，将 `go/src/libkperf` 整个目录拷贝到 `$GOPATH/src/` 目录下，其中 `$GOPATH` 为用户项目目录。

**Go用例代码运行**
```shell
cd go/src/libkperf_test
export GO111MODULE=off
export LD_LIBRARY_PATH=../libkperf/lib:$LD_LIBRARY_PATH
go test -v # 全部运行
go test -v -test.run TestCount #指定运行的用例
```

#### Java

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `java_agent` | `false` | 是否编译java/java_agent模块，针对Java进程，增加perf-pid.map的数据解析 |
| `java_trace` | `false` | 是否编译java/java_trace模块，使用ASM字节码插桩方法对Java程序进行trace |

**编译 Java Agent**：

```shell
bash build.sh install_path=/path/to/install java_agent=true
```

编译完成后，会在安装目录的 `lib` 子目录生成 `libkperfmap.so`。使用 Java 符号解析功能前，需要设置：

```shell
export KPERF_JAVA_AGENT_LIB="/path/to/install/lib:libkperfmap.so"
```

**编译 Java Trace**：

```shell
bash build.sh java_trace=true
```
使用该选项前，请确保 Java 环境变量已正确配置，并且系统中已安装 Gradle 或 Maven。

### 其他示例

#### Python：读取 PMU 计数

```python
import time
from collections import defaultdict
import subprocess

import kperf

evtList = ["r11", "cycles"]
pmu_attr = kperf.PmuAttr(evtList=evtList)
pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
if pd == -1:
    print(kperf.errorno())
    print(kperf.error())
    raise SystemExit(1)

kperf.enable(pd)

for _ in range(3):
    time.sleep(1)
    data_iter = kperf.read(pd)
    evtMap = defaultdict(int)
    for data in data_iter.iter:
        evtMap[data.evt] += data.count

    for evt, count in evtMap.items():
        print(f"event: {evt} count: {count}")

kperf.disable(pd)
kperf.close(pd)
```

运行：
```bash
python example.py
```

### Go：读取 PMU 计数

```go
package main
import "libkperf/kperf"
import "fmt"
import "time"

func main() {
  attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF}
	fd, err := kperf.PmuOpen(kperf.COUNT, attr)
	if err != nil {
		fmt.Printf("kperf pmuopen counting failed, expect err is nil, but is %v\n", err)
    return
	}
	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		fmt.Printf("kperf pmuread failed, expect err is nil, but is %v\n", err)
    return
	}

	for _, o := range dataVo.GoData {
    fmt.Printf("event: %v count: %v\n", o.Evt, o.Count)
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}
```

编译运行：
```shell
go build example.go
./example
```

静态模式编译：

```shell
go build -tags="static" example.go
```

或直接运行：
```shell
go run example.go
```

#### C++：进程采样并解析调用栈
```C++
#include <iostream>

#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

int main() {
    int pid = getpid();
    int pidList[1];
    pidList[0] = pid;
    char *evtList[1];
    evtList[0] = "cycles";
    // 初始化事件列表，指定需要计数的事件cycles。
    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 1;
    attr.pidList = pidList;
    attr.numPid = 1;
    attr.symbolMode = RESOLVE_ELF_DWARF;
    attr.callStack = 1;
    attr.freq = 200;
    attr.useFreq = 1;
    // 调用PmuOpen，返回pd。pd表示该任务的id。
    int pd = PmuOpen(SAMPLING, &attr);
    // 开始采集。
    PmuEnable(pd);
    // 采集1秒。
    sleep(1);
    // 停止采集。
    PmuDisable(pd);
    PmuData *data = NULL;
    // 读取PmuData，它是一个数组，长度是len。
    int len = PmuRead(pd, &data);
    for (int i = 0; i < len; ++i) {
        // 获取数组的一个元素。
        PmuData *d = &data[i];
        // 获取调用栈对象，它是一个链表。
        Stack *stack = d->stack;
        while (stack) {
            // 获取符号对象。
            if (stack->symbol) {
                Symbol *data = stack->symbol;
                std::cout << std::hex << data->addr << " " << data->symbolName << "+0x" << data->offset << " "
                          << data->codeMapAddr << " (" << data->module << ")"
                          << " (" << std::dec << data->fileName << ":" << data->lineNum << ")" << std::endl;

            }
            stack = stack->next;
        }
    }
    // 释放PmuData。
    PmuDataFree(data);
    // 类似fd，当任务结束时调用PmuClose释放资源。
    PmuClose(pd);
}
```

编译运行：

```shell
g++ -o sample sample.cpp -I /path/to/install/include -L /path/to/install/lib -lkperf -lsym
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
./sample
```

### FAQ
#### 1、Q：如何正确使用launch app模式进行进程采集
  * PmuOpen之后，再通过信号，唤醒子进程调用application
  * 无需调用PmuEnable
  * 推荐使用单fd打开，可显著降低多线程场景下的采集开销
  * 参考文档：[详细使用参考](./docs/Details_Usage.md#%E9%80%9A%E8%BF%87%E4%BD%BF%E8%83%BDenableexecon%E7%9A%84%E6%96%B9%E5%BC%8F%E9%87%87%E9%9B%86%E8%BF%9B%E7%A8%8B)

#### 2、Q：为何多线程应用采集时会出现数据丢失
  * 原因：PmuOpen/PmuEnable/PmuDisable等操作在采集多线程应用场景时，其中每个线程加载和使能存在先后
  * 当前解决方案：
    * 使用launch模式（单fd打开，已验证可提高PmuOpen效率）[详细使用参考](./docs/Details_Usage.md#%E9%80%9A%E8%BF%87%E4%BD%BF%E8%83%BDenableexecon%E7%9A%84%E6%96%B9%E5%BC%8F%E9%87%87%E9%9B%86%E8%BF%9B%E7%A8%8B)
    * 使用--per-thread模式 （fd数量=事件数 X 线程数），减少核级数开销 参考

#### 3、Q：如何提升符号解析速度？尤其在压测场景下？
  * 问题：原DWARF解析耗时>1s,影响性能
  * 已优化方案：
    * 集成llvm-symbolizer，目前行号解析效率提升30X
    * 支持配置模式，symbolMode使用RESOLVE_ELF模式，将不再去解析获取源文件和行号

#### 4、Q：如何支持Cgroup采集？存在哪些限制？
  * 限制：
    * uncore与core事件需分两次PmuOpen
    * PA事件不支持在采集进程模式下与core事件共用PmuOpen
  * 建议：采集时注意事件分离，避免错误合并

#### 5、Q：SPE采集时为何PmuRead耗时长？如何优化
  * 原因：SPE模式下，PmuRead会自动调用PmuDisable，读取完数据会自动调用PmuEnable
  * 建议：
    * 单线程顺序执行PmuOpen/PmuCollect/PmuClose.

#### 6、Q：如何采集HITM事件（Flase Sharing）并保证数据稳定性
  * 采集方式： 指定CPU核心进行采集
  * 稳定性：
    * 指定cpu采集： 数据稳定、地址正确
    * 指定pid采集： 存在不一致，建议优先使用指定cpu采集