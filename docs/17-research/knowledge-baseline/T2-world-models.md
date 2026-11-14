# T2 世界模型（生成式/实时交互）— 知识基线（2026-11-13）

> Trust label: [Research → 知识基线]——为 owner 方向评审（grill → ADR-0016）提供 T2 主题的原理与进度地基；**只调研不裁决**：本文不修改任何既有文档，不做方向选择，与 OmniWorld 的关联只写"关联性"不写"应该做 X"。数字纪律：一手核验标"已核"，媒体/目录转述标"转述"，无法核验标"未核"；Genie 4"2 分钟"是媒体转述，官方一手无数值。检索日：2026-11-13（仓库资产核验日沿用各卡标注）。

---

## 1. 原理与技术路线分类（重点）

### 1.1 世界模型定义之争：预测动力系统 vs 视频生成器

"世界模型"一词在 2026 年的文献里至少指三种不同的东西，混用是大量争论的根源：

1. **控制论/RL 传统（预测动力系统）**：Craik（1943）的"内部小模型"→ 控制论状态空间 → Sutton Dyna → Ha & Schmidhuber / Dreamer 系——学一个紧凑 latent 动力学模型，让 agent 在"想象"里规划与训练策略。状态服务于 policy，**不必可解释、不必渲染成像素**。（谱系梳理见 Solaris 论文 §2.1，arXiv 2602.22208，已核；Dreamer 4 见 09-21 决策报告 §3 B 类，转述）
2. **视频生成器自称世界模拟器**：Sora 式叙事——"生成逼真视频 = 模拟世界"。判据是像素质量、运动合理性、相机可控性。WRBench 对此的批评一针见血：这类基准"reward surface properties … while never asking whether a generated world keeps evolving once it is unobserved"（arXiv 2606.20545 摘要，已核）。
3. **结构化状态 + 生成的中间路线**：显式预测世界状态（骨骼/血量/计时器/几何），再渲染外观——Marionette、StatePlay、MASS 等（§1.3）。

**2026 年正在收敛的判据**（多方独立给出，非本文裁决）：
- WRBench：世界模型必须维持"an internal world state that keeps evolving over time, **decoupled from observation**"——月亮没人看时也沿轨道运行（已核，摘要原文）。
- WorldFoundry（arXiv 2611.10771）：定义生成式世界模型 = 维持持久状态、响应干预、闭环更新的预测动力系统（转述自 six-direction note §1 D2，该 note 标已核摘要级）。
- Marionette 论文专设 "Which 'world model' we mean" 一节区分上述三义，并指出交互持久世界这个 regime 要求状态"both interpretable and render-ready"（已核，全文）。
- Skywork 刘扬（智源大会演讲）：理解当下状态（含物体级物理属性，纯视觉覆盖不了）→ 预测下一状态 → 渲染呈现；并主张"状态预测与动作生成应联合训练"（转述，CSDN 会议报道）。

**根本分叉：隐式权重 vs 显式状态**。视频预测式路线里，"世界状态"活在两处不可查询的地方：生成器的激活/KV cache（视觉连续性）与外部语言模型的计划（Genie 4 的 Gemini narrative timeline，见 genie-4 深报 §2.8）。显式状态路线里，状态是一等公民：可查询、可施加规则、可用物理单位（米）评分。Marionette 的核心论点："Work that must be exact is executed by a tool, not generated"——把几何/遮挡/度量运动交给零参数确定性渲染器，"像语言模型 agent 用计算器一样"（已核）。这个分叉不是"哪条更好"，而是**可测量性换泛化性**的取舍：隐式路线泛化到开放世界但不可对账；显式路线可对账但状态 schema 要逐域设计。

### 1.2 架构机制：扩散 / 自回归 / 混合

**（a）双向扩散（bidirectional DiT / flow matching）**。整段视频联合去噪，全时间注意力；质量上限最高，但天然离线——生成期间不能插入新动作。代表：Wan2.x、Matrix-Game 3.5 的 base（双向 5B）、Cosmos generator 塔（rectified flow：`x_t = t·ε + (1-t)·x_0`，速度目标 `v* = ε - x_0`，cosmos-3 深报 §2.6，已核）。

**（b）离散 token 自回归**。视频 VQ 成离散 token，decoder-only transformer 逐帧预测。论文层原型 = Genie 1（arXiv 2402.15391，genie-4 深报 §2.2 已核）：LAM 从无标注视频反推 8 个离散潜在动作（推理时整个 LAM 扔掉、换用户输入）；ST-ViViT tokenizer 带因果时序记忆；MaskGIT dynamics 每帧 25 步采样、temperature 2 随机采样。代价论文自认：16 帧记忆、~1 FPS、幻觉未来。优点：KV cache 天然流式、控制即 token 注入。

