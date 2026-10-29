# Regression: 阶段 C 收口 — 判据 (c) 重测 + 延迟预算汇总（2026-10-19）

> 阶段 C（C1–C5）全部性能与指标主张的**汇总证据页**；单片细节在 [C3 报告](2026-10-19-stage-c-c3-rtdetrv2s.md) 与 [C4 报告](2026-10-19-stage-c-c4-depth-anything.md)。设计：[L1](../../docs/design/stage-c-ai-in-loop/概要设计.md) + [C5 L2](../../docs/design/stage-c-ai-in-loop/slices/05-criterion-c-remeasure-and-stage-report.md)。票据：[05](../../.scratch/stage-c-ai-in-loop/issues/05-criterion-c-remeasure-and-stage-report.md)。
> **owner 已定口径（2026-10-19，批准 C5 L2 时）**：判据 (c) 如实裁定 PARTIAL + 完整论证；CUDA EP 窗口保持 PARTIAL，不拿别人机器的数字填表。

## Claim under test

两件事：① **判据 (c)**——"ECE 能判别'接近校准的正常模型'与'注入错误置信度的模型'"，旧基线分离量 0.9617，阶段 A/B 两轮重测为 PARTIAL（0.189 / 0.184 < 0.30，problem-log 006/007 定位为**场景**而非机制），当时的说法是"真实模型进场才有关闭条件"。本切片检验这个说法。② **阶段 C 出口判据**（PLAN §六 Phase 2 行）逐条对照。

## Environment（完整元数据）

| Item | Value |
|---|---|
| OS / CPU / GPU | Windows 11 build 26200 · i9-14900HX (24C/32T) 31.8 GiB · RTX 4060 Laptop 8 GiB（**本轮未用**：CUDA EP 阻塞） |
| CUDA 侧 | CUDA 12.6.20 + **零 cuDNN**；ORT 1.31 CUDA EP 需 cuDNN 9 / CUDA 12.8+ |
| Compiler | MSVC 19.29.30133 (VS2019 x64) + CMake 3.20 + Ninja |
| Build（指标与延迟数字） | `build-ort-c5/`：Release + `OW_WITH_ONNXRUNTIME=ON` |
| Build（回归门） | `build/`：`build.bat --test`，ORT **OFF**，Debug |
| Commit | 基线 `5400af4`（C5 L2 批准）+ 本批未提交改动 |

⚠️ **构建目录迁移说明**：`build-ort-rel/` 本轮被 Windows Search 索引器反复咬住——`mt.exe` 嵌入清单时报 `c101008d` / 退出码 `0x1f`（共享冲突），且**换一个全新目录同样发生、只是换目标文件**，重试第 1 次即通过。这不是代码问题，也不是目录损坏；`build-ort*/` 已全部进 `.gitignore`。下次遇到：**先重试，别先怀疑链接器**。

## 复现命令

```bat
cmd //c build.bat --test                                  rem OFF：15/15（含 ow_slice16）
cmake --build build-ort-c5 && ctest --test-dir build-ort-c5   rem ON：17/17

rem P0 冻结合成面板（= slice16 的字面量来源）
build\omniworld_loop.exe --ticks 600 --seed 42 --out p0.json

rem P1 真模型 vs 注入者（20 ticks ≈ 12 s；600 ticks 需 ~6 分钟，见 §3）
build-ort-c5\omniworld_loop.exe --ticks 20 --seed 42 ^
  --adapters rtdetr_v2_s,blob_detect_overconfident --models zero_delta ^
  --mode reference --ep cpu --out p1.json

rem G6 / G7 退出码语义
build-ort-c5\omniworld_loop.exe --ticks 1 --adapters does_not_exist        rem exit 1 + 原因
build-ort-c5\omniworld_loop.exe --ticks 1 --adapters rtdetr_v2_s --mode performance --ep cuda  rem exit 1
build-ort-c5\omniworld_loop.exe --ticks 1 --adapters rtdetr_v2_s --mode reference   --ep cuda  rem exit 0（语义不变）
```

## §1 判据 (c) 重测：三个面板，一个结论

**面板口径先定义后测量**（problem-log 006 的纪律：跨场景比 ECE 之前先比场景参数）。

