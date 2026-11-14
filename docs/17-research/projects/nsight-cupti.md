# Nsight Systems / CUPTI 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建

## 0. 一句话

NVIDIA 的 GPU 性能观测工具链：**Nsight Systems** 给出系统级时间线（CPU↔GPU 全景：CUDA API / 内核 / 内存拷贝 / NVTX），**CUPTI** 是其底层的追踪与剖析 C API——对本项目而言，它们不是引擎组件，而是给 D5"拷贝次数 + 同步点"判据产出**可复现证据**、并把 GPU 活动与自家 tick 号关联起来的官方通道。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| CUPTI = "The CUDA Profiling Tools Interface"，"provides C and Python APIs for building profiling and tracing tools for CUDA applications, providing detailed insight into how code executes on both the CPU and GPU" | docs.nvidia.com/cuda/cupti/ |
| CUPTI 暴露的 API：Activity / Callback / Host Profiling / Range Profiling / PC Sampling / SASS Metric / PM Sampling / Checkpoint / Profiling（Profiling 自 CUDA 13.0 起弃用，官方建议改用 Range Profiling） | docs.nvidia.com/cuda/cupti/ |
| Activity API："Asynchronously record CUDA activities, e.g. CUDA API, Kernel, memory copy"；追踪收集 "timestamps and associated metadata for CUDA activities such as runtime and driver API calls, kernel launches, and memory copy operations" | docs.nvidia.com/cuda/cupti/ |
| CUPTI 以动态库形式发布在所有 CUDA 支持平台；静态库仅 Linux x86_64 与 ARM SBSA；CUPTI Python 仅 Linux（x86_64 / aarch64 sbsa） | docs.nvidia.com/cuda/cupti/ |
| Nsight Systems = "a system-wide performance analysis tool designed to visualize an application's algorithms, identify the largest opportunities to optimize, and tune to scale"，覆盖 "CUDA, cuBLAS, cuDNN, and NVIDIA TensorRT" 的 "system workload and CUDA® libraries trace, network communications, OS interactions, and more" | developer.nvidia.com/nsight-systems |
| Nsight Systems 采集侧含 GPU metrics（PCIe / NVLink / DRAM / SM 利用率 / Tensor Core / warp occupancy）与 CPU 侧并行度/利用率 | developer.nvidia.com/nsight-systems |
| `nsys --trace` 可选值（本机 2024.4.2 实测逐字）：`cuda, nvtx, cublas, cublas-verbose, cusolver, cusolver-verbose, cusparse, cusparse-verbose, opengl, opengl-annotations, nvvideo, vulkan, vulkan-annotations, dx11, dx11-annotations, dx12, dx12-annotations, openxr, openxr-annotations, wddm, python-gil, none`，默认 `cuda,nvtx,opengl` | 本机 nsys 2024.4.2 `profile --help` |
| CUDA 追踪选项：`--cuda-trace-all-apis`（默认跳过"not critical for performance analysis"的 API）、`--cuda-event-trace`、`--cuda-memory-usage`（"Track the GPU memory usage by CUDA kernels"）、`--cuda-graph-trace=graph\|node`（node 粒度 "may cause significant runtime overhead"） | docs.nvidia.com/nsight-systems/UserGuide/ |
| `--capture-range` 支持 `cudaProfilerApi` / `nvtx`（本机实测列表：`none, cudaProfilerApi, nvtx, hotkey`） | docs.nvidia.com/nsight-systems/UserGuide/ + 本机 nsys help |
| 内存拷贝追踪三重证实：① CUPTI 官方文档将 memory copy 列为 Activity 类别；② nsys `analyze` 内置规则名 `cuda_memcpy_async` / `cuda_memcpy_sync` / `cuda_memset_sync`；③ 本机实测导出的 SQLite 中存在 `CUPTI_ACTIVITY_KIND_MEMCPY` 表（见 §5 实测记录） | docs.nvidia.com/cuda/cupti/ + docs.nvidia.com/nsight-systems/UserGuide/ + 本机实测 |
| NVTX = "a cross-platform API for annotating source code to provide contextual information to developer tools"，提供 markers、push/pop（可嵌套）与 start/end（可跨线程）ranges、资源命名、domain/category；"By default, NVTX API calls do *nothing*"（无工具时近零开销，`-DNVTX_DISABLE` 可编译期整体移除） | github.com/NVIDIA/NVTX |
| "Nsight Systems logs NVTX calls and shows them on a timeline alongside driver/OS/hardware events"；"The CUPTI API supports recording traces of NVTX calls" | github.com/NVIDIA/NVTX |
| Nsight Systems 与 CUPTI 的关系（实证，非官方逐字声明）：本机 nsys target 目录为每个 CUDA 版本捆绑一份 CUPTI（`cupti64_100.dll` … `cupti64_126.dll`）；导出 SQLite 的表名即 CUPTI activity kind（`CUPTI_ACTIVITY_KIND_MEMCPY` 等） | 本机 Nsight Systems 2024.4.2 安装目录 + 本机实测导出 |