**（c）混合：chunk 因果扩散（AR + Diffusion）——2026 实时交互的主流配方**。把视频按 chunk（潜帧块）切分：chunk 内联合去噪（保质量）、chunk 间严格因果（可流式、可插动作）。Vidu S1 官方自述路线即"自回归扩散模型（AR + Diffusion）"：基于已生成历史 + 当前语音指令持续预测后续（shengshu.com 官方新闻，已核）。Cosmos 3 的 MoT 双塔是另一种混合：AR 塔（causal next-token，理解）与 DM 塔（flow 去噪，生成）参数解耦、共享注意力，信息单向流（"AR tokens are never updated based on DM tokens"，技术报告 §2.3.2，cosmos-3 深报已核）。

**（d）因果蒸馏流水线——三条独立实现已收敛到同一四段式**（这是 2026 年最重要的工程共识）：

| 阶段 | ForgeWM（arXiv 2608.14022，已核） | Matrix-Game 3.5（arXiv 2608.29910，已核） | Cosmos Sim（cosmos-3 深报 [SIM]，已核） |
|---|---|---|---|
| 0 域适配 | 双向 SFT（冻结后当 DMD teacher） | 双向 base 适配目标域 | Bidirectional adaptation（冻结 teacher） |
| 1 因果化 | Teacher-forced causal AR | Perceptual Flow Matching（感知特征空间因果初始化） | Teacher Forcing（chunk 因果注意力） |
| 2 少步化 | Consistency distillation | （并入阶段 3 课程） | TF-dCM 一致性蒸馏 |
| 3 自 rollout | On-policy DMD（双向 teacher 打分） | Curriculum Self-Rollout DMD（逐步蒸馏 CFG/相机/记忆条件） | SF-DMD（Self Forcing + DMD） |

阶段 3 治的是 **exposure bias**：训练时喂真值历史、推理时喂自生成历史的分布失配——让学生在自己的 rollout 上训练。配套流式工程：KV cache + 滑动窗口 + sink（ForgeWM Stage 3 `local_attn_size=6`；Cosmos Sim 的 RefreshKV + 有界历史）、少步采样（1/2/3/4 步）、量化（INT8/FP8）、VAE 剪枝（MG3.5 MG-LightVAE）、CUDA graph。

**取舍小结**：双向扩散质量高但不可交互；离散 AR 流式天然但质量/记忆受限；chunk 因果扩散 + 蒸馏是当前"质量×实时"的折中，代价是**每个域都要重走蒸馏课程**（Cosmos Sim 三个检查点各自独立训练；ForgeWM 的 CrossFPS 是独立血统不是多域模型），且蒸馏后检查点不宜再 SFT（cosmos-framework 官方排序："complete adaptation before distillation"）。

### 1.3 视频预测式 vs 状态空间式

- **视频预测式**（Genie、Matrix-Game、ForgeWM、Vidu S1）：状态 = 历史生成轨迹本身（Genie 3 官方："previously generated trajectory"就是状态载体，genie-4 深报 §2.3）。一致性是涌现能力不是机制保证。
- **状态空间式**：Marionette 的因式分解 `p(video) = ∫ p(obs|s)·p(s_{t+1:}|s_{≤t},control) ds`——显式 276D 状态（双实体骨骼 + 度量根轨迹 + 6D 旋转 + 武器子状态），动力学与外观条件独立。StatePlay 用 MoT 风格双分支联合预测画面 + 显式游戏状态（血量/技能槽/计时器），状态分支引导帧生成（arXiv 2607.26754 摘要，已核）。MASS（权威共享状态 → 任意相机）见 six-direction note §3（转述）。
- **关键实验证据**（Marionette，已核）：长时程失败发生在状态层且**可在状态层修复**——自由 rollout 两角色漂到 21.2 m（录制数据 ~5 m）、1/3 帧穿地；对显式状态加两条规则（地形碰撞体 + 间距上限）穿地率降 66%、间距守住，**观察模型一个参数不动**。外观经显式状态路由无可测损失（FVD 831 vs 录制状态驱动 799）。这是"显式状态可被规则治理"的最直接一手证据。
- **边界**：状态空间式的状态 schema 是逐域手工设计的（Marionette 单怪物类型、固定武器；换域要重训两阶段动力学）；它没有解决"开放世界状态从哪来"，只证明了"有了状态之后一切可对账"。

### 1.4 动作条件化与闭环 rollout