| 面板 | 参与者 | 参数 | 诚实/正常侧 | 注入侧 | **分离量** | vs 0.30 |
|---|---|---|---|---|---|---|
| **P0 冻结合成对** | `blob_detect` vs `blob_detect_overconfident`（+ 2 世界模型） | 600 ticks / seed 42 / 64² / footprint 0.02 / reference | **0.19890** | 0.01000 | **0.18890** | ❌ 未达 |
| **P1 真模型 vs 注入者** | `rtdetr_v2_s` vs `blob_detect_overconfident`（+ zero_delta） | 20 ticks / seed 42 / 同上 | **0.24165** | 0.01000 | **0.23165** | ❌ 未达 |
| **P2 深度生产者** | `depth_anything_v2_s` | — | **排除**（C4 报告 §8：其 correct 与置信度无因果关系） | — | — | 不参与 |

两个方向性事实，比分离量本身更重要：

1. **注入者在两个面板里都是 ECE 更低的那一个**（0.010 vs 0.199 / 0.242）。它"看起来最校准"，因为合成场景里它的准确率≈0.98，0.99 的自报恰好对得上——**这正是 problem-log 007 诊断出的结构，真实模型进场并没有改变它**。
2. **真模型的 ECE 不是校准量，是偶然命中量**。同一个 `rtdetr_v2_s`，换面板/换 seed 就换数：

| 运行 | ticks / seed | `ece:rtdetr_v2_s` |
|---|---|---|
| C4 报告 §7（两家族同面板，performance，seed 42） | 5 / 42 | 0.3898 |
| C4 报告 §8（reference，seed 600） | 3 / 600 | 0.4866 |
| **本轮 P1**（真模型 vs 注入者） | 20 / 42 | **0.2417** |

跨度 0.24–0.49，而它"检测"的是彩色方块场景里的整帧退化框（C3 §3），correct 取决于框心是否碰巧落在某实体 0.1 半径内。**一个随场景漂移 0.25 的量，不可能同时是"这个模型校准得好不好"的量。**

## §2 裁定：判据 (c) 维持 PARTIAL，且关闭条件被重新定位

**裁定 = PARTIAL（在环半条未达标；机制半条达标且未退化）。**

- **机制半条 ✅**：per-producer 分解存在、可分辨、对置信度扰动敏感（B2/B4 单测 + 本轮 P0 字面量复测确认未退化）。
- **在环半条 ❌**：需要"一个接近校准的真实模型"与"一个注入错误置信度的模型"在同一面板被分开。本轮证明**这个前提在本世界不可满足**——不是模型不够好，而是**真值与模型的定义域不相交**：真值世界是 y≡0 平面上的 8 个彩色方块，COCO 检测器在其中没有可对应的对象。
- **不做的事**：不把 0.30 调到 0.25 让它"过"（006/007 两轮已确立此纪律）；不把"数值上有分离"当成判据达标；不用深度生产者凑第三个面板。
- **关闭路径重新定位**：判据 (c) 的在环部分**不属于阶段 C 可关闭项**，移交 **Phase 3（真实数据入环）**——真实视频 + 真标注时，检测器的框第一次有可比真值，"接近校准的真实模型"这个席位才存在。阶段 C 的诚实产出是：**证明了为什么它现在不存在**。

## §3 延迟预算（阶段 C 出口判据的性能半条）

Release ON 构建，CPU EP 单线程（确定性约束，非最快配置），`observe()` 全程：

| 参与者 | 逐 tick p50 | 来源 |
|---|---|---|
| `rtdetr_v2_s`（640×640 输入） | **518 ms**（单独）/ 619 ms（同面板） | C3 §5、本轮 §1 |
| `depth_anything_v2_s`（518×518 输入） | **1252 ms**（单独）/ 1228 ms（同面板） | C4 §6 |
| 两家族同面板 | 1908.8 ms/tick | C4 §6 |
| 内建 blob 检测器 | < 1 ms | 阶段 A/B |

**由此得出一条约束设计的结论**：判据 (c) 的 600-tick 面板若换成真模型 = **6 分钟/面板**（本轮 P1 因此只用 20 ticks 并如实标注样本量）。这不是缺陷，是"真实模型进环"必须付的账；**性能模式延迟预算的 GPU 侧（CUDA EP）本机未测**，缺 cuDNN 9 / CUDA 12.8+ → 该判据保持 PARTIAL，窗口标定移交环境就绪后。

