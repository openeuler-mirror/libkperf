# ddr_bandwidth

`ddr_bandwidth` 用于统计目标进程的函数级 DDR 带宽。

工具结合三类硬件数据：

* **DDRC PMU**：采集系统实际的物理 DRAM 读写流量。
* **PCIe PMU**：采集 PCIe DMA 流量，并从 DDRC 流量中扣除。
* **Arm SPE**：采集 system-wide 内存访问样本。Read 使用完整 system-wide 样本作为分母；Write 在目标 PID 内部归一化。

## 基本原理

DDRC/PCIe 归因窗口默认是 **100 ms**，Arm SPE 默认每 **50 ms** 读取一次。`--spe-read-interval` 可单独设置 SPE 读取间隔；实际间隔上限为 `--interval`，每个归因窗口边界都会读取 SPE。窗口内的多批 SPE 数据累计后参与归因。

系统剩余 DDR 流量计算为（包含其他进程和内核流量，不能直接等同于目标进程流量）：

```text
Residual Read  = max(0, DDRC Read  - PCIe DMA Read)
Residual Write = max(0, DDRC Write - PCIe DMA Write)
```

解析为 `[kernel]` 的 SPE 记录会按时间戳进入 system-wide 统计。Kernel Read 是 residual Read 的互斥分项；Kernel Write 仅是基于 system-wide store 比例的重叠观测值，不会从 Target Write 或 Other Write 中扣除。它们不会进入 Write 的目标 PID 分母、函数表、进程表或 store-PC 调试表，也不会按样本携带的 PID 强行归属给某个进程。

### Read 归因

Read 侧只使用 SPE 中数据来源为：

```text
HIP_LOCAL_MEM
HIP_REMOTE_MEM
```

的 load sample。

每个 DRAM load sample 权重相同：

```text
函数 Read Bytes
    = CPU Read Bytes
      × 函数 DRAM load sample 数
      / 窗口内 system-wide（用户态 + 内核态）DRAM load sample 数
```

### Write 归因

Write 侧根据 SPE store sample 数进行估算。考虑 store retirement 到 dirty cache line 写回 DDR 的延迟，窗口 `i` 使用当前窗口以及前两个 SPE 有效窗口（`i-2`、`i-1`、`i`）的 store sample；每个样本使用相同权重：

```text
函数 Write Bytes[i]
    = CPU Write Bytes[i]
      × sum(函数 store samples[i-2..i])
      / sum(目标 PID store samples[i-2..i])
```

第 0、1 个窗口只使用当时已经存在的窗口。历史样本仅作为当前 DDRC Write residual 的归因权重；每个窗口的 physical write residual 仍只分配一次。

当三窗口目标 PID store samples 达到 `--min-samples` 时，Target Write 直接取该窗口的全部 Residual Write；各函数再按其占目标 PID store samples 的比例分配。

Write 归因不再使用 store 指令访问宽度作为权重。

由于 CPU store、cache dirty eviction 和最终 DRAM writeback 并非严格一一对应，且不同 store 指令的 SPE 采样密度可能存在差异，因此 Write BW 仍作为估算结果。

## 使用方法

### 构建

```bash
bash build.sh

cd tools
bash build_tools.sh ddr_bandwidth
```

生成文件：

```text
tools/bin/ddr_bandwidth
```

### 采集运行中的进程

单进程：

```bash
sudo tools/bin/ddr_bandwidth -p <PID> -d 10
```

例如：

```bash
sudo tools/bin/ddr_bandwidth -p 12345 -d 10
```

多进程可使用逗号分隔：

```bash
sudo tools/bin/ddr_bandwidth -p 12345,12346 -d 10
```

也可以重复指定：

```bash
sudo tools/bin/ddr_bandwidth -p 12345 -p 12346 -d 10
```

### 拉起程序并采集

```bash
sudo tools/bin/ddr_bandwidth -d 10 -- ./my_app
```

### 指定 PCIe BDF

为了减少多个 PCIe BDF 共享 PMU counter 导致的 multiplex，需要精确 DMA 扣除时可显式指定 BDF：

```bash
sudo tools/bin/ddr_bandwidth \
    --pcie-bdf 21:00.0 \
    -p 12345 \
    -d 10
```

常用参数：

