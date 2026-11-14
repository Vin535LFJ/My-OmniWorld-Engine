# Newton / MuJoCo-Warp 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建

## 0. 一句话

Newton 是由 NVIDIA、Google DeepMind、Disney Research 发起、2025-09 捐入 Linux Foundation 的开源 GPU 加速物理仿真引擎（NVIDIA Warp 内核 + MuJoCo-Warp 主后端 + OpenUSD，Apache-2.0，官方支持 Windows）；MuJoCo-Warp 是 MuJoCo 模型的 GPU 加速版（Apache-2.0，NVIDIA GPU 必需），两者对 OmniWorld 的角色都是**外挂的物理执行后端（仿真器=外部真值权威）**，不是可嵌入组件——可嵌入的是 MuJoCo 本体（结论引用闭环笔记，不重核）。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| 定位："Newton is a GPU-accelerated, extensible, and differentiable physics simulation engine designed for robotics, research, and advanced simulation workflows."；仓库描述 "An open-source, GPU-accelerated physics simulation engine built upon NVIDIA Warp, specifically targeting roboticists and simulation researchers." | newton-physics.github.io（docs/guide/overview.html）；github.com/newton-physics/newton（README） |
| 发起方："Newton was initiated by Disney Research, Google DeepMind, and NVIDIA"；公告口径（GTC 2025，2025-03-18 博客）："Newton is an open-source, extensible physics engine being developed by NVIDIA, Google DeepMind, and Disney Research to advance robot learning and development." | github.com/newton-physics/newton（README）；developer.nvidia.com/blog/announcing-newton… |
| 治理：2025-09-29 由三方捐入 Linux Foundation，"Under the Linux Foundation's governance, Newton will remain vendor neutral"；README 自称 "a Linux Foundation project that is community-built and maintained" | linuxfoundation.org/press/linux-foundation-announces-contribution-of-newton…；github.com/newton-physics/newton（README） |
| 构成："Built on top of NVIDIA Warp and integrating MuJoCo Warp"；README："integrates MuJoCo Warp as its primary backend"；使用 OpenUSD 框架；扩展并泛化了 Warp 已弃用的 warp.sim 模块 | newton-physics.github.io（docs/guide/overview.html）；github.com/newton-physics/newton（README）；developer.nvidia.com/blog/announcing-newton… |
| 求解器家族（文档列表）：XPBD、VBD、MuJoCo、Featherstone、SemiImplicit、Kamino、ImplicitMPM、Style3D；宣称支持可微仿真、碰撞与接触、可变形体、耦合求解器、执行器、`newton.ik` 逆运动学模块 | newton-physics.github.io（docs/guide/overview.html，含侧栏导航条目） |
| 资产导入：加载 "URDF, MJCF, USD, and more" | newton-physics.github.io（docs/guide/overview.html） |
| MuJoCo-Warp（MJWarp）定位："MJWarp is a GPU-accelerated version of the MuJoCo physics simulator, designed for NVIDIA hardware."；"MJWarp is maintained by Google DeepMind and NVIDIA as part of the Newton project."；README 定位可作 MuJoCo 的 drop-in 替换（有功能缺口，见 §2） | github.com/google-deepmind/mujoco_warp（README raw） |
| MJWarp 能力：刚体、软体、布料、符号距离场（SDF）；内置 GPU 批量渲染器（ray-tracing，宣称 "millions of frames per second on NVIDIA GPUs"）；场景基准"每晚跑并公布" | github.com/google-deepmind/mujoco_warp（README）；pypi.org/project/mujoco-warp |
| 版本与节奏（成熟度的官方可观测面）：Newton 当前 **1.7.0**（2026-10-14 发布）+ 1.8.0rc1（2026-11-04）；自 2025-04（0.1.1a1）起约每月一版；MuJoCo-Warp 当前 **3.17.0**（2026-10-22），版本号与 MuJoCo 3.x 对齐，2026 年内从 3.5.x 到 3.17.0 共 20+ 个版本；两仓库均活跃（pushed 2026-11-04）、未归档 | pypi.org/pypi/newton/json；pypi.org/pypi/mujoco-warp/；api.github.com/repos/{newton-physics/newton, google-deepmind/mujoco_warp} |
| 性能声称（宣传口径，未在本仓库测量）：MuJoCo-Warp 相对 MuJoCo CPU "more than a 70x acceleration for humanoid simulations and a 100x speedup for in-hand manipulation tasks" | developer.nvidia.com/blog/announcing-newton… |

## 2. 边界与限制

### 2.1 官方承认的限制

