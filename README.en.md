# libkperf

#### Description
Implement a low overhead pmu collection library, providing abstract interfaces for counting, sampling and symbol resolve.

#### Software Architecture
This repo includes two modules: pmu collections and symbol resolve.

Pmu collection module is developed on syscall perf_event_open to enable kernel pmu counting and sampling, using -thread or per-core mode depending on user input.  
Pmu data packets are read from ring buffer and are parsed to different structure for counting, sampling and spe sampling.  
For sampling, symbols are resolved according to ips or pc from data packet. Each symbol contains symbol name, address, source file path and line number if possible.  

Symbol resolve module is developed on elfin-parser, a library for parsing elf and dwarf. The module manages all symbol data in well-designed data structures for fast query.

#### Installation
Run bash script:

```sh
sh build.sh
```
Headers and libraries will be installed to ./output directory.

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

Here are some examples:
* Get pmu count for a process.
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
