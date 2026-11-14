# 六方向创新点调研与整合判断（2026-11-13 第二轮）

> **Trust label: [Research → 方向裁决输入]**——owner 要求"全面重定义一次：先做各方向资料调研（特别是仓库既有资料与历史问答），找出各方向创新点，再整合判断、同步分析"。本文是第一轮 [兴趣全景梳理](2026-11-13-interest-landscape-and-redefinition.md) 的深化与修正：框架沿用，**新颖性主张按新证据打折**。只提案不裁决；不改 PLAN / CURRENT，阶段 D 照常。
> **方法**：两个只读子代理深挖仓库既有资产（projects/ 13 卡 + deep/ 11 篇 + 全景报告 + 09-21/09-24/11-05 决策链 + 闭环/动画/智驾三 note + NOTES.md），主对话联网补证（WRBench / MemoBench / WROP / WorldFoundry 更新 / 交互世界生成 45 条目录）。来源与核验状态见 §7。
> **owner 同日补充**：四层架构（L1 理解 / L2 构建 / L3 运行 / L4 表现 + 横向 World Contract + GPU Observability）被 owner 再次确认为有意义的骨架——本文 §4 直接采用它作为重定义的逻辑分层。

> **⚠️ 2026-11-13 第三轮修正附记（知识基线 T8 一手核验后）**：本文凡引用"WorldFoundry 对比表自认没有系统完全支持闭环"处（§0.3、§1 D2、§3 打折表、§5 为什么是它）**一律作废**——arXiv 2611.10771 PDF p.5 Table 1 中 WorldFoundry 自身行 Closed loop 标 ✓（定义="controls depend on prior outputs or environment feedback"），具身闭环路径真实存在（OpenVLA×LIBERO-10，每任务 1 episode）。差分论据改为"**闭环语义深度 + 世界状态契约 + 位级复现 oracle**"，运行时层缺口结论经一手核验后维持。权威口径见 [T8 知识基线 §3a.6/§8](knowledge-baseline/T8-evaluation-measurement-ecosystem.md) 与 [ADR-0017 草案](../adr/0017-worldfoundry-differential.md)。本文其余内容保留原文不改（合并纪律）。

---

## 0. 整合结论（与 owner 同步的判断）

1. **"很多都是通的"——已证实，且通点唯一。** 六个方向的一手资料各自独立指向同一个缺口：**持久、可测量、可复现的世界状态层**。六条独立证据：
   - Genie 4 官方"当前局限"：物体恒存性随时间漂移（[genie-4 卡](projects/genie-4.md)）；
   - WRBench（2026-06，9600 视频 / 23 模型）：《Current World Models Lack a Persistent State Core》——模型把世界当"跟拍镜头"，镜头转回时目标停在被抛弃时的状态，**事件在无观察期间不推进**；
   - Omniverse SimReady 工作流：OpenUSD 被明确用作"agent 之间的共享契约层"（2026-09 官方博客）；
   - Isaac Sim Replicator 的空白区："标注进环后的关联判定、置信度语义、逐 tick 账本"（[deep/isaac-sim §4](deep/2026-11-05-isaac-sim.md)）；
   - 角色动画侧项目：唯一值得长期投资的资产 = 运动数据层，"与 World Contract 同构（identity/time/ownership/confidence）"（[动画 note §6](2026-10-27-character-animation-route.md)）；
   - 量产 AVM：confidence 被藏在标定时刻的一次性假设里，鬼影 = 假设失效（[智驾调研 §1](2026-11-08-autonomous-driving-visual-survey.md)）。
