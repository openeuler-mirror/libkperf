# libkperf

#### Description
libkperf is a lightweight performance collection library on linux, that enables developers to perform performance collection in an API fashion. libkperf provides performance data in memory and allows develops to process data directly, reducing overhead of writing and reading perf.data.

#### Supported CPU Architectures
- Kunpeng

#### Supported OS
- openEuler
- OpenCloudOS
- TencentOS
- KylinOS
- CentOS

#### Build
Minimum required GCC version:
- gcc-4.8.5 and glibc-2.17.

Minimum required Python version:
- python-3.7.

To build a library with C API:
```shell
git clone --recurse-submodules https://gitee.com/openeuler/libkperf.git
cd libkperf
bash build.sh install_path=/path/to/install
```
Note:

- If the compilation error message indicates that numa.h file is missing, you need to first install the corresponding numactl-devel package.

To build a library with debug version:
```shell
bash build.sh install_path=/path/to/install buildType=debug
```

To build a python package:
```shell
bash build.sh install_path=/path/to/install python=true
```

To uninstall python package:
```shell
python3 -m pip uninstall -y libkperf
```

#### Documents
Refer to ```docs``` directory for detailed docs:
- [Detailed usage](./docs/Details.md)

#### Instructions
All pmu functions are accomplished by the following interfaces:
* PmuOpen  
	Input pid, core id and event and Open pmu device.
* PmuEnable  
	Start collection.
* PmuRead  
	Read collection data.
* PmuDisable  
	Stop collection.
* PmuClose  
	Close pmu device.

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

Python examples:
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
        for data in data_iter.iter:
            evtMap[data.evt] += data.count

        for evt, count in evtMap.items():
            print(f"event: {evt} count: {count}")

    kperf.disable(pd)
    kperf.close(pd)
```
