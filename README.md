# libkperf

#### 描述

libkperf是一个轻量级linux性能采集库，它能够让开发者以API的方式执行性能采集，包括pmu采样和符号解析。libkperf把采集数据内存化，使开发者能够在内存中直接处理采集数据，避免了读写perf.data带来的开销。

#### 在什么时候可能会需要libkperf
- 想要轻量化采集系统和应用的性能数据。
- 当分析一个高负载的应用，却不想对应用性能有较大影响的时候。
- 不想管理多个性能分析进程(perf)，想获取易于解析的性能数据。
- 想轻量化地获取cpu的热点。
- 想分析cache的miss分布和时延。
- 想要跟踪系统调用函数的耗时。
- 想获取内存读写带宽。
- 想获取网络带宽和时延。

#### 支持的CPU架构
- 鲲鹏

#### 支持的OS
- openEuler
- OpenCloudOS
- TencentOS
- KylinOS
- CentOS

#### Release Notes
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

#### 编译
最低依赖gcc版本：
- gcc-4.8.5 和 glibc-2.17

最低依赖python版本：
- python-3.6

编译生成动态库和C的API：
```shell
git clone --recurse-submodules https://gitee.com/openeuler/libkperf.git
cd libkperf
bash build.sh install_path=/path/to/install
```
说明：
- 如果编译报错提示没有numa.h文件，需要先安装对应的numactl-devel包。
- 如果编译连接在Found PythonInterp报CMake错误，需要先安装所需的python3-devel包。

如果想要编译调试版本：
```shell
bash build.sh install_path=/path/to/install build_type=debug
```

如果想要编译python包：
```shell
bash build.sh install_path=/path/to/install python=true
```

安装后若需要卸载python库， 可以执行下述命令
```shell
python3 -m pip uninstall -y libkperf
```

想要编译go的包
```shell
bash build.sh go=true
```
执行成功后，需要把go/src/libkperf整个目录拷贝到GOPATH/src/下，GOPATH为用户项目目录

#### 文档
详细文档可以参考docs目录：
- [详细使用文档](./docs/Details_Usage.md)

Python API文档可以参考docs目录：
- [Python API说明文档](./docs/Python_API.md)

Go API文档可以参考GO_API.md:
- [GO API说明文档](./docs/Go_API.md)

#### 快速使用

主要有以下几个API： 
- PmuOpen
   输入pid、core id和event，打开pmu设备。
- PmuEnable
  开始收集。
- PmuRead
  读取采集数据。
- PmuDisable
  停止收集。
- PmuClose
  关闭pmu设备。

以下是一些示例： 

- 获取进程的pmu计数
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
        std::cout << "evt=" << d->evt << "count=" << d->count << std::endl;
    }
    // 释放PmuData。
    PmuDataFree(data);
    // 类似fd，当任务结束时调用PmuClose释放资源。
    PmuClose(pd);
}
```

- 对进程进行采样
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

- Python 例子
```python
import time
from collections import defaultdict
import subprocess

import kperf

def Counting():
    evtList = ["r11", "cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(kperf.errorno())
        print(kperf.error())

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

- Go 例子
```go
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


#### 示例代码快速运行参考：

* **针对C++示例代码：**
可以将示例代码放到一个c++源文件的main函数中，并引用此动态库相关的头文件(#include "symbol.h"、#include "pmu.h"、#include "pcerrc.h")，再使用g++编译链接此动态库，生成可执行文件即可运行。

编译指令参考：
```bash
g++ -o example example.cpp -I /install_path/include -L /install_path/lib -lkperf -lsym
```

* **针对python示例代码：**
可以将示例代码放到一个python源文件的main函数中，并导入此动态库相关的头文件包(import kperf、import ksym)，在运行此python文件即可。

运行指令参考：
```bash
python example.py
```

* **针对Go示例代码:**
可以直接跳转到 go/src/libkperf_test目录下
```shell
export GO111MODULE=off
export LD_LIBRARY_PATH=../libkperf/lib:$LD_LIBRARY_PATH
go test -v # 全部运行
go test -v -test.run TestCount #指定运行的用例
```

* **GO静态模式编译:**
```shell
go build -tags="static"
```

如果动态库没有安装到系统默认的/usr/bin目录下，在运行时会报错：找不到"libkperf.so"动态库。此时需要将"libkperf.so"所在目录添加到"LD_LIBRARY_PATH"环境变量中：
```shell
export LD_LIBRARY_PATH=/XXX/libkperf/output/lib:$LD_LIBRARY_PATH
```