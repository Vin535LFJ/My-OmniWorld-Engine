# W04 深度层系列总摘要（2026-11-05）

> **性质声明**：本文件是 11 份深度报告的**证据综述与导航索引**，不是决策文档——文中一切"系列级观察"只是证据间的跨报告共性归纳，由此推出的任何方向取舍，**裁决权归 owner**。
> **检索日期**：2026-11-05（少数报告的补充核验跨到 2026-11-06，以各报告头部标注为准）。
> 上层材料：[deep/README.md](deep/README.md)（深度层约定）· [projects/](projects/README.md)（事实卡）· [third-party-research-map.md](third-party-research-map.md)（研究地图）· [research-queue.md](research-queue.md)（调研队列）。

## 一、W04 深度层是什么 & 与 projects/ 事实卡的分工

W04 生态调研分两层，互相去重、只链接不复述：

- **projects/ 事实卡**管"**是什么**"：定义、许可、硬件、平台、接口、边界裁定（§6 族谱归宿）、UNVERIFIED 台账（§7）——活文档，随项目演进修订。
- **deep/ 深度报告**管"**怎么做到的**"：架构、算法、数据流、关键取舍（每节按"怎么做 → 为什么这样设计 → 代价是什么"三层展开）——一次性深潜，文件名带日期（`2026-11-05-<slug>.md`）；项目演进时另起新日期报告，旧报告不删。

深度报告的纪律与事实卡相同（deep/README.md §约定）：Trust label 头、一等来源（论文原文 / 官方技术文档 / 官方源码 / 官方博客公告）、每条机制性陈述附 URL 与检索日期、核验不了的标 **UNVERIFIED** 不作为决策依据；闭源项目只讲官方公开口径，推断必须显式标注并给依据。本系列 11 份报告全部通过该纪律核验（头部三行齐全：Trust label / 最后核验：2026-11-05 · 状态：首建 / 配套事实卡互链），11 张配套事实卡均已在文件末尾追加指向深度报告的链接行。

**覆盖范围声明（生态两边的深浅，勿当全景读）**：本系列 11 份深度报告全部位于生态的**被测系统/组件侧**（生成式世界模型、几何/分割观测生产者、场景表示、物理后端、数据层与观测工具、仿真平台），**评测基准/评测框架侧一篇未深**——`research-queue.md` Intelligence 轨明列的评测侧对象（WorldMark、WBench、stable-worldmodel、cosmos-evaluator、World-In-World 等，queue 行 9/10/21/22）均未进入 W04 深度层。本摘要全文（含 §三 导航、§四 证据缺口 rollup、§五 结论定位）描述的只是"已深的那半边"，不构成 W04 全景。

**可入环错位提示**：本次已深的生成式世界模型恰好都是自身承认不可入环的对象——genie-4 闭源、无程序化端点（`projects/genie-4.md` §5/§6）；cosmos-3 官方最低平台 16 GB、不在本机 8 GB 线内（`projects/cosmos-3.md` §4）；两项核验结论见 `research-queue.md` 行 13 的进展记录。同文件明文"MineWorld/Matrix-Game 类开源候选仍待逐卡核验"（Stage C 评测主体，8 GB 可跑）且 WorldFoundry & cosmos-evaluator 差分研究为 High 优先（queue 行 9/13/20）——"能真正进评测环"的那批对象一份深报都没有；后续深度层的补深应优先对齐这批对象。

## 二、系列级综合观察（原理层跨项目共同模式）

> 每条注明依据的报告；依据均为各报告已核验内容，不含 UNVERIFIED 项（证据缺口见 §四，不进本节）。

**观察 1：把"可确定的"从学习/并发组件里剥出去，是跨系统共同的可靠性来源。**
依据：gvhmr（网络只回归 151 维消歧中间表示，GV→世界系展开全部是无参数确定性几何——相机平移不进学习问题）；vggt（回归目标的 gauge 歧义用确定性约定钉死：首帧特化 camera token + GT 归一化使目标唯一）；openusd（多作者意见折叠是确定性纯求值：prim index 预计算缓存 + strong-to-weak 解析）；newton-mujoco-warp（MuJoCo CPU 管线官方 "entirely deterministic"，GPU 化后确定性降为 opt-in 位掩码）；rerun（全链路单一 Arrow 物理形状 + .rrd framing 稳定/数据模型版本化分离——这是**格式层承诺**，非字节级确定性：同一报告把"二进制 rrd 进不了字节级回归"列为反面印证，见 §三 E rerun 条目）。
对 OmniWorld 的含义：位级确定性目标（同 seed 逐字节复现）不是孤例，是这一批系统共同吃过的教训——学习组件负责歧义，确定性管线负责账本。

