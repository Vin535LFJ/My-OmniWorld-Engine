# Regression: Stage C 切片 C4 — Depth-Anything-V2-Small 第二个模型家族入环（2026-10-19）

> C4 全部性能与能力主张的**唯一证据来源**。设计：[L2 C4](../../docs/design/stage-c-ai-in-loop/slices/04-depth-anything-v2-small-adapter.md)（路线 A，owner 2026-10-19 批准）；票据：[04-depth-anything-v2-small-adapter](../../.scratch/stage-c-ai-in-loop/issues/04-depth-anything-v2-small-adapter.md)；同族前作：[C3 报告](2026-10-19-stage-c-c3-rtdetrv2s.md)。

## Claim under test

**契约家族无关**：一个输出形态与检测框毫无关系的模型（稠密逐像素相对深度图）能否仍只经 `observe(const Observation&) → Proposal[]` 单文件入环、**核心零新增行**。要证四件事：① 稠密→稀疏归约确定且可复现；② 预处理与**该模型自己的**参考处理器同语义（不许从 C3 迁移）；③ 两个家族同面板时 per-producer 分解仍然工作（C5 判据 (c) 依赖它）；④ 深度在本世界的真实地位——**没有深度真值**，这条必须量化而不是嘴上承认。

## Environment（完整元数据）

| Item | Value |
|---|---|
| OS / CPU / GPU | Windows 11 build 26200 · i9-14900HX (24C/32T) 31.8 GiB · RTX 4060 Laptop 8 GiB（**本轮不可用**，同 C3） |
| CUDA 侧 | CUDA 12.6.20；**零 cuDNN** → CUDA EP 仍阻塞（L1 R-C1 的已知形态） |
| Compiler | MSVC 19.29.30133 (VS2019, x64) + CMake 3.20 + Ninja |
| ORT | `onnxruntime-win-x64-gpu-cuda12-1.31.0`（gitignored） |
| 模型 | `models/depth-anything-v2-small-onnx.onnx` **99,060,839 B**（`tools/fetch_models.mjs`；onnx-community/depth-anything-v2-small，Apache-2.0——仅 Small 尺寸宽松许可） |
| Commit | 基线 `d8c4691`（C4 L2 产出，design-review）；owner 批准头、实现与本报告在同一批未提交改动里 |
| Build ①（回归门） | `build.bat --test` → `build/`，ORT **OFF**，Debug |
| Build ②（**全部延迟数字来自此构建**） | `build-ort-rel/`：Release + `OW_WITH_ONNXRUNTIME=ON` |

## 复现命令

```bat
cmd //c build.bat --test                 rem OFF 14/14
cmake --build build-ort-rel && ctest --test-dir build-ort-rel   rem ON 16/16
set OW_DA_DEBUG=1                        rem 打印探测到的 I/O 与逐格统计
set OW_DA_TEST_IMAGE=<repo>\models\photo_640x480.rgb
build-ort-rel\ow_tests.exe slice14
build-ort-rel\omniworld_loop.exe --ticks 5 --seed 42 --adapters rtdetr_v2_s,depth_anything_v2_s ^
  --models zero_delta --mode performance --ep cpu --out both.json
build-ort-rel\omniworld_loop.exe --ticks 1 --adapters depth_anything_v2_s --ep cuda
```

## Results

### §1 构建矩阵

| 构建 | 套件 | 结果 |
|---|---|---|
| OFF（默认） | ctest 14 项 | **14/14 绿** |
| ON（Release） | ctest 16 项（新增 ow_slice14） | **16/16 绿** |

参考模式基线字面量（600/42 场景 `574361e62eec92b9` / `459ff85132cc103e`）在两种构建下均不变；`ow_slice03` 的注册表断言按 problem-log 010 的哨兵如期从 3 改到 **4**（ON 下 `depth_anything_v2_s` 排在 `rtdetr_v2_s` 之前，std::map 字典序）。

### §2 模型 I/O：探测结果 vs 设计期的未知

设计时**故意不写死形状**（L2 §2），运行期探测实测：

| 项 | 实测值 |
|---|---|
| 输入名 | `pixel_values` |
| 输入静态声明 | `[-1, 3, -1, -1]` → **H/W 是动态轴** |
| 输出名 | `predicted_depth`（唯一输出） |
| 输出运行期形状 | 秩 3：方形观察 → `[1, 518, 518]`；640×480 照片 → **`[1, 392, 518]`** |

`392 = 28 × 14`、`518 = 37 × 14` —— `keep_aspect_ratio` + `ensure_multiple_of: 14` 的参考处理器行为被复现（照片按长边 518 保比例，短边落到 14 的倍数）。

