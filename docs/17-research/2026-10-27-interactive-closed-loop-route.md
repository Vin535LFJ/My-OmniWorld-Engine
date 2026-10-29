# 交互闭环路线研究笔记：物理仿真 × 世界模型 × 动作策略 × 执行接口（2026-10-27）

> **性质：Research 笔记，不是计划、不是切片、不改路线图。** 按 [`research-queue.md`](research-queue.md) 的路由规则（Research → Exploration → Spike/PoC → Benchmark → Architecture Review → ADR → Core），本笔记只把一个话题从"想法"推进到"有出处 + 有实验证据"，**不构成任何架构承诺**。
> **触发**：owner 2026-10-27 提出——"把 MuJoCo / Isaac Lab 的物理仿真、Cosmos 的世界模型、LeRobot / VLA 的动作策略、ROS 2 的执行接口联系起来，研究的不只是能生成视频或回答指令的模型，而是能预测环境、制定动作、执行任务并根据反馈持续调整的交互式系统"。
> **本笔记不新增任何产品路径依赖**：唯一新增文件是 `tools/stale_frames.mjs`（一个实验脚本，不进产品路径），实验用到的数据、面板与许可结论沿用 D1/D3 已核验的成果。
> **上承**：PLAN §一/§二/§三/§五/§六 · L0 §2（World Contract 权威文本）· ADR-0011（契约后视镜）· ADR-0013（评分空间）· 阶段 D L1 与 D3 报告。

---

## 0. 结论摘要（先看这七条）

