# libkperf

### Description

libkperf is a lightweight performance collection library on linux, that enables developers to perform performance collection in an API fashion. libkperf provides performance data in memory and allows develops to process data directly, reducing overhead of writing and reading perf.data.

### When may I use libkperf?

- Want to collect performance data of system or appliation with low overhead.
- When Analyzing performance of a heavy workload without having a large impact on performance.
- Do not want to manage multiple performance processes and want to parse performance data in a friendly way.
- Collect hotspot with low overhead.
- Want to analyze latency and miss rate of cpu cache.
- Want to trace elapsed time of system calls.
- Want to collect bandwidth of ddr controller.
- Want to collect bandwidth and latency of network.

### Supported CPU Architectures

- Kunpeng

### Supported OS

- openEuler
- OpenCloudOS
- TencentOS
- KylinOS
- CentOS

### Release Notes
v2.1:

- Support SPE Data Source collection.
- Support metric-based profiling.

v2.0:

- Support profile-guided optimization by providing APIs for generating perf.data files.
- Support low-overhead BPF-based collection in Counting mode.
- Support kernel bypass collection in Counting mode.
- Support performance counting and sampling for cgroups.

v1.4:

- Support collection of DDRC bandwidth, PCIe bandwidth, and selected L3C events and SMMU events.
- Support CPU frequency collection.
- Provide blocked sample collection mode.
- Provide Go APIs.

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

### Documents

- [Detailed usage](./docs/Details_Usage.md)
- [C/C++ API reference](./docs/C_C++_API.md)
- [Python API reference](./docs/Python_API.md)
- [Go API reference](./docs/Go_API.md)

### Dependencies

| Dependency | Version requirement |
| --- | --- |
| Minimum GCC / glibc version | `gcc-4.8.5` and `glibc-2.17` |
| Maximum supported GCC / glibc version | `gcc-12.2.0` and `glibc-2.36` |
| Minimum Python version | `python-3.6` |

Notes:

- If the build fails because `numa.h` is missing, install the corresponding `numactl-devel` package first.
- If a CMake error occurs around `Found PythonInterp` during compilation or linking, install the required `python3-devel` package first.

### Quick Start