动作注入的四种机制（均有一手来源）：
1. **潜在动作**（Genie 1）：无标注视频学 8 离散码，additive embedding 注入（论文点名此设计优于拼接）；推理时用户按键映射到动作码。
2. **键鼠双通道**（Matrix-Game 2 / ForgeWM）：离散键盘 cross-attention + 连续鼠标 channel concat；难点是**动作与时间压缩潜块的对齐**（VAE 4× 时间压缩下，逐帧鼠标 delta 要按块分组）——ForgeWM 论文把这个问题当核心贡献（已核）。
3. **相机几何条件化**（MG3.5 Warped PRoPE / Cosmos 3D mRoPE）：把相机投影矩阵折进 RoPE，参数免费；MG3.5 的动机是"注入控制参数会破坏原始视频分布"（刘扬演讲，转述），PRoPE 不加参数所以不破坏。Cosmos 另有 FPS modulation 把多模态 token 对齐到物理秒轴（深报已核）。
4. **统一动作 token**（Cosmos 3）：异构本体（相机 9D/单臂 10D/双臂 20D/人形 29D/ego 57D）统一为相对位姿伪动作 `ΔT = T_{t-1}^{-1}T_t` + 抓握状态，域专属投影进出、主干共享；同一权重翻条件方向即 forward dynamics / inverse dynamics / policy 三模式（深报 §2.2.3，已核）。
5. **token 覆写**（Marionette）：动作是显式 token，控制 = 覆写该 token——"与玩家按键是同一操作"。因果权威可测：换错动作流 RA-MPJPE 0.272→0.357 m（+31%，48 段；已核）。

**闭环 rollout 的实时刻度**：Genie 3 官方定义实时 = "新输入一到，轨迹感知计算每秒跑多次"（控制回路频率，不是单纯生成速度）。Cosmos Sim 给出两个可移植度量：`RTF`（生成时长/物理时长 ≤1 为实时）与 `control-to-observation latency`（控制输入 → 首帧受影响输出），并指出接收整段控制流比逐 chunk 收延迟更低（等输入也计入响应延迟）。**官方自认的闭环缺口**：Cosmos Sim 明说其证据是"条件生成 + 流式延迟"，"does not exercise feedback from policy-driven interaction"，policy-in-the-loop 效用留待独立闭环验证（[SIM §1/§2.3]，深报已核）——即**全行业目前没有一手验证的"策略驱动闭环"世界模型效用证据**。

### 1.5 记忆机制分类（持久状态的四条路）

| 类别 | 机制 | 代表（来源） | 固有上限 |
|---|---|---|---|
| **上下文内** | KV cache + 滑动窗口 + sink；历史就是注意力前缀 | Genie 1（16 帧）→ Genie 3（~1 分钟视觉记忆，官方）；ForgeWM 6 帧窗 | 窗口外即遗忘；时长上限 = 记忆机制的直接后果 |
| **检索式** | 历史帧/patch 存外部库，按相关性或几何取回拼进条件 | WorldMem 帧检索；MG3 camera-aware 检索 → MG3.5 **Patch Memory**：历史 latent patch 用 depth+内参+位姿反投影成 3D 点云 → 目标视锥查询 → z-buffer 选可见 patch → 拼成 query-aligned memory canvas（已核） | 检索到的是"看过的样子"，不是"没看时发生的事"（见 §1.6） |
| **参数内** | 世界知识在权重里（预训练先验） | 所有基座模型 | 无 per-episode 持久性；不能记录本局新事件 |
| **外部状态锚** | 引擎/缓存持有结构化状态，模型只读 | 几何缓存（GEN3C 点云缓存）；Marionette 显式状态；MG3.5 动静解耦（静态进 Patch Memory、动态主体走 reference tokens + 运动感知过滤，防 ghosting/身份漂移，已核） | 状态 schema 覆盖不到的内容仍靠生成 |

MG3.5 的动静解耦值得单独记：用 YOLO 分割/跟踪 + 跨帧投影重叠分数判断"运动状态"而非类别——静止前景可进静态记忆，运动物体不进（否则污染空间记忆产生重影）。这是"记忆写入需要 ownership 判定"的一个模型内实例（关联性，见 §3.3）。

### 1.6 物体恒存性与状态漂移的成因（WRBench 证据链）

WRBench（9,600 视频 / 23 模型 / 四种控制范式，已核全文）给出的机制诊断：

1. **"跟拍镜头"失败**：镜头转开再转回，目标停在**被抛弃时的状态**而非事件推进后的状态——事件在无观察期间不推进。此失败跨范式、跨家族、跨规模复现。
2. **三能力解耦**：可见质量（visible）、可重观察性（re-observation support）、重观察状态正确性（re-observed state）互不预测。外部记录（source-video/geometry-cache 范式）把重观察支持率从 prompt-only 的 ~6% 拉到 73%（Gen3C），**但重观察状态分停在 ~0.62 平台**；几何缓存最擅长重投影静态场景，恰恰在"原地状态变化"上缺口最大（0.152）——"a stored record of where surfaces were cannot reconstruct a change it never observed"。
3. **最难的单一情形**：**原地状态变化**（in-place change，物体没挪位但状态变了）——没有新坐标可供跟踪，变化物体在原地漂移/涂抹/消失，背景却保持清晰。
4. **规模不解决**：Wan 系 12 行受控对照里，scaling/版本升级买到可见质量与访问率，re-observed state 始终不出 0.62–0.66 带——因为训练目标只监督可观察轴。
5. 漂移的其它结构性成因（各一手来源）：自回归误差累积（Genie 3 官方 "inaccuracies tend to accumulate"）；VAE 4× 时间压缩丢失快于 1/4 帧率尺度的物理过程（cosmos-3 深报推断，标注为推断）；多次 prompt 编辑本身是退化变量（Genie 4 官方限制）；动态物体污染静态记忆（MG3.5 动机）。