**观察 2：流式 + 有界状态换实时，代价一律是长程一致性与误差累积。**
依据：genie-4（历史生成轨迹即世界状态；"inaccuracies tend to accumulate over time" 为 Genie 3 官方原话，一致性是 emergent capability 而非机制保证）；sam-2（FIFO 近期 6 帧 + 提示帧槽位 + ≤16 个 object pointer，每帧计算量与视频长度无关——这就是 ~44 FPS 口径的架构基础）；newton-mujoco-warp（nconmax/njmax 固定容量缓冲，溢出=未定义行为+bitmask 自查）；cosmos-3（流式推理 chunk 因果 + 初始 sink 区 + 滚动窗口有界历史）；rerun（15–60 Hz 图像与 500–1000 Hz IMU 免对齐共存，对齐延迟到查询期 fill_latest_at）。
对 OmniWorld 的含义：所有调研对象都把"很久以前"交给有界记忆或外部账本——持久身份层与 tick 账本正是它们让渡出去的部分，也是评测环可以补测量的地方。

**观察 3：置信度在全部 11 个对象中都不是一等公民，且从不带裁决语义。**
依据（11/11 逐份）：vggt（唯一逐像素置信度，但它是 aleatoric 损失均衡点的副产品、官方无校准主张，camera 输出干脆没有置信度）；gvhmr（仅 6 关节静止概率）；sam-2（遮挡头 object presence 标量）；genie-4（官方公开面零置信度接口；SynthID 是内容溯源不是置信度）；cosmos-3（输出天然没有 confidence 一环，评测桥须把生成误差当独立误差源建模）；rerun（数据模型无一等 confidence 组件，报告 §4）；openusd（报告 §2.7 明列"无 ownership/confidence 对应物"是它不是运行时的机制原因之一）；wan22-animate（输入侧消费显式置信度——姿态关键点分数有 ≥0.75 门槛，但**输出侧没有任何置信度**，报告 §4）；newton-mujoco-warp（MJWarp 无 confidence 通道，仿真输出按定义置信度为 1，仅 Data.overflow bitmask 与 solver_niter 可作隐式健康信号，报告 §4）；nsight-cupti（被观测对象本身无置信度语义，confidence 轴映射为 trace 证据的元数据——丢记录数/采集配置/版本，报告 §4）；isaac-sim（Replicator 真值"构造即真值"，标注数据结构无 per-detection 置信度语义，报告 §4）。
对 OmniWorld 的含义：World Contract ⑦（强制单标量置信度 + 唯一消费者是 ECE 校准）没有任何现成外部对应物——置信度接口必须引擎侧自建。在"生成式世界模型的置信度/校准评测"这一语境下，目前唯一可借用的现成官方度量是 cosmos-3 报告 §2.7 记录的 RTF 与 control-to-observation latency——注意二者是**响应性度量而非置信度度量**，且 cosmos-3 报告 §4 已明言"ECE/校准口径对生成式预测器需要重新定义而非直接复用"；观测侧的现成原料另见 §三 nsight-cupti 条目（D5 判据"拷贝次数+同步点"与三组时间戳），与本条语境不同、不互斥。

**观察 4：多源融合的仲裁普遍是隐式的（强度序或后写胜出），无作者权、无账本。**
依据：openusd（LIVERPS 强度序隐式仲裁多作者，败者意见不留痕）；rerun（后写胜出 + 历史全留，但无 per-write 作者字段——ownership 只能编码进 path 约定）；genie-4（用户与 Gemini 双写者无裁决语义，"multiple complex prompt edits" 被官方点名为漂移诱因）；newton-mujoco-warp（callback.control 分相注入 + 接触不自动回流——单向写路径即所有权纪律）。
对 OmniWorld 的含义：OmniWorld 的"单作者 + belief 账本显式留痕"与它们是同一问题（多源融合）的正交解；做适配器导入/导出时必须显式补映射约定（如 rerun 报告 §5 的 Proposal→path 约定表）。

**观察 5：统一接口吞掉异构任务——接口即架构，代价必须显式计价。**
依据：cosmos-3（MoT 双塔一条 token 序列覆盖 11 种工作流，代价是总参数精确翻倍 4B/16B/64B = 2× dense 2B/8B/32B，官方句式证实）；wan22-animate（把参考图/引导帧/待生成区域统一进 Wan-I2V 的 noise+conditional+mask 三元输入，最小化后训练分布偏移，统一 Animate/Replace 两任务）；sam-2（点/框/掩码全部编进同一个 256 维 token 空间，掩码可作提示）；vggt（camera/register token 是几何的"接口"，钉死输出坐标系）；isaac-sim（Kit 应用 = 声明式扩展依赖清单，换物理后端 = 换 .kit 文件整组开关扩展）。
对 OmniWorld 的含义：World Contract 四维本身就是对外部系统的"统一接口"；集成任何一家的代价（参数翻倍、专用缓冲、声明式依赖图）都应写成接口约定，而不是藏在实现里。

