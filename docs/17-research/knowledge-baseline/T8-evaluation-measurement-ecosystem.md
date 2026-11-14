# T8 世界模型评测与测量生态 — 知识基线（2026-11-13）

> **Trust label: [Research → 知识基线 + ADR-0017 差分输入]** — 本 note 为方向评审（grill → ADR-0016）与 WorldFoundry 差分 ADR-0017 草案提供世界模型评测/测量生态的一手证据基线；**只调研不裁决**：所有对比只陈述事实与证据，方向取舍留给 owner。
> 方法：仓库四份上游资产只读复用（§5）+ 本轮联网一手核验（arXiv 摘要/HTML 全文/PDF 全文提取、官方 GitHub README、官方文档站、HF 官方数据卡）。核验状态逐条见 §7；未一手核验一律标"未核/转述"。
> **本轮最重要的修正**：上游笔记"WorldFoundry 对比表自认没有系统完全支持闭环"**与原文不符**——Table 1 中 WorldFoundry 自身行 Closed loop 标 ✓（详见 §3a.6 与 §8.4）。差分 ADR 不得再引用该表述。

---

## 1. 原理与方法学分类

### 1.1 评测对象分层：五层金字塔

世界模型评测不是一个指标问题，而是**五个不同对象层**的测量问题。层越高，越需要干预（intervention）与真值（ground truth），越无法用开环视频评分覆盖：

| 层 | 测什么 | 代表基准/指标 | 评分方式 | 真值需求 |
|---|---|---|---|---|
| L-P 像素质量 | 帧保真、美学、时序闪烁 | VBench 系、PSNR/SSIM/LPIPS、HPSv3 | 自动特征模型 + 像素比对 | 低（参考帧或无参考） |
| L-S 语义一致 | 指令遵循、主体/背景一致、跨 turn 一致 | WBench 5 维 22 子指标、MemoBench VQA 4 维 | 专用视觉模型 + VLM-as-judge | 中（prompt 即弱真值） |
| L-Ph 物理规律 | 因果保真、碰撞/支撑/守恒、运动学可行 | VideoPhy 系、Physics-IQ、KineBench SPARC/Maruyama | VLM 判断 + 物理仿真器回放 | 高（物理引擎或人工） |
| L-St 状态持久 | 无观察期间事件是否推进、身份/数量/位置守恒 | WRBench（re-observed state）、MemoBench ORS、WROP 300 考卷 | 人校准评分链 / 检测式指标 / 真值轨迹比对 | 很高（事件真值 + 可观察性干预） |
| L-T 任务完成 | 闭环任务成功率、指令达成、错误恢复 | World-In-World、KineBench ManiSkill3 rollout、WorldFoundry embodied（LIBERO/RoboTwin） | 仿真器判定 + 执行指标 | 最高（环境即真值权威） |

**关键结构事实**：L-P/L-S 可以纯开环批处理；L-St 必须构造"可观察性干预"（相机转开/遮挡）才能提问；L-T 必须闭环。当前生态的绝大多数基准集中在 L-P/L-S，L-St 是 2026 年才系统性打开的层（WRBench 06 月、MemoBench 06 月、WROP 09 月），L-T 的闭环测量仍碎片化（09-21 §4.7"每家重写 action adapter/状态 schema"）。**层间不可互相替代**：WBench 发现"物理正确性与渲染质量相关而非与控制能力相关"、MemoBench 发现"像素指标领先者（Stable Virtual Camera）VQA 视觉质量反而落后"——低层高分不蕴含高层能力（均已核，§7）。

### 1.2 开环 vs 闭环协议

- **开环（open-loop）**：固定 prompt/相机轨迹/动作序列 → 模型一次性生成 → 对产物评分。产物中心、执行后离线评分。WorldFoundry 的主体工作流即此（"generate once, inspect the files, and score them later without loading the model again"，官方 Architecture 文档原话，已核）。
- **闭环（closed-loop）**：WorldFoundry 论文 Table 1 的列定义原文："**controls depend on prior outputs or environment feedback**"（已核，PDF p.5）。实践中有两种形态：
  1. **交互式世界模型会话**：模型 session-resident，动作逐轮输入、模型内部状态（KV cache/latent memory）承载历史——状态在模型内，外部不可审计；
  2. **具身 policy↔simulator**：策略出动作 → 仿真器执行 → 观测回灌（KineBench、World-In-World、WorldFoundry embodied path）——真值在仿真器，但**没有共享的 canonical 世界状态对象**（09-24 §0"两派各缺一半"）。
- **第三种（OmniWorld 主张，仓库既有）**：引擎持有 canonical 状态 + 真值/信念分离 + 逐 tick 在环评分。闭环不只是"控制依赖反馈"，而是"**谁写动力学、谁持有身份、信念如何被裁决**"的所有权问题（闭环 note §2 方案 b：仿真器=真值权威，引擎镜像+评分，已核）。
- **批处理 vs 实时**：闭环评测不必然实时（离线闭环合法），但"实时帧预算内的闭环测量"（perceive→decide→act→observe 端到端延迟分解）目前无人作为契约提供（闭环 note §2.3；WorldFoundry 把 serving engines 列为 future work，已核）。

四种协议形态对照（事实综合，各行来源见 §2/§7）：

| 形态 | 状态持有者 | 真值来源 | 评分时机 | 复现粒度 | 代表 |
|---|---|---|---|---|---|
| 开环批处理 | 无（产物即一切） | 参考视频/人工/生成器 | 执行后离线 | 产物 hash | WBench、WRBench、MemoBench、WROP、WF 主体 |
| 交互式会话 | 模型内部（KV/latent memory） | 弱（prompt/人评） | 逐 turn 或事后 | 会话级（无位级承诺） | WF Studio realtime、OpenWorldLib stream |
| 具身 policy↔sim | 仿真器 | 仿真器（构造真值） | episode 落盘后 | episode 级 | KineBench、World-In-World、WF embodied |
| 引擎在环（OmniWorld 主张） | 引擎 canonical 状态 | 外部真值权威镜像 | **逐 tick 在环** | **位级 episode** | 仓库 grounding loop（参考模式已实证） |

### 1.3 VLM-as-judge 的偏差与治理

已核的偏差实例与治理手段：

1. **静态视频刷一致性分**（MemoBench）：无相机条件模型（LTX-Video）靠生成近静态视频在一致性类 VQA 维度拿高分（4 维中 2 维第一），但 Instruction Following 维度暴露其不执行视角变化 → 治理 = motion-gated 评测 + 把"指令遵循"与"一致性"分维发布（已核）。
2. **可判定性与评分混淆**（WRBench）：若目标根本未回到视野/不可辨认，"返回一致性"分数无意义 → 治理 = **judgeability gate 与 scorer 分离**：Qwen-3-VL-Instruct-8B 先判定 H（隐藏区间非平凡）∧U（回到视野）∧J（证据可辨认），Qwen-3.5-9B 才计算 re-observed 分数；不满足判 NA 而非 0（已核，HTML §3.4）。这是"空集≠稳定"（E1 M2）在基准层的同构设计。
3. **LMM 物理评分对细微效应不可靠**（WBench 自述 Limitations 原话："The physical dimension relies partly on LMM-based evaluation whose reliability may degrade for subtle effects"，已核）。
4. **人校准是评分链的锚**：WRBench 2,547 条去重人工裁决按基准输出同粒度校准自动评分（而非学一个整体偏好分）；WBench 400 名众包标注盲测成对比较，10 个维度 Spearman ρ≥0.94（4 个 =1.00）；MemoBench VQA 与人工多数答案一致率 92.9%、Cohen's κ=0.85（均已核）。**结论：VLM 分数只有与人校准链绑定才可作为排名依据**。
5. VLM-as-judge 的系统性失效模式综述（VBVR-Pro，称"identifies recurring failure modes of the prevalent VLM-as-a-judge paradigm"）——**转述**（见于 WROP 参考文献检索摘要，未读原文）。