2. **但新颖性必须打折：缺口已有三层占用者。** 基准层（WRBench / MemoBench / WROP）、架构层（Marionette / MASS / StatePlay）、基础设施层（Omniverse SimReady、**WorldFoundry 2026-11-07 大更新**）。"第一个测量物体恒存""第一个状态+生成混合架构""第一个 AI 自动准备场景"都**不再是可主张的创新**。
3. **真正未被占用的仍是运行时层**：生产者无关 + 契约语义（身份/时间/所有权/置信度）+ 单写者裁决 + 位级可复现 + 闭环校准测量。WorldFoundry 自己的对比表承认"没有系统完全支持闭环"；它统一的是**执行编排**（保留各模型原生语义），不是**世界状态语义**。queue 的定位主张"state-centric real-time loop vs model-centric batch pipelines"**经此轮证据后依然成立，且更具体**。
4. **整合判断：重定义 = 四层架构为逻辑分层，OmniWorld 本体是 L3 心脏 + 横向契约与测量；L1/L2/L4 是可替换的生产者与表现。** 六个方向不是六个并行项目，是同一个运行时的六类座位。这与 owner 11-05 方向评审输入中 Q2 混合式（高）/ Q4 评测闭环（高）的既有倾向一致，不需要推翻 ADR-0008。
5. **首战推荐 B'（L2×D2 组合）**：用程序化生成器制造"带真值的世界"，把开源生成式世界模型插进闭环，出"镜头转开再转回，物体还在不在 / 事件推进没推进 / 身份漂没漂 / 置信度校不校准"的位级可复现报告。WROP 已证明"生成器出考卷"可行，WRBench 已证明"状态核缺失"是公认病灶——**但没有人把生成器纳入契约管理、真值/信念分离、闭环位级复现**。这是四层协同的最小完整证明，也是 Saltline 感触的正确落地方式：不是复刻一个游戏，而是把"生成器"变成受契约管理、可测量的世界资产。

---

## 1. 六方向调研卡

> 每卡：已核事实（仓库+新证据）→ 创新点 → 谁已做了 → OmniWorld 座位 → 本机 8GB 可行性。仓库路径相对 `docs/17-research/`。

### D1 生成式世界模型 / 实时交互（L3 隐式 + L4）

- **仓库已核**：Genie 4 闭源、无权重、无公开 API、零导出，官方六条限制（[genie-4 卡](projects/genie-4.md)）；Cosmos 3 全家族 Linux-only、最小实测平台 16GB（[cosmos-3 卡](projects/cosmos-3.md)）；W04 结论"已深研的生成式模型恰好都不可入环"（[deep-research-index §一](2026-11-05-deep-research-index.md)）。
- **新证据（交互世界生成 45 条目录，检索全文）**：开源可入环候选比 W04 时更丰富——**ForgeWM**（四阶段蒸馏配方全开放：代码+权重+89GB 数据，实时 Minecraft/FPS）、**Solaris**（多人 Minecraft，训练/推理/评测/数据全开放）、**StatePlay**（联合预测画面+显式计时/血量/能量槽，模型+10K 数据+训练器开放）、**Matrix-Game 3.0/3.5**（长时记忆/patch 记忆，两个 5B 权重已发布；3.5 INT8 三步学生 20FPS@单 H100）、**ABot-World-0**（单桌面 GPU 无限 rollout，权重公开，16FPS@RTX 5090）、**LingBot-World**（权重公开，16FPS 480p@单 GPU 节点）。
- **创新点**：给隐式世界配**外部持久状态锚**（身份/事件/位置由引擎持有，模型只负责表现与预测）。
- **谁已做了**：WRBench/MemoBench 在基准层诊断了病灶；Matrix-Game 3.5/ReWorld/WorldPlay 在模型内部加记忆（检索式、非契约式）；**没有人做"引擎持有权威状态 + 生成模型作生产者"的通用运行时**。
- **座位**：被测量/被锚定的世界生产者（L3 隐式生产者 + L4 表现后端候选）。
- **8GB**：实时交互几乎全部要 H100/5090 级；**离线闭环评测可行**（引擎现况 RT-DETR CPU EP 518ms/帧，评测环本就不要求实时）。ForgeWM/Solaris/StatePlay 的小变体是否落入 8GB **待逐卡核验**（queue 既有任务，升级为首战前置）。

### D2 世界状态测量与评测生态（横向契约 + 指标）——主战场