## 2. 边界与限制

### 官方承认的限制

- **采集范围**：CUPTI 追踪的是 CUDA 相关活动（API 调用、内核、拷贝、同步等），不覆盖 OS 级全景——那是 Nsight Systems 的职责（它再加 OS runtime、网络、CPU sampling 等）。两者是不同抽象层：CUPTI 是给工具作者的库，Nsight Systems 是成品工具（docs.nvidia.com/cuda/cupti/、developer.nvidia.com/nsight-systems）。
- **默认采样 API 子集**：Nsight Systems 默认跳过"not critical for performance analysis"的 CUDA API，全量要显式 `--cuda-trace-all-apis`（docs.nvidia.com/nsight-systems/UserGuide/）。
- **开销有形态警告**：CUDA graph 的 node 粒度追踪 "may cause significant runtime overhead"；CUDA 数据缓冲区写盘 "may cause profiler overhead"（可由 `--cuda-flush-interval` 控制）（docs.nvidia.com/nsight-systems/UserGuide/ + 本机 nsys help）。
- **特权要求**：CPU context switch 追踪与 CPU sampling 需要管理员/root——本机实测：非管理员运行时两条 warning 明示 "disabling"（本机 2026-11-05 运行记录）。Windows 上跑 CLI 必须 administrator（docs.nvidia.com/nsight-systems/UserGuide/）。
- **平台限制**：仅支持 64 位环境；Arm SBSA 目标只能用 CLI 剖析；CUPTI 静态库与 CUPTI Python 仅 Linux（docs.nvidia.com/cuda/cupti/、docs.nvidia.com/nsight-systems/InstallationGuide/）。
- **Profiling API 弃用**：旧 Profiling API 自 CUDA 13.0 起弃用，迁移方向是 Range Profiling（docs.nvidia.com/cuda/cupti/）。

### 我们的推断（未获官方逐字背书，单独列出）

- **时间线不是计量基准**：nsys 的时间线适合回答"发生了什么、谁等谁"，不适合当延迟主张的数字来源——本仓库的延迟主张纪律是只认 Release 构建 + `benchmarks/regression/`（AGENTS.md），nsys 数字只能作**证据/解释**，不能进性能主张正文。
- **CUPTI 直连是有代价的工程**：Activity API 需要自建缓冲区管理、解析与时间基对齐；除非确有"程序内自观测"需求，先用 nsys 的成品采集 + SQLite 导出更便宜。
- **NVTX 注解应在库内用 domain 隔离**：NVTX README 明确建议库作者用 domain 隔离自己的注解——引擎若加注解，应建独立 domain（如 `omniworld`），避免与第三方库注解混杂（这是对官方建议的采纳，不是官方对本仓库的陈述）。

## 3. 许可（代码 / 权重 / 数据 三分）

**代码：**

- **CUPTI 库（可再分发，条款已核验）**：受 **CUDA Toolkit EULA** 约束——与 D1 笔记 §5 核验的 **NVIDIA Video Codec SDK License Agreement 是两份不同的文书**，D1 §5 的结论**不直接覆盖 CUPTI**，但其决策模式（目标码集成、SDK 本体不入库、仓库只留脚本+许可记录）经核验后**可整套复用**。CUPTI 的一手证据有两层：
  - 本机实物（CUDA v12.6 `EULA.txt`，Last updated: January 12, 2024）：§1.1 授予 "Distribute those portions of the SDK that are identified in this Agreement as distributable, **as incorporated in object code format into a software application**"；§2.2 规定可分发部分列于 Attachment A；**Attachment A 逐字含 "CUDA Profiling Tools Interface (CUPTI) Library — Windows: cupti.dll / Mac OSX: libcupti.dylib / Linux: libcupti.so"**，同表还有 "NVIDIA Tools Extension Library"（nvToolsExt.dll 等）。
  - 在线现行版（docs.nvidia.com/cuda/eula/index.html，v13.5，Last updated: January 26, 2026）：同样结构，Attachment A 列 CUPTI 库；附加条件：应用须有 "material additional functionality"、可分发部分 "**shall only be accessed by your application**"、须带署名声明（"This software contains source code provided by NVIDIA Corporation."）、不得独立分发 SDK、不得逆向、不得置于 copyleft 许可下、Critical Applications 限制条款。
  - 注意命名演变：Attachment A 写的是 `cupti.dll`，实际文件按版本命名（本机 CUDA 12.6 为 `cupti64_2024.3.0.dll`；官方文档称 CUDA 13.5 起为 `cupti64_13.dll`）——EULA 自述涵盖命名变体（如 `cudart64_90.dll` 例子）。