### 1.4 真值从何而来

| 真值来源 | 实例 | 性质 | 代价/边界 |
|---|---|---|---|
| 人工标注/裁决 | WRBench 2,547 verdicts；WBench 400 标注者 | 偏好级/判定级，非逐帧状态真值 | 贵；只能校准，不能穷举 |
| 物理仿真器 | KineBench ManiSkill3（20 任务）；WorldFoundry LIBERO/RoboTwin | 构造级真值（引擎即权威） | sim-to-real gap；资产/许可依赖 |
| 程序化生成器 | WROP：150 个 Blender 生成器，随机化速度/光照/相机等 nuisance 参数、保留认知结构，每任务 10,000+ 样本 | 逐帧真值轨迹（trajectory.npz）+ 评分契约（metadata.json） | 合成域窄；但**真值免费且可无限扩**（已核，HF 数据卡） |
| 真实数据+标注 | MemoBench 360 clips 含真实子集（溶解/燃烧/扩散等不可逆过程）；仓库 E1 的 COCO 面板 | 真实分布 | 无逐帧状态真值；E1 证明独立照片面板只能给上界不能给梯度（已核） |

**真值/信念分离**是运行时层的独有要求：基准层只需"真值存在"，运行时层还需"被评系统拿不到真值"（否则闭环自证——仓库 `predict(Tick, const WorldState&)` 读真值的结构缺口，闭环 note §4，已核）。WROP 的五件套把真值与考卷分离发布，但**生成器本身不在任何契约管理下、无版本/置信度/单写者语义**（09-24/11-13 上游结论，本轮 HF 卡核验其文件布局后维持）。

### 1.5 置信度校准：ECE 与可靠性图

- **原理**（经典方法，常识性引用，本轮未重验原文）：把预测按自报置信度分 bin，ECE = Σ_m (|B_m|/n)·|acc(B_m) − conf(B_m)|；可靠性图（reliability diagram）画每 bin 的 conf vs acc，对角线=完美校准。出处：Naeini et al.（AAAI 2015，Bayesian binning）、Guo et al.（ICML 2017，现代神经网络校准）——不给 arXiv 编号，未本轮核验。
- **per-producer 必要性**（仓库 E1 实测，已核）：混合 producer 的总体 ECE 会被"神谕项"稀释；`ece:rtdetr_v2_s` 是唯一可靠检出观察时滞退化的指标（0.198829→0.293768，+47.8%）。
- **置信度语义必须同质**（闭环 note §3.2，已核）：状态主张正确率 vs 动作成功率是两种自报概率，混在一个 bin 里 ECE 失真 → 动作 producer 必须单开 bin 组。
- **ECE 自身的失效模式**：bin 数/边界敏感、空 bin 处理、以及"置信度语义缺失时根本无法计算"——11/11 被研对象置信度非一等公民（deep-research-index §二，仓库已核），生成式世界模型普遍无置信度输出 → 契约⑦必须引擎自建（上游结论，本轮未发现反例：WorldFoundry 论文/文档无 calibration/ECE 契约，仅集成 PAWBench 这一"stochastic dynamics calibration"基准，已核）。
- **可靠性图读法**（结合 E1 数据）：理想诊断图应显示"退化沿对角线远离"——E1 中 `ece:rtdetr_v2_s` 从 0.198829（lag0）跳到 0.293768（lag1）后进入 0.277–0.302 平台，说明检测器在陈旧输入下**自报把握未随实际正确率下降**（过度自信方向退化）；而总体 ece 被神谕项钉在常数附近（`ece:zero_delta` 恒 0.5 是面板构造性质）——**不分解的可靠性图会把两种 producer 的失效画成一条无信息的平均线**（E1 已核数据的直接推读）。

### 1.6 指标失效模式清单（Goodhart 家族）

| 失效模式 | 机理 | 一手证据 | 对策 |
|---|---|---|---|
| 分母收缩 | 误差只在"匹配上"的样本求均值，系统退化→匹配变少→均值反而变好 | E1 M1：S1 lag1 divergence 降 13% 而覆盖率降 4.3pp（已核） | 误差+覆盖率**成对发布** |
| 空集刷分 | 稳定性指标在"两边都空"时判稳，什么都不相信的系统得满分 | E1 M2：identity_stability ≡ P(相邻两 tick 零匹配)，算术验证（已核） | 排除空-空配对；WRBench 的 NA 语义同构 |
| 神谕稀释 | 读真值的 producer 把平均指标拉到接近 0 | E1 M3：S1 71% 账本条目来自读真值模型（已核） | per-producer 分解（divergence 尚缺，ECE 已有） |
| 静态刷分 | 不动的系统天然"一致" | MemoBench：LTX-Video 近静态视频刷高一致性（已核） | motion-gated（≥90° GT 相机旋转子集）+ CamCtrl 联读 |
| 单指标排名反转 | 三指标中两个把退化系统排更好 | E1-2（已核） | 指标成组发布 + 与计数联读 |
| 感知分≠能力分 | 像素/美学领先者控制能力垫底 | WorldMark：最佳感知质量模型平移方向准确率与响应延迟垫底（已核，摘要） | 分维发布，不合成单一总分 |

**方法论结论**（E1 已定案，本轮外部证据全部同向）：任何单指标闭环结论不成立；指标必须成组（误差+覆盖+计数）、门控（motion/judgeability）、分解（per-producer）发布。

### 1.7 位级复现作为 oracle：原理与边界

- **原理**：确定性参考模式 + 逐 tick 账本 + 状态哈希 ⇒ 同 seed 同输入 ⇒ **逐字节相同的 episode**。它本身不测质量，而是把"测量"变成可回归、可 diff、可归因的实验：E1-5 中六个 lag 的 `world_hash` 全为 `725d0db5f1fad19a`（真值侧未漂移）而 `ledger_hash` 各异（信念侧确实变了）——这是"实验只改了观察、没有偷改真值"的可核对证据（已核）。位级 oracle 是"指标会骗人"（§1.6）的终极审计手段：任何指标异常都能重放到同一字节现场。
- **边界**：
  1. 只在受控确定性生产者下成立；GPU 物理默认非确定（MJWarp CONTACTS 确定性位是空桩，newton 卡，仓库已核）；浮点归约顺序/并行调度/异步推理都会破坏位级一致（闭环 note §2.3 指出当前 VLA 评测普遍做不到）。
  2. 位级复现≠正确性：确定性地错也是确定性的；必须与真值评分联用。
  3. **与 WorldFoundry 的"reproducibility"不是一回事**（已核）：WF 的复现 = RunPlan 指纹使 `requests.jsonl` 跨机器逐行一致（**计划级确定性**）+ ArtifactRef 内容哈希（**产物溯源**）+ run_manifest 的 reproducibility fingerprint；它**不承诺生成执行位级确定**，GPU 生成模型本身也做不到。差分点不在"谁有 hash"，而在"**确定性闭环 episode 作为测量 oracle**"这一层（§8）。

