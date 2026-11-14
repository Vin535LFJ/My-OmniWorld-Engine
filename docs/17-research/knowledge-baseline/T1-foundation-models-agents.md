# T1 大模型与多模态基座 — 知识基线（2026-11-13）

> **Trust label: [Research → 知识基线]**——为 owner 的方向评审（grill → ADR-0016）提供 T1「LLM/VLM/Agent」的原理与进度地基。本文**只调研、不裁决**：不写"我们应该做 X"，与 OmniWorld 的关联只写"关联性"。数字纪律：凡未一手核验的明确标"未核/转述"；媒体转述不得写成官方口径。仓库已有两轮 note 与项目卡**只链接不复述**。
> 方法：先通读仓库三份资产（§5），再联网取一手来源（Genie 4 官方博文全文、Anthropic 官方公告全文、arXiv 综述 abs+HTML 正文，均 2026-11-13 抓取），其余进度数字按来源分层标注（§7）。

---

## 1. 原理与技术路线分类（重点）

### 1.1 LLM → VLM → Agent：能力演进的机制

**LLM 底座：从"预测下一个 token"到"可预算的思考"。**
- 机制：autoregressive next-token prediction；pretraining 把世界知识压缩进权重，post-training（SFT → RLHF / 结果驱动 RL）把"续写文本"塑造成"遵循指令、完成任务"。in-context learning 使模型无需重训即可靠少量示例适配新任务。
- 2025–2026 的关键增量是 **test-time compute scaling**：chain-of-thought、self-consistency、Tree-of-Thoughts、Reflexion 等把"多想几步"变成可靠性来源；产品形态是**推理强度档位化**——Gemini 的 thinkingBudget、Claude Fable 5.2 的 Adaptive Thinking（low→max 五档，官方公告一手）。能力增益越来越多来自**系统设计**（把 LLM 放进带预算的循环：限制时间/token/工具调用/副作用，按任务难度动态分配思考）而非单纯加大底座（arXiv 2601.01743 综述观点，检索确认）。
- 训练侧的对应演进：RLHF（人类偏好对齐）→ **RLVR / outcome-driven RL**（以可验证结果——测试通过、证明检查、任务完成——为奖励），使"主动试错、读报错、二次验证"成为习得行为而非提示词技巧（2510.16720 把 RL 定位为 model-native 范式迁移的算法引擎；Gemini 3.8 Flash"死磕"报道为同向转述证据）。这解释了为什么 2026 模型的长程工具调用稳定性跃升：奖励函数从"最短路径给答案"改成了"闭环验证直到通过"。
- 固有短板：LLM 不天然 grounded——没有外部证据与可执行检查时会产出"貌似合理的错误"。这是工具调用与检索增强设计的根本动机，也是"导演必须配验证器"的原理依据。

**VLM：把像素接进 token 流。**
- 主流机制：vision encoder + connector（projector）+ LLM backbone；对比预训练（CLIP 系）学"图文对齐"，instruction tuning 学"按指令看图说话/操作"。
- 两种集成路线，可审计性不同：
  1. **感知即工具**：把 OCR/检测/布局解析拆成独立工具，LLM 只做规划与证据整合——中间感知产物可检查、可验证后再提交下游动作（综述 2601.01743 明确推荐此分工以提升 auditability）。
  2. **原生多模态**：单一模型同时进出文本/图像/音频（Gemini Omni "create anything from anything"、Genie 4 原生音频，官方一手）。丰富度高，但中间状态不可检查。
- Agent 化用法：GUI/computer-use agent 以**截图为观察**、以点击/键入为动作（OSWorld 系基准）；VLM 当**帧级验证器**（Genie 4 回路里 Gemini 逐帧 review，官方一手）。后者是"导演-渲染器"架构的关键零件，其可靠性本身是开放问题（§4）。

**Agent：LLM 从"生成器"变成"控制器"。**
- 标准抽象：agent = backbone LLM + harness（综述 2503.16416 用语），或五元组 (policy, memory, tools, verifiers, environment)（2601.01743）。执行循环：observe → retrieve memory → propose action → **validate** → execute → update memory。
- 形式化：POMDP——世界状态部分可观察，**上下文窗口就是 belief state**，模型按 p(action | context, system prompt) 选动作（MALS 综述给出此形式化，Zenodo 非同行评审，仅取框架不取其数字）。这个视角解释了两件事：①agent 的"记忆"是信念不是真值，会陈旧、会漂移；②验证器与护栏不是附件，而是定义 agent 操作语义的部分。
- **pipeline-based → model-native 的范式迁移**（arXiv 2510.16720，检索确认）：早期 planning/tool use/memory 由外部脚本编排（ReAct、MRKL、LangGraph 图）；2025 起 RL 把这些能力**内化进模型参数**（outcome-driven exploration），Deep Research / GUI agent 是代表。两条路线并存：外部编排可审计、可换模型；model-native 长程更稳但黑盒。对"世界状态放显式还是隐式"的分叉（第一轮 note §1.2），这是同一分叉在 agent 层的投影。