- **MuJoCo-Warp 功能缺口**（README 原文列举）："Integrator: IMPLICITFAST midpoint integrator feature is not supported"；"Solver: PGS not yet supported (noslip not supported with discrete integrator)"；"Actuator / Sensors: PLUGIN types not yet supported"；"Flex: experimental — not all features are implemented or optimized yet"。（github.com/google-deepmind/mujoco_warp，README raw）
- **MuJoCo-Warp 可微性未提供**："Differentiability via Warp is not yet available."（同上；进度指向其 issue #500）——与 Newton 文档"宣称可微"并读：可微性在 Warp 层存在、在 MJWarp 主后端尚未兑现。
- **MuJoCo-Warp 硬件前提**："MuJoCo Warp requires an NVIDIA GPU for fast simulation but supports CPU for development and debugging."（同上）
- **Newton API 稳定性策略**（官方兼容性页）：私有 API（`newton._src.*`、下划线前缀）"can change in any release"，用户代码禁止 import；**破坏性变更落在 minor 版本**（micro 版本不出）；弃用特性保留至少一个 minor 周期；pre-release "should be considered unstable"。仅最新 minor 线接收 micro 修复。（newton-physics.github.io，docs/guide/compatibility.html）
- **Python 3.10 弃用**：README 写明 removal 计划在 Newton 1.9，推荐 3.11+。（github.com/newton-physics/newton，README）
- **CUDA 12 弃用**："12 is deprecated in Newton 1.7"——1.7.x 仍可用（经 CUDA 12 构建的 Warp 1.17 wheel），推荐 CUDA 13.4+（Warp 1.18 wheel）。（同上）
- 官方文档/README 均**未给自己贴 alpha/beta 标签**；成熟度只能从版本节奏、治理与稳定性策略推断（见 2.2）。

### 2.2 我们的推断（非官方声称）

- **成熟度 = 快速演进中的 1.x，非稳定 LTS**：Newton 一年内 0.1a → 1.8rc，破坏性变更合法落在 minor；采用它意味着随 minor 版本跟进适配。这与"Linux Foundation 治理 + 3,100+ commits + 5,886★"并存，两头都是事实。
- **Python-only ⇒ 只能进程外集成**：Newton/MJWarp 都是 Python 包（warp-lang JIT 内核）；对 C++20 的本引擎不存在进程内链接选项。集成形状只能是：外挂后端进程 + 资产文件交换（MJCF/USD/URDF）+ 状态镜像——正是闭环笔记 §2.2 裁定的"引擎 ↔ 仿真器"同构位。
- **MJWarp 版本与 Newton 内核不同步**：Newton 1.7.0 的 `sim` extra 锁 `mujoco-warp~=3.15.0`，而 MJWarp 最新为 3.17.0——经 Newton 使用 MJWarp 时通常滞后一至两个版本。（pypi.org/pypi/newton/json 与 pypi.org/project/mujoco-warp 对照）
- **性能数字不可直接引用**：70x/100x 是 NVIDIA 在人形/灵巧手场景对 MuJoCo CPU 的宣传口径，与本仓库的俯视 2D + 框级真值工作负载无关；本仓库纪律是"没有 benchmark 数据不宣称性能"。
- **MJWarp 的确定性/重放语义未核**（对闭环笔记判据 J2"逐字节可复现 episode"是关键问题）：两项目官方材料均未陈述 GPU 仿真的逐位确定性或 seeded-replay 保证 → 见 §7。

## 3. 许可（代码 / 权重 / 数据 三分）

| 层 | 许可 | 依据 |
|---|---|---|
| **代码**：Newton | **Apache-2.0**（LICENSE.md 原文；GitHub API spdx_id；PyPI `license_expression: Apache-2.0` 三方一致） | github.com/newton-physics/newton（LICENSE.md raw）；api.github.com；pypi.org/pypi/newton/json |
| **代码**：MuJoCo-Warp | **Apache-2.0**（LICENSE 原文 + PyPI license_expression + GitHub API spdx_id） | github.com/google-deepmind/mujoco_warp（LICENSE raw）；pypi.org/project/mujoco-warp |
| **代码**：底层依赖 NVIDIA Warp | **Apache-2.0**（"Warp is provided under the Apache License, Version 2.0."） | github.com/NVIDIA/warp（README raw） |
| **代码**：MuJoCo 本体 | **Apache-2.0，可嵌入**——**结论引用既有核验**：[闭环笔记 §5](../2026-10-27-interactive-closed-loop-route.md)（2026-10-27 查 LICENSE 原文）。本次顺手印证：PyPI mujoco 包描述自述 "MuJoCo and its Python bindings are licensed under the Apache License, Version 2.0"（2026-11-05 查） | github.com/google-deepmind/mujoco/LICENSE（经闭环笔记）；pypi.org/pypi/mujoco/json |
| **文档**：Newton | **CC-BY-4.0**（文档页脚 "Documentation licensed under CC-BY-4.0"；README 同口径） | newton-physics.github.io；github.com/newton-physics/newton（README） |
| **权重** | **不适用**——本项目是仿真器/引擎，不是模型；不发布权重。（若将来引入其附带的可微策略资产，需逐项重查） | —— |
| **数据/资产** | Newton 仓库携带示例资产（URDF/MJCF/USD 例程）；其**逐项资产许可未核** → **UNVERIFIED**（仓库有 `.licenserc.yaml`/`.licenserc-docs.yaml` 表明有许可扫描纪律，但示例资产条目未逐个打开核对）。若复用任何示例资产，须逐项核对。另注意 `rtx` extra 暗示渲染相关可选依赖与 NVIDIA 生态的边界，未核 | github.com/newton-physics/newton（根目录文件清单，api.github.com） |

