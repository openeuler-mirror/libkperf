# libkperf

#### Description
Implement a low overhead pmu collection library, providing abstract interfaces for counting, sampling and symbol resolve.

#### Software Architecture
This repo includes two modules: pmu collections and symbol resolve.

Pmu collection module is developed on syscall perf_event_open to enable kernel pmu counting and sampling, using -thread or per-core mode depending on user input.  
Pmu data packets are read from ring buffer and are parsed to different structure for counting, sampling and spe sampling.  
For sampling, symbols are resolved according to ips or pc from data packet. Each symbol contains symbol name, address, source file path and line number if possible.  

Symbol resolve module is developed on elfin-parser, a library for parsing elf and dwarf. The module manages all symbol data in well-designed data structures for fast query.

#### Download

Git method:

```shell
git clone --recurse-submodules https://gitee.com/openeuler/libkperf.git
```
If you only use
```shell
git clone https://gitee.com/openeuler/libkperf.git
```
Please continue with the execution
```shell
cd libkperf
git submodule update --init --recursive
```

When unable to use git:

1. Download the libkperf compressed file and decompress it.

2. Go to the third_party directory of libkperf on Gitee, click on the link(as shown in the example elfin-parser@13e57e2 Click on the submit ID after @), to redirect and download the compressed package of the third-party library. After decompression, place it in the third_party directory of the local libkperf project. (elfin Parser is necessary for installation)

#### Installation
Run bash script:

```sh
bash build.sh install_path=/home/libkperf
```
As mentioned above, the header and library will be installed in the/home/libkperf output directory, and installPath is an optional parameter. If not set, it will be installed in the output directory under libkperf by default.

If you want to add additional python library support, you can install it as follows:
```shell
bash build.sh python=true
```

If you need to uninstall the python library after installation, you can run the following command:
```shell
python -m pip uninstall -y libkperf
```

#### Instructions
All pmu functions are accomplished by the following interfaces:
* PmuOpen  
	Input pid, core id and event and Open pmu device.
* PmuEnable  
	Start collection.
* PmuRead  
	Read pmu data and a list is returned.
* PmuDisable  
	Stop collection.
* PmuClose  
	Close pmu device.

Refer to pmu.h for details of interfaces.

Here are some examples:
* Get pmu count for a process.
```C
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

* Sample a process
```C
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
// Use SAMPLING for sample task.
int pd = PmuOpen(SAMPLING, &attr);
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
    // Get an element from array.
	PmuData *d = &data[i];
    // Get stack object which is a linked list.
    Stack *stack = d->stack;
    while (stack) {
        // Get symbol object.
        if (stack->symbol) {
            ...
        }
        stack = stack->next;
    }
}
// To free PmuData, call PmuDataFree.
PmuDataFree(data);
// Like fd, call PmuClose if pd will not be used.
PmuClose(pd);
```
* config event group function 
```C
int pidList[1];
pidList[0] = pid;
unsigned numEvt = 16;
char *evtList[numEvt] = {"r3", "r4", "r1", "r14", "r10", "r12", "r5", "r25",
                        "r2", "r26", "r2d", "r17", "r8", "r22", "r24", "r11"};
// initialize event list, the same group id is the same event group.
// if event grouping is not required, leave the event group_id list blank.
// In addition, if group_id is -1, the event group function is forcibly disabled.
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = numEvt;
attr.pidList = pidList;
attr.numPid = 1;
struct EvttAttr groupId[numEvt] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13};
attr.evtAttr = groupId;
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

- Counting supports fork thread.
```C
int pidList[1];
pidList[0] = pid;
unsigned numEvt = 1;
char *evtList[numEvt] = {"cycles"};
PmuAttr attr = {0};
attr.evtList = evtList;
attr.numEvt = numEvt;
attr.pidList = pidList;
attr.numPid = 1;
// In count mode, enable it you can get the new child thread count, default is disabled.
attr.includeNewFork = 1;
// Call PmuOpen and pmu descriptor <pd> is return.
// <pd> is an identity for current task.
int pd = PmuOpen(COUNTING, &attr);
// Start collection.
PmuEnable(pd);
// Collect for two second.
sleep(2);
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

Python examples:
```python
import time
from collections import defaultdict
import subprocess

import kperf

def Counting():
    evtList = ["r11", "cycles"]
    evtAttr = [2, 2] # Event group id list corresponding to the event list. the same group id is the same event group.
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


def NewFork():
    # test_new_fork demo in test_perf, you can find test_new_fork.cpp
    p=subprocess.Popen(['test_new_fork']);
    pidList=[p.pid]
    evtList=["cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList, includeNewFork=True, pidList=pidList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(kperf.error())
        return
    kperf.enable(pd)
    time.sleep(4)
    pmu_data = kperf.read(pd)
    for data in pmu_data.iter:
        print(f"evt:{data.evt} count:{data.count} tid:{data.tid} pid:{data.pid}")
    kperf.disable(pd)
    kperf.close(pd)


def PerfList():
    event_iter = kperf.event_list(kperf.PmuEventType.CORE_EVENT)
    for event in event_iter:
        print(f"event: {event}")


if __name__ == '__main__':
    Counting()
    PerfList()
    NewFork()
```


#### Contribution

1.  Fork the repository
2.  Create Feat_xxx branch
3.  Commit your code
4.  Create Pull Request


#### Gitee Feature

1.  You can use Readme\_XXX.md to support different languages, such as Readme\_en.md, Readme\_zh.md
2.  Gitee blog [blog.gitee.com](https://blog.gitee.com)
3.  Explore open source project [https://gitee.com/explore](https://gitee.com/explore)
4.  The most valuable open source project [GVP](https://gitee.com/gvp)
5.  The manual of Gitee [https://gitee.com/help](https://gitee.com/help)
6.  The most popular members  [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
