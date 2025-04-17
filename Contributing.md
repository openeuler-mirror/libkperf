### Issue
欢迎对libkperf做反馈，提出改进意见，记录相关问题。
- https://gitee.com/openeuler/libkperf/issues

### 改进文档
文档在docs/目录下，如果希望改进文档，可以提交PR。

### 开发者相关
- libkperf需要保持较高的兼容性，建议使用gcc 4来编译工程，请基于C++11来开发。
- 如果要编译调试版，可以用编译命令```bash build.sh build_type=debug```. 在调试时，可以设置环境变量PERF_DEBUG=1，用于打印调试信息。
- 如果要编译并运行UT用例，可以用编译命令```bash build.sh test=True```. UT用例中需要执行SPE采样，如果环境不支持SPE，那么这些用例会失败。鲲鹏上配置SPE的方法参考：https://www.hikunpeng.com/document/detail/zh/kunpengdevps/userguide/usermanual/kunpengoper_06_0010.html
- 提交PR时，请描述问题、原因、方法，以便后续跟踪问题和特性。请在提交前通过UT用例测试。

### Issue
Feedback on libkperf is welcome.
- https://gitee.com/openeuler/libkperf/issues

### Documentation
The document is in the docs/ directory, if you want to improve the documentation, please submit a PR.

### Developer
- libkperf needs to maintain high compatibility, it is recommended to use gcc 4 to compile the project, please use C++11 to develop it.
- If you want to compile the debug version, you can use the build command ```bash build.sh buildType=debug```. For debug logging, set environment variable PERF_DEBUG=1.
- If you want to compile and run your UT use case, you can use the compilation command ```bash build.sh test=True```. SPE sampling is required in UT use cases, and if the environment does not support SPE, then these use cases will fail. For details about how to configure SPE on Kunpeng, see https://www.hikunpeng.com/document/detail/zh/kunpengdevps/userguide/usermanual/kunpengoper_06_0010.html
- When submitting a PR, please describe the problem, cause, and method so that you can track the problem and feature later. Please run all unit tests successfully before submitting.