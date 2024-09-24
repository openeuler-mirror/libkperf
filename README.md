# libkperf

#### 描述

libkperf是一个轻量级linux性能采集库，它能够让开发者以API的方式执行性能采集，包括pmu采样和符号解析。libkperf把采集数据内存化，使开发者能够在内存中直接处理采集数据，避免了读写perf.data带来的开销。

#### 编译

编译生成动态库和C的API：
```shell
git clone --recurse-submodules https://gitee.com/openeuler/libkperf.git
cd libkperf
bash build.sh install_path=/path/to/install
```

如果想要编译调试版本：
```shell
bash build.sh install_path=/path/to/install buildType=debug
```

如果想要编译python包：
```shell
bash build.sh install_path=/path/to/install python=true
```

安装后若需要卸载python库， 可以执行下述命令
```shell
python3 -m pip uninstall -y libkperf
```

#### 文档
详细文档可以参考docs目录：
- [详细使用文档](./docs/Details.md)

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

- 获取进程的pmu计数。 

```C
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
	  ...
}
// 释放PmuData。
PmuDataFree(data);
// 类似fd，当任务结束时调用PmuClose释放资源。
PmuClose(pd);
```

- 对进程进行采样
```C
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
            ...
        }
        stack = stack->next;
    }
}
// 释放PmuData。
PmuDataFree(data);
// 类似fd，当任务结束时调用PmuClose释放资源。
PmuClose(pd);
```

Python 例子:
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
