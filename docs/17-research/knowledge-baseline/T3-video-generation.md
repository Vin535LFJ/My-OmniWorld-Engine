# T3 视频生成模型 — 知识基线（2026-11-13）

> Trust label: [Research → 知识基线]——为 owner 方向评审（grill → ADR-0016）提供"视频生成这门技术到底怎么工作、走到哪了"的系统性地基。**只调研不裁决**：本文不做方向选择，与 OmniWorld 的关联只写"关联性"，不写"应该做 X"。
> 核验口径：每条事实标注 一手（论文原文/官方 README/官方摘要页，本次检索）/ 仓库已核（复用既有核验）/ 转述（媒体/百科/第三方笔记）/ 未核。媒体榜单与商业数字一律不写成官方口径。检索日期：2026-11-13。
> 去重纪律：仓库既有结论只链接不复述（[wan22-animate 卡](../projects/wan22-animate.md)、[Wan-Animate 深报](../deep/2026-11-05-wan22-animate.md)、[兴趣全景](../2026-11-13-interest-landscape-and-redefinition.md)、[六方向整合](../2026-11-13-six-direction-innovation-integration.md)）。

---

## 1. 原理与技术路线分类（重点）

### 1.1 总图景：一条 latent 扩散流水线 + 三种骨干范式

现代视频生成的主流形态是 **latent video diffusion**：不在像素空间直接生成，而是四步流水线——

```
文本/图像/音频条件 ──编码器──┐
                            ▼
随机噪声 latent ──▶ 去噪骨干（DiT/U-ViT/AR）×N 步 ──▶ 干净 latent ──3D VAE 解码──▶ 视频帧
```

- **3D causal VAE** 负责时空压缩。以 Wan-VAE 为例（仓库深报已逐行核验源码）：视频 `(1+T)×H×W×3` 压到 `(1+T/4)×H/8×W/8×16`，时间 4×、空间 8×8；首帧只做空间压缩（"1+T"结构，兼容图像数据与时间因果性）；feature cache 支持任意长视频分块编解码。[仓库已核：深报 §2.1(a)]
- **去噪骨干**在 latent patch 序列上工作。DiT（Diffusion Transformer，Peebles & Xie, arXiv:2212.09748，仓库深报已核摘要）用纯 transformer 替换 U-Net，scaling 越大质量越好——这是 Sora 之后全行业收敛到 DiT 的根据。
- **训练目标**从 DDPM 的 ε-prediction 演进到 **flow matching / rectified flow**：线性插值 `x_t = t·x_1 + (1−t)·x_0`，模型学速度场 `v = x_1 − x_0`，MSE 损失。直线路径意味着大步长采样只有数值积分误差、无 DDPM 式指数误差放大——这是后来"50 步蒸馏到 4 步甚至 1 步"在数学上可行的前提。[仓库已核：深报 §2.1(a) 引 Wan 报告式(1)–(3)；蒸馏适配性分析另见转述来源 §7-10]
- **条件注入**：文本经 T5/umT5 编码后由每层 cross-attention 注入；时间步经共享 adaLN MLP 调制；图像条件走 latent 通道拼接 + CLIP 全局特征（Wan-I2V 三元组：noise latent 16ch + conditional latent 16ch + mask 4ch = 36ch）。[仓库已核：深报 §2.1(b)]

**三种骨干范式**（转述口径：百度百科 Vidu AI 词条把 U-ViT / DiT / 自回归并列为三大范式；机制描述本身与一手论文一致）：

| 范式 | 代表 | 机制要点 | 代价 |
|---|---|---|---|
| **DiT（全序列双向扩散）** | Sora 系、Wan、HunyuanVideo、多数开源模型 | 整段视频所有帧 token 一次性去噪，full spatio-temporal attention，未来帧可影响过去帧 | 生成单帧需处理全序列 ⇒ 延迟分钟级、无法流式；算力随帧数二次增长 |
| **U-ViT（DiT 变体）** | Vidu（生数/清华朱军团队，2022 年提出） | Transformer 骨干但保留 U-Net 式长跳跃连接，融合浅层细节与深层语义 | 与 DiT 同属双向扩散，流式限制相同 |
| **自回归（AR）** | 离散 token 路线（VideoPoet 类）；AR 扩散路线（CausVid/Self-Forcing/MAGI-1）；Sand.ai（转述） | 逐帧/逐 chunk 生成，`p(x^1:N)=∏p(x^i|x^<i)`，因果注意力 + KV cache | 离散 token 每帧数千 token 太贵；AR 扩散有 exposure bias（§1.4） |

**混合形态**是 2025–2026 的主线：双向扩散教师 → 因果少步学生（§1.6）；块内双向 + 块间因果（chunk-wise AR）；以及"语言模型当导演 + 生成模型当渲染器"的系统级混合（Genie 4 官方架构，仓库已核）。

### 1.2 DiT 内部的时空一致性从何而来

以 Wan2.1/2.2 为参照系（仓库深报已源码级核验，此处只提机制骨架）：

1. **Patchify**：kernel/stride (1,2,2) 的 Conv3d 把 latent 变 token 序列，`L=(1+T/4)×H/16×W/16`。
2. **Full spatio-temporal self-attention**：每个 token 能看到同帧所有空间 token 与所有帧的时间 token（RoPE 三轴分频编码位置）。**帧间一致性不是显式约束，而是全注意力 + 海量视频数据学出来的统计先验**——模型见过"同一只猫在连续帧里长什么样"，于是去噪时自然维持它。
3. **共享 adaLN**：时间嵌入经共享 MLP 预测 6 个调制参数、每块只学 bias，省约 25% 参数。
4. **CFG（classifier-free guidance）**：有条件/无条件双前向放大条件影响；Animate 默认关（guide_scale=1.0），Wan T2V 默认开。

**Wan2.2 的两项架构增量**（本次检索官方 README 中文版，一手）：

