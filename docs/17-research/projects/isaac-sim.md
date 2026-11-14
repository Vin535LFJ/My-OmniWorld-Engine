# Isaac Sim 6.0.1 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建
> 索引行：[`../third-party-research-map.md`](../third-party-research-map.md)（Isaac Sim 行，Boundary = Simulation adapter）。本卡是该行新事实（6.0.1 硬件规格逐字、许可格局变化、Isaac Lab 关系）的依据。

## 0. 一句话

NVIDIA 建在 Omniverse Kit 上的开源（核心 Apache-2.0）机器人仿真平台，官方 6.0.1 最低规格为 **GeForce RTX 4080 / 16 GB VRAM**；本机 RTX 4060 Laptop 8 GB 低于官方最低档一半显存 ⇒ 对 OmniWorld 是**条件性仿真适配器候选 + 合成数据管道参照**，不是可本地运行、更不是可嵌入的目标。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| 官方定义（6.0.1 文档首页）："Import robots and scenes from URDF, MJCF, Onshape CAD, or USD. Simulate with PhysX or Newton, add RTX and physics-based sensors, generate synthetic data, prepare robots for Isaac Lab, and validate robot stacks with ROS 2." | docs.isaacsim.omniverse.nvidia.com `/6.0.1/index.html` |
| 四个官方徽章："Apache 2.0 — Open-source licensing for the simulator stack" / "USD-native — One scene representation from asset import to deployment" / "PhysX + Newton — Switch between supported physics backends in one simulator" / "RTX + Physics Sensors — Use rendering and physics-based sensor models in one place" | docs.isaacsim.omniverse.nvidia.com `/6.0.1/index.html` |
| "Isaac Sim is open source and built to fit into existing robotics stacks."（"Read the code, extend the simulator, and fit it into your stack."） | docs.isaacsim.omniverse.nvidia.com `/6.0.1/index.html` |
| 官方参考架构 = 五个任务组：Geometry Authoring（SimReady/Isaac 资产）→ Importing Assets（URDF/MJCF/Onshape/CAD 导入器）→ Scene Setup（PhysX 物理 + 传感器）→ Interaction with the Digital Twin（GUI / Standalone Python / Extensions(C++·Python) / OmniGraph）→ Use Cases（Isaac Lab、SDG、ROS 2 Bridge、Isaac ROS） | docs.isaacsim.omniverse.nvidia.com `/6.0.1/introduction/reference_architecture.html` |
| 合成数据生成（Omniverse Replicator）标注输出：2D/3D bounding boxes、分割掩码、深度，**COCO/KITTI 格式** | docs.isaacsim.omniverse.nvidia.com `/6.0.1/introduction/reference_architecture.html` |
| 版本事实：6.0.1 GA 是 6.0.0 之上的维护版——Kit SDK 110.1.1→110.1.2、资产工具链升级、新增 `isaacsim.replicator.nurec_utils` 扩展（NuRec）、修复 ROS 2 dynamic message buffer 泄漏、新增 `shutdown_watchdog_timeout` 启动配置；release notes 页未标注日历日期 | docs.isaacsim.omniverse.nvidia.com `/6.0.1/overview/release_notes.html` |
| 6.0.0 主要特性：Kit 110.1.1；multitick rendering（相机与 RTX Lidar 由物理仿真时间驱动）；**实验性 Newton 物理引擎支持 + 引擎无关 tensor API（`isaacsim.core.experimental`）**；ROS 2 经 Pixi 的完整 Windows 支持；DGX Spark 支持；破坏性变更 "Removed omni.isaac.* compatibility shims" | docs.isaacsim.omniverse.nvidia.com `/6.0.1/overview/release_notes.html` |
| GitHub 发布序列（版本号→发布日，页未显式标年份、按页脚年份 2026 读）：6.0.0 = 2026-06-04，**6.0.1 = 2026-06-22**；其后 6.1.0（09-10）、6.2.0（10-14，Latest）、7.0.0a1（09-18，pre-release） | github.com `isaac-sim/IsaacSim/releases` |
| Isaac Lab 与 Isaac Sim 的关系：Isaac Lab 是"unified and modular framework for robot learning"，"supports NVIDIA Isaac Sim for photo-realistic scenes and RTX rendering"，但**也可不带 Isaac Sim 独立运行**——"can also run standalone with the Newton physics backend ... without requiring a full Isaac Sim installation"；Isaac Lab 3.0 引入 multi-backend 架构（shared asset/sensor/scene APIs） | isaac-sim.github.io `IsaacLab/main/index.html` |
| Isaac Lab 3.0.0 安装页：默认装 Newton，"The standard installation runs without Isaac Sim"；`isaacsim` extra 安装 "Isaac Sim (isaacsim[all,extscache] version **6.2.0.0**)"（pypi.nvidia.com）；"Isaac Sim 5.1 and older are unsupported" | isaac-sim.github.io `IsaacLab/main/source/isaaclab/installation.html` |
| Isaac Lab 许可：**BSD-3-Clause，部分组件 Apache-2.0** | isaac-sim.github.io `IsaacLab/main/index.html` |

