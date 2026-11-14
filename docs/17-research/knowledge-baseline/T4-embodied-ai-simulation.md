# T4 具身智能与物理仿真 — 知识基线（2026-11-13）

> Trust label: [Research → 知识基线] —— 为 owner 方向评审（grill → ADR-0016）提供 T4「具身智能与物理仿真（VLA/WAM/仿真器）」的原理与进度地基。**只调研不裁决**：本文不修改任何既有文档、不构成架构承诺；与 OmniWorld 的关联只写"关联性"，不写"我们应该做 X"。
> 方法：先通读仓库既有资产（newton/isaac 卡与深报、闭环路线 note、六方向整合 note、genie-4 卡），复用其已核验结论；外部新增事实经 web 搜索 / webfetch 一手核验（检索日期 2026-11-13），凡未一手核验的标 **未核** 或 **转述**。
> 数字纪律：媒体转述不得写成官方口径；每个 arXiv 编号均经搜索或 webfetch 确认存在（见 §7）。

---

## 1. 原理与技术路线分类（重点）

### 1.1 VLA：感知→动作的端到端策略

**定义。** VLA（Vision-Language-Action model）= 在预训练 VLM 基础上微调出的机器人策略：输入多路 RGB 图像 + 自然语言指令 + 本体感知状态（proprioception，关节角/夹爪位姿），输出机器人动作。核心动机是继承互联网级语义知识（物体常识、指令理解），弥补机器人演示数据的稀缺（π0 论文 §I，已核）。

**架构三段式（以 π0 为参照，已核全文）：**

1. **视觉编码器**：图像 patch 化后投影进语言 token 的 embedding 空间（late fusion VLM 配方）。SmolVLA 用 SigLIP 编码器 + pixel shuffle 把每帧压到 **64 个 visual token**（512×512 图从 1024 token 压缩而来），这是消费级推理成本的关键取舍（已核）。
2. **VLM/LLM backbone**：π0 用 PaliGemma 3B；SmolVLA 用 SmolVLM2（SigLIP + SmolLM2 decoder）；GO-1 用 InternVL2.5-2B（转述）。backbone 负责场景理解与指令遵循。
3. **动作头（action expert）**：独立权重的小 transformer，处理机器人专属的 state/action token。π0 的 action expert 为 **300M 参数**（从零初始化），总模型 3.3B；这一"双专家"设计类比 mixture-of-experts：图文 token 走 VLM 权重，动作 token 走 expert 权重（已核）。SmolVLA 的 expert 约 100M，交替 cross-attention（动作 token 看 VLM 特征）与 self-attention（动作 token 互看，保时序平滑）（已核）。

**动作表示的三条路线（原理分野的核心）：**

| 路线 | 机制 | 代表 | 优劣 |
|---|---|---|---|
| **离散自回归 token** | 动作分箱/FAST tokenizer 离散化，像文本一样 next-token prediction | RT-2、OpenVLA、π0-FAST | 与语言共享同一 loss、易吸收异构数据；但自回归解码慢、量化损失精度，高频灵巧控制吃力（π0 §II 已核） |
| **扩散策略（Diffusion Policy）** | 策略 = 动作空间上的条件去噪扩散过程；学 action-distribution 的 score 梯度，推理时迭代去噪 | Diffusion Policy（arXiv:2303.04137，已核） | 天然表达**多模态动作分布**（同一观测下左绕/右绕都对）、高维动作序列、训练稳定（绕开 EBM 的配分函数估计）；代价是多步去噪延迟 |
| **Flow matching** | 扩散的变体：学一个把高斯噪声输运到动作分布的向量场，线性-Gaussian 概率路径 A^τ = τA + (1−τ)ε，回归目标 u = A − ε | π0 / π0.5 / SmolVLA | 少步积分即可（π0 用 **10 步 forward Euler**，δ=0.1），支持 **50Hz** 高频动作块；π0 推理时缓存观测前缀的 KV、每步只重算 action token（已核） |

**Action chunking + receding horizon（两条路线共用的执行语义）。** 策略一次预测 H 步动作块（π0 H=50；GO-1 H=30，转述），只执行其中一段就重新观测重规划。Diffusion Policy 把这形式化为三个 horizon：观测 To / 预测 Tp / 执行 Ta，实验发现执行 horizon=8 步最优、对最多 4 步推理延迟鲁棒（已核）。这解释了为什么"动作块"既是精度手段（时序一致性、抗 idle action 过拟合）也是延迟手段（摊薄推理成本）。

**异步推理（SmolVLA 的工程贡献，已核）。** 同步模式 = 执行完一个 chunk 才停下来算下一个，机器人在推理期间"失明"。异步栈三机制：① early trigger——动作队列低于阈值（如 70%）就把最新观测发给 Policy Server；② 控制线程与推理线程解耦（可远程 GPU）；③ chunk fusion——相邻 chunk 的重叠动作按合并规则拼接防抖。官方口径：任务成功率相近（≈78%）但完成时间快 ~30%（9.7s vs 13.75s）、固定时间窗内完成量 2×（19 vs 9 个方块）。**关联性**：这正是闭环延迟预算（perceive→decide→act→observe）里"推理与执行重叠"的现成工业形状，也是"观察时滞→信念误差"（E1 实验）在真实策略侧的对应物。

**训练范式：pre-training / post-training 两段配方（π0，已核）。**
- 预训练吃"广而杂"：自采 7 种机器人构型、68 个任务、>10,000 小时演示 + OXE 开源集（22 种机器人）；开源数据占混合 9.1%；按 task-robot 组合样本数 n^0.43 加权抑制过采样；动作/状态向量零填充到最大维度（18 维）实现 cross-embodiment 统一。
- 后训练吃"精而专"：5–100+ 小时高质量任务数据。原理：**只训高质量数据学不会纠错**（错误在干净数据里不出现），只训低质量数据学不会流畅——两段合起来才有"尽量像专家 + 出错能恢复"。
- π0.5 把配方推到异构 co-training（摘要已核；细节转述自第三方精读，未核）：六类数据 MM（移动操作）/ME（多环境静态臂）/CE（跨本体实验室）/HL（人工子任务标注）/WD（网络多模态）/VI（口头指令"遥操作"）；预训练阶段动作走 FAST 离散 token 与语言共享 next-token loss（α=0），后训练激活 flow matching expert（α=10）；推理时同一模型先自回归预测语言子任务 ℓ̂ 再条件化生成低层动作。摘要级结论：端到端系统首次在全新真实住宅完成 10–15 分钟级长程灵巧家务（已核摘要）。