- **仓库已核**：09-21 §4.7 评测生态碎片化（WBench/WorldMark/KineBench/stable-worldmodel/WorldFoundry 等"每家重写 action adapter/状态 schema"，WorldMark 原话 "no shared protocol has been established"）；09-24 扩至 18 家核 stars/许可/闭环度；**E1 实验指标教训**（观察陈旧 → 覆盖率 -31%、ECE +47.8%，但 divergence 反降、identity_stability 反升——三指标中两个把退化系统排更好 → M1 成对发布 / M2 空集≠稳定 / M3 per-producer 分解，[闭环 note §6-7](2026-10-27-interactive-closed-loop-route.md)）；置信度在 11/11 被研对象中均非一等公民 → 契约⑦必须引擎自建（index §二）。
- **新证据**：
  - **WRBench**（arXiv 2606.20545，2026-06）：把相机运动当作"对可观察性的干预"，人校准评分链；核心发现=返回目标停在被抛弃状态而非事件推进后的状态；主张"物理状态核稳定性应成为世界模型设计的一等目标"。
  - **MemoBench**（arXiv 2606.27537，2026-07）：disappear-and-reappear 范式，360 clips，10 开源模型；**无模型 ORS 超过 0.6**；无相机条件模型靠生成近静态视频刷高一致性分 → 必须 motion-gated 评测（与 E1"指标会骗人"教训同构）。
  - **WROP Object Permanence Benchmark**（Hokin，CC BY-NC）：150 认知科学任务 ×2 = 300 考卷，**Blender 生成器**随机化速度/光照/相机，每题五件套含逐帧真值轨迹 trajectory.npz + metadata.json 评分契约（identity/color/size/count 守恒 + 失败标签）；14 模型 4197 答卷全开源；配套 150 万训练样本。
  - **WorldFoundry 大更新**（arXiv 2611.10771，2026-11-07，OpenEnvision/NTU/Princeton）：270 模型 / 74 基准 / 62 指标；定义生成式世界模型=维持持久状态、响应干预、闭环更新的预测动力系统；manifest 驱动执行 + 两级产物契约（GenerationRequest/Result + ArtifactRef）+ evidence-aware scorecard；128×A100/H100 验证。**其对比表自认：没有比较系统完全支持闭环**；OpenWorldLib 是唯一有文档化执行+训练工作流的对照。
- **创新点（打折后）**：不做"又一个基准"，做**基准之下的运行时**——契约语义统一（身份/时间/所有权/置信度）+ 单写者裁决 + 真值/信念分离 + 位级可复现 episode + per-producer 校准 + 闭环延迟预算。WRBench/MemoBench/WROP 都是**批处理、视频级、VLM/像素评分**；WorldFoundry 是**编排级、保留原生语义**。没有任何一家提供"世界状态语义统一 + 实时闭环 + 位级复现"。
- **座位**：引擎本体 = 评测基础设施的运行时层；WorldFoundry/WROP 可成为**上游贡献对象或适配器目标**（敲门砖轨道：被 merge 的 PR 是可核验外部信号，09-24 §5）。
- **8GB**：评测环本身零外部依赖可跑（参考模式已证明）；被测模型按 D1 离线口径。
- **⚠️ 强制动作**：queue WorldFoundry 盯防条触发条件已满足（"On their release; then differential ADR within one week"）——**差分 ADR 必须在一周内产出**，本文 §3 是其输入。

### D3 具身闭环：VLA / WAM / 物理仿真（A 段）

- **仓库已核**：闭环路线已 settled 四件——仿真器=外部真值权威、引擎镜像进 WorldState（方案 b，与 D3 load_annotations 同构）；Action 是出向"信念→意图"，需独立 Action Channel 而非契约第十点；BeliefView 是 Phase 4 第一切片（现 predict 读真值=闭环自证）；E1 三条指标定案（[闭环 note](2026-10-27-interactive-closed-loop-route.md)）。MuJoCo Apache-2.0 可嵌入；MJWarp GPU 默认非确定、CONTACTS 确定性位是空桩 → J2 同 seed 双跑位级比对是 spike 楔子（[newton 卡](projects/newton-mujoco-warp.md)）；Isaac Sim 16GB 墙、Replicator 契约空白（[isaac-sim 卡](projects/isaac-sim.md)）；SmolVLA 450M 宣称消费级可跑（未实测）。
- **新证据**：生数 Motubrain/Motus2 = MoT 统一架构（理解/生成/行动专家共享注意力），Motus2 策略/模拟器/评估器三接口同参数 + 触觉专家（官网 WAIC 已核）；Genie 4 + Gemini 4 Argon 机器人"想象排练"（Apollo 下跳棋）；WorldFoundry 把具身评测重心放在闭环任务完成（LIBERO/RoboTwin）而非离线动作准确率。
- **创新点**：闭环三件"没有别人做好测"的事（R§2.3 原文）——①位级相同 episode 的可复现闭环测量；②perceive→decide→act→observe 端到端延迟分解；③信念校准与信用分配。**WorldFoundry 更新后此主张反而被强化**（其对比表自认闭环无完全支持）。
- **座位**：Phase 4 消费者（D6 收口后）；WAM/VLA = Agent Adapter 家族；物理仿真器 = 真值权威生产者。
- **8GB**：MuJoCo 可行；Cosmos-Predict2-2B 32.54GB 出局（只能离线数据生产者）；SmolVLA 待核。