与本仓库红线的关系：全链 Apache-2.0 + CC-BY-4.0，**无 AGPL/GPL 冲突**；但有声明保留义务（Apache-2.0 NOTICE/声明条款）。

## 4. 硬件要求（显存/驱动/OS，来源+日期）

| 维度 | 官方口径 | 来源 |
|---|---|---|
| OS（Newton） | README："OS: Linux (x86-64, aarch64), Windows (x86-64), or macOS (CPU only)"；兼容页测试矩阵："Ubuntu 22.04/24.04 (x86-64 + ARM64), Windows, macOS (CPU only)"。**Windows x86-64 官方支持** ✅ | github.com/newton-physics/newton（README）；newton-physics.github.io（docs/guide/compatibility.html）· 2026-11-05 |
| OS（MuJoCo-Warp） | **官方未列 OS 矩阵**（README 只谈 GPU/CPU）→ Windows 可用性属推断，见下方 UNVERIFIED | github.com/google-deepmind/mujoco_warp（README raw）· 2026-11-05 |
| GPU（Warp 层，Newton/MJWarp 共同依赖） | "require an NVIDIA R580-series or newer driver and a Turing (`sm_75`) or newer GPU for CUDA acceleration"；Windows/Linux wheel 同时支持 CPU 执行与 CUDA 加速；macOS 仅 CPU | github.com/NVIDIA/warp（README raw）· 2026-11-05 |
| GPU（Newton 测试矩阵） | 测过的 GPU："NVIDIA Ada Lovelace, Blackwell" | newton-physics.github.io（docs/guide/compatibility.html）· 2026-11-05 |
| CUDA 工具链 | "No local CUDA Toolkit is required"（README，wheel 自带）；推荐 CUDA 13.4+（Warp 1.18 wheel）；CUDA 12 在 Newton 1.7.x 仍可用（Warp 1.17 wheel） | github.com/newton-physics/newton（README）；newton-physics.github.io（compatibility.html） |
| Python | >=3.10（3.10 弃用、1.9 移除；CI 覆盖 3.10–3.14） | pypi.org/pypi/newton/json；github.com（README） |
| **显存** | **官方无任何显存数字**（Newton README/docs、MJWarp README/PyPI 均未给出）→ **UNVERIFIED**。对照：本机 RTX 4060 Laptop 8 GiB（D1 实测，见闭环笔记 §0.5）；本机 CC 8.9（Ada）满足 Turing+ 门槛，驱动是否 ≥R580 未查（可用 `build\runtime_probe.exe --json` 实测） | —— |

**UNVERIFIED（显式声明）**：
1. **MuJoCo-Warp 在 Windows 上可运行**——官方未声明；推断依据：MJWarp 为纯 Python 包、其 JIT 底座 Warp 官方提供 Windows x86-64 wheel（CPU+CUDA 双支持）（github.com/NVIDIA/warp README）。无 MJWarp 官方 CI/文档证据，**不作为决策依据**；判定方式见 §7。
2. **Newton/MJWarp 在 8 GiB 显存上的可行性**——无官方数字、未实测。
3. 本机 NVIDIA 驱动版本——本次未运行 `runtime_probe.exe`（未跑的检查不报告为已跑）。

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