## 三、分组导航与各报告摘要

11 份报告按领域分六组（3+3+1+1+2+1）。每份给出：深度报告链接、配套事实卡链接、摘要要点。

### A. 生成式世界模型

**[genie-4] Genie 4 / Project Genie（Google DeepMind，闭源）** —— [深度报告](deep/2026-11-05-genie-4.md) · [事实卡](projects/genie-4.md)
- Genie 4（2026-10-27 研究预览）= 逐帧生成的像素/音频世界核心 + Gemini 家族在环（Gemini Omni 锚定外观行为、Gemini 3.8 Flash 环内引导 observe–plan–verify、Gemini 4 Argon 任务分解）；官方 Future work 自认这是 hybrid world models（Gemini 推理 + Genie 像素）路线的 early step。
- 实时交互的官方机制口径（Genie 3 官方原文，代际直系）：自回归逐帧生成、历史生成轨迹即世界状态、"实时"= 新输入一到，整个轨迹的计算每秒发生多次；一致性是 emergent capability 而非机制保证，误差累积为官方原话。
- 文本条件化双层化：Genie 3 的 promptable world events（文本直达生成器）→ Genie 4 的 Gemini 上下文适配用户 prompt + 输入间隙自主 steering；"multiple complex prompt edits" 是官方点名的漂移诱因。
- 对 OmniWorld：World Contract 四维全部是 Genie 4 的反命题——object permanence 官方承认漂移、时间=帧流不可复现、用户与 Gemini 双写者无裁决语义、零置信度接口（SynthID 是溯源非置信度）；同为 "hybrid"，Google 混在黑盒内，OmniWorld 状态在白盒可对账。
- 红线执行：Genie 4 无论文、分辨率/帧率/延迟官方零披露（不得沿用 Genie 3 的 720p/24fps）；机制层只写官方口径，Genie 1 论文（arXiv:2402.15391）是唯一论文层谱系参照。

**[wan22-animate] Wan2.2-Animate（阿里 Wan 团队，开源）** —— [深度报告](deep/2026-11-05-wan22-animate.md) · [事实卡](projects/wan22-animate.md)
- Wan-I2V（Wan2.1-I2V-14B dense 基座，config 实证 Wan2.1_VAE.pth + 40 层单模型）上的后训练模型；核心原理：把参考图外观/时间引导帧/待生成区域统一进 Wan-I2V 的 noise latent + conditional latent + binary mask 三元输入，参考图 latent 拼在序列时间维最前而非作首帧——最小化后训练分布偏移并统一 Animate/Replace 两任务（论文 §3.2 + 官方源码逐行核验）。
- 控制信号双通道解耦：骨架经 ViTPose-H 提取、retarget（可选 FLUX.1-Kontext 辅助）、空间对齐渲染后由专用 Conv3d patchify **加性注入**；表情不用 landmarks，用 LIA 式编码器把 512×512 驱动脸压成逐帧 512 维向量（QR 正交基投影），因果 Conv1d 时间对齐后由每 5 层共 8 处 FaceBlock 以 cross-attention 注入。
- 替换模式 = 人物置零的环境帧条件 + mask 在人物区为 0 + Relighting LoRA（rank=128，IC-Light 合成同角色异光照训练对）；基座管外观一致、LoRA 管光照适配；LoRA target 范围论文与开源代码不一致，已如实记录为 UNVERIFIED。
- 推理机制：每段 78 帧 = 1 个 reference 帧位 + 77 个目标帧（4n+1 约束的是 77 帧目标段而非 78 帧整段：77=4×19+1）、段间 1/5 帧 temporal latents 滑窗衔接、Flow-DPM++ 20 步、CFG 默认关闭、30fps；驱动视频不足整段时官方代码用 ping-pong 回弹填充（animate.py:201-213）。
- 为什么适合角色动画不适合通用编辑（三条结构性理由）：控制信号全部人形中心；同门对照 VACE 反向不稳定；人物区域是整段重新去噪而非编辑。pose/face/mask/bg 四路接口与 World Contract 四轴逐轴同构（报告 §4）。

