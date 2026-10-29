# ONNX Runtime 作为可选依赖：默认 OFF 是参考模式的构建级证明

Status: accepted（2026-10-19）

> **性质**：本 ADR 是对 [阶段 C L1 §2 Round 1 #2](../design/stage-c-ai-in-loop/概要设计.md) 已冻结裁决（ORT 先行 / TensorRT EP 预留不实现，owner 逐轮确认，2026-10-16）的**正式记录**，不是新决策。补记的直接原因：**C3 是 ORT 第一次真正链进 `ow_core` 的切片**（C1 只引入 CMake 选项，没有消费者）——按 `docs/adr/README.md`「引入新的重量级依赖」的触发条件，此刻必须有 ADR。

参考模式（ADR-0009）是契约回归的唯一 oracle，其"零外部依赖"承诺此前只是**事实上**成立——仓库里没有第三方库，所以不需要论证。阶段 C 要把真实模型放进环（RT-DETRv2-S / Depth-Anything-V2-Small），而真实模型必须有推理框架；一旦框架变成常规链接项，"参考模式不依赖它"就从事实变成**需要被结构保证的性质**：任何一次手滑把 ORT 头文件 include 进核心模块，都会静默地把 oracle 变成受第三方版本行为摆布的东西，而这类污染在单构建配置下**看不出来**。同时，GPU 路径（CUDA EP）与逐字节承诺的冲突已由 ADR-0009 分层解决，但"同一个适配器内部同时持有两条 provider 路径"是新形态：provider 选择若做成核心分支，`loop` 就会长出 `if (cuda)`，破坏适配器不变式（Gate-1 判据 (b)）。

决定把 ORT 定义为**可选依赖**，并把参考模式的独立性下沉到构建层：

- **`OW_WITH_ONNXRUNTIME` 默认 OFF**。OFF 构建（`build.bat --test`，即参考模式与全部基线哈希的所在）不链接、不 include、不感知 ORT 的存在——这是"零外部依赖"的**构建级证明**，不再依赖"恰好没写"。
- **ON 时只有 `src/perception/*.cpp` 消费它**：单文件适配器 + 编译守卫 `#if OW_WITH_ONNXRUNTIME`；核心模块对 ORT **零 include**（`loop` / `belief` / `observation` / `truth_world` / `metrics` 一律不知 ORT 为何物）。
- **provider 是适配器的内部细节，不是核心的分支**：`ExecProvider` 类型放 `core/types.hpp`（避免 `perception → loop` 反向依赖，同 `BoundsPolicy` 先例），核心只做一次 `set_execution_provider(cfg.ep)` 转发，**不含任何 EP 条件逻辑**；`--ep cpu|cuda` 为运行时选择（L1 Round 2 #6），TensorRT EP 预留不实现。
- **模型文件不进 git**：`tools/fetch_models.mjs` 拉取 + SHA256 pin + 许可逐条记录（L1 Round 2 #5）。

## Consequences

- **构建矩阵从 1 变 2，代价立刻兑现**：ON 构建必须跑**全量** ctest，而不只是新切片的测试。C3 收口时 ON 构建首次跑全量套件即发现 `ow_slice03` 的注册表计数断言（写死 2 个适配器）在 ON 下为 3 → 红；该断裂已在 ON 配置下潜伏三个切片（problem-log 010）。纪律：**新构建配置一旦存在，它的全量套件就是必须跑的**。
- **基数断言必须绑定配置**：凡断言全局数量（注册表大小、枚举长度）的测试在可选依赖下不再是行为的函数，而是构建开关的函数（`kExpected = ON ? 3 : 2`，C4 会到 4）。
- **ADR-0009 的"每个 GPU 组件落地时声明容差窗口"在本切片只兑现一半**：CPU EP 参考路径实测输出抖动为 **0** → 窗口退化为逐位精确（不适用容差比较）；**CUDA EP 性能路径本机无法标定**（CUDA 12.6 + 零 cuDNN，ORT 1.31 的 CUDA EP 需 cuDNN 9 / CUDA 12.8+），窗口与延迟预算移交 C5 实测——**不拿别人机器的数字填表**（[C3 报告](../../benchmarks/regression/2026-10-19-stage-c-c3-rtdetrv2s.md) §7/§9）。
- **性能数字必须标构建类型**：同一份代码 Debug 与 Release 的 `observe()` 耗时恰好同值（570.2 vs 570.0 ms），因为 `/O2` 只覆盖本项目循环、推理在 ORT 预编译 DLL 内且参考路径故意单线程——但这属于**事后才知道的巧合**，纪律不变：延迟主张只能来自 Release 构建（C3 报告的延迟数字全部来自 `build-ort-rel/`）。
- **工件 schema 增一字段**：`first_fault`（隔离层捕获的第一条故障原因）。动机是实测到的可观测性归零——`--ep cuda` 失败时工件里只有 `adapter_faults: 1`，原因只存在于 ORT 自己的 stderr。已回写 [L0 §2 ⑨](../design/00-总体设计.md)（与 `boundary_events` 同一条"只加字段不改语义"规则）；problem-log 009。
- **CLI 错误语义两项已在 C5 裁定并实现**（2026-10-19，owner 批准）：① setup 期异常此前无出口——`main()` 不接 → 进程 fastfail（实测 exit `0xC0000409`、零输出、无工件），现由 `main()` 统一 try/catch → 具名错误 + 非零退出；② **性能模式下 `adapter_faults > 0` ⇒ 退出码非零**（工件仍照常写出，`first_fault` 带原因），因为"一次有参与者静默失败的运行不构成一次合格测量"；参考模式语义不变。这等于给 ADR-0009 的性能模式承诺补了一条**可机器判定的合格性信号**（原文明说"容差窗口内输出 + 延迟预算达标"，未定义失败时进程该说什么）。实测对照见阶段 C 收口报告 §5。
