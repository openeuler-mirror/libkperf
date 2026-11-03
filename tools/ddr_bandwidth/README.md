# ddr_bandwidth

`ddr_bandwidth` 用于统计目标进程的函数级 DDR 带宽。

工具结合三类硬件数据：

* **DDRC PMU**：采集系统实际的物理 DRAM 读写流量。
* **PCIe PMU**：采集 PCIe DMA 流量，并从 DDRC 流量中扣除。
* **Arm SPE**：采集 system-wide 内存访问样本。Read 和 Write 都使用完整 system-wide 样本作为分母。

## 基本原理

DDRC/PCIe 归因窗口默认是 **100 ms**，Arm SPE 默认每 **50 ms** 读取一次。`--spe-read-interval` 可单独设置 SPE 读取间隔；实际间隔上限为 `--interval`，每个归因窗口边界都会读取 SPE。窗口内的多批 SPE 数据累计后参与归因。

系统剩余 DDR 流量计算为（包含其他进程和内核流量，不能直接等同于目标进程流量）：

```text
Residual Read  = max(0, DDRC Read  - PCIe DMA Read)
Residual Write = max(0, DDRC Write - PCIe DMA Write)
```

解析为 `[kernel]` 的 SPE 记录会按时间戳进入 system-wide 统计。Target User、Other User 和 Kernel 使用相同的 system-wide 分母，形成互斥分项；没有可靠样本分母、目标样本未达到 `--min-samples` 或窗口无效的流量保留为 Unattributed。Kernel 样本不会进入目标函数表或进程表，也不会按样本携带的 PID 强行归属给某个进程。

### Read 归因

SPE数据具有source字段的环境上，Read侧只使用SPE中数据来源为：

```text
HIP_LOCAL_MEM
HIP_REMOTE_MEM
```

的 load sample。
其他source不可用的环境上，降级使用所有具有有效 OP 信息的 SPE load sample
作为 DDRC 物理读流量的统计分配权重，并在首次处理 load 时输出警告。回退权重会包含 cache hit，因此只能用于近似函数热点分配，精度低于
source 字段判断；DDRC 汇总流量仍是物理流量，但函数级 Read BW 不是独立硬件计数值。

每个符合当前模式的 Read sample 权重相同：

```text
Target User Read Bytes = Residual Read Bytes × Target Read samples / System Read samples
Other User Read Bytes  = Residual Read Bytes × Other Read samples  / System Read samples
Kernel Read Bytes      = Residual Read Bytes × Kernel Read samples / System Read samples

函数 Read Bytes
    = Residual Read Bytes
      × 函数 Read sample 数
      / 窗口内 system-wide（用户态 + 内核态）Read sample 数
```

Target User、Other User 和 Kernel 也分别按各自 Read sample 占 system-wide Read sample 的比例分配。目标样本未达到 `--min-samples` 时，不把目标份额强制分给函数，而是留在 Unattributed。

### Write 归因

Write 侧根据 SPE store sample 数进行估算。考虑 store retirement 到 dirty cache line 写回 DDR 的延迟，窗口 `i` 使用当前窗口以及前两个 SPE 有效窗口（`i-2`、`i-1`、`i`）的 store sample；每个样本使用相同权重：

```text
函数 Write Bytes[i]
    = Residual Write Bytes[i]
      × sum(函数 store samples[i-2..i])
      / sum(system-wide store samples[i-2..i])
```

第 0、1 个窗口只使用当时已经存在的窗口。历史样本仅作为当前 DDRC Write residual 的归因权重；每个窗口的 physical write residual 仍只分配一次。

Target User、Other User 和 Kernel 均使用同一组三窗口 system-wide store 分母。目标三窗口 store samples 达到 `--min-samples` 时，目标函数按其占 system-wide store samples 的比例分配；未达门槛的目标份额保留为 Unattributed。这样每个窗口都满足：

```text
Target User Write + Other User Write + Kernel Write + Unattributed Write
    = Residual Write
```

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

全局采集所有用户态进程（两种写法等价）：

```bash
sudo tools/bin/ddr_bandwidth -a -d 10
sudo tools/bin/ddr_bandwidth -d 10
```