**[cosmos-3] NVIDIA Cosmos 3（开放权重，OpenMDW-1.1）** —— [深度报告](deep/2026-11-05-cosmos-3.md) · [事实卡](projects/cosmos-3.md)
- 单一 Mixture-of-Transformers（MoT）双塔统一 VLM/视频生成/世界仿真/动作预测：一条 token 序列、两套参数（AR reasoner 塔走 causal next-token，DM generator 塔走 rectified flow 去噪），仅在共享联合注意力算子相遇——Q_DM 全注意力读 [K_AR;K_DM]，AR 永不读 DM（技术报告 §2.3，全文已解析）。
- 家族分层从 Cosmos 1.0 的模型间分工内化为模型内分工：Wan2.2 VAE tokenizer、多条件混合预训练（T2I/T2V/I2V/V2V = 20%/56%/16%/8% 同权重）、动作三模式（forward/inverse dynamics/policy）靠 clean/noisy token 排布翻转实现，换任务不改架构。
- MoT 采纳依据（Meta+Stanford 原论文）：按塔规则路由优于 MoE 学习路由，Chameleon 7B 设定 55.8% FLOPs 追平 dense、wall-clock 47.2–75.6%；Cosmos 3 代价是总参数精确翻倍（4B/16B/64B = 2× dense 2B/8B/32B，官方句式证实）。
- 后训练两条已验证生产线：模态级 SFT + 交互仿真专用四阶段因果蒸馏课程（bidirectional adaptation → Teacher Forcing → TF-dCM → SF-DMD）配流式推理（chunk 因果、有界历史+sink；官方度量 RTF 与 control-to-observation latency）。
- "Physical AI 数据工厂"定位有完整证据链：模型栈即数据生产线、训练管线内数据飞轮闭环、OpenMDW-1.1 使产出可自由出厂；对 OmniWorld：输出天然缺 World Contract 四要素，评测必须走独立评测桥，官方 RTF/延迟度量是 Phase 4 可直接借用的测量口径。

### B. 视频理解与结构恢复

**[vggt] VGGT（Meta × Oxford VGG，CVPR 2025 Best Paper）** —— [深度报告](deep/2026-11-05-vggt.md) · [事实卡](projects/vggt.md)
- ~1.2B 前馈 Transformer 把 SfM/MVS 的多阶段迭代优化摊销为一次前向：1～数百帧 → 24 层 alternating-attention backbone → 同时输出相机外内参、深度图、3D 点图、跨帧点轨迹，稠密输出自带逐像素置信度。
- 三个关键机制：① Alternating Attention（帧内自注意力+全局自注意力交替，等参数量下同时优于 global-only 与 cross-attention）；② 首帧特化 camera/register token 钉死首相机坐标系消除 gauge 模糊；③ 置信度来自 aleatoric-uncertainty 损失（γ·L·conf − α·log conf），均衡点上 conf ∝ 预期误差，无需校准监督。
- 与 COLMAP/BA 的对比：纯前馈 10 帧 0.2 s（H100）在 CO3Dv2/Re10K 压过全部优化式方法；但 phototourism 基准 IMC 上前馈（71.26）不压过 VGGSfMv2（76.82），+BA 后处理（1.8 s）才达 SOTA（84.91）——优化未被消灭，只是被移出推理路径。
- 对 OmniWorld：四类输出与 World Contract 的 identity（跨帧轨迹）/confidence（逐像素置信度）天然同构，是最对位的几何观测生产者；但每场景一次前馈、无时序模型、坐标系 gauge 绑定输入首帧——time 轴与 canonical frame ownership 必须引擎侧接管；许可约束下只可外部运行、产物入环（事实卡 §3/§6）。
- 工程边界：全局自注意力显存平方级增长（Table 9 为 2026-05-15 修复前数据）；released 训练栈不含 track 损失（NotImplementedError），轨迹头微调无官方路径。

**[gvhmr] GVHMR（浙大 zju3dv，SIGGRAPH Asia 2024）** —— [深度报告](deep/2026-11-05-gvhmr.md) · [事实卡](projects/gvhmr.md)
- 把单目视频变成世界坐标系人体运动序列：核心原理是不在世界系里回归运动，而在逐帧定义的 Gravity-View（重力-视线）坐标系里回归 SMPL-X 全局姿态——天然重力对齐、逐帧唯一、帧间只剩 1-DOF 绕重力 yaw，再用相机相对旋转把序列几何展开回世界系。
- 架构两段式：网络只回归消歧中间表示（151 维逐帧目标），GV→世界系的坐标换算（yaw 累积旋转、速度 cumsum、y 轴翻转）全部是无参数确定性几何——相机平移被排除出学习问题。
- 三个关键机制：① GV 坐标系使相机旋转误差影响降阶（DPVO→GT 陀螺仪仅 276.5→274.9 mm，WHAM 掉 19.5 mm）；② RoPE Relative Transformer 非自回归主干 + ±60 帧带状掩码，任意长视频一次前向（换绝对位置编码 126.3→304.4 mm）；③ stationarity 后处理贡献一半以上脚滑指标（FS 6.8→3.0 mm）。
- 与普通 HMR 的本质差别：经典 HMR 学"图像→相机系姿态"，GVHMR 学"图像+相机旋转→重力对齐全局姿态+局部速度"；相机系姿态质量与 WHAM 同级，增益集中在世界系指标。
- 对 OmniWorld：定位为②适配器（运动观测生产者）；输出契约与 World Contract 同构但每项有缺口——identity 只承诺单条 top-1 track、time 有硬编码 30fps 假设、ownership 是四个预处理 .pt 缓存+一个 results 产物、confidence 仅有 6 关节静止概率。