### D4 程序化生成 / AI 资产生产（L2）——Saltline 感触的落点

- **仓库已核**：第一轮 note §3.4（Saltline 镜像一手证据：单 prompt 8h、Three.js r180、模型/材质/音效全代码生成；几猫=换皮）；角色动画 note 三层拆解（动作提取/角色驱动/合成渲染）与"运动数据层=唯一长期资产"；knowledge/rendering 技能地图。
- **新证据**：**WROP 用 Blender 生成器批量制造带真值的认知考卷**（150 任务可各产 1 万+ 样本，150 万训练语料）——"生成器当数据工厂"已被验证且规模化；UE PCG/Houdini 的规则图+参数化资产+依赖更新是成熟工业参照（第一轮已述）。
- **创新点（组合式，非单点）**：
  1. **生成器作为受契约管理的世界资产**：参数 → 几何+碰撞体+语义+置信度+版本，进 Proposal 写路径，可复现（同参数同 seed 逐字节）。Saltline 证明 AI 能写生成器，但产物是一次性 demo；WROP 证明生成器能出考卷，但生成器本身不在契约下、真值与信念不分离。
  2. **L2×D2 组合：程序化真值世界工厂**——用生成器制造"带真值身份/轨迹/事件的世界"，作为生成式世界模型的闭环考场。这是四层协同的最小完整证明（§5 首战 B'）。
  3. AI 写生成器 + 规则约束验证（混合式）：穿模/法线/碰撞体一致性/比例失真由规则检查器验收（Saltline 拆解 §九的失效清单直接变成验收项）。
- **座位**：L2 新层（四层中唯一真正新增）；生成器库 = 可复用作品集资产。
- **8GB**：**完全可行**（程序化几何/材质是 CPU/GPU 轻计算，无大模型依赖；Three.js 或 C++ 均可）。这是六方向中本机可行性最高的新增能力。

### D5 重建 / 空间智能 / 智驾（L1→L2 真实输入）

- **仓库已核**：智驾调研五条结论（AVM= bowl 曲面+双 LUT 非真三维、BEV/Occupancy 是感知中间态非契约状态、"视频→重建→自由视角→闭环仿真"是工业主攻线、相通性命题抽象层成立/工程产物层不成立、8GB 可行性分层）；VGGT→COLMAP→gsplat 链已核（≤20 帧 5.58GB 档，原权重 CC-BY-NC）；四大驾驶数据集全 NC → 只能非商业研究/作品集形态；里程碑候选=一段 Argoverse2 log→VGGT→gsplat→自由视角渲染+PSNR/SSIM/延迟进回归报告（S§8）。
- **新证据**：AMD 约 82 亿美元收购 World Labs（转述未核）；Marionette"显式状态→确定性几何渲染→扩散只画外观"（部分开源）证明混合管线在学术界已跑通。
- **创新点**："可测量的共享世界状态层恰好站在两域割裂带上"（S§5.3 原文）；AVM 的静态标定假设 → 契约显式置信度的切入口（S§1）。
- **座位**：真实世界输入生产者；**并入 ADR-0015 裁决**（⚠️ 编号冲突：归档已有旧 ADR-0015 gpu-memory-synchronization，新 ADR 应取 **0016**）。红线（多相机/标定/GPU 渲染器）未裁决前不动。
- **8GB**：离线短片段重建+回放渲染可行；流式实时重建不现实（VGGT 无增量接口）；生成式驾驶世界模型不可行。

### D6 渲染管线 / GPU 可观测性（L4 + 横向）