```text
-p, --pid PID          目标进程 PID，可指定多个
-d, --duration SEC     采集时间，默认 10 秒
-i, --interval MS      DDRC 归因窗口，默认 100 ms
    --spe-read-interval MS  SPE 读取间隔，默认 50 ms，不超过归因窗口
    --min-samples N    每方向最低有效样本数，默认 1；Read 看当前窗口，Write 看当前加前两窗口
    --period N         SPE sample period，默认 2048，不是毫秒
    --min-latency N    SPE minimum latency
    --top N            输出 Top N 函数
    --pcie-bdf BDF     指定 PCIe BDF，可重复
    --debug            输出 timing、逐窗口归因、SPE 配置/event/health 和 store-PC 调试信息
```

## 输出结果说明

函数级结果示例：

```text
CPU DDR BANDWIDTH BY USER FUNCTION

PID         Function              Read BW     Write BW(est*)   Total BW    Read samples   Store samples
                                  (MiB/s)       (MiB/s)        (MiB/s)

12345       ReadData                850.00              -        850.00      68000              0
12345       WriteData                    -         420.00        420.00          0          35000
```

字段含义：

* **PID**：该用户态函数样本所属的目标进程；函数名不再拼接 PID 前缀。
* **Read BW**：归因到该函数的物理 DDR Read 带宽。
* **Write BW(est*)**：根据 SPE store sample 数估算的 DDR Write 带宽。
* **Total BW**：Read BW + Write BW。
* **Read samples / Store samples**：该用户态函数采集到的 DRAM load / store 样本数，包含因窗口证据不足而未用于分配流量的样本。原子操作可能同时计入两列；`[kernel]` 样本只进入 Read 系统分母和系统级汇总。

多进程采集时还会输出进程级汇总：

```text
CPU DDR BANDWIDTH BY PROCESS (USER-SPACE)

PID              Read BW   Write BW(est*)   Total BW    Samples
                 (MiB/s)      (MiB/s)       (MiB/s)

12345            1024.30        812.52       1836.82     182934
12346             523.11        921.35       1444.46     156237
```

用于查看各目标进程用户态函数对应的 DDR 带宽和 SPE sample 分布。

汇总结果示例：

```text
DDR BANDWIDTH SUMMARY

Metric                         Read       Write       Total
                             (MiB/s)     (MiB/s)     (MiB/s)

DDRC physical traffic        2604.45      192.15     2796.60
Observed PCIe DMA               0.24        0.00        0.24
PCIe DMA deducted               0.24        0.00        0.24
System residual              2604.21      192.15     2796.36
Target PID(s) user attributed 2580.00        0.00     2580.00
Kernel observed (read partition) 8.00         0.00        8.00
Kernel observed (write overlap*) 0.00       120.00      120.00
Other user/system               16.21       192.15      208.36
* Kernel observed Write 与 Target Write 重叠，不能相加。
```

各项含义：

* **DDRC physical traffic**：DDRC 实际测得的系统物理 DRAM 流量。
* **Observed PCIe DMA**：PCIe PMU 观测到的 DMA 流量。
* **PCIe DMA deducted**：实际从 DDRC 流量中扣除的 DMA 流量。
* **System residual**：扣除 PCIe DMA 后剩余的 CPU/System DDR 流量。
* **Target PID(s) user attributed**：Read 为目标用户态函数的归因之和；Write 在目标 store evidence 达标时取完整 Residual Write，函数表再在目标 PID 内部分配。
* **Kernel observed (read partition)**：按 system-wide kernel load sample 比例估算，是 Residual Read 的互斥分项。
* **Kernel observed (write overlap*)**：按 system-wide kernel store sample 比例估算的 informational observation；它与 Target Write 重叠，不能相加，也不参与 `Other user/system` 的扣减。
* **Other user/system**：Read 扣除目标用户归因和 Kernel Read；Write 只扣除 Target Write，不扣除重叠的 Kernel Write observation。

```text
Attribution coverage: 93.13%
User function windows: read 100/100, write 0/100
```

其中：

* **Attribution coverage**：CPU/System residual 中成功归因到目标进程的比例。
* **read 100/100**：100 个窗口中，100 个窗口有足够用户态 Read SPE 样本且有可归因读流量。
* **write 0/100**：100 个窗口中没有可用于用户态函数 Write 归因的 store 样本。

### Debug 输出

使用：

```bash
sudo tools/bin/ddr_bandwidth \
    --debug \
    --period 4860 \
    -p 12345 \
    -d 10
```

`--debug` 会额外输出：