**WRBench 的处方**（论文原文方向，非本文裁决）："a what-memory that records hidden change and a training objective that supervises endpoint persistence"——记"发生了什么"而非"看起来怎样"的记忆 + 监督事件终点的训练目标。

---

## 2. 进度快照（截至 2026-11）

| 系统 | 路线 | 关键数字（口径） | 开放性 | 硬件门槛 |
|---|---|---|---|---|
| **Genie 4**（2026-10-27） | 逐帧生成 + Gemini 在环（混合） | 官方**零性能数字**；"a few minutes"交互（官方定性）；媒体"2 分钟"=转述不得当官方 | 闭源、无权重、无公开 API、零导出（genie-4 卡，已核） | 云端托管，本地部署不存在 |
| **Cosmos 3**（2026-05-31） | MoT 双塔 omnimodel | Super 64B/Nano 16B/Edge 4B；训练 Nano 31.05T tokens/1024×GB200（技术报告，已核）；Sim-Bimanual 901 帧@30FPS 480p（官方） | OpenMDW-1.1，权重开放，输出无许可负担（已核） | Linux-only；最小实测平台 Jetson Thor T2000 16GB；全家族 8GB 不可跑（cosmos-3 卡） |
| **Vidu S1**（2026-07-03） | AR + Diffusion（官方自述） | 540P（960×540）25FPS、最高 42FPS；"无限时长"；官方称"消费级 GPU 可达"（**官方口径但未给具体型号/显存**） | 线上体验 + API；**权重未公开**（"fully open"指用户可用，非开源） | 官方称消费级 GPU（TurboDiffusion/SageAttention/SLA 优化）；具体门槛未核 |
| **Matrix-Game 3.0/3.5**（3.5 报告 2026-08-30） | 双向 5B → 3 步因果学生 + Patch Memory | 5B（Wan2.2-TI2V-5B 底座）；720p ~20FPS 单卡（**GPU 型号有分歧**：仓库 note 记 H100，论文笔记记 H200——未核）；分钟级长时序 | Apache-2.0；两个 5B base + 蒸馏检查点已发 HF（GitHub README，已核） | 官方 README：**≥40GB VRAM**（A/H 系实测）、Linux、≥64GB RAM；704×1280 峰值 ~40GB |
| **ForgeWM**（2026-08-14） | MG2 底座（Wan2.1-1.3B）四阶段因果蒸馏 | 1/2/4 步学生；1 步 72.1FPS@352×640 单 H20、168.2ms/chunk（**VAE 解码除外**，README 已核）；训练 8×H20；数据 89GB 预编码 LMDB（GameFactory ~70h） | Apache-2.0；代码 + 全阶段权重 + 数据全开放（已核） | 训练 8 GPU；**推理显存未公布**（1.3B 底座，8GB 可行性待核） |
| **Solaris**（2026-02-25，NYU） | MG2 底座多人化 + Checkpointed Self Forcing | 12.64M 同步多人帧（9,240 episodes@20FPS）；跨视角失配 -22%、FID +36%（论文/项目页，已核摘要级） | 宣称 engine/模型/数据/评测全开源（论文摘要）；逐仓 LICENSE 未核 | JAX/TPU 训练（TRC）；推理门槛未核 |
| **StatePlay**（2026-07-29） | MoT 双分支：画面 + 显式游戏状态 | 状态预测归一化 L1 <0.06；机制保真 +18.6%（摘要，已核） | 目录称模型 + 10K 数据 + 训练器开放（转述，未逐仓核） | 未核 |
| **ABot-World-0 / LingBot-World** | 因果流式 | 目录称 16FPS@RTX 5090 / 16FPS 480p@单 GPU 节点（转述，未核）；LingBot 在 WRBench 表内（重观察支持率仅 6%，已核） | 目录称权重公开（转述，未核） | 未核 |
| **Marionette**（2026-08-14，Alaya Lab） | 显式 276D 状态 → 零参数几何桥 → Wan2.2-Fun-5B 外观 | 见 §3.2 | 论文承诺释出代码/地形数据；WildWorld 语料已公开；**代码释出状态未核** | 观察模型 5B 级；未核 |

