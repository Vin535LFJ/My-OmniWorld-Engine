# 04: Depth-Anything-V2-Small 适配器（C4）

**What to build:** 第二个真实模型家族（深度估计）经同一接缝单文件接入——证明契约家族无关（检测 + 深度两类参与者走同一条路）；深度图 → 观测增强或 Proposal 语义的映射设计（若超出 v0.1 冻结语义 → 升 ADR 走 v0.2，不硬改）。

**Blocked by:** 03（RT-DETRv2-S 模式先立）

**Status:** done — 2026-10-19（路线 A 经 owner 批准；证据见 [C4 regression 报告](../../../benchmarks/regression/2026-10-19-stage-c-c4-depth-anything.md) 与 [L2 偏差记录](../../../docs/design/stage-c-ai-in-loop/slices/04-depth-anything-v2-small-adapter.md)）

**流程：** 预授权执行（同 C1 授权链）。

- [x] DA-V2-Small 单文件适配器接入；核心零改动 → **严格成立**：`src/perception/depth_anything.cpp` 一个文件，driver/observation/belief/metrics **零新增行**
- [x] 深度输出的契约映射设计（在三态可见性 / Proposal 语义内解决；需语义扩展则走 ADR）→ 路线 A：稠密→8×8 网格秩归约→`NewEntity`+transform+置信度；**零语义扩展、零 ADR**
- [x] 容差窗口声明 + 延迟预算路径同 C3 → CPU EP 抖动实测 0（逐位精确）；CUDA EP 仍缺 cuDNN 9，窗口移交 C5
- [x] git diff = 单文件 + build/文书 → 1 源文件 + CMake 2 行 + tests + 文书
- [x] slice14 测试：映射行为 + 确定性 + 容差 → ON 构建全量 **16/16**、OFF **14/14**

## Comments