全局模式仍使用 system-wide SPE 分母，并把所有非内核用户态样本加入函数归因；函数表和进程表
按带宽输出 Top N。它不会执行目标 PID 存活检查或目标进程信号同步。系统进程较多时样本量和
符号解析开销会明显增加，应适当提高 `--period` 并通过 `--debug` 检查实际窗口数。

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

#### 多进程采集精度与 SPE 影响

SPE 是 system-wide 的统计采样，不是每个 PID 独占的精确计数器。多个目标进程并发运行时，它们共享
每个 CPU 上的 SPE 采样机会和 AUX buffer，硬件不会为各 PID 分配固定或相同的样本配额。因此不同进程的
CPU 运行时间、CPU 迁移、访存指令类型、采样随机性、采样碰撞及缓冲区压力，都可能造成各 PID 样本数偏差。

DDRC/PCIe 给出的系统物理流量总量不受目标 PID 数量影响，但进程级和函数级带宽需要按照 SPE 样本比例
进行归因。某个 PID 的样本偏多或偏少会直接传导到其 Read/Write 带宽结果，因此多进程表中的样本数和
带宽比例只能作为统计估计，不能视为各进程的精确硬件流量占比；多进程同时采集的结果也可能与分别进行
单进程采集的结果不同。进程运行时间很短、各进程负载差异较大或有效样本很少时，这种偏差会更加明显。

使用建议：

* 对精度要求较高时，优先分别对每个目标进程进行单进程采集。
* 必须多进程采集时，尽量让各进程覆盖相同且稳定的工作阶段，适当延长采集时间并重复测量。
* 可适当减小 `--period` 增加样本数，但过小会增加采样开销和 AUX buffer 压力，需要观察结果是否稳定。
* 使用 `--debug` 检查各 PID 样本数、空窗口、读取失败和 `DEBUG SPE / WINDOW HEALTH`；样本严重不均衡或
  有效样本过少时，应降低对进程级、函数级归因结果的置信度。

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

工具优先从 `/sys/bus/event_source/devices/<pmu>/events/` 读取 PCIe payload
事件配置。部分旧版或裁剪版内核没有导出事件别名时，会使用 HiSilicon PCIe
PMU ABI 定义的 raw event code。任一指定或自动发现的 BDF 无法打开、启用或完成
baseline 时，本次采集不使用残缺的 PCIe 数据做 DMA 扣减。

常用参数：

```text
-a, --all             全局采集所有用户态进程；未指定 -p/启动命令时默认启用
-p, --pid PID          目标进程 PID，可指定多个
-d, --duration SEC     采集时间，默认 10 秒
-i, --interval MS      DDRC 归因窗口，默认 100 ms
    --spe-read-interval MS  SPE 读取间隔，默认 50 ms，不超过归因窗口
    --min-samples N    每方向最低目标用户态样本数，默认 1；Read 看当前窗口，Write 看当前加前两窗口
    --period N         SPE sample period，默认 2048，不是毫秒
    --min-latency N    SPE minimum latency
    --top N            输出 Top N 函数；全局模式也限制进程表行数
    --pcie-bdf BDF     指定 PCIe BDF，可重复
    --debug            输出聚合 timing、SPE 配置/pipeline/event 和窗口健康统计
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
* **Read samples / Store samples**：该用户态函数采集到的有效 Read load / store 样本数，包含因窗口证据不足而未用于分配流量的样本。SPE source字段有效的环境上 Read samples 仅包含 DRAM-source load，其他 CPU 类型包含全部有效 load。原子操作可能同时计入两列；`[kernel]` 样本只进入 system-wide Read/Write 分母和系统级汇总。

多进程或全局采集时还会输出进程级汇总：

```text
CPU DDR BANDWIDTH BY PROCESS (USER-SPACE)

PID              Read BW   Write BW(est*)   Total BW    Samples
                 (MiB/s)      (MiB/s)       (MiB/s)