**潜在动作路线（GO-1 / ViLLA，转述自论文摘要转载与 GitHub README）。** 为吸收无动作标注的人类视频：① LAM（Latent Action Model）用逆动力学编码器 + 前向解码器从连续帧对 (I_t, I_{t+H}) 学离散潜在动作 token（VQ-VAE 量化，k=4）；② Latent Planner（VLM 骨干上 24 层 transformer）以 LAM 伪标签为监督预测潜在动作；③ 扩散 action expert 以潜在 token 为条件去噪出低层动作块。原理要点：**潜在动作空间比离散化低层动作空间小几个数量级**，是 web 视频→机器人策略的高效桥梁。与 §1.2 的 WAM 共享"从视频学动力学"的思想，但 GO-1 的潜在动作仍是策略的中间表征，不做显式未来画面预测。

### 1.2 WAM / MoT：联合预测未来状态 + 动作

**定义分野。** VLA 学的是条件分布 p(A_t | o_t)——观测到动作的映射；WAM（World-Action Model）联合建模 video 与 action 两个连续模态的分布，把"动作后果预测"纳入同一模型。生数官方对 Motus 的表述（官网技术页，已核）：通过 UniDiffuser 统一建模和调度 video/action，**一次训练即可推理五种模式：VLA、世界模型（动作条件化视频预测）、视频生成、逆动力学模型（IDM）、视频-动作联合预测**。

**MoT（Mixture-of-Transformers）三流架构（Motubrain 官网，已核）。** Text Stream / Video Stream / Action Stream 三条模态流，各自融合对应预训练基座（语言模型/视频生成模型/动作模型）的先验，跨流共享注意力。关键机制：
- **H-bridge attention**：只在 transformer 中间层做 video-action joint attention——保留跨模态交互、省掉全层联合注意力的开销；
- **relative EEF 统一动作表征**：动作 = 相对当前末端执行器状态的变化，跨本体可迁移，官方称 50–100 条同本体数据即可适配新机器人；
- **view-dependent RoPE offset**：token 级拼接任意数量相机视角，用 3D RoPE 视角偏移区分；
- **noisy condition**：训练时 50% 概率给条件帧加高斯噪声，提升部署鲁棒性；
- 后训练用 **Teacher Forcing 自回归 + Diffusion**，支持长程记忆与实时闭环；推理优化（DiT cache、FP8 量化、CUDA graph）达 ~5Hz（较 Motus 加速 ~10×），IDM/Video-to-Action 模式（推理时只更新 action 分支、不生成完整视频）进一步到 **11Hz**（均为官方自报口径，已核页面、未独立复测）。

**为什么联合建模改变数据经济学（官网口径，已核）。** VLA 只能吃"特定机器人 + 特定任务 + 完整动作标注"的轨迹；WAM 还能通过 video prediction 模式吃无动作标注视频、通过正/逆动力学模式吃无语言指令数据。官方主张多任务正向 scaling：任务越多共享世界知识越多、平均成功率越高（对比 VLA 的任务间互相干扰）——此为官方自报曲线，未独立核验。

**Motus2：单模型三接口的闭环自演化（官方研究页条目已核；摘要经智源社区转载，标注转述）。** 2026-09-11 官方研究页列出 "Motus2: A Self-Evolving General World Model for Dexterous Manipulation"。摘要核心：**单一共享权重模型暴露三类控制接口**——
1. **策略接口**（world-action model）：生成候选动作块；
2. **模拟器接口**（action-conditioned world model）：预测动作的视觉后果；
3. **评估器接口**（value model）：对预测结果打分。
三者耦合成"决策—学习"闭环：推理时 Best-of-N 规划（评估器挑动作），训练时候选打分转化为策略更新信号——即"**在自己的想象里排练、给自己打分、再改进**"。数据路径：大规模单目第一人称视频 → 同步双目第一人称 → 机器人轨迹 + 人机对齐数据；专家示范供动作学习，**失败/次优交互供动力学与价值学习**。扩展：全局自回归 + 混合记忆增强滑动窗口上下文；引入**触觉专家**做接触感知控制；部署于双目 + 双臂 + 双灵巧手 + 全手掌触觉阵列的仿生平台。媒体数字（5 项真机任务平均成功率 84%；放置手机/多指操作两任务 65%→75% 结合规划与基于模型的 RL）来自新智元报道转载——**转述，未核**，不作决策依据。Motus2 的 arXiv 编号本次检索未获得——**未核**，不引用编号。

**与 VLA 的原理对比小结。** VLA 把世界知识隐式压进策略权重；WAM 把"世界会怎么变"显式做成可查询的接口（模拟器/评估器），因此天然支持推理时规划与失败数据利用，代价是推理要生成/评估未来画面（Motubrain 用"只更新 action 分支"的 IDM 模式绕开）。**关联性**：WAM 的三接口与闭环路线"仿真器=外部真值权威 + Action Channel + 信念校准"在功能上同构——区别在 WAM 的"模拟器"是学习出来的神经近似（无真值保证、无位级复现），而物理仿真器是解析真值生产者；两者是同一闭环座位的两种实现体，可测量性截然不同。

### 1.3 世界模型 + 策略的"想象排练"

三个层次，保真度与可测量性递减/递增各异：