**这里踩到并修掉一个真实错误**：第一版用**会话期**元数据判断输出布局，动态轴下拿到的是 `-1` → 抛 `unexpected output extent`。教训 = 对导出图，静态元数据只够做"秩/通道"分类，**真实尺寸必须运行期从张量取**（`rtdetr.cpp` 本来就是这么读的，我没照做）。→ problem-log 011。

### §3 预处理：一手配置又一次推翻直觉

| 键 | DA-V2-Small（`DPTImageProcessor`，实测拉取） | 对比 C3 的 RT-DETR |
|---|---|---|
| `size` | 518×518 | 640×640 |
| `keep_aspect_ratio` | **true** | （无，强制方形） |
| `ensure_multiple_of` | **14** | （无） |
| `resample` | **3 = bicubic** | 2 = bilinear |
| `do_rescale` | 1/255 | 1/255 |
| `do_normalize` | **true**（ImageNet mean/std） | **false** |

**两个真实模型在同一阶段对"要不要归一化"给出相反答案。** 若把 C3 刚学到的"÷255 不归一化"当成品味迁移过来，C4 立刻错——这条已经写进 L2 §3.1 并进 problem-log。

实现取舍（记入 L2 偏差 #1）：双三次核取 Keys a=−0.5（Catmull-Rom），与参考处理器的 bicubic **同族不同参**；不声称与 PIL 位一致，只声称本项目自身可复现 + 语义同族。

### §4 稠密→稀疏：归约的实测行为（含一个被打脸的预期）

8×8 网格均值 → 帧内 min-max → 按秩取高于中位数的格。三种输入的真实结果（Release，CPU EP 单线程）：

| 输入 | 输出条数 | 格均值区间 | 最大格位置 |
|---|---|---|---|
| 合成场景 64×64（8 实体，footprint 0.02） | **32**（= 上界） | [1.875, 3.590]，spread 1.715 | row 7, col 7 |
| **纯灰帧（RGB 全 0x80）** | **32** | [0.795, 3.260]，spread 2.465 | — |
| 真实照片 640×480 | **32** | [1.403, 3.964]，spread 2.562 | row 5, col 5 |

**纯灰帧也产生 32 条结构化提案**——L2 §5 我为"退化帧 → 空列表"写的分支**在真实输入下一次都没触发**，而我在写测试时又差点把 `CHECK(a.empty())` 按直觉写上（幸好在跑之前按 problem-log 008 改成了"有界 + 确定性"）。这是 008 那条教训**在同一个切片内第二次复发**：模型不会把"没有信息"翻译成"没有输出"，ViT 会从位置编码里造出结构。008 已追加这条复现证据。

### §5 深度极性：测出来的，不是假设的

L2 §9 R-C4b 明确"极性未证实，不进任何断言前提"。实测（猫照：红沙发 + 两只虎斑猫 + 两个遥控器，前景在画面下半）：

- 高于中位数的 32 格里，**下半 27 格 / 上半 5 格**；下半平均置信度 0.859 > 上半 0.818；
- 最大格在 row 5 col 5（= 画面中下偏右，正是那只大猫的身体）。

→ **`predicted_depth` 值越大 = 越近**（相对逆深度 / disparity 形态）。测试断言的是"两半有区分度且都非空"（极性无关），方向作为**实测事实**记在这里。

### §6 延迟（Release，CPU EP 单线程，`observe()` 全程）

| 运行 | ticks | detect p50 | 逐 tick 均值 |
|---|---|---|---|
| 深度单独在环（performance, seed 42） | 5 | **1252 ms**（p95 1672） | 6472.7 / 5 = **1294.5 ms** |
| 两家族同面板（performance, seed 42） | 5 | 深度 **1228 ms** / RT-DETR **619 ms** | 9544.1 / 5 = 1908.8 ms |
| 两家族同面板（reference, seed 600） | 3 | 深度 1268 ms / RT-DETR 603 ms | 5581.6 / 3 = 1860.5 ms |
| 测试内直测（无 debug 打印） | — | — | 首调 **1318.0 ms**（含会话建立）/ 稳态 **1040.7 ms** |

（以上全部来自**收口评审修正后的最终二进制**；修正只动校验路径，热循环未变——同配置的 `world_hash` / `ledger_hash` / 各 `ece` 值与修正前逐位一致，见 §7。）

诚实标注：① 这是 `observe()` 全程（双三次预处理 + 推理 + 网格归约），不是纯推理；② **99 MB 的 ViT-S 稠密输出，CPU 单线程下约为 C3 检测器的 2.4 倍**（1252 vs 518 ms 单独对比）；③ 两家族同面板时 RT-DETR 从 518 → 619 ms，是共享进程的相互影响，不是谁变慢——**同面板数字不能与单独数字混引**；④ 参考路径**故意**单线程（确定性优先），这不是 CPU EP 最快配置，不能用来论证"必须上 GPU"。

### §7 确定性与家族无关（本片的正证）

