# 2026-11-05 方向评审决策输入摘要（W04 首轮生态调研）

> **用途**：为 owner 的方向评审（Genie 4 之后的 A/B 抉择）提供一手证据综述。依据 = `docs/17-research/projects/` 下 11 张项目卡（2026-11-05 首轮 Tier 1，规格见 [`workflows/04-ecosystem-survey.md`](../../workflows/04-ecosystem-survey.md) §8）。
> **纪律**：结论只引用各卡已核验（verified）的事实，每条标注依据的卡路径与关键来源域名；**UNVERIFIED 的事实全部只进文末「证据缺口」小节，不进任何结论**。本文是证据综述，**裁决权归 owner**。
> **问题清单**（owner 方向问题，即本次汇总 ask 给出的 Q1–Q4）：Q1 目标A 创作产品 vs 目标B 开发者运行时；Q2 生成式 / 结构化 / 混合式；Q3 第一批用户；Q4 第一应用。
> **判定基准**：文内「本轮证据归纳」仅为证据归纳，非裁决。置信度判定基准——**高** = ≥3 个独立来源（项目卡或卡+ADR）且无反证；**中高** = 2 个独立来源；**中** = 单一来源或间接支撑。
> **文内指代**：阶段 A–D 与 D2/D3/D5 指 PLAN.md / CURRENT.md 的阶段划分及阶段 D 切片（D2 = 帧序列供给通路，D3 = 标注装载为真值，D5 = GPU 数据流与零拷贝；当前阶段 D 进行中，见 [`CURRENT.md`](../../CURRENT.md)）。
> **2026-11-14 终裁标注**：本文 Q1/Q2/Q4 的证据归纳已经 owner grill 终裁采纳——Q1=开发者运行时、Q2=混合式、Q4=评测闭环为第一应用，见 [ADR-0016 D1.5](../adr/0016-direction-v2-world-state-runtime.md)；**Q3（第一批用户）未终裁**，仍是期权触发器问题（外部信号出现才启动）。本文其余内容保留原文作证据存档。

---

## Q1：目标A 创作产品 vs 目标B 开发者运行时

本轮证据归纳（非裁决）：**目标B（开发者运行时）是开放的本体位；目标A（创作产品）在本轮 11 张卡覆盖范围内全部由闭源玩家占据（未穷尽全生态），只能作为下游期权。**

- 创作产品赛道存在且需求真实，但全是闭源黑箱：Genie 4（2026-10-27 公告）的 trusted tester 场景覆盖 previsualization / 游戏原型 / 机器人仿真，证明实时生成世界的产品需求存在；但它闭源、无权重、无公开 API、无任何导出（mesh/Gaussian/视频下载）能力，官方承认数分钟漂移、不可复现——**无法在它上面构建可测量的系统**。依据：`docs/17-research/projects/genie-4.md` §1/§2/§5，来源域名 deepmind.google / blog.google / github.com。
- 开发者基础设施赛道是开放的：OpenUSD 是 AOUSD 标准化的场景描述/组合标准（"数据模型 + 文件语义，非运行时"），是我们的单向导出目标；Rerun 是 Apache-2.0 系的"物理 AI 数据层"SDK（entity path × timeline 双轴）。两者都是开发者可接入、可分叉、可审计的开放生态。依据：`docs/17-research/projects/openusd.md`（openusd.org / aousd.org / github.com/PixarAnimationStudios/OpenUSD）、`docs/17-research/projects/rerun.md`（rerun.io / github.com/rerun-io/rerun）。
- 开放权重的世界模型（Cosmos 3，OpenMDW-1.1）是开发者模型而非消费产品：官方支持线为 Linux only，卡内核验的最小平台为 Jetson Thor T2000 16 GB（`cosmos-3.md` §4/§6，核验日期 2026-11-05，来源 nvidia.com）；本仓库开发机为 RTX 4060 Laptop 8 GiB（D1 环境核验笔记实测，见 [`docs/17-research/2026-10-26-stage-d-environment-and-licenses.md`](2026-10-26-stage-d-environment-and-licenses.md)），低于官方支持线故不可跑，只能离线生产数据 / 充当被评对象。依据：`docs/17-research/projects/cosmos-3.md` §0/§4，来源域名 nvidia.com / research.nvidia.com / github.com/NVIDIA/Cosmos / openmdw.ai。
- 既有方向决策已裁定：全生态"所有人都在发模型/帧/资产；没人发可分享的活世界状态运行时层"——空档在开发者运行时，不在创作产品。依据：`docs/17-research/2026-09-21-direction-decision.md` §4；该裁定被 openusd 卡按官方文档逐条复核成立，复核记录见 `docs/17-research/projects/openusd.md` §2 与 §6。

## Q2：生成式 / 结构化 / 混合式

本轮证据归纳（非裁决）：**混合式**——生成式做感知与内容生产，结构化可测量世界状态做主干，确定性仿真做外部真值。