1. **潜空间想象（DreamerV3，arXiv:2301.04104，摘要已核）**：世界模型把感官输入编码为 categorical 潜表征，预测未来表征与奖励；actor-critic 策略**完全在想象轨迹上训练**。单一超参配置跨 150+ 任务，首次从零（无人类数据/课程）在 Minecraft 挖到钻石。这是"世界模型作为训练环境"的 RL 原型：想象便宜、可无限 rollout，但潜空间动力学误差会复利。
2. **像素级想象（Genie 4 + Gemini Robotics，仓库 genie-4 卡已核 2026-11-05）**：官方演示 "Gemini 4 Argon" 做多步任务分解、步骤以 text caption 流式注入 Genie 4 渲染，配合 Gemini Robotics 在生成世界里让 Apptronik Apollo 人形机器人下棋；模型页称 "an infinite playground for robotics simulations"。官方同时亲承六条限制：物体恒存性随时间漂移、多智能体交互难、连续交互仅"a few minutes"等；闭源、无权重、无公开 API、无世界状态导出。**"想象排练"在此=在生成世界里预演任务**，但生成世界自身不可复现、无真值账本。
3. **解析真值排练（物理仿真器）**：MuJoCo/Newton/Isaac Sim 提供的是按定义置信度为 1 的动力学真值（在"仿真器=外部真值权威"模型里），可逐 tick 镜像、可位级复现（CPU 侧有官方保证，GPU 侧有条件，见 §1.4）。**关联性**：闭环路线已裁定仿真器坐"真值权威生产者"位；生成式世界模型（D1 方向）坐"隐式生产者"位（输出=Proposal）——两者的评分空间不同（构造真值 vs 带噪观测，呼应 ADR-0013 与 E1 教训），不可混用同一基准。

### 1.4 物理仿真原理：积分器、接触求解、确定性、GPU 并行

本节机制性结论复用仓库两份深报（2026-11-05 一手核验：官方文档 + MJWarp main 分支源码快照 + 论文），此处只写原理骨架与引用，不重核。

**积分器（时间离散化）。** 仿真每步解运动方程：位置/速度状态 + 约束力 → 加速度 → 积分推进。MuJoCo 提供 Euler / semi-implicit（implicitfast）/ implicit-in-velocity / RK4 / discrete 等格式；MJWarp 的 `step` 按积分器分派 euler/rungekutta4/implicit/discrete（深报 §2.4 引源码）。Newton 的 SolverMuJoCo 默认改用 implicitfast（"for better stability on stiff systems"）。积分格式决定离散化语义与稳定性——**timestep 与积分器是复现实验的第一等元数据**（NVIDIA HF blog 的 parity 纪律：timestep 必须在 put_model 前统一，否则后续所有测量继承失配）。

**接触求解（约束力的数学定义）。** MuJoCo 把约束力定义为**凸优化问题的唯一全局解**：对偶问题是在可行域（等式约束无界、摩擦盒、接触锥）上的严格凸二次代价；Newton/CG 解 reduced primal，PGS 解 dual（MuJoCo 官方 computation 文档，深报已核）。PhysX 5 走另一族：PGS 与 TGS（Temporal Gauss-Seidel）迭代求解器，TGS 本质是子步化——"概念上等价于用更小 timestep 多次调用 PGS、每次只迭代一次"，换来关节精度与高质量比处理，代价是每迭代更慢；求解器类型是**场景级不可变属性**（Isaac 深报 §2.4 已核）。两族对比：凸优化全局解（MuJoCo）vs 迭代近似（PhysX）——前者语义干净、后者工程上对关节误差更宽容。

**确定性的来源与边界（CPU 侧）。** MuJoCo 官方声明："simulation pipeline is entirely deterministic and reproducible"——保存/重载状态再 step，下一状态逐位相同。但附三个官方警告（深报 §2.7a 已核）：① warmstart 加速度必须一并保存才能位级相等；② **接触事件具有高 Lyapunov 指数**——任何位级差异经接触积分都会指数放大（这是刚体仿真乃至真实物理的共性）；③ 精确复现**只在同一版本、同一架构内保证**。⇒ "确定性"不是无条件的物理属性，而是版本/架构/状态快照三元组下的工程承诺。

**GPU 并行为什么破坏位级确定。** 机制链条（深报 §2.7 已核，全部一手）：
- 根源：**浮点加法不满足结合律** + GPU atomic 求值顺序不确定。Warp 官方文档："CUDA is free to apply those updates in different orders… different orders can produce different rounded results"。MJWarp 主路径大量用 atomics（接触分槽 atomic_add、islands DSU 的 atomic_cas），故非边缘特性。
- 缓解：Warp 1.15 起 opt-in 确定性 codegen——累加型 atomic 重写为 sort-then-reduce，分槽型重写为 count-scan-write；两档保证：RUN_TO_RUN（同 GPU 架构内可复现）/ GPU_TO_GPU（跨架构，更保守更慢）。atomic_cas/tile atomics **不可重写**（DSU 恰用 cas）。
- MJWarp 的 `opt.deterministic` 位掩码：CONTACTS（接触稳定排序）/ CONSTRAINT（约束行规范序）/ ATOMICS（确定性浮点归约）/ ALL。**关键缺口：main 分支（2026-11-05 快照）`_sort_contacts` 是 pass 空桩**，与类型 docstring 承诺不一致；3.17.0 release tag 是否含实现未核。官方自带 determinism_test.py 做位级比对（RUN_TO_RUN 档有测试护栏），但 NVIDIA 博客明文限定："These are Warp capabilities, **not guarantees of… determinism for an entire MJWarp rollout**"。
- 附加数值差异：MJWarp 用 **float32**（MuJoCo CPU 是 float64），solver 参数与小摩擦值敏感；warmstart 语义两侧有已知偏差。

**吞吐 vs 延迟的显式分工。** MJWarp 官方："optimized for throughput… MuJoCo targets latency. A single MJWarp step is expected to be slower than MuJoCo's"。GPU 批量的收益来自 `(nworld, ·)` 前导维让数千独立世界共享编译产物并行推进 + CUDA Graph 把上百次 kernel launch 压成一次重放；代价是固定容量缓冲（nconmax/njmax 预分配，**溢出=未定义行为**，溢出 rollout 不得用于验证/benchmark）、单大连通机构 >~60 DoF 是官方自认弱项、首跑分钟级 kernel 编译。**关联性**：GPU 批量仿真的"world 索引隔离"与闭环 J2 判据（同 seed 逐字节复现 episode）正面相撞——CPU MuJoCo 有文档级保证，GPU MJWarp 默认不成立、opt-in 有条件成立；这是 spike 必须先实测的位（六方向 note D3 已把"同 seed 双跑位级比对"列为楔子）。