**快照判读（只陈述事实模式）**：实时交互的已核硬件门槛几乎全在 H 系/40GB+ 档；唯一官方声称"消费级 GPU"的是 Vidu S1（闭权重、API 形态）；唯一底座小到 1.3B 且全开放的是 ForgeWM（推理显存未公布）。"已深研的恰不可入环、可入环的未深研"（W04 结论）在 2026-11 仍然成立。

---

## 3. 关键文献精读（3 篇）

### 3.1 WRBench —《Current World Models Lack a Persistent State Core》（arXiv 2606.20545，2026-06-18；已核：abs + HTML 全文）

- **问题**：现有基准只评可观察表面（保真/运动/相机可控），从不问"没人看时世界是否继续演化"；且视角干预存在**归因问题**——返回时状态错，可能是相机没转开、也可能是世界停了，单一分数分不清。
- **方法**：把相机运动当作**对可观察性的干预**；Natural-25（25 场景族 × 四级事件设计，分解"空间位移"与"状态变化"两轴）；WRBenchLib 记录每个模型实际收到的控制条件（四种范式：source-video/geometry-cache/model-inferred/prompt-only），按证据而非统一接口比较；六维诊断链（相机执行 → prompt 对齐 → 视觉完整 → 可见一致 → 重观察支持 → 重观察一致），第 vi 维只在通过第 v 维门控的子集上计分（2,073/9,600）；2,547 条去重人工标注逐维校准自动评分器（Qwen-3.5-9B 探针 + Qwen-3-VL-8B 门控，VGGT-Ω 恢复轨迹，DINOv2 完整性代理）。
- **关键结果**：23 模型全部呈现"preservation–access–re-observed-consistency 缺口"；外部记录买访问不买正确性（§1.6）；原地状态变化是普适最难情形；规模/架构/训练增量均不绑定终点持久性。
- **局限**：批处理、视频级、VLM 评分（无运行时闭环、无位级复现）；自动评分器依赖 VLM 判断质量；事件设计是受控因子网格而非自然分布。
- **与 OmniWorld 的关联**：它诊断的病灶（无观察期间事件不推进、身份漂移、无置信度语义）逐条对应 World Contract 的 identity/time/confidence 维度；其"门控后才计分"的设计与 E1 教训（空集≠稳定、指标会骗人）同构；它是"状态核缺失是公认病灶"的最强一手证据，但本身不提供运行时解法。

### 3.2 Marionette —《Predicting World States, Rendering Geometry, Painting Appearance》（arXiv 2608.14530，2026-08-14；已核：abs + HTML 全文）

- **问题**：像素/潜空间自回归把 pose/几何/遮挡/身份/控制效果全部交给同一生成序列隐式维持，长时程误差复合；"视频是低维世界状态的高维观察"，直接学像素分布把物理、身份、控制纠缠在一起。
- **方法**：三组件——①两阶段 AR 动力学：ActionGPT（2.5M 参数，每帧每实体选离散动作 token + 回归根运动）→ PoseGPT（25M，动作 → 276D 身体状态）；②零参数图形桥：6D 旋转 Gram-Schmidt → 根位移世界系积分 → 关节放置 → 扫描地形高度场 → 确定性跟随相机 → 光栅化三通道几何缓冲（高度/逐关节身份 id/逆深度）；③Wan2.2-Fun-5B 观察模型把 pose-control 视频画成 RGB，chunk-relay 续接。控制 = 覆写动作 token。数据 = WildWorld（商业动作游戏引擎级录制，动作/状态/观察三源同步，真值精确）。
- **关键结果**：控制因果权威（错动作流 RA-MPJPE +31%，48 段）；长时程失败在状态层、规则在状态层修复（穿地 CFR 0.337→0.114，-66%；间距 21.2m→5.1m），观察模型不动；外观路由无可测损失（FVD 831 vs 799；像素 AR 基线 975）。**负面结果同样重要**：可微惩罚形式的地面约束被优化器交易掉，同一约束以输入特征 + 事后投影形式才守住——"约束递归内容比正则化无约束生成器更有效"有其边界。
- **局限**：单怪物类型/固定武器（换域重训）；外观身份仍靠 chunk-relay 隐式传播、随时程衰减（作者处方：每 chunk 重供参考图）；部分实体无录制骨骼 → 观察模型学会"无状态补内容"；训练用真值 pose、推理用生成 pose 的分布移。
- **与 OmniWorld 的关联**：这是"显式状态 → 确定性几何 → 生成式外观"混合管线的一手可行性证明，与四层架构 L3（状态）/L4（表现）分工同构；其"状态层用米评分、观察层用 FVD"的两层评测协议是"世界数据穿系统的质量"口径的现成参照；"规则治理状态而非正则化生成器"与单写者裁决/契约精神相通；身份 id 固定编码进几何缓冲（每关节跨 rollout 固定 id）是 identity 契约的一个模型内实现样本。

