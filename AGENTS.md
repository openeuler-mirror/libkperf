# AGENTS.md

libkperf: lightweight Linux PMU sampling + symbol-resolution library (C/C++ core,
with Python/Go/Java bindings). Tuned for Kunpeng/AArch64; PMU event + SPE tests only
pass on Kunpeng hardware with SPE enabled.

## Build

Everything goes through `build.sh` with `option=value` flags (CMake is driven by it;
do not invoke cmake directly unless reproducing a step):

```shell
git clone --recurse-submodules ...   # elfin-parser, googletest, libbpf, capstone are submodules
bash build.sh install_path=/path/to/install   # default install_path is ./output
```

- `bash build.sh` ALWAYS rebuilds `llvm-symbolizer` (a full LLVM checkout) and
  `elfin-parser` from scratch (`rm -rf` their build dirs). Expect a very long build;
  this is not incremental. There is no fast "rebuild only libkperf" path via build.sh.
- Option values are compared to the lowercase string `true`. Use `test=true`,
  `python=true`, `go=true`, `bpf=true`, `asan=true`, `elf_llvm=true`, `utrace=true`,
  `java_agent=true`, `java_trace=true`. `test=True` (as seen in Contributing.md) does
  NOT enable tests. `build_type=debug` for a debug build.
- `bpf=true` requires `clang` + `bpftool` and a BTF kernel
  (`/sys/kernel/btf/vmlinux`; `build_skel_files` dumps vmlinux.h and generates
  `pmu/bpf/*.skel.h`).
- `java_trace=true` requires JDK + Maven or Gradle on PATH.
- System deps: `numactl-devel` (numa.h) and `python3-devel` (CMake FindPythonInterp).

## Code layout (non-obvious)

- Two link targets: `libkperf` and `libsym`. Link both: `-lkperf -lsym`.
- `pmu/CMakeLists.txt` globs sources from `pmu/`, `util/`, `symbol/`, and `pmu/{pfm,
  trace,decoder,bpf,lbr_driver,proc}` into `kperf`. `symbol/` builds `libsym`.
- `util/CMakeLists.txt` (target `profu`) is NOT referenced by the top-level CMake —
  util `.cpp` files are globbed directly into `kperf`/`sym`. Editing util/CMakeLists
  has no effect on a normal build.
- `add_subdirectory()` order (root CMakeLists): symbol → pmu → [java_agent] →
  [python] → [go] → [test]. Optional dirs are gated by the flags above.

## Language / compatibility constraints

- Core library MUST stay C++11 (high compat, gcc-4.8.5 minimum). C++14 is enabled ONLY
  for the `test/` and googletest build. Do not introduce C++14/17 features in
  `pmu/`, `symbol/`, `util/`, `include/`.
- Global warnings are suppressed (`add_compile_options(-w)` in Common.cmake), so the
  compiler will not flag issues you may be relying on — be careful manually.

## Code style constraints

- 不要过度设计：按当前需求实现最简方案，不预留框架层/抽象层/配置项。
- 精简代码：删除冗余中间变量、重复分支、无用封装；能一个函数做完的不拆两个。
- 不要过度校验：只在信任边界（用户 API 入参、外部数据）校验，内部函数间不重复检查。
- 每行不超过 120 字符。

## Tests

`bash build.sh test=true` builds AND runs tests via `build/common.sh:execute_binary`,
which runs `_build/test/test_perf/test_perf` and `_build/test/test_symbol/test_symbol`.
SPE-dependent cases fail without Kunpeng SPE support.

Run one case after a `test=true` build:
```shell
export LD_LIBRARY_PATH=/path/to/install/lib:$LD_LIBRARY_PATH
./_build/test/test_perf/test_perf   --gtest_filter=Suite.Case
./_build/test/test_symbol/test_symbol --gtest_filter=Suite.Case
```
Debug logging during runs: `PERF_DEBUG=1` (debug build).

Python: `cd python/tests && pytest test_*.py -s -v` (build with `python=true`).
Go: `cd go/src/libkperf_test && export GO111MODULE=off && export
LD_LIBRARY_PATH=../libkperf/lib:$LD_LIBRARY_PATH && go test -v -test.run TestCount`.

## Notes

- Runtime needs `LD_LIBRARY_PATH=<install>/lib` unless installed to a default path.
- Docs live in `docs/` (Details_Usage, C_C++_API, Python_API, Go_API); PRs go through
  Gitee/AtomGit with the template in `.gitee/` — describe problem, cause, method, and
  pass all UT before submitting.