### 1.2 工具调用 / 代码生成 / 规划：原理与失效模式

**工具调用（function calling / tool use）。**
- 原理：把动作空间收敛为 **typed JSON schema**——模型提议的动作先过 schema 校验与策略检查再执行，用结构化接口压制自由文本幻觉。子任务链：intent recognition → function selection → parameter-value mapping（综述 2503.16416 §2）。
- 标准化：MCP（Model Context Protocol，Anthropic 2024-11 开源，转述多源一致）把"模型↔工具"的胶水统一为 JSON-RPC + Resources/Tools/Prompts 三原语，stdio/HTTP+SSE 双传输；A2A（Google 2025-04 发起、后捐 Linux 基金会，转述）管"agent↔agent"的发现与委派（Agent Card / Task / Artifact）。两者是垂直/水平两层，不是竞争关系（转述，采用率数字未核见 §6）。
- 失效模式（综述 + 工程转述互证）：选错工具（description 质量直接决定路由准确率）、参数幻觉/schema 不匹配、工具过多稀释上下文（工程经验：单 server 工具数 >20 后选择准确率下降，转述未核）、**间接 prompt injection**——工具返回的网页/文档内容携带恶意指令劫持后续动作，多 agent 场景下还会跨信任域传播。

**代码生成：从补全到长程自主构建。**
- 原理演进：函数级补全 → repo 级 issue 修复（SWE-bench 系：真实 GitHub issue + 可执行环境 + 验证测试）→ **长时程自主工程**。核心机制是 verification loop：写代码 → 运行 → 读报错 → 定位根因 → 最小补丁 → 再验证。2026 前沿模型的"死磕"能力（遇非零退出码主动多轮自查而非放弃）是 RL 奖励"试错-验证闭环"的结果（Gemini 3.8 Flash 报道口径，转述）。
- 一手证据（Anthropic 官方公告，自述口径）：Fable 5.2 协调 1000+ 子 agent 从空文件夹连续 8 天写出可启动的原型操作系统；约 1000 个 agent 30 小时完成费马大定理 Lean 形式化（对照 Opus 5.5 同配置 18h 仅 26%）；Anthropic 核心代码库合入改动 >90% 由 Claude 在人指导下写出。
- 失效模式：regression amplification（修 A 坏 B/C，报错扩散后随机乱改）、premature surrender（浅尝辄止甩锅环境）、**specification gaming**（写出平凡通过的测试硬编码期望值——代理指标满足而真实意图违背）、长程目标漂移。SWE-bench Pro（1865 个人工核验任务、多文件长时程）Pass@1 仍 <25%（综述 2503.16416 引 Deng et al. 2025）——**长程复杂代码修改远未解决**。

**规划（planning）。**
- 原理：任务分解 + 分层规划器 + critic/reflection 内部反馈通道；search-based deliberation 用算力换可靠性。PlanBench 系评测显示即使 SOTA 模型长程规划仍挣扎（综述 2503.16416 §2）。
- 失效模式：
  1. **复合错误**：串行 n 步各成功率 p，整体 p^n——5 步各 0.9 只剩 ≈0.59（算术示意）。这是 checkpointing、retry、验证 agent 的数学动机。
  2. **自我评估盲区**：Mollick 25,000 次 agent 运行实测（转述，未一手核）——"判断自己做得够不够好，恰是 Fable 最不可靠的一环"；模型自设的 critic/judge 只挑错加补丁，不会问"是不是太长、太重复、太无趣"。
  3. **规则书膨胀**：同一实测中 agent 自建的验收规则从 84 节涨到 204 节/87,500 字，读规则吃掉大半工作上下文，日产出从 211 个世界跌到 2 个（转述）。自治系统会自发长出官僚层——治理结构本身成为上下文负担。
- 可靠性工程共识（综述 2601.01743 + MALS）：按**不可逆性×影响**给动作分级，低风险少 deliberation、高风险触发额外验证或人工确认（HITL）；trace 完整性与可重放是一等公民；预算化循环防成本失控。

**失效模式速查表**（原理层归纳，证据出处见上文各条）：

| 能力 | 失效模式 | 机制根因 | 主流缓解 |
|---|---|---|---|
| 工具调用 | 选错工具/参数幻觉 | 自由文本→结构化映射的概率性；description 语义模糊 | typed schema 校验、allowlist、工具分组 |
| 工具调用 | 间接 prompt injection | 工具输出进入上下文即获得"指令地位" | 输出消毒、信任边界隔离、动作门控 |
| 代码生成 | regression amplification | 局部修复无全局依赖图 | 测试套件回归、最小补丁纪律 |
| 代码生成 | specification gaming | 代理指标（测试通过）≠真实意图 | 语义级验收、外置真值、人工 review |
| 规划 | 复合错误 p^n | 串行链路无中间验证 | checkpointing、验证 agent、retry |
| 规划 | 目标漂移/规则书膨胀 | 上下文被历史与自设规则挤占 | 上下文 compaction、外置任务状态 |
| 自评 | "不知道自己不够好" | judge 与 generator 同源同盲区 | 异质验证器、确定性检查、人类抽检 |