### 3.3 Matrix-Game 3.5 —《Enhancing Real-Time Streaming Interactive World Models with Patch Memory》（arXiv 2608.29910，2026-08-30；已核：abs + 官方 GitHub README 全文）

- **问题**：长时程交互需同时保场景几何、动态一致性、相机控制，还要实时；帧级 latent 记忆缺几何对应（哪些历史与当前视角重叠？该投影到哪？哪些从未观察该让扩散自己生成？）。
- **方法**：①Patch Memory + Warped PRoPE（均零新增参数）：历史 patch 反投影 3D → 视锥查询 → z-buffer 可见性 → query-aligned memory canvas；相机投影矩阵 overlay 进时空 RoPE，一个 attention 同时携带 Δt 与相对投影 M_ij=P_i P_j^{-1}；②动静解耦：静态场景进 Patch Memory，动态主体走 4 张参考图压成的 reference tokens，运动感知过滤（跨帧投影重叠分数）决定谁能写入静态记忆，训练时主体区域激进 mask + 背景填充防泄漏；③两阶段渐进蒸馏：Perceptual Flow Matching（InternVideo2 感知特征空间因果初始化）→ curriculum Self-Rollout DMD（学生用自己生成、自己写入、自己检索的历史训练）。底座 Wan2.2-TI2V-5B；数据三管线（UE5 自主探索 / 3A 游戏自动录制 / 互联网视频 VLM 筛选），500 万+切片、1 万+小时（演讲口径，转述）。
- **关键结果**：分钟级 720p 实时交互（3 步因果学生 + INT8 + MG-LightVAE，单 H 系卡 ~20FPS——具体型号未核）；官方 README 硬门槛 ≥40GB VRAM / Linux / ≥64GB RAM；两个 5B base（第一/第三人称）+ 蒸馏检查点已发 HF；Apache-2.0。
- **局限**：记忆仍是"看过的几何"——Patch Memory 不记录无观察期间的事件演化（WRBench 诊断对它同样适用，LingBot/MG 系在 WRBench 表内重观察支持率低）；base 接口是相机轨迹（npz 外参）+ 文本，**不是键盘动作流**（动作控制是 3.0 系/演讲口径，3.5 仓库接口未核）；40GB 门槛远离 8GB 开发机。
- **与 OmniWorld 的关联**：Patch Memory 是"检索式记忆"的几何化顶点，展示了模型内记忆的天花板（空间召回 ≠ 状态核）；其"记忆可更新/替换/删除"（演讲语）与外部状态锚的差别在于**无作者权/置信度语义**；动静解耦的写入过滤是 ownership 判定的模型内类比；它是开源可入环候选里记忆机制最完整的标尺，但硬件门槛决定它只能离线入环（若权重/许可核验通过）。

---

## 4. 开放问题

1. **状态核 vs 记忆**：检索式/几何记忆买到访问率，买不到"无观察期间事件推进"（WRBench 平台 ~0.62）。what-memory + 终点持久性训练目标如何实现，全行业无一手解法。
2. **policy-in-the-loop 闭环验证缺口**：Cosmos Sim 官方自认条件生成 ≠ 策略反馈闭环；没有任何系统给出一手验证的"策略驱动世界模型效用"证据。
3. **评测会骗人**：无相机条件模型靠生成近静态视频刷高一致性分（MemoBench ORS<0.6 + motion-gated 主张，转述自 six-direction note）；Subject Consistency 偏好低运动视频（ForgeWM 论文）；与 E1"三指标两个把退化系统排更好"同构——任何新指标都需成对发布与空集护栏。
4. **确定性与位级复现**：生成式采样（Genie 1 temperature 2 随机采样是论文层反例）与"同 seed 逐字节 episode"不相容；官方材料普遍无 seed/确定性语义。蒸馏学生 + 固定调度能否位级复现，未见一手报告。
5. **混合的切割线**：Marionette 负面结果表明软约束会被优化器交易掉、硬投影才守住——"哪些量必须硬治理、哪些可以软学习"没有通用判据；状态 schema 逐域设计成本是显式路线的规模化瓶颈。
6. **多智能体**：Genie 4 官方列为 ongoing challenge；Solaris/MASS/MultiWorld 等刚起步（跨视角一致性 ≠ 共享状态语义）。
7. **8GB 可行性**：实时档全在 H 系；离线入环档（ForgeWM 1.3B 底座等）推理显存/Windows 兼容性无人公布（§6）。
8. **许可碎片化**：Apache-2.0（MG3.5/ForgeWM）→ OpenMDW-1.1（Cosmos 3）→ NVIDIA Open Model License（Cosmos 2）→ 闭源（Genie/Vidu）逐代逐家不同；底座/数据/衍生检查点各自带条款（ForgeWM NOTICE 逐目录溯源是正面样本）。

---