12345            1024.30        812.52       1836.82     182934
12346             523.11        921.35       1444.46     156237
```

用于查看各目标进程用户态函数对应的 DDR 带宽和 SPE sample 分布。全局模式按带宽排序并只显示
`--top` 指定的进程数。

汇总结果示例：

```text
DDR BANDWIDTH SUMMARY

Metric                         Read       Write       Total
                             (MiB/s)     (MiB/s)     (MiB/s)

DDRC physical traffic        2604.45      192.15     2796.60
Observed PCIe DMA               0.24        0.00        0.24
PCIe DMA deducted               0.24        0.00        0.24
System residual (DDRC - DMA) 2604.21      192.15     2796.36
Target PID(s) user (SPE-attributed) 2580.00  19.22     2599.22
Other user (SPE-attributed)     16.21      153.72      169.93
Kernel (SPE-attributed)          8.00        9.61       17.61
Unattributed                     0.00        9.60        9.60
```

各项含义：

* **DDRC physical traffic**：DDRC 实际测得的系统物理 DRAM 流量。
* **Observed PCIe DMA**：PCIe PMU 观测到的 DMA 流量。
* **PCIe DMA deducted**：实际从 DDRC 流量中扣除的 DMA 流量。
* **System residual**：扣除 PCIe DMA 后剩余的 CPU/System DDR 流量。
* **Target PID(s) user (SPE-attributed)**：按目标用户态样本占 system-wide 样本的比例估算；函数表中的目标函数使用相同分母。
* **Other user (SPE-attributed)**：按非目标用户态样本占 system-wide 样本的比例估算；不进入目标函数表。
* **Kernel (SPE-attributed)**：按 kernel 样本占 system-wide 样本的比例估算，是 Residual Read/Write 的互斥分项，不是独立硬件计数值。
* **Unattributed**：无可靠分母、目标证据未达到门槛或窗口无效而未分配的 Residual 流量。

```text
System partition coverage: 99.66%
User function windows: read 100/100, write 0/100
```

其中：

* **System partition coverage**：CPU/System residual 中成功划分为 Target User、Other User 或 Kernel 的比例；剩余部分对应 Unattributed。
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
DEBUG SPE SAMPLE PIPELINE DIAGNOSTICS
DEBUG SPE / WINDOW HEALTH
DEBUG SPE EVENT STAT
Attribution windows: invalid=... read_sparse=... read_no_denominator=... write_sparse=... write_no_denominator=...
```

其中包括采集时间、SPE record pipeline、各 event bit的聚合计数、SPE drain 数量与耗时占比、实际/理论窗口数、时间戳异常、空窗口、读取失败，
以及归因无效、目标样本稀疏和缺少 system-wide 分母的窗口数。
为避免海量输出及额外调试开销，不再打印逐窗口、逐函数、event-mask 组合和 store-PC 明细。
`PmuDataExt::event` 是位图，因此同一条 record 可能同时计入多个 event bit。
这些信息仅用于调试，不参与正式 DDR 带宽权重计算。

## 注意事项

DDRC、PCIe PMU 和 SPE 都按系统级作用域采集；SPE 的 system-wide 分母同时包含用户态和内核态。
PID 模式的函数分子只取目标 PID 的用户态样本，全局模式取所有非内核用户态样本。kernel PC 仅形成
系统级观测 bucket，不按 PID 归因。

因此，为了获得更准确的进程和函数级归因结果，建议采集期间尽量减少其他高 DDR 带宽进程的干扰。

Read BW 表示对 **physical DDR read traffic** 的统计归因结果，不等同于函数执行的逻辑 load 字节数。SPE source 字段有效的环境上使用 DRAM-source SPE sample；其他 CPU 类型使用全部有效 load 作为近似权重，可能包含 cache hit，函数级结果应视为热点估算。

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
- 任一 PCIe collector 读取失败或需要重新同步时，该窗口仍计入 DDRC 物理总量，但不执行 DMA 扣除和函数归因，避免把未知 DMA 流量误算给 CPU 函数。
- DDRC 不可用时，表格以 `Mops/s` 输出 `样本数 × period / 时间` 的 SPE 操作率估计；它不是字节数，也不是物理 DDR 带宽。