**[sam-2] SAM 2（Meta FAIR）** —— [深度报告](deep/2026-11-05-sam-2.md) · [事实卡](projects/sam-2.md)
- 用流式记忆 transformer 统一图像/视频可提示分割：视频逐帧流入，当前帧特征经 4 层 Memory Attention（self-attn→cross-attn 读记忆库→MLP）条件化后进 Mask Decoder，每帧计算量与视频长度无关——"约 44 FPS"实时口径的架构基础。
- 记忆库三类条目分工：近期 6 帧 64 通道空间特征记忆（FIFO）+ 提示帧记忆（永不逐出、专属槽位）+ ≤16 个 256 维 object pointer（每帧 1 个、切成 4×64-dim token、窗口更长）；消融证明特征记忆 64 维优于 256 维；GRU 与直接存储的同口径对比（Table 11）中 GRU 指标略高但更慢（SA-V val 65.3 vs 64.5，LVOSv2 68.9 vs 67.0），论文结论是"GRU 不带来改善"、直接存储更简单更高效——并非直接存储指标占优；object pointer 在 SA-V val +3.8 J&F。
- Promptable 统一嵌入：点/框编成 256-dim sparse token、掩码经 conv 栈编成 dense map——全部进同一个 TwoWayTransformer 的 256 维空间；提示帧效力写入记忆后即传播，掩码可作提示（固定 logits 20·mask−10 直通路）。
- 遮挡头（object presence 标量）+ 多候选掩码（3 个，按 predicted IoU 选）让"对象不在"成为合法且可传播的输出——消失帧以遮挡嵌入写记忆、obj_ptr 混入 no_obj_ptr。
- 掩码 ≠ 身份跟踪的机制原因：obj_id 是调用方管理的 client-side id、匹配只对单对象自己的记忆做（无 gallery/检测/re-ID）、输出无语义标签——identity/confidence 必须由引擎侧 World Contract 接管，跟丢应映射为观测缺失 + confidence 衰减而非身份切换（"无再识别"为综合推断，报告建议实验验证）。

### C. 场景表示与组合

**[openusd] OpenUSD（Pixar）** —— [深度报告](deep/2026-11-05-openusd.md) · [事实卡](projects/openusd.md)
- 核心原理：把场景表示为分布多个持久层中的 opinion 集合，组合引擎（Pcp）把每个 prim 的意见源预计算成有序 prim index 并缓存，值解析沿该索引 strong-to-weak 取第一个有效意见——非破坏性协作由此成为确定性纯求值问题。
- LIVERPS 强度序（Local/Inherits/VariantSets/Relocates/References/Payload/Specializes，与源码 PcpArcType 枚举序一致）+ per-prim index 缓存是性能支点（官方：每步重算 "would be very costly"）；封装（reference）/广播订阅（inherit）/全局最弱（specializes）是三种编辑拓扑的刻意分化。
- value resolution 的机制取舍：组合缓存/解析不缓存；属性四值源序 TimeSamples>Splines>Default>ValueClips+schema fallback；动画值强度序优先于时间序（不能跨源稀疏合并，Value Clips 是唯一官方出路）。
- "不是运行时"的四条机制原因：①持久状态只有 authored opinions，Stage 只是缓存视图；②authoring 是唯一事件；③为消费烘焙数据而生（OpenUSD dev 树经完整文件树检索验证无 usdrt/Fabric 路径）；④无 ownership/confidence 对应物。
- 对 OmniWorld：USD 用强度序隐式仲裁多作者（败者意见不留痕），OmniWorld 用 tick 账本显式留痕——同一问题的正交解；可吸收的工程纪律：Edit Target、session layer、defer-dirty、依赖驱动的精确失效、prim index 可诊断性。

### D. GPU 物理仿真