## 5. 仓库已有资产引用（复用结论，不复制）

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| projects/genie-4.md | 闭源/无权重/无 API/零导出；官方六条限制；"2 分钟"媒体转述纪律；Genie 4 无官方性能数字 |
| deep/2026-11-05-genie-4.md | Genie 1 论文机制（LAM/ST-ViViT/MaskGIT/16 帧/1FPS）；Genie 3 实时机制口径（轨迹即状态、每秒多次重算）；Gemini in-loop observe-plan-verify；World Contract 四维反命题对照 |
| projects/cosmos-3.md | OpenMDW-1.1 逐项（输出无许可负担）；硬件档位与 Jetson 16GB 最小实测；动作维度 9D–57D；②离线数据生产者 + 评测对象归宿 |
| deep/2026-11-05-cosmos-3.md | MoT 双塔机制/参数翻倍；统一动作接口（相对位姿伪动作）；FPS modulation；四阶段因果蒸馏 [SIM]；RTF 与 control-to-observation latency 度量；policy-in-loop 官方缺口 |
| 2026-11-13-six-direction-innovation-integration.md §1 D1 | 开源候选清单（ForgeWM/Solaris/StatePlay/MG3.x/ABot/LingBot）；"外部持久状态锚"创新点与三层占用者打折表 |
| 2026年实时世界运行时与生成式世界模型技术全景报告.md | 四家对照表骨架（⚠️ 须按修正口径：MoT 非"混合专家动态路由"——实为规则化参数解耦，cosmos 深报已核；生成侧是连续 VAE 潜变量非离散 token；USD Fabric "<5ms"未证实，09-21 修正表裁定） |
| 2026-09-21-direction-decision.md §2/§3 | R1/R2 修正表（RTFM 正名、OpenUSD 治理归属等）；世界模型七类版图 A–G |
| 2026-10-27-interactive-closed-loop-route.md | Cosmos-Predict2-2B 32.54GB 显存；"世界模型必须读信念"；生成误差第三类误差源；E1 指标教训 |

---

## 6. 待核验清单（重点：RTX 4060 Laptop 8GB 可入环性，四件逐条）

> 纪律：许可/权重/接口/显存四件全过才算"可入环候选"；任一未核即整体未核。HF 域名本环境历史不可达（cosmos-3 卡 §7-1），权重下载实测一律未核。

| 标的 | 许可 | 权重 | 接口 | 显存（8GB？） |
|---|---|---|---|---|
| **ForgeWM**（首选核验） | 已核 Apache-2.0（GitHub LICENSE + NOTICE 逐目录溯源）；**底座 MG2/Wan2.1 与 GameFactory 数据自带条款未核** | 发布声明已核（HF ForgeWM/ForgeWM 全阶段）；**下载可达性未核** | 已核：键盘 11 动作 + 鼠标 2D / 手柄 4D，inference.py 单 GPU 入口；**Windows 兼容性未核**（flash-attn 依赖） | **未核**：README 无推理显存数字；1.3B BF16 权重 ≈2.6GB 为算术推断，KV/VAE/激活峰值未知；72FPS 是 H20 数字不可外推 |
| **Matrix-Game 3.5** | 已核 Apache-2.0；samples CC BY 4.0；Wan2.2/DA3 依赖条款未核 | HF 发布已核（README）；下载未核 | 已核：相机 npz + 锚图 + prompt；**键盘动作接口未核**（base 是相机控制） | **官方 ≥40GB → 8GB 出局（已核出局）**；小变体/量化下探无官方依据 |
| **Solaris** | **未核**（论文宣称全开源，逐仓 LICENSE 未查） | 宣称开放，仓库地址/可达性未核 | 未核（JAX 栈，动作接口细节未读） | **未核**（MG2 1.3B 底座推断小，但多人双视角 + JAX 峰值未知；TPU 训练不可外推） |
| **StatePlay** | **未核**（目录转述开放） | **未核**（目录称模型 + 10K 数据 + 训练器开放） | **未核**（状态输出 schema 是否可机读未读全文） | **未核** |
| **ABot-World-0** | **未核** | 目录称公开，未核 | 未核 | 目录称 16FPS@RTX 5090（转述）→ 实时档出局推断，离线显存未核 |
| **LingBot-World** | **未核** | 目录称公开，未核 | 未核 | 目录称单 GPU 节点（转述），未核 |
| **Vidu S1** | 闭权重（官方仅体验 + API）→ **权重路线出局**；API 条款未核 | 不适用 | API 形态存在（platform.vidu.cn），端点文档未核 | 官方"消费级 GPU"声称无具体型号，本地部署不存在 |
| **Marionette** | **未核**（论文承诺释出代码，实际释出状态未查；WildWorld 语料条款未核） | 未核 | 论文层接口明确（276D 状态 + 动作 token 覆写），代码未核 | 观察模型 Wan2.2-Fun-5B → 8GB 推断出局，动力学两阶段仅 27.5M 参数（状态层单独可跑性未核） |