- **NVTX3 头文件（宽松）**：官方仓库 LICENSE.txt 逐字："**NVTX is under the Apache License v2.0 with LLVM Exceptions**"（github.com/NVIDIA/NVTX）。v3 为 header-only、零依赖。CUDA Toolkit 也随附 NVTX（且被 EULA 列为可分发组件）；但本机 CUDA 12.6 `include/nvtx3/nvToolsExt.h` 文件头是 2009–2016 的专有声明（旧式样板）——**用 Apache-2.0 的 GitHub 版头文件更干净**。
- **CUPTI 头文件与静态库**：`extras/CUPTI/include/` 下头文件随 Toolkit 分发、按同一 EULA 管理；未发现单独的开源许可（截至核验日）。
- **Nsight Systems 本体**：CUDA Toolkit EULA 前言写明其适用于 "NVIDIA Nsight tools (Visual Studio Edition)"；**独立版 Nsight Systems 安装包自带的 EULA 文本未逐一核验（UNVERIFIED）**。官方产品页**无定价声明**——"是否免费"没有找到官方逐字原文（UNVERIFIED，见 §7）。

**权重：** 不适用（无模型权重）。

**数据：** 不适用（工具不产生对外数据集；采集到的 `.nsys-rep`/`.nsys-qdstrm`/SQLite 是本机性能证据文件，按仓库纪律不入库，归 `benchmarks/regression/` 体系管理的是可复现指标，不是原始 trace）。

## 4. 硬件要求（显存/驱动/OS，来源+日期）

| 项 | 要求 | 来源（检索日期均为 2026-11-05） |
|---|---|---|
| Nsight Systems host OS | Windows 10 / Server 2019+（仅 64 位）、Linux（Ubuntu 14.04+ 已知可用）、macOS 10.10+（仅 host 端） | docs.nvidia.com/nsight-systems/InstallationGuide/ |
| Nsight Systems 目标平台 | x86_64、Arm SBSA、Arm Windows、Tegra（QNX 经 Embedded Edition）；Ubuntu 22.04/24.04/26.04、RHEL 8+、Win10/11、Server 2022+ | 同上 |
| CUDA 版本支持 | CUDA 10.0+（Arm SBSA 10.2+）；CUDA 11.x → 驱动 450+；DX12 追踪 → 驱动 411.63+ | 同上 |
| GPU 架构 | "NVIDIA GPU architectures starting with Turing"（Turing 及以后） | 同上 |
| 位数 | 仅纯 64 位环境（不支持 32 位系统或进程） | 同上 |
| CUPTI | "all CUDA-supported platforms"（动态库）；安装于 CUDA Toolkit 的 `extras/CUPTI/` | docs.nvidia.com/cuda/cupti/ |
| 显存 | 两者的官方文档均未声明最低显存要求（**UNVERIFIED**：仅"未见声明"，非"无要求"） | — |
| **本机适配（D1 实测，引用不重测）** | RTX 4060 Laptop（CC 8.9）+ 驱动/运行时 12.6：CC 8.9 > Turing，CUDA 12.6 ∈ 支持区间 → **在支持范围内**；nsys 2024.4.2 本机实跑成功（非管理员模式，CPU context switch 与 CPU sampling 被自动禁用） | 本卡 §5 实测 + docs/17-research/2026-10-26-stage-d-environment-and-licenses.md §1 |

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