## 2. 边界与限制

### 2.1 官方承认的限制

| 限制 | 原文/出处 |
|---|---|
| 低显存警告："GPUs with less than 16GB VRAM may be insufficient to run a complex scene rendering more than 16MP per frame. Consider upgrading to a higher spec if that is your use case."（requirements 页**没有**任何"8 GB 最低"的表述） | docs.isaacsim.omniverse.nvidia.com `/6.0.1/installation/requirements.html` |
| "GPUs without RT Cores (A100, H100) are not supported."（条款点名的对象是数据中心卡） | 同上 |
| "The Isaac Sim container is only supported on Linux."；aarch64 构建仅支持 DGX Spark，且 cuRobo/cuMotion 在 DGX Spark 上不受支持 | 同上 |
| "Windows 10 is not supported."（三档 OS 均为 Ubuntu 22.04/24.04 或 Windows 11） | 同上 |
| 6.0.0 移除 `omni.isaac.*` 兼容 shim + 大批 deprecation（core.api、sensors、wheeled_robots、lula/motion_generation）——升级有迁移负担 | docs.isaacsim.omniverse.nvidia.com `/6.0.1/overview/release_notes.html` |
| Newton 物理支持在 6.0 仍标注 experimental；老 `isaacsim.sensors.*` 扩展 deprecated | 同上 |
| 显存/内存脚注："More RAM and VRAM is recommended for advanced usage of Isaac Sim. Isaac Lab usage will require additional RAM and VRAM for training." | docs.isaacsim.omniverse.nvidia.com `/6.0.1/installation/requirements.html` |

### 2.2 我们的推断（不是官方声明）

- **本机不适合运行完整 Isaac Sim**：RTX 4060 Laptop 8.00 GiB（D1 实测，见 [`../2026-10-26-stage-d-environment-and-licenses.md`](../2026-10-26-stage-d-environment-and-licenses.md) §1）是官方最低 VRAM 档（16 GB）的一半，且型号低于最低档 GPU（RTX 4080）。官方只给了"低于 16 GB 对 >16 MP 复杂场景可能不够"的警告，**没有明说 8 GB 不能安装/启动**；本机未实测（未安装）。此推断置信度高但非官方结论。
- **驱动差距**：本机实测 CUDA 驱动/运行时 12.6/12.6（D1），显示驱动号未记录；官方只说测试驱动为 Linux 595.58.03 / Windows 595.97（"was tested on"），不是硬性最低值。差距的具体影响 **UNVERIFIED**。
- **重应用形态，与"参考模式零外部依赖"红线相斥**：最低 50 GB SSD + 32 GB RAM + 构建需从 NVIDIA blob 下载 Omniverse Kit SDK（github.com/isaac-sim/IsaacSim README）⇒ 进程内嵌入不现实，任何集成只能是**外部进程级**（Isaac Sim 侧跑，引擎侧经数据/标注交换对接）——与 [`../2026-10-27-interactive-closed-loop-route.md`](../2026-10-27-interactive-closed-loop-route.md) §2"引擎不写仿真器"的所有权模型一致。

## 3. 许可（代码 / 权重 / 数据 三分）