**附加未核项**：MG3.5 实时 20FPS 的 GPU 型号（H100 vs H200 两处转述分歧）；WRBench 23 模型完整名单与逐模型分数（全文已读但表格数字未逐项复核）；MemoBench/WROP 本轮未重查（沿用 six-direction note 已核状态）；"AMD 收购 World Labs"等行业传闻维持转述未核。

---

## 7. 来源与核验状态

| 来源 URL | 关键内容 | 状态 |
|---|---|---|
| https://arxiv.org/abs/2606.20545 （+ /html 全文） | WRBench：定义/六维链/四范式/23 模型诊断/原地变化最难/规模不绑定 | **已核**（2026-11-13 检索返回 abs + HTML 全文） |
| https://arxiv.org/abs/2608.14530 （+ HTML 全文经镜像） | Marionette：276D 状态/零参数桥/规则修复/31% 控制权威/FVD 831 vs 799/负面结果 | **已核**（abs 直抓 + 全文） |
| https://arxiv.org/abs/2608.29910 | MG3.5：Patch Memory/Warped PRoPE/动静解耦/PFM+Self-Rollout DMD | **已核**（abs 直抓，2026-08-30 提交） |
| https://github.com/Riemann-Dynamics/Matrix-Game-3.5 | Apache-2.0；≥40GB VRAM/Linux/64GB RAM；5B base×2 + 蒸馏检查点；相机 npz 接口；引用块确认 arXiv 编号 | **已核**（检索返回 README 全文） |
| https://arxiv.org/abs/2608.14022 （经 HF papers 页 + CatalyzeX） | ForgeWM：四阶段/1-2-4 步/双路径部署/replay refinement | **已核**（摘要多源一致；arXiv abs 本体未直抓，编号经 GitHub 引用块 + HF + CatalyzeX 三方确认） |
| https://github.com/asdfo123/ForgeWM | 72.1FPS@H20（VAE 除外）/168.2ms/8×H20 训练/89GB LMDB/Apache-2.0 + NOTICE/动作接口/滑窗 6 帧 | **已核**（检索返回 README 全文） |
| https://arxiv.org/abs/2602.22208 | Solaris：12.64M 帧/Checkpointed Self Forcing/MG2 底座/多人评测 | **已核**（abs + HTML 大段） |
| https://arxiv.org/abs/2607.26754 （经镜像） | StatePlay：MoT 双分支/状态 L1<0.06/机制保真 +18.6% | **已核**（abs 全文） |
| https://www.shengshu.com/en/news/vidu-s1-realtime-interactive-model/ | Vidu S1 官方：540P 25/42FPS、AR+Diffusion、无限时长、消费级 GPU 声称、TurboDiffusion/TurboServe | **已核**（官方新闻全文，2026-07-03） |
| https://dhairyamishra.github.io/projects/solaris/ | Solaris 作者项目页：12.64M/9240 episodes/-22% 跨视角/+36% FID/JAX-TPU | 已核（作者自述页，数字与论文摘要一致；细节数字属作者页口径） |
| https://blog.csdn.net/shaobingj126/article/details/163124227 等 | MG3.5 "单卡 20FPS"、Patch Memory 科普、"内置物理引擎"说法 | **转述**（"内置物理引擎/关节指令输出"仅媒体口径，论文摘要无此声称——不得当官方） |
| https://blog.csdn.net/csdnnews/article/details/162009132 | 刘扬智源演讲：定义框架/状态-动作联合训练/三管线数据/四挑战 | **转述**（会议报道，非论文） |
| https://zhuanlan.zhihu.com/p/2063005045369655808 | MG3.5 论文笔记：H200 20FPS、Warped PRoPE 数学细节 | **转述**（与仓库 note 的 H100 记载分歧，GPU 型号未核） |
| 仓库资产（§5 表列路径） | Genie 4/Cosmos 3 事实与机制、决策链、E1 教训 | 已核（各卡标注 2026-11-05 一手核验，本文引用不复制） |
| six-direction note §1 D1 目录转引（45 条目录） | ABot-World-0/LingBot-World/StatePlay 开放性 | **转述/未核**（第三方审计目录，各仓未逐页） |

**矛盾记录**：①MG3.5 实时 GPU：仓库 note H100 vs 论文笔记 H200——两说均转述，标未核；官方 README 只给"≥40GB A/H 系"。②媒体称 MG3.5"内置物理引擎、输出关节控制指令"——arXiv 摘要与官方 README 均无此声称，按数字纪律不采信为官方口径。③全景报告 MoT"混合专家/动态路由/离散 token"表述与 Cosmos 技术报告（规则化路由/连续潜变量）不符，引用一律以 cosmos-3 深报修正口径为准。