**API（CUPTI 侧）：**
- Activity API（异步缓冲记录：runtime/driver API 调用、kernel、memory copy、synchronization…）；Callback API（订阅 CUDA 事件）；PC Sampling / SASS Metric / PM Sampling（微架构层）；Checkpoint API（设备功能态保存/恢复）（docs.nvidia.com/cuda/cupti/）。
- **关联自家 tick 号的官方机制（本卡核心问题）**：
  1. **CUPTI External Correlation**（本机 `extras/CUPTI/include/cupti_activity.h` 逐字）：`CUPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION = 39`（行 465）；`CUpti_ExternalCorrelationKind` 枚举含 `CUSTOM0=3 / CUSTOM1=4 / CUSTOM2=5`，注释逐字 "**Custom correlation kinds are reserved for usage in external tools**"（行 5697–5735）；`CUpti_ActivityExternalCorrelation` 记录 "correlates native CUDA records (e.g. CUDA Driver API, kernels, memcpys, ...) with records from external APIs"，字段 `externalKind` + **`externalId: uint64_t`**（行 5737–5763）→ **把 tick 号直接当 `externalId` 写入，activity 流里即可回查每个 kernel/memcpy 属于哪个 tick**。
  2. **correlationId**：kernel/memcpy 等 activity 记录携带 `correlationId`，与发起它的 runtime/driver API 记录的 ID 相同（同文件行 2073/2219 等）——API 调用↔GPU 执行的两段式对账。
  3. **NVTX range → GPU 时间线投影**（官方 UserGuide 逐字）："The Timeline View also **projects these ranges onto the GPU timeline**, allowing you to see what GPU activity was launched within each CPU range" → **每个 tick 包一个 NVTX push/pop range（payload 携带 tick 号），GUI 里 GPU 工作直接落在对应 range 下方**；NVTX README 载明 "The CUPTI API supports recording traces of NVTX calls"。
- 三条通道的分工：NVTX 投影给人看（GUI/零代码侵入，`-DNVTX_DISABLE` 可整体摘除）；External Correlation 给机器对账（tick 号↔activity 的结构化 join）；correlationId 是 CUPTI 内部 API↔执行的对账。

**导出 / 资产格式（Nsight Systems 侧）：**
- 采集产物：`.nsys-rep`（报告）+ `.qdstrm`（中间结果，本机实测两者都生成）；`nsys import` 可把 `.qdstrm` 转回 `.nsys-rep`（docs.nvidia.com/nsight-systems/UserGuide/）。
- `nsys export --type`：`sqlite, hdf, info, arrow, jsonlines, arrowdir, parquetdir`（官方文档）→ **SQLite/Arrow/Parquet 都能直接进数据管线**，无需逆向任何格式。
- `nsys stats/analyze` 输出：column/table/csv/tsv/json 等；内置规则含 `cuda_memcpy_async`、`cuda_memcpy_sync`、`cuda_memset_sync`、`cuda_api_sync`（docs.nvidia.com/nsight-systems/UserGuide/）。
- **本机实测记录（2026-11-05，全部实际执行）**：
  1. `"C:/Program Files/NVIDIA Corporation/Nsight Systems 2024.4.2/target-windows-x64/nsys.exe" --version` → `NVIDIA Nsight Systems version 2024.4.2.133-244234382004v0`。
  2. `nsys profile -t cuda,nvtx --force-overwrite true -o /tmp/nsight-check/probe build/runtime_probe.exe --json` → 生成 `probe.nsys-rep` 与 `.qdstrm`；非管理员 warning 两条（context switch / CPU sampling 自动禁用）。
  3. `nsys export --type sqlite -o /tmp/nsight-check/probe.sqlite probe.nsys-rep`（146 events）→ 生成 585 KB SQLite。
  4. 对该 SQLite 的 schema 字节 grep 到表名：`CUPTI_ACTIVITY_KIND_MEMCPY`、`CUPTI_ACTIVITY_KIND_RUNTIME`、`CUPTI_ACTIVITY_KIND_SYNCHRONIZATION`、`CUPTI_ACTIVITY_KIND_CUDA_EVENT`（另有 `TARGET_INFO`、`ANALYSIS_DETAILS`）→ **"GPU 时间线表化后用 SQL 与自家 tick 表 join"的路线在参数化数据上成立**。
  5. nsys target 目录逐文件核对：自带 `cupti64_100.dll` … `cupti64_126.dll`（每个 CUDA 大版本一份）+ NVTX 注入 DLL。