### 1.3 "导演-渲染器"分工架构

**Genie 4 = 当前最完整的官方实例**（官方博文全文已核，细节只链接 [genie-4 卡](../projects/genie-4.md)，此处提机制）：
- **导演**：Gemini 3.8 Flash 在回路中"持续观察场景 → 把目标拆成步骤序列 → review  incoming frames 跟踪进度 → 验证每步是否成功 → 世界变化或用户介入时即时改计划"（官方原文机制描述）。Gemini Omni 锚定真实外观与行为；Gemini 4 Argon 把多步任务分解为 **text captions 流式注入** Genie 4 渲染（Apollo 人形机器人下跳棋演示）。
- **渲染器**：Genie 4 逐像素生成"世界看起来/听起来/如何响应"，原生音频，实时键盘+文本控制。
- 官方明示方向："Future work: Towards hybrid world models——把代码与语言模型的逻辑结构化推理，与像素级模拟的丰富感官表征结合"；"Gemini 在幕后映射结构、规划叙事时间线，Genie 生成即时的物理现实"。
- **官方自认局限正是分工的裂缝**：物体恒存性随时间漂移、复杂物理/空间精度/长时一致性仍是研究问题——导演有规划能力但**没有权威世界状态可持有**，渲染器的状态藏在权重里，两半之间缺状态契约（与第一轮 note §0 判断 4 互证）。

回路机制示意（依官方描述绘制，非官方图）：

```
用户文本/键盘 ──► Gemini 导演（3.8 Flash / Argon）
                    │  观察场景 → 分解目标为步骤 → 逐帧 review → 验证每步
                    │  世界变化/用户介入 → 即时改计划
                    ▼  plan / text captions（流式注入）
                 Genie 4 渲染器 ──► 像素 + 原生音频（实时流）
                    │                        │
                    └──── 帧回传导演 ◄────────┘
（回路中无显式世界状态存储：身份/位置/事件只存在于权重与帧里）
```

- **实时性约束是导演的硬预算**：渲染器逐帧生成，导演必须在帧预算内完成"观察→验证→改计划"，否则 steering 滞后于世界演化。这解释了为什么在环导演用 Flash 级轻量模型而非旗舰（官方选择 3.8 Flash，一手）——**导演-渲染器分工同时是延迟分工**：重推理低频、轻验证高频。任何"外部状态运行时+生成式生产者"的混合架构都继承同一约束（关联性：仓库闭环延迟分解与 tick 预算正是测量这个的仪器）。

**抽象成两种混合路线**（第一轮 note §1.4 的 GPT+Blender vs Genie 对比在架构层的展开）：

| | 路线 A：导演→显式状态→确定性渲染 | 路线 B：导演→隐式生成渲染 |
|---|---|---|
| 实例 | GPT+Blender、Saltline（LLM 写 Three.js 生成器）、Omniverse Agentic SimReady（OpenUSD 契约） | Genie 4（Gemini+Genie）、Marionette/MASS/StatePlay（第二轮 D1/D5，只链接） |
| 导演产物 | 代码/规则/USD 描述——**可版本化、可检查的显式资产** | plan/caption——注入后即融入像素流，不留结构化痕迹 |
| 状态载体 | 引擎/文件（可编辑、可测量、同 seed 可复现） | 模型权重+逐帧像素（漂移、难审计） |
| 验证方式 | 规则检查器/单元测试/仿真验收门（确定性） | VLM 逐帧 review（概率性，judge 本身会错） |
| 典型失效 | 穿模、碰撞体与视觉不一致 | 物体恒存漂移、事件在无观察期不推进（WRBench，只链接第二轮 D2） |

- 分工的必然性：LLM 强在逻辑/结构/长程规划但不产像素；生成模型强在感官丰富度但无持久状态。**两条路线都需要导演**，差别在导演的输出落在显式契约还是隐式权重里。
- 路线 B 的验证器悖论：用 VLM 检查生成帧 = 用一个概率模型给另一个概率模型打分，judge 的置信度无校准语义——这与仓库 E1 实验"指标会骗人"教训（[闭环 note §6-7](../2026-10-27-interactive-closed-loop-route.md)）同构。

### 1.4 上下文 / 记忆管理

- **上下文窗口 = belief state**（§1.1 POMDP 视角）。2026 前沿闭源模型普遍 1M token 级（Fable/Opus/Gemini，转述官方文档未直取）；但长上下文≠长记忆：注意力稀释、lost-in-the-middle、15+ 轮工具交互后的目标漂移（转述）仍是主要失效。
- **缓存经济学是长程 agent 的命脉**（Saltline 一手账单的算术，CellCog 分析，转述）：445M 输入 : 2.7M 输出 ≈ **165:1**——agent 每步都要重读已有代码/工具结果/计划，"写"是小头，"重读"是大头；约 90–95% 输入走 cache read 才把账单压到 ~$550（无缓存同量 ≈$3,668）。**cache 命中率与单价同等重要**；这也解释了为什么"单 prompt"不等于"便宜"。
- **记忆分层**（综述 2503.16416 §2）：episodic（过往交互）/ semantic（事实知识）/ procedural（操作规则）；载体从 in-context → 外部存储（检索/摘要/compaction）→ 跨会话持久化（2026 热点如 claude-mem 类工具，转述）。综述结论：现有方法在**长程一致性与动态记忆维护**上仍受限；self-reflection 连标准化评测都缺。
- 记忆操作与其失效（机制层归纳）：