---

## 2. 进度快照（截至 2026-11）

> 闭环度沿用 09-24 口径（1=纯离线打分 … 5=真环境实时闭环）。stars 为 09-24 GitHub API 快照（本轮未重验，标"快照"）；许可/规模凡本轮一手核验者标"已核"。

| 系统 | 规模/对象 | 许可 | 闭环度 | 评分方式 | 核验状态 |
|---|---|---|---|---|---|
| **WorldFoundry**（OpenEnvision，arXiv 2611.10771，2026-11-07） | 270 模型条目 / 74 基准 / 62 注册指标条目；128×A100/H100 80GB 验证 | Apache-2.0（自研代码；vendored 组件原许可） | 2–3（主体产物中心；具身 policy↔sim 闭环路径存在） | manifest 执行 + 产物契约 + evidence scorecard（diagnostic/benchmark-comparable/exact reproduction 三级） | **已核**（论文 PDF 全文+README+官方文档 5 页）；仓库代码未逐页 |
| **OpenWorldLib**（OpenDCAI） | 统一推理代码库：pipelines/operators/synthesis/reasoning/representations/memories 六模块；视频/3D/VLA/多模态推理 | 未核 | 2–3（多轮执行+流式；WF Table 1 列其有闭环/无训练） | 各任务 eval_func（导航保真/视觉质量等） | **转述**（CSDN 镜像教程+WF 论文 Related Work 原文）；官方仓未逐页 |
| **WRBench**（arXiv 2606.20545，2026-06-18） | 9,600 视频 / 23 模型 / 4 控制范式；Natural-25 场景；2,547 人工裁决 | 论文 arXiv 非独占许可；代码仓许可未核 | 2（相机干预开环生成+评分） | 人校准评分链；judgeability gate（Qwen-3-VL-8B）与 scorer（Qwen-3.5-9B）分离；DINOv2 视觉完整性代理 | **已核**（摘要+HTML 全文） |
| **MemoBench**（arXiv 2606.27537v6，2026-07-19） | 360 真值 clips（1080p，合成+真实）；10 模型（正文；摘要写 8，论文自身不一致） | 论文 CC BY 4.0；代码/数据许可未核 | 2 | ORS（SAM-3 检测式）+ PSNR/SSIM/LPIPS + CamCtrl + VQA 4 维（κ=0.85）；motion-gated 子集 | **已核**（HTML 全文） |
| **WROP**（arXiv 2609.28654，2026-09-23，Hokin Deng 等） | 150 认知任务×Blender 生成器；1.5M 训练样本（214GB）；300 考卷；14 模型 4,197 答卷 | **CC BY-NC 4.0**（数据+考卷；非商用） | 2（60 帧续写，开环） | 五件套真值契约（trajectory.npz+metadata.json）；盲测成对 Elo（人评） | **已核**（HF 官方数据卡两张）；论文全文未逐页 |
| **WBench**（arXiv 2605.25874，美团 LongCat+复旦） | 289 用例 / 1,058 turns / 20 模型 / 22 自动子指标 / 5 维 | MIT（09-24 快照 240★） | 2（多 turn 交互协议） | 专用视觉模型+LMM；人校准 ρ≥0.94 | **已核**（摘要+部分全文）；stars 快照 |
| **WorldMark**（arXiv 2604.21686v2） | 500 用例 / 10 模型；WASD 共享词汇+per-model adapter | MIT（09-24 快照 45★） | 1.5 | 控制系统视角：方向准确率/纯度、响应延迟、运动稳定性（逐轴）+记忆/画质套件 | **已核**（摘要）；stars 快照 |
| **KineBench**（arXiv 2607.19876，ECCV 2026，TeleAI 等） | 20 ManiSkill3 任务×4 套件；IDM-free | MIT（09-24 快照 6★） | **4**（视频→6D 位姿→仿真器回放） | 执行成功率 + SPARC 平滑度 + Maruyama 可操作度 | **已核**（摘要+全文检索）；stars 快照 |
| **cosmos-evaluator**（NVIDIA） | checker 微服务（hallucination/attribute/objects）+ Arbitrator 编排 | Apache-2.0（代码；SegFormer/CWIP 权重另附 EULA） | 1（开环视频评分） | 经典 CV（动态掩码差分+距离变换容差）+ LLM 出题/VLM 答题；NGC Arbitrator 容器并行分发+审计 | **已核**（NGC 官方描述+第三方 NOTICE 引官方仓）；09-24 快照 52★ |
| stable-worldmodel（galilai-group） | 采集→训练→MPC 评测统一平台 | MIT（09-24 快照 2,215★） | **5** | 任务级 MPC 评测 | 转述（09-24 已核，本轮未重验） |
| World-In-World（ICLR'26 Oral） | WM 经统一动作 API→LIBERO/导航 sim 执行计分 | 见仓库（09-24） | 4.5–5 | 闭环任务效用 | 转述（09-24 已核；WF Table 1 亦列其闭环） |
| AutoWorldModelBench（EA） | registry[N,34]+states[T,N,23]+actions[T,7] 状态张量协议 | BSD-3-Clause（09-24） | 4 | 实体状态级 | 转述（09-24 已核） |

**生态结构判断**（事实陈述）：基准层（WRBench/MemoBench/WROP）已把 L-St 病灶钉死；编排层（WorldFoundry/OpenWorldLib）已把"执行+证据"统一；**没有任何一家提供"canonical 世界状态语义（身份/时间/所有权/置信度）+ 单写者裁决 + 位级可复现闭环 episode"**（09-24 负检索 + 本轮 WF 全文核验后维持；WF 的 state traces 只是 ArtifactRef 的一种产物类型，不是运行时状态契约，已核）。

---

## 3. 关键文献精读

### 3a. WorldFoundry: Towards Unified Infrastructure for World Intelligence（arXiv 2611.10771v1，2026-11-07，OpenEnvision/NTU/Princeton 等）

**核验方式**：arXiv HTML（仅 §1–2 渲染成功）+ **39 页 PDF 全文提取**（pypdf）+ GitHub README + 官方文档站 5 页（architecture/design/evaluation-core/workflow/model-runtime）。以下全部一手已核。

#### 3a.1 问题

视觉可信≠世界建模：产物可能缺持久状态、物理动力学、因果一致的交互响应。感知中心基准+碎片化工作流掩盖了"生成式基础模型 vs 生成式世界模型"的区别。瓶颈不仅是算法的，也是基础设施的（摘要/§1 原话）。

#### 3a.2 定义与能力框架