**控制面：**
- CLI：`nsys profile`（一键）/ `launch`+`start`+`stop`+`shutdown`（交互式，多会话并发）/ `sessions list|status`（docs.nvidia.com/nsight-systems/UserGuide/）。
- 采集开关：`--capture-range=nvtx`（按 NVTX range 启停采集）+ `--nvtx-domain-include/exclude`（domain 过滤）→ **可以做"只采指定 tick 区间"的定向采集**。
- `cudaProfilerApi` 触发（`cudaProfilerStart/Stop`）是第二个程序化启停通道。

## 6. 与 OmniWorld 的边界 + 族谱归宿

**族谱归宿：性能层工具链（关联本仓库 D5 GPU 数据流切片）——定位为「External reference（开发期观测工具链）」。**

理由：

1. **不进引擎数据通路，故不是 ②适配器候选**：适配器（如 ORT/FFmpeg）承担数据变换职责；Nsight Systems/CUPTI 只在**开发与诊断期**进入工作区（采集→证据→退出），运行时路径上没有它的位置。NVTX 头文件是唯一可能进源码的东西，但它是注解 API——无工具时为空操作、可 `-DNVTX_DISABLE` 整体摘除，属"可编译剔除的诊断埋点"，不构成依赖。
2. **不是 ③技能项目**：它不规格化任何重复活动（对照 `workflows/*.md` 的定义），只是被既有工作流（性能切片的"取证"环节）**使用**的工具。
3. **不是 ⑤明确让渡、也不是评测对象**：OmniWorld 的评测对象是引擎自身的 World Contract 指标，不是剖析工具；但剖析工具是**证据生产者**。
4. **竞品参照价值有限但有一处**：Nsight Compute（deep-dive per-kernel 指标）与本卡对象是分工关系（系统级 vs 内核级），可作"何时升级工具"的参照线，不展开立项。
5. **与 D5 的精确接口**：D1 笔记 §1/§7 的判据是"以**拷贝次数 + 同步次数**为主、字节数为辅"——本卡实测证明 nsys 导出 SQLite 里有 `CUPTI_ACTIVITY_KIND_MEMCPY` 与 `CUPTI_ACTIVITY_KIND_SYNCHRONIZATION` 表，**恰好就是该判据两根主轴的原始记录**；D5 做 GPU 数据流改造时，"改造前后逐 tick 的 memcpy/同步计数对比"可以直接由这套工具链产出，且方法可复现（命令已在 §5 留档）。
6. **许可以外的边界**：CUPTI 库虽被 EULA 允许目标码集成再分发（§3），但**本引擎没有"产品内嵌剖析器"的需求**——遵守"可分发 ≠ 该分发"：默认交付不携带 CUPTI 运行库；若未来引入 CUPTI 直连（如程序内自观测），按 ADR-0012 模式做成默认 OFF 的可选构建（profiling-only build），并遵守 EULA §1.1.2 的访问限制与署名要求。

## 7. 未解决问题 + 重查触发条件

| # | 未解决问题 | 状态 | 重查触发条件 |
|---|---|---|---|
| 1 | CUPTI 追踪开销的**官方量化数字**（已抓取页面未载明 overhead 百分比） | **UNVERIFIED**（未找到，不编造） | 若做 CUPTI 直连（程序内自观测），先实测带/不带采集的 tick 时延差；或查 CUPTI 文档 overhead 章节的现行版 |
| 2 | Nsight Systems 独立版的**定价/免费**官方声明（产品页无定价文字；本地 EULA 前言点名的是 "Nsight tools (Visual Studio Edition)"，独立安装包 EULA 未逐字核验） | **UNVERIFIED**（不是"免费"，是"未找到官方原文"） | 进入正式对外作品集、或需在文档中写"所用工具许可成本"时 |
| 3 | Nsight Systems UserGuide 中**内存操作时间线行的逐字 UI 文档**（主页抓取截断于选项表中段；memory-copy 追踪本身已由 CUPTI 文档 + analyze 规则名 + 本地 MEMCPY 表三方证实，仅 UI 逐字描述缺） | **UNVERIFIED**（仅限 UI 措辞） | 需要在文档/报告中引用 GUI 行名原文时，改抓 UserGuide PDF（docs.nvidia.com/nsight-systems/pdf/UserGuide.pdf） |
| 4 | CUPTI 库版本 ↔ CUDA 版本的**官方对应表**（本机 `cupti64_2024.3.0.dll` ↔ CUDA 12.6 为实测推断；官方命名规则仅核验到 "CUDA 13.5 起 cupti64_13.dll"） | **UNVERIFIED**（对应关系推断自安装目录） | 升级 CUDA 版本、或构建脚本需要按 CUPTI 版本写文件名时 |
| 5 | CUPTI External Correlation 在 **Nsight Systems 导出件里是否落表**（本机采集的 probe 无自定义 externalId，SQLite 中未验证该表内容；CUPTI API 层面已核验） | **UNVERIFIED**（API 层 verified、nsys 导出层未实测） | D5 真正接入 tick↔GPU 关联时，跑一次带 `CUSTOM0` externalId 的采集验证端到端 |
| 6 | NVTX3 头文件本机副本（CUDA 12.6 `include/nvtx3/`）文件头仍是 2009–2016 专有样板——若在源码中引入 NVTX，统一取 GitHub Apache-2.0 版头文件 | 已核验（本机文件头 + GitHub LICENSE.txt） | 引入 NVTX 注解时（作为构建决策记录的一部分） |