**[newton-mujoco-warp] Newton / MuJoCo-Warp（NVIDIA × DeepMind × Disney Research）** —— [深度报告](deep/2026-11-05-newton-mujoco-warp.md) · [事实卡](projects/newton-mujoco-warp.md)
- 三层栈：Warp 是静态类型 Python 子集的 kernel 语言（@wp.kernel 与 CUDA kernel 一一对应，经 LLVM/NVRTC 编译），MJWarp 是 MuJoCo 管线在 Warp 上的 stage 级镜像实现（同一 MJCF、固定 timestep），Newton 是 wrap mujoco_warp 的多求解器框架层。
- 核心原理：用 (nworld, ·) 批处理数据布局让数千独立世界共享同一编译产物并行推进，接触/约束走 nconmax/njmax 固定容量缓冲（溢出=UB+bitmask 自查），CUDA Graph 把一次 step 的上百次 kernel launch 压成一次重放——单步延迟让位于批量吞吐（官方：MJWarp 单步慢于 MuJoCo CPU）。
- 约束求解按世界并行 + islands/睡眠/compact solver（GPU 上的并查集 DSU 分岛），并用 wp.capture_while 以"未收敛世界计数"驱动 CUDA Graph 条件循环实现全局早退——全部世界收敛即停。
- 确定性链条（对 J2 最关键）：MuJoCo CPU 管线 "entirely deterministic"（须存 warmstart）→ GPU 浮点 atomic 求和顺序不定默认破坏复现 → Warp 1.15 起 opt-in sort-then-reduce（RUN_TO_RUN 同架构位精确）→ MJWarp 有 opt.deterministic 位掩码（CONTACTS/CONSTRAINT/ATOMICS，源码核验），但 CONTACTS 位在 main 分支是 pass 空桩，且 NVIDIA 官方明文"这些是 Warp 能力，不是整个 MJWarp rollout 的保证"。
- 对 OmniWorld：外挂物理执行后端（仿真器=外部真值权威）候选；实体 id 是 MJCF 编译期索引（只能作 truth 实体的外部别名）；callback.control 分相注入与"接触不自动回流"与契约⑤单向写路径同构；GPU 后端上 J2 逐字节复现默认不成立，spike 第一步必须同 seed 双跑位级比对。

### E. 可观测性

**[rerun] Rerun（Rerun.io，Rust，Apache-2.0 开源核心）** —— [深度报告](deep/2026-11-05-rerun.md) · [事实卡](projects/rerun.md)
- "物理 AI 的数据层"：多模态时序数据的记录/存储/查询/可视化/训练一体化 SDK；无 arxiv 论文，最高可得来源层为官方文档+源码+博客（报告头部有来源分层说明）。
- 核心原理一句话：把所有模态统一降维成「entity path（身份轴）× timeline（时间轴）双轴索引下的 Arrow 列式 chunk」，记录→传输→落盘→查询→渲染→训练共享同一物理数据形状，中间零格式转换。
- 三个关键机制：① chunk 贯穿一切（SDK 微批 200ms/2MiB → store compaction 384KiB/4096 行 → rrd → 查询 → GPU 直通；0.18 重写后标量场景写/摄入 100x、内存 35x）；② 双轴模型 + partial updates（latest-at ≈ AsOf join / range 查询，多频率数据免对齐共存）；③ .rrd framing 稳定/Sorbet 模型版本化（footer 换随机访问，LZ4 仅省≥10% 时按消息启用）。
- 与 World Contract 同构：entity path+TUID 对应 identity（TUID 单调不复用）、多 timeline+static shadow 对应 time、"后写胜出+历史全留"对应契约⑥——但无 per-write 作者字段（ownership 须编码进 path 约定）、数据模型无一等 confidence 组件。
- 反面印证自研路线：GPU/Wgpu 渲染依赖、150–174MB SDK、二进制 rrd 进不了字节级回归、viewer RAM 满 75% 即丢最老数据——与 ADR-0012 零依赖+JSON 工件字节级回归相反；导出 adapter 仅在闭环路线富可视化需求落地后触发。

**[nsight-cupti] Nsight Systems / CUPTI（NVIDIA）** —— [深度报告](deep/2026-11-05-nsight-cupti.md) · [事实卡](projects/nsight-cupti.md)
- 观测栈两层：CUPTI 是给工具作者的观测基座（动态库、惰性初始化、支持 CUDA_INJECTION64_PATH 注入零修改采集），Nsight Systems 是把 CUPTI 信号与 OS/CPU/NVTX 采集器拼成系统级时间线的成品工具；官方文档直写 "Nsight Systems uses CUPTI for CUDA profiling"，CUPTI 单订阅者仲裁反向证明 nsys 本身就是 CUPTI 订阅者。
- 关键机制一（Callback API）：订阅者在 CUDA API 调用的 ENTER/EXIT 相位被同步调用，能拿参数/返回值与 ENTER→EXIT 配对；代价是热路径税、单订阅者、回调内 CUDA 调用重入限制。
- 关键机制二（Activity API）：GPU 事件异步写进默认在显存的缓冲（~3MB/缓冲、约10万条、池上限 250、预分配 3 块 ping-pong），worker 线程批量交付；代价是缓冲不足丢记录（有计数器可查）与配置敏感——KERNEL kind 会强制 GPU 上 kernel 串行（官方注释逐字）。
- 关键机制三（两级关联）：correlationId 自动把 API 调用与 GPU activity 一对一挂钩（前置：启用 RUNTIME/DRIVER activity）；external correlation 通过每线程 push/pop id 栈把宿主 uint64 id（CUSTOM0-2 官方明文 reserved for external tools）注入 activity 流——tick 号直接当 externalId 即可机器 join。
- 对 OmniWorld：D5 判据"拷贝次数+同步点"的原料（MEMCPY/SYNCHRONIZATION activity + queued/submitted 时间戳）原子存在；World Contract 映射——identity=tick 号经 external correlation 入流，time=三组时间戳，ownership=观测是读操作但订阅权互斥，confidence=trace 证据必须附带丢记录数/采集配置/nsys 版本元数据。