- **仓库已核**：D5 零拷贝进行中（H2D 12.81GB/s，收益在减少往返与同步）；Nsight/CUPTI 本机实测成功（SQLite MEMCPY/SYNCHRONIZATION 表恰为 D5 判据证据通道，tick 号可经 external correlation 挂钩，[nsight-cupti 卡 §5](projects/nsight-cupti.md)）；Holoscan 5.0 / DeepStream 9.2 均 Apache-2.0，削弱"零拷贝需自研"前提（queue 10-28 更正）；Vulkan 离屏渲染器=技能阶梯座位（PLAN 族谱③）。
- **新证据**：Saltline 的性能纪律案例——材质烘焙成纹理缓存复用、Triplanar 免逐模型 UV、按材质合并几何（枪械 4-7 draw call）；Marionette 的"确定性几何渲染+生成式外观"是 L4 后端候选架构。
- **创新点**：GPU 确定性/重放的测量 niche（third-party-map 原文"正是我们的测量 niche"）；tick↔GPU 活动关联 = 契约⑨的证据通道。
- **座位**：D5/D6 照常 + 技能阶梯（kernel 作者方向）；L4 后端按证据回归。
- **8GB**：本机即主战场。

---

## 2. 相通性矩阵：六方向如何经 World Contract 连通

> 仓库各卡**早已各自声明**与契约的连接点（本轮子代理汇总，非新发明）——这就是"很多都是通的"的代码级证据。

| 方向/对象 | identity | time | ownership | confidence |
|---|---|---|---|---|
| MJWarp/Newton（D3） | MJCF 编译期索引只作外部别名，引擎自铸 id | 固定步单调推进 = "tick 即版本" | control 相唯一写入口；"接触不自动回流"镜像"信念不回写" | 仿真输出按定义为真值；overflow bitmask/solver_niter 应进 tick 日志 |
| VGGT（D5） | track head 仅单次调用内，跨调用身份归引擎 | 时间轴全由引擎持有 | gauge 钉死首帧 → canonical frame 所有权必须在引擎侧 | precision [1,∞) 与 sigmoid [0,1] 两族不可同列；camera 无 conf |
| GVHMR（D5/动画） | 单条 top-1 轨迹，身份管理落引擎 | 30fps 硬编码 → 帧索引→引擎时间轴须显式 | 预处理四缓存 = 单写者单元 | 仅 stationary 概率，其余由适配器合成 |
| SAM2（D1/D5） | obj_id 调用方赋予、无再识别 → 引擎接管 | — | — | 跟丢映射为 confidence 衰减而非身份切换（"为什么 identity 归引擎管"的现成案例） |
| Wan2.2-Animate（动画） | reference latent = 身份"每段重新证明" | — | mask = 空间 ownership 单写者 | 输出无 confidence → 评测环补齐 |
| Rerun（D6） | entity path × timeline 与 identity/time 同构 | 同左 | "后写胜出+历史全留" = 契约⑥"只记录不仲裁"同一选择 | — |
| Isaac Sim（D3） | — | 多速率时钟域 vs 单一 tick 域 | 物理独占写回 = 方案(b)镜像 | 构造真值 vs 真实观察 = 两个评分空间（呼应 D4/ADR-0013） |
| 生成式世界模型（D1） | 出画再回画身份漂移 → 引擎持有权威身份 | 事件在无观察期间不推进（WRBench）→ 引擎持有事件时钟 | 模型输出=Proposal，不是真值写 | 模型无置信度语义 → 契约⑦引擎自建 + ECE 校准 |

**结论**：契约不是为某一个方向设计的，它是六方向共同缺口的同一副骨架。重定义不需要新发明横向层——需要的是**把已有契约明确宣布为四层架构的横向基础设施，并按座位接入生产者**。

---

## 3. 新颖性打折表（防假新颖，本轮最重要的修正）