- **按去噪时间步路由的 MoE**：A14B = 高噪声专家（早期步，管整体布局）+ 低噪声专家（后期步，管细节纹理），各约 14B、总 27B、每步只激活 14B ⇒ 推理算力/显存几乎不变而容量翻倍。切换点由信噪比（SNR）决定：`t_moe` 对应 `SNR_min` 的一半，`t < t_moe` 切低噪声专家。官方以验证损失曲线消融证明 MoE 优于四种 dense/单专家复用配置。**注意这不是 LLM 式 token 路由 MoE，而是"去噪阶段分工"**——扩散过程天然分"定结构"与"抠细节"两个阶段，各配一个专家。
- **高压缩 VAE（TI2V-5B）**：Wan2.2-VAE 压缩比 4×16×16（整体 64×），加 patchify 后总压缩 4×32×32 ⇒ 5B dense 模型可在单张消费级 GPU（≥24GB）9 分钟内生成 5 秒 720P@24fps。**压缩比是显存/速度的一等杠杆**：token 数决定 attention 成本，VAE 压得越狠 DiT 越便宜，代价是重建保真度上限。

### 1.3 长视频为什么会漂移：三种拼接策略与共同的病根

训练窗口有限（典型 5 秒 / 81 帧），长视频只能外推，三条路线：

| 策略 | 机制 | 漂移来源 |
|---|---|---|
| **滑窗分段 + 条件帧衔接**（Animate、多数 I2V 延长） | 每段 77 帧，段间只传上段结尾 1 或 5 帧作 temporal latents | 段间信息带宽仅 1–5 帧；跨段外观/光照漂移无模型内修正机制（仓库深报 §2.2 已论证，定量数据官方未公布） |
| **滑窗 AR + KV cache**（CausVid） | 因果注意力，窗口外上下文直接丢弃 | CausVid 附录 E 官方承认：10 秒窗口截断导致"先前见过的物体/环境长时间后重新出现时视觉不一致"——**物体恒存性失效的机制性根源之一**（一手） |
| **rolling KV cache**（Self-Forcing） | 固定大小缓存只留最近 L 帧 KV，复杂度从 O(TL²) 降到 O(TL) | 缓存淘汰 = 上下文遗忘；且训练时模型总能看到首帧、推理滚动后首帧被淘汰 ⇒ 分布错配闪烁。Self-Forcing 的修复是训练时限制局部 attention 窗口模拟"看不到起始帧"（一手摘要 + 转述细节） |

**共同病根有两层**：

1. **误差累积（exposure bias）**：AR 推理时第 i 帧条件在模型自己生成的、带误差的前缀上；训练若用 ground-truth 前缀（Teacher Forcing）或加噪真实前缀（Diffusion Forcing），模型从未见过自己的错误 ⇒ 小误差逐帧放大，表现为饱和度漂移、内容崩坏（CausVid 论文 Fig.7 定性展示，一手）。
2. **无状态核**：即使误差不累积，模型也没有"世界在镜头外继续演化"的机制——上下文里没有的信息就不存在。WRBench（arXiv:2606.20545，仓库已核）的实证：镜头转回时目标停在被抛弃时的状态，**事件在无观察期间不推进**。这不是视频质量 bug，而是"像素分布模型 ≠ 状态动力系统"的范式级缺口（详见 §1.8）。

Vidu Q3 宣称的解法是**扩大单次生成窗口**：全局注意力一次处理完整 16 秒序列，"误差不会随时间累积"（转述：百度百科引官方发布材料）。机制上成立——窗口内确实无拼接漂移——但只是把漂移推迟到 16 秒边界，跨窗口/跨镜头一致性仍靠参考图机制（Reference-to-Video）外部锚定。

### 1.4 声画同步（audio-visual sync）原理

三条技术路线，同步质量由"共享多少底层表示"决定：

1. **后期配音（added audio）**：先生成无声视频，再叠 TTS/音效/音乐。口型对齐靠后处理 lip-sync 模型。绝大多数 2025 年前模型如此；2026 年仍有大量模型 silent by design（转述：Vivideo 2026-06 盘点）。
2. **音频驱动视频（audio-conditioned）**：音频作为条件输入，视频生成跟随音频。代表 Wan2.2-S2V-14B（官方 README：音频编码器 + 参考图 + 可选姿态视频，视频长度随音频时长自动调整，可接 CosyVoice TTS；一手/仓库已核）。同步是"条件跟随"，音频本身不是生成的。
3. **声画联合生成（native audio / joint denoising）**：音频与视频在**同一生成过程**中产出。Vidu Q3 的转述机制：多模态统一扩散模型，在 U-ViT 的同一噪声空间内联合训练视觉/音频/文本三域，"一个噪声-同时去噪"，画面帧、对白波形、环境音轨同步生成而非拼接；音频分支称采用"3D VAST 式"合成，先预测嘴型系数再反向生成带空间方位感的对白（**转述，未核**——百科词条口径，未见论文）。Sora 2 / Veo 3.1 / Kling 2.6+ / LTX-2 / Wan2.5 同属此路线（转述：多方媒体互证，官方技术细节均未公开）。

**为什么联合生成同步更好**（机制推断，与转述材料一致）：音画共享同一场景潜表示 ⇒ 关门声与门的视觉重量、口型与音素由同一去噪过程协调，而非两个独立模型事后对齐。这把"生成→配音→lip-sync"三步管线坍缩成一步。**边界**：联合生成不保证物理正确的声学（混响/遮挡/多普勒），媒体实测仍见音画错位（转述：Artflo 对 Sora 2 物理测试中"音频用旁白式配乐而非真实碰撞声"）。

### 1.5 运镜控制（camera control）