### F. 仿真平台

**[isaac-sim] Isaac Sim 6.0.1（NVIDIA）** —— [深度报告](deep/2026-11-05-isaac-sim.md) · [事实卡](projects/isaac-sim.md)
- 本体形态是"Kit 应用 = 声明式扩展依赖清单"：v6.0.1 的 isaacsim.exp.base.kit 逐行列出 isaacsim.*/omni.* 扩展依赖（含 omni.physx.tensors、omni.replicator.core、semantics.schema.*），渲染/物理/传感器/SDG 全部以 Kit 扩展挂进同一个 OpenUSD 场景（官方 "USD scene, physics state, sensors, semantics, and graphs in one runtime"）。
- 场景层是 USD+Fabric 双层结构：USD 管组合语义与文件存储，Fabric 把组合结果拉平成 GPU 友好 SoA 运行时；USDRT 提供 pin-compatible 的 Fabric 读写 API——"资产语义 vs 活状态"的分工，代价是 post-composition-only 的双轨一致性窗口。
- 传感器即渲染器：相机 = USD Camera prim + RTX 渲染 + Replicator render product；RTX Lidar/Radar/Acoustic 走 RTX Sensor SDK 在渲染时用 RTX 硬件光线追踪、结果写入 GenericModelOutput AOV——材质光谱一致性是设计回报，RT Cores 硬门槛 + DLSS 降噪成为保真变量是代价。
- 物理可插拔有源码级机制：isaacsim.exp.full.newton.kit 以 enabled=false 关闭全部 omni.physx.* 扩展、换装 isaacsim.physics.newton.* + omni.warp.core；PhysX 5 侧 TGS 为推荐求解器但属场景级不可变属性，GPU pipeline 有明确 CPU 降级清单与固定容量 buffer（超限丢对象）。
- Replicator 管线 = render product → annotator（渲染同源的像素级真值）→ writer（COCO/KITTI 落盘）→ orchestrator 驱动；Isaac Lab 是多后端 Python 框架（Newton/OVRTX/OVPhysX/Isaac Sim），Kit-less Newton 默认安装、render_interval 解耦物理/渲染步频——与"轻物理内核+重渲染可选"的本引擎取舍同向。

## 四、证据缺口 rollup（各报告 UNVERIFIED 清单，不进综合结论）

> 本节收录各报告 §5（UNVERIFIED 清单 / 未解决问题 + 证据缺口）的实际条目，仅作决策前待办与重查触发的索引；**这些主张不进 §二 的综合观察**。条数按报告原文收录；与工作流清单中 unverifiedCount 字段的 ±1 出入来自"来源矛盾/证据缺口是否计入"的口径差，以报告原文为准。

