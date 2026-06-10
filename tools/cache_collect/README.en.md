# Cache Collect Tool
### Description
This tool is designed to collect L1I/L2I cache and L1D/L2D cache information of program.

1. It generates statistics of hotspots from the function and instruction perspectives. 

2. It reports the L1I cache miss ratio, L1D cache miss ratio, L2I cache miss ratio, L2D cache miss ratio, and IPC (Instructions Per Cycle) for each process.

In L2I cache collect mode, the corresponding data can be output as a txt file for BOLT in the work directory.
```
Format: 1 [funcName] [offset] [number]
```
[number] is the sample count for [funcName] at [offset]

### Build
Build the libkperf tool first:
```
bash build.sh
```

then:
```
cd tools
bash build_tools.sh cache_collect
```

### Run
Use './cache_collect --help/-h' to view the help information.

```
  Usage: ./cache_collect --pid/-p <pid> [options]

  Required:
    --pid/-p <pid>           : Target process ID(s). Multiple IDs can be separated by ','
  Optional:
    --duration/-d <seconds>  : Set collection time of hotspots. Unit: s, default: 10
    --level/-l <level>       : Set to 'inst' for instruction-level summary. Default: function-level summary
    --mode/-m <mode>         : Set to 'dcache' to collect L1D/L2D cache data. Default: L1I/L2I cache data
    --sort/-o <sort>         : Sort hotspot table by 'cycles', 'l1', or 'l2'. Default: cycles
    --interval/-i <ms>       : Interval for reading the ring buffer. Unit: ms, default: 1000
    --frequency/-f <freq>    : Sampling frequency, default: 1000
    --bolt/-b <option>       : Generate BOLT format output file. Options: 'cycles', 'l2i_cache', 'l2i_cache_refill', or 'all'. Only for default mode.
    --summary/-s <seconds>   : Set collection time of summary ratio and IPC collection. Unit: s, default: 5
  Examples:
    ./cache_collect -p 125785 -d 10 -l inst -m dcache -o l1 -i 2000
    ./cache_collect -p 125785,143789 -m dcache -f 4000 -b cycles
```

### Example
pwd: /home/test/libkperf/tools/cache_collect/build
#### The function level:
command:
```
./cache_collect -p 1630278 -b all
```

The output results is:
```
==================================================================================================================================================================================================================
                                                                    HOTSPOT FUNC
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Function                                          Pid         Start Addr        End Addr          Length            l1 icache refill    l1 icache      l2 icache refill    l2 icache      Cycles         Ratio(%)
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
main.addSub                                       1630278     244e20            244f30            110               76                  144             22                 2293           13820058736    52.65
main.addSubFuse                                   1630278     244f30            245030            100               7921                2586            5                  15             12423196469    47.33
gogo                                              1630278     7b0e0             7b120             40                1428                1017            1234               32             2575901        0.01
runtime.sighandler                                1630278     5fa80             60070             5f0               56                  1741            56                 213            2568366        0.01
runtime.retake                                    1630278     572e0             575b0             2d0               0                   0               47                 738            433392         0.00
runtime.unlock2                                   1630278     1c580             1c650             d0                780                 5707            1                  31             296167         0.00
runtime.nanotime1.abi0                            1630278     7f3c0             7f4a0             e0                306                 4522            0                  1              3155           0.00
runtime.usleep.abi0                               1630278     7f150             7f1b0             60                0                   31              2                  0              34             0.00
runtime.sysmon                                    1630278     56e30             572e0             4b0               33                  1               755                0              30             0.00
....
___________________________________________________________________________________________________________________________________________________________________________________________________________________
Bolt file: /home/test/libkperf/tools/cache_collect/build/1630278_20251106_160118_cycles.txt
Bolt file: /home/test/libkperf/tools/cache_collect/build/1630278_20251106_160118_l2i_cache_refill.txt
Bolt file: /home/test/libkperf/tools/cache_collect/build/1630278_20251106_160118_l2i_cache.txt
====================================================================================================================================================================================================================
SUMMARY
-------------------------------------------------------------------------------------------------------------------------
Pid             l1 icache Miss Rate      l1 dcache Miss Rate      l2 icache Miss Rate      l2 dcache Miss Rate       IPC
-------------------------------------------------------------------------------------------------------------------------
1630278                      59.60%                   49.89%                     5.7%                    0.08%       2.71
-------------------------------------------------------------------------------------------------------------------------
```

The content of '1630278_20251106_160118_cycles.txt' is:
```
no_lbr cycles:
1 main.addSub/1 20 5352
1 main.addSubFuse/1 20 4813
1 gogo/1 20 1 1 runtime.sighandler/1 30 1
1 runtime.retake/1 7c 2
1 runtime.unlock2/1 30 1
1 runtime.nanotime1.abi0/1 98 3
1 runtime.usleep.abi0/1 48 4
1 runtime.sysmon/1 e4 2
```

#### The instruction level:
command:
```
./cache_collect -p 1630278 -l inst -b cycles
```

The output results is:
```
===================================================================================================================================================================================
                                                                    HOTSPOT INST
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Addr                FuncName                                          Pid            l1 icache refill    l1 icache      l2 icache refill    l2 icache      Cycles         Ratio(%)
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
244f50              main.addSubFuse                                   1630278        1004                1699           27                  581            4574485132     17.49
244e98              main.addSub                                       1630278        50                  0              8                   337            2237117248     8.55
244e50              main.addSub                                       1630278        0                   90             10                  443            2157851199     8.25
244e40              main.addSub                                       1630278        0                   0              3                   65             1647745701     6.30
244edc              main.addSub                                       1630278        0                   132            5                   125            1616315405     6.18
244eb0              main.addSub                                       1630278        37                  0              3                   86             1483391344     5.67
244fa4              main.addSubFuse                                   1630278        0                   64             1                   15             1343424392     5.14
244fe8              main.addSubFuse                                   1630278        0                   0              0                   30             1115313116     4.26
244fac              main.addSubFuse                                   1630278        1                   0              2                   16             900182732      3.44
244fc4              main.addSubFuse                                   1630278        42                  553            0                   1              755279597      2.89
244f88              main.addSubFuse                                   1630278        60                  1357           0                   19             714211321      2.73
244f6c              main.addSubFuse                                   1630278        93                  131            3                   15             639790055      2.45
244e74              main.addSub                                       1630278        0                   0              0                   0              565132676      2.16
...
___________________________________________________________________________________________________________________________________________________________________________________
Bolt file: /home/test/libkperf/tools/cache_collect/build/1630278_20251106_161435_cycles.txt
...
```

The content of bolt file is :
```
no_lbr cycles:
1 main.addSubFuse/1 20 1768
1 main.addSub/1 78 869
1 main.addSub/1 30 838
1 main.addSub/1 20 641
1 main.addSub/1 bc 629
1 main.addSub/1 90 576
1 main.addSubFuse/1 74 500 
1 main.addSubFuse/1 b8 433
...
```