- 连闭源领跑者自己都宣布走向混合：Genie 4 官方 "Future work: Towards hybrid world models"，独立印证"生成式语义 + 确定性约束"的混合架构方向（仓库既有全景笔记 [`docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md`](2026年实时世界运行时与生成式世界模型技术全景报告.md) §"神经生成与确定性运行时的双向融合演进机制" 持同一结论）。依据：`docs/17-research/projects/genie-4.md` §1/§6，来源域名 deepmind.google。
- 我们的观察生产者全部是"生成式模型 → 结构化带置信度输出"：SAM 2 逐帧掩码（obj_id 语义恰说明 identity/confidence 必须引擎侧接管）、VGGT 一次前馈输出相机/深度/点图/轨迹且自带逐像素置信度、GVHMR 输出世界坐标 SMPL-X 序列——三者输出都落 World Contract 的 time/identity/confidence。依据：`docs/17-research/projects/sam-2.md`（ai.meta.com / github.com/facebookresearch/sam2 / arxiv.org）、`docs/17-research/projects/vggt.md`（arxiv.org / github.com/facebookresearch/vggt）、`docs/17-research/projects/gvhmr.md`（arxiv.org / github.com/zju3dv/GVHMR / zju3dv.github.io）。
- 物理座位是结构化/确定性的：Newton / MuJoCo-Warp（Apache-2.0，Linux Foundation 治理）是外挂 GPU 物理执行后端，仿真器 = 外部真值权威；其 GPU 确定性与重放声明正是我们的测量生态位。依据：`docs/17-research/projects/newton-mujoco-warp.md` §6，来源域名 newton-physics.github.io / github.com/google-deepmind/mujoco_warp / developer.nvidia.com / linuxfoundation.org。
- 生成式世界模型（Cosmos 3）在本仓库的位置是离线数据生产者 + Phase 4 被评对象，不进产品路径——即"生成式进数据，结构化进状态"的分工。依据：`docs/17-research/projects/cosmos-3.md` §6。

## Q3：第一批用户

本轮证据归纳（非裁决）：**第一批用户是交互式世界模型的开发者 / 评测社区**（需要可测量闭环 harness 的人）；其次、赛道级推断为机器人仿真/具身训练开发者（`isaac-sim.md` 的机器人合成数据生态与 `newton-mujoco-warp.md` 的 robot learning 定位指向该人群；本轮无直接用户证言，属赛道推断）；暂不是创作产品的终端消费者。

- ADR-0008 已定"评测是第一个应用"；评测社区的痛点是各家（WBench/WorldMark/WorldArena/KineBench 等）各自重写 action adapter、state schema、sim 耦合——这正是开发者用户画像。依据：`docs/adr/0008-direction-reopen-gpu-native-world-engine.md`；社区格局见 `docs/99-archive/2026-09-28-docs-slim/01-architecture/decisions/ADR-0007-project-direction.md`（被 [`docs/17-research/research-queue.md`](research-queue.md) 第 9 行「WorldFoundry & cosmos-evaluator differential」条目引用盯梢）。
- 开放权重的被评对象正在成形：Cosmos 3 是本轮 11 卡范围内唯一开放许可 + 可下载权重 + 全工作流（含 forward dynamics）的候选被评对象，其官方 Evaluator（PAIBench / Physics-IQ 口径）是差异化参照的指标对照；据 cosmos-3 卡 §6 的对比，Genie 3 / Marble 不开放权重、评不了（二者未在本轮 11 卡中独立建卡，该对比引自 cosmos-3 卡）。依据：`docs/17-research/projects/cosmos-3.md` §6，来源域名 nvidia.com / research.nvidia.com。
- 评测管线的"生产者"侧也已就位：VGGT 的几何输出可作评测管线的伪真值生产者（"评测是第一个应用"语境下的正用），SAM 2 落在 Observation 契约的真实视频半边。依据：`docs/17-research/projects/vggt.md` §6.3（github.com/facebookresearch/vggt）、`docs/17-research/projects/sam-2.md` §6.1（ai.meta.com）。
- 创作侧需求（previsualization / 游戏原型 / 机器人仿真）在本轮调研范围内只被闭源产品满足，未见开放替代品（不排除 11 卡之外的开放项目）。依据：`docs/17-research/projects/genie-4.md` §1，来源域名 deepmind.google。

## Q4：第一应用

本轮证据归纳（非裁决）：**第一应用 = 世界模型评测闭环（Grounding Loop 在合成+真实面板上的可测量评测）**，与 ADR-0008 一致；角色动画是独立侧项目，不在本引擎路线。

- 评测闭环的各席都有卡背书：被评对象候选（Cosmos 3，开放权重）、伪真值/观测生产者（VGGT 几何、SAM 2 分割）、证据工具链（Nsight Systems / CUPTI 给 D5"拷贝次数 + 同步点"判据产出可复现证据并关联 tick↔GPU）。依据：`docs/17-research/projects/cosmos-3.md`、`docs/17-research/projects/vggt.md` §6、`docs/17-research/projects/sam-2.md` §6、`docs/17-research/projects/nsight-cupti.md` §6（developer.nvidia.com / docs.nvidia.com）。
- 真实数据入环路径（D2/D3 面板纪律）与 Isaac Sim 的 Replicator"传感器 → 标注 → COCO/KITTI"管道同构，证明"标注格式对齐评测生态"是仿真器侧的成熟做法，可作对照设计。依据：`docs/17-research/projects/isaac-sim.md` §6（docs.isaacsim.omniverse.nvidia.com / isaac-sim.github.io）。
- 侧项目边界清晰：Wan2.2-Animate 与 GVHMR 服务角色动画路线（独立项目，队列已登记"不与阶段 D 抢资源"），任何接入都不在引擎产品路径。依据：`docs/17-research/projects/wan22-animate.md` §6（github.com/Wan-Video/Wan2.2 / huggingface.co/Wan-AI）、`docs/17-research/projects/gvhmr.md` §6、`docs/17-research/research-queue.md` 角色动画路线条。