### 1.5 sim2real 与"仿真数据工厂"

**sim2real gap 的三个来源（原理综述，教科书级共识）：** ① 动力学差距（接触/摩擦/执行器模型 vs 真实物理，float32 与求解器近似加剧）；② 感知差距（渲染 vs 真实成像；MJWarp 内置渲染器官方自认"single hit raycaster… low fidelity"，无 GI/PBR）；③ 分布差距（仿真场景多样性不足）。主流对策：**domain randomization**（随机化质量/摩擦/光照/纹理，让真实世界落在训练分布内——MJWarp 的 per-world batched model fields 官方支持质量域随机化，深报已核）、system identification（用真实数据校准仿真参数）、以及 sim 内大规模 RL + 真机微调。

**GPU 端到端 RL 的范式起点（Isaac Gym，arXiv:2108.10470，仓库 2026-11-05 已核摘要）**："physics simulation and the neural network policy training reside on GPU"，物理缓冲直通 PyTorch tensor、零 CPU 瓶颈，宣称 2–3 个数量级加速（官方宣传口径，与本仓库工作负载无关）。Isaac Lab（arXiv:2511.04831，仓库已核）是其架构延续，且 3.0 起 **Newton-only 安装可完全绕开 Isaac Sim**（官方安装页："workstation memory requirements do not apply to a Newton-only installation"）——"轻物理内核 + 重渲染可选"的一手实例。

**合成数据工厂（Isaac Sim Replicator，仓库深报已核）。** 管线四段：Render Product → Annotator → Writer → Orchestrator。立身之本是**真值与渲染同源**：标注不是事后 CV 推理，而是渲染管线同源产出的几何/语义真值（2D/3D bbox、分割、深度 → COCO/KITTI 格式）；官方博客称 "time accurate and deterministic, the dataset can be created in repeatable way"、"ground truth… accurate at a pixel level"。**关联性**：这与 D 系列"真实帧 + 标注入环"同构但方向相反（Replicator 从场景生成标注，D 系列从标注构造场景）；其空白区恰是 World Contract 的领地——标注进环后的关联判定、置信度语义、逐 tick 账本（深报 §4 结论）。另一面：**构造真值 ≠ 真实观测**，把合成真值直接当真实基准会隐式更换评分空间（ADR-0013 / E1 的镜像问题）。

**世界模型作为数据工厂/训练环境。** 三种形态：① 离线帧生产者（Cosmos-Predict2-2B 官方模型卡 32.54GB 显存，本机不可跑 ⇒ 闭环 note 已裁定只走"别处出帧 + 清单/SHA256/许可记录"路线）；② 神经模拟器接口（Motus2 的 simulator 接口在模型内部预演动作后果，供 Best-of-N 与 MBRL——学习出来的动力学，无真值保证）；③ 生成式考场（WROP 用 Blender 生成器批量产带真值轨迹的认知考卷，六方向 note D2/D4 已核转述）。**关联性**：三种形态对应三个座位——离线数据生产者 / Agent Adapter 内部的想象组件 / 受契约管理的真值世界工厂；可测量性依次递增，与"仿真器=真值权威"的裁定相容。

### 1.6 闭环评测：任务完成率 vs 离线动作准确率

**为什么离线动作准确率不够（原理）。** 离线评测 = 在演示数据上量预测动作与专家动作的误差（open-loop MSE / token accuracy）。三个结构性缺陷：① 分布漂移——策略一执行就离开演示分布，误差复利，离线误差不预测闭环成败；② 多模态惩罚——专家向左绕、策略向右绕同样成功，离线误差却判错（Diffusion Policy 的多模态论证，已核）；③ 无任务语义——动作像不像 ≠ 任务成不成。因此业界评测重心转向**闭环任务完成率**：LIBERO / RoboTwin / RoboCasa 等仿真基准按 rollout 成功/部分成功计分（GO-1 用归一化完成分数、10 次 rollout 平均，转述；Motubrain 报 RoboTwin2.0 Clean 95.8 / Randomized 96.1，官方自报已核页面）。WorldFoundry（arXiv:2611.10771，六方向 note 已核）把具身评测重心放在闭环任务完成（LIBERO/RoboTwin）而非离线动作准确率，且其对比表自认"没有比较系统完全支持闭环"。

**闭环评测自身的测量缺陷（E1 教训，仓库已实测）。** 闭环 note §6 的时滞实验证明：观察陈旧让覆盖率掉 31%、per-producer ECE 恶化 47.8%，但 divergence 反降、identity_stability 反升——**三指标中两个把退化系统排得更好**；读真值的神谕模型还会稀释平均误差（71% 账本条目来自神谕）。定案三条：M1 误差与覆盖率成对发布、M2 空集≠稳定、M3 per-producer 分解。**关联性**：任何 T4 闭环评测（任务完成率之外）必须继承这组口径——完成率本身也可能被"环境随机性 + 异步推理 + 训练随机性"污染，这正是闭环 note §2.3 主张的三件"没有别人做好测"的事：位级相同 episode 的可复现测量、端到端延迟分段、信念校准与信用分配。

---

## 2. 进度快照（截至 2026-11）

### 2.1 模型侧