## 8. 来源清单

检索/实测日期：2026-11-05。

**官方在线文档与页面：**

1. CUPTI 总览（v2026.4.0，Last updated Nov 02, 2026）：https://docs.nvidia.com/cuda/cupti/ （2026-11-05 抓取）
2. CUDA Toolkit EULA（v13.5，Last updated January 26, 2026）：https://docs.nvidia.com/cuda/eula/index.html （2026-11-05 抓取）
3. Nsight Systems 产品页：https://developer.nvidia.com/nsight-systems （2026-11-05 抓取）
4. Nsight Systems User Guide：https://docs.nvidia.com/nsight-systems/UserGuide/index.html （2026-11-05 抓取，页面选项表在抓取中截断——见 §7.3）
5. Nsight Systems Installation Guide：https://docs.nvidia.com/nsight-systems/InstallationGuide/index.html （2026-11-05 抓取）

**官方仓库：**

6. NVTX README（github.com/NVIDIA/NVTX，release-v3）：https://github.com/NVIDIA/NVTX （2026-11-05 抓取）
7. NVTX LICENSE.txt（"NVTX is under the Apache License v2.0 with LLVM Exceptions"）：https://raw.githubusercontent.com/NVIDIA/NVTX/release-v3/LICENSE.txt （2026-11-05 抓取）

**本机实物（一手证据，2026-11-05 实测）：**

8. CUDA Toolkit v12.6 EULA.txt（Last updated: January 12, 2024）：`C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/EULA.txt`（§1.1 授权、§1.1.2 分发要求、§2.2、Attachment A 第 887–905 行：CUPTI Library 与 NVIDIA Tools Extension Library 条目）
9. CUPTI 头文件：`C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/include/cupti_activity.h`（行 465：`CUPTI_ACTIVITY_KIND_EXTERNAL_CORRELATION = 39`；行 5697–5735：`CUpti_ExternalCorrelationKind` 含 CUSTOM0–2；行 5737–5763：`CUpti_ActivityExternalCorrelation` 结构）
10. CUPTI 库实物：`.../CUDA/v12.6/extras/CUPTI/lib64/`（`cupti64_2024.3.0.dll`、`nvperf_host.dll`、`pcsamplingutil.dll` 等）
11. Nsight Systems 2024.4.2.133 安装：`C:/Program Files/NVIDIA Corporation/Nsight Systems 2024.4.2/`（target 目录自带 `cupti64_100.dll`–`cupti64_126.dll`；`nsys --version`、`nsys profile --help` 逐字输出）
12. 本机实测链：`nsys profile -t cuda,nvtx -o /tmp/nsight-check/probe <repo>/build/runtime_probe.exe --json` → `nsys export --type sqlite` → SQLite schema 含 `CUPTI_ACTIVITY_KIND_MEMCPY/RUNTIME/SYNCHRONIZATION/CUDA_EVENT` 表（过程与输出见 §5）

**仓库内引用（引用不复制）：**

13. docs/17-research/2026-10-26-stage-d-environment-and-licenses.md（D1 笔记：§1 硬件/带宽实测、§5 Video Codec SDK 许可结论——本卡 §3 指明其与 CUDA Toolkit EULA 是不同文书）
14. AGENTS.md（性能主张纪律：延迟主张只来自 Release 构建；可选依赖默认 OFF 模式）
15. docs/adr/（ADR-0012 可选依赖构建模式——本卡 §6 的"若引入 CUPTI 直连"的构建纪律参照）

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-nsight-cupti.md](deep/2026-11-05-nsight-cupti.md)