| 接口面 | 内容 | 来源 |
|---|---|---|
| **API** | Newton：modern Python API（`pip install "newton[examples]"`；核心依赖 `warp-lang>=1.17.0`）；`newton.ik` 模块；概念页覆盖 Collisions/Contacts、Deformables、Coupled Solvers、Actuators、USD Parsing | pypi.org/pypi/newton/json；newton-physics.github.io（overview + 侧栏） |
| **MJWarp 接入路径**（官方列名） | ① 经 **MJX**（JAX）；② 经 **Isaac Lab**（through Newton，PyTorch）；③ 经 **mjlab**。MJWarp 直接用 `pip install mujoco-warp`，可加载 MuJoCo 模型（drop-in，见 §2.1 缺口） | github.com/google-deepmind/mujoco_warp（README） |
| **资产格式** | 导入：URDF / MJCF / USD（Newton）；MJWarp 沿用 MuJoCo 的 MJCF→MJB 编译链；Newton 有 `importers` extras（trimesh、usd-core、newton-usd-schemas、open3d 等）与 OpenUSD 资产结构协作声明（公告：Disney Research/DeepMind/Intrinsic/NVIDIA 在定义机器人 OpenUSD 资产结构） | newton-physics.github.io（overview）；pypi.org/pypi/newton/json；developer.nvidia.com/blog/announcing-newton… |
| **导出/互操作** | OpenUSD 框架原生支持（官方强调 "Newton emphasizes GPU-based computation, OpenUSD support, differentiability, and user-defined extensibility"）；未查到独立的状态快照/导出格式承诺 → 逐 tick 状态镜像需走其 Python API 自取 | github.com/newton-physics/newton（README） |
| **控制面** | 无 ROS 层、无 HTTP/gRPC 服务面承诺（官方材料未提及）；动作/控制语义位于上层框架（Isaac Lab / mjlab），不在 MJWarp 本体——对 OmniWorld 意味着 Action Channel 若对接，须穿过 Python 边界或上层框架 | github.com/google-deepmind/mujoco_warp（README）·（负面结论=官方材料缺失，2026-11-05 查） |
| **与 MuJoCo 本体的关系** | MuJoCo 本体是 C/C++ 库（Apache-2.0，可嵌入，闭环笔记已核）；MJWarp 是同一模型格式的 GPU 加速执行器——**资产可共享（同一 MJCF），执行后端可互换**，这给"CPU 嵌入式 MuJoCo 起步 → GPU 后端升级"留了同一资产格式的通道 | github.com/google-deepmind/mujoco_warp（README）；闭环笔记 §5/§8.1 |

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：②适配器候选（物理执行后端），边界类型 "Simulation adapter"**（与 map 中 CARLA、Isaac Sim 同列）。

理由：
1. **闭环笔记已裁定座位**（§2）：闭环 = 引擎 ↔ 仿真器，仿真器是**外部真值权威**（方案 b，与 D3 `TruthWorld::load_annotations` 同构），引擎不写仿真器、信念永不回写真值。Newton/MJWarp 恰好是"外部动力学权威"的实现体，接口形状就是读状态镜像 + 发观测 + 收动作——纯适配器语义。
2. **PLAN §二 ② 的先例一致**：CARLA/Isaac 类仿真器登记为 Adapter，回归条件"某 phase 的 benchmark 需要它即回归"（闭环笔记 §0.1）。MJWarp/Newton 属同一家族、同一条件。
3. **不可能进核心**：Python-only（§2.2）与参考模式零外部依赖红线（ADR-0012：默认 ORT OFF 即该承诺的构建级证明）互斥；它的正确物理位置是**进程外后端**，与 ORT 适配器"可选依赖、独立构建目录"的治理方式同构但更进一步（连进程内都不在）。
4. **不是 ③技能项目/⑤让渡**：无训练栈、无创作工具面；也不是竞品——它不评测世界数据，只执行动力学。
5. **弱"评测对象"成分**：闭环延迟预算的 `act` 段与 J2（逐字节可复现 episode）一旦做 spike，MJWarp 就是**被测系统的一部分**——这是适配器角色的自然延伸，不是独立归宿。

**Do not copy**：求解器/内核实现（XPBD/VBD/MJWarp kernel）、Warp 的 Python 栈、其 USD schema 设计照搬。**可研究**：GPU 批量仿真如何声明确定性与重放（对 J2 的直接参照）、夜间基准的发布形态（对 `benchmarks/regression/` 唯一证据源纪律的参照）、资产格式通道（MJCF 共享使 MuJoCo↔MJWarp 后端可互换）。

## 7. 未解决问题 + 重查触发条件