| 模型 | 机构/时间 | 规模与架构 | 关键数字（口径） | 开放性/许可 |
|---|---|---|---|---|
| **π0** | Physical Intelligence，2024-10（v4 2026-01，RSS 2025） | PaliGemma 3B + 300M flow matching action expert = 3.3B | >10,000h 自采数据、7 构型 68 任务 + OXE；H=50、10 步 Euler、50Hz（一手已核） | 权重经 openpi 发布（π0_base 等，转述自第三方精读）；训练数据私有 |
| **π0.5** | PI，2025-04 | π0 + FAST 离散/flow 连续双头 + 高层子任务预测 | 首次端到端在全新真实住宅做 10–15 分钟长程家务（一手摘要已核）；~100 家庭 400h、280k+80k 步、104 地点 scaling（转述，未核） | openpi 发布 pi05_base/pi05_libero/pi05_droid（转述）；co-training 混合数据未整体公开 |
| **SmolVLA** | Hugging Face，2025-06 | 450M：SmolVLM2 + ~100M flow matching expert；64 visual token、层跳过 L/2 | <30k episodes、487 社区数据集 ~10M 帧；SO100 社区预训练 51.7%→78.3%；异步快 30%、吞吐 2×（一手已核）；"消费级 GPU/CPU/MacBook 可跑"为官方宣称（**本机未实测**） | 代码/权重/数据全开放（LeRobot，Apache-2.0；数据逐个看许可，闭环 note 已核） |
| **GO-1 / AgiBot World** | 智元 + 港大 + 上海 AI Lab，2025-03 论文；2025-09-19 开源 | ViLLA：LAM + Latent Planner（InternVL2.5-2B）+ 扩散 action expert | 1,001,552 轨迹 / 2976.4h / 217 任务 / 100 台机器人；较 OXE 平均 +30%、较 RDT +32%、复杂任务 >60%（**转述**，摘要转载未直接抓取）；推理 ~7GB 显存、全量微调 ~70GB、仅 AE 微调 ~24GB（GitHub README 一手已核） | 仓库数据+代码 **CC BY-NC-SA 4.0**（README 一手已核）⇒ 非商业限制，与 Apache 红线无冲突但商用受限 |
| **Motubrain（Motus 系）** | 生数科技，Motus 2025-12 / Motubrain 2026-04 | 三流 MoT（text/video/action）+ UniDiffuser 统一 video-action；五种推理模式 | RoboTwin2.0 Clean 95.8 / Randomized 96.1 双第一、WorldArena 第一、~5Hz→IDM 模式 11Hz、50–100 条数据适配新本体（**官方自报**，页面已核、未独立复测） | 官网技术页公开；权重/代码开源状态本次未核 |
| **Motus2** | 生数科技，2026-09-10 发布（外滩大会，媒体口径） | 单共享权重模型三接口：策略/模拟器/评估器 + 触觉专家 + 混合记忆 | 5 任务平均 84%、两任务 65%→75%（规划+MBRL）——**媒体转述，未核**；>50 万段双目-触觉-动作轨迹（第三方解读，未核） | 官方研究页有条目（已核）；无开源代码声明（转述）；arXiv 编号未获得 |
| **Genie 4 + Gemini Robotics** | Google DeepMind，2026-10-27 | 生成式世界模型 + Gemini 4 Argon 任务分解在环 | Apollo 人形机器人下棋演示（官方，仓库卡已核）；官方无分辨率/帧率/时长数值，仅 "a few minutes"；物体恒存漂移为官方亲承限制 | **闭源、无权重、无公开 API**（trusted testers + waitlist） |
| **DreamerV3** | Hafner et al.，2023-01（v2 2024-04） | 潜空间世界模型 + 想象 actor-critic | 单配置跨 150+ 任务；Minecraft 从零挖钻石（一手摘要已核） | 官方代码开源（本次未逐页核 LICENSE） |

### 2.2 仿真器侧（版本/许可，复用仓库 2026-11-05 已核事实卡）

| 仿真器 | 版本（核验时点） | 许可 | 硬件门槛 | 确定性 |
|---|---|---|---|---|
| **MuJoCo 本体** | 3.x（C/C++，可嵌入） | Apache-2.0（LICENSE 原文已核） | CPU 即可，本机可行 | 官方文档级位级保证（同版本同架构 + warmstart 保存） |
| **MuJoCo-Warp** | 3.17.0（2026-10-22） | Apache-2.0 | NVIDIA GPU 必需（CPU 仅调试）；显存无官方数字；Windows 支持未官方声明 | GPU 默认非确定；opt-in 位掩码，CONTACTS 位 main 空桩 |
| **Newton** | 1.7.0（2026-10-14）+ 1.8.0rc1 | Apache-2.0（文档 CC-BY-4.0） | Warp 底座：R580+ 驱动、Turing+ GPU；官方支持 Windows x86-64 | 继承 MJWarp；可微性在 Warp 层存在、MJWarp 未兑现（issue #500） |
| **Isaac Sim** | 6.0.1 GA（2026-06-22）；6.2.0 为 Isaac Lab 锚定版 | 核心 Apache-2.0；**Kit SDK/资产/WebRTC 仍 NVIDIA 专有** | 最低 RTX 4080 / **16GB VRAM** / 32GB RAM（官方逐字已核）⇒ 本机 8GB 低于门槛一半 | PhysX GPU pipeline 有静默降级/丢对象清单；多速率时钟域 |
| **Isaac Lab** | 3.0（multi-backend） | BSD-3-Clause（部分 Apache-2.0） | **Newton-only 安装免除 Isaac Sim 显存要求**（官方安装页已核） | 取决于所选物理后端 |

---

## 3. 关键文献精读（3 篇）

### 3.1 π0: A Vision-Language-Action Flow Model for General Robot Control（arXiv:2410.24164，已核全文节选）

- **问题**：通用机器人策略面临数据稀缺、泛化、鲁棒性三重瓶颈；先前自回归 VLA（OpenVLA 等）无法支撑高频灵巧控制。
- **方法**：VLM（PaliGemma 3B）+ 独立 300M action expert；conditional flow matching 学动作块分布（线性-Gaussian 路径、beta 分布采样偏噪声端）；Transfusion 式双目标（离散 token 交叉熵 + 连续 token flow loss）；cross-embodiment 零填充统一 18 维；pre-training（广杂）/post-training（精专）两段配方；高层 VLM 策略输出语言子任务辅助长程任务。
- **关键结果**：>10,000h 数据（当时最大规模机器人学习实验）；叠衣服/清理桌面/装盒等 5–20 分钟长程任务；50Hz 控制；推理 10 步 Euler + 前缀 KV 缓存。对比 OpenVLA/Octo/π0-small（470M 无 VLM 初始化）验证 VLM 预训练与 flow matching 的贡献。
- **局限**：训练数据私有（自采部分）；评测为实验室自评；绝对成功率以图呈现、无统一表（第三方精读指出，转述）；50Hz 依赖 chunking + 缓存的工程组合，非模型单步延迟。
- **与 OmniWorld 的关联**：① action expert 的"独立权重 + 唯一动作出口"与 Action Channel"动作是出向意图、不进信念层"形状同构；② 其评测全部是真机 rollout 成功率——位级 episode 复现、延迟分段、置信度校准三件均缺，正是闭环 note §2.3 的测量空位；③ 动作块 H=50 的执行语义（执行一段即重规划）是闭环 tick 映射必须显式声明的边界。