| 层 | 许可事实 | 来源 |
|---|---|---|
| **代码**（仿真器本体，github.com/isaac-sim/IsaacSim） | **Apache License 2.0**。LICENSE 文件原文："The Isaac Sim software in this repository is covered under the Apache 2.0 License terms below." | github.com `isaac-sim/IsaacSim`（release/6.3 分支 `LICENSE`）；docs.isaacsim.omniverse.nvidia.com `/6.0.1/common/licenses-isaac-sim.html` |
| **依赖/运行时组件**（构建或使用所必需，**非 Apache**） | 官方许可页原文："Building or using the software requires additional components licenced under other terms."——点名 **Omniverse Kit SDK** 与 **3D models and textures**，走 "NVIDIA Isaac Sim Additional Software and Materials License"；另有独立的 WebRTC Streaming Client 许可与 Omniverse License Agreement | docs.isaacsim.omniverse.nvidia.com `/6.0.1/common/licenses-isaac-sim.html` |
| **权重** | Isaac Sim 本体**不含**模型权重（它是仿真器，不是模型发布）。6.0.1 新增的 NuRec 相关功能及 Replicator 内置 AI 模型的权重许可条款 **UNVERIFIED**（官方许可页未逐项列出，本卡未核验到逐模型条款） | — |
| **数据/资产**（SimReady 资产、3D 模型、材质） | **非 Apache**：随 "Additional Software and Materials License" 分发（NVIDIA 专有条款）。SimReady 资产描述见官方参考架构页，许可归属见许可页的 "3D models and textures" 条目 | docs.isaacsim.omniverse.nvidia.com `/6.0.1/common/licenses-isaac-sim.html`、`/6.0.1/introduction/reference_architecture.html` |

**与既有仓库结论的 delta（新事实，索引行后续补链接的依据）**：[`../2026-10-27-interactive-closed-loop-route.md`](../2026-10-27-interactive-closed-loop-route.md) §5 记录"Isaac Sim 走 Omniverse 私有许可、不可再分发 ⇒ Isaac Lab 只能当本地研究工具"。该表述对应 Isaac Sim 开源（5.0，2025-08）之前的格局；**截至 2026-11-05，6.x 仿真器代码本身是 Apache-2.0**，分发约束**转移到了依赖组件层**（Kit SDK、资产、WebRTC client 仍为 NVIDIA 专有条款）。⇒ "MuJoCo 优先"的结论**不变**，但支撑论据从"许可不可再分发"换成了"①依赖组件仍专有、②进程内嵌入不现实（重应用）、③本机 8 GB 低于官方最低 16 GB"三条。

## 4. 硬件要求（逐字核验，2026-11-05）

来源（下表全部数字与引文）：docs.isaacsim.omniverse.nvidia.com `/6.0.1/installation/requirements.html`（x86_64 部分；检索 2026-11-05）。

| 项 | Minimum | Good | Ideal |
|---|---|---|---|
| GPU | **"GeForce RTX 4080"** | "GeForce RTX 5080" | "RTX PRO 6000 Blackwell" |
| VRAM | **"16GB"** | "16GB" | "48GB" |
| RAM | "32GB" | "64GB" | "64GB" |
| CPU 核心 | "4" | "8" | "16" |
| CPU | "Intel Core i7 (7th Generation)" / "AMD Ryzen 5" | i7 (9th) / Ryzen 7 | i9, X-series+ / Ryzen 9, Threadripper+ |
| 存储 | "50GB SSD" | "500GB SSD" | "1TB NVMe SSD" |
| OS | Ubuntu 22.04/24.04 或 Windows 11（"Windows 10 is not supported."） | 同左 | 同左 |
| 驱动 | Linux 595.58.03 / Windows 595.97（脚注："Isaac Sim was tested on these driver versions."） | 同左 | 同左 |
| RT Cores | "GPUs without RT Cores (A100, H100) are not supported." | 同左 | 同左 |
| 架构 | 容器仅 Linux；aarch64 仅 DGX Spark（DGX OS 7，驱动 580.159.03） | — | — |

**外部声称核验结论**：外部材料所称"16 GB / RTX 4080 级"与官方 6.0.1 requirements 页**逐字相符**——最低档 GPU 就是 "GeForce RTX 4080"，最低 VRAM 就是 "16GB"。