| 主张 | 证据 |
|---|---|
| 适配器内确定性 | 同一 Observation 两次 `observe()` 逐字段相等（合成帧、灰帧各测一次） |
| 端到端确定性 | 同配置两次（3 ticks / seed 600 / 两家族 / reference）→ `world_hash=2c231ac4a54ff077`、`ledger_hash=9568233598d528eb`、`entries=123` **逐位一致**（墙钟 5.874 s vs 6.046 s，时间不进哈希） |
| 家族无关（接缝） | 两家族同面板跑通、`adapter_faults=0`、slice14 断言 `ece:rtdetr_v2_s` 与 `ece:depth_anything_v2_s` **同时存在** |
| per-producer 分解工作 | 同面板 `ece:rtdetr_v2_s=0.3898`、`ece:depth_anything_v2_s=0.6470`、`ece:zero_delta=0.5` —— B4 的分表在两个真实家族上仍可分解 |
| 参考模式字节不变 | OFF 14/14 + ON 16/16，600/42 基线字面量不动 |

`world_hash` 与 C3 单检测器运行**完全相同**（`2c231ac4a54ff077`）：世界演化与参与者无关，这是单一作者设计的直接体现。

### §8 深度的真实地位：量化，不是嘴上承认

本世界 y≡0、无深度真值（L2 §8 的路线 A 代价）。这不是抽象风险，跑一次就看得见（5 ticks / seed 42 / 深度单独在环）：

| 指标 | 深度单独 | 两家族同面板 | 对照（C3 仅 RT-DETR） |
|---|---|---|---|
| 账本条数 | 200 | 205 | 45 |
| applied / **announced** | 68 / **132** | 71 / **134** | 43 / 2 |
| `identity_stability` | **0.000000** | **0.000000** | 0.750000 |
| `divergence` | 0.0310 | 0.0322 | 0.0043 |
| 该生产者 ECE | 0.6470 | 0.6470 | — |

**32 个固定格心 / tick 的产出把 `identity_stability` 直接压到 0**：账本被"每 tick 在同一批位置上重新宣布"的条目淹没。这就是 L2 §8 说的"设计上不可能正确"的生产者在面板里的实际样子——**给 C5 的警告从推断升级为实测**（已同步进 C5 票据）。

**同时必须公平**：`ece:depth_anything_v2_s=0.6470` 不是"模型差"，是**置信度语义错配的度量结果**——它的 conf 是帧内 min-max 归一的**显著度**（L2 §9 R-C4d 已声明它不是概率，也不是"秩"，虽然保序等价），而 correct 判据是"格心是否碰巧落在某实体 0.1 半径内"。**修正 C4 票据里我写的一句过强判断**：深度 Proposal 并非"永远 `correct=false`"（`matched_error` 只要求 Matched 且 err≤0.1，格心可以**偶然命中**），准确说法是**其 correct 与置信度无因果关系，因而 ECE 无意义**。两者都指向同一个结论：不能进判据 (c) 面板。

**第二个场景点（seed 600 / reference / 3 ticks）**：`ece:depth_anything_v2_s = 0.6803`、`ece:rtdetr_v2_s = 0.4866`——同一生产者在两个场景下相差 0.033（深度）与 0.097（检测）。这正是 problem-log 006 的形态：**跨场景比 ECE 之前先比场景参数**。C5 定面板口径时，"用哪个场景测出的数"必须先固定并写进报告，否则分离量不可解释。

### §9 CUDA EP（无新信息，如实记录）

`--ep cuda` 在深度适配器上表现与 C3 完全相同：依赖层 Error 127（缺 cuDNN 9 / CUDA 12.8+）→ 隔离层 → `adapter_faults=1` + `first_fault` 带完整原因与指引（实测已确认写入 `ow_c4_cuda.json`）。CMake 的 provider DLL 拷贝（C3 修）对新适配器同样生效。

## 票据验收裁定

| 票据 AC | 裁定 | 依据 |
|---|---|---|
| DA-V2-Small 单文件接入；核心零改动 | ✅ **PASS（严格成立）** | 新增 `src/perception/depth_anything.cpp` 一个文件；`git diff` 核心模块**零新增行**（driver/observation/belief/metrics 未动）——C3 的 PARTIAL 没有变成常态 |
| 深度输出的契约映射在三态可见性 / Proposal 语义内解决 | ✅ PASS（路线 A） | §5/§7：NewEntity + transform + 置信度，**零语义扩展、零 ADR**；路线 B（观测增强）按票据规则留待 Phase 3 |
| 容差窗口声明 + 延迟预算路径同 C3 | ⚠️ **PARTIAL（同因）** | CPU EP 抖动实测 0 → 窗口退化为逐位精确；CUDA EP 窗口仍待 cuDNN，移交 C5 |
| git diff = 单文件 + build/文书 | ✅ **PASS（严格）** | 1 源文件 + CMake 2 行 + tests + 文书；无核心改动 |
| slice14 测试：映射行为 + 确定性 + 容差 | ✅ PASS | ON 16/16；含"两家族同面板"与"有界 + 确定性"断言 |