- **Prompt 级**：自然语言描述运镜（"slow dolly-in / whip pan"），三大闭源 API 均如此（转述）。可表达但不可复现——同一 prompt 不保证同一相机轨迹。
- **条件向量级**：Vidu Q3 称把"远景-中景-特写"机位标签编码为条件向量注入 cross-attention，去噪时动态决定下一帧机位，实现单镜头内自动切镜（转述，未核）。
- **显式轨迹级**：Wan 生态的 ATI（官方 README Community Works 列名：统一物体/局部/相机运动的轨迹控制框架）、Wan-Move（NeurIPS 2025，点级运动控制）——把相机/物体轨迹作为空间对齐条件注入，与 Animate 的骨架加性注入同构（README 列名为一手，机制细节未逐篇核验）。
- **3D 导演台**：Vidu Q4 Preview 配套 RunningHub 提供"3D 导演台"人物调节（转述）——显式 3D 控制面 + 生成式表现，是"混合"路线在产品层的又一实例。

**对状态运行时的含义（关联性，非裁决）**：prompt 级运镜 = 不可复现的控制面；轨迹级运镜 = 可版本化、可核验的控制面。相机轨迹若由外部显式持有（引擎侧），视频模型退化为"给定相机路径的条件渲染器"——这正是 Marionette/MASS 类混合架构的形状（仓库六方向 note §3 已核）。

### 1.6 实时化：蒸馏 × 因果化两条腿

**为什么原生模型不可能实时**：50 步去噪 × 全序列双向注意力 × 每步处理全部帧 ⇒ Wan2.1-1.3B 生成 5 秒视频需 103 秒首帧延迟（转述：Self-Forcing 论文对比表，经知乎笔记）。实时化必须同时砍三个因子：

**腿 1：步数蒸馏（50 步 → 4 步 → 2 步 → 1 步）**

- **DMD（Distribution Matching Distillation）**：最小化生成分布与数据分布的 reverse KL，梯度 ≈ 两个 score 函数之差（真实数据 score 冻结 + 生成器 score 在线训练）。分布级监督 ⇒ 允许教师/学生**架构不同**——这是"双向教师蒸馏因果学生"的理论基础（CausVid §3.2，一手）。
- **ODE 初始化**：先用教师的 ODE 轨迹对拟合学生（MSE 回归），再做 DMD 精调。CausVid 消融证明初始化质量决定 DMD 上界（一手）。
- **Consistency Models / rCM 路线**：把 ODE 轨迹上任意点直接映射到起点。Causal-rCM（arXiv:2606.25473，2026-06，本次检索摘要页一手）把 TF 连续时间 CM（sCM/MeanFlow，自研 custom-mask FlashAttention-2 JVP kernel，收敛快 10×）作为 Self-Forcing DMD 的初始化，**2 步因果 Wan2.1-1.3B 达 VBench-T2V 84.63**，并应用到 Cosmos 3 做动作条件交互世界模型——蒸馏配方直接通向世界模型实时化。
- 代价：reverse KL 蒸馏**降低输出多样性**（CausVid 附录 A 官方承认，一手）——mode-seeking 的数学本性。

**腿 2：因果化（双向注意力 → 因果注意力 + KV cache）**

- **CausVid**（arXiv:2412.07772，CVPR 2025，本次检索 CVF 全文一手）：非对称蒸馏——因果学生从拥有"未来特权信息"的双向教师学习。50 步→4 步，首帧延迟 219s→1.3s，单 GPU 9.4 FPS 流式；VBench-Long 84.27。零样本支持 I2V、流式 V2V、动态换 prompt。**官方承认的边界**：10 秒滑窗上下文截断致长程不一致；VAE 需攒 5 个 latent 帧才能出像素（延迟下限）；极长视频仍退化。
- **Self-Forcing**（arXiv:2506.08009，NeurIPS 2025 Spotlight，本次检索摘要页一手）：训练时就按推理方式自回归 rollout（KV caching），每帧条件在**自己生成的**前缀上，对完整视频施加视频级分布匹配损失（DMD/SiD/GAN）⇒ 从根源消除 exposure bias。few-step + 随机梯度截断控制训练成本；rolling KV cache 支持长视频外推。官方摘要：单 GPU 亚秒级延迟实时流式，质量匹敌甚至超过慢得多的非因果模型。基于 Wan2.1-T2V-1.3B；官方 GitHub 要求 ≥24GB（4090/A100/H100 实测）、Linux；chunk-wise 17 FPS / 首帧 0.69s、frame-wise 首帧 0.45s（转述：知乎论文笔记，与 README"单 4090 实时"一致）。
- **Causal Forcing**（arXiv:2602.02214，ICML 2026；**编号见于第三方论文笔记，原文未核**）：诊断 Self-Forcing 的病根为"帧级单射性"被双向教师 ODE 破坏（同一噪声帧对应多个干净帧 ⇒ MSE 坍缩成条件期望 ⇒ 画面糊），改用 TF 训练的 AR 教师做 ODE 初始化；同延迟下动态度 +19.3%、指令遵循 +16.7%（转述）。
- **评测纪律**："实时"必须同时报 throughput 与 first-frame latency（Self-Forcing 论文主张，转述）——高吞吐但首帧 103s 的系统不满足交互。

**实时化的硬件现实**（与仓库 D1 结论互证）：上述开源实时方案全部要 4090/H100 级；产品级实时（Vidu S1 540P/42FPS、Genie 4）全在云端。8GB 本机与"实时流式"无缘，但**离线闭环评测不要求实时**（仓库六方向 note D1 已核）。

### 1.7 视频生成 → 3D/4D 资产链路（Vidu4D 类重建）

视频扩散模型被观察到具有**强 3D 一致性**（同一物体跨帧像同一个 3D 实体）⇒ 可当作"廉价多视角先验"：文本→视频→4D 重建，绕开昂贵的多视角拍摄。Vidu4D（NeurIPS 2024，§3 精读）是此链路代表：

```
文本 ──视频扩散模型(Vidu)──▶ 单段生成视频 ──Vidu4D 重建──▶ 4D 表示（可自由视角渲染的时序 3D）
```