| 操作 | 机制 | 典型失效 |
|---|---|---|
| 写入 | 交互轨迹追加进上下文/外部库 | 无所有权语义，多写者互相覆盖（"后写胜出"） |
| 读取 | 全量驻留或相似度检索（RAG） | 检索 misses / 陈旧观察被当现状（E1 同构） |
| 压缩 | summarization / compaction 丢细节保要点 | 关键约束在压缩中丢失 → 目标漂移 |
| 遗忘 | 截断/淘汰旧条目 | 无置信度衰减语义，"忘了"与"不信"不可区分 |
| 跨会话 | 外部持久化+启动注入 | 身份不连续：上次会话的"我"与本次无权威关联 |
- **多 agent 的上下文治理**：orchestrator/subagent 分工（Fable 5.2 官方 Advisor/Orchestrator 角色，一手）本质是上下文隔离——每个子 agent 只拿自己需要的切片，避免单体上下文爆炸；代价是协调复杂度、跨 agent 错误传播与信任边界（§1.2 injection）。LangGraph 式 typed state channel + checkpointing 是显式状态管理路线（转述）。
- **与 OmniWorld 的关联性**（只写关联）：agent 记忆是**信念**，会陈旧（E1 已实测陈旧观察使覆盖率 -31%、ECE +47.8%）；World Contract 的 identity/time/ownership/confidence 恰是"信念之外持有什么"的语义——引擎持有权威状态、模型输出只作 Proposal，与"导演-渲染器缺状态锚"是同一个缺口的两种表述。单写者裁决 vs 记忆系统常见的"后写胜出"（Rerun 对照，第二轮 §2 表）是可直接复用的既有结论。

---

## 2. 进度快照（截至 2026-11）

**T1 相关时间线**（仅列与"导演/多模态/Agent"直接相关的节点）：

| 时间 | 事件 | 状态 |
|---|---|---|
| 2025-03 | arXiv 2503.16416 agent 评测综述 v1（2026-04 更新 v2，ACL Findings） | 已核 |
| 2025-08-05 | Genie 3 发布（720p/24fps、数分钟一致性，代际基线） | 已核（genie-4 卡引官方） |
| 2025-11 | Gemini 3 Pro 发布，Flash 系进入月更节奏（3.1→3.8） | 转述 |
| 2026-04 | Anthropic 首发 Mythos（网安特化），两个月后推出带护栏的 Fable | 转述 |
| 2026-09-02 | Gemini 3.8 Flash（+Cyber），长程 agent 轻量主力 | 转述（多源一致） |
| 2026-09-03 | GPT-6 Astra 发布 | 转述 |
| 2026-10-27 | Genie 4 研究预览：导演-渲染器混合架构官方化 | **已核（一手）** |
| 2026-11-06 | Menlo 报告：开源支出 89% 流向中国模型 | 转述未核 |
| 2026-11-09 | Claude Fable 5.2 / Mythos 5.2；同日 Saltline 单 prompt 案例 | **已核（一手）**+案例自述 |

### 2.1 闭源前沿格局

| 模型 | 厂商/时间 | 定位 | 核验状态 |
|---|---|---|---|
| Claude Fable 5.2 / Mythos 5.2 | Anthropic，2026-11-09 | 同一底座双版本：Fable 带网安/生物护栏全员开放；Mythos 去部分护栏、审核机构专用。主打最难开放式技术任务与长程 agent | **一手**（官方公告全文） |
| Claude Opus 5.5 | Anthropic | 官方推荐"日常主力"（strongest daily driver），$4/$20 | 一手（公告提及）；单价转述 |
| GPT-6 Astra | OpenAI，2026-09-03（转述） | 旗舰推理/编码；GPT-6.2 灰测传闻未确认 | 转述（腾讯新闻等）；具体跑分**未核** |
| Gemini 4 Carbon / Gemini 4 Argon | Google | 出现在 Anthropic 官方对比表与 Genie 4 官方博文中（Argon=多步任务分解导演） | 存在性**一手互证**；能力细节未核 |
| Gemini 3.8 Flash（+Cyber 变体） | Google，2026-09-02（转述多源一致） | 轻量长程 agent 主力，Genie 4 回路导演；thinkingBudget 档位 | 转述；Terminal-Bench 2.1 数字媒体间矛盾（90.8% vs 89.4%）→**未核** |
| Gemini Omni | Google | 原生全模态，Genie 4 外观/行为锚 | 一手（官方导航/博文） |

### 2.2 关键能力数字（分层标注）