**与本机的差距**（本机数据来源：仓库 D1 实测 [`../2026-10-26-stage-d-environment-and-licenses.md`](../2026-10-26-stage-d-environment-and-licenses.md) §1，runtime_probe.exe）：

| 维度 | 本机（RTX 4060 Laptop, Windows 11 build 26200, i9-14900HX, 31.80 GiB RAM） | 官方最低档 | 差距判定 |
|---|---|---|---|
| VRAM | **8.00 GiB**（空闲 6.93） | 16 GB | **一半，低于官方最低档** |
| GPU 型号 | RTX 4060 Laptop | GeForce RTX 4080 | 低于最低档型号 |
| OS | Windows 11 ✅ | Windows 11 | 达标 |
| RAM | 31.80 GiB | 32 GB | 差 0.2 GiB，贴线不达标 |
| 驱动 | CUDA 驱动 12.6（显示驱动号未记录） | 测试驱动 Windows 595.97 | 差距存在，能否运行 UNVERIFIED |
| RT Cores | A100/H100 条款不适用（Ada 消费卡有无 RT Cores 属产品事实，本卡未核验来源） | — | 不确定项，见 §7 |

**重要的缝隙（Isaac Lab 侧）**：Isaac Lab 3.0 安装页明说 "Isaac Sim's workstation memory requirements **do not apply to a Newton-only installation**"，且 "The standard installation runs without Isaac Sim"——即"物理闭环仿真"这条用途可以**绕过 16 GB 显存墙**（isaac-sim.github.io `IsaacLab/main/source/isaaclab/installation.html`，检索 2026-11-05）。

## 5. 可集成接口

| 接口类别 | 官方事实 | 来源 |
|---|---|---|
| 场景表示 | OpenUSD 原生（"USD-native — One scene representation from asset import to deployment"）；所有资产必须先转 OpenUSD，默认单位米 | docs.isaacsim.omniverse.nvidia.com `/6.0.1/index.html`、`/6.0.1/introduction/reference_architecture.html` |
| 导入/导出 | URDF / MJCF / Onshape 导入器；OBJ/FBX/glTF 资产导入器；CAD 转换器（Catia/Solidworks/AutoCad/Creo）；USD Tools | 同上（参考架构页） |
| 控制面（官方四种数字孪生交互机制） | GUI；Standalone Python（headless、Jupyter、VS Code）；**Extensions（C++、Python 或混合）**；OmniGraph（可视化脚本）；开发工具另有 MCP Server | 同上 + `/6.0.1/index.html` 侧栏 |
| 物理后端 | PhysX + Newton 可切换；6.0 起**引擎无关 tensor API**（`isaacsim.core.experimental`，Newton 为 experimental） | `/6.0.1/index.html`、`/6.0.1/overview/release_notes.html` |
| 传感器 | RTX 渲染传感器（相机/Lidar/Radar）+ 物理型传感器（contact、IMU、force、effort、proximity）；6.0 新增实验性 RTX 相机/声学/物理 raycast 传感器 API | 参考架构页 + release notes |
| 合成数据导出 | Omniverse Replicator：2D/3D bbox、分割、深度 → **COCO/KITTI 格式**（与本仓库 D 系列的面板格式直接兼容的语义） | 参考架构页 |
| ROS 2 | ROS 2 Bridge（Extensions 机制实现）；6.0 起经 Pixi 的完整 Windows 支持、RTX Radar PointCloud2 发布 | `/6.0.1/overview/release_notes.html`、参考架构页 |
| 流送/部署 | WebRTC livestreaming（Docker Compose 部署）；容器仅 Linux；CI/CD 经 OSMO 编排 | release notes + 参考架构页 + requirements |
| Python 发行 | `isaacsim` pip 包（pypi.nvidia.com；Isaac Lab 3.0 的 `isaacsim[all,extscache]==6.2.0.0` 证实该发行通道存在） | isaac-sim.github.io `IsaacLab/main/source/isaaclab/installation.html` |

## 6. 与 OmniWorld 的边界 + 族谱归宿

