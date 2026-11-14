# T5 程序化生成与 3D 资产生产 — 知识基线（2026-11-13）

> **Trust label: [Research → 知识基线]**——为 owner 方向评审（grill → ADR-0016）提供 T5 主题的知识地基：这门技术到底怎么工作、工业界走到哪了、哪些结论已一手核验。本文**只调研不裁决**：与 OmniWorld 的关联只写"关联性"（L2 构建层、生成器契约、真值世界工厂、位级复现），不写"我们应该做 X"；不改 PLAN / CURRENT / research-queue / 任何既有文档。
> **去重纪律**：仓库已有事实卡与 note 只链接复用结论，不复制大段。
> **数字纪律**：凡未一手核验的标"未核"；媒体/第三方转述不得写成官方口径；所有 arXiv 编号均经本轮搜索确认存在。

---

## 1. 原理与技术路线分类（重点）

### 1.1 程序化生成的本质：内容 = f(参数, seed)

程序化生成（Procedural Content Generation, PCG）是把"内容本身"替换为"生产内容的配方"：不存储一千棵树，存储一个树生成器 + 一千组参数/seed。学术线的经典分类（Togelius et al. 2011 search-based 分类学，经 [SBPCG 综述 arXiv 2311.04710](https://arxiv.org/html/2311.04710v1) 转述，已核该综述存在与内容）：

- **按目的**：压缩（省存储/省人力）、可重玩性（每次生成新惊喜）、适应性（experience-driven，按玩家状态生成）、创作支持（mixed-initiative，算法提议、设计师裁决）。
- **按必要性**：necessary content（每件产物必须可用，如关卡）vs optional content（允许部分出彩，如装饰植被）——这决定了验收标准的松紧。
- **按时间**：offline（开发期生成）vs online（运行时生成）；deterministic vs stochastic。

综述的核心结论（已核，检索部分正文）：**成熟管线都是混合式**——噪声铺地形、solver 保证任务可达、search 调难度、学习模型出风格——因为不同内容层的"正确性条件"不同。这与"AI 出初稿 + 规则管约束/验证"的混合式思路（[六方向 note §1 D4](../2026-11-13-six-direction-innovation-integration.md)）在方法论上同构。

### 1.2 数学基础与方法家族

| 家族 | 数学机制 | 典型产物 | 关键性质 |
|---|---|---|---|
| **噪声（noise）** | Perlin gradient noise（1985）：格点伪随机梯度 + 平滑插值 = "连贯的随机"；fBm 多倍频叠加控制频谱；domain warping 增加有机感；Voronoi/Worley 噪声出细胞/裂纹结构 | 地形高度图、纹理、云、密度场 | 纯函数、无状态、天然 seed 化；同一坐标同一 seed 永远同值——是确定性最好的家族 |
| **文法 / L-system** | 重写规则（rewriting rules）：从公理出发并行/串行替换符号串，海龟解释成几何；shape grammar 把规则作用于形状而非字符串 | 植物、道路网、建筑立面、任务链 | 规则即资产；表达力来自规则组合而非参数扫描；可验证结构性质（如连通性） |
| **约束求解（solver-based）** | CSP/SAT/ASP：把"可达性、难度包络、不穿模"写成硬约束，求解器保证满足 | 关卡布局、谜题、任务图 | 用灵活性换正确性保证——necessary content 的首选（综述已核结论） |
| **WFC（WaveFunctionCollapse）** | 本质是局部邻接约束上的 CSP：每格初始为全部 tile 的叠加态，观察（坍缩最低熵格）→ 约束传播删不相容选项 → 重复；矛盾则回溯/重启（Gumin 2016 开源实现，机制为领域通识，原始仓库本轮未逐页核） | 纹理合成、tile 地图、简单 3D 布局 | 从"一个样例"学出局部约束再全局求解；结果多样但**不保证全局语义正确**，仍需验收 |
| **搜索式（SBPCG）** | 编码（genome）+ 适应度（fitness）+ 搜索算法（进化/爬山）；generate-and-test 是其退化形式 | 关卡、武器、相机控制 | 质量由 fitness 定义——fitness 编码了什么简化，产物就继承什么盲区（综述已核） |
| **学习式（PCGML / 生成模型）** | 从内容语料学统计分布：GAN/diffusion/LLM；3D 侧即 Hunyuan3D 类 mesh 生成 | 风格化纹理、mesh、关卡草稿 | 擅长风格与统计保真，弱在新颖性、正确性保证、小数据（游戏出厂带关卡不带数据集，综述已核） |

**与 OmniWorld 的关联性（只写关联）**：这些家族全部是"显式配方式"生产——产物可参数化、可 seed 化、可版本化，天然落在四层架构的 L2 构建层；其中 solver/规则家族的"正确性保证"与引擎的测量/验收纪律同向。

### 1.3 规则图与参数化资产：三大工业机制（本轮一手核验）

三家工业工具收敛到同一个抽象：**dataflow DAG（节点图）+ 显式参数接口 + 依赖驱动的增量重算**。这正是"生成器作为可版本化资产"的工业既成形态。

**UE PCG**（[官方 Overview](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-overview)，已核检索全文；[Electric Dreams 术语集](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-in-electric-dreams?application_version=5.2)，已核检索节选）：
- 核心数据类型是 **Point**：transform、bounds、color、density（0–1 存在概率）、steepness、**seed**。点沿图流动，经 Sampler（生成点）→ Filter（按属性/密度删点）→ Modifier（变换）→ Spawner（放置 mesh/actor）。
- **seed 是分层派生的**：官方术语集明示 point seed（int64）= 由点位置、节点 seed、组件 seed 计算而来——确定性不是全局一个随机数，而是结构化的 seed 派生树。
- **参数化资产**：Graph Parameters（用户可覆盖值）+ Graph Instance（类比 Material Instance）+ Subgraph 复用 + 模板图；C++/Blueprint 属性标 `PCG_Overridable` 即自动成为可覆盖引脚。
- **属性系统**：静态属性（`$Position` 等）+ 动态属性（metadata），分 Data/Points/Elements 三个 domain——语义标注（tag、mesh 路径、material 路径）随点流动，Level to PCG 工具可把整个手工关卡导出为带 actor tag 的点云再程序化增广。
- **增量与规模**：图编辑实时更新 viewport；与 World Partition 集成按加载 cell 流式生成；官方节点表含 Sanity Check Point Data（越界即报错并取消生成）——图内自带验收节点的雏形。

**Houdini**（[官方 Cooking 文档](https://www.sidefx.com/docs/houdini/basics/cooking.html)，已核检索节选；[Houdini Engine for Gamedev 官方 PDF](https://www.sidefx.com/media/uploads/products/engine/hengine_games.pdf)，已核检索全文；[PDG/TOPs 官方介绍](https://www.sidefx.com/ja/docs/houdini19.0/tops/intro.html)，已核检索全文）：
- 官方口径："In Houdini, everything you do creates a node"——节点网络定义数据流，随时回改任意上游参数；**cooking = 求值节点网络算出当前帧场景状态**，改参数/接线即触发重算。
- **依赖传播**：上游几何变化 → 下游 modifier 节点标 dirty → 重 cook（官方论坛工程师表述，转述）；表达式引用（channel reference、`point()` 等）构成输入之外的隐式依赖边。
- **HDA（Houdini Digital Asset）**：把网络封装成单节点 + 艺术家参数接口；官方明示"Changes made to the core asset, even deep into production, will update all instantiations"——**生成器版本升级自动传播到全部实例**，这是"生成器作为资产"的最强工业证据。
- **Houdini Engine**：宿主（UE/Unity/Maya）内经 API 加载 HDA，宿主改参数 → Engine cook → 结果回传宿主；配方可脱离 Houdini UI 分发。
- **PDG/TOPs（Procedural Dependency Graph）**：把工作拆成 work items + 依赖图，processor 扇出（如 Wedge 按参数变体批量生成）、partition 扇入等待、scheduler 派发到本机/渲染农场；官方明示"变更时只重 cook 依赖该变更的最小工作集"——**这就是"生成器当数据工厂"的工业调度机制**（WROP 的 Blender 批量生成在概念上是它的轻量同类）。

**Blender Geometry Nodes**（[官方 4.5 手册 Geometry Nodes Modifier](https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/geometry_nodes.html)，已核检索全文）：
- 节点组（node group）作为 **modifier** 挂在 mesh/curve/text/volume 对象上，几何沿 modifier 栈流动；节点组可共享、每个 modifier 实例可有独立输入值——同 HDA/Graph Instance 的"配方与实例分离"。
- **Fields**：输入可切换"单值 vs 逐元素属性"——field 是延迟求值的逐元素函数，是 GN 的核心抽象；Output Attributes 可把任意 field 输出物化为命名属性（语义标注出口）。
- **Bake**：Simulation Zone / Bake 节点的结果可烘焙进 .blend（packed）或磁盘目录——"活配方"与"烘焙产物"显式分离（与 OpenUSD 的 pose cache 血统同一取舍，见 [deep/openusd §2.7](../deep/2026-11-05-openusd.md)）。
- 节点组可标记为 Asset 跨项目复用；4.5 起有 Tool context（节点组直接做成编辑工具）。
- WROP 的 150 个考卷生成器正是建在这套机制上（§3.1）——证明 Blender GN/Python 足以承担"带真值的数据工厂"。

**共同机制提炼**（关联性）：参数接口 = 生成器契约的输入侧；点/属性/命名属性 = 语义随几何流动的载体；dirty 传播 + 最小重算 = 依赖更新的成熟范式（OpenUSD change processing 是同一范式的场景描述版，[deep/openusd §2.5](../deep/2026-11-05-openusd.md) 已核，互链）；HDA 的"改配方全实例更新" = 生成器版本化的工业先例。三家都**没有**置信度与单写者语义——生成器契约中"参数→几何+碰撞+语义+**置信度**+版本"的后两项是工业机制之外的增量（与 [openusd 卡 §2.2](../projects/openusd.md) 对 USD 的同一结论平行）。

### 1.4 seed 确定性与跨平台浮点陷阱

一手参照：Glenn Fiedler《Deterministic Lockstep》（gafferongames.com，已核 webfetch 全文）：

- **确定性的定义是位级**："Not close. Not near enough. Exactly the same. Exact down to the bit-level"——同初值 + 同输入序列 → 每帧全状态 checksum 相同。
- **同机同二进制同 OS 都不自动保证**：作者实例——ODE 物理引擎在 solver 内部用 RNG 随机化约束处理顺序以提升稳定性，直接破坏确定性；修复 = 每帧把内部 seed 钉到帧号（`dSetRandomSeed`）。**教训：确定性要审计到第三方库内部 RNG 与求值顺序，不只是自己的 seed。**
- **跨编译器/OS/架构基本不保证**："Differences in floating point behavior between compilers, OS's and even instruction sets make it almost impossible to guarantee determinism"；甚至 debug vs release 构建因浮点优化都可能不同。固定步长（同 delta time）是必要前提。
- 配套专文《Floating Point Determinism》（同站 `/post/floating_point_determinism/`，本轮仅确认链接存在，正文**未核**）。

**对"生成器位级复现"的含义（关联性，非裁决）**：
1. 仓库参考模式已做到同 seed 逐字节（ADR-0009，[全景 note §2](../2026-11-13-interest-landscape-and-redefinition.md) 已核结论）——生成器要进同一纪律，需把"工具链版本 + 生成器代码版本 + 参数 + seed"整体作为复现键，而不只是 seed。
2. 噪声/文法类纯函数生成器最容易位级复现；凡引入物理 solver、并行归约、GPU 浮点的环节都是风险点（仓库已有平行证据：MJWarp GPU 默认非确定，[六方向 note §1 D3](../2026-11-13-six-direction-innovation-integration.md) 引 newton 卡，互链）。
3. UE PCG 的分层 seed 派生（§1.3）说明工业界把"确定性"做成结构性质（seed 树）而非全局开关——生成器契约可借鉴该形态。
4. 跨平台位级复现若必需，通行手段是定点数/整数化或严格 FP 纪律（禁 fast-math、钉求值顺序）——代价与收益属待核验/待裁决项（§6）。

### 1.5 几何 + 碰撞体 + 语义 + 材质的一体化生成

只有 mesh 的生成器产物不可仿真、不可测量。一体化生成要同时产出四层：

| 层 | 内容 | 工业现状 |
|---|---|---|
| 几何 | mesh/曲线/实例 | 三家节点图均成熟（§1.3） |
| 碰撞体 | 凸包/凸分解/基元近似，与视觉几何一致性 | UE spawner 继承 static mesh 碰撞；Houdini 有凸分解工具链；**"视觉-碰撞一致性"无通用自动验收标准**（Saltline 失效清单恰含此项，[全景 note §3.4](../2026-11-13-interest-landscape-and-redefinition.md)） |
| 语义 | 身份、类别、tag、真值轨迹 | UE 点属性/actor tag；Blender 命名属性；WROP 的 trajectory.npz + metadata.json（§3.1）；机器人向的 SimReady 以 OpenUSD 承载（NVIDIA 博客已核，[全景 note §3.1](../2026-11-13-interest-landscape-and-redefinition.md)，互链） |
| 材质 | PBR 参数/纹理；程序化材质免逐模型 UV（triplanar）、烘焙缓存复用 | Saltline 的性能纪律案例（[六方向 note §1 D6](../2026-11-13-six-direction-innovation-integration.md)，互链）；Hunyuan3D-Paint 走生成式纹理路线（§1.7） |

**关联性**：这正是"生成器契约：参数→几何+碰撞+语义+置信度+版本"的技术内容物。工业界四层各有机制但**没有统一契约**把四层绑定为一次可验收、可复现的生成事务；WROP 证明了"几何+真值轨迹+评分元数据"可以同源产出（§3.1），SimReady 证明了"验证门"工作流存在但绑定 NVIDIA 生态（互链既有结论）。规则验收器（穿模/法线/碰撞一致性/比例）对应 UE 的 Sanity Check 节点与 SimReady 验证门的通用化形态。

### 1.6 OpenUSD 的 layer/composition/reference 为何适合当资产互操作契约

机制细节**全部互链** [deep/2026-11-05-openusd.md](../deep/2026-11-05-openusd.md)（LIVERPS 强度序、prim index 缓存、六组合弧、change processing，均已一手核）与 [projects/openusd.md](../projects/openusd.md)（许可 TOST 1.0、Core Spec 1.0、边界）。此处只写"为何适合当契约"的原理层归纳：

1. **非破坏性叠加 = 版本化 deltas**：Layer 是 opinion 的稀疏持久容器，覆盖（override）不改源文件——生成器产物的每次修订可以是一层新 layer，审计链天然存在。
2. **reference 封装 + variant 备选 = 参数化资产的交换形态**：生成器的"配方实例"可表达为 reference + variant selection + 覆盖层，跨 DCC 无损流转。
3. **组合是确定性纯求值**：同层集合 → 同 Stage 视图，可复现——与生成器的 seed 确定性同构，二者拼接后端到端可复现。
4. **边界（既有裁定，互链）**：USD 是"数据模型+文件语义"不是活世界运行时；无 ownership/confidence 对应物；动画值强度序优先于时间序（逐 tick 状态流须单源 timeSamples）；OmniWorld 对其仅单向导出（既有决策，本文不触碰）。
5. **生态信号**：NVIDIA Agentic SimReady 用 OpenUSD 当 agent 间共享契约层（已核，互链 [全景 note §3.1](../2026-11-13-interest-landscape-and-redefinition.md)）——"USD 作为资产契约"已被工业头部实践背书，但绑定其生态。

### 1.7 两条 AI 路线：AI 生成 mesh vs AI 写生成器代码

**路线 A：AI 直接生成 mesh/纹理**（代表：Hunyuan3D 2.0，[官方仓库 README](https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/README_zh_cn.md) 已核检索全文，技术报告 arXiv 2501.12202 经官方 BibTeX 确认存在；Tripo 等商业 API 为转述**未核**）：
- 机制：两阶段解耦——Hunyuan3D-DiT（flow-based diffusion transformer，图像条件 → 裸 mesh）+ Hunyuan3D-Paint（几何/扩散先验 → 高分辨率纹理，可给手工 mesh 上色）。
- 官方硬件口径（已核，README 原句）：shape 生成 6 GB VRAM，shape+texture 共 16 GB——**8GB 卡只装得下几何阶段，纹理阶段超**（官方口径，本机未实测）。
- 强项：有机形状、一句话/一张图即得、风格多样。弱项：拓扑不可控、无参数化编辑面、无碰撞/语义伴生、同 prompt 不保证同 mesh（采样随机性）、权重许可需逐个核。

**路线 B：AI 写生成器代码**（代表：Saltline，一手证据互链 [全景 note §3.4](../2026-11-13-interest-landscape-and-redefinition.md)：单 prompt 约 8h、Three.js r180、模型/材质/音效全代码生成、几猫换皮证据）：
- 产物是**代码**而非资产：参数化、可 seed、可版本化、运行成本近零、可审计可验收——L2 可行性的直接证明。
- 已证教训：产物是一次性 demo，无复用系统、无状态测量、无正确性验收（既有结论，互链）。

**取舍对照（原理层，非裁决）**：

| 维度 | A：AI 生成 mesh | B：AI 写生成器 |
|---|---|---|
| 产物形态 | 静态资产（死数据） | 配方（活代码） |
| 可编辑性 | 需重生成或 DCC 手工改 | 改参数/改代码，数值级 |
| 确定性 | 采样随机，位级复现需钉全部采样链 | 纯函数生成器天然可位级复现（§1.4 纪律下） |
| 碰撞/语义 | 不伴生，需后处理 | 可在生成时同源产出（§1.5） |
| 边际成本 | 每次生成耗 GPU 推理 | 写一次，跑无穷次，CPU 即可 |
| 有机形状/人脸 | 强 | 弱（规则难写） |
| 验收 | 只能看像不像 | 规则检查器可逐条验收 |

**关联性**：两条路线不互斥——A 的 mesh 可作为 B 的场景中的静态 prop（生成器管布局/身份/碰撞，AI mesh 管外观）；"生成器作为可版本化世界资产"的契约形态只对 B 天然成立，对 A 需把"模型版本+采样参数+seed"整体当配方存档。WROP（§3.1）是 B 路线被规模化验证的实证：150 个手写/AI 辅助生成器产出 150 万带真值样本。

---

## 2. 进度快照（截至 2026-11）

| 对象 | 状态 | 核验口径 |
|---|---|---|
| **UE PCG** | 官方文档已至 UE 5.8 版；节点图/参数/实例/World Partition 流式生成成熟（§1.3）。第三方称"5.0–5.6 experimental，5.7 production-ready；5.8 加非破坏手工编辑、复杂 metadata、GPU runtime scatter" | 机制已核（官方）；**5.7/5.8 版本节点为第三方转述未核**（官方 release notes 未逐页） |
| **Houdini** | HDA + Houdini Engine（UE/Unity/Maya 宿主内 cook）+ PDG/TOPs 批量依赖调度，工业成熟多年；"改核心资产全实例更新"官方明示 | 已核（官方 PDF/文档）；具体最新版本号本文不主张 |
| **Blender Geometry Nodes** | 4.5 手册：fields、实例、命名属性、Simulation Zone + Bake、Tool context、资产化复用 | 已核（官方手册） |
| **AI 3D 生成** | Hunyuan3D 2.0（2025-01 开源权重+推理代码，后续 mini 0.6B/turbo/多视角变体，Blender addon）；shape 6GB / 全管线 16GB 官方口径 | 已核（官方 README）；Tripo 及 2026 年最新 SOTA 排位**未核** |
| **OpenUSD** | Core Spec 1.0 已批准（2025-12 公告）、v26.11.1 发行、Hydra 2.0 scene index 默认化 | 互链 [openusd 卡](../projects/openusd.md)（2026-11-05 已核） |
| **Saltline（AI 写生成器）** | 2026 年案例：单 prompt 8h 全代码生成 1.4MB 单 HTML；一次性 demo 无沉淀 | 互链 [全景 note §3.4](../2026-11-13-interest-landscape-and-redefinition.md)（镜像一手已核） |
| **WROP（生成器当数据工厂）** | 2026-09：150 Blender 生成器 × 1 万样本 = 150 万训练语料 + 300 考卷 + 14 模型 4197 答卷全开源；CC BY-NC 4.0 | 本轮已核（HF dataset card 全文 + 论文摘要/部分正文，§3.1） |

**定位一句话**：规则图/参数化/依赖更新是**成熟工业机制**（三家同构）；AI 写生成器已被 Saltline 证明可行但无资产化沉淀；生成器当数据工厂已被 WROP 证明可规模化但生成器本身不在契约管理下、无位级复现承诺（[六方向 note §3 打折表](../2026-11-13-six-direction-innovation-integration.md) 既有结论，本轮一手复核后仍成立）。

---

## 3. 关键文献精读

### 3.1 WROP：Training Object Permanence in World Models（arXiv 2609.28654，2026-09-23）

来源：[HF dataset card](https://huggingface.co/datasets/Hokin/object-permanence-benchmark)（已核检索全文）+ [HF papers 摘要](https://huggingface.co/papers/2609.28654)（已核）+ [HyperAI 论文正文节选](https://hyper.ai/en/papers/2609.28654/pdf)（已核部分正文）；细节补充经 [Emergent Mind 第三方精读](https://www.emergentmind.com/papers/2609.28654)（**转述**，逐条标注）。

- **问题**：视频生成模型（世界模型）是否具备物体恒存性（OP）与坚固性（OS）？若无，能否用认知科学启发的合成数据训练出来？
- **方法**：150 个手工设计的 Blender 生成器，分六认知家族（OP：动态遮挡/静态场景遮挡/容器恒存；OS：阻挡/支撑移除/碰撞）。每生成器**结构参数**（数量/几何/轨迹/遮挡配置/孔径/接触时机）与**干扰参数**（光照/颜色/材质/相机/速度）独立随机化，防视觉捷径；每任务 1 万+ 样本 → 150 万训练语料。每样本五件套契约：`input_video.mp4`（60 帧）+ `target_video.mp4`（60 帧）+ `prompt.txt` + **`trajectory.npz`（逐帧逐物体真值轨迹）** + `metadata.json`（场景状态元数据）；1280×720、24fps。固定 300 题考卷（每题 2 样本），14 模型（4 continuation / 3 reference-to-video / 7 edit）共 4197 答卷；20 名评分者盲测成对比较 → Bradley-Terry Elo。另微调 PWM-WROP（16B，自 Cosmos3-Nano 初始化，架构/tokenizer 不变），训练栈 PWM 为 Trainium2 原生 PyTorch，全开源。
- **关键结果**（论文口径，已核摘要+正文节选）：PWM-WROP Elo 1679.5，continuation 类第一、总榜第三（次于两个统计并列的 reference-to-video 商业系统 1723.6）；原生输出仅 320×192，同分辨率下 LPIPS/MS-SSIM 最优。家族级不均匀：遮挡/身份保持提升最大，碰撞/接触动力学仍弱（论文自认）。
- **局限**：①轨迹是 **Blender 关键帧解析式 authored，不是刚体仿真**——物理正确性押在生成器实现与人工检查上（转述：Emergent Mind 精读；与 dataset card"hand-designed"口径一致）；②非受控对比——PWM-WROP 与基线架构/分辨率/接口全不同，Elo 差不能归因于数据 alone（论文自认，已核节选）；③人评稀疏（361 场跨模型比较），家族级置信区间宽；④对自然视频/复杂场景的迁移未证；⑤**许可 CC BY-NC 4.0**（dataset card 原句已核：署名 Hokin Deng，仅非商用，商用另联系）。
- **与 OmniWorld 的关联**：这是"程序化生成器当数据工厂"的最完整公开实证——生成器同源产出视觉+真值轨迹+评分元数据，考卷契约（五件套）就是生成器契约的一个已落地样本。它**没有**的：生成器本身的版本/契约管理、真值与信念分离、运行时闭环、位级复现承诺（与 [六方向 note §3](../2026-11-13-six-direction-innovation-integration.md) 打折表结论一致，本轮升级为一手复核）。其"人评+像素指标"评分与仓库"逐字段对真值评分"是两条测量路线；论文开放问题原文承认"基于状态（轨迹/身份/接触事件/不变量）的显式评估器是否更优"仍未解（转述：Emergent Mind）——这正是引擎测量层的关联位。NC 许可决定其只能作机制参照与非商用研究素材（§6）。

### 3.2 UE PCG 官方文档（Overview + Electric Dreams 术语集）

来源：[PCG Overview（UE 5.8 文档）](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-overview)（已核检索全文）、[Electric Dreams PCG 术语集（UE 5.2 文档）](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-in-electric-dreams?application_version=5.2)（已核检索节选）。

- **问题**：如何让技术美术在引擎内构建"从资产工具到整个世界"的可迭代程序化内容，且与手工管线无缝混编。
- **方法**：点云为中心的 dataflow 图（§1.3 机制）；密度作为一等公民（存在概率场，可被噪声/纹理/距离调制）；属性+domain 系统承载语义；Graph Parameters/Instance/Subgraph 承载参数化复用；分层 seed 派生承载确定性；World Partition 集成承载规模。
- **关键结果**：编辑器实时增量更新；官方森林教程从 Landscape 采样到加权 mesh  spawning 全程图内完成；Level to PCG 工具把手工关卡可逆地转回点云配方（手工→程序化的双向桥）。
- **局限**：官方定位为内容生成工具set，非 gameplay 逻辑载体（第三方指南口径，转述）；深度绑定 UE 资产/Actor 体系，图不可脱离引擎复用；文档未给位级复现承诺（seed 派生机制存在，但跨平台确定性无官方声明——**未核/无声明**）。
- **与 OmniWorld 的关联**：规则图机制的最完整工业参照——"参数→点（带语义属性）→资产"与生成器契约"参数→几何+碰撞+语义"同形；分层 seed、Sanity Check 验收节点、手工编辑与程序化混编（5.8 非破坏编辑，转述未核）都是可借鉴的机制点位。引擎不重造 UE（既有让渡边界），关联仅在机制参照与 L2 生成器库的设计输入。

### 3.3 SBPCG 综述：The Quest for Content（arXiv 2311.04710，2023-11）

来源：[arXiv HTML](https://arxiv.org/html/2311.04710v1)（已核检索部分正文；编号经本轮搜索确认存在）。

- **问题**：2011 年 Togelius 分类学之后，search-based PCG 十二年（2011–2022）走到哪了。
- **方法**：Scopus 系统检索 + 三阶段筛选 + 滚雪球 + 作者反馈，498 篇筛出 118 篇；分类学融合 Togelius 2011 与 Hendrikx 2013（六层内容：bits/space/systems/scenarios/design/derived），再按近十年实际工作细分（levels→timeline/room/dungeon 等）。
- **关键结果**（已核节选）：levels 与 indoor maps 两类工作量之和超过其余全部；buildings/road networks/urban environments 等类型十年间无人用 SBPCG/PCGML 做，仍由传统方法（噪声/分形/L-system）承担——**方法家族按内容层分工，不是代际替代**；作者推测约束手写成本把研究者推向了 PCGML。
- **局限**：只覆盖 search-based（明确排除 neuroevolution 等混合），不代表 PCG 全貌；2022 年截止，未覆盖 diffusion/LLM 时代。
- **与 OmniWorld 的关联**：为 L2 生成器库提供"按内容层选方法家族"的地图（§1.2 表的学术出处）；其反复强调的"评估是最难问题、fitness 编码盲区"与仓库 E1 指标教训（指标会骗人，互链 [六方向 note §1 D2](../2026-11-13-six-direction-innovation-integration.md)）同构——生成器验收器设计可直接继承该问题意识。

---

## 4. 开放问题

1. **位级复现的边界画在哪**：同机同工具链位级复现（参考模式已有）与跨平台位级复现（浮点陷阱，§1.4）成本差一个量级；生成器契约的"可复现"承诺应指哪一级——属方向评审待裁决项，本文不裁。
2. **AI 写生成器的验收标准**：Saltline 失效清单（穿模/法线/碰撞不一致/比例失真）可转验收项，但规则检查器的覆盖面与漏检率无公开基准；"生成器正确"与"产物正确"是两层验收。
3. **置信度语义从何而来**：解析式生成器的真值按定义为真（confidence=1？），AI mesh/AI 写代码的不确定性如何进入契约⑦——工业三家与 USD 均无对应物（§1.3/§1.6），是纯增量也是纯空白。
4. **生成器版本演化**：参数 schema 变更、旧 seed 产物失效、HDA 式"全实例更新"在评测语境下意味着旧考卷/旧 episode 是否作废——版本化契约需回答。
5. **碰撞+语义的一体化生成无通用标准**：SimReady 绑定 NVIDIA 生态（互链既有结论）；开源侧 WROP 只到轨迹+元数据，不到碰撞体——通用生成器契约的该层无现成参照。
6. **WROP 类 NC 资产的复用形态**：机制可参照、数据不可入商用交付；若用于非商用评测素材，署名与分发条款细节需逐文件核（§6）。
7. **AI mesh 与生成器的接缝**：静态 prop 的身份/碰撞由谁铸造（生成器侧 or 导入侧）——与引擎"身份归运行时铸造"的既有语义如何对接，属 L2 设计输入。

---

## 5. 仓库已有资产引用

| 路径（相对 docs/17-research/） | 可复用结论（只链接不复述） |
|---|---|
| [2026-11-13-interest-landscape-and-redefinition.md](../2026-11-13-interest-landscape-and-redefinition.md) §3.4 | Saltline 一手拆解：单 prompt 8h / Three.js r180 / 全代码生成 / 几猫换皮证据 / "一次性 demo 无沉淀"教训；§3.1 SimReady 已核；§2 参考模式位级 oracle |
| [2026-11-13-six-direction-innovation-integration.md](../2026-11-13-six-direction-innovation-integration.md) §1 D4 / §3 / §5 | 生成器契约表述（参数→几何+碰撞+语义+置信度+版本）；"生成器当资产"新颖性打折表（WROP/UE PCG 已占用部分）；首战 B' 真值世界工厂提案；8GB 可行性结构判断 |
| [projects/openusd.md](../projects/openusd.md) | TOST 1.0 许可、Core Spec 1.0、官方边界（非 rigging/非事件驱动）、单向导出既有裁定 |
| [deep/2026-11-05-openusd.md](../deep/2026-11-05-openusd.md) | LIVERPS/prim index/change processing 机制全文；"不是运行时"四条机制原因；ownership/confidence 无对应物；Edit Target 与 session layer 的可借鉴概念 |
| [2026-10-27-character-animation-route.md](../2026-10-27-character-animation-route.md) §6 | 运动数据层=唯一长期资产，与 World Contract 同构（identity/time/ownership/confidence）——L2 动画座位的契约先例；§5 Blender 作为外部工具不触发 GPL 链接红线 |

---

## 6. 待核验清单

1. **WROP 许可细节**：dataset card 的 CC BY-NC 4.0（署名 Hokin Deng、非商用、商用联系邮箱）**已核**；但 150 万训练语料仓、生成器代码仓、PWM-WROP 权重仓的各自 LICENSE **未逐页核**（第三方称语料约 214GB、权重 BF16 15B——转述未核，与论文"16B"口径存在出入，引用前须以 model card 为准）。
2. **生成器位级复现的浮点陷阱**：Gaffer《Floating Point Determinism》正文未抓取（仅确认链接存在）；Blender/UE 生成器同 seed 跨机位级比对**无实测**；仓库侧平行证据（MJWarp 非确定）互链未在本轮重核。
3. **RTX 4060 8GB 下程序化生成完全可行的依据**：目前是**结构论证**（程序化几何/材质为 CPU/轻 GPU 计算、无大模型依赖，[六方向 note §1 D4](../2026-11-13-six-direction-innovation-integration.md) 既有判断）+ Saltline 在浏览器跑通的旁证，**本机未实测**；Hunyuan3D shape 6GB 为官方口径未实测，texture 16GB 官方口径超 8GB。
4. **UE PCG 版本节点**："5.7 production-ready、5.8 非破坏编辑/GPU scatter"来自第三方引擎参考文档（转述），官方 release notes 未逐页核。
5. **Tripo 及 2026 年 AI 3D SOTA 排位**：未核，本文不作主张。
6. **WFC 原始仓库（Gumin 2016）**：机制描述为领域通识转述，原始仓库未逐页核。
7. **Houdini 依赖传播细节**：cooking 定义已核官方；dirty 传播具体规则引自官方论坛工程师帖（转述），官方文档专页未逐页核。

---

## 7. 来源与核验状态

| # | 来源 URL | 关键内容 | 状态 |
|---|---|---|---|
| 1 | https://huggingface.co/datasets/Hokin/object-permanence-benchmark | WROP 300 考卷/五件套契约/trajectory.npz/14 模型 4197 答卷/CC BY-NC 4.0 原文/引用 arXiv 2609.28654 | **已核**（检索全文，一手 dataset card） |
| 2 | https://huggingface.co/papers/2609.28654 | WROP 论文摘要：150 生成器/1.5M 语料/PWM-WROP 16B/Elo 排名 | **已核**（摘要检索全文） |
| 3 | https://hyper.ai/en/papers/2609.28654/pdf | 论文正文节选：六家族/60+60 帧协议/320×192/人评方法 | **已核**（部分正文；全文未逐页） |
| 4 | https://www.emergentmind.com/papers/2609.28654 | 关键帧非仿真/非受控对比/家族级结果/开放问题 | **转述**（第三方精读，与已核摘要一致处采信，其余标注） |
| 5 | https://groundtruth.day/news/wrop-object-permanence-world-models.html | 语料 214GB/权重 15B BF16/90 vs 60 生成器分族 | **转述未核**（数字与论文口径有出入，不作依据） |
| 6 | https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-overview | UE PCG 点/密度/属性/参数/实例/World Partition/Sanity Check | **已核**（检索全文，官方） |
| 7 | https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-in-electric-dreams?application_version=5.2 | point seed=位置+节点 seed+组件 seed；Level to PCG；assembly | **已核**（检索节选，官方） |
| 8 | https://github.com/donchitos/claude-code-game-studios/blob/abc0de79d6ceb0b5d7aa693945849b3937afbe6a/docs/engine-reference/unreal/plugins/pcg.md | PCG 5.7 production-ready/5.8 新特性 | **转述未核**（第三方，官方 release notes 未逐页） |
| 9 | https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/geometry_nodes.html | GN modifier/fields/输出属性/Bake/资产化 | **已核**（检索全文，官方手册） |
| 10 | https://www.sidefx.com/docs/houdini/basics/cooking.html | cooking=求值节点网络 | **已核**（检索节选，官方） |
| 11 | https://www.sidefx.com/media/uploads/products/engine/hengine_games.pdf | HDA 封装/Engine 宿主内 cook/改核心资产全实例更新/批处理 | **已核**（检索全文，官方 PDF） |
| 12 | https://www.sidefx.com/ja/docs/houdini19.0/tops/intro.html | PDG work items/依赖图/最小重 cook/wedge 扇出/农场调度 | **已核**（检索全文，官方日文文档） |
| 13 | https://github.com/Tencent-Hunyuan/Hunyuan3D-2/blob/main/README_zh_cn.md | 两阶段 DiT+Paint/开源权重/官方 BibTeX arXiv 2501.12202 | **已核**（检索全文，官方仓库） |
| 14 | https://github.com/vanheton/Hunyuan3D-2 | shape 6GB / shape+texture 16GB VRAM 官方 README 镜像/变体清单 | **已核**（检索全文；镜像与官方互证） |
| 15 | https://arxiv.org/html/2311.04710v1 | SBPCG 综述：118 篇/分类学/方法家族分工结论 | **已核**（检索部分正文；编号确认存在） |
| 16 | https://gafferongames.com/post/deterministic_lockstep/ | 位级确定性定义/ODE 内部 RNG 案例/跨编译器 OS 架构不保证/固定步长前提 | **已核**（webfetch 全文） |
| 17 | https://gafferongames.com/post/floating_point_determinism/ | 浮点确定性专文 | **未核**（仅确认链接存在） |
| 18 | 仓库内部：全景 note / 六方向 note / openusd 卡+深报 / 动画 note | Saltline/SimReady/USD 机制/运动数据层等既有已核结论 | **已核**（本轮只读复用，互链不复制） |

**arXiv 编号核验记录**：2609.28654（WROP，经 HF dataset card 官方引用 + HF papers 页确认）；2501.12202（Hunyuan3D 2.0，经腾讯官方仓库 BibTeX 确认）；2311.04710（SBPCG 综述，经 arxiv.org 正文页确认）。本文未引用任何未经确认的编号。