- 核心难点：生成视频有**非刚性漂移与帧间畸变**（模型脑补的"3D 一致性"是统计的、不精确），直接当多视角数据重建会崩。
- 解法：canonical 静态状态 + 时变 warping field（把每帧畸变"归位"到同一静态物体），Dynamic Gaussian Surfels 表示 + 法线一致性正则 + 旋转/缩放双分支精修抑制纹理闪烁。
- **边界（官方承认）**：依赖视频质量、大场景扩展性差、逐场景优化非实时。产物是"可渲染的 4D 资产"，不是"可交互的世界状态"——没有物理、没有语义实体、没有事件。

**链路定位（关联性）**：这是"隐式生成 → 显式资产"的转换器，落在四层架构 L2（构建）的输入侧；与 VGGT→gsplat（真实视频→3D，仓库已核）互为镜像——一个从真实数据重建、一个从生成数据重建，后者带幻觉但零拍摄成本。重建产物的身份/时间/所有权语义仍需外部契约补齐（surfel 没有 id，warping field 没有 tick）。

### 1.8 视频生成 vs 世界模型：像素真实 ≠ 状态真实

**判据不是画质，是反事实**（仓库兴趣全景 §1.4 已确立）："如果我改变条件，世界会怎样"——能否回答这个问题、且答案可测量，才是世界模型。

| 维度 | 视频生成模型（L4） | 世界模型（L3） |
|---|---|---|
| 优化目标 | 像素分布似然/人类偏好 | 状态转移正确性（物理、恒存、因果） |
| 时间语义 | 帧序列的统计连续 | 事件在无观察期间继续推进 |
| 干预响应 | prompt 换词≈重新生成 | 动作条件改变 → 同一世界演化 |
| 失效形态 | 漂移、闪烁、音画错位 | 物体出画再回来变了、胸像该倒不倒（Genie 4 官方自认，仓库已核） |
| 可测量性 | VBench/偏好榜（像素级） | WRBench/MemoBench/WROP（状态级，仓库已核） |

三条一手/已核证据支撑"边界真实存在"：

1. **CausVid 附录 E**：滑窗截断 ⇒ 重现物体不一致——即使像素质量 SOTA，恒存性仍无保障（一手）。
2. **WRBench**（23 模型 9600 视频）：《Current World Models Lack a Persistent State Core》——返回目标停在被抛弃状态（仓库已核）。
3. **MemoBench**：10 开源模型无一 ORS>0.6；无相机条件模型靠生成近静态视频刷一致性分 ⇒ 必须 motion-gated 评测（仓库已核）——**像素级一致性指标会骗人**，与引擎 E1"指标会骗人"教训同构。

**但边界在移动**：动作条件化（Causal-rCM × Cosmos 3）、显式状态联合预测（StatePlay，仓库 D1 已核）、记忆机制（Matrix-Game 3.x）都在把 L4 模型往 L3 推；混合架构（Gemini 导演 + Genie 渲染、Marionette 状态→几何→扩散外观）则从系统侧绕过边界。**收敛点**（仓库兴趣全景 §0 判断 3，已核）：显式管正确性、生成式管丰富度。

**对 OmniWorld 的关联性（只列关联，不裁决）**：

- **L4 表现后端候选**：离线视频生成是"世界状态→画面"的数字出口之一；Animate 的 pose/face/mask/bg 四路条件接口恰好消费运动数据层产物（仓库深报 §4 已论证与 World Contract 四轴逐轴同构：reference latent=identity 每段重新证明、mask=ownership 单写者、双时间轴=time 基准显式转换、输出无 confidence=评测环补齐）。
- **被测量的生产者**：视频/世界模型的漂移（恒存、身份、事件推进）正是引擎 grounding 指标的靶子；位级复现纪律与生成式后端的帧级不确定性天然冲突 ⇒ 只能进程外接入（仓库 wan22-animate 卡 §2/§6 已核）。
- **闭环评测**：Self-Forcing/CausVid 类因果模型是"可交互生产者"的开源标的，其 exposure bias 修复史（TF→DF→SF→Causal Forcing）说明**训练-推理分布对齐**是该领域的核心难题——与引擎"参考模式/性能模式"双模纪律关注的是同一类问题在不同层的投影。

---

## 2. 进度快照（截至 2026-11）

### 2.1 闭源产品层

| 模型 | 时间 | 关键数字/能力 | 口径 |
|---|---|---|---|
| **Vidu Q3**（生数） | 2026-01-30 发布 | 16 秒 1080P 声画同出（可后升 4K）、多镜头自动切换、多语言对白、像素级文字渲染、参考生视频（R2V）；Artificial Analysis 文生视频榜 1241 分，发布时中国第一/全球第二（次于 Grok-imagine-video），2026-02-14 称登顶第一 | **转述**（百度百科/量子位/甲子光年互证；AA 榜单原页未直接核验；"全球第一"必须注明是 AA 文生视频榜、时点 2026-02，非官方自封口径） |
| **Vidu Q3 Turbo/Pro** | 2026-06 上线华为云 MaaS | Turbo（T2V/I2V/H2V/R2V）速度优化；Pro 最高 4K | 转述（IT之家） |
| **Vidu Q4 Preview** | 2026-10-08 | 2K/4K 输出、最多 15 张参考图 + 3 段参考音频；RunningHub 首发 540P 低至 3 分/秒 | 转述（百度百科；官方页未核） |
| **Vidu S1** | 2026 WAIC 展示 | 实时流式交互生成，540P / 最高 42FPS，无限时长连续对话（数字人场景） | **仓库已核**（生数官网 WAIC 新闻，一手；见兴趣全景 §3.3） |
| **Sora 2 / 2 Pro**（OpenAI） | 2025-09-30 发布 | 原生同步音视频（对白/音效/环境音同 pass 生成）；物理真实感为媒体评测强项；app 15s / Pro web 25s（2025-10 口径）；API 侧时长上限媒体口径不一（15s–1min） | 转述（Artflo/heise/多家互证；**矛盾登记**：ofox.ai 称 Sora 2 Pro"无原生音频"，与其余全部来源矛盾，判为低质来源不采信） |
| **Veo 3 / 3.1**（Google） | Veo 3 2025-05；3.1 2025-10，2026-01 升级 | 原生音频（lip-sync 媒体评测领先）、2026-01 升级支持素材生视频/原生竖屏/1080P/4K 超分；Vertex AI 企业通道 | 转述（CSDN 月度总结/heise/媒体对比；官方页未核） |
| **Kling 2.5 Turbo / 2.6 / 3.0**（快手） | 2025-09 → 2026 | 2.6 Pro 原生 lip-sync 对白；3.0 多镜头分镜/参考图一致性/30s+；Motion Control 参考视频运镜 | 转述（ofox/toolcenter/Artflo；版本线混乱，精确参数未核） |
| **Seedance 2.0**（字节） | 2026-01 | 单镜头表现力/情绪张力媒体口碑强 | 转述（甲子光年/月度总结） |
| **Genie 4**（DeepMind） | 2026-10-27 研究预览 | 一致性约 2 分钟（媒体转述，官方无数值）、原生音频、Gemini 导演混合架构；闭源无公开程序化端点 | **仓库已核**（genie-4 卡 + 兴趣全景 §3.2；数字纪律沿用） |