**C4 verdict：契约家族无关成立——稠密深度图与稀疏检测框走同一条接缝、核心零新增行、per-producer 分解在两个真实模型上仍可分辨（0.3898 vs 0.6470）。深度的地位被量化而非辩护：它把 identity_stability 压到 0，因为本世界没有深度真值；这是路线 A 的预期代价，不是缺陷，且已作为实测证据交付给 C5。**

## Findings 与排期

| # | 发现 | 归属 |
|---|---|---|
| H1 | 动态轴导出模型：静态元数据只够分类，真实尺寸必须运行期取 | 已修（problem-log 011） |
| H2 | 纯灰帧仍产出 32 条结构化提案——"空输出"预期在模型侧不存在 | problem-log 008 追加复现证据；C4 L2 §5 的退化分支保留但标注"实测未触发" |
| H3 | 预处理常数**不许跨模型迁移**（DA-V2 要归一化，RT-DETR 不要） | L2 §3.1 + 本报告 §3；C5/后续任何新模型开工前先拉一手配置 |
| H4 | 深度生产者在面板里把 `identity_stability` 压到 0（实测） | **C5 判据 (c) 面板口径必须先定**（已写进 C5 票据） |
| H5 | 我此前"深度条目永远 correct=false"的判断过强（格心可偶然命中） | 已在 §8 修正，并同步更正 C4/C5 票据措辞 |
| H6 | CUDA EP 仍缺 cuDNN 9 / CUDA 12.8+ | owner 侧；C5 标定窗口的前置 |

## 收口评审（Standards + Spec 两轴）与本轮修正

按 AGENTS 规则 #5 对实际 diff 做两轴审查。**这一轮审出一个真实缺陷**（不是措辞问题）：

| 轴 | 发现 | 处置 |
|---|---|---|
| Spec | **真实缺陷**：若某网格收不到像素（`mh` 或 `mw` < 8），`cell[i] /= cell_n[i]` 是 `0/0` → NaN；NaN 使 `v <= median` 判假 → **产出 conf=NaN 的提案**，违反契约⑦ 的 [0,1]，而 `kDegenerateEps` 拦不住它 | 运行期校验加 `mh,mw ≥ kGrid`（同时保证每格至少一像素），并记入偏差 #2 |
| Spec | 输出布局校验过宽：`[1,4,H,W]` 会落进 `n_elems/4` 边界被**静默按单通道读取** | 改为精确等式 `n_elems == mh*mw` + 秩 4 强制 `shape[1]==1` |
| Spec | §5 行 2 要求异常含"实际形状与张量名"，实际只印了形状 | 两处抛错统一带上张量名 |
| Spec | §5 行 1 要求退化时"debug 打印退化原因"，实现直接 return | 补打印 |
| Spec | §6 测试 1 要求打印探测到的 I/O，但只在 `OW_DA_DEBUG` 下打印 → 普通 ctest 永远看不到 | 探测行改为**无条件打印一次**（每进程一次，非噪声） |
| Spec | §3.1 写"最短边缩到 518 档"，实现与本报告 §2 都是**长边**锚定 | 更正 L2 措辞（实现正确） |
| Spec | §3.2/§8/票据把 min-max 归一显著度称作"归一化秩" | 三处措辞按实现更正（保序等价，但不是秩） |
| Spec | §8/§9 与 C5 预警写"永远 `correct=false`"——过强（格心可偶然命中） | 全部改为"correct 与置信度无因果关系 → ECE 无意义"（H5） |
| Spec | `same_props` 只比 producer/conf/x/z，报告却写"逐字段相等" | helper 补 `tick_observed` / `target` / `y`，措辞转为成立 |
| Standards | `map_shape_` 成员被赋值却从不读取（v1 bug 的残留） | 删除 |
| Standards | 测试注释声称断言了"退化分支给出空列表"，实际无此断言 | 注释按实测改写（纯灰帧 32 条） |
| Standards | ORT 会话胶水与 `rtdetr.cpp` 重复；`load_photo`/`same_props` 与 slice13 重复 | **不抽公共层**：前者是单文件不变式的自觉代价、后者由每片自包含约定许可；两处均登记为已知重复（L2 偏差 #9/#10），并记下已出现的漂移 |
| Standards | 报告把 `d8c4691` 说成"L2 批准头"（它是批准**前**的 design-review 提交） | 更正为"L2 产出；批准头与实现同批" |

修完复跑：ON 构建 **16/16 绿**、OFF 默认构建 **14/14 绿**、`--ep cuda` 与"模型文件缺失"两条失败路径的 `first_fault` 均实测带原因。