指针材料定位：map 索引行 = **Simulation adapter**（Study: sensor/simulation integration；Do not copy: Full simulator）；[`../research-queue.md`](../research-queue.md) 交互闭环条目把"MuJoCo spike（Apache-2.0、可嵌入）"列为 Phase 4 第一候选；[`../2026-10-27-interactive-closed-loop-route.md`](../2026-10-27-interactive-closed-loop-route.md) §8.3 明确"不 vendor Isaac Sim"。本卡的归宿与之一致并细化：

- **② 适配器候选（Simulation adapter，条件性）**：官方四通道交互（§5）+ 6.0 引擎无关 tensor API，与闭环笔记 §2 方案 (b)（仿真器=外部真值权威，引擎镜像进 `WorldState`）在形状上对接。**触发条件**：(a) Phase 4 闭环评测真的需要机器人级物理场景（当前第一个消费者已定为 MuJoCo 最小任务，closed-loop 笔记 §8.1.3）；(b) 拿到 ≥16 GB VRAM 的 GPU 或云实例——本机 8 GB 低于官方最低档（§4），适配器开发在本机不可行。两条都满足才从"参照"升级为"适配"。
- **⑤ 明确让渡**：不 vendor、不克隆、不嵌入——重应用形态（Kit SDK 下载、50 GB SSD、32 GB RAM 最低）与"参考模式零外部依赖"（ADR-0012 的构建级承诺）结构性相斥（§2.2）。Isaac Sim 的仿真器本体永远不会出现在本仓库的产品路径里。
- **竞品参照（研究价值所在）**：① **Replicator 的"传感器 → 标注 → COCO/KITTI 格式"管道**与本仓库"真实帧 + 标注入环"（D2/D3 面板纪律）同构——它证明"标注格式对齐评测生态"是仿真器侧的成熟做法，可作 D 系列数据管道的对照设计；② **Isaac Lab 的"默认不装 Isaac Sim、Newton standalone 即可跑"**是"轻物理内核 + 重渲染可选"架构的一手实例，与本引擎"参考模式零依赖 + 可选依赖关在默认 OFF 构建里"的取舍同向。
- **评测对象？** Isaac Sim 不是世界模型，不是直接评测对象。它的上层框架 **Isaac Lab 是 Phase 4 闭环物理后端候选之一，与 MuJoCo 竞争同一席位**——MuJoCo 以"Apache-2.0 可嵌入 + 无专有依赖 + 无 16 GB 门槛"胜出（closed-loop 笔记 §5、§8.1.3；本卡 §3 的许可 delta 与 §4 的硬件差距是更新后的论据）。
- **不应因许可新格局而重开的门**：Isaac Sim 核心已 Apache-2.0，但 Kit SDK/资产仍走 NVIDIA 专有条款（§3），且闭环路线"把 MuJoCo spike 放第一"的三个论据（依赖专有、嵌入不现实、显存门槛）在本卡核验后依然全部成立——没有理由改判。

## 7. 未解决问题 + 重查触发条件

**UNVERIFIED（不作为决策依据）**：

1. **Isaac Sim 6.0.1（或任何版本）能否在 8 GB VRAM GPU 上安装/启动**（headless 或最小模式）——官方 requirements 页只有"<16 GB 对 >16 MP 复杂场景可能不够"的警告，无 8 GB 明确说法；本机未安装未实测。低显存下的真实可用性只能实测。
2. **Isaac Lab 3.0 与 Isaac Sim 6.0.1 的兼容性**——Isaac Lab 安装页只把 `isaacsim` extra 锚定到 6.2.0.0，且只写"Isaac Sim 5.1 and older are unsupported"；6.0.x 是否受支持未明说。若闭环路线坚持用 Isaac Lab + Isaac Sim，需选 6.2 而非本卡主角 6.0.1。
3. **NuRec / Replicator 关联 AI 模型权重的许可条款**——官方许可页未逐项列出。
4. **本机显示驱动与官方测试驱动（Windows 595.97）的兼容性**——本机只实测了 CUDA 12.6（D1），显示驱动号未记录；官方脚注仅声明 tested-on，非硬性门槛。
5. **RTX 4060 Laptop 的 RT Cores 有无**——官方"无 RT Cores 不支持"条款只点名 A100/H100；4060 的 RT Cores 属产品事实，本卡未核验其官方来源（若核验，GeForce 产品页为一等来源）。
6. **GitHub release 日期的年份读法**——releases 页显示 "22 Jun 23:14" 未显式标年份，按页脚年份 2026 读作 2026-06-22；此读法是推断而非页面原文。

