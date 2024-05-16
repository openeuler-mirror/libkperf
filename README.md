# libkperf

#### 描述

实现了一个低开销的pmu集合库，为计数、采样和符号解析提供了抽象接口。 

#### 软件构架

这个存储库包括两个模块：pmu集合和符号解析。 

Pmu收集模块是在syscall perf_event_open上开发的，用于启用内核pmu计数和采样，根据用户输入使用-thread或per-core模式。
从环形缓冲区读取Pmu数据包，并将其解析为不同的结构，进行计数，采样和spe采样。
对于采样，根据ips或pc从数据包中解析符号。每个符号包含符号名称、地址、源文件路径和行号（如果可能）。

符号解析模块是在elfin-parser上开发的，elfin-parser是一个解析elf和dwarf的库。该模块以设计良好的数据结构管理所有符号数据，以实现快速查询。 

#### 安装

运行bash脚本:

```shell
bash build.sh installPath=/home/libkperf
```

如上，头文件和库将安装到/home/libkperf输出目录，installPath是可选参数，若没有设置，则默认安装到libkperf下的output目录。

如果要额外增加python库支持，可以通过如下方式安装
```shell
bash build.sh python=true
```

安装后若需要卸载python库， 可以执行下述命令
```shell
python3 -m pip unistall -y libkperf
```

#### 指令

所有pmu功能都通过以下接口完成： 

- PmuOpen
   输入pid、core id和event，打开pmu设备。
- PmuEnable
  开始收集。
- PmuRead
   读取pmu数据并返回一个列表。
- PmuDisable
  停止收集。
- PmuClose
  关闭PMU装置。

以下是一些示例： 

- 获取进程的pmu计数。 

```
int pidList[1];
pidList[0] = pid;
char *evtList[1];
evtList[0] = "cycles";
// Initialize event list and pid list in PmuAttr.
// There is one event in list, named 'cycles'.
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = 1;
attr.pidList = pidList;
attr.numPid = 1;
// Call PmuOpen and pmu descriptor <pd> is return.
// <pd> is an identity for current task.
int pd = PmuOpen(COUNTING, &attr);
// Start collection.
PmuEnable(pd);
// Collect for one second.
sleep(1);
// Stop collection.
PmuDisable(pd);
PmuData *data = NULL;
// Read pmu data. You can also read data before PmuDisable.
int len = PmuRead(pd, &data);
for (int i = 0; i < len; ++i) {
	...
}
// To free PmuData, call PmuDataFree.
PmuDataFree(data);
// Like fd, call PmuClose if pd will not be used.
PmuClose(pd);
```

Python 例子:
```python
import time
from collections import defaultdict

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
        for data in data_iter:
            evtMap[data.evt] += data.count

        for evt, count in evtMap.items():
            print(f"event: {evt} count: {count}")

    kperf.disable(pd)
    kperf.close(pd)


def PerfList():
    event_iter = kperf.event_list(kperf.PmuEventType.CORE_EVENT)
    for event in event_iter:
        print(f"event: {event}")


if __name__ == '__main__':
    Counting()
    PerfList()
```

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