The following steps start from fetching the source code, then build the basic library, and finally run a minimal C++ example. Python and Go examples are available in the [Other Examples](#other-examples) section.

PMU collection in libkperf usually follows this call sequence:

| API | Description |
| --- | --- |
| `PmuOpen` | Open a PMU device with PID, core ID, event, and other attributes. |
| `PmuEnable` | Start collection. |
| `PmuRead` | Read collected data. |
| `PmuDisable` | Stop collection. |
| `PmuDataFree` | Free `PmuData`. |
| `PmuClose` | Close the PMU device. |

#### 1. Get the source code

```shell
git clone --recurse-submodules https://atomgit.com/openeuler/libkperf.git
cd libkperf
```

#### 2. Build and install the basic C/C++ library

```shell
bash build.sh install_path=/path/to/install
```

If `install_path` is not specified, the files will be installed to the `output` directory under the current directory by default. In the following examples, `/path/to/install` is used to represent the user-specified installation directory. Replace it with the actual path according to your environment.

After the build completes, the installation directory contains headers and libraries:

```text
/path/to/install/
├── include
└── lib
```

If the library is not installed in a default system library path, set `LD_LIBRARY_PATH` before running programs that depend on libkperf:

```shell
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
```

#### 3. Compile and run the example program
Create `example.cpp`: collect PMU counts for a process

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

Build and run:

```shell
g++ -o example example.cpp -I /path/to/install/include -L /path/to/install/lib -lkperf -lsym
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
./example
```

### Build Options

`build.sh` supports `option=value` arguments:

```shell
bash build.sh option=value
```

For example:

```shell
bash build.sh install_path=/path/to/install build_type=debug
```

#### Feature options

| Option | Default value | Description |
| --- | --- | --- |
| `install_path` | `output` in the current directory | Specify the installation path. Build artifacts are installed under this path. |
| `build_type` | `Release` | Specify the build type. Set it to `debug` to build a debug version. |
| `test` | `false` | Whether to build and run libkperf C/C++ test cases. |
| `asan` | `false` | Whether to enable AddressSanitizer to detect memory issues. |
| `bpf` | `false` | Whether to build the BPF collection feature for counting mode. |
| `elf_llvm` | `false` | ELF parsing uses elfin-parser by default. Enable this option to use llvm-symbolizer. |
| `utrace` | `false` | Whether to build `utrace` mode and capstone. |

Examples:

```shell
bash build.sh install_path=/home/test build_type=debug test=true
bash build.sh install_path=/path/to/install asan=true
bash build.sh install_path=/path/to/install utrace=true
```

#### Python package

| Option | Default value | Description |
| --- | --- | --- |
| `python` | `false` | Whether to build the Python package. |
| `python_exe` | System default Python | Specify the interpreter used to install the Python package. |
| `whl` | `false` | Whether to generate a Python `.whl` package. |

**Build and install the Python package**:

```shell
bash build.sh install_path=/path/to/install python=true
```

If multiple Python versions exist in the environment:

```shell
bash build.sh install_path=/path/to/install python=true python_exe=$(which python3)
```

Generate a `.whl` package:

```shell
bash build.sh install_path=/path/to/install python=true whl=true
```

**Uninstall the Python package**:

```shell
python3 -m pip uninstall -y libkperf
```

**Note**: If a Python module reports an error similar to the following:

```text
OSError: /usr/lib/python3.9/site-packages/_libkperf/libsym.so: cannot open shared object file: No such file or directory
```

The installation may have failed because the `setuptools` version is incompatible. Try downgrading it:

```shell
python3 -m pip uninstall setuptools
python3 -m pip install setuptools==58
```

**Run Python test cases**:

```shell
cd python/tests
pytest test_*.py -s -v
```

#### Go package

| Option | Default value | Description |
| --- | --- | --- |
| `go` | `false` | Whether to build the Go package. |

**Build the Go package**:

```shell
bash build.sh install_path=/path/to/install go=true
```

After the command succeeds, copy the entire `go/src/libkperf` directory to `$GOPATH/src/`, where `$GOPATH` is the user project directory.

**Run Go test cases**:

```shell
cd go/src/libkperf_test
export GO111MODULE=off
export LD_LIBRARY_PATH=../libkperf/lib:$LD_LIBRARY_PATH
go test -v # Run all tests.
go test -v -test.run TestCount # Run a specified test case.
```

#### Java

| Option | Default value | Description |
| --- | --- | --- |
| `java_agent` | `false` | Whether to build the `java/java_agent` module, which adds `perf-pid.map` parsing for Java processes. |
| `java_trace` | `false` | Whether to build the `java/java_trace` module, which traces Java programs by using ASM bytecode instrumentation. |

**Build Java Agent**:

```shell
bash build.sh install_path=/path/to/install java_agent=true
```

After the build completes, `libkperfmap.so` is generated in the `lib` subdirectory of the installation path. Just set the `LD_LIBRARY_PATH` of this directory.
**Build Java Trace**:

```shell
bash build.sh java_trace=true
```

Before using this option, make sure Java environment variables are correctly configured and Gradle or Maven is installed.

### Other Examples

#### Python: read PMU counts

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

Run:

```bash
python example.py
```

#### Go: read PMU counts

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

Build and run:

```shell
go build example.go
./example
```

Static build:

```shell
go build -tags="static" example.go
```

Or run directly:

```shell
go run example.go
```

#### C++: sample a process and resolve call stacks

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

Build and run:

```shell
g++ -o sample sample.cpp -I /path/to/install/include -L /path/to/install/lib -lkperf -lsym
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
./sample
```

### FAQ
#### 1、Q: How to correctly use launch app mode for process profiling?
  * After PmuOpen, use a signal to wake up the child process to invoke the application
  * No need to call PmuEnable
  * Recommended to use a single fd for opening, which can significantly reduce profiling overhead in multi-threaded scenarios
  * Reference document: [Detailed Usage Guide](./docs/Details_Usage.md#profiling-processes-via-enableexecon)

#### 2、Q: Why does data loss occur when profiling multi-threaded applications?
  * Cause: Operations such as PmuOpen/PmuEnable/PmuDisable have timing differences in loading and enabling each thread when profiling multi-threaded applications
  * Current solutions:
    * Use launch mode (single fd opening, verified to improve PmuOpen efficiency) — [Detailed Usage Guide](./docs/Details_Usage.md#profiling-processes-via-enableexecon)
    * Use --per-thread mode (number of fds = number of events × number of threads) to reduce core-level overhead — see reference

#### 3、Q: How to improve symbol resolution speed, especially in stress testing scenarios?
  * Problem: Original DWARF resolution takes >1 second, impacting performance
  * Optimized solutions:
    * Integrated llvm-symbolizer, currently improves line number resolution efficiency by 30x
    * Supports configurable mode: using RESOLVE_ELF mode in symbolMode will skip source file and line number resolution

#### 4、Q: How to support Cgroup profiling? What are the limitations?
  * Limitations:
    * Uncore and core events require two separate PmuOpen calls
    * PA events cannot be mixed with core events in a single PmuOpen when profiling a process
  * Suggestion: Keep events separated during profiling to avoid incorrect merging

#### 5、Q: Why does PmuRead take a long time during SPE profiling? How to optimize?
  * Cause: In SPE mode, PmuRead automatically calls PmuDisable, and after reading data, automatically calls PmuEnable
  * Suggestions:
    * Execute PmuOpen/PmuCollect/PmuClose sequentially in a single thread

#### 6、Q: How to profile HITM events (False Sharing) and ensure data stability?
  * Profiling method: Specify a CPU core for profiling
  * Stability:
    * Profiling by specifying a CPU core: data is stable, addresses are correct
    * Profiling by specifying a PID: inconsistencies may occur; using CPU-core profiling is recommended