| 第一轮/直觉主张 | 谁已做了（新证据） | 他们没做的 | OmniWorld 剩余空间 |
|---|---|---|---|
| "第一个测物体恒存/状态一致性" | WRBench（23 模型 9600 视频）、MemoBench（ORS<0.6）、WROP（300 考卷+真值轨迹） | 运行时闭环、契约语义、位级复现、per-producer 校准；全是批处理视频级评分 | 做基准之下的**运行时**，不做又一个基准 |
| "状态+生成混合架构创新" | Marionette（276D 状态→确定性几何→扩散外观）、MASS（权威共享状态→任意相机）、StatePlay（画面+显式状态联合预测） | 生产者无关的通用基础设施；多为单模型单游戏、闭源/部分开源 | 不主张架构创新，主张**基础设施创新**（任何生产者经同一契约接入） |
| "AI 自动准备场景" | Omniverse Agentic SimReady（OpenUSD 契约+验证门+Isaac 交付） | 机器人向、NVIDIA 生态绑定、非本地免费通用运行时 | 避开；OpenUSD 只做单向导出（既有决策不变） |
| "生成器当资产" | WROP（Blender 生成器出 150 万样本）、UE PCG/Houdini 工业规则图 | 生成器受契约管理（版本/置信度/单写者）、真值/信念分离、位级复现 | **L2×D2 组合**：程序化真值世界工厂（首战 B'） |
| "统一评测基础设施" | **WorldFoundry 2026-11-07 更新**（270 模型/74 基准/产物契约/scorecard） | 世界状态语义统一（它保留原生语义）、实时闭环（其对比表自认无完全支持）、位级复现 oracle | 差分定位：**state-centric real-time loop vs model-centric batch orchestration**；可作上游贡献对象 |
| "闭环可复现测量" | WorldFoundry 强调闭环但无系统完全支持；VLA 评测普遍做不到位级 episode | 同 seed 逐字节 episode + 延迟分段 + 信念校准三件套 | R§2.3 主张**被强化**，Phase 4 兑现 |

**WorldFoundry 差分要点（供一周内差分 ADR 使用）**：
1. 它统一**执行与证据**（manifest/ArtifactRef/scorecard），不统一**世界状态语义**（无身份铸造/所有权裁决/置信度契约）；OmniWorld 的九条语义正是它的空白。
2. 它的评估是**产物中心、执行后离线评分**；OmniWorld 是**状态中心、逐 tick 在环评分**（真值/信念分离、单写者）。
3. 它的闭环列自认无完全支持；OmniWorld 的 grounding loop + E1 指标教训是现成差异化证据。
4. 互补面：它的 270 模型目录可作为 OmniWorld 生产者候选池；OmniWorld 可作为它的闭环运行时后端 → 贡献/集成期权（敲门砖轨道）。
5. 风险面：若它下一步补上状态语义与闭环，窗口收窄 → 差分 ADR 必须含"什么证据触发转向贡献而非自建"。

---

## 4. 整合重定义提案 v2（采用 owner 确认的四层架构）

### 4.1 定位一句话

> **OmniWorld 是以四层架构组织的、可测量的世界状态运行时：L3 运行心脏 + 横向 World Contract/GPU Observability 是本体；L1 理解、L2 构建、L4 表现是可替换的生产者与表现后端；任何世界生产者（程序化生成器、生成式世界模型、真实视频重建、物理仿真器）经同一契约接入，引擎负责身份/时间/所有权/置信度与正确性测量。**

### 4.2 四层座位表（现状 → 提案）

| 层 | 现状（已核） | 提案 | 对应方向 |
|---|---|---|---|
| **L1 理解** | 适配器机制已验证（RT-DETR/DA-V2 同契约零核心改动）；真实帧+标注入环 | 不动；重建类生产者（VGGT→gsplat）按证据回归 | D5 |
| **L2 构建** | **空白** | 新增：程序化生成器库（Saltline 式几何/材质/IK）+ 生成器契约（参数→几何+碰撞+语义+置信度+版本）+ AI 写生成器、规则验证器验收 | D4 |
| **L3 运行** | **已有心脏**（九条契约+单写者裁决+grounding loop+双模式） | 语义不动；生产者集合扩展：生成式世界模型=隐式生产者（输出=Proposal）、物理仿真器=真值权威（方案 b）、程序化生成器=真值世界工厂 | D1/D3/D4 |
| **L4 表现** | CPU 光栅器参考组件 | 座位保留：Vulkan/生成式后端按证据回归；Marionette 式"状态→确定性几何→生成式外观"为候选后端架构；现"不碰"清单需 ADR 复议才动 | D6/D1 |
| **横向** | World Contract + 延迟/拷贝计量 + Nsight/CUPTI 证据通道 | 即四层提案的横向基础设施，**无需新发明**；明确宣布为四层共用 | D2/D6 |