- 2026-10-19 · **L2 已产出**：[`docs/design/stage-c-ai-in-loop/slices/04-depth-anything-v2-small-adapter.md`](../../../docs/design/stage-c-ai-in-loop/slices/04-depth-anything-v2-small-adapter.md)。§8 是需 owner 裁定的契约级选择（A：稠密深度 → 稀疏 Proposal，零契约改动 / B：观测增强路线，需升 ADR）。**未批不动工。**
- 2026-10-19 · **模型 I/O 实测**（从 ONNX 字符串表提取，非推断）：输入名 `pixel_values`、输出名 `predicted_depth`；张量形状**没有**从文件读出，留给 slice14 测试 1 运行期探测并打印（避免把猜测写成常量）。
- 2026-10-19 · **参考处理器一手配置**（`onnx-community/depth-anything-v2-small` 的 `preprocessor_config.json`，`DPTImageProcessor`）：`size` 518×518 / `keep_aspect_ratio` true / `ensure_multiple_of` 14 / `resample` **3 = bicubic** / `do_rescale` 1/255 / **`do_normalize` true（ImageNet mean/std）**。**与 RT-DETR 恰好相反**（那片是 `do_normalize: false`）——预处理常数不允许跨模型迁移，这条是本片最可能踩的坑。
- 2026-10-19 · **两条硬事实（决定设计边界，已核对代码）**：① 真值世界是 y≡0 的 x/z 平面、`Entity` 无尺寸成员（`src/truth_world/truth_world.cpp:22-25`）→ **没有深度真值**；② 适配器收 `const Observation&`（`src/perception/adapter.hpp:19`）且 `observation.visibility` 在 `src/`+`apps/` **无任何消费者**（只有测试读）→ "观测增强"路线必须动数据方向 = 契约级变更。
- 2026-10-19 · **给 C5 的预警**（L2 §8/§9 R-C4c；措辞经实现期自纠修正）：`matched_error` 只要求裁决为 Matched 且 err ≤ 0.1，固定格心**可以偶然命中**实体——所以准确说法不是"永远 `correct=false`"，而是**其 correct 与否与置信度无因果关系 → 该生产者的 ECE 无意义**（实测 `ece:depth_anything_v2_s = 0.6470`，且它把 `identity_stability` 压到 **0.000000**）。两种说法指向同一结论：**不得进判据 (c) 分离量面板**。已同步进 C5 票据。
- 2026-10-19 · **TDD 红相位**：slice14 先注册后失败——`ow_slice03` 哨兵按 problem-log 010 预言变红（`names.size()` 3 ≠ 4），slice14 因适配器缺席而崩。崩溃形态本身又是一条证据：**未捕获异常 = 静默 fastfail**（同 C3 的 G6）。
- 2026-10-19 · **绿相位实测**（Release `build-ort-rel/`）：输入探测 `[-1,3,-1,-1]`（动态轴）、输出 `predicted_depth` 运行期 `[1,518,518]`（方形观察）/ `[1,392,518]`（640×480 照片，`392 = 28×14` → keep-aspect + multiple-of-14 行为被复现）。
- 2026-10-19 · **深度极性 = 实测**：猫照下半 27 格 / 上半 5 格高于中位数，最大格落在大猫身体 → **值越大越近**。测试断言保持极性无关，方向只作为测量结果入报告 §5。
- 2026-10-19 · **problem-log 008 在同一切片内复发**：我为"纯平帧 → 空列表"写了行为行，写测试时又差点 `CHECK(a.empty())`；实测**纯灰帧照样 32 条结构化提案**（spread 2.465）。断言改为"有界 ≤32 + 确定性"。
- 2026-10-19 · **踩坑并修**：第一版用会话期静态元数据取输出 H/W，动态轴下全是 −1 → `unexpected output extent`，且测试无异常出口 → 进程静默消失。修法 = 静态只分类、运行期取真实尺寸 + 测试加 `try_observe()`。→ problem-log 011。
- 2026-10-19 · **家族无关的正证**：两家族同面板跑通、`adapter_faults=0`、`ece:rtdetr_v2_s=0.3898` 与 `ece:depth_anything_v2_s=0.6470` 同时存在；两次同配置运行 `world_hash=2c231ac4a54ff077` / `ledger_hash=9568233598d528eb` 逐位一致。
- 2026-10-19 · **延迟**：`observe()` 全程（预处理+推理+归约）稳态 **1097.8 ms**、逐 tick p50 **1262 ms**（单独在环）/ 1329 ms（同面板）；同面板下 RT-DETR 从 518 → 632 ms，**同面板数字不与单独数字混引**。
- 2026-10-19 · **新坑**：2 条（problem-log 011 新增；008 追加复发证据）。
- 2026-10-19 · **两轴收口评审**（AGENTS 规则 #5）：审出**一个真实缺陷**——网格若收不到像素则 `0/0` → NaN 置信度（NaN 使 `v <= median` 判假，会产出 conf=NaN 的提案、违反契约⑦）；修法是把运行期校验收紧为"单通道精确等式 + `mh,mw ≥ 8`"，同时消灭该路径。另修：多通道图会被静默按单通道读取、异常缺张量名、退化分支未打印原因、I/O 探测行只在 debug 下打印、`same_props` 漏比 tick/target/y、死成员 `map_shape_`、测试注释声称了不存在的断言；文档侧更正 4 处措辞与实现不符（最长边锚定、"秩"→显著度、"永远 correct=false" 过强、`d8c4691` 定性）。**ORT 胶水与测试 helper 的重复经论证后保留**（单文件不变式 / 每片自包含约定），登记为 L2 偏差 #9/#10。全部数字已在修正后的最终二进制上复测。
- 2026-10-19 · **修后复跑**：ON **16/16**、OFF **14/14**；同配置哈希与修正前逐位一致（改动只在校验路径）；`--ep cuda` 与"模型文件缺失"两条失败路径的 `first_fault` 均实测带原因。
