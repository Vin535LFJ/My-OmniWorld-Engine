# 知识基线（Knowledge Baseline）— 八技术主题通览

> **Trust label: [Research → 知识基线]** — owner 2026-11-13 拍板："先将各领域的知识、原理、进度通一遍收集起来，再进行方向制定"。组织方式 = 按技术主题（owner 选定）；深度 = 原理+进度通览 + 每主题 2–3 篇关键文献精读（owner 选定"1和2"）；WorldFoundry 差分 ADR 并入本趟（已产出 [ADR-0017 草案](../../adr/0017-worldfoundry-differential.md)，Proposed 待 grill）。
> **只调研不裁决**：本目录不改 PLAN / CURRENT；方向决策走 grill → ADR-0016（预留号）。阶段 D 照常。
> 方法：8 个只读研究子代理并行，各写一份 note；仓库既有资产（13 项目卡 + 11 深报 + 两轮 11-13 note + 三条专题 note）只链接复用不重写；联网一手核验（arXiv 摘要/HTML/PDF 全文、官方 GitHub/文档站、HF 数据卡），未一手核验一律标"未核/转述"。
> **知识库治理副本**：owner 指定 `E:\AI-Tools\test_skill\knowledge-base\` 为知识库治理项目的存放处，本目录 8 份 note + 索引已同步副本（2026-11-13）；研究原件以本目录为准（research-queue / ADR 引用链在此）。

## 主题索引

| # | 主题 | 文件 | 一句话定位 | 本轮最重要发现 |
|---|---|---|---|---|
| T1 | 大模型与多模态基座 | [T1-foundation-models-agents.md](T1-foundation-models-agents.md) | LLM/VLM/Agent 作为"导演"：工具调用、代码生成、场景编排 | Genie 4 混合架构一手已核：导演有规划、**无权威状态存储**；失效模式收敛于"验证者不可靠"→外置确定性真值的必要性 |
| T2 | 世界模型（生成式/实时交互） | [T2-world-models.md](T2-world-models.md) | 技术路线分类、记忆机制、隐式 vs 显式状态分叉 | WRBench：外部记忆买访问率、买不到状态正确性；四阶段因果蒸馏成行业收敛配方；8GB 唯一全开放小底座候选 = ForgeWM（Wan2.1-1.3B，显存未公布） |
| T3 | 视频生成模型 | [T3-video-generation.md](T3-video-generation.md) | 离线/实时流式、声画同步、视频→4D 链路 | 长视频漂移有机制性根源（滑窗截断/rolling KV 遗忘，官方自认）；实时化全部 ≥24GB；8GB 最有希望路径 = LightX2V（Wan2.1 官方 README 列名 4060） |
| T4 | 具身智能与物理仿真 | [T4-embodied-ai-simulation.md](T4-embodied-ai-simulation.md) | VLA vs WAM、仿真器确定性、闭环评测 | VLA 动作头收敛到 flow matching + action chunking；MJWarp GPU 默认非确定（官方 FAQ 一手）；SmolVLA 450M = 8GB 最可行策略消费者候选（未实测） |
| T5 | 程序化生成与 3D 资产生产 | [T5-procedural-generation-assets.md](T5-procedural-generation-assets.md) | 规则图/参数化资产/seed 确定性、AI 写生成器 | 三大工业工具收敛同一抽象（DAG+参数+dirty 传播）但均无 ownership/confidence 语义；WROP 真值轨迹是 authored 关键帧非物理仿真；AI mesh 产死数据 vs AI 写生成器产活配方 |
| T6 | 3D 重建与空间智能 | [T6-reconstruction-spatial-intelligence.md](T6-reconstruction-spatial-intelligence.md) | 前馈/优化/生成式重建、3DGS、智驾视觉管线 | VGGT-Ω 官方端到端显存表取代旧基准面（8GB 可行性需重做）；"原生鱼眼世界模型"传闻部分证实部分证伪（OmniDreams 正文为针孔类 4 视角） |
| T7 | 渲染管线与 GPU 系统 | [T7-rendering-gpu-systems.md](T7-rendering-gpu-systems.md) | 光栅化/路径追踪/神经渲染、零拷贝、确定性、可观测性 | Marionette 同机同 seed stage1 byte-identical、跨机不可移植——位级 oracle 边界被公开实证画在几何/状态层；确定性归约几乎免费（<0.2%~7.8%），代价取决于非确定性所在层 |
| T8 | 评测与测量生态（主战场） | [T8-evaluation-measurement-ecosystem.md](T8-evaluation-measurement-ecosystem.md) | 五层评测金字塔、闭环协议、校准、位级 oracle | **修正上游关键主张**：WF Table 1 自身行 Closed loop ✓，"自认无闭环"作废；差分改打"闭环语义深度+状态契约+位级 oracle"；运行时层缺口经一手核验仍成立 |

## 跨主题收敛（事实陈述，非裁决）

1. **"验证者不可靠"是八主题共同病灶**：T1 agent 自评盲区 / T2 状态核缺失 / T3 漂移机制 / T4 完成率可被骗 / T5 生成器无验收契约 / T6 置信度语义漂移 / T7 跨机位级漂移 / T8 指标 Goodhart 家族——全部指向"外置、确定性、可测量的真值与状态层"。
2. **位级复现的边界已被外部实证画出**：Marionette（几何/状态层同机位级一致，外观层跨机分叉）与 MJWarp（确定性 opt-in 代价 −31%~−81%）共同说明：位级 oracle 应锚在**状态/几何层**，不锚在生成外观层——与仓库参考模式设计同构。
3. **8GB 本机可行性分层清晰**：评测环/程序化生成/离线短片段重建可行；实时生成式世界模型与实时流式视频生成全部出局（≥24GB 或 H100 级）；候选核验清单见各 note §6。

## 对方向评审（ADR-0016）的输入状态

- 知识地基：本目录 8 份 note（原理层补齐，W04 覆盖错位中"评测侧零深报"已由 T8 补上）。
- 差分义务：ADR-0017 草案已产出（11-20 截止前），待 owner grill。
- 仍未决（留给 grill，不在本目录裁决）：定位 v2 确认 / 首战 B' / 评审组织（三问见第二轮整合 note §6）；8GB 标的逐卡核验（ForgeWM 优先）是 B' 前置。

## 上游与下游

- 上游：[第一轮兴趣全景](../2026-11-13-interest-landscape-and-redefinition.md) → [第二轮六方向整合](../2026-11-13-six-direction-innovation-integration.md)（⚠️ 其 WF 闭环表述已被 T8 修正，见该文件修正附记）→ 本目录（知识地基）。
- 下游：grill → ADR-0016（方向）+ ADR-0017（差分，Proposed）；research-queue 已登记。