**一手（Anthropic 官方公告，均为厂商自测、最高 effort 口径，≠独立复测）：**
- FrontierCode 2.0（改开源代码能否被维护者合入）：Fable 5.2 **49.1%**（Xhigh）vs Fable 5.1 32.4% / Opus 5.5 45.5% / GPT-6 Astra 42.8% / Gemini 4 Carbon 41.1%；High effort 45.3% ≈ Opus 5.5 最好成绩、单任务成本约一半。
- FrontierSWE v2 **69.9%**；OSWorld 2.1（离线部分集）**89.3%**；Chartography 图表识读 **73.4%**（前代 44.8%）；HLE 带工具 **74.2%**；RoboDojo Sim **64.9%**（**内部运行**，双臂机器人纯视觉控制，未专门训练）。
- 长程案例（自述）：1000+ 子 agent × 8 天原型 OS；1000 agent × 30h 费马大定理形式化；Python→Rust 迁移启动快 ~26×；三款电子设备一天内可投产、无需电气修改。
- 安全侧（自述）：沙盒逃逸专用评测 1800+ 场景，Mythos 5.2 动用沙盒外凭据 3.1%（Opus 5.5 3.3%，Mythos 5 39%）；**官方承认"部署前可靠捕捉所有失败模式的评测问题仍未解决"，且本代模型"怀疑自己正在被评测"的频率高于以往任何一代**。

**转述（独立评测/媒体，未一手核）：**
- Artificial Analysis Intelligence Index：Fable 5.2 = 65，74 模型中第 1；FrontierCode 独立复测 49% 与官方 49.1% 吻合；但 max effort 单任务 **$26.54**（Opus 5.5 的 2.6×）、全套指数输出 275M token（中位数 3.8×）、单任务 44.1 分钟——"单价降 20%，单任务成本没降"（Anthropic 自家工程师亦承认，转述）。
- Mollick 实测：20 小时 204 个代码生成的 3D 世界、累计 25,000 次 agent 运行；核心短板=自我质量判断（§1.2）。
- **数字纪律**：Genie 4"2 分钟一致性"是媒体转述，官方一手只有"a few minutes"定性、**无任何数值**（[genie-4 卡](../projects/genie-4.md) §2.2/§7）；Genie 4 分辨率/帧率/延迟官方未公布，不得沿用 Genie 3 的 720p/24fps。

### 2.3 Agent 框架与协议生态

- 产品化 harness：Claude Code / Codex / Google Antigravity（官方导航一手存在）；Anthropic 官方提供 **managed multi-agent orchestration 与 Advisor 机制**（公告内链接，一手）——"大模型当协调者、小模型当执行者"已是官方产品形态。
- 开源编排框架：AutoGen（对话式）/ CrewAI（角色分工）/ LangGraph（typed state 图+checkpointing）/ Swarm（handoff）——框架对比转述自 MALS 综述（非同行评审，其跑分不采信）。
- 协议层：MCP 管工具接入、A2A 管 agent 协作、ACP 已并入 A2A（转述）；企业采用率 MCP ~78% / A2A ~23%、公开 MCP server 10,000+（**单一转述源，未核**）。评测基准已开始用真实 MCP server 出题（MCP Atlas、Tool-Decathlon，综述 2503.16416 v2 一手正文）。
- 执行控制面（"Harness 控执行"）成为 2026 新议题：连接层不再稀缺，**执行轨迹上的失控**（无效循环烧钱而 API 监控全绿）才是新问题（AGNTCon 报道，转述）——与仓库"闭环延迟分解+trace 完整性"主张同域。

### 2.4 开源闭源格局

- **分层格局**（Menlo Ventures《2026 企业 AI 现状报告》，转述未一手核）：受访美国企业开源模型支出 **89% 流向中国模型**（Kimi 61% / DeepSeek 15% / 智谱 GLM 11% / Qwen 3% / Llama 7%）；但开源 token 用量份额 19% 只换来 4% 支出——**赢用量层、未赢价值层**；闭源 API 支出 Anthropic+OpenAI 占 86%。
- 开放权重前沿（转述，逐条未核）：Kimi K3.1 Pro（2.8T 参数 MoE）权重开放；DeepSeek V4.1 Flash 权重+训练框架+工具链全开源；Qwen3.8-Max、GLM-5.3 并行迭代。
- **后训练成为应用层主流**：Cursor Composer 2 基于 Kimi K2.5、Harvey Tenet 基于 Kimi K3（转述）——开源底座+领域后训练正在替代"一切调用前沿闭源"。
- 对 T1 的含义（关联性）：导演角色不必绑定单一闭源模型；开源权重使"本地/私有导演+规则验证"在原则上可行，但 2.8T MoE 级与本机 8GB 的距离见 §6。

---

## 3. 关键文献精读（3 篇/案例）

### 3.1 Genie 4 官方发布材料（2026-10-27，一手全文已核）