### 3.2 SmolVLA: A Vision-Language-Action Model for Affordable and Efficient Robotics（arXiv:2506.01844，已核全文节选 + 官方博客）

- **问题**：VLA 动辄数十亿参数、依赖私有数据与昂贵硬件，社区无法复现/参与；社区采集的廉价机械臂数据（LeRobot 生态）被忽视。
- **方法**：450M = SmolVLM2 骨干 + ~100M flow matching expert；三个效率设计——64 visual token（pixel shuffle、弃 tiling）、VLM 层跳过（只用前 L/2 层特征）、expert 内 cross/self-attention 交替；<30k episodes 纯社区数据（487 个 SO100 数据集、~10M 帧、30FPS 标准化、Qwen2.5-VL-3B 重写任务标注）；异步推理栈（early trigger + 解耦线程 + chunk fusion）。
- **关键结果**：仿真（LIBERO/Meta-World）与真机（SO100/SO101）匹配或超过 10× 大的 VLA；社区预训练使 SO100 成功率 51.7%→78.3%（+26.6pt）；异步模式成功率不变但完成快 30%、固定窗吞吐 2×。
- **局限**：官方"CPU/MacBook/消费级 GPU 可跑"是宣称口径，**本机 RTX 4060 8GB 未实测**；社区数据域窄（SO100 单臂为主），跨本体泛化证据有限；论文 v1 无后续修订（本次检索所见）。
- **与 OmniWorld 的关联**：① 它是 Phase 4 闭环"第一个真实策略消费者"的最可行候选（450M + Apache-2.0 生态 + 官方消费级宣称），但 8GB 可行性是待核验清单第一位；② 异步推理栈 = 闭环延迟预算中 decide 段与 act 段重叠的现成参照，其"观察陈旧度"正是 E1 时滞实验的策略侧版本；③ 全开放训练配方（数据清单/过滤工具/标注 prompt）是"可复现测量"纪律在模型侧的罕见正面样本。

### 3.3 MuJoCo/MJWarp 确定性文档链（MuJoCo computation 文档 + Warp deterministic_execution 文档 + MJWarp 源码/FAQ；经仓库深报 2026-11-05 一手核验，本次复用不重核）

- **问题**：GPU 批量仿真能否支撑"同 seed 逐字节可复现 episode"（闭环判据 J2）？
- **方法（证据链）**：MuJoCo CPU 官方确定性声明 + 三警告（warmstart/Lyapunov/版本架构边界）→ Warp 文档的 atomic 浮点非结合律机制 → Warp 1.15 确定性 codegen（sort-then-reduce / count-scan-write，RUN_TO_RUN / GPU_TO_GPU 两档）→ MJWarp `DeterminismType` 位掩码源码（types.py:308–318）与消费点 → 官方 determinism_test.py 位级比对协议（3 步 × 2 次、20+ 物理字段）→ NVIDIA HF blog 限定句（"not guarantees… for an entire MJWarp rollout"）。
- **关键结果**：CPU 侧 J2 有文档级保证；GPU 侧默认不成立，opt-in ATOMICS+CONSTRAINT 位有测试护栏，**CONTACTS 位在 main 是 pass 空桩**（collision_driver.py:1154–1157，2026-11-05 快照）；atomic_cas（islands DSU）不可重写；float32 vs float64 是额外 parity 风险面。
- **局限**：深报只核了 main 分支，3.17.0 release tag 是否含 CONTACTS 实现未核；issue #562（GPU determinism）/#500（可微性）进展未抓取；GPU_TO_GPU 档在 MJWarp 的暴露未核。
- **与 OmniWorld 的关联**：这是 J2 判据的直接技术地基——"位级复现"在 CPU MuJoCo 后端是官方承诺、在 GPU 后端是待实测条件承诺；闭环 spike 的第一步（同 seed 双跑位级比对，默认 vs DeterminismType.ALL）由此确定。MJWarp 的 overflow bitmask / solver_niter 自报健康面可映射进契约⑨ tick 日志（只加字段不改语义）。

---

## 4. 开放问题

1. **VLA 与 WAM 的边界会不会消失？** π0.5 的"高层子任务预测"已在 VLA 内部引入显式中间表征；Motus2 把策略/模拟/评估收进单权重。两条路线在"统一序列模型 + 多专家"上趋同，但**可测量性分歧仍在**：WAM 的神经模拟器无真值保证，物理仿真器有。哪个成为闭环评测的主流被试，决定评测运行时的接口形状。
2. **想象排练的保真度如何验证？** Motus2 用自家评估器给自家想象打分（自证风险，与"策略读真值=闭环自证"同病）；Genie 4 的生成世界自身漂移。神经模拟器的预测误差 vs 解析仿真真值的偏差，目前无公认测量协议。
3. **GPU 确定性 vs 吞吐的取舍点在哪？** Warp 确定性模式更慢且容量受限；CONTACTS 位空桩何时补上（3.18+？issue #562？）；跨架构复现（GPU_TO_GPU）在 MJWarp 是否可配置——全部未核。
4. **闭环指标如何防骗？** E1 已证明单指标会反向；任务完成率本身也受环境随机性/异步推理/训练随机性污染。"完成率 + 覆盖率 + per-producer 校准 + 位级 episode"的成组口径尚无业界先例（WorldFoundry 对比表自认闭环无完全支持）。
5. **8GB 消费级硬件的真实边界？** SmolVLA 宣称未实测；GO-1 推理 ~7GB 贴近 8GB 上限（含 OS/渲染占用后是否可行未测）；MJWarp 显存无官方数字；Isaac Sim 16GB 墙明确。
6. **动作置信度的语义。** 策略自报"动作会成功"的概率 vs 状态主张"正确"的概率是两种 producer（闭环 note §3.2 已裁定不可混用）；现有 VLA/WAM 均无原生置信度输出，契约⑦的校准链在策略侧如何接，是 Phase 4 L1 的未答题。