---

## 证据缺口（UNVERIFIED 事实，不作为决策依据）

以下为各卡内标注 **UNVERIFIED** 的事实（来源不可达或互相矛盾，卡内已说明原因；检索日期均为 2026-11-05）：

- `projects/genie-4.md`：分辨率/帧率/延迟官方数字（公告未公布）、交互时长具体上限、Gemini API/AI Studio 的 Genie 端点形态/定价/速率、模型级使用许可条款、任何导出/资产格式能力、本地运行所需 GPU 规格。
- `projects/wan22-animate.md`：Animate 专项显存数字（官方从未发布）、Windows 可行性（官方无声明）、Wan-Animate-2 的 I/O 契约与 diffusers 集成质量（论文号占位符互相矛盾）、托管 API 单价、训练数据许可（不存在可引用文本）。
- `projects/cosmos-3.md`：因 huggingface.co 在本环境全域不可达——逐模型显存 GB 数字、Cosmos Datasets / Physical AI Datasets 逐数据集许可标签、technical-report.pdf 内容（文件存在但本环境无法解析）、HF Diffusers pipeline 文档页细节（pipeline 类名 `Cosmos3OmniPipeline` 已由 GitHub README Quickstart 直验）。
- `projects/vggt.md`：能否自行导出 ONNX、官方 Windows/驱动声明、8 GB 显存可行性（推断，未实测）、商业检查点 HF LICENSE 与 GitHub LICENSE.txt 的逐字一致性。
- `projects/gvhmr.md`：论文无 Limitations 章节（遮挡/快速运动边界未被官方承认或否认）、权重独立许可文本（仅整包条款）、推理显存（社区自测，非官方）、Windows 跑通性。
- `projects/sam-2.md`：§7 未解决问题清单（开放项，标 UNVERIFIED）。
- `projects/openusd.md`：官方性能资产各自的分发许可条款、OpenExec 首入版本号（v26.08 release body 为空，未寻到官方发布说明）。
- `projects/newton-mujoco-warp.md`：MJWarp 官方 Windows 支持（未列 OS 矩阵）、官方显存数字（官方无任何数字）、Newton 示例资产逐项许可、LF 公告未指明的具体治理子实体。
- `projects/rerun.md`：Rerun Hub 法律条款文本、`rerun_cpp_sdk.zip` 是否附带与仓库一致的 LICENSE。
- `projects/nsight-cupti.md`：CUPTI 追踪开销的官方量化数字、Nsight Systems 独立版定价/免费的官方声明、UserGuide 内存操作时间线行的逐字 UI 文档、CUPTI 库版本 ↔ CUDA 版本官方对应表、CUPTI External Correlation 在 nsys 导出 SQLite 层是否落表（API 层已核验）。
- `projects/isaac-sim.md`：本机驱动差距（12.6 vs 官方测试驱动 595.x）的具体影响、NuRec/Replicator 内置 AI 模型的权重许可条款、拿到 ≥16 GB GPU/云实例后的可行性（待实测）。

注：本次汇总 ask 所给清单中 11 张卡的卡级 verified 均为 true；但卡级 verified ≠ 卡内每条事实都已核验。以上为卡内事实级 UNVERIFIED 项，不进结论。

---

## 推荐倾向 + 置信度（证据综述，裁决归 owner）

- **Q1 倾向目标B 开发者运行时**（创作产品作为下游期权而非本体）——置信度 **中高**。依据：genie-4 / cosmos-3 / openusd / rerun 卡 + `2026-09-21-direction-decision.md`。
- **Q2 倾向混合式**（生成式感知/生产 + 结构化可测量世界状态 + 确定性仿真后端）——置信度 **高**。依据：genie-4 官方 hybrid 声明、sam-2 / vggt / gvhmr / newton-mujoco-warp / cosmos-3 卡。
- **Q3 倾向第一批用户 = 交互式世界模型开发者 / 评测社区**（其次机器人仿真开发者）——置信度 **中**。依据：ADR-0008、cosmos-3 / vggt / sam-2 / genie-4 卡。
- **Q4 倾向第一应用 = 世界模型评测闭环**——置信度 **高**。依据：ADR-0008 + vggt / sam-2 / cosmos-3 / nsight-cupti / isaac-sim 卡。

> 本摘要只做证据综述与倾向归纳；方向裁决权在 owner（见 [`workflows/README.md`](../../workflows/README.md) 衔接图：W04 产出喂给 grill / 对抗评审 / ADR）。