```text
DEBUG TIMING
DBG_WINDOW
DBG_FUNC
DBG_TIMESTAMP
DEBUG SPE CONFIG
DEBUG SPE SAMPLE PIPELINE DIAGNOSTICS
DEBUG SPE / WINDOW HEALTH
DEBUG SPE EVENT STAT
DEBUG SPE STORE PC STAT
```

其中包括采集时间、逐窗口 DDRC/DMA/分母/分子/归因字节、实际 SPE period/data filter/event filter 配置、SPE record pipeline、各 event bit 及实际 event-mask 组合、SPE drain 数量与耗时、时间戳异常、空窗口/读取失败，以及 store PC、指令编码和访问宽度等诊断信息。`PmuDataExt::event` 是位图，因此同一条 record 可能同时计入多个 event bit。
这些信息仅用于调试，不参与正式 DDR 带宽权重计算。

## 注意事项

DDRC、PCIe PMU 和 SPE 都按系统级作用域采集；SPE 的 system-wide 分母同时包含用户态和内核态，函数分子只取目标 PID 的用户态样本。kernel PC 仅形成系统级观测 bucket，不按 PID 归因。

因此，为了获得更准确的进程和函数级归因结果，建议采集期间尽量减少其他高 DDR 带宽进程的干扰。

Read BW 表示基于 DRAM-source SPE sample 对 **physical DDR read traffic** 的统计归因结果，不等同于函数执行的逻辑 load 字节数。

Write BW 基于 store sample 数进行统计归因，不使用 store 指令访问宽度作为正式权重。由于 store retirement 与 dirty cache line 最终写回 DRAM 存在时间差，同时 SPE 对不同 store 指令的采样密度可能存在差异，因此 Write BW 应视为估算值。

## 重复采集不一致：参数影响和本次修正

函数级带宽是按 SPE 样本比例分配系统剩余流量的统计估计，不是函数独立的硬件字节计数器。即使 attribution coverage 为 100%，也不代表准确率为 100%。表格使用整个采集时段作为分母，不是函数实际运行时间。

| 参数 | 影响 | 调整时观察什么 |
| --- | --- | --- |
| `--period` | 采样的操作间隔。通常越小样本越多，但采样开销、碰撞和缓冲区压力也可能增加 | 同一函数各方向的样本数、采集开销、重复运行分配比例 |
| `-i / --interval` | DDRC/PCIe 归因窗口。大窗口通常样本更多，但会混合不同函数阶段；小窗口容易证据不足 | sparse windows、coverage、阶段变化 |
| `--spe-read-interval` | 用户态读取 SPE 缓冲区的间隔，与硬件 period 不同 | `DEBUG SPE / WINDOW HEALTH` 中读取次数、耗时与失败情况 |
| `-d / --duration` | 总采集时长，决定覆盖到哪些工作阶段 | 预热、循环次数、启动/结束阶段是否相同 |
| `--min-latency` | 丢弃低延迟样本，改变函数之间的样本分布 | 一般带宽归因保持 0，非零时输出偏差提示 |

### 稀疏窗口与时序

- 默认每个窗口的读、写方向只需要 1 个目标样本即可参与归因，以减少短时间采集的未归因窗口；单样本窗口的统计波动会更大。
- 可通过 `--min-samples N` 提高证据门槛。提高门槛可能降低覆盖率，也可能偏向活跃阶段；必须同时比较未归因流量，不能仅比较分配后的数值。
- Read 保持严格的当前窗口样本比例；Write 只向前回看两个 SPE 有效窗口，不把全程样本混合后分配全部流量。
- 先读取计数器确定窗口边界，再读取及解析 SPE；每条 SPE 记录按自身时间戳放入半开窗口 [start, end)，迟到记录会回填已封口窗口。各 PMU 仍是顺序操作，窗口并非硬件原子同步。
- 处理耗时超过调度周期时跳过过期截止点，合并到实际较长窗口，避免连续补出极短窗口。实际采集时间可能超过请求时间。
- 结束或中断时先读取尾部 DDRC 边界，再停止并 drain SPE；边界后的 SPE 样本不进入最终窗口。已报告 SPE 读取失败的窗口、DDRC 失败后的首个恢复窗口不参与函数分配。
- DDRC 不可用时，带宽列显示 `N/A`，仅报告样本数。`样本数 × period` 是操作数量的估计量，不能直接当作字节数。