---

## 5. 仓库已有资产引用

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| [projects/newton-mujoco-warp.md](../projects/newton-mujoco-warp.md) | Newton 1.7.0 / MJWarp 3.17.0 版本与许可（全链 Apache-2.0）；Python-only ⇒ 只能进程外集成；MJWarp 功能缺口（PGS/IMPLICITFAST/PLUGIN）；70x/100x 为宣传口径不可引用；Windows/8GB 显存 UNVERIFIED |
| [deep/2026-11-05-newton-mujoco-warp.md](../deep/2026-11-05-newton-mujoco-warp.md) | Warp→MJWarp→Newton 三层栈机制；(nworld,·) 批布局与固定缓冲溢出 UB；确定性完整证据链（§2.7）；World Contract 四维映射（§4）；J2 在 CPU/GPU 后端的分别结论 |
| [projects/isaac-sim.md](../projects/isaac-sim.md) | 6.0.1 硬件逐字（RTX 4080/16GB 最低）；许可新格局（核心 Apache-2.0、依赖层专有）；本机 8GB 差距表；Isaac Lab Newton-only 绕过显存墙的缝隙 |
| [deep/2026-11-05-isaac-sim.md](../deep/2026-11-05-isaac-sim.md) | Kit/Fabric 双层场景与多速率 ringbuffer；RTX 传感器即渲染器；PhysX TGS/PGS 与 GPU 降级清单；Replicator 真值同源管线与契约空白区（§4） |
| [2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) | 闭环四件 settled：仿真器=外部真值权威（方案 b）、Action Channel（出向意图、不占契约第十点）、BeliefView（Phase 4 第一切片）、E1 三条指标定案（M1/M2/M3）；J1–J4 判据；许可分档表 |
| [2026-11-13-six-direction-innovation-integration.md](../2026-11-13-six-direction-innovation-integration.md) | §1 D3：T4 座位（WAM/VLA=Agent Adapter 家族、仿真器=真值权威生产者）；Motubrain/Motus2 MoT 与三接口新证据；WorldFoundry 闭环缺口强化 R§2.3 主张 |
| [projects/genie-4.md](../projects/genie-4.md) | Genie 4 官方能力与六条限制（物体恒存漂移/a few minutes）；Gemini 4 Argon + Gemini Robotics 机器人演示；闭源无 API；数字纪律（媒体"2 分钟"不得写成官方口径） |
| [2026-10-26-stage-d-environment-and-licenses.md](../2026-10-26-stage-d-environment-and-licenses.md) | 本机实测：RTX 4060 Laptop 8.00 GiB / CUDA 12.6 / Windows 11（经上述卡引用） |

---

## 6. 待核验清单

| # | 项 | 现状 | 核验方式 |
|---|---|---|---|
| 1 | **SmolVLA 450M 消费级可跑宣称** | 官方博客口径（CPU/MacBook/消费级 GPU），本机未实测 | RTX 4060 8GB 上 `pip install lerobot[smolvla]` + 最小推理实测显存/延迟 |
| 2 | **MJWarp CONTACTS 确定性位空桩** | main 分支 2026-11-05 快照为 pass；3.17.0 tag 未比对；issue #562 未抓取 | 比对 release tag 源码；抓 issue；spike 同 seed 双跑位级比对（默认 vs ALL） |
| 3 | **RTX 4060 8GB 约束下的仿真器可行性** | MJWarp 显存无官方数字、Windows 支持未官方声明；本机驱动是否 ≥R580 未查 | `runtime_probe.exe` + 最小 box/humanoid 场景 batch×显存实测 |
| 4 | **GO-1 推理 ~7GB 在 8GB 卡的真实余量** | README 官方数字（示例卡 RTX 4090），本机含 OS 占用后未测 | 实测或查社区复现报告 |
| 5 | **Motus2 成功率数字（84% / 65→75%）** | 媒体转述（新智元），官方一手未核 | 抓生数官网 Motus2 页/论文原文 |
| 6 | **Motus2 arXiv 编号** | 本次检索未获得，不引用 | 官方研究页链接跟进 |
| 7 | **GO-1 论文细节数字（+30%/+32%/>60%/1,001,552 轨迹）** | 摘要转载与第三方精读，论文原文未直接抓取 | webfetch arXiv:2503.06669 全文 |
| 8 | **π0.5 训练细节（α 调度/280k+80k 步/六类数据比例）** | 第三方精读笔记，论文全文未核 | webfetch arXiv:2504.16054 全文 |
| 9 | **Motubrain RoboTwin2.0 95.8/96.1 与 11Hz** | 官方页面已核但属自报，无独立复测；权重开源状态未核 | 查 RoboTwin 官方榜单与生数仓库 |
| 10 | **Genie 4 性能数字** | 官方无分辨率/帧率/时长数值；任何第三方数字无官方依据 | 盯官方技术报告/API 文档 |
| 11 | **openpi 权重许可逐模型条款** | 转述（第三方精读称已发布），LICENSE 未逐页核 | 抓 Physical-Intelligence/openpi LICENSE 与 HF 模型卡 |

---

## 7. 来源与核验状态

检索日期统一 **2026-11-13**（仓库内引用另注）。