### 4.3 与既有决策链的一致性

- **ADR-0008 引擎身份**：不变。变的是生产者集合与 L2 新增。评测仍是第一应用，对象升级为"任何世界生产者"。
- **owner 11-05 评审输入倾向**：Q2 混合式（高）/ Q4 评测闭环（高）= 本提案；Q1 开发者运行时（中高）= 长期形态；Q3 评测社区（中）= 期权触发器（WorldFoundry 贡献是现成入口）。
- **PLAN ⑤ 游戏化/编辑器让渡行**：首战 B' 的"程序化小世界"是**评测考场**不是游戏产品（无玩法/无 HUD/无玩家），本文判断不踩让渡行；若后续做场景 A（可玩雨夜港口）则必须显式复议。
- **ADR-0015（智驾）**：并入统一评审；**新 ADR 取号 0016**（归档旧 0015 重号，子代理已核）。
- **角色动画侧项目**：运动数据层与契约同构（C§6），L2 动画座位（程序化 IK）可复用其结论；按既有决策仍不入产品路径。
- **阶段 D**：D5/D6 照常，本文不改 CURRENT。

### 4.4 目标函数校验（PLAN §四）

- 第一层磨练技术：L2 程序化几何/材质 = 渲染管线素养；D6 GPU 数据流 = kernel 作者阶梯；闭环测量 = AI 系统实证。✅
- 第二层敲门砖：B' 产出"别人没有的位级可复现闭环测量报告"+ 可复用生成器库；WorldFoundry/WROP 上游 PR = 可核验外部信号。✅
- 第三层创业期权：不投入；评测产品化期权由外部信号触发（不变）。✅

---

## 5. 首战提案 B'：程序化真值世界工厂 × 生成式世界模型闭环考场

**用户可见结果**：一条命令生成一个带真值的小世界（如集装箱堆场：每个箱子有权威身份/位置/碰撞体，事件有真值时钟）；把一个开源生成式世界模型插进闭环跑一段"镜头转开→事件发生→镜头转回"；报告回答四个问题——物体还在不在（恒存）、事件推进没推进（状态核）、身份漂没漂（同一性）、置信度校不校准（ECE）——同 seed 逐字节复现。

**为什么是它**：
1. **四层协同的最小完整证明**：L2 生成器造真值世界 → L3 闭环运行 → L1/L4 生成模型作生产者/表现 → 横向契约测量。
2. **差异化经打折后仍成立**：WROP 有考卷无运行时；WRBench 有诊断无契约/复现；WorldFoundry 有编排无状态语义/闭环；Marionette 有混合架构无通用性。
3. **本机可行**：生成器零大模型依赖；被测模型离线入环（引擎已证明 518ms/帧可跑）；参考模式位级 oracle 现成。
4. **Saltline 感触的正确落地**：不是复刻游戏，而是把"生成器"沉淀为受契约管理、可测量、可复用的世界资产——第一轮 note §3.4 的教训（一次性 demo 无沉淀）直接变成设计约束。
5. **不踩让渡行**：考场不是游戏产品（§4.3）。

**前置（按序）**：
1. WorldFoundry 差分 ADR（queue 强制，一周内）——确认运行时层缺口仍成立。
2. 8GB 可入环开源标的逐卡核验（ForgeWM/Solaris/StatePlay 小变体优先；许可+权重+接口三件）。
3. grill → 方向 ADR（0016，与智驾/动画合并评审）→ L1 设计 → 切片。

**备选**：纯 B（不用程序化真值，直接测开源模型间一致性）——改动更小但缺 L2 协同证明；A（可玩雨夜港口）——作品集冲击力最强但踩让渡行且工作量最大，建议作为 B' 生成器库成熟后的技能侧线。

**推翻条件**：差分 ADR 发现 WorldFoundry 已补状态语义+闭环 → 转向贡献；8GB 标的全部出局且离线也不可行 → 降级为纯程序化实验室（A 侧线）；WROP 类考卷被证明足够且社区无运行时需求 → 评测期权不启动（外部信号规则不变）。

---

## 6. 待 owner 拍板（行为语言，三问）