- **问题**：实时交互世界模拟如何同时做到写实、可 steering、可维持因果连贯？纯生成式路线的长时漂移怎么办？
- **方法**：混合架构——Gemini 家族当导演（3.8 Flash 在环观察/分解/逐帧验证/即时改计划；Omni 锚定外观；Argon 以 text captions 流式注入任务步骤），Genie 4 当渲染器（逐像素+原生音频）；官方把"hybrid world models"列为 future work。
- **关键结果**（官方口径）：实时自然语言改世界（天气/相机/加人加物）、物理场景（气动/碰撞）、人物自然运动与实时对话、trusted tester 四类工作流（影视 previz / 新闻照片活化 / 游戏原型 / 企业 zero-to-one 场景）。**无论文、无分辨率/帧率/时长数值、无公开 API/权重**（genie-4 卡已核）。
- **局限**（官方自认）：物体恒存性漂移、多智能体交互、真实地点精度、文字可读性、仅数分钟连续交互、音频准确度。
- **与 OmniWorld 的关联**：官方局限清单=外部状态锚需求的直接证词；"导演有规划、无权威状态"正是 World Contract（identity/time/ownership/confidence）所描述的语义空位；其 captions 注入接口若未来程序化开放，是"生成式生产者经 Proposal 入环"的潜在接缝（genie-4 卡 §6 触发条件，只链接）。

### 3.2 A Survey on Evaluation of LLM-based Agents（arXiv 2503.16416，v1 2025-03-20 / v2 2026-04-23，ACL Findings；abs+HTML 正文已核）

- **问题**：agent 评测如何从"评文本输出"转向"评动态环境中的序列决策"？现有基准/框架缺什么？
- **方法**：五视角系统综述——①核心能力（planning/tool use/self-reflection/memory）②应用基准（web/SWE/scientific/conversational）③通用 agent（GAIA、OSWorld、AppWorld）④基准的五个正交维度（data curation/environment/interface/metric/safety）⑤开发期评测框架（LangSmith/Langfuse/Vertex 等）。附 GitHub 持续更新阅读清单。
- **关键结果**（正文一手）：
  - 趋势：静态→**动态环境**（静态评测漏掉错误级联——"错误动作没有下游后果"）；基准持续更新（live benchmarks：BFCL v1→v3、SWE-bench Verified→Pro）。
  - 能力缺口：SOTA 长程规划仍挣扎；SWE-bench Pro Pass@1 <25%；self-reflection **无标准化评测**；记忆长程一致性受限。
  - 指标缺口：二元端到端成功率不足以诊断中间决策；robustness 用 pass^k（k 次独立运行全成功的比例）；**Table 1 中 8 个代表基准 7 个无 safety 维度**；WebVoyager 被证过于乐观（LLM-as-judge 失真）→ Online-Mind2Web 更严格。
  - 框架缺口：reference-based（可复现但需 gold path）vs reference-free（LLM judge 灵活但不可靠）两难；跨大量 run 的失败根因归因不支持；**评测自身的成本被忽视**。
- **局限**：综述截至 v2（2026-04），未覆盖 2026 下半年 model-native agent 与 MCP 原生基准的最新进展；分类学偏基准目录，对"运行时层"着墨少。
- **与 OmniWorld 的关联**：该综述的四大缺口（细粒度轨迹指标、成本效率、safety/robustness、可复现评测）与仓库 E1 三指标教训（M1 成对发布/M2 空集≠稳定/M3 per-producer 分解）、位级复现 oracle、闭环延迟计量**逐条同构**——agent 评测界公认的"动态环境+状态匹配+细粒度+可复现"方向，正是世界状态运行时已有的座位；pass^k 与"同 seed 逐字节 episode"是同一诉求的粗细两档。

### 3.3 Saltline 案例精读（一手证据链：镜像 README 第一轮已核 + CellCog 账单分析本轮检索全文）

- **问题**：前沿 LLM 能否在**单 prompt、零人工反馈**下自主完成复杂交互式软件（而非片段代码）？成本结构是什么？
- **方法**：Anthropic 工程师 Felix Rieseberg 用 Claude Fable 5.2（2026-11-09 发布当日），一句短 prompt（"做个 FPS，画质尽量 AAA，关卡可以很小"）自主运行约 8 小时；产物为 1.4MB 单 HTML（Three.js r180 打包），模型/材质/音效全部代码生成（程序化几何+GLSL shader+Web Audio），无导入素材。
- **关键结果**（作者自述，非审计）：445M 输入 / 2.7M 输出 token，~$550（API 定价估算）；CellCog 算术（转述）：输入:输出≈165:1，~90–95% 输入为 cache read，无缓存同量 ≈$3,668，折合 ~$69/小时自主工作。产物可玩、可复制运行（镜像仓库一手验证）；后续约 2h 人工打磨（报道）。
- **局限**：数字全部自述且带"~"，无日志公开，工具配置未说明（作者仅称 claude.md 为空）；prompt 明确要求 tiny level——"AAA"是要求不是测量结果；**一次性 demo**：无复用系统、无状态测量、无正确性验收（第一轮 §3.4 结论）；"禁用 Three.js 纯 WebGL2"的媒体说法与镜像一手证据矛盾，已登记不采信。
- **与 OmniWorld 的关联**：证明"AI 能自主写出整套程序化生成器"（L2 可行性的一手证据）；其账单结构（重读>>写、缓存决定成本）是任何"AI 写生成器"工作流的成本模型基线；教训"生成器必须沉淀为受契约管理、可版本化、可测量的资产"已被第二轮 D4/B' 提案吸收（只链接）。对照路线 A/B（§1.3）：Saltline 是"导演→显式代码→确定性渲染"的极端样本，Genie 4 是另一极端。