1. **这条路在仓库里已经有座位，不需要新开方向。** L0 §2 写着 "Action 显式预留"（[00-总体设计.md:62](../../docs/design/00-总体设计.md#L62)，ADR-0011 C3 裁定"显式延后"）；PLAN §六 Phase 4 就是「世界模型/智能体应用 · 最小 Agent 契约」（[PLAN.md:79](../../PLAN.md#L79)）；PLAN §二 ② 把 CARLA/Isaac、Brain/VLA、ROS 2 列为"绕着支点长回来"的 Adapter，回归条件是"某 phase 的 benchmark 需要它即回归"（[PLAN.md:24](../../PLAN.md#L24)）。
2. **真正的结构问题不是"能不能接上去"，而是"闭环里谁写动力学"。** 契约 ④⑤ 规定真值侧唯一作者、非拥有者只能发 Proposal、信念永不回写真值（[00-总体设计.md:55-56](../../docs/design/00-总体设计.md#L55)）。**如果 MuJoCo 只作为"参与者"接入，它推不动世界——闭环断在最后一米。** 本笔记的结论是：**闭环 = 引擎 ↔ 仿真器，引擎不写仿真器**（§2）。
3. **D3 已经把这条路的一半铺好了。** `TruthWorld::load_annotations`（[src/truth_world/truth_world.cpp:29](../../src/truth_world/truth_world.cpp#L29)）的通用形状就是"外部真值权威 → 镜像进 canonical 状态 → 全员对它评分"，而且真值世界仍是唯一作者（`friend class TruthWorld` + 私有 `reset_scene`，[world_state.hpp:34-44](../../src/world_state/world_state.hpp#L34)）。物理仿真器要接的位置，是这个函数的姐妹版本，不是新接口。
4. **有一个当下就能测、零核心改动的量**：观察时滞 → 信念误差。§6 给了在 D3 面板上的实测（lag 0/1/2/4/8/16，200 帧真实照片 + 真实标注，**零核心改动**）。**结果同时暴露了两个指标缺陷**：时滞让"信念覆盖率"掉 31%、让检测器 ECE 恶化 48%，但 `divergence` 不涨反跌、`identity_stability` 反而上升——**现有三个指标里有两个会把退化的系统排得更好**（§6.6）。这直接决定 Phase 4 的指标必须重做。
5. **算力是硬墙，且是可引用的硬数字**：Cosmos-Predict2-2B-Video2World 官方模型卡写明 **需要 32.54 GB 显存**（[HF 模型卡](https://huggingface.co/nvidia/Cosmos-Predict2-2B-Video2World)，2026-10-27 查）；14B 更大。本机 RTX 4060 Laptop **8.00 GiB**（D1 实测）⇒ **Cosmos 在本机不可跑**，它只能是**离线数据生产者**（在别处出帧，仓库只进清单 + 许可记录，与 D1 对数据集的处理方式一致）。
6. **许可分档（一手来源，2026-10-27 查）**：MuJoCo / LeRobot / ROS 2 = **Apache-2.0**（可进产品路径）；Isaac Lab = **BSD-3-Clause 但依赖 Isaac Sim（Omniverse 私有许可、不可再分发）** ⇒ 只能当本地研究工具，不能进产品路径；Cosmos = **NVIDIA Open Model License**（允许商用与派生模型，但不是开源许可）。详见 §5。
7. **有一处必须先修的结构缺口**：世界模型接缝今天拿到的是**真值状态**——`WorldModelAdapter::predict(Tick, const WorldState&)`（[src/world_model/world_model.hpp:14](../../src/world_model/world_model.hpp#L14)），`zero_delta` 直接遍历 `ws.entities()`（[src/world_model/zero_delta.cpp:14](../../src/world_model/zero_delta.cpp#L14)）。交互闭环里策略必须基于**信念**而不是真值，否则闭环自证。D3 已经为渲染器立了同类规矩（`--annotations requires --frames`，理由写在 [driver.cpp:438-446](../../src/loop/driver.cpp#L438)）。详见 §4。

---

## 1. 四个部件 ↔ 仓库接缝对照

| 外部部件 | 仓库里对应的位置 | 现状 | 缺什么 |
|---|---|---|---|
| 物理仿真（MuJoCo / Isaac） | L0 容器图里的 `future` 适配器槽位（"仿真器 / 渲染器 / 视频解码"，[00-总体设计.md:92](../../docs/design/00-总体设计.md#L92)）；真值侧 = D3 的 `load_annotations` 同构位 | 无任何仿真代码；`WorldState::step` 只是匀速积分 + 墙反射（[world_state.hpp:37](../../src/world_state/world_state.hpp#L37)） | 动力学、碰撞、动作回灌、任务定义 |
| 世界模型（Cosmos / 第三方） | `WorldModelAdapter` 接缝：`predict(Tick, const WorldState&) → Proposal[]`，预测走**同一条**裁决路径（[world_model.hpp:14](../../src/world_model/world_model.hpp#L14)） | 两个玩具模型（`zero_delta` / `linear_model`）；真实世界模型接缝**未验证** | 输入给它的应该是信念而非真值（§4）；`Proposal` 只能断言 transform + class_tag，装不下像素/占据图（[proposal.hpp:16](../../src/core/proposal.hpp#L16)） |
| 动作策略（LeRobot / VLA） | **不存在**。`src/` 全树 grep `action|policy|actuat|command` 只命中 `BoundsPolicy` 与注释 | L0 §2 预留、ADR-0011 C3 显式延后，**零实现** | Action schema、动作空间、任务语义、策略的输入接口 |
| 执行接口（ROS 2） | PLAN §二 ② 的 Adapter 位 | 无；PLAN §五 把 ROS 2/VLA/LLM 列为 **BACKLOG（不做）**（[PLAN.md:66](../../PLAN.md#L66)） | 它是**传输层**，真实需求出现在 D5 的零拷贝 IPC，不是研究贡献本身 |

**一个必须说清的现实**：这条路线会碰到 PLAN §五 BACKLOG 里的条目（ROS 2 / VLA / LLM）。按 PLAN §六 与路线规则，**把条目从 BACKLOG 移进某个 phase 需要 Gate/ADR，不是"想研究就能研究"**。本笔记只提供移位所需的证据，不代替那个决定。

---

## 2. 闭环的所有权模型：引擎 ↔ 仿真器

### 2.1 约束推导

契约 ④：每个字段类只有一个作者，v1 = 真值世界独占运动学与类别，其他参与者**结构性无写接口**。契约 ⑤：非拥有者的唯一写路径是 Proposal 五元组 → 裁决 → 信念账本留痕，**信念永不回写真值**。

因此把一个机器人/机械臂/仿真体放进闭环时，"谁推进物理状态"只有三种可能：

| 方案 | 动力学作者 | 引擎的接口 | 契约影响 | 评价 |
|---|---|---|---|---|
| (a) 仿真器作为**参与者** | 引擎（真值世界） | 仿真器发 Proposal 描述"物体会怎么动" | 无 | **闭环断**：动作无法进入动力学，仿真器只能描述、不能驱动 |
| (b) 仿真器作为**真值权威** | 仿真器 | 引擎读仿真器状态，镜像进 `WorldState`，全员对它评分 | 需要"外部真值源"这条通道（**D3 已有**） | ✅ 本笔记采纳 |
| (c) 混合（引擎 + 仿真器各写一部分字段） | 两方 | 两个作者 | **违反 ④**（一个字段类两个作者） | ❌ 否决 |

### 2.2 采纳 (b)，且它与 D3 同构

D3 已经把"外部真值权威"做成了工程形状：`load_annotations(WorldState&, const AnnotationSet&, Tick)` 读一份外部声明（标注），把场景替换进 canonical 状态，**id 仍单调不复用**（契约 ②），`boundary_events` 保持累计语义（契约 ⑨），真值侧仍然只有 `TruthWorld` 一个作者。物理仿真器要接的就是同一个形状：

```
仿真器（动力学权威）──读──▶ 引擎：真值镜像 + 观测 + 提议裁决 + 账本 + 指标
       ▲                                    │
       └────────── 动作（出向，信念→意图）─┘
```

**动作是出向的，不是真值写。** 契约 ④⑤ 一个字都不用改：引擎不新增真值写接口，信念不覆盖真值，引擎对"世界怎么动"没有发言权——它只对"谁在说谎、说得多自信、闭环慢多少"负责。

### 2.3 这条路线的真正研究贡献在哪

不是"我们也接了 MuJoCo"。是三件现在没有别人做好测的事：

1. **闭环误差的可复现测量**：逐字节确定性 + tick 账本 + 逐 tick 日志 ⇒ 同一策略、同一环境、同 seed ⇒ 逐位相同的 episode。当前 VLA 评测普遍做不到（训练随机性、环境随机性、异步推理混在一起）。
2. **闭环延迟预算**：契约 ⑨ 已经逐阶段计量（render / perceive / adjudicate / metrics）。闭环需要的是 `perceive → decide → act → observe` 的**端到端**分解，D5 的 GPU 数据流正是它的分母（12.81 GB/s H2D 实测见 D1）。
3. **信念的校准与信用分配**：现有 ECE 量的是"自报把握 vs 实际正确率"，闭环里它会自然延伸到"策略的自报把握 vs 动作后果"。`BeliefEntry` 已经把提议和裁决结果一起留痕（[ledger.hpp:13](../../src/belief/ledger.hpp#L13)），是现成的信用分配底座。

这正好落在 PLAN §三 注册的期权分支上：**"评测交互式世界模型时的默认接入路径——'MLPerf 之于测量'"**（[PLAN.md:50](../../PLAN.md#L50)）。也就是说，owner 的方向与已注册期权是同一件事的两种说法，**触发条件是"外部兴趣信号出现"**——而研究队列里已经在盯两个信号源（WorldFoundry 面向 interactive-worlds 评测的更新、cosmos-evaluator，[research-queue.md:9](../../docs/17-research/research-queue.md#L9)、[:19](../../docs/17-research/research-queue.md#L19)）。

---

## 3. Action 契约：现在到底缺什么

### 3.1 现状

- L0 §2：**"Action：显式预留（✅ 后视镜裁定）——智能体写路径未定，预留，usage-driven，等真实智能体参与者出现才设计；v0.1 的 schema 级 Action 不继承。"**（[00-总体设计.md:62](../../docs/design/00-总体设计.md#L62)）
- ADR-0011 C3：**"Action（智能体写路径）→ 显式延后，契约留'未定，预留'"**（[0011:17](../../docs/adr/0011-contract-rear-mirror-delta-review.md#L17)）
- 代码：`src/` 全树**零** Action / policy / actuation 实现。

### 3.2 Proposal 五元组能不能装 Action

不能，但**能装"意图"**。差别是关键的：

| | Proposal | Action 需要的东西 |
|---|---|---|
| 主体 | 参与者对**世界状态**的断言 | 参与者对**世界**的请求 |
| 目标 | `target`：哪个实体 | 目标可以是实体，也可以是"没有目标"（探索） |
| 内容 | `fields`：transform / class_tag | 控制量：关节角、速度、夹爪、持续时长、时序 |
| 置信度 | 自报"这条主张正确的概率"（契约 ⑦，零裁决权力） | 自报"这个动作会成功"的概率——**语义不同**，混用会污染 ECE |
| 方向 | 入向（写进信念层） | 出向（兑现到环境） |

把动作塞进 Proposal 会同时破坏两件事：契约 ⑤"信念永不回写真值"的语义边界（动作不是信念），以及 ECE 的可解释性（两种自报概率混在一个 bin 里）。

### 3.3 三个候选与取舍

| 候选 | 形状 | 优点 | 缺点 |
|---|---|---|---|
| **A. Action 成为契约第十点** | 新增字段类 + 写路径 | 契约完备 | **过早**：usage-driven 规则说"等真实消费者出现才设计"；现在设计 = 凭空造语义，且要过 ADR |
| **B. Agent Adapter 家族（建议）** | 新参与者家族 `observe(...) → {Proposal, Action}`；Action 走**独立的出向通道**（Action Channel），不进信念层 | 契约 ①②③⑤ 不动；动作与信念语义分离；ECE 干净 | 需要定义 Action Channel 的接口与失败语义 |
| **C. 动作当作 Proposal 的特例** | `ProposedFields` 加 `action` 变体 | 改动最小 | 污染置信度语义与账本；否决（见 §3.2） |

**建议 B**，且**不占契约第十点**：动作是**参与者的一种输出**，不是世界状态的一种字段。L0 的契约讲的是"世界数据谁写"，动作不写世界数据，它请求世界动。这条建议要等第一个真实智能体消费者出现（一个 MuJoCo 任务 + 一个策略）才值得落成 ADR——正是 ADR-0011 说的 usage-driven。

### 3.4 Action 契约要回答的清单（留给 Phase 4 的 L1）

1. 动作的作用域：世界坐标 / 关节空间 / 任务空间（"抓杯子"）？
2. 时序：持续 tick 数？是否可中断？
3. 失败语义：动作被拒绝 / 越界 / 超时，账本里怎么记？（现有 `AdjudicationResult` 有 `reason`，可直接借用形状）
4. 自报把握的语义：动作成功率 vs 状态主张正确率——**必须分两个 producer**，否则 ECE 失真。
5. 观测侧：策略看到什么？见 §4。
6. 任务与评分：成功/失败由谁定义？

---

## 4. 世界模型接缝的输入问题（真值 vs 信念）

**代码事实**：`LoopDriver::run` 把 `ws`（canonical 真值状态）交给世界模型：`m->predict(t, ws)`（[driver.cpp:560](../../src/loop/driver.cpp#L560)）。`zero_delta` 因此"知道"每个实体的精确位置。

**为什么在闭环里这是错的**：真实交互系统里，世界模型/策略拿到的是**它自己观测到的东西**，不是真值。若策略直接读真值，闭环就是自证的——和 D3 里"渲染器画出真值所命名的实体"是同一种病（D3 的护栏是 `--annotations requires --frames`，[driver.cpp:442](../../src/loop/driver.cpp#L442)）。

**三种修法**：

| 修法 | 做法 | 取舍 |
|---|---|---|
| (a) **信念视图**（建议） | 新增 `BeliefView`（信念账本的投影：按实体聚合的最近主张 + 置信度），世界模型与未来的 Agent Adapter 读它 | 加法式（不动现有签名，参考 ADR-0013 的先例）；需要定义投影规则（冲突怎么办——契约 ⑥ v1 只记录不仲裁，所以投影要显式声明"取最近/取最高把握/全给"） |
| (b) 滤波后的信念 | 在信念之上做时序滤波（跟踪 + 平滑） | 更接近真实系统，但引入状态、破坏"参考模式逐字节复现"的简单性，且要新的指标 |
| (c) 保持现状 + 禁止闭环用法 | 世界模型仍读真值，但闭环实验必须显式声明"世界模型是神谕" | 零成本，但**污染所有闭环实验的语义**——E1 的 §6 就会踩这个坑（见 §6.4） |

**建议 (a)**，且作为 Phase 4 的第一个切片（它比 Action 契约更小、更独立、可单独验证）。

---

## 5. 许可与算力核验（一手来源，2026-10-27 查）

| 部件 | 许可 / 约束 | 来源 | 能否进产品路径 |
|---|---|---|---|
| MuJoCo | **Apache License 2.0** | [LICENSE](https://raw.githubusercontent.com/google-deepmind/mujoco/master/LICENSE) | ✅ 可以（Apache-2.0，附声明义务） |
| Isaac Lab | 框架 **BSD-3-Clause**（部分 Apache-2.0）；但依赖 **Isaac Sim**，后者走 Omniverse 私有许可、**限制随应用分发** | [Isaac Lab License 文档](https://docs.nvidia.com/isaaclab/references/license)、[分发讨论](https://forums.developer.nvidia.com/t/licensing-when-distributing-omniverse/371343) | ⚠️ **只能当本地研究工具**，不能进产品路径（不可再分发） |
| Cosmos-Predict2 | **NVIDIA Open Model License**（允许商用、允许派生模型、NVIDIA 不主张输出所有权）；规模 **2B / 14B**；2B Video2World 模型卡写明 **需 32.54 GB 显存**，BF16 | [HF 模型卡](https://huggingface.co/nvidia/Cosmos-Predict2-2B-Video2World) | ⚠️ 许可可商用但**非开源**；**本机 8 GiB 跑不动** ⇒ 只能离线生产帧数据 |
| LeRobot | **Apache-2.0** | [LICENSE](https://raw.githubusercontent.com/huggingface/lerobot/main/LICENSE) | ✅ 代码可以；**数据集逐个看许可**（SmolVLA 明确只用 compatibly licensed 的社区数据集） |
| SmolVLA | 450M 参数，Hugging Face 开源发布，宣称消费级硬件可跑 | [HF blog](https://huggingface.co/blog/smolvla) | ✅ 代码/权重可用（须逐模型查许可） |
| ROS 2（rclcpp） | **Apache-2.0** | [LICENSE](https://raw.githubusercontent.com/ros2/rclcpp/master/LICENSE) | ✅ 可以；但工作量见 PLAN §二 ② 的"≥6 个月" |

**与本仓库红线的关系**：AGPL/GPL 红线不涉及以上任何一项（无 AGPL、无 GPL、无无许可组件）。真正的门槛是**不可再分发**（Isaac Sim）与**算力**（Cosmos）。

**Cosmos 的定位结论**：在本机不可跑 ⇒ 若要用，走"**离线生产帧数据**"路线：别处生成 → 按 D1 的纪律只入库脚本 + 固定清单 + SHA256 + 许可记录，不入库数据本身。这也正好是 §6 实验要用的接入方式。

---

## 6. 实验 E1：观察时滞 → 信念误差（本笔记的实测证据）

### 6.1 为什么先做这个实验

在谈 Cosmos / VLA / MuJoCo 之前，有一个量必须先有：**当输入不是"当下"时，信念账本会错到什么程度**。它有三个用处：

1. 给"闭环里预测延迟值不值"一个可比的标尺（比"感觉有点慢"强）。
2. 检验现有指标在闭环语境下是否还成立（**结果：部分不成立**，见 §6.4）。
3. 它是任何真实世界模型的**零成本对照**：一个"完美预测器"必须复现 lag-0 的数字。

### 6.2 方法（零核心改动）

- 帧清单（`frames.txt`）就是"tick → 文件"的列表，**时滞注入 = 把列表整体平移**。新脚本 [`tools/stale_frames.mjs`](../../tools/stale_frames.mjs) 只重排路径，不碰像素、不碰引擎。
- lag L 的定义：**tick t 收到的观测是 L tick 之前的帧**（头不足则取最早一帧）。真值侧不动（标注清单仍描述 tick t）⇒ 度量的是输入时滞，不是真值漂移。
- **lag 0 = 完美预测器**（观测即当下）；**lag 1 = "场景不变"型预测器**（persistence：预测 t 就是 t-1 那一帧）。
- 面板、数据、许可沿用 D3：COCO val2017 子集 200 张，图像许可准入 id ∈ {4,7,8}（CC-BY / 无已知版权限制 / 美国政府作品），标注 CC BY 4.0；数据不入库。
- Release ON 构建 `build-ort-c5/`（ORT **CPU EP**，本实验不使用 GPU），`--adapters rtdetr_v2_s --mode reference --ep cpu --ticks 200`。
- **三组配置**（`--models` 决定世界模型集合，**三组必须分开读**）：

| 系列 | `--models` | 含义 |
|---|---|---|
| **S1 面板对照** | `zero_delta` | 与 D3 发布的面板配置**逐字一致**（`applied=1527 / announced=702 / divergence=0.010747`）⇒ 本系列的 lag 0 数字即 D3 的数字 |
| **S2 CLI 默认** | 省略 | ⚠️ **省略 `--models` 不等于关闭世界模型**：CLI 默认是 `{zero_delta, linear_extrapolation}`（[driver.cpp:263](../../src/loop/driver.cpp#L263)）。这一组是多出**一个**读真值的线性外推模型 |
| **S3 纯感知** | `""`（空列表） | 只剩检测器。`--models ""` 是唯一能表达"无世界模型"的方式（解析器会先 `clear()` 再按逗号切分，[driver.cpp:311-324](../../src/loop/driver.cpp#L311)） |

- **为什么必须分三组**：S1/S2 里的世界模型**读真值**（`zero_delta` 直接遍历 `ws.entities()`，[zero_delta.cpp:14](../../src/world_model/zero_delta.cpp#L14)），它们的主张几乎与真值重合，于是**把 divergence 稀释到接近 0**。S1 在 200 帧上共有 2 229 条账本记录，其中 **2 438/3 448 条（71%）来自读真值的模型**（S2 计数），检测器只贡献 1 010 条。**在闭环语境下，"平均匹配误差"这个指标会被神谕项吃掉**——这是 E1 的第二个方法论发现（§6.4 第 7 点）。


### 6.3 结果

见 §6.5 的表（数据由本节末尾的复现命令产出，artifact 在 `data/panel/lag/`，**不入库**）。

### 6.4 读法与限制（先看限制）

1. **这不是生成式世界模型的评测。** 像素是数据集自己的；只改了它们在时间上的位置。真实预测器（Cosmos 等）会在此基础上再叠加"生成误差"。
2. **这不是闭环任务评测。** 没有动作、没有任务、没有成功率——闭环指标是缺口（§7）。
3. **面板不能表达"同一个场景晚一点的样子"。** COCO 的 200 张是**互不相干的独立照片**（这也是 `identity_stability` 恒为 0 的同一个原因）。所以 lag ≥ 1 并不是"稍微过期的同一场景"，而是**一张完全无关的照片**。⇒ **这条曲线给的是上界（最坏情况），不是"预测质量 → 误差"的梯度。** 真正的梯度需要**同一场景的时序数据**（行车记录仪 / 机器人录像），本仓库目前没有（D5 的视频回放方向会带来这种数据）。这一点必须写死，否则曲线会被误读成"时滞影响很小"。
4. **`divergence` 只在"匹配上"的主张上求均值**（[metrics.cpp:41-49](../../src/metrics/metrics.cpp#L41)）。**时滞变大时，分母会跟着缩**——不匹配的主张不计入误差。于是可能发生"指标变好、系统实际变差"。§6.5 的数据里就发生了：lag 1 的 divergence **下降**，而 `applied` 下降、`announced` 上升。**任何单独引用 divergence 的闭环结论都不成立**，必须与 `applied/announced/rejected` 一起读。
5. **`identity_stability` 在本面板恒为 0**（每 tick 换场景，COCO 实例标注无 track id——D3 已核验），对时滞**无分辨力**，本实验不用它。
6. **`ece:zero_delta` 恒为 0.5**（世界模型外推"逐 tick 换场景"必然失败，是面板构造的性质，D3 已定性），它在本实验里是常数项，不代表时滞效应。
7. **读真值的世界模型会稀释 divergence。** S1/S2 里 `zero_delta` 与 `linear_extrapolation` 的主张几乎与真值重合，把平均匹配误差压到接近 0（§6.2 末段）。**闭环指标必须在"感知侧"与"神谕侧"分开读**——per-producer 分解（`ece:<producer>`）已有，divergence **还没有**对应分解。这是一个具体可做的指标缺口（§7）。
8. **世界模型看到的是真值**（§4）。所以 S1/S2 里的 `zero_delta` 是一个**神谕**项：它对时滞不敏感，曲线里的变化全部来自**感知侧**。这既是优点（隔离干净），也是陷阱（真实闭环里世界模型也是错的一方）。S3（纯感知）给出的才是"没有神谕稀释"的读数。
9. **CLI 默认会悄悄带上世界模型**（§6.2 的 S2）：省略 `--models` ≠ 关闭。任何"只跑感知"的实验都必须显式写 `--models ""`，否则结论会被默认模型污染。


### 6.5 数据

**S1 · 面板对照（`--models zero_delta`，与 D3 发布配置逐字一致）**

| lag | applied | announced | 覆盖率\* | divergence | ece | **ece:rtdetr_v2_s** | identity_stability |
|---|---|---|---|---|---|---|---|
| **0** | **1527** | **702** | **68.5%** | **0.010747** | **0.363534** | **0.198829** | 0 |
| 1 | 1431 | 797 | 64.2% | 0.009333 | 0.406603 | **0.293768** | 0 |
| 2 | 1430 | 795 | 64.3% | 0.008860 | 0.406706 | 0.293659 | 0 |
| 4 | 1421 | 797 | 64.1% | 0.008897 | 0.410950 | 0.302290 | 0 |
| 8 | 1431 | 773 | 64.9% | 0.009594 | 0.406073 | 0.289833 | 0 |
| 16 | 1440 | 721 | 66.6% | 0.009396 | 0.402795 | 0.277007 | 0 |

> lag 0 与 D3 报告发布的数字**逐位相同**（1527 / 702 / 0.010747 / 0.363534 / ece:rtdetr 0.198829）⇒ 工具链无漂移，本系列可直接与 D3 对话。

**S2 · CLI 默认（省略 `--models` ⇒ `zero_delta` + `linear_extrapolation`）**

| lag | applied | announced | 覆盖率\* | divergence | ece | ece:rtdetr_v2_s |
|---|---|---|---|---|---|---|
| 0 | 2746 | 702 | 79.6% | 0.005976 | 0.411780 | 0.198829 |
| 1 | 2650 | 797 | 76.9% | 0.005040 | 0.439632 | 0.293768 |
| 4 | 2640 | 797 | 76.8% | 0.004789 | 0.442533 | 0.302290 |
| 16 | 2659 | 721 | 78.7% | 0.005089 | 0.437852 | 0.277007 |

**S3 · 纯感知（`--models ""`，无神谕稀释）**

| lag | applied | announced | 覆盖率\* | divergence | ece | identity_stability |
|---|---|---|---|---|---|---|
| 0 | 308 | 702 | 30.5% | 0.053283 | 0.198829 | **0.130653** |
| 1 | 212 | 797 | 21.0% | 0.062999 | 0.293768 | **0.276382** |
| 2 | 211 | 795 | 21.0% | 0.060050 | 0.293659 | 0.261307 |
| 4 | 202 | 797 | 20.2% | 0.062588 | 0.302290 | 0.246231 |
| 8 | 212 | 773 | 21.5% | 0.064759 | 0.289833 | 0.226131 |
| 16 | 221 | 721 | 23.5% | 0.061223 | 0.277007 | 0.246231 |

\* **覆盖率 = applied / (applied + announced + rejected)**，**本笔记新推导的量，不是引擎指标**——引擎目前没有"信念覆盖率"这个概念（§7 的缺口之一）。它是"系统认得出来的东西占比"，随退化的方向是**下降**。

### 6.6 读数：E1 的四条结论

**E1-1 · 时滞的代价落在"覆盖率"和"校准"上，不落在"平均匹配误差"上。**
S3（纯感知）：覆盖率 **30.5% → 21.0%**（匹配到的实体少了 **31%**），而 divergence 只从 0.053283 涨到 0.062999（**+18%**）。S1：覆盖率 68.5% → 64.2%，divergence **反而下降** 13%。**系统明显变差，而"平均匹配误差"几乎没动甚至变好。**

**E1-2 · 现有三个指标里，有两个会把退化的系统排得更好。**
- S1 的 `divergence`：随时滞**下降**（0.010747 → 0.009333）。
- S3 的 `identity_stability`：随时滞**上升**（0.130653 → 0.276382）——**系统更差，分数更高**。
- 唯一可靠检出退化的是 **per-producer ECE**：`ece:rtdetr_v2_s` **0.198829 → 0.293768（+47.8%）**，并在 lag 1 后进入平台（0.277–0.302）。

**E1-3 · `identity_stability` 在"无跨 tick 身份"的数据上不是稳定性度量（已用算术验证）。**
该指标比较相邻两 tick 的"匹配实体 id 多重集"。标注面板每 tick 是新场景、id 从不复用 ⇒ **非空集合绝不可能相等** ⇒ 相等的唯一可能是**两边都空**。即 `identity_stability ≡ P(相邻两 tick 都零匹配)`。验算：lag 0 → p_empty = √0.130653 = 0.361 ⇒ 约 128 个 tick 至少匹配上 1 个 ⇒ 308/128 = **2.41 匹配/非空 tick**；lag 1 → 0.526 ⇒ 95 tick ⇒ 212/95 = **2.23**；六个 lag 的"每非空 tick 匹配数"全部落在 **2.00–2.41** 的窄带内 ⇒ 假设成立。
⇒ **一个什么都不相信的系统（永远零匹配）会拿到 `identity_stability = 1.0`。** 该指标奖励的是"没有信念"，不是"身份稳定"。这是引擎侧的指标缺陷，不是本实验的构造问题（D3 报告已把本面板的 identity_stability 记为"无语义"，但没有指出它**反向**）。

**E1-4 · 不复利，但那是面板的性质，不是"时滞影响小"。**
ECE 与覆盖率在 lag 1 就到平台。原因是 COCO 的 200 张是**互不相干的独立照片**：lag ≥ 1 不是"同一场景晚一点"，而是"一张完全无关的照片"（§6.4 第 3 点）。⇒ **这条序列给的是上界（最坏情况），不是"预测质量 → 误差"的梯度。** 真正的梯度需要同一场景的时序数据（本仓库暂无；D5 的视频回放方向会带来）。

**E1-5 · 真值侧逐位不变（实验只动了一侧）。**
六个 lag 的 `world_hash` 全为 `725d0db5f1fad19a`（truth 侧未漂移），而 `ledger_hash` 各不相同（信念侧确实变了）。这是"实验只改了观察、没有偷改真值"的可核对证据。


---

## 7. 闭环指标集（缺口清单）

现有指标全部评的是**状态主张**（divergence / ECE / 身份稳定性）。闭环还需要：

| 指标 | 语义 | 引擎里谁产出 |
|---|---|---|
| 任务成功率 | 策略完成任务的比例 | **缺**：需任务定义与判定器 |
| 动作误差 | 动作 vs 理想动作 | **缺**：需 Action 契约 |
| 闭环延迟预算 | perceive → decide → act → observe 的端到端分解 | **部分已有**：逐阶段计时（契约 ⑨）齐备，缺"act"段与端到端口径 |
| 策略置信度校准 | 自报把握 vs 实际成功率 | **已有形状**：ECE 分 producer（[metrics.cpp:73](../../src/metrics/metrics.cpp#L73)），只需给动作 producer 单开一组 bin |
| 跨 tick 信用分配 | 哪个 tick 的决策导致后续失败 | **缺**，但底座在：`BeliefEntry` 已把提议+裁决一起留痕 |
| 闭环身份稳定性 | 跨 tick 同一实体 | **缺**（本面板每 tick 换场景，恒 0；真实序列数据集才有意义） |

**E1 已经定下的三条具体修改**（不是"应该加指标"，是"这个指标会骗人"）：

| # | 问题 | 证据 | 建议 |
|---|---|---|---|
| **M1** | `divergence` 只在**匹配上**的主张上求均值，分母随退化收缩 | S1 lag 1：divergence **降** 13%，而覆盖率**降** 4.3 个百分点 | 加**覆盖率**（applied / 全部信念），并与 divergence **成对发布** |
| **M2** | `identity_stability` 在无跨 tick 身份的数据上 ≡ "相邻两 tick 都零匹配"的频率，**奖励没有信念** | §6.6 E1-3 的算术验证（六个 lag 的"每非空 tick 匹配数"落在 2.00–2.41 窄带） | 空集 ≠ 稳定：要么排除空-空配对，要么在有真实 track id 的数据上才有意义（D5 的视频回放） |
| **M3** | `divergence` 没有 per-producer 分解 ⇒ 读真值的神谕模型把平均值稀释到接近 0（S1 的 71% 账本条目来自神谕） | §6.2 末段 | 仿 ECE 的 `ece:<producer>`，加 `divergence:<producer>` |

**一条方法论结论**：E1 已经暴露"单个指标会误导"（§6.4 第 4 点、§6.6 E1-2）。**闭环指标必须成组发布**（误差 + 覆盖 + 计数），否则会出现"指标变好、系统变差"。这条应写进 Phase 4 的报告口径，也应当回写进 D4 的判据 (c) 口径检查。


---

## 8. 路线建议与触发条件

### 8.1 建议

1. **不打断阶段 D。** D4（观察空间评分 + 判据 (c) 重测）→ D5（GPU 数据流）→ D6（收口）是完整链条，D5 还欠环境三件套（cuDNN 9 / CUDA 12.8+ / Video Codec SDK）。本方向是 **Phase 4**，不是阶段 D 的插曲。
2. **Phase 4 的第一个切片应该是 §4 的"信念视图"，不是 Action 契约**——它更小、更独立、可单独验证，且是任何闭环实验的前提（否则世界模型/策略读真值 = 自证）。
3. **闭环的第一个真实消费者**建议取 MuJoCo 上的一个最小任务（例如"把物体推到目标点"）+ 一个简单控制器/策略。理由：MuJoCo 是 Apache-2.0、可嵌入、无需 Isaac Sim 那种不可再分发的依赖。
4. **Cosmos 走离线数据生产者路线**，产物按 D1 纪律只进清单 + SHA256 + 许可记录。

### 8.2 这条路线被判为"值得做"的判据（可回滚）

- **J1**：E1 这类"输入误差 → 信念误差"的曲线能复现，且**结论对不同面板稳定**（否则测量本身不稳）。
- **J2**：一个最小 MuJoCo 任务能跑出**逐字节可复现**的 episode（否则闭环测量的价值打折）。
- **J3**：闭环端到端延迟预算能被拆到每段，并证明某一段是瓶颈（否则 D5 的零拷贝没有靶子）。
- **J4**：策略的置信度在闭环里**有校准意义**（ECE 可用）——若策略无法自报把握，契约 ⑦ 的校准链在闭环里就断了。

### 8.3 明确不做

不让 Cosmos 进产品路径；不 vendor Isaac Sim；不引入付费大算力（PLAN §五 训练边界）；不在阶段 D 未收口前动契约（ADR 流程）；不做多相机与位姿估计（PLAN §五 ④）。

---

## 9. 未核验 / 明确不知道的

1. **Cosmos 在 8 GiB 上是否有任何可行路径**（未测；官方模型卡给的是 32.54 GB @2B）。
2. **MuJoCo ↔ 本引擎的接线成本**（未做 spike；§2.2 的形状是推断，不是实测）。
3. **ROS 2 的真实需求**（未评估；D5 之前不必）。
4. **VLA 策略在本引擎的可迁移性**（未测；本仓库目前是 2D 俯视 + 框级真值，与机器人数据集的域差距很大——这是域不匹配问题，不是实现问题）。
5. **训练边界**：本笔记未涉及任何训练；若闭环需要训练策略，受 PLAN §五 的红线约束（免费资源尺度内）。

---

## 10. 复现命令

```bat
rem 前置（本笔记依赖其产物）
node tools/fetch_coco_subset.mjs && node tools/coco_to_panel.mjs
cmake --build build-ort-c5   rem Release + ON；跑前必做，陈旧二进制陷阱见 AGENTS.md

rem 1) 生成时滞清单（零核心改动：只重排清单里的路径）
for %%L in (0 1 2 4 8 16) do node tools/stale_frames.mjs --frames data/panel/frames.txt --lag %%L --out data/panel/frames_lag%%L.txt

rem 2) S1 面板对照（与 D3 发布配置逐字一致；lag 0 应复现 applied=1527 announced=702 divergence=0.010747）
for %%L in (0 1 2 4 8 16) do build-ort-c5\omniworld_loop.exe --frames data/panel/frames_lag%%L.txt ^
  --annotations data/panel/truth.txt --ticks 200 --adapters rtdetr_v2_s --models zero_delta ^
  --mode reference --ep cpu --out data/panel/lag/lag%%L_full.json

rem 3) S3 纯感知（--models "" 是唯一能表达"无世界模型"的方式；省略 --models 会用 CLI 默认集合）
for %%L in (0 1 2 4 8 16) do build-ort-c5\omniworld_loop.exe --frames data/panel/frames_lag%%L.txt ^
  --annotations data/panel/truth.txt --ticks 200 --adapters rtdetr_v2_s --models "" ^
  --mode reference --ep cpu --out data/panel/lag/lag%%L_per.json

rem 4) S2 CLI 默认（省略 --models ⇒ zero_delta + linear_extrapolation）
for %%L in (0 1 4 16) do build-ort-c5\omniworld_loop.exe --frames data/panel/frames_lag%%L.txt ^
  --annotations data/panel/truth.txt --ticks 200 --adapters rtdetr_v2_s ^
  --mode reference --ep cpu --out data/panel/lag/lag%%L_det.json
```

> 每组运行约 110 s（200 帧 × RT-DETR CPU EP ≈ 537 ms/帧）。产物在 `data/panel/lag/`，**不入库**（数据纪律同 D1）。

---

## 参考

- 仓库内：L0 §2（[00-总体设计.md](../../docs/design/00-总体设计.md)）· [ADR-0011](../../docs/adr/0011-contract-rear-mirror-delta-review.md) · [ADR-0013](../../docs/adr/0013-scoring-space.md) · [PLAN.md](../../PLAN.md) · [D1 环境与许可](../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md) · [D3 回归报告](../../benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md) · [阶段 D L1](../../docs/design/stage-d-real-data/概要设计.md)
- 外部（2026-10-27 查）：[MuJoCo LICENSE](https://raw.githubusercontent.com/google-deepmind/mujoco/master/LICENSE) · [Isaac Lab License](https://docs.nvidia.com/isaaclab/references/license) · [Omniverse 分发条款讨论](https://forums.developer.nvidia.com/t/licensing-when-distributing-omniverse/371343) · [Cosmos-Predict2-2B-Video2World 模型卡](https://huggingface.co/nvidia/Cosmos-Predict2-2B-Video2World) · [LeRobot LICENSE](https://raw.githubusercontent.com/huggingface/lerobot/main/LICENSE) · [SmolVLA blog](https://huggingface.co/blog/smolvla) · [rclcpp LICENSE](https://raw.githubusercontent.com/ros2/rclcpp/master/LICENSE) · [MuJoCo vs Isaac Sim 许可对照](https://www.roboticscenter.ai/rl-environments/mujoco-vs-isaac-sim) · [ROS2SmolVLA（ROS 2 × VLA 的现成桥）](https://arxiv.org/html/2608.23320v1)