**重查触发条件**：

| 触发 | 动作 |
|---|---|
| 拿到 ≥16 GB VRAM 的 GPU 或可用的云 GPU 实例 | 重估 §6"② 适配器候选"可行性；实测 UNVERIFIED-1 |
| Phase 4 闭环路线考虑 Isaac Lab 的 Newton standalone 路径（绕过 Isaac Sim 显存墙） | 重查 Isaac Lab 3.0 Newton 后端对 Windows 11 + 8 GB 的要求与许可；与 MuJoCo spike 结果对比 |
| Isaac Sim 7.0.0 GA（7.0.0a1 pre-release 已于 GitHub 出现）或 6.2.x 成为 Isaac Lab 锚定版本 | 刷新 §1/§4 版本与规格事实；重查 `omni.isaac.*` shim 之后是否又有破坏性迁移 |
| NVIDIA Isaac Sim Additional Software and Materials License / WebRTC Streaming Client 条款变化 | 重核 §3 依赖层许可结论（分发约束的全部重量都在这一层） |
| 交互闭环方向升格为 Gate/ADR（closed-loop 笔记 §8.1 的移位决定） | 用本卡 §6 作为"仿真器席位"输入，MuJoCo vs Isaac Lab 的取舍必须引用本卡 §3/§4 而不是旧表述 |

## 8. 来源清单

**外部（全部检索日期 2026-11-05）**：

1. Isaac Sim 6.0.1 官方要求页（硬件规格逐字来源）：https://docs.isaacsim.omniverse.nvidia.com/6.0.1/installation/requirements.html
2. Isaac Sim 6.0.1 release notes（6.0.1/6.0.0 变更清单）：https://docs.isaacsim.omniverse.nvidia.com/6.0.1/overview/release_notes.html
3. Isaac Sim 6.0.1 文档首页（官方定义、四徽章、开源声明、侧栏结构）：https://docs.isaacsim.omniverse.nvidia.com/6.0.1/index.html
4. Isaac Sim 官方参考架构（五任务组、Replicator 标注格式、四交互机制）：https://docs.isaacsim.omniverse.nvidia.com/6.0.1/introduction/reference_architecture.html
5. Isaac Sim 许可页（Apache 2.0 + 依赖组件独立许可的拆分）：https://docs.isaacsim.omniverse.nvidia.com/6.0.1/common/licenses-isaac-sim.html
6. IsaacSim 仓库 LICENSE 原文（Apache 2.0，release/6.3 分支）：https://raw.githubusercontent.com/isaac-sim/IsaacSim/release/6.3/LICENSE
7. IsaacSim 仓库 Releases 页（6.0.1 = 22 Jun 等发布序列）：https://github.com/isaac-sim/IsaacSim/releases
8. Isaac Lab 文档首页（框架定义、与 Isaac Sim 的关系、BSD-3-Clause 声明）：https://isaac-sim.github.io/IsaacLab/main/index.html
9. Isaac Lab 3.0 安装页（isaacsim extra → 6.2.0.0、"5.1 and older are unsupported"、Newton-only 免除 Isaac Sim 显存要求）：https://isaac-sim.github.io/IsaacLab/main/source/isaaclab/installation.html

**仓库内（本卡引用其结论、不复制内容）**：

- `docs/17-research/third-party-research-map.md`（Isaac Sim 索引行：Simulation adapter）
- `docs/17-research/research-queue.md`（交互闭环条目：MuJoCo spike 优先的理由）
- `docs/17-research/2026-10-26-stage-d-environment-and-licenses.md` §1（本机 RTX 4060 Laptop 8.00 GiB / CUDA 12.6 实测）
- `docs/17-research/2026-10-27-interactive-closed-loop-route.md` §2、§5、§8.1.3、§8.3（引擎↔仿真器所有权模型；旧许可表述；MuJoCo 第一消费者决定；不 vendor 决定）

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-isaac-sim.md](deep/2026-11-05-isaac-sim.md)