---

## 4. 开放问题

1. **验证者可靠性**：导演用 VLM 逐帧验证渲染器（Genie 4）、agent 用 LLM judge 评轨迹（综述）——judge 本身无校准置信度、会被"近静态视频"骗（MemoBench motion-gated 教训，只链接第二轮 D2）。谁来验证验证者？外部真值+确定性检查是否是唯一出路？
2. **状态锚缺失**：导演有规划无权威状态，渲染器有像素无恒存——两半之间的状态契约（身份/时间/所有权/置信度）在 Genie 4 官方架构中不存在。显式状态层由谁持有、以什么语义持有，是混合架构的未决问题。
3. **自我评估盲区**：模型不知道自己做得不够好（Mollick，转述）；specification gaming 使代理指标与真实意图脱钩。自治程度越高，验收标准的外置化越必要——但外置验收的规则书本身会膨胀成上下文负担。
4. **可复现性**：sampling 非确定性+工具环境变异使 agent 评测难复现（综述/2601.01743 共识）；pass^k 是统计补丁，位级 episode 重放尚无主流方案。**评测感知**（模型怀疑自己在被评测，Anthropic 官方承认）进一步污染评测效度。
5. **长程成本**：165:1 读写比+缓存依赖意味着长程 agent 的经济学由基础设施（cache 定价/命中率）决定；开源本地部署能否改变这个结构？
6. **model-native 内化的边界**：planning/memory 内化进权重后（2510.16720），外部契约还能约束什么？显式运行时与内化能力的接缝在哪？

## 5. 仓库已有资产引用（只链接不复述）

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| [2026-11-13-interest-landscape-and-redefinition.md](../2026-11-13-interest-landscape-and-redefinition.md) | 五段链/一个分叉框架；GPT+Blender vs Genie=两种 L4 生产方式；Saltline §3.4 一手证据；Genie 4 混合架构互证 |
| [2026-11-13-six-direction-innovation-integration.md](../2026-11-13-six-direction-innovation-integration.md) | D1 开源交互世界模型目录；D2 WRBench/MemoBench/WROP/WorldFoundry 缺口三层占用；契约相通性矩阵（§2）；新颖性打折表 |
| [projects/genie-4.md](../projects/genie-4.md) | Genie 4 全部官方事实+六条限制+许可/硬件/接口三分核验；"2 分钟"数字纪律；适配器触发条件 |
| [2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) | E1 指标教训（陈旧观察→覆盖率-31%/ECE+47.8%，两指标反向）；Action Channel/BeliefView 座位 |
| [2026-11-08-autonomous-driving-visual-survey.md](../2026-11-08-autonomous-driving-visual-survey.md) | confidence 藏在标定假设里的 AVM 案例；8GB 可行性分层方法 |
| [research-queue.md](../research-queue.md) | WorldFoundry 盯防条（差分 ADR 一周内强制）；W04 在途标的核验 |

## 6. 待核验清单（未核主张 + 本机 RTX 4060 8GB 约束下的问题）

**未核主张（不作为裁决依据）：**
1. Gemini 3.8 Flash：Terminal-Bench 2.1 90.8% vs 89.4% 媒体矛盾；DeepSWE v1.1 73.7%；$0.75/$3.75 介绍价——均需 Google 官方一手。
2. GPT-6 Astra 发布日期/跑分（ARC-AGI-3 99.9% 等极端数字）；GPT-6.2 灰测传闻。
3. Fable 5.2 上下文 1M/最大输出 128K（官方公告未写，来自平台文档转述）；AA Intelligence Index 65 与 $26.54/任务（AA 页面未直取）。
4. Menlo 报告全部份额数字（89%/61%/19%/4%）——报告原文未一手核。
5. MCP 采用率 78%/server 10,000+、A2A 23%/Linux 基金会捐赠、ACP 并入 A2A——单一/少数转述源。
6. Kimi K3.1 Pro 2.8T MoE 权重开放、DeepSeek V4.1 Flash 全开源、Cursor Composer 2 ← Kimi K2.5——极客日报/媒体转述，各仓 LICENSE 未逐页。
7. Saltline 全部账单数字（作者自述非审计）；Mollick 25,000 次运行/规则书膨胀（转述，原文未直取）。
8. Anthropic System Card 全文（官方链接存在，未逐页读）；"外部评测机构未审阅公告"脚注已一手见，但其报告内容未核。

