# libkperf tools

## 简介
本目录存放libkperf相关示例工具。  
主要功能包括：
- cache_collect: 收集程序的L2 Cache信息
- false_sharing: 通过SPE采样的data source信息定位伪共享问题
- pmu_hotspot: IO和计算热点混合采样(Blocked Sample)
- lbr_driver: 通过驱动方式采集lbr
- perf_data: 生成采样模式下的perf.data文件

## 目录结构
```
libkperf/tools
├── cache_collect/
├── pmu_hotspot/ # C++、python、go示例代码和编译运行脚本
├── lbr_driver/
├── pmu_datasrc.cpp
├── pmu_perfdata.cpp
├── case/ # 示例demo
├── bin/ # 编译产物输出目录（自动生成）
│ └── case # 示例demo输出目录
├── build_tools.sh # 构建脚本
├── README.md
└── CMakeLists.txt # 顶层CMake入口
```

## 编译
1. 编译libkperf
```bash
bash build.sh
```

2. 项目提供统一脚本`build_tools.sh`进行全量编译，命令如下：
```bash
cd tools
./build_tools.sh
```
如只需要编译某个工具，添加名称：
```bash
./build_tools.sh pmu_datasrc
```

所有生成的可执行文件会输出到 `tools/bin`目录

- lbr_driver：该工具对硬件与内核版本有特定要求，仅支持在指定环境使用，因此不参与统一编译。
- case文件夹下所有示例demo默认全部参与编译