### 2.2 开源权重格局

| 家族 | 权重/许可 | 关键事实 | 口径 |
|---|---|---|---|
| **Wan2.1**（阿里，2025-02-25） | Apache-2.0，T2V-14B/1.3B、I2V-14B、FLF2V、VACE | T2V-1.3B 官方 8.19GB 峰值显存（4090，offload+t5_cpu），5 秒 480P 约 4 分钟（261.4s）；1.3B=30 层/dim1536 | **一手**（官方 GitHub README + 效率表，本次检索） |
| **Wan2.2**（2025-07-28） | Apache-2.0，T2V/I2V-A14B（MoE 27B 总/14B 激活）、TI2V-5B、S2V-14B、Animate-14B | MoE 按 SNR 路由；TI2V-5B 720P@24fps ≥24GB；数据 +65.6% 图/+83.2% 视频；A14B 单卡 480P 峰值 41GB 级 | 一手（README）+ **仓库已核**（Animate 卡/深报） |
| **Wan-Animate-2**（2026-08-07） | Apache-2.0，Base+Distillation 双权重 + Lite | 端到端免预处理、蒸馏 10 步、Lite"实时流式"；默认 8×A800 | **仓库已核**（Animate 卡 §1/§4） |
| **Wan2.5+** | API 闭源（2.5 起原生音频） | 开源线止步 2.2 的口径见第三方报告；Wan 2.7 开源说法仅见媒体 | 转述/**未核** |
| **CausVid / Self-Forcing** | 代码+权重公开（基于 Wan2.1-1.3B） | 4 步因果流式，≥24GB；Self-Forcing 训练 data-free（DMD/SiD 版） | 一手（GitHub README） |
| **SkyReels-V3**（昆仑万维，2026-01-29） | 宣称完全开源 | 参考图转视频/视频演唱/音频驱动虚拟形象三能力单架构 | 转述（月度总结；仓库/LICENSE 未核） |
| **LTX-2 / LTX-Video**（Lightricks） | 开源（LTX-Video 1.9B 见 Self-Forcing 对比表） | LTX-2 原生音频；消费级友好口碑 | 转述/**未核** |
| **交互世界模型开源池** | ForgeWM/Solaris/StatePlay/Matrix-Game 3.x/ABot-World-0/LingBot-World | 权重开放性与硬件口径见仓库 D1 卡（45 条目录已核，各仓 LICENSE 未逐页） | **仓库已核**（六方向 note §1 D1） |

**格局判断（描述性）**：开源权重与闭源产品的质量差媒体口径约 12–18 个月（转述）；但**实时流式/交互赛道开源反而激进**（Self-Forcing 系全开放、ForgeWM 连 89GB 训练数据都开放，仓库已核）——因为该赛道学术属性强、产品化弱。声画联合生成的开源权重截至本次检索未见与闭源对齐者（Wan2.5 闭源、LTX-2 待核）。

---

## 3. 关键文献精读（3 篇）

### 3.1 Wan 技术报告（arXiv:2503.20314）+ Wan2.2 官方 README —— 开源 DiT 视频生成的完整解剖

- **问题**：如何把视频生成做成可复现、可 scaling 的开放基座（数据/架构/评测全公开）。
- **方法**：Wan-VAE（3D causal，4×8×8，127M）+ DiT（14B：40 层/dim5120，full ST-attention，RoPE 三轴，共享 adaLN）+ flow matching（rectified flow，logit-normal 时间步采样）+ 大规模数据策展 + Wan-Bench 自动评测。2.2 增量：SNR 路由双专家 MoE（27B 总/14B 激活）、电影级美学标注数据、4×16×16 高压缩 VAE（TI2V-5B）。
- **关键结果**：Wan2.1 VBench 86.22%（发布时开源第一，官方口径）；1.3B 8.19GB 消费级可跑；TI2V-5B 单消费卡 9 分钟出 5 秒 720P；MoE 验证损失优于全部 dense 对照。
- **局限**：训练数据不公开（只有规模声称）；Wan-Bench 为自建基准（口径自定）；2.2 的 MoE 训练细节（负载均衡等）README 未展开，第三方报告标"合理推断"。
- **与 OmniWorld 的关联**：仓库深报已把其 I2V 输入三元组（noise/condition/mask）与 World Contract 四轴逐轴对照——**mask=ownership、reference=identity、双时间轴=time、置信度缺位=confidence 须评测环补齐**。Wan 系是"生成式表现后端"接口契约的最完整开源参照；其 MoE"按去噪阶段分工"思想与引擎"参考/性能双模"无直接关系但同为"阶段特化"设计。
- 核验：报告全文与源码逐行核验见[仓库深报](../deep/2026-11-05-wan22-animate.md)（2026-11-05/06，commit 42bf4cfa 锚定）；2.2 README 本次检索一手。

### 3.2 Vidu4D（arXiv:2405.16822，NeurIPS 2024）—— 生成视频→4D 重建的链路样板

- **问题**：单段**生成**视频（非多视角实拍）含非刚性漂移与帧间畸变，如何重建出时空一致的高保真 4D（时序 3D）表示？
- **方法**：两阶段。①warping field 初始化；②**Dynamic Gaussian Surfels（DGS）**：维护一个 canonical 静态 Gaussian surfel 集合（2D 面元，几何比 3DGS 更准），用时变 warping 函数把静态面元变换到每帧的 warped 状态；warped-state 法线一致性正则（连续 warping 场估法线）保几何完整；旋转/缩放双分支精修抑制 warping 纹理闪烁。上游接 Vidu 视频扩散模型（arXiv:2405.04233）构成 text→video→4D 全链。
- **关键结果**：新视角渲染的纹理细节与法线质量优于 BANMo/D-NeRF（NeRF 系）与 Deformable-GS/SCGS（GS 系）；对生成视频伪影更鲁棒（论文定性+定量，本次检索 NeurIPS proceedings 全文一手）。
- **局限（官方 §5 承认）**：依赖上游视频质量；大场景扩展性差；逐场景优化、计算上无法实时。另有生成内容滥用风险声明。
- **与 OmniWorld 的关联**：这是"隐式生成→显式资产"转换器的学术样板，落在 L2 输入侧；与 VGGT→gsplat 链（真实视频侧，仓库已核）互补。其 canonical+warping 分解 = "身份不变量 + 时变状态"的表示级版本，与契约 identity/time 分离同构；但产物无实体 id/无事件/无置信度——**重建出的 4D 资产要进世界运行时，语义层必须外部补齐**。逐场景优化非实时 ⇒ 只能离线资产生产者，不能进 Grounding Loop。
- 核验：arXiv 编号经 HF papers 页确认；方法/局限经 NeurIPS 2024 proceedings PDF 全文（本次检索，一手）。作者含朱军（生数首席科学家）——Vidu 视频模型与 Vidu4D 重建的同门链路。

### 3.3 Self-Forcing（arXiv:2506.08009，NeurIPS 2025 Spotlight）—— 实时流式生成的训练范式修正

- **问题**：AR 视频扩散的 exposure bias——训练条件在 ground-truth 前缀、推理条件在自己的带错前缀，长视频误差累积；且双向扩散模型无法流式。
- **方法**：训练时执行 inference-like **self-rollout**（KV caching 逐帧/逐 chunk 自回归生成），每帧条件在自生成输出上；对完整视频施加 holistic 分布匹配损失（DMD 主结果，SiD/GAN 亦可）；few-step（4 步）+ 随机梯度截断（每 chunk 只保留随机一个去噪步的梯度）控制成本；**rolling KV cache**（O(TL)）+ 训练时局部 attention 窗口模拟缓存淘汰，支持长视频外推。基座 Wan2.1-T2V-1.3B；DMD/SiD 版训练 data-free。
- **关键结果**：官方摘要（一手）：单 GPU 亚秒延迟实时流式，质量匹配或超过慢得多的非因果扩散模型。社区口径（转述）：chunk-wise 17 FPS/首帧 0.69s，VBench 84.31 vs CausVid 81.20 vs Wan2.1 原生 84.26（延迟 103s→0.69s，约 150×）；训练 64×H100 约 1.5–2 小时。
- **局限（论文讨论，经转述笔记）**：远超训练长度仍退化；梯度截断可能削弱长程依赖学习；rolling cache 遗忘首帧的问题靠训练技巧缓解而非根治。后续 Causal Forcing（未核）指其双向教师 ODE 初始化违反帧级单射性、画质有 10–20% 损失——**该路线仍在快速迭代，结论半衰期短**。
- **与 OmniWorld 的关联**：①开源实时流式标的的接口形状（chunk 因果 + KV cache + 动态 prompt）= 可交互生产者的适配器契约参照；②"训练分布必须对齐推理分布"与引擎"参考模式/性能模式必须行为一致"是同一纪律在不同层的体现；③其 rolling cache 遗忘 = 无外部状态锚的又一实证——引擎持有权威身份/事件时钟的关联性论据（不裁决）；④硬件 ≥24GB ⇒ 本机 8GB 只能作离线评测对象或云端标的。
- 核验：arXiv 摘要页 + GitHub 官方 README 本次检索一手；FPS/VBench 细表经知乎笔记转述（与 README 定性一致，未逐表核原文）。

---

## 4. 开放问题

1. **长时一致性的天花板在哪**：窗口扩大（Vidu Q3 16s 单窗）只是推迟漂移；rolling cache/检索记忆（Matrix-Game 3.x）是模型内补丁。**外部状态锚 + 生成式表现**的混合路线是否唯一能过"分钟级恒存 + 事件推进"关的路线？WRBench/MemoBench 已把病灶量化，但尚无系统在闭环里证明修复（仓库 D2 已核）。
2. **蒸馏的多样性代价与状态正确性的关系**：reverse KL mode-seeking 降低输出多样性——对内容创作是缺点，对"可复现世界"是否反而有利？未核，无文献直接回答。
3. **声画联合生成的开源对齐**：闭源已全面 native audio，开源权重何时跟上（LTX-2 待核）？联合 latent 空间里音频/视频的状态一致性如何测量（音画同步误差是否可做成 grounding 指标）？
4. **视频→4D 链路的规模化**：Vidu4D 逐场景优化非实时、大场景差；feed-forward 化（VGGT 式）能否搬到生成视频侧？重建产物的幻觉几何如何验收（WROP 式真值考卷可否用于 4D 资产质检）？
5. **运镜控制的可复现性**：prompt 级运镜不可复现；轨迹级控制（ATI/Wan-Move）与外部相机状态持有的契约形状是什么？"3D 导演台"类产品是否事实上承认了显式控制面的必要性？
6. **帧级单射性之争**（Causal Forcing vs Self-Forcing）：AR 蒸馏的理论基础仍在修正中，选型结论半衰期短——跟踪而非锁定。
7. **8GB 本机的离线视频生成边界**：Wan2.1-1.3B 官方 8.19GB 略超 8GB；LightX2V 宣称 4060 8GB 可跑（官方 README 列名）但无实测数字——哪些配置（量化/offload/分辨率/帧数）真正落入 8GB，需逐卡实测（§6）。

---

## 5. 仓库已有资产引用

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| [projects/wan22-animate.md](../projects/wan22-animate.md) | Animate 双 Apache-2.0 许可；官方无 Animate 显存数字（同族 14B 锚点 41–60GB）；托管 API 按输出秒计费/2–30s；Wan-Animate-2 代际风险（免预处理/蒸馏 10 步/Lite 实时）；"只能进程外生成后端/评测对象"边界 |
| [deep/2026-11-05-wan22-animate.md](../deep/2026-11-05-wan22-animate.md) | Wan-VAE/DiT/flow matching 源码级机制（§2.1）；统一输入范式与段间 1/5 帧漂移论证（§2.2）；World Contract 四轴逐轴对照（§4）；dense/MoE config 硬对照；30fps 双时间轴 |
| [2026-11-13-interest-landscape-and-redefinition.md](../2026-11-13-interest-landscape-and-redefinition.md) | 五段生命周期链与 L4 占位（§1）；显式/隐式分叉表（§1.2）；Vidu S1 540P/42FPS 与 Motubrain MoT 官网已核（§3.3）；生数融资额/AA 榜单第一=未核口径（§6）；视频生成 vs 世界模型判据=反事实（§1.4） |
| [2026-11-13-six-direction-innovation-integration.md](../2026-11-13-six-direction-innovation-integration.md) | D1 开源交互世界模型池（ForgeWM/Solaris/StatePlay/Matrix-Game 3.x 等）与 8GB 结论；WRBench/MemoBench/WROP 状态核缺失证据链（D2）；Marionette 混合架构；"实时交互要 H100/5090 级、离线闭环评测可行"（D1） |
| [2026-10-27-character-animation-route.md](../2026-10-27-character-animation-route.md)（经上述卡间接引用） | 运动数据层=唯一长期资产；P2 决定性实验与自测验收指标（PCK/MPJPE/身份余弦/时序抖动） |
| [projects/genie-4.md](../projects/genie-4.md)（经全景 note 间接引用） | Genie 4 闭源/无公开端点/官方六条局限；"2 分钟"为媒体转述的数字纪律 |

---

## 6. 待核验清单（未核主张）

**商业/榜单类（沿用仓库口径，不作裁决依据）**：

1. Vidu Q3"Artificial Analysis 全球第一"（2026-02-14 时点）——榜单原页未直接核验；1241 分/发布时全球第二为百科+媒体互证转述。
2. 生数 B 轮近 20 亿/ARR $20M/估值超 20 亿美元/5 亿美金融资——仓库已标转述，本次百科另见"超 6 亿元轮"与"阿里云领投近 20 亿 B 轮"两种口径，**矛盾登记**，沿用未核。
3. Vidu Q4 Preview 参数（2K/4K、15 图+3 音频、3 分/秒）——百科转述，官方页未核。
4. Vidu Q3"3D VAST 式音频分支/机位条件向量/字形-像素对齐模块"——百科转述的技术机制，无论文支撑，未核。
5. Sora 2 API 时长上限（15s/25s/1min 各来源不一）；ofox.ai"Sora 2 Pro 无原生音频"与主流来源矛盾，不采信。
6. Kling 版本线（2.5/2.6/3.0）精确参数与 Wan2.5+ 闭源边界、Wan 2.7 开源说法——媒体转述，未核。
7. Causal Forcing arXiv:2602.02214 编号与全部数字——仅见第三方论文笔记，原文未核。
8. Self-Forcing 细表数字（17FPS/0.69s/VBench 84.31）——知乎笔记转述，未逐表核论文原文。
9. TurboDiffusion（生数开源蒸馏框架，2025-12）——百科一句带过，仓库/论文未核。

**RTX 4060 Laptop 8GB 离线可行性（重点待实测）**：

| 候选 | 现有证据 | 8GB 判断 |
|---|---|---|
| Wan2.1-T2V-1.3B | 官方 8.19GB 峰值（4090，offload+t5_cpu，480P 81 帧）——一手 | **边缘偏超**：8.19>8.0；减帧数/降分辨率/GGUF-Q4 量化可能落入，未实测 |
| LightX2V（Wan2.1/2.2 加速框架） | Wan2.1 官方 README Community Works 原文"can run on RTX 5090 and RTX 4060 (8GB VRAM)"——一手列名，但属第三方项目宣称 | **最有希望的已宣称路径**；具体模型/分辨率/步数/耗时未核，需实测 |
| DiffSynth-Studio FP8/逐层 offload | 仓库已核（支持 Wan2.2 全家族 low-VRAM inference） | 14B 级即便 FP8 也远超 8GB；1.3B/5B+offload 待实测 |
| Wan2.2-TI2V-5B | 官方 ≥24GB——一手 | **出局**（原生）；量化+offload 后未核 |
| Self-Forcing / CausVid | 官方 ≥24GB、Linux——一手 | **出局**（实时流式本机无望；离线少步推理量化后未核） |
| LTX-Video 1.9B / LTX-2 | 参数量见 Self-Forcing 对比表（转述）；消费级口碑（转述） | 候选，显存/许可/Windows 支持全未核 |
| Wan2.2-Animate-14B | 仓库已核：14B 级数十 GB | **出局**（与仓库结论一致） |
| Vidu4D 重建 | 逐场景优化，官方无显存数字 | 未核；GS 优化类负载通常 8GB 可跑小场景，待查 |

**其他**：Wan2.2 README 中文版经镜像仓取得（satoshinji2992/wan2.2），与官方仓同源但未逐字 diff；AA 榜单、生数官网 Q3/Q4 新闻页、SkyReels-V3/LICENSE、LTX-2 模型卡均待后续逐页核验。

---

## 7. 来源与核验状态

| # | 来源 URL | 关键内容 | 状态 |
|---|---|---|---|
| 1 | https://arxiv.org/abs/2506.08009 | Self-Forcing 摘要：self-rollout/KV cache/holistic loss/rolling cache/亚秒延迟；NeurIPS 2025 Spotlight | **已核**（摘要页一手，2026-11-13） |
| 2 | https://github.com/guandeh17/Self-Forcing | 官方 README：≥24GB/Linux、Wan2.1-1.3B 基座、data-free 训练、64×H100 训练成本、4090 实时 | **已核**（一手） |
| 3 | https://openaccess.thecvf.com/content/CVPR2025/papers/Yin_From_Slow_Bidirectional_to_Fast_Autoregressive_Video_Diffusion_Models_CVPR_2025_paper.pdf | CausVid 全文：非对称 DMD、ODE 初始化、1.3s/9.4FPS、VBench-Long 84.27、附录 E 长程不一致/VAE 延迟下限/多样性损失 | **已核**（CVF 一手；arXiv:2412.07772 经 ar5iv 互证） |
| 4 | https://proceedings.neurips.cc/paper_files/paper/2024/file/ed3c686f9cda57e56cc859402c775414-Paper-Conference.pdf | Vidu4D 全文：DGS/warping field/法线正则/双分支精修/局限；arXiv:2405.16822 经 HF papers 页确认 | **已核**（一手） |
| 5 | https://arxiv.org/abs/2606.25473 | Causal-rCM：TF-CM 初始化 + SF-DMD、2 步 Wan2.1-1.3B VBench 84.63、应用于 Cosmos 3 | **已核**（摘要页一手） |
| 6 | https://github.com/Wan-Video/Wan2.1/blob/main/README.md | Wan2.1：8.19GB/261.4s 效率表、1.3B config、LightX2V"4060 8GB"列名、ATI/Wan-Move 社区列名 | **已核**（官方 README 一手） |
| 7 | https://github.com/satoshinji2992/wan2.2/blob/main/README.zh-CN.md | Wan2.2：MoE 27B/14B、SNR 切换、TI2V-5B 4×16×16 VAE/9 分钟 720P/≥24GB、S2V 管线、+65.6%/+83.2% 数据 | **已核**（官方 README 中文镜像；与官方仓同源未逐字 diff） |
| 8 | https://arxiv.org/abs/2503.20314 · https://arxiv.org/abs/2509.14055 · https://arxiv.org/abs/2212.09748 | Wan 报告/Wan-Animate/DiT | **仓库已核**（深报 2026-11-05 全文/摘要核验，本次只链接） |
| 9 | https://baike.baidu.com/item/Vidu%20Q3/67330811 · https://baike.baidu.com/item/Vidu%20Q%E7%B3%BB%E5%88%97/69132570 | Q3 发布/16s 1080P/AA 1241 分/榜单时点/Q4 Preview/U-ViT 机制描述 | **转述**（百科，机制描述与官方发布材料口径一致但无论文） |
| 10 | https://zhuanlan.zhihu.com/p/2061490356462542896 | Self-Forcing 细表（17FPS/0.69s/VBench 84.31）、rolling cache 首帧淘汰修复、TF/DF/SF 对比 | **转述**（论文笔记，与一手摘要定性一致） |
| 11 | https://github.com/zhaoyang97/Paper-Notes/blob/main/docs/ICML2026/model_compression/causal_forcing_autoregressive_diffusion_distillation_done_right_for_high-quality.md | Causal Forcing：帧级单射性/AR 教师 ODE/TF>DF；arXiv:2602.02214 | **转述/未核**（编号与数字均待原文核验） |
| 12 | https://artflo.ai/blog/sora-2-review · https://www.heise.de/tests/Sechs-KI-Videogeneratoren-mit-Tonspur-im-Vergleich-11093896.html | Sora 2 2025-09-30 发布/原生音视频/15s–25s；Veo 3.1 音频；Kling 2.5 Turbo 对比实测 | **转述**（媒体互证） |
| 13 | https://ofox.ai/blog/ai-video-generation-apis-sora-veo-kling-compared-2026 · https://www.toolcenter.ai/zh/articles/ai-video-generation-tools-2026 | 三 API 能力表/Kling 3.0/Wan 2.7 说法 | **转述**；ofox"Sora 2 无原生音频"与 #12 矛盾，**不采信该条** |
| 14 | https://www.ithome.com/0/967/217.htm | Vidu Q3 Turbo/Pro 上线华为云 MaaS（2026-06-22） | **转述** |
| 15 | https://blog.csdn.net/QbitAI/article/details/160194113 · https://xueqiu.com/1464536232/378654232 | Q3 参考生实测/AA 登顶口径（2026-02/03） | **转述**（量子位/甲子光年） |
| 16 | https://vivideo.ai/research/which-ai-video-models-have-native-audio | native audio vs added audio 分类；2026 年仍属少数能力 | **转述** |
| 17 | https://blog.csdn.net/hanmo22357/article/details/156516018 | 2026-01 月度：Veo 3.1 升级/SkyReels-V3 开源/LingBot-World/Q3 发布 | **转述** |
| 18 | https://github.com/hdy117/world_model/blob/main/Wan2.1-2.2-架构训练推理报告.md | Wan2.1/2.2 第三方架构报告（MoE 训练细节标"合理推断"；Wan2.5+ 闭源口径） | **转述**（仅用于交叉印证，推断部分不采信） |
| 19 | 仓库内部（§5 表所列 6 项） | 既有核验结论 | **仓库已核**（只链接不复述） |

**矛盾登记**：①ofox.ai"Sora 2 Pro 无原生音频"vs 其余全部来源——判低质不采信；②生数融资额多口径（仓库未核项 + 百科两种）——沿用未核；③CausVid VBench-Long 84.27（CVF 正文）vs 82.85（CVPR poster 摘要页）——版本差异，本文取 CVF 全文口径；④Vidu Q3 发布时"全球第二"vs 2026-02-14"全球第一"——时点不同非矛盾，引用必须带时点与榜单名。