- **操作性定义**：生成式世界模型 = 维持持久、历史条件化环境表征，随动作/干预更新该表征，并生成反映观察与反事实干预后果的时间一致轨迹的预测动力系统。**持久性是操作性的**：干预必须在状态中留下约束后续预测的痕迹，且每个新观察必须修订该状态——两者同时成立闭环才算闭合（§1 原话，已核）。
- 三操作抽象：state inference / forward simulation / interactive response（功能级而非架构级，不要求每个系统实现全部三者）。
- 能力脊柱：Perceptual Grounding（基础）→ Persistent State Modeling → Dynamics-Aware Prediction → Closed-Loop Interaction（脊柱）+ Controllable Transformation（正交能力）。**"closure, not plausibility"是划界判据**（已核）。

#### 3a.3 manifest 执行机制与两级产物契约

- **声明式 manifest**：模型/基准目录条目记录来源、checkpoint 引用、运行时绑定、安装 profile、硬件期望、验证注记、已知 blockers；`integration_status`（公开支持声明）与 `backend_stage`（实现深度）分离，只有 `integrated + in_tree_runtime` 才是完整可运行集成（model-runtime 文档原话，已核）。
- **执行链**：surface（CLI/TUI/Studio/Python API/MCP）编译 intent → `GenerationRequest`（ids/task/inputs/controls/expected output schema）→ `WorldModelRunner`（最小接口 from_config/generate/cleanup）→ pipeline+operator 原生执行 → `GenerationResult`（status/timing/errors/resolved config/artifact refs）。
- **ArtifactRef**（已核，PDF §4 原文）：每个物化输出由 ArtifactRef 描述——**semantic role + URI**，可选 MIME type、媒体属性、文件大小、**content hash**。JSON 可序列化，跨进程/环境/机器持久可检。产物类型含 media/geometry/4D/actions/trajectories/**state traces**。
- **输出目录即契约**（Architecture 文档原话）：`run_manifest.json`（身份+状态+reproducibility fingerprint）、`execution_plan.json`、`requests.jsonl`、`results.jsonl`、`sample_ledger.jsonl`、`artifacts.jsonl`、`metrics/per_sample.jsonl`、`metrics/summary.json`、`scorecard.json`、`report.md`（派生，非权威）。
- **执行与评分硬分离**：模型运行时永不 import 基准评测器；基准评产物不评 checkpoint；生成一次可异地异时评分（已核）。

#### 3a.4 evidence-aware scorecard

- 三级证据声明（evaluation-core 文档 + PDF §4，已核）：**exact_reproduction**（全 pin 官方设置/数据/评测器/运行时）→ **benchmark_comparable**（官方数据+评测不变）→ **diagnostic**（自定义数据或仅指标）。caller flag 只能降级不能升级。
- `score_valid`（评测器产出可用分）与 `leaderboard_valid`（另需可比溯源+基准运行器证据）分离；缺数据/跳样本/未验证官方路径**不得**在 scorecard 里变成表面成功（"unavailable work stays unavailable"，已核）。
- 原则："Evidence over claims"、"Official validation is not benchmark parity"（已核）。

#### 3a.5 规模与验证

- **270 模型条目 / 74 基准 / 62 注册指标条目**（摘要原话，已核）；正文另称"over 100 benchmark and metric implementations"，每个模型 runner 经文档化参考工作流端到端执行验证（已核）。
- **128×NVIDIA A100/H100（各 80GB）**（§5.1 原话，已核）；其他硬件评测留作 future work；B200/B300 受限（§6，已核）。
- 基准目录含 WBench、World-in-World、STEVO-Bench（"control, physics, and unobserved state evolution"）、PAWBench（stochastic dynamics calibration）、T2VWorldBench、WorldOlympiad 等，状态"Integrated & Verified"（PDF 附录表，已核）。

#### 3a.6 对比表（Table 1）对闭环支持的原文表述 —— **上游结论修正点**

- 列定义原文："**Closed loop: controls depend on prior outputs or environment feedback**"；"Training: protocol-specific recipe only"（✓=支持，中间符号=专门支持，×=无文档化工作流）。
- **WorldFoundry 自身行：Video✓ 3D/4D✓ Inter.WM✓ Embodied✓ Model runner✓ Hosted API✓ Closed loop✓ Training× Protocols✓ Evidence✓ WM def.✓**（PDF 提取，11 符号与 11 列对齐无歧义，已核）。
- OpenWorldLib/Cosmos/Genie Envisioner/LeRobot/StarVLA/World-in-World 等行亦含多个 ✓，但 PDF 文本提取丢失"专门支持"图标符号，**逐列对齐不可靠**（未核）。
- **因此：上游笔记"其对比表自认没有比较系统完全支持闭环"无法从一手表格复现**。可成立的表述是：WF 的闭环=控制依赖反馈（具身 rollout + 交互会话），其闭环证据规模有限——§5.2.4 原文：OpenVLA 经 WF 在 LIBERO-10 上**每任务仅 1 个记录 episode**，任务(a) 179 步完成，(b)(c) 达 520 步上限未完成（已核）。
- 与 OpenWorldLib 关系（Related Work 原话，已核）："the most closely related infrastructure for heterogeneous integration"，其 pipeline/operator/representation/reasoning/memory 模块统一视频/空间/具身系统，支持多轮执行与从保存结果评测；WF 的差异是**把操作性世界模型判据与显式执行契约耦合**。README 致谢 OpenWorldLib（OpenDCAI）/FastVideo/allenai vla-evaluation-harness（已核）。

#### 3a.7 局限（§6 原文，已核）

目录收录≠可运行≠资产就绪≠官方验证≠基准资格（五态分离）；**当前只做 inference/interaction/artifact/evaluation，通用训练基础设施是 future work**；优化机制（FlashAttention/Triton/NVFP4/并行）效果未系统刻画；**专用 serving engines 将通过模块化后端适配器集成（future work）**，改进以端到端延迟/吞吐/内存/扩展效率/复现性评估，且优化输出必须落在声明的保真容差内。

#### 3a.8 它统一了什么 / 没统一什么（供差分 ADR）

| 统一了（已核） | 没统一（已核其缺席） |
|---|---|
| 执行编排：manifest→runner→产物，六 surface 同一契约 | **世界状态语义**：无身份铸造/时间轴/所有权裁决/置信度契约；"Model differences are preserved"是明文设计原则 |
| 产物溯源：ArtifactRef URI+hash，输出目录即契约 | **逐 tick 在环评分**：评分在执行后（embodied rollout 亦按 episode 落盘后评） |
| 证据分级：diagnostic/comparable/exact + eligibility | **位级执行复现**：只有计划指纹+产物 hash，无确定性 episode 承诺 |
| 模型/基准目录：270/74/62，五态就绪分离 | **实时帧预算**：无延迟预算契约，serving 是 future work |
| 具身闭环 rollout 路径（policy↔sim，LIBERO/RoboTwin） | **真值/信念分离**：无 BeliefView 类结构；策略读仿真器观测，无引擎持有 canonical 状态 |

**与 OmniWorld 的关联**：它是"基准之下的编排层"的最强占用者，与 OmniWorld"基准之下的运行时层"主张**不同层**；其 270 模型目录可作生产者候选池，其产物契约可作适配目标（§8 互补面）。其论文对"持久状态/闭环"的**定义**与 OmniWorld 契约高度同向（这是思想同源证据），但其**实现**停在执行与证据（这是差分证据）。

**原文阅读指引**（供 ADR-0017 起草者按图索骥，本轮已核位置）：
- 定义与闭环判据：PDF §1（p.2–3）"closure, not plausibility"段；§3.1–3.2 能力脊柱。
- Table 1 与列定义：PDF p.5（含"Reading the table"脚注）——**引用闭环列必读此处**。
- 契约细节：PDF §4（ArtifactRef 字段原文）；官方文档 evaluation-core.md（claim levels/score_valid/leaderboard_valid）与 workflow.md（输出文件清单）。
- 闭环实证规模：PDF §5.2.4（LIBERO/RoboTwin 判据 + OpenVLA LIBERO-10 每任务 1 episode）。
- 演进方向（盯防用）：PDF §6（serving engines/训练/数据准备/保真容差验收）。
- 硬件与验证口径：PDF §5.1（128×A100/H100 80GB）；README System Requirements（Linux+CUDA 12.8+）。

### 3b. WRBench: Current World Models Lack a Persistent State Core（arXiv 2606.20545，2026-06-18，USTC/X-Humanoid/中科院自动化所等）

**核验方式**：摘要页 + HTML 全文（本轮 curl 提取）。已核。

- **问题**：现有基准奖励保真/运动/相机可控，从不问"相机不看时世界是否继续演化"。物体恒存要求内部世界状态**与观察解耦**地持续演化（"月亮无人看时仍沿轨道"）。
- **方法**：把**相机运动当作对可观察性的干预**（只改观察角度，不改世界事件）；Natural-25 场景 + WRBenchLib 统一场景/事件/相机控制工具；评分分解为人校准链三问：①相机是否执行了请求的交互 ②在视野内场景是否连续可辨认 ③返回目标是否与已启动的事件一致。六维：相机控制(i–ii)、视觉完整性(iii，DINOv2 特征代理 min(全局,局部))、可见空间/状态一致(iv)、re-observation support(v)、re-observed 空间/状态一致(vi)。**judgeability gate（Qwen-3-VL-Instruct-8B）与 scorer（Qwen-3.5-9B）分离**，不可判定记 NA。2,547 条去重人工裁决按输出同粒度校准。
- **关键结果**：9,600 视频 / 23 模型 / 4 控制范式（源视频条件/几何缓存条件/模型推断条件/纯 prompt 条件）；顽固发现=**模型把世界当"跟拍镜头"（tracking shot）：返回目标停在被抛弃时的状态，而非事件推进后的状态**；该失败跨范式/跨家族/跨规模增量复现 → 更干净画面、更紧控制、更富几何先验、更大参数量都不自动带来世界状态演化。识别"preservation–access–re-observed-consistency  gap"：可见质量与返回可达性决定"返回测试是否被提出"，而返回状态正确性是独立能力。
- **主张**：物理状态核（state kernel）稳定性 + 视角干预下 worldline 一致性应成为世界模型设计的一等目标。
- **局限**：开环生成+事后评分（无运行时闭环）；VLM 评分链依赖 judgeability 设计；真值来自事件脚本+人校准而非逐帧状态真值；代码仓许可未核。
- **与 OmniWorld 的关联**：病灶诊断书——"事件在无观察期间不推进"正是引擎持有事件时钟/权威状态的理由（相容性矩阵 D1 行）；其 NA 语义与 E1 M2"空集≠稳定"同构，可互相引用；它没有契约/复现/运行时，是"基准层已占用、运行时层空白"的最直接证据。

### 3c. MemoBench: Benchmarking World Modeling in Dynamically Changing Environments（arXiv 2606.27537v6，2026-07-19，JHU/MIT 等，Alan Yuille/Paul Liang/Yilun Du 组）

**核验方式**：摘要页 + HTML v6 全文（本轮 curl 提取）。已核。

- **问题**：既有记忆基准要么只测"目标在视野内"的一致性，要么遮挡期间场景静态（无事件）→ 测不到"世界在看不見时变了没有"。
- **方法**：**disappear-and-reappear 范式**于动态变化环境：目标经历物理过程（溶解/燃烧/扩散等不可逆变化）→ 消失于视野（D 相）→ 必须以**更新后的状态**重现（R 相）。360 真值 clips（1920×1080，合成+真实），V-D-R 三相。指标组：ORS + 像素保真（PSNR/SSIM/LPIPS，分相计算 R 相跌落 Δ）+ 相机可控性；VQA 4 维（Instruction Following / Object & Background Consistency / Continuity of Memory / Physics Adherence），LLM scorer，与人工多数一致 92.9%（κ=0.85）。
- **ORS 定义**（已核）：因 R 相视角不同于 V 相，mask IoU 不可靠 → 检测式：SAM-3 文本提示分割逐 R 帧查询目标，覆盖率过滤（0.05%–50% 图面积，fallback 0.05%–70%）拒噪声/背景；S_ors = (n_d/n_R)·(1/n_d)Σp_i（检出率×平均置信度）。
- **关键结果**：**无模型 ORS 超过 0.6**（最高 HunyuanWorldPlay 0.582；LingBot-World 0.381）；**无相机条件模型靠近静态视频刷高一致性分**（LTX-Video ORS 0.330 但 CamCtrl 低，VQA 一致性两维第一）→ **motion-gated 评测**（≥90° GT 相机旋转子集）必要；即使相机条件模型，物体恒存也不随轨迹控制能力涌现；无单一模型同时强 CamCtrl+高 ORS+好画质。
- **数字纪律注**：正文"ten models"（7 C+TI2V：LingBot-World/Wan2.2/FantasyWorld/HunyuanWorldPlay/HunyuanGameCraft/Matrix-Game 2.0/Stable Virtual Camera；3 TI2V：Open-SoRA/LTX-Video/CogVideoX），**摘要写"eight"**，附录分相分析亦写"eight models"——论文自身口径不一致，引用时须注明（已核该不一致本身）。上游笔记"10 开源模型"与正文一致。
- **局限**：开环、视频级、60 帧量级短程；ORS 依赖 SAM-3 检出（检测器上限≠记忆上限，论文以 100% 检出率对照排除）；代码/数据许可未核。
- **与 OmniWorld 的关联**：motion-gated 与 E1"指标会骗人"同构（外部独立证据）；其"遮挡期间事件推进"考法可直接移植为 B' 考场的题型；它同样无运行时/契约/复现。

### 3d.（补充）WROP: Training Object Permanence in World Models（arXiv 2609.28654，2026-09-23）

**核验方式**：HF 官方数据卡两张（train + benchmark，本轮检索全文）；论文全文未逐页。数据卡内容已核，论文细节标转述。

- **五件套评分契约**（已核）：每题 `input_video.mp4`（60 帧 1280×720@24fps）+ `target_video.mp4`（60 帧参考答案）+ `prompt.txt` + `trajectory.npz`（**全 120 源帧逐刚体位姿真值**）+ `metadata.json`（参数/溯源/video_split）。
- **生成器即数据工厂**（已核）：150 手工认知任务（6 类；groundtruth.day 称 90 permanence+60 solidity，转述），Blender 生成器随机化速度/光照/相机角等 nuisance 参数、保留认知结构，每任务 10,000+ 样本；1.5M 样本/214GB/150 tar。
- **考卷与答卷全开源**（已核）：300 题（150×2）；14 模型（1 自研 PWM-WROP + 5 开源 + 8 商业）4,197 mp4（runway-aleph-v2v 297）；盲测成对 Elo，PWM-WROP 续写类第一、总第三（仅次于两个统计并列的商业 reference-to-video 系统）——Elo 细节/20 名评分者为 groundtruth.day **转述**。
- **许可**：CC BY-NC 4.0（署名 Hokin Deng，非商用；商用另联）——已核。**对 OmniWorld 的含义**：考卷/真值可非商业研究使用，不可进商业产品路径；生成器模式（参数→带真值世界）可学习重建，不受 NC 传染（学形态不抄资产，同 09-24 §4 纪律）。
- **与 OmniWorld 的关联**：证明"生成器出考卷"可行且规模化（B' 首战的直接先例）；但生成器不在契约下、真值/信念不分离、无运行时闭环、无位级复现——L2×D2 组合空间维持（上游打折表结论，本轮核验后维持）。PWM-WROP 16B（论文摘要）vs 模型卡 BF16 15B（groundtruth.day 转述）——口径不一，8GB 本机均不可跑（未实测，按参数量推断）。

---

## 4. 开放问题（只陈述证据，不裁决）

基准层/架构层/基础设施层被占用后，**运行时层还剩什么**——本轮证据清单：

1. **状态语义统一仍是空白**：WF 明文"保留模型原生语义"，其契约（GenerationRequest/Result/ArtifactRef）统一的是**执行与产物**，不是身份/时间/所有权/置信度；OpenWorldLib 的 memories 模块是模型内上下文，非共享 canonical 状态（转述+已核其文档描述）。09-24 负检索（"world state server"等词只返回课程作业）本轮未见反例。
2. **闭环的"语义深度"差**：WF 闭环=控制依赖反馈（已核其定义与 LIBERO 路径），但每任务 1 episode 的证据规模、无真值/信念分离、无逐 tick 在环评分、无单写者裁决。"闭环存在"与"闭环可测量/可归因/可复现"是两件事——后者无人提供（E1 三件套主张被强化，但须改用 §3a.6 的准确表述）。
3. **位级复现 oracle 无人做**：WF 计划指纹+产物 hash ≠ 确定性 episode；VLA 评测普遍做不到（闭环 note §2.3）；MJWarp 确定性空桩说明连物理仿真器侧都未解决（仓库已核）。
4. **校准测量无人做**：WF 无 ECE/置信度契约（仅集成 PAWBench 基准）；11/11 被研对象置信度非一等公民（仓库已核）；E1 证明 per-producer ECE 是唯一可靠退化检出器。
5. **实时帧预算无人承诺**：WF serving engines 是 future work（已核）；"perceive→decide→act→observe 端到端延迟分解"仍是缺口（闭环 note §7）。
6. **需求侧证据仍是推断**（09-21 §5 负面证据义务，本轮无变化）：碎片化承认（WorldMark 原话）+ 集成劳动重复可见，但"签名式"运行时需求未找到；Gate 2 外部信号仍是硬退出条件。
7. **反向风险（对方演进）**：WF 已把 state traces 列为产物类型、已定义持久状态/闭环的操作性判据、future work 含 serving+训练+数据准备——**它向运行时层演进的接口已经预留**（已核）。窗口不是静态的。
8. **8GB 本机可行性分层**：评测环本身零外部依赖可跑（参考模式已证明，RT-DETR CPU EP ~518–537ms/帧）；WF 官方要求 Linux+CUDA 12.8+ 统一环境（已核 README）→ 本机 Windows 8GB 不能直接跑 WF，只能离线数据生产者/适配器模式（未实测）。
9. **跨基准可比性无权威**：WF 用 claim levels（diagnostic/comparable/exact）把"可比性"变成显式声明（已核），但各基准的评分链（WRBench 双模型 gate / MemoBench SAM-3+VQA / WROP 人评 Elo）互不等价；同一模型在不同基准的分数不可直接合成——"统一目录"不等于"统一量纲"（事实陈述，WF 明文不做 undifferentiated ranking，已核）。
10. **judgeability 军备竞赛**：WRBench 用 NA 语义防"不可判定刷分"、MemoBench 用 motion-gated 防"静态刷分"、E1 用空集排除防"零信念刷分"——三者同构但各自手写；**"指标有效性门控"本身是否需要成为契约级共享语义**（而非每个基准重新发明），是运行时层的候选问题（只陈述，不裁决）。

---

## 5. 仓库已有资产引用

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| [2026-11-13-six-direction-innovation-integration.md](2026-11-13-six-direction-innovation-integration.md) §1 D2 / §3 | 直接上游：新颖性打折表、WorldFoundry 差分要点五条（**其中"对比表自认闭环无完全支持"一条已被本轮一手核验修正，见 §3a.6**）；首战 B' 提案与推翻条件 |
| [2026-09-21-direction-decision.md](2026-09-21-direction-decision.md) §4.7 / §5 | 评测生态碎片化实证（WorldMark "no shared protocol" 原话）；白地判定与负面证据义务；Strategy D 定位 |
| [2026-09-24-github-reuse-map.md](2026-09-24-github-reuse-map.md) §1 / §5 / §6 | 18 家 stars/许可/闭环度快照；敲门砖贡献地图（WorldFoundry 在列）；盯防清单触发条件 |
| [2026-10-27-interactive-closed-loop-route.md](2026-10-27-interactive-closed-loop-route.md) §6–7 | E1 实测：覆盖率 -31% / ECE +47.8% / divergence 反降 / identity_stability 反升；M1 成对发布 / M2 空集≠稳定 / M3 per-producer；world_hash/ledger_hash 位级证据；闭环指标缺口表 |
| projects/newton-mujoco-warp.md（经上游引用） | MJWarp GPU 默认非确定、CONTACTS 确定性空桩 → J2 位级比对 spike 楔子 |
| deep/2026-11-05-deep-research-index.md §二（经上游引用） | 置信度在 11/11 被研对象中非一等公民 → 契约⑦引擎自建 |

---

## 6. 待核验清单

1. **WorldFoundry GitHub 仓库未逐页**：代码/catalog YAML/issues/PRs/当前 stars/ releases 未读；本轮只核 README+官方文档站 5 页+论文 PDF。贡献可行性（CONTRIBUTING/DCO/Agent Skills）仅 README 级。
2. **Table 1 非 WF 行的逐列对齐**：PDF 提取丢"专门支持"图标 → OpenWorldLib 等行的 Closed loop/Training 列精确值未核（需查 PDF 原图或 HTML 修复版）。
3. **各基准代码/数据许可细节**：WRBench（JinPLu/WRBench）、MemoBench（MemoBench-Team）仓库 LICENSE 未逐页；OpenWorldLib 许可/stars/硬件口径未核（CSDN 转述：80/141GB GPU 测试、Linux、CUDA 12.1）。
4. **WROP 论文全文**：Elo 协议细节、20 评分者、六类任务划分、PWM-WROP 参数量（16B vs 15B）——HF 卡已核，论文未逐页。
5. **8GB 离线评测可行性**：WF 统一环境本机不可跑（Linux-only 已核），但"离线产物→WF 评分"或"WROP 考卷→本机引擎"路径未实测；ForgeWM/Solaris/StatePlay 小变体 8GB 入环核验仍是 queue 既有任务（上游 §5 前置 2）。
6. **MemoBench 模型数 8 vs 10**：论文自身不一致，引用需注明版本（v6）。
7. **ECE 经典文献**（Naeini 2015 / Guo 2017）：常识性引用，本轮未重验原文。
8. **stars/活跃度**：全部为 09-24 快照，本轮未重验（WF 07-11 开源首日 100+★ 为 README 自述，已核该自述）。

---

## 7. 来源与核验状态

| # | 来源 URL | 关键内容 | 状态 |
|---|---|---|---|
| 1 | https://arxiv.org/abs/2611.10771 + https://arxiv.org/html/2611.10771v1 | WF 摘要/定义/§1–2；270/74/62；CC BY 4.0；2026-11-07 | **已核**（HTML 仅 §1–2 渲染） |
| 2 | https://arxiv.org/pdf/2611.10771v1（39 页全文提取） | Table 1 原表+列定义；ArtifactRef 字段；128×A100/H100 80GB（§5.1）；三级证据；LIBERO-10 每任务 1 episode（179/520 步）；§6 局限（训练/serving future work） | **已核**（PDF 文本提取；表格图标符号丢失一项标未核） |
| 3 | https://github.com/OpenEnvision/WorldFoundry（README raw） | Apache-2.0；2026-07-11 开源；Linux+CUDA 12.8+；Studio/TUI/CLI；致谢 OpenWorldLib/FastVideo/vla-evaluation-harness | **已核**（README；仓库其余未逐页） |
| 4 | https://openenvision.github.io/WorldFoundry/docs/maintainers/architecture（+ /design /evaluation-core /workflow /model-runtime 的 .md 版） | 七职责所有权；输出目录即契约；GenerationRequest/Result/BenchmarkSpec/Metric；claim levels；RunPlan 指纹；embodied closed-loop rollout；OpenVLA callable entrypoint | **已核**（官方文档 5 页全文） |
| 5 | https://arxiv.org/abs/2606.20545 + https://arxiv.org/html/2606.20545v1 | WRBench 9600/23/4 范式；Natural-25；六维；judgeability gate 双模型；2547 人工裁决；tracking shot 结论；GitHub JinPLu/WRBench | **已核**（摘要+HTML 全文） |
| 6 | https://arxiv.org/abs/2606.27537 + https://arxiv.org/html/2606.27537v6 | MemoBench 360 clips/V-D-R/ORS 公式/SAM-3/ORS<0.6（max 0.582）/motion-gated ≥90°/VQA 4 维 κ=0.85/10 模型（摘要 8） | **已核**（HTML v6 全文） |
| 7 | https://huggingface.co/datasets/Hokin/object-permanence + /object-permanence-benchmark | WROP 150 任务/1.5M/214GB/300 题/14 模型 4197 答卷/五件套/CC BY-NC 4.0/arXiv 2609.28654 | **已核**（HF 官方数据卡） |
| 8 | https://huggingface.co/papers/2609.28654 + https://groundtruth.day/news/wrop-object-permanence-world-models.html | WROP 摘要；Elo 第三/续写第一；20 评分者；90+60 任务划分；PWM-WROP=Cosmos3-Nano 微调/BF16 15B | 摘要**已核**；细节**转述**（groundtruth.day 二手） |
| 9 | https://arxiv.org/abs/2605.25874（经检索含 PDF 片段） | WBench 289/1058/22/20/5 维；158 导航共享子集；ρ≥0.94；LMM 物理评分局限自述 | **已核**（摘要+部分全文） |
| 10 | https://arxiv.org/abs/2604.21686 | WorldMark WASD adapter/10 模型/500 用例/控制系统指标/"no shared protocol" 原话 | **已核**（摘要 v2） |
| 11 | https://arxiv.org/abs/2607.19876 + https://arxiv.org/html/2607.19876 | KineBench IDM-free/YOLO→MoGeV2→FoundationPose→pyroki→ManiSkill3/SPARC+Maruyama/20 任务 4 套件/ECCV 2026/Wan2.2 60→30% | **已核**（摘要+全文检索） |
| 12 | https://catalog.ngc.nvidia.com/orgs/nvidia/cosmos/containers/arbitrator + https://github.com/nebius/nebius-physical-ai/blob/main/skills/NOTICE-NVIDIA-COSMOS-OSS | Arbitrator 编排（发现/并行分发/审计/submit-and-poll）；cosmos-evaluator Apache-2.0（nvidia-cosmos org）；hallucination=经典 CV；SegFormer/CWIP 权重 EULA | **已核**（NGC 官方+第三方 NOTICE 引官方仓；后者为二手引用） |
| 13 | https://blog.csdn.net/gitblog_00476/article/details/151456565 等 OpenWorldLib 镜像教程 | 六模块结构/流式推理/80–141GB GPU 测试/Linux/CUDA 12.1 | **转述**（CSDN 镜像，官方仓未逐页） |
| 14 | 仓库四份上游文档（§5 表） | E1 数据/18 家快照/打折表/碎片化实证 | **已核**（本轮只读复用了结论；E1 数字以仓库报告为准） |
| 15 | VBVR-Pro（VLM-as-judge 失效模式） | 经 WROP 参考文献检索摘要见到 | **转述/未核** |

---

## 8. ADR-0017 差分输入摘要（供主对话起草差分 ADR）

> 纪律：本节只陈述事实与证据，不做方向裁决。所有"已核"指本轮一手核验（§7）。

### 8.1 逐维对比表

| 维度 | WorldFoundry（已核） | OmniWorld（仓库已核） | 差分性质 |
|---|---|---|---|
| **状态语义** | 无 canonical 状态契约；明文保留模型原生语义；state traces 仅为 ArtifactRef 产物类型 | 九条契约：身份铸造/单一 tick 域/字段类单写者/Proposal 裁决/信念不回写真值/置信度一等公民 | **层差**：执行契约 vs 世界状态契约 |
| **闭环** | Table 1 自身行 Closed loop ✓；定义=控制依赖先前输出/环境反馈；具身 policy↔sim（LIBERO/RoboTwin）+交互会话；LIBERO-10 每任务 1 episode | grounding loop：仿真器=真值权威（方案 b）、引擎镜像+逐 tick 在环评分、BeliefView（Phase 4 首切片）、真值/信念分离 | **语义深度差**：闭环存在 vs 闭环可测量/可归因 |
| **复现** | RunPlan 指纹（requests.jsonl 跨机器一致）+ ArtifactRef content hash + run_manifest reproducibility fingerprint；**不承诺执行位级确定** | 参考模式位级 oracle：同 seed 逐字节 episode + world_hash/ledger_hash（E1-5 实证）；MJWarp 确定性 spike 在队列 | **oracle 差**：计划级/产物级 vs 执行级 |
| **校准** | 无 ECE/置信度契约；集成 PAWBench（stochastic dynamics calibration 基准） | per-producer ECE 已实现且经 E1 验证为唯一可靠退化检出器（+47.8%）；动作/状态置信度分 bin 设计 | **测量能力差** |
| **执行编排** | 强：manifest/五态就绪/六 surface/原生扩散内核/128 GPU 验证/270 模型端到端 runner 验证 | 弱：单机单循环，适配器机制已验证（RT-DETR/DA-V2 零核心改动）但无目录/编排规模 | **对方强项**（不与其竞争编排） |
| **模型目录** | 270 模型/74 基准/62 指标，evidence scorecard 三级声明 | 无目录；生产者候选池可借用其目录 | **互补** |
| **评分时机** | 产物中心、执行后离线评分（输出目录即契约） | 状态中心、逐 tick 在环评分（账本+指标同循环） | **架构差** |
| **指标方法论** | scorecard 防"缺数据变成功"；无 motion-gated/成对发布类契约 | E1 M1/M2/M3 三条定案（成对发布/空集≠稳定/per-producer） | 同向但层次不同（证据诚实 vs 指标判别力） |
| **硬件/许可** | Linux+CUDA 12.8+ 统一环境；Apache-2.0（自研）；验证仅 A100/H100 | Windows 本机 8GB 参考模式已证明；评测环零外部依赖 | 本机可行性差（WF 不能本机跑，已核 README） |
| **训练/serving** | 均 future work（§6 原文） | 不训练（PLAN 红线）；实时帧预算是主张 | 双方都未兑现实时 serving |

### 8.2 互补面（贡献期权，事实陈述）

1. **目录→生产者池**：WF 270 模型目录含 D1 候选（Matrix-Game 2/3/3.5、LingBot、ABot-World、HY-WorldPlay、Cosmos 系等，model-runtime 文档点名，已核）——OmniWorld 的 8GB 入环标的筛选可直接消费其 catalog YAML（许可/硬件/接口三件）。
2. **OmniWorld→WF 闭环后端**：WF 的 embodied closed-loop 路径已预留 callable entrypoint + policy adapter 接口（OpenVLA  worked example，已核）；"引擎持有真值世界 + 位级复现 episode"可作为其闭环评测的运行时后端候选——贡献接口存在。
3. **产物契约对接**：WF 的 GenerationResult/ArtifactRef 与 OmniWorld 的 MCAP 录播/账本可互转（形态同构：JSONL 行+哈希溯源）；WROP 五件套考卷可经适配器进 OmniWorld 闭环考场（CC BY-NC 限非商业研究）。
4. **敲门砖轨道**（09-24 §5 既有决策）：WF 在贡献目标清单内（"先盯防大更新，再评估贡献还是差异化"）——大更新已落地（2026-11-07），**一周内差分 ADR 的触发条件已满足**（上游强制动作）。

### 8.3 风险面（对方补上状态语义+闭环的触发证据清单）

以下任一出现即窗口收窄信号（均为可核验外部事件，非预测）：

1. WF 契约族新增**身份/所有权/置信度字段**（GenerationRequest/Result 或 state traces 升级为 canonical schema）——盯 `evaluation/api/` semver 变更与 catalog YAML。
2. WF 提供**逐 tick 在环评分**或真值/信念分离结构（而非 episode 落盘后评）。
3. WF serving engines 落地并承诺**延迟预算/确定性执行**（其 §6 已预告以"reproducibility"为优化验收维度之一——措辞已存在，盯其是否升级为位级承诺）。
4. WF 训练扩展落地（versioned dataset manifests/checkpoint lineage 已预告）→ 生命周期复现闭环。
5. 社区信号：评测团队公开采用 WF 作闭环默认路径（Gate 2 Track A 类证据被对方收割）。
6. OpenWorldLib memories 模块演化为跨模型共享状态（当前为模型内上下文，转述未核）。

### 8.4 什么证据会让我们转向贡献而非自建（供 grill 提问）

- **证伪自建必要性**：若 §8.3 第 1+2 条同时出现（状态语义+在环评分被 WF 覆盖），则"运行时层空白"主张失效 → 转向贡献（其接口已预留，§8.2.2）。
- **证伪差异化价值**：若 B' 首战产出的位级复现闭环报告无法展示"只有 OmniWorld 能测的指标"（09-21 风险 #3 判据原话），则差异化不成立。
- **支持贡献优先**：若 WF 社区对"闭环运行时后端/状态契约"类 issue/PR 有明确欢迎信号（maintainer 回应/roadmap 收录），贡献的 Gate 2 信号成本低于自建。
- **支持自建维持**：本轮证据现状——WF 明文保留原生语义、评分在执行后、无位级执行承诺、serving/训练均 future work（全部已核）→ **运行时层缺口在本轮核验后仍然成立**；但差分 ADR 的论据必须从"对方自认无闭环"（已被修正）改为"**闭环语义深度+状态契约+位级 oracle**"三点。
- **纪律提醒**：09-21 负面证据义务仍有效——运行时需求是推断的（碎片化承认+集成劳动重复），不是签名式需求；Gate 2 外部信号硬退出条件不变。

### 8.5 给起草者的三句话总结

1. WorldFoundry 统一了**执行与证据**（manifest/ArtifactRef/scorecard，270/74/62，128 GPU），没统一**世界状态语义**（身份/时间/所有权/置信度）——这是层差不是竞品差。
2. 它**有**闭环（Table 1 自身行 ✓，具身 rollout 路径真实存在），但其闭环是"控制依赖反馈"，不是"canonical 状态+真值/信念分离+逐 tick 在环评分+位级复现"——差分必须打语义深度，不能再打"有无"。
3. 互补接口已预留（callable policy adapter/目录/产物契约），风险接口也已预留（state traces/serving/训练 future work）——贡献期权与盯防触发器应同时写进 ADR-0017。

---

## 附：术语对照（供 ADR 起草统一口径）

| 英文 | 中文/本 note 用法 | 出处锚点 |
|---|---|---|
| persistent state core / state kernel | 持久状态核 | WRBench 标题与主张 |
| observability intervention | 可观察性干预（相机转开/遮挡） | WRBench §1 |
| disappear-and-reappear / V-D-R | 消失-重现范式（可见-消失-重现三相） | MemoBench |
| ORS (Object Reappearance Score) | 目标重现分（检出率×平均置信度） | MemoBench 式(6) |
| motion-gated evaluation | 运动门控评测（≥90° GT 相机旋转子集） | MemoBench 附录 A.7.2 |
| judgeability gate | 可判定性门（不可判定记 NA 而非 0） | WRBench §3.4 |
| evidence-aware scorecard | 证据感知记分卡（三级声明+eligibility） | WF §4 / evaluation-core 文档 |
| claim level: diagnostic / benchmark_comparable / exact_reproduction | 证据三级：诊断/基准可比/精确复现 | WF evaluation-core 文档 |
| ArtifactRef | 产物引用（semantic role+URI+hash） | WF §4 |
| closed loop（WF 定义） | 控制依赖先前输出或环境反馈 | WF Table 1 脚注 |
| ECE / reliability diagram | 期望校准误差/可靠性图 | §1.5（经典方法） |
| per-producer decomposition | 按生产者分解指标 | E1 M3 |
| bit-level reproducible episode | 位级可复现 episode（同 seed 逐字节） | 闭环 note §2.3 / E1-5 |

---

*本 note 由 T8 研究子代理产出（2026-11-13）。唯一写入=本文件；未改任何代码/PLAN/CURRENT/research-queue/既有文档。所有裁决留给 owner grill。*