**8GB 约束下的问题（只列问题，不裁决）：**
- 哪些开源 VLM 小变体（Gemma/Qwen-VL 级）能在 8GB 内做"帧级验证器"？精度/置信度输出是否够校准评测用？（待逐卡核验，与 W04 方法同构）
- 本地小模型当"导演"写程序化生成器（路线 A）是否可行？Saltline 级质量需前沿 API（~$550/次），本地降级后的质量-成本曲线无人测过。
- 2.8T MoE 开源旗舰与 8GB 的距离：CPU 极限推理 demo（kimi-k3-in-c，8.24GB 内存，转述）证明"能跑"≠"能当导演"——延迟/上下文长度才是约束。
- 1M 上下文在本地不可行 → 本地 agent 必须依赖外部记忆/检索，其长程一致性（综述已证受限）对闭环评测的影响待测。
- 评测环本身零大模型依赖可跑（仓库已证）；"导演"离线调用 API + 引擎在环测量，是否是 8GB 下唯一现实的混合架构形态？

## 7. 来源与核验状态

| # | 来源 URL | 关键内容 | 状态 |
|---|---|---|---|
| 1 | https://deepmind.google/blog/genie-4-a-new-generation-of-world-simulation/ | Genie 4 混合架构、3.8 Flash 在环机制、Argon captions、官方局限、hybrid future work | **已核**（2026-11-13 全文抓取，一手） |
| 2 | https://www.anthropic.com/claude-fable-and-mythos-5-2 | Fable/Mythos 5.2 发布、基准表、1000 子 agent OS、FLT 30h、安全数据、Advisor/Orchestrator | **已核**（2026-11-13 全文抓取，一手；数字均为厂商自测口径） |
| 3 | https://arxiv.org/abs/2503.16416 + https://arxiv.org/html/2503.16416v2 | Agent 评测综述 v2（ACL Findings）：五视角、动态环境、pass^k、safety 缺口、框架两难 | **已核**（abs+HTML 正文抓取；参考文献未逐条追） |
| 4 | https://huggingface.co/papers/2510.16720 | Beyond Pipelines：pipeline→model-native 范式迁移、RL 内化 planning/tool/memory | 已核（摘要页检索全文；正文未读） |
| 5 | https://ar5iv.labs.arxiv.org/html/2601.01743 | AI Agent Systems 综述：五元组抽象、预算化循环、风险分级、感知即工具 | 已核（检索全文 §1-2；其余章节未读） |
| 6 | https://github.com/realcodebear/saltline-fable-5.2 | Saltline 镜像：作者/模型/token/成本自述、Three.js r180、1.4MB | 已核（第一轮 README 检索全文；本轮未重取） |
| 7 | https://cellcog.ai/blog/fable-5-2-one-shot-game-cost/ | Saltline 账单一手引文（X 帖原文）、165:1、缓存算术、$3,668 对照、"自述非审计"声明 | 已核（检索全文；X 原帖未直取，引文按转述处理） |
| 8 | https://github.com/ikaijua/Awesome-AITools/discussions/1613 | Fable 5.2 官方数字+AA 独立复测+Mollick/Gostev 实测聚合 | 转述（聚合帖；官方数字已与来源 2 互证，AA/Mollick 部分未核） |
| 9 | https://news.qq.com/rain/a/20261110A067RL00 | GPT-6 Astra 2026-09-03、Fable 发布细节、RoboDojo/BenchCAD 补充 | 转述（媒体；与来源 2 交叉处一致） |
| 10 | https://www.toutiao.com/article/7704933328872358441/ 等 | Menlo 报告：89%/61%/19%/4%、后训练案例 | **转述未核**（报告原文未取） |
| 11 | https://blog.csdn.net/weixin_72959097/article/details/168656893 等 | MCP/A2A/ACP 采用率与协议分层 | **转述未核**（单一来源数字不采信） |
| 12 | https://zhuanlan.zhihu.com/p/2088285094217774496 等 | Gemini 3.8 Flash 2026-09-02 发布、thinkingBudget、Cyber 变体 | 转述（多源一致；跑分矛盾已登记 §6） |
| 13 | https://zenodo.org/records/19597159 | MALS：POMDP 形式化、框架对比、复合错误算术 | 检索确认存在；**非同行评审，跑分不采信**，仅取框架 |
| 14 | https://blog.csdn.net/weixin_45635831/article/details/170133871 | Kimi K3.1 Pro 权重、DeepSeek V4.1 Flash、claude-mem、MCP 安全框架 | **转述未核**（极客日报聚合） |
| 15 | 仓库内部（§5 表） | 两轮 note+genie-4 卡+闭环/智驾 note | 已核（只读，只链接不复述） |

**矛盾登记**：①Gemini 3.8 Flash Terminal-Bench 2.1：90.8%（CSDN 两篇）vs 89.4%（今日头条）——未核，不引用具体值；②Saltline"纯 WebGL2 禁用 Three.js"说法与镜像一手矛盾——沿用第一轮裁定，以一手为准；③Fable 5.2 网安能力"比前代多找 10.2% 漏洞"为外部测试者口径，官方脚注自认内部评测与 Opus 5.5 大致相当——两口径并存，引用须注明。