1. **定位 v2 确认**：四层架构为逻辑分层、本体=L3 心脏+横向契约与测量、六方向为座位不并行——是否按此重定义？（演进，不推翻 ADR-0008）
2. **首战 B'**：程序化真值世界工厂 × 生成式模型闭环考场——是否作为第一个验证场景？（备选：纯 B / A）
3. **评审组织**：WorldFoundry 差分 ADR（一周内强制）+ 方向 ADR-0016（含智驾/动画合并）——是否合并为一次 grill 方向评审？

---

## 7. 来源与核验状态

| # | 来源 | 关键内容 | 状态 |
|---|---|---|---|
| 1 | [WRBench: Current World Models Lack a Persistent State Core (arXiv 2606.20545)](https://huggingface.co/papers/2606.20545) | 9600 视频/23 模型；相机=可观察性干预；返回目标停在被抛弃状态 | 已核（摘要检索全文） |
| 2 | [MemoBench (arXiv 2606.27537)](https://arxiv.org/html/2606.27537v5) | disappear-and-reappear；ORS<0.6；motion-gated 评测；10 开源模型配置 | 已核（检索全文） |
| 3 | [WROP object-permanence-benchmark](https://blog.csdn.net/gitblog_00758/article/details/168435938) | 300 考卷/Blender 生成器/真值轨迹+评分契约/14 模型 4197 答卷/CC BY-NC | 已核（检索全文；HF 原仓未逐页） |
| 4 | [WorldFoundry (arXiv 2611.10771, 2026-11-07)](https://arxiv.org/html/2611.10771) | 270 模型/74 基准/62 指标；产物契约+scorecard；对比表自认闭环无完全支持 | 已核（检索全文）；[GitHub](https://github.com/OpenEnvision/WorldFoundry) 未逐页 |
| 5 | [交互世界生成 45 条目录](https://github.com/zsyverse/game-ai-benchmarks-papers/blob/main/research/interactive-world-generation-sources.md) | ForgeWM/Solaris/StatePlay/Matrix-Game 3.x/ABot/Marionette/MASS 等开放性与硬件口径 | 已核（检索全文；第三方审计目录，各仓 LICENSE 未逐页） |
| 6 | [Genie 4 官方发布转载](https://www.ncsti.gov.cn/kjdt/kjrd/rgzn_kjrd/202610/t20261029_259910.html)、[知乎边界分析](https://zhuanlan.zhihu.com/p/2098692029257618791) | 2 分钟一致性/原生音频/混合架构/官方局限 | 已核（第一轮 §6 同源）；"2 分钟"为媒体转述，官方一手无数值（genie-4 卡数字纪律） |
| 7 | [生数 WAIC 官网两篇](https://www.shengshu.com/zh/news/waic-zhujun-world-model-infrastructure/) | MoT/Motubrain/Motus2/Vidu S1 540P42FPS | 已核（第一轮 §6 同源，一手） |
| 8 | [NVIDIA Agentic SimReady 博客](https://developer.nvidia.com/blog/how-to-use-ai-agents-to-prepare-3d-scenes-for-simulation/) | OpenUSD 契约层+验证门 | 已核（第一轮 §6 同源） |
| 9 | [Saltline 镜像](https://github.com/realcodebear/saltline-fable-5.2)、[模块化恢复](https://github.com/realskyrin/fps3ademo-saltline)、[几猫报道](https://www.163.com/dy/article/LBK3K7KR0516BJGJ.html) | 一手开发信息/换皮证据 | 已核（第一轮 §6 同源） |
| 10 | 仓库内部（子代理汇总，路径见各卡） | 13 卡+11 深报+决策链+三 note+NOTES.md | 已核（只读深挖） |
| 11 | AMD 收购 World Labs / 小鹏 X-World / NVIDIA OmniDreams / 生数融资额 | 行业整合与商业数据 | **转述/未核**，不作裁决依据 |

**子代理发现的仓库内务问题（登记，不在本文处理）**：①在途"ADR-0015"与归档旧 0015 重号 → 新 ADR 取 0016；②W04 覆盖错位（已深研模型恰不可入环，评测侧零深报）→ 本轮 D1/D2 新证据部分补齐；③全景报告 USD Fabric 数值未证实、R2 多处错误已被 09-21 修正表裁定（引用须看修正后口径）；④Genie 4 数字纪律：媒体"2 分钟"不得写成官方口径。