| 来源 URL | 关键内容 | 状态 |
|---|---|---|
| https://arxiv.org/abs/2410.24164 （+ /html/2410.24164 全文节选） | π0：3.3B（PaliGemma 3B+300M expert）、flow matching 公式、H=50、10 步 Euler、50Hz、>10,000h、7 构型 68 任务、n^0.43 加权、v4 2026-01-08、RSS 2025 | **已核**（本次搜索返回 abs 页 + 全文大段） |
| https://arxiv.org/abs/2504.16054 | π0.5 摘要：异构 co-training、首次全新住宅长程灵巧操作；v1 2025-04-22 | **已核**（本次 webfetch abs 页）；训练细节数字为**转述**（第三方精读） |
| https://arxiv.org/abs/2506.01844 （+ ar5iv/html 全文节选） | SmolVLA：450M、SmolVLM2、64 token、层跳过、flow matching expert、<30k episodes、异步栈 | **已核**（本次搜索返回全文大段） |
| https://github.com/huggingface/blog/blob/5dcbf81aef28cd025c940459b0da0a55bb42d0f0/smolvla.md | SmolVLA 官方博客：487 数据集 ~10M 帧、51.7→78.3%、异步 30%/2×、消费级宣称、expert ~100M | **已核**（HF 官方博客仓库原文，本次搜索返回全文） |
| https://github.com/OpenDriveLab/AgiBot-World/blob/main/README.md | GO-1 开源（2025-09-19）、推理 ~7GB/全量 FT ~70GB/AE FT ~24GB、CC BY-NC-SA 4.0、arXiv:2503.06669 引用、GO-1 Air 变体 | **已核**（官方仓库 README，本次搜索返回全文） |
| https://arxiv.org/abs/2503.06669 | GO-1/AgiBot World 论文（1M+ 轨迹、217 任务、+30%/+32%/>60%、ViLLA 三阶段） | **转述/未核**（编号经 README 引用确认存在；数字来自摘要转载与知乎/CSDN 精读，原文未直接抓取） |
| https://arxiv.org/abs/2303.04137 （+ /html/2303.04137v4 全文节选） | Diffusion Policy：条件 DDPM、score 梯度、To/Tp/Ta、46.9% 平均提升（12 任务 4 基准）、DDIM 100/10、0.1s@3080、horizon=8、抗 4 步延迟 | **已核**（本次搜索返回 abs + 全文大段） |
| https://arxiv.org/abs/2301.04104 | DreamerV3：潜空间想象训练、单配置 150+ 任务、Minecraft 钻石；v2 2024-04-17 | **已核**（本次搜索返回 abs 页摘要） |
| https://www.shengshu.com/zh/motubrain/ | Motubrain 官方技术页：三流 MoT、UniDiffuser 五模式、relative EEF、H-bridge、noisy condition 50%、Teacher Forcing AR+Diffusion、5Hz/11Hz、RoboTwin2.0 95.8/96.1、WorldArena 第一、50–100 条适配 | **已核**（官方页面，本次搜索返回全文；基准数字为官方自报，未独立复测） |
| https://www.shengshu.com/zh/research/ | 官方研究页条目：Motus2（2026-09-11）、Motubrain（2026-04-29）、Motus（2025-12-15）、Vidu S1（2026-07-03） | **已核**（官方页面，本次搜索返回） |
| https://hub.baai.ac.cn/paper/e3d21ea0-37f2-4147-bc82-8d7fab1cb8f2 | Motus2 摘要转载：单权重三接口（策略/模拟器/评估器）、闭环自演化、单目→双目→机器人数据、触觉、仿生平台；作者含 Jun Zhu | **转述**（智源社区转载论文摘要，官方原文未直接抓取） |
| https://www.sina.cn/news/detail/5342348076123925.html ；https://c.m.163.com/news/a/L6N1SU4605527PMU.html | Motus2 发布（2026-09-10 外滩大会）、84% 平均成功率、65→75%（Best-of-N + MBRL） | **转述/未核**（新智元/IT时代网媒体口径，不作决策依据） |
| https://deepmind.google/blog/genie-4-a-new-generation-of-world-simulation/ ；https://deepmind.google/models/genie/ | Genie 4 官方能力/六条限制/Gemini 4 Argon + Gemini Robotics Apollo 演示 | **已核**（经仓库 genie-4 卡，2026-11-05 一手核验，本次复用不重核） |
| https://mujoco.readthedocs.io/en/stable/computation/index.html ；https://mujoco.readthedocs.io/en/latest/mjwarp ；https://nvidia.github.io/warp/stable/user_guide/execution_and_performance/deterministic_execution.html | MuJoCo 确定性声明与三警告；MJWarp FAQ/位掩码/吞吐定位；Warp atomic 机制与确定性两档 | **已核**（经仓库 newton 深报，2026-11-05 一手核验，本次复用不重核） |
| https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp | NVIDIA 官方："not guarantees… for an entire MJWarp rollout"；溢出 rollout 作废纪律；parity 纪律 | **已核**（经仓库 newton 深报，2026-11-05，复用） |
| https://docs.isaacsim.omniverse.nvidia.com/6.0.1/installation/requirements.html ；https://isaac-sim.github.io/IsaacLab/main/source/isaaclab/installation.html | Isaac Sim 最低 RTX 4080/16GB；Isaac Lab Newton-only 免除显存要求 | **已核**（经仓库 isaac 卡，2026-11-05，复用） |
| https://arxiv.org/abs/2108.10470 ；https://arxiv.org/abs/2511.04831 | Isaac Gym GPU 端到端 RL（2–3 个数量级，宣传口径）；Isaac Lab 技术报告 | **已核**（经仓库 isaac 深报，2026-11-05 摘要核验，复用） |
| https://arxiv.org/abs/2611.10771 | WorldFoundry：270 模型/74 基准；对比表自认闭环无完全支持；具身评测重心=闭环任务完成 | **已核**（经六方向 note，2026-11-13 检索全文，复用） |
| https://huggingface.co/nvidia/Cosmos-Predict2-2B-Video2World | Cosmos-Predict2-2B 需 32.54GB 显存（模型卡） | **已核**（经闭环 note，2026-10-27，复用） |

**编号存在性声明**：本文引用的全部 arXiv 编号（2410.24164 / 2504.16054 / 2506.01844 / 2503.06669 / 2303.04137 / 2301.04104 / 2108.10470 / 2511.04831 / 2611.10771）均经本次搜索/webfetch 或仓库既有核验记录确认存在；Motus2 论文编号未获得，全文未引用任何未确认编号。