## §4 参考模式不变式（阶段 C 的铁律，逐条复验）

| 项 | 结果 |
|---|---|
| OFF 默认构建全量 ctest | **15/15 绿**（含新增 ow_slice16） |
| ON 构建全量 ctest | **17/17 绿**（slice13 + slice14 + slice16） |
| 600/42 基线哈希 | `world_hash=574361e62eec92b9` / `ledger_hash=459ff85132cc103e` / `entries=9746` —— **引入 ORT 与两个真模型后逐位不变** |
| P0 面板 ECE 字面量 | 0.19890412565780013 / 0.009999990463256836 / 0.4940744921482492，slice16 钉死（±1e-12，见 L2 偏差） |
| 真模型确定性 | C3 §4 两轴 + C4 §7 两轴（同配置两次运行逐位一致） |

slice16 **故意不断言"分离量 < 0.30"**：把 PARTIAL 写进测试会让一个未达标的判据伪装成绿灯（L2 §6 测试 3）。

## §5 CLI 错误语义两项（owner 批准，实测前后对照）

| # | 场景 | 修前（实测） | 修后（实测） |
|---|---|---|---|
| **G6** | `--adapters does_not_exist` | exit `0xC0000409`、stdout/stderr **零输出**、无工件 | **exit 1** + `error: unknown perception adapter: does_not_exist` |
| **G7a** | performance + `--ep cuda`（EP 失败） | exit 0，只有 `faults=1` | **exit 1** + `error: 1 adapter fault(s); first_fault: ...`，**工件仍写出**（`adapter_faults: 1` + `first_fault` 完整） |
| **G7b** | reference + `--ep cuda` | exit 0 | **exit 0（不变）**——参考模式语义未动 |
| **G7c** | performance 无故障 | exit 0 | exit 0 ✓ |

原则：**一次有参与者静默失败的运行不构成一次合格测量**，退出码是"这次能不能用"的唯一机器可读信号；隔离层"单适配器故障不 halt 循环"的契约属性原样保留（工件照写、原因照给）。

## §6 阶段 C 出口对照（PLAN §六 Phase 2 行 + L1 §1）

| 出口判据 | 裁定 | 依据 |
|---|---|---|
| 真模型 = 单文件适配器、核心零改动 | ✅ 达成（C3 为 PARTIAL、C4 严格） | C3 有 3 处与模型无关的通用核心改动（如实记录，不取平均）；C4 核心零新增行 |
| 参考模式位级复现保持 | ✅ 达成 | §4 全表 |
| 性能模式延迟预算实测（4060 8GB） | ⚠️ **PARTIAL** | CPU EP 侧完整（§3）；CUDA EP 侧环境阻塞，窗口与预算移交（owner 已确认此裁定） |
| 附带关闭判据 (c) 在环部分 | ⚠️ **PARTIAL + 论证** | §1/§2；关闭条件重新定位到 Phase 3 |
| 对外工件：延迟分解 benchmark 笔记 | ✅ 达成 | 三份报告（C3 / C4 / 本收口）+ 两份 L2 偏差记录 + problem-log 008–012 + ADR-0012 |

**阶段 C verdict：架构主张兑现——两个真实预训练模型（稀疏检测 + 稠密深度两个家族）经同一条 `observe() → Proposal[]` 契约入环，参考模式的逐字节 oracle 在引入外部依赖后仍然成立，且该独立性由默认 OFF 的构建层保证（ADR-0012）。两条 PARTIAL 都是**边界**而非缺陷：一条卡在缺 cuDNN 的机器上，一条卡在一个没有对象真值的世界里。**

## §7 模型清单与许可

| 模型 | 文件 / 大小 | 来源与许可 | 完整性 |
|---|---|---|---|
| RT-DETRv2-S（`rtdetr_r18vd` 导出） | `models/rtdetrv2-s-onnx.onnx` 82,572,357 B | `onnx-community/rtdetr_r18vd`，Apache-2.0（社区 ONNX 导出） | `tools/fetch_models.mjs` + SHA256 pin |
| Depth-Anything-V2-Small | `models/depth-anything-v2-small-onnx.onnx` 99,060,839 B | `onnx-community/depth-anything-v2-small`，Apache-2.0（**仅 Small 尺寸**宽松许可，Base/Large/Giant 为 CC-BY-NC-4.0） | 同上 |
| 真实照片测试资产 | `models/photo_640x480.rgb`（gitignored） | 许可由 owner 决定，不入库；解码工具 `tools/png_to_rgb.mjs` 已入库 | 重跑 SHA256 与实测所用文件一致 |
| ONNX Runtime | `third_party/onnxruntime-win-x64-gpu_cuda12-1.31.0/`（gitignored） | MIT | 可选依赖，默认 OFF |