| # | 未解决问题 | 触发条件 |
|---|---|---|
| 1 | **MJWarp 官方 Windows 支持**（本卡 UNVERIFIED #1）——Phase 4 MuJoCo spike 排期时 `pip install mujoco-warp` + 本机 RTX 4060/Windows 实测一步到位；同时盯 MJWarp README/docs 是否补 OS 矩阵 | Phase 4 L1 启动，或 MJWarp 文档更新 |
| 2 | **8 GiB 显存可行性**（UNVERIFIED #2）——spike 中以最小 humanoid/box 场景实测 batch×显存；官方若发布硬件需求页则更新 | spike；或官方发布硬件页 |
| 3 | **GPU 仿真的确定性/seeded-replay 语义**（§2.2 末条）——直接决定闭环笔记判据 J2 能否在 MJWarp 后端上成立；查其 issue 跟踪/变更日志，spike 实测同 seed 两次运行位级比对 | 闭环 spike；J2 被列为 Gate 前置 |
| 4 | **MJWarp 功能缺口是否触及最小任务**（PGS/IMPLICITFAST midpoint/PLUGIN 执行器、Flex 实验性）——"把物体推到目标点"类最小任务大概率不触雷（属于刚体基础域），需 spike 确认 | 闭环 spike 任务定义时 |
| 5 | **Newton 版本锚定**——1.8.0rc1 已出、1.9 将移除 py3.10、破坏性变更落在 minor：进入适配器设计前必须锁版本并写明升级策略 | 适配器 ADR 前 |
| 6 | Newton 示例资产逐项许可（§3 UNVERIFIED）；LF 公告未指明具体治理子实体（README 只说 "a Linux Foundation project"）——低优先级 | 复用任何示例资产前 |
| 7 | 版本事件用日期小节追加（本卡为活文档，先例：D1 note §6.1） | 每次 Newton/MJWarp major/minor 发布或许可变更 |

## 8. 来源清单

全部检索日期 **2026-11-05**（仓库内引用除外，另注）。

**Newton**
- README：https://github.com/newton-physics/newton
- LICENSE.md 原文：https://raw.githubusercontent.com/newton-physics/newton/main/LICENSE.md
- 仓库元数据（license/description/活跃度）：https://api.github.com/repos/newton-physics/newton
- PyPI JSON（版本 1.7.0 / 1.8.0rc1、依赖、日期）：https://pypi.org/pypi/newton/json ；改名提示页：https://pypi.org/project/newton-physics/
- 文档首页（版本 1.7.0、CC-BY-4.0 页脚）：https://newton-physics.github.io/newton/stable/
- 文档 Overview（定位/solver/导入格式）：https://newton-physics.github.io/newton/stable/guide/overview.html
- 文档 Compatibility（OS/CUDA/GPU/API 稳定性策略）：https://newton-physics.github.io/newton/stable/guide/compatibility.html
- NVIDIA 公告博客（GTC 2025，2025-03-18）：https://developer.nvidia.com/blog/announcing-newton-an-open-source-physics-engine-for-robotics-simulation/
- Linux Foundation 公告（2025-09-29）：https://www.linuxfoundation.org/press/linux-foundation-announces-contribution-of-newton-by-disney-research-google-deepmind-and-nvidia-to-accelerate-open-robot-learning

**MuJoCo-Warp**
- 仓库页：https://github.com/google-deepmind/mujoco_warp
- README raw（定位/缺口/维护声明原文）：https://raw.githubusercontent.com/google-deepmind/mujoco_warp/main/README.md
- LICENSE 原文：https://raw.githubusercontent.com/google-deepmind/mujoco_warp/main/LICENSE
- 仓库元数据：https://api.github.com/repos/google-deepmind/mujoco_warp
- PyPI（3.17.0，2026-10-22；extras；license_expression）：https://pypi.org/project/mujoco-warp/

**底层依赖与关联**
- NVIDIA Warp README（平台/驱动/许可）：https://raw.githubusercontent.com/NVIDIA/warp/main/README.md
- MuJoCo PyPI JSON（description 自述 Apache-2.0，顺手印证）：https://pypi.org/pypi/mujoco/json

**仓库内引用（不重核）**
- MuJoCo Apache-2.0 可嵌入结论：[docs/17-research/2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) §5（LICENSE 原文 2026-10-27 查）· §2（引擎↔仿真器所有权模型）· §8.1（MuJoCo 最小任务建议）
- 卡结构规格：[docs/17-research/projects/README.md](README.md) · 索引：[docs/17-research/third-party-research-map.md](../third-party-research-map.md)

---

## 版本事件

### 2026-11-05 · 首建
- Newton 1.7.0（2026-10-14）/ MuJoCo-Warp 3.17.0（2026-10-22）时点首建；Newton 1.8.0rc1 已发布（2026-11-04）。

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-newton-mujoco-warp.md](deep/2026-11-05-newton-mujoco-warp.md)