- **genie-4**（§5.1，9 条）：分辨率/帧率/延迟/模型规模；内部生成范式（是否逐帧自回归、token 化、记忆机制）；交互时长数值上限；Gemini API/AI Studio 端点形态与定价；模型级使用许可；导出能力；Gemini 3.8 Flash 回路验证准确率与延迟贡献；音频对话准确度数值；训练数据。另 §5.2 记录：博文 modified_time（2026-11-03）晚于事实卡记录的 2026-10-30（页面复抓时全部引语句仍验证存在，非矛盾）。
- **wan22-animate**（§5，7 条）：Relighting LoRA target 范围（论文↔开源代码矛盾，训练代码未开源）；训练细节未公开；命名与血统（Wan2.2 名义 vs Wan2.1 基座，官方无解释，推断）；段间漂移定量；face 4× 下采样对快速表情的影响；retarget 算法细节未逐行审读；IC-Light 合成光照与真实光照的域差距。
- **cosmos-3**（§5，6 条未闭）：HF 模型卡级显存数字（HF 域不可达）；Cosmos Datasets 逐数据集许可；Diffusers pipeline 文档细节；Sim 检查点 closed-loop 的 policy-in-the-loop 实效（官方自认未决）；"17-25x faster" 测度口径（部分）；MoT 双塔初始化消融（无一手来源）。#1 训练算力已闭（pdftotext 全文解析，事实卡 §7-3 已同步）。
- **vggt**（§5，6 条 UNVERIFIED + 4 条证据缺口）：置信度校准语义；camera head 迭代精化的论文↔代码差；原始训练实际损失超参；四元数符号连续性处理；RoPE 2D 论文缺失；轨迹头单轮位移上限。证据缺口：Table 9 为显存修复前数据且无官方新基准；单目重建未定量测试；released 训练栈 track 损失 NotImplementedError；VGGT-Omega 与本体关系未调研。
- **gvhmr**（§5，9 条）：训练 epoch 数两来源冲突（论文 500 vs 项目页 420）；推理显存 6 GB（社区自测）；Windows 跑通性；权重独立许可条款；遮挡/快速运动/多人失败边界（论文无 Limitations 章节）；SimpleVO vs DPVO 精度差距；`siga24_release.yaml` 引用 NetworkEncoderRoPEV2 的仓库内部不一致；FS 指标形式化定义原文（补充材料未获取）；TPAMI 期刊版差异。
- **sam-2**（§5，6 条）："无再识别机制"（综合推断，建议小实验验证）；逐模块延迟占比（论文附录 D.3 截断）；2.1 配置 MaskDownSampler per-layer 参数；obj_ptr_proj 更细内部结构；HF Transformers Sam2Model 与官方 predictor 行为一致性；每模型显存占用。证据缺口：完全无记忆的消融基线数字；训练用 internal video 不可得；2.1 相对 2.0 的逐 flag 变更说明（`fixed_no_obj_ptr` 两代均为 true，非 2.1 新增）。
- **openusd**（§5，4 条）：AOUSD Core Spec 1.0.1 §10.4 normative 正文未取得（WebFetch 截断 + raw 下载超时，LIVERPS 以 glossary 为准）；OpenExec 首入版本号（承事实卡 §7.1）；Hydra 2.0 各 filter 首入版本；NVIDIA Fabric 场景源接 Hydra 的具体接口形态。证据缺口：SIGGRAPH 讲义与 ACM 课程记录逐页对应未核验；OpenExec 文档抓取超时。
- **newton-mujoco-warp**（§5，8 条）：`DeterminismType.CONTACTS` 实现现状（main 分支 `_sort_contacts` 是 pass 空桩，文档承诺与实现不一致）；issue #562/#500 进展；XPBD 论文算法细节（PDF 无法解析）；Warp GPU_TO_GPU 档在 MJWarp 的暴露；事实卡项（Windows 官方支持、8 GiB 可行性、驱动版本、示例资产许可）；70x/100x 性能声称（宣传口径）；mjlab arXiv 号未抓取；MJWarp 夜间基准发布形态与数据。
- **rerun**（§5，7 条）：viewer 端 latest-at 缓存实现结构；Rerun Hub selective streaming 协议细节；re_renderer 渲染管线细节；MCAP/LeRobot importer 转换语义；0.42 changelog 未逐条核验；"lakehouse-style metadata layer" 博客级主张；超长时序回放性能（无官方数字、无本机实测）。
- **nsight-cupti**（§5，6 条）：CUPTI 追踪开销无官方量化数字；"Nsight Systems uses CUPTI…" 原句核验路径（HTML 截断、PDF 超限，按官方页面文本采信）；nsys 多源时钟对齐机制；`CUPTI_API_QUEUED` 相位是否在 CUDA 13+ 存在；external correlation 在 nsys SQLite 导出件的落表形态（需端到端实测）；跨线程 external correlation 的官方指引。
- **isaac-sim**（§5，5 条新增 + 承事实卡 6 条）：BasicWriter/CocoWriter/KittiWriter 在 6.0.1 的扩展归属；RTX Sensor SDK 独立技术文档；TGS 版本史官方口径（PhysX 5.1 引入 vs 社区"4.0 引入"记载）；multitick 渲染完整机制（传感器帧时间戳语义）；`isaacsim.core.experimental` tensor API 具体形状。另承事实卡 §7：8 GB 可行性、Isaac Lab 3.0×6.0.1 兼容性、NuRec 权重许可、驱动兼容、RT Cores、release 日期读法。证据缺口：本报告未运行任何 Isaac Sim 二进制（本机 8 GB 低于官方最低档），全部结论为文档/源码级核验。

## 五、结论定位

本文件是 **11 份 W04 深度报告的证据综述**：它汇总、分组并交叉引用各报告已核验的机制结论，不改变任何报告或事实卡的既有裁定；系列级观察（§二）只是跨报告的共性归纳，供 owner 做方向取舍时参考。**覆盖范围**：本系列全部位于被测系统/组件侧，评测基准/评测框架侧未深、可入环的开源候选未深（见 §一 覆盖范围声明）——做方向取舍时不应把本系列当作 W04 全景。**一切裁决（是否采信、是否立项、是否调整 World Contract 映射）归 owner**；证据缺口（§四）中的任何条目在闭环验证之前不得作为决策依据——与各报告 Trust label 的纪律一致。