## §8 阶段 C retro（做对了什么 / 什么该改流程）

**做对了（有证据支撑的，不是感受）**：

1. **每条主张都先有工件再有话**——三份报告 + 每条 AC 指向具体章节；owner 批准前不写代码（C4/C5 各停一次 gate），结果 C4 的路线 A/B 分叉是在**纸上**决定的，不是在代码里改出来的。
2. **两轴评审真抓到了缺陷**，不是一次仪式：C4 抓到 `0/0 → NaN` 置信度（会产出违反契约⑦ 的提案）与"多通道图被静默按单通道读取"；C3 抓到 ON 构建全量套件的潜伏断裂；本轮抓到"测试输出里印了被禁止的裁定字符串"和两处交叉引用错页。**评审的价值与它是否审自己写的代码无关**。
3. **失败被当成资产登记**：`first_fault`（009）、CLI 退出码（G6/G7）、退化分支永不触发（008 复发）——三条都是先撞墙、再补通道、再写进流程层（AGENTS 硬规则 #4）。

**该改流程（下一阶段的入口）**：

1. **判据的前置要在写 L1 时就验证语义，而不是等实现追上**——判据 (c) 挂着"等真实模型"跨了三个阶段，而它真正等的是"定义域相交"。→ 已写进 problem-log 012；阶段 D 的 L1 共创时先问这一句。
2. **模型侧断言的默认检查项**：写任何"应该输出空/零/无"之前先测一次。008 在两个切片里各犯一次，第二次是在**已经写下教训之后**——说明教训进文档 ≠ 进习惯，需要的是**评审清单里的一行**，不是记忆。
3. **测试矩阵翻倍的成本没有被预先计入**：C1 引入 ON 构建时没人说"那全量套件要跑两遍"，代价在 C3 才被发现。→ 已在 AGENTS #4 补规则，阶段 D 若再加配置轴（如 NVDEC 有/无）应**在 L1 阶段就把矩阵写清**。
4. **环境类失败要有登记处**：本轮 Windows Search 索引器导致 `mt.exe` 链接失败约 6 次，每次都要重新诊断。它不是代码问题，因此 problem-log 不收；但"重试即可"这种知识丢了就是半小时。**决定：把它写进本报告的构建说明（已在 Environment 段），并在 `docs/17-research/research-queue.md` 挂一条"构建环境陷阱"收集页是否值得建。**

**流程回填核对（SOP-7 步 5 / L2 §9.6）**：`docs/agents/workflow-manual.md` 逐节核对结果——可选依赖的全量套件纪律**已进 AGENTS 硬规则 #4**（更靠近执行点，无需在 manual 重复）；SOP-1 步 6"全量验证"的表述仍写单一 `build.bat --test`，**建议回填一句"存在多构建配置时逐个跑全量"**。该回填属文档级、不改语义，**留待 owner 确认后执行**（不擅自改流程手册）。

## §9 遗留（下一阶段入口）

| # | 项 | 归属 |
|---|---|---|
| L1 | CUDA EP 容差窗口 + GPU 延迟预算（缺 cuDNN 9 / CUDA 12.8+） | owner 环境就绪后；不阻塞 Phase 3 开工 |
| L2 | 判据 (c) 在环部分：需真实视频 + 真标注才有可比真值 | **Phase 3** |
| L3 | 深度进契约的"观测增强"路线（需 ADR，动观察数据方向） | **Phase 3**（那时深度才有几何可对） |
| L4 | 两适配器 ORT 胶水重复（L2 C4 偏差 #9 登记）；第三个模型出现时再评估抽取 | 机会性 |
| L5 | Windows Search 索引器与 `mt.exe` 清单嵌入冲突（§Environment）；建议把 `build-ort*/` 排除出索引 | owner 侧机器配置，不改仓库 |
