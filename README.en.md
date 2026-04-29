# libkperf

#### Description

libkperf is a lightweight performance collection library on linux, that enables developers to perform performance collection in an API fashion. libkperf provides performance data in memory and allows develops to process data directly, reducing overhead of writing and reading perf.data.

#### When may I use libkperf?

- Want to collect performance data of system or appliation with low overhead.
- When Analyzing performance of a heavy workload without having a large impact on performance.
- Do not want to manage multiple performance processes and want to parse performance data in a friendly way.
- Collect hotspot with low overhead.
- Want to analyze latency and miss rate of cpu cache.
- Want to trace elapsed time of system calls.
- Want to collect bandwidth of ddr controller.
- Want to collect bandwidth and latency of network.

#### Supported CPU Architectures

- Kunpeng

#### Supported OS

- openEuler
- OpenCloudOS
- TencentOS
- KylinOS
- CentOS

#### Release Notes

v1.3:

- Support elapsed time collection of system calls.
- Support branch record sampling.

v1.2:

- Support tracepoint.
- Support group event collection.

v1.1:

- Support python API.

v1.0:

- Support performance counting, sampling and SPE sampling.
- Support core and uncore events.
- Support symbol analysis.

#### Build

Minimum required GCC version:

- gcc-4.8.5 and glibc-2.17.

Maximum supported GCC version:

- gcc-12.2.0 and glibc-2.36.

Minimum required Python version:

- python-3.6.

To build a library with C API:

```shell
git clone --recurse-submodules https://atomgit.com/openeuler/libkperf.git
cd libkperf
bash build.sh install_path=/path/to/install
```

Note:

- If the compilation error message indicates that numa.h file is missing, you need to first install the corresponding numactl-devel package.
- If you encounter a CMake error related to 'Found PythonInterp' during compilation and linking, you need to first install the required python3-devel package.

To build a library with debug version:

```shell
bash build.sh install_path=/path/to/install build_type=debug
```

To build a python package:

```shell
bash build.sh install_path=/path/to/install python=true
```

If the environment contains multiple Python versions, you need to specify the Python interpreter to be installed
```shell
bash build.sh python=true python_exe=$(which python3)
```

To uninstall python package:

```shell
python3 -m pip uninstall -y libkperf
```

If a Python module runtime error similar to the following is reported:
OSERROR: /usr/lib/python3.9/site-packages/_libkperf/libsym.so: cannot open shared object file: No such file or directory
The Python installation fails due to an incompatible setuptool version. This can be resolved by downgrading.
```shell
python3 -m pip uninstall setuptools
python3 -m pip install setuptools=58
```

TO build a Go package:

```shell
bash build.sh go=true
```
After the command is executed successfully, copy go/src/libkperf to GOPATH/src. GOPATH indicates the user project directory.

#### Documents

Refer to ```docs``` directory for detailed docs:

- [Detailed usage](./docs/Details_Usage.md)

Refer to ```docs``` directory for python API specification docs:

- [Python API specification](./docs/Python_API.md)

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

* Get pmu count for a process

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
        PmuData *d = &data[i];
        std::cout << "evt=" << d->evt << " count=" << d->count << std::endl;
    }
    // To free PmuData, call PmuDataFree.
    PmuDataFree(data);
    // Like fd, call PmuClose if pd will not be used.
    PmuClose(pd);
}

```

* Sample a process

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
                 Symbol *data = stack->symbol;
                std::cout << std::hex << data->addr << " " << data->symbolName << "+0x" << data->offset << " "
                          << data->codeMapAddr << " (" << data->module << ")"
                          << " (" << std::dec << data->fileName << ":" << data->lineNum << ")" << std::endl;
            }
            stack = stack->next;
        }
    }
    // To free PmuData, call PmuDataFree.
    PmuDataFree(data);
    // Like fd, call PmuClose if pd will not be used.
    PmuClose(pd);
}

```

* Python examples
```python
import time
from collections import defaultdict

import kperf

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

* Go example
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

#### Quick Run Reference for Example Code:

* **For C++ Example Code:**
  You can place the sample code into the main function of a C++ source file, and include the header files related to this dynamic library (#include "symbol.h", #include "pmu.h", #include "pcerrc.h"). Then, use g++ to compile and link this dynamic library to generate an executable file that can be run.

Compilation Command Reference:

```bash
g++ -o example example.cpp -I /install_path/include -L /install_path/lib -lkperf -lsym
```

* **For Python Example Code:**
  You can place the sample code into the main function of a Python source file, and import the packages related to this dynamic library (import kperf, import ksym). Running the Python file will then utilize the functionalities provided by these packages.

Run Command Reference:

```bash
python example.py
```

* **For Go example Code:**
  You can directly go to the go/src/libkperf_test directory.

```shell
export GO111MODULE=off
export LD_LIBRARY_PATH=../libkperf/lib:$LD_LIBRARY_PATH
go test -v # run all
go test -v -test.run TestCount #specify the test case to run
```

* **GO language static mode compilation:**
```shell
go build -tags="static"
```

If the dynamic library is not installed in the system default directory '/usr/bin', an error will occur at runtime: No such file or directory of 'libkperf.so'.
In this case, You need to add the directory of 'libkperf.so' to the 'LD_LIBRARY_PATH' environment variable:
```shell
export LD_LIBRARY_PATH=/XXX/libkperf/output/lib:$LD_LIBRARY_PATH
```

#### FAQ
##### 1、Q: How to correctly use launch app mode for process profiling?
  * After PmuOpen, use a signal to wake up the child process to invoke the application
  * No need to call PmuEnable
  * Recommended to use a single fd for opening, which can significantly reduce profiling overhead in multi-threaded scenarios
  * Reference document: [Detailed Usage Guide](./docs/Details_Usage.md#profiling-processes-via-enableexecon)

##### 2、Q: Why does data loss occur when profiling multi-threaded applications?
  * Cause: Operations such as PmuOpen/PmuEnable/PmuDisable have timing differences in loading and enabling each thread when profiling multi-threaded applications
  * Current solutions:
    * Use launch mode (single fd opening, verified to improve PmuOpen efficiency) — [Detailed Usage Guide](./docs/Details_Usage.md#profiling-processes-via-enableexecon)
    * Use --per-thread mode (number of fds = number of events × number of threads) to reduce core-level overhead — see reference

##### 3、Q: How to improve symbol resolution speed, especially in stress testing scenarios?
  * Problem: Original DWARF resolution takes >1 second, impacting performance
  * Optimized solutions:
    * Integrated llvm-symbolizer, currently improves line number resolution efficiency by 30x
    * Supports configurable mode: using RESOLVE_ELF mode in symbolMode will skip source file and line number resolution

##### 4、Q: How to support Cgroup profiling? What are the limitations?
  * Limitations:
    * Uncore and core events require two separate PmuOpen calls
    * PA events cannot be mixed with core events in a single PmuOpen when profiling a process
  * Suggestion: Keep events separated during profiling to avoid incorrect merging

##### 5、Q: Why does PmuRead take a long time during SPE profiling? How to optimize?
  * Cause: In SPE mode, PmuRead automatically calls PmuDisable, and after reading data, automatically calls PmuEnable
  * Suggestions:
    * Execute PmuOpen/PmuCollect/PmuClose sequentially in a single thread

##### 6、Q: How to profile HITM events (False Sharing) and ensure data stability?
  * Profiling method: Specify a CPU core for profiling
  * Stability:
    * Profiling by specifying a CPU core: data is stable, addresses are correct
    * Profiling by specifying a PID: inconsistencies may occur; using CPU-core profiling is recommended