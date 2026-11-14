# Newton / MuJoCo-Warp 深度调研报告（W04 深度层）

> Trust label: [Research] —— 一等来源（论文原文 / 官方技术文档 / 官方源码 / 官方博客公告）；每条机制性陈述附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建
> 配套事实卡：[../projects/newton-mujoco-warp.md](../projects/newton-mujoco-warp.md) —— 许可 / 硬件 / 平台 / 版本 / 性能宣传口径等事实只链接不复述。

**方法与证据形态说明**：本报告的机制性陈述锚定在四类来源上——① 官方技术文档（Warp docs v1.19、MuJoCo/MJWarp readthedocs、Newton docs 1.7.0）；② 官方源码（`google-deepmind/mujoco_warp` main 分支 7 个 `_src` 模块，2026-11-05 经 raw.githubusercontent.com 抓取到本地快照逐行核对，下文引用行号以该快照为准）；③ 论文原文（arXiv VBD、作者页 XPBD PDF）；④ NVIDIA 官方博客。源码行号随 main 分支漂移，引用时均注明"main 分支，2026-11-05 取得"。

---

## 0. 摘要

**它是什么**：MuJoCo-Warp（MJWarp）是 DeepMind 与 NVIDIA 联合维护的"用 NVIDIA Warp 语言重写的 MuJoCo"——同一套 MJCF 模型语义、同一套凸优化约束求解理论，但执行主体从 C 库换成 JIT 编译的 GPU kernel；Newton 是架在 Warp 与 MJWarp 之上的多求解器框架层（另有 XPBD/VBD/Featherstone 等 8 个求解器）。三者构成三层栈：**Warp（kernel 语言）→ MJWarp（MuJoCo 管线的 GPU 实现）→ Newton（框架）**。

**核心原理一句话**：把 MuJoCo 的确定性 CPU 管线逐 stage 重写为 Warp kernel，用 `(nworld, ·)` 批处理数据布局让数千个独立世界共享同一份编译产物并行推进，用 CUDA Graph 把一次 `step` 的上百次 kernel launch 压成一次重放——**单步延迟让位于批量吞吐**。

**最关键的三个机制**：
1. **Warp kernel 编程模型**：静态类型 Python 子集 + `@wp.kernel`，与 CUDA kernel 一一对应，经 NVRTC/LLVM 编译成 CUDA/C++（§2.2/§2.3）；
2. **批处理数据布局**：`mjw.Model`/`mjw.Data` 的每个设备数组带世界前导维，接触/约束用**固定容量缓冲**（`nconmax`/`njmax`）预分配，溢出是未定义行为、靠 bitmask 自查（§2.4）；
3. **per-world 并行求解 + 全局早退**：约束求解按世界并行，islands/睡眠/compact solver 处理稀疏活动，`wp.capture_while` 以"未收敛世界计数"驱动 CUDA Graph 条件循环，全部世界收敛即停（§2.5）。

**对本项目的意义**：它是闭环笔记裁定座位（引擎 ↔ 仿真器，仿真器=外部真值权威）里"GPU 执行后端"的事实候选。它的**确定性缺口**（GPU atomic 默认破坏逐位复现，详见 §2.7）恰好打在闭环判据 J2"逐字节可复现 episode"的正面上——spike 时必须先实测，不能引用任何宣传口径。

---

## 1. 项目定位与生态位

Newton 由 NVIDIA、Google DeepMind、Disney Research 发起，2025-09 捐入 Linux Foundation；MJWarp 官方定义为 "an implementation of MuJoCo written in Warp and optimized for NVIDIA hardware and parallel simulation"（[MJWarp docs](https://mujoco.readthedocs.io/en/latest/mjwarp)，2026-11-05），README 声明由 "Google DeepMind and NVIDIA as part of the Newton project" 维护。定位、治理、版本节奏、70x/100x 性能宣传口径：见事实卡 §1/§2/§4（不重核）。

生态位归宿：**②适配器候选（物理执行后端）**，与闭环笔记 §2 裁定的"引擎 ↔ 仿真器"座位同构——详见事实卡 §6；本报告 §4 从 World Contract 四维深化"为什么这个座位成立"。

与族内其他成员（MJX、mjlab、Isaac Lab）的关系一句话：MJX 是 JAX 侧的 MuJoCo GPU 化（可微、TPU 友好），MJWarp 是 NVIDIA GPU 侧（吞吐优先），mjlab/Isaac Lab 是其上的训练框架，MuJoCo 本体是同一 MJCF 资产格式的 CPU 权威实现——**资产可共享、执行后端可互换**（事实卡 §5 已载）。

---

## 2. 原理详解

### 2.1 架构总览：Warp → MJWarp → Newton 三层栈

NVIDIA 官方博客对本仓选型栈的分层定义最直白（[HF blog, NVIDIA, 2026-09-23](https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp)，2026-11-05）：

| 层 | 官方定义（原文） |
|---|---|
| NVIDIA Warp | "Python kernel language: single instruction, multiple threads (SIMT), autodiff, PyTorch/JAX interop" |
| MJWarp | "MuJoCo physics on Warp: same MJCF, batched GPU throughput" |
| Newton | "Multi-solver API, USD, sensors, managers, training loops" |

数据流：**MJCF（XML）→ MuJoCo 编译器（mjModel）→ `mjw.put_model` 上载为设备上的 `mjw.Model` → `mjw.make_data`/`put_data` 分配批处理 `mjw.Data` → `mjw.step(m, d)` 一次推进整个批次**。博客对这条管线的描述："MuJoCo loads and compiles the MJCF model; MJWarp implements the physics in NVIDIA Warp, which compiles CUDA kernels to advance simulation states on NVIDIA GPUs"（同上）。

Newton 层的包装关系：官方文档明说 "SolverMuJoCo wraps mujoco_warp behind Newton's standard solver interface"（[Newton docs, solvers/mujoco.html](https://newton-physics.github.io/newton/stable/solvers/mujoco.html)，2026-11-05）。Newton 自己不重写物理，而是提供 ModelBuilder/Model/State/Control/Contacts 的统一抽象 + 多求解器插槽 + USD/URDF/MJCF 导入（[Newton docs, guide/overview.html](https://newton-physics.github.io/newton/stable/guide/overview.html)，2026-11-05）。

**为什么分三层**：kernel 语言（可复用的 GPU 编程底座）、物理语义（MuJoCo 的凸优化约束求解理论）、框架（资产/多求解器/训练集成）各自演进速度不同——MJWarp 版本号跟随 MuJoCo 3.x（事实卡 §1），Warp 独立发版，Newton 锁定前两者的兼容区间（Newton 1.7.0 的 `sim` extra 锁 `mujoco-warp~=3.15.0`，事实卡 §2.2）。分层让"同一 MJCF 资产、三种执行后端（MuJoCo CPU / MJWarp / MJX）"成立，这是官方 FAQ 明说的对比维度（[MJWarp docs](https://mujoco.readthedocs.io/en/latest/mjwarp)）。

**本报告的机制拆解**：§2.2/§2.3 讲 Warp 层（kernel 模型 + 代码生成），§2.4/§2.5/§2.6 讲 MJWarp 层（数据布局/求解器/碰撞），§2.7 讲贯穿三层的确定性性质，§2.8 讲 Newton 层（可微性 + 求解器家族 + world 批处理）。

---

### 2.2 机制一：Warp 的 kernel 编程模型（与 CUDA 的关系）

**怎么做。** Warp 的编程面是**带类型标注的 Python 子集**：

- kernel 用 `@wp.kernel` 装饰，"all the function input arguments must be statically typed"；kernel 不返回值，结果通过 `wp.array` 参数写出（[Warp docs, user_guide/basics.html](https://nvidia.github.io/warp/stable/user_guide/basics.html)，2026-11-05）。
- 启动与 CUDA 一一对应："To launch a kernel with 1024 threads, we use `wp.launch()`"——`dim` 支持 1D–4D 网格，线程内用 `tid = wp.tid()` 取全局逻辑线程号。文档原话："**Conceptually, Warp kernels are similar to CUDA kernels**"，kernel body "is executed a certain number of times in parallel"，与 CUDA kernel 有 "a one-to-one correspondence"（同上）。
- 数据容器是 `wp.array`（至多 4 维、固定大小、同构 dtype、宿主或设备内存），Python 侧切片是零拷贝视图，标量索引在 Python 侧被有意禁用（同上）。
- 设备函数用 `@wp.func` 定义，可被 kernel 调用、可重载、可多返回值；例外是不能调 `wp.tid()`（同上）。
- 与 NumPy 式"逐算子向量化"框架的本质区别：Warp 是**每线程执行一次 kernel body**的编译模型，NumPy 是 host 侧对整个数组的批量算子调用——后者每次算子调用都要付 launch/中间数组开销（同上）。

**为什么这样设计。** 两个取舍：(a) **SIMT 显式性**——`wp.tid()` 把"当前线程拥有哪个点/接触/体/世界"显式交给程序员，NVIDIA 博客将其列为机器人仿真三价值之一："Explicit parallel work. wp.tid() identifies the point, contact, body, or world owned by the current logical thread"（[HF blog](https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp)）。(b) **语言分工**——"The kernel language is a performance-oriented subset of Python, while ordinary Python remains responsible for configuration, allocation, and launch orchestration"（同上）：普通 Python 留在宿主做编排（这正是 MJWarp 用 Python `dataclasses` 组织 Model/Data 的原因），kernel 语言被限制到能做静态编译的子集。

**代价。** ① 语言子集限制：kernel 不能读任意 Python 全局、不支持 tuple 初始化与 Python list、必须显式类型（Warp docs basics）。② 每个 kernel 是"一逻辑线程一元素"的映射，数据布局（coalesced 访问）由程序员负责——Warp 文档说明多维行主序线程排序与默认连续数组布局对齐以获得 coalesced 访问（同上）。③ kernel 与宿主代码是两套执行语义：`@wp.func` 可以直接从 Python 调但"then run under the normal interpreter (experimental)"——同一函数两条性能路径（同上）。

---

### 2.3 机制二：Warp 代码生成与执行（JIT / SSA / 缓存 / CUDA Graph / AOT）

**怎么做。** 这一层是"Python 源码 → 原生代码"的编译器管线（[Warp docs, user_guide/programming_model/code_generation.html](https://nvidia.github.io/warp/stable/user_guide/programming_model/code_generation.html)，2026-11-05）：

1. **按模块 JIT**：kernel 按 Python 模块分组编译；"When a kernel is launched, Warp checks if the module is up-to-date and will compile it if needed"。
2. **交给原生编译器**："Warp passes the generated source code to native compilers (e.g., **LLVM for CPU and NVRTC for CUDA**) to produce executable code"；Warp 静态链接 NVRTC，因此无需本地 CUDA Toolkit（与事实卡 §4 "No local CUDA Toolkit is required" 一致）。
3. **生成 SSA 形式**："The generated code follows static-single-assignment (SSA) form"，带回指源码行的注释；同 module 会同时生成 forward、gradient（及可选的 custom replay）三个版本。
4. **常量折叠**：kernel 捕获的外部值类型变量"are folded into the kernels by value"；**数组不能被捕获**（持有设备指针），必须作为 kernel 输入传递——这是 MJWarp 全部状态走 `(nworld, ·)` 数组参数的语言层根因。
5. **`wp.static(...)`**：定义期执行任意 Python，用于常量折叠、编译期分支消除、循环展开、函数指针选择——MJWarp 大量用它做 kernel 特化（如 `solver.py:1703` 的 `_solve_init_dof(warmstart, sparse, ...)` 工厂，main 分支快照）。
6. **缓存与 AOT**：编译产物缓存在 `kernel_cache_dir`（热路径 ~165ms → ~5ms）；`wp.compile_aot_module()` 可在其他机器预编译 `.cu/.ptx/.cubin`。
7. **CUDA Graph**：宿主一次 `wp.ScopedCapture()` 内的 kernel launch 序列可捕获为 graph，`wp.capture_launch` 一次重放。NVIDIA 博客强调其语义边界："Graph capture replays launches against existing buffers; it does not fuse arbitrary kernels"（[HF blog](https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp)）。

**为什么这样设计。** JIT + 特化让"一个 kernel 定义 → 按 dtype/形状/常量展开出多份机器码"成立，这正是 MJWarp 能把 MuJoCo 的上百个 C 函数表达为参数化 kernel 工厂的前提（`@cache_kernel` 装饰器 + `wp.static` 编译期常量，见 §2.5 的求解器工厂）。CUDA Graph 则针对 MJWarp 的负载形态："MJWarp functions comprise many kernel launches; capture with `wp.ScopedCapture()` and launch via `wp.capture_launch(capture.graph)` for significantly faster repeated calls"（[MJWarp docs, Performance Tuning](https://mujoco.readthedocs.io/en/latest/mjwarp)）。

**代价。** ① **首跑编译延迟**：博客实测提醒 "The first run may also spend several minutes compiling kernels"（HF blog）。② **缓存陷阱**：运行期新增 kernel 触发模块重编译；升级后陈旧缓存要 `wp.clear_kernel_cache()`（MJWarp FAQ 同口径）；模块重载还会破坏 graph capture。③ **Graph 约束**：替换缓冲、改 `nworld`、改模型都要重新 capture（HF blog）；Newton 文档补充：capture 期间宿主侧校验被跳过，"`cone-scale` validation … are 'left pending until the next eager solref update'"——模型变更的校验应在 capture 外做（[Newton docs, solvers/mujoco.html](https://newton-physics.github.io/newton/stable/solvers/mujoco.html)）。

---

### 2.4 机制三：MuJoCo 管线的 GPU 化——批处理数据布局与 stage 镜像

**怎么做。** 两条主线：数据布局和管线结构。

**(a) 数据布局：`(nworld, ·)` 前导维 + 固定容量缓冲。**

- 类型映射：`mjModel → mjw.Model`（`put_model`），`mjData → mjw.Data`（`put_data` 保状态 / `make_data` 新分配）；关键差异是"fields are Warp arrays on device, and Some fields are missing… for features that are unsupported"（[MJWarp docs, Basic Usage](https://mujoco.readthedocs.io/en/latest/mjwarp)）。`put_model` 兼具兼容性检查职能：模型含不支持特性时**抛错而非静默丢弃**（HF blog："it raises if the model uses unsupported features rather than silently dropping them"）。
- 每个设备数组带世界前导维："Every device array carries a leading world dimension"——宿主状态要以 `mjd.qpos[None, :]` 形状 `(1, nq)` 上载；"Scaling to thousands of worlds later changes only that leading dimension, not the calls"（HF blog）。
- 批量维度分配参数：`nworld`（世界数）、`nconmax`（单世界期望接触数，总容量 ≈ `nconmax × nworld`）、`naconmax`（全局接触总容量，优先级更高）、`njmax`（单世界约束硬上限，**严格强制**）（MJWarp docs）。这些是**编译期固定的预分配缓冲**，不是动态结构。
- **溢出语义**：固定缓冲溢出是未定义行为；`Data.overflow` 是每世界 bitmask（`mjw.OverflowType`），`Option.warn_overflow` 默认打印 GPU 警告——"which serialize threads and hurt throughput"；官方建议"Check overflows periodically, not per step, to avoid device-to-host syncs"（MJWarp docs）。NVIDIA 博客把纪律说得更硬："Exceeding those capacities invalidates the affected rollout for verification or benchmarking, even when execution continues with an overflow warning"（HF blog）。
- 模型侧批处理："Many `mjw.Model` fields have a leading batch dimension (default 1, applied to all worlds)；per-field batch sizes can be set via `put_model(mjm, batch_sizes={...})`，indexed as `field[worldid % field.shape[0]]`"——支持 per-world 质量域随机化，但官方承认 "MJWarp does not provide utilities for construction of dependent per-world field variants"（MJWarp docs, Batched Model Fields）。

**(b) 管线结构：对 MuJoCo CPU 管线的 stage 级镜像。**

MuJoCo CPU 端的权威管线（[MuJoCo docs, computation/index.html](https://mujoco.readthedocs.io/en/stable/computation/index.html)，2026-11-05）分四段：**Position stage**（`mj_kinematics` → `mj_comPos` → `mj_makeM`/`mj_factorM` → `mj_collision` → `mj_makeConstraint`/`mj_island` → `mj_projectConstraint` …）、**Velocity stage**（`mj_comVel`、被动力、`mj_referenceConstraint`、`mj_rne`）、**Control callback**（`mjcb_control`）、**Force/acceleration stage**（`mj_fwdActuation` → `mj_fwdAcceleration` → `mj_fwdConstraint` → 积分器）。文档同时强调其命令式本质："The MuJoCo computation pipeline is entirely imperative, nothing happens automatically"——`mj_step` 结束后 `mjData` 中的导出量对应**上一个**状态。

MJWarp 把同一 stage 序列表达为 kernel launch 序列（官方源码 `mujoco_warp/_src/forward.py`，main 分支 2026-11-05）：`_forward`（forward.py:3498–3534）依次为 `sleep.wake`/`update_sleep` → `fwd_position` → `sensor_pos` → `fwd_velocity` → `sensor_vel` → `callback.control` → `fwd_actuation` → `fwd_acceleration` → `solver.solve` → `rne_postconstraint` → `sensor_acc`；`step`（forward.py:3550–3564）在其后按积分器分派 `euler`/`rungekutta4`/`implicit`/`discrete`。函数名与 MuJoCo CPU API 的 stage 函数逐个对应，这是"同一 MJCF 可在 CPU/GPU 后端间互换并做 parity 验证"的结构基础。

**为什么这样设计。** ① **语义复刻优先于重新发明**：MJWarp 的价值声明是 drop-in 的 MuJoCo 替身（事实卡 §1 引 README），镜像 stage 是这个声明的实现手段，也让"Gate 1 单世界 parity 验证"（HF blog 的迁移流程）可行。② **吞吐 vs 延迟的显式分工**：官方文档把两者定义为不同目标——"MJWarp is optimized for throughput (steps per unit time), while MuJoCo targets latency (single-step time). A single MJWarp step is expected to be slower than MuJoCo's"（MJWarp docs）；NVIDIA 博客给出精确定义："Latency is wall-clock time for one simulation step. Aggregate throughput is the total number of world-steps completed per measured wall-clock second"（HF blog）。③ **固定缓冲**：GPU 上动态分配接触/约束数组会摧毁吞吐，预分配 + 溢出 bitmask 是标准取舍。

**代价。** ① 单世界更慢（同上，官方明说）；② 固定预算要人工按"任务中接触最重的瞬间"校准，溢出 rollout 直接作废（HF blog §"Size contact and constraint capacity"）；③ **float32**："MJWarp uses float32 vs MuJoCo's float64 `mjtNum`；solver settings and small friction values can be sensitive"（MJWarp docs FAQ）；④ 功能缺口（IMPLICITFAST midpoint、PGS、PLUGIN 执行器、Flex 实验性）——见事实卡 §2.1，不重述；⑤ per-world 模型变体要手工拼字段（上面引用的官方免责声明）。

---

### 2.5 机制四：约束求解在 GPU 上的组织

**怎么做。** MJWarp 复刻的是 MuJoCo 的**凸优化约束求解理论**，然后把三个维度并行化。

**(a) 求解语义（MuJoCo CPU 侧权威定义）。** MuJoCo 把约束力定义为凸优化的唯一全局解："the constraint forces are defined as the unique global solution to a convex optimization problem"；对偶问题是在可行域 Ω（等式约束无界、摩擦盒约束、接触锥约束）上的严格凸二次代价（[MuJoCo docs, computation/index.html](https://mujoco.readthedocs.io/en/stable/computation/index.html)）。算法面："The Newton and CG solvers work with the reduced primal formulation, while the PGS solver works with the dual formulation"；CG 是 "non-linear conjugate gradient method with the Hager-Zhang formula"；Newton 用精确 Hessian + Cholesky 且 "is the default solver"（同上）。MJWarp 不支持 PGS（事实卡 §2.1），所以 GPU 路径是 Newton/CG 两个 primal 算法。

**(b) 世界内并行：per-world 独立求解 + inertia 分解。** `solver.solve(m, d)`（官方源码 `mujoco_warp/_src/solver.py:4209`，main 分支 2026-11-05）对 `(nworld, njmax)` 形状的 Jacobian/EFM 缓冲启动 kernel——每个逻辑线程块处理一个世界的求解状态。惯性矩阵分解是 per-tree 决策："Per-tree, stored in `qLD`, choosing Warp's tile Cholesky, MuJoCo's sparse L'DL routine, or a diagonal inverse based on tree size"（[MJWarp docs, Differences from MuJoCo](https://mujoco.readthedocs.io/en/latest/mjwarp)）。稀疏 Jacobian 省显存：官方数字，Aloha clutter 基准（nv=136、2048 worlds、njmax=384）sparse `efc.J` 约 84 MB vs dense 约 408 MB（同上，Memory 节）。

**(c) islands / 睡眠 / compact solver。** 官方文档："MJWarp puts stationary objects to sleep and groups bodies into islands, sleeping entire islands. A **compact solver** (for scenes with many total DoFs but few active, e.g., <64) identifies active DoFs, compacts them into a workspace of size `nvmax`, runs Newton with GPU tile ops (blocked Cholesky), and scatters results back"（MJWarp docs, Large scenes / sleeping）。islands 的构建在源码里是 **GPU 上的并查集（DSU）**：`island.py`（main 分支）实现 `_dsu_find`（带路径压缩的原子路径收缩）与 `_dsu_union`（"atomically hook the higher root to the lower root"，用 `wp.atomic_cas` 竞争挂接；island.py:34–71）——即约束树 → 岛的分组本身也是并行 kernel，而非 CPU 预处理。

**(d) 全局早退（与 MJX 的关键差异）。** 官方文档："Unlike MJX, MJWarp's solver early-exits once all worlds converge"（MJWarp docs, Performance Tuning）。源码实现（solver.py:4272–4291）：维护 `nsolving` 计数器（初值 `nworld`），当 `m.opt.graph_conditional` 时进入 `wp.capture_while(nsolving, while_body=_solver_iteration, ...)`——这是 CUDA Graph 的**条件节点**："condition_iteration is a warp array of size 1 … it counts the number of worlds that are not converged, it becomes 0 when all worlds are converged"（solver.py 内注释，main 分支快照）。JAX 路径不兼容 graph conditional，退化为 Python for-loop 固定迭代数（solver.py:4292–4297 内注释："This branch is mostly for when JAX is used"）。

**为什么这样设计。** ① per-world batch 是吞吐模型的必然——RL 采样的收益来自世界间并行，世界内（单机构的 DoF 链）天然串行链长；② islands+睡眠解决"批里大部分世界/子树静止"的浪费——把不动的树整岛睡眠、活动 DoF 紧凑进 `nvmax` 工作区，让 block Cholesky 的 tile 并行度集中在活集上；③ 早退把"所有世界都收敛了还空转迭代"的开销在 graph 层面消除——这是**用 CUDA Graph 条件节点把数据相关的迭代次数保留下来**的少数实用化场景。

**代价。** ① **单大连通机构是弱项**："not as well as MuJoCo for single large kinematic trees"；"improving single connected mechanisms beyond ~60 DoFs remains an active priority"（MJWarp docs）——官方自认的机制性短板；② 固定 `njmax`/`nconmax` 预算 + 溢出 UB（§2.4 已述）；③ compact solver 有前置条件（Newton solver、sleep 开启、`nvmax` 分配时指定、初始 asleep，MJWarp docs）；④ graph conditional 与 JAX 路径分裂——同一求解器两条执行语义；⑤ 求解迭代数过少会阻止收敛（"Too-low values can prevent solver convergence"，MJWarp docs）。

---

### 2.6 机制五：碰撞与接触的 GPU 组织

**怎么做（MJWarp 侧）。** 碰撞是全管线中最不规则的负载，MJWarp 的组织方式（官方源码 `mujoco_warp/_src/collision_driver.py` + MJWarp docs）：

- **broadphase**：`Option.broadphase` 选 NxN 或 SAP；源码中 `sap_broadphase` 沿轴扫掠后，用 Warp tile 操作做分段排序（`_segmented_sort`：`wp.tile_load` → `wp.tile_sort` → `wp.tile_store`，collision_driver.py:660–686，main 分支快照）——碰撞对的排序压缩进 tile 层。
- **narrowphase**：官方维护一张 collider 管线表（`MJ_COLLISION_TABLE`）：primitive 对走专用 kernel，convex/mesh 对走 GJK/EPA 管线，CCD 类对（box-box、mesh、heightfield、SDF）走连续碰撞检测；box-box 默认 GJK/EPA（4 接触点），`mjDSBL_NATIVECCD` 可切回移植自 `engine_collision_box.c` 的专用 primitive collider（最多 8 接触点，官方说 "may improve contact stability for tasks involving box stacking"）；`PLANE<>MESH`（4 vs 3）与 `HFieldCCD`（4 vs mjMAXCONPAIR）与 MuJoCo 有差异（MJWarp docs, Differences / Pair-wise colliders 表）。
- **接触缓冲**：所有接触写入 `d.contact` 固定数组（`nconmax` 布局），配额溢出走 §2.4 的 bitmask 语义；CCD 相关内存随 `ccd_iterations` 与 mesh 顶点/边数伸缩，可用 `nccdmax`/`naccdmax` 单独压缩（MJWarp docs, Memory）。
- **接触传感器的语义差异**：MJWarp 在主管线里算 sensor 数据，导致"每 geom 对固定报一个接触（即使没在碰撞）"——官方 FAQ 直说这是其管线化组织的副作用（MJWarp docs, FAQ: Spurious contacts）。

**怎么做（Newton 侧，独立管线）。** Newton 另有一套 solver 无关的碰撞管线（[Newton docs, concepts/collisions.html](https://newton-physics.github.io/newton/stable/concepts/collisions.html)，2026-11-05）：broadphase 三模式（NxN "O(N²), optimal for small scenes (<100 shapes)" / SAP "O(N log N)" / EXPLICIT 预计算对）；narrowphase 主算法是 **MPR**（"MPR is the primary algorithm for convex shape pairs"，穿透深度不用 EPA），分离距离用 GJK，mesh 对走 BVH 三角查询、预计算 SDF 后是 "O(1) distance lookups"，双方带 SDF 可选 **hydroelastic contact**（"Marching cubes extracts the pressure-balanced contact surface"）；接触归约"bins contacts by normal direction and scores them by spatial distribution and penetration depth"。关键接口契约：narrowphase 输出"lightweight geometric descriptors that decouple the solver from the underlying shape complexity"——"the solver never needs to query mesh triangles or SDF grids; it only works with the contact arrays"（同上）。`SolverMuJoCo` 可选喂 Newton 接触（`use_mujoco_contacts=False`），从而获得 MuJoCo/MJWarp 本体没有的 non-convex mesh/SDF/hydroelastic 接触（[Newton docs, solvers/mujoco.html](https://newton-physics.github.io/newton/stable/solvers/mujoco.html)）。

**世界隔离的碰撞面**：两套管线都把 world 索引当一等过滤维度——Newton 文档："Index -1: Global entities that collide with all worlds"，世界内实体只与本世界交互（Newton docs, collisions）；`replicate()` 一次把模板盖成 N 份世界（例：1024 份）。MJWarp 的同源机制见 §2.8 的 Newton worlds 与其 `worldid` 前导维。

**为什么这样设计。** 碰撞对预测是数据并行的，但**每个对的计算量高度不规则**（球-球 1 次投影 vs mesh-CCD 数百次查询）——所以组织成"broadphase 剪枝 → 固定容量接触缓冲 → narrowphase 按对分派到专用 kernel"，而把不规则性留给对类型表（MJ_COLLISION_TABLE）静态编码。Newton 侧把"接触描述符"与求解器解耦（同上引文），换来的是同一接触数组可喂不同求解器——这是它多求解器架构的地基。

**代价。** ① CCD/ multiccd 内存伸缩需人工压缩；② 接触归约/合成会吃掉接触保真度（官方默认开归约，`reduce_contacts=False` 可关）；③ MJWarp 内置渲染器明确定位为 "a single hit raycaster optimized for high throughput and low fidelity"——"no global illumination, physically based materials, or view-dependent SH above degree 0"（MJWarp docs, Renderer differences）——这是吞吐取向在感知面的直接代价。

---

### 2.7 机制六：批量并行的确定性 / 重放性质（对 J2 最关键的一节）

这一节沿"MuJoCo CPU 确定性 → GPU atomic 破坏 → Warp deterministic 模式 → MJWarp 开关 → 测试与缺口"的链条讲，全部为一手来源。

**(a) MuJoCo CPU 端的确定性声明及其边界。** 官方文档原文："MuJoCo's simulation pipeline is entirely deterministic and reproducible – if a state in a trajectory is saved and reloaded and mj_step called again, the resulting next state will be identical"（[MuJoCo docs, computation/index.html, Reproducibility](https://mujoco.readthedocs.io/en/stable/computation/index.html)）。三个官方警告直接对应本仓库 J2 判据：① "warmstart accelerations … should be saved if bit-wise equality is required"；② "Contact events have high Lyapunov exponents; this is a property of any rigid-body simulator (and indeed of real-world physics)"——任何位级差异经接触积分都会放大；③ "Exact reproducibility is only guaranteed within a single version, on the same architecture"（同上）。

**(b) GPU 上为什么默认破坏。** Warp 官方文档给出机制：**浮点原子操作的求值顺序不确定性**——"If many threads execute `wp.atomic_add(out, i, value)` at the same time, CUDA is free to apply those updates in different orders. **Floating-point addition is not associative**, so different orders can produce different rounded results"（[Warp docs, user_guide/execution_and_performance/deterministic_execution.html](https://nvidia.github.io/warp/stable/user_guide/execution_and_performance/deterministic_execution.html)，2026-11-05）。MJWarp FAQ 的官方回答同口径："No. There may be ordering or small numerical differences between results computed by different executions"，归因 "non-deterministic atomic operations on GPU"；`wp.set_device("cpu")` 路径是确定的；GPU 确定性由 issue #562 跟踪（[MJWarp docs, FAQ](https://mujoco.readthedocs.io/en/latest/mjwarp)）。MJWarp 在求解器/约束构造里大量用 atomics（contact 排布用 `wp.atomic_add` 分槽、DSU 用 `wp.atomic_cas`/`atomic_min`，见 §2.5 源码引用），所以这不是边缘特性而是主路径。

**(c) Warp 的确定性模式：把 atomic 模式重写为可复现算法。** Warp 1.15 起提供 opt-in 的 module 级 codegen 选项（HF blog："deterministic execution, introduced in Warp 1.15"）。机制（Warp docs, deterministic_execution.html）：Warp 识别两类 atomic 模式并重写——**累加型**（返回值被忽略的 `atomic_add`）：每线程先写 (目标, 值) 记录，然后"sorts records into stable order and reduces per group"，类似 CUDA 的 sort-then-reduce-by-key；**分槽型**（消费返回值，`slot = wp.atomic_add(count, 0, 1)`）：改写为 count-scan-write（计数 pass → 稳定排序 → 分段前缀扫描 → kernel 重执行），两次运行的 atomic 调用序列不一致则抛 `wp.WarpDeterminismError`。三档保证：`NOT_GUARANTEED`（默认）；`RUN_TO_RUN`——"Results are reproducible across repeated runs on the **same GPU architecture**"；`GPU_TO_GPU`——跨架构，走更保守的归约路径（避开 CUB reduce-by-key，按 "thread-index order" 固定累加序），"more conservative and can be slower"（同上）。限制：`atomic_cas`/`atomic_exch`/tile atomics 不被重写；确定性 target 不能在同一 kernel 内被读写；需要固定 scatter 容量（下限 1024 条记录，`deterministic_max_records` 可调）（同上）。

**(d) MJWarp 的确定性开关：`opt.deterministic` 位掩码。** MJWarp 定义了自己的 `DeterminismType` IntFlag（官方源码 `mujoco_warp/_src/types.py:308–318`，main 分支 2026-11-05，docstring 原文）：

```python
class DeterminismType(enum.IntFlag):
  """Bitmask for deterministic execution.
    NONE: non-deterministic execution
    CONTACTS: stable contact ordering via geometric radix sort
    CONSTRAINT: canonical constraint row ordering
    ATOMICS: invariant floating-point atomic reductions
    ALL: all determinism features """
```

三个位在源码中的消费点（main 分支快照）：**ATOMICS** → 选择带 `module_options={"deterministic": wp.DeterministicMode.RUN_TO_RUN, ...}` 的确定性 kernel 变体（`constraint.py:3127–3129` 等，solver.py 内多处 `bool(m.opt.deterministic & DeterminismType.ATOMICS)` 分派）；**CONSTRAINT** → 约束行规范化排序 `_sort_constraints`（`constraint.py:6348–6349`）；**CONTACTS** → 接触稳定排序钩子（`collision_driver.py:1238–1239`）。注意：**CONTACTS 的实现在 main 分支当前是空桩**——`_sort_contacts` 的函数体是 `"""Deterministic contact sort.""" # TODO(team): Implementation contact sorting. pass`（collision_driver.py:1154–1157，2026-11-05 取得）→ 见 §5 UNVERIFIED #1。

**(e) 官方测试是"位级比对"形态。** MJWarp 自带 `determinism_test.py`（main 分支 2026-11-05）：协议是 `_NSTEP = 3` 步 × `_NREPEAT = 2` 次，第二次起与首次确定性运行做**逐位比对**（`_bitwise_diff`），使用 `wp.DeterministicMode.RUN_TO_RUN`；不变式字段清单 `_PHYSICS_FIELDS` 覆盖 qpos/qvel/qacc/qfrc_constraint 等 20+ 个字段；覆盖 tree/flex/contact/sleep 场景；`test_flags`/`test_option` 验证位掩码语义（同文件 28–48、417–448、773–856 行）。这证明 MJWarp 的确定性模式是**有测试护栏的已实现特性**（RUN_TO_RUN 档），而非纸面 API。

**(f) 但官方同时明确否认"rollout 级保证"。** NVIDIA 官方博客的限定句是本节最重要的一句："**These are Warp capabilities, not guarantees of differentiability or determinism for an entire MJWarp rollout.**"（[HF blog](https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp)）。加上 MuJoCo CPU 侧的 Lyapunov 警告（§2.7a），链条完整了：**GPU 批量仿真默认不保证逐位复现；opt-in 的确定性模式把同架构内的主要 atomic 源堵上（atomics 位）并规范行序（constraint 位），但接触排序位在 main 上尚无实现，且跨架构、跨版本都不在保证范围内。**

**为什么这样设计。** 确定性是**编译期 module 选项**而非运行时开关（Warp docs："determinism is a module-level code-generation option"）——因为重写的是 kernel 的代码形态（atomic → sort+reduce），不能按次切换；默认关闭保吞吐，需要回归测试/调试的工作负载才付重写开销。MJWarp 把它细分为三个正交位，允许按需只开 ATOMICS（求解器内规约确定性）而不付接触排序的代价。

**代价。** ① 确定性模式更慢且容量受限（固定 scatter 容量、`deterministic_max_records`）；② `atomic_cas`/tile atomics 不可重写——而 islands 的 DSU 恰恰用 `atomic_cas`（§2.5c），这部分顺序无确定性重写；③ 分槽型 atomic 的确定性要求两次运行调用序列完全一致，否则直接报错而非静默兜底（Warp docs）；④ 对本仓库的结论性含义见 §4。

---

### 2.8 机制七：可微性与 Newton 框架层（Tape、求解器家族、world 批处理、SolverMuJoCo 包装）

**(a) Warp 的可微机制：源码变换 + Tape 重放。** 官方文档："By default, Warp generates a forward and backward (adjoint) version of each kernel definition"（[Warp docs, user_guide/differentiability.html](https://nvidia.github.io/warp/stable/user_guide/differentiability.html)，2026-11-05）。`wp.Tape` 记录 forward kernel launch 序列，`tape.backward(loss)` 反向重放 adjoint；梯度经 `requires_grad=True` 数组的 `.grad` 暴露。语义细节：数组被覆写时"correctly propagates gradients only through the final write"（与 PyTorch/JAX 一致）；write-after-read 会破坏梯度（要分配新数组）；自定义梯度用 `@wp.func_grad`、自定义重放用 `@wp.func_replay`；**动态循环不在 backward 中重放/展开**——需要时改静态循环或把循环挪进 `@wp.func`（同上）。可关闭：`enable_backward=False` 跳过反向代码生成省内存——**MJWarp 源码正是这么做的**：`solver.py:39` 设 `wp.set_module_options({"enable_backward": False, ...})`（main 分支快照），且 FAQ 明确 "Differentiability via Warp is not yet available"（issue #500 跟踪，[MJWarp docs](https://mujoco.readthedocs.io/en/latest/mjwarp)）。→ 与事实卡 §2.1 的"可微性在 Warp 层存在、在 MJWarp 主后端尚未兑现"互为印证，此处补充了源码级证据。

**(b) Newton 的求解器家族：坐标系 × 积分格式的矩阵。** 官方文档把 8 个求解器按两个轴组织（[Newton docs, solvers/index.html](https://newton-physics.github.io/newton/stable/solvers/index.html)，2026-11-05）：

| 求解器 | 坐标系 | 积分 | 备注（官方口径） |
|---|---|---|---|
| SolverMuJoCo | generalized | explicit / semi-implicit / implicit-in-velocity | 唯一支持 equality constraints；wrap MJWarp |
| SolverFeatherstone | generalized | semi-implicit | 完全可微 |
| SolverXPBD | maximal | implicit | "enforce joints as pairwise body constraints but do not use the articulation kinematic-tree structure" |
| SolverSemiImplicit | maximal | semi-implicit | 完全可微 |
| SolverKamino | maximal | semi-implicit（Euler / Moreau-Jean） | 实验性；带 kinematic loops 的硬接触机构 |
| SolverVBD | maximal | implicit | 实验性；cloth/soft/rigid 统一；"opt-in unified compliant ALM and a deprecated legacy AVBD path" |
| SolverStyle3D / SolverImplicitMPM | — | implicit | 纯可变形体 / 粒子，无关节 |

XPBD 的论文原文（书目已核：Macklin, Müller, Chentanez, "XPBD: Position-Based Simulation of Compliant Constrained Dynamics", MIG '16, [ACM DL doi:10.1145/2994258.2994272](https://dl.acm.org/doi/10.1145/2994258.2994272)；作者页 PDF [mmacklin.com/xpbd.pdf](https://mmacklin.com/xpbd.pdf) 可达，HTTP 200 application/pdf，2026-11-05）解决的是 PBD 的"约束刚度依赖迭代数与时间步"问题；其核心机制描述（secondary 索引口径）与 Newton 文档"implicit、maximal coordinates"的分类一致——**论文内文本报告未逐条核（PDF 无法在此环境解析），深层算法细节不进正文**，见 §5 UNVERIFIED #3。VBD 有完整 arXiv 原文（[arXiv:2403.06321](https://arxiv.org/abs/2403.06321)，SIGGRAPH 2024，2026-11-05 抓摘要）："a block coordinate descent solution for the variational form of implicit Euler through vertex-level Gauss-Seidel iterations"，宣称 "unconditional stability"，per-vertex 局部更新"achieve reductions in global variational energy with maximized parallelism"——Newton 文档将其列为统一的 cloth/soft/rigid 求解器（experimental）正是该论文主张的工程化。

**(c) Newton 的 world 批处理：索引分组而非对象复制。** 官方文档："Newton enables multiple independent simulations, referred to as worlds, within a single Model object. Each world is an index-based grouping of all primary simulation entities"（[Newton docs, concepts/worlds.html](https://newton-physics.github.io/newton/stable/concepts/worlds.html)，2026-11-05）。实现形态：`finalize()` 后 Model 持有 per-entity world 索引数组（`body_world`/`shape_world`/…）+ 累积起点数组（`*_world_start`）；GPU kernel 用 2D 网格 `(world_count, max_entities_per_world)` 启动，线程用起点数组把 (world, 相对索引) 映射到全局实体索引，"kernels need to check whether the relative entity index is within bounds for the current world"（同上）。world 索引 −1 是全局实体（与所有世界碰撞）。同构异质：完全异质世界（不同实体类型混合）"still under active development and may change without prior notice"（同上）。

**(d) SolverMuJoCo 包装：push / integrate / pull 三相循环。** 官方文档（[Newton docs, solvers/mujoco.html](https://newton-physics.github.io/newton/stable/solvers/mujoco.html)）：`separate_worlds=True`（多世界 GPU 默认）时 "builds a MuJoCo model from the first world only and replicates it across all worlds via mujoco_warp"——所有世界必须结构同构，不匹配构造期抛 ValueError。每步三相：① Newton→MuJoCo 推送（control、state，可选转换 Newton 接触）；② `mujoco_warp` 积分；③ MuJoCo→Newton 拉回状态。**接触不自动回流**——要 `update_contacts()` 显式拉取。Newton 侧默认积分器改 implicitfast（"MuJoCo's default is euler），for better stability on stiff systems"）。这些语义是"外挂仿真器适配器"接口形状的官方参考实现——§4 直接引用。

---

## 3. 能力边界与工程现实

以下只列**官方承认的机制性限制**（许可/平台/显存/版本策略等事实链接事实卡 §2/§3/§4，不重述）：

1. **吞吐-延迟定位**：单世界单步 MJWarp 慢于 MuJoCo CPU；优化目标是批量吞吐（§2.4b 引文，MJWarp docs）。实时控制/交互式界面官方仍指 MuJoCo CPU。
2. **单大连通机构 ~60 DoF**：多 geom/多 DoF 场景优于 MJX，但单棵大运动树不如 MuJoCo，>60 DoF 单连通机构是"active priority"（MJWarp docs）。
3. **确定性缺口**（§2.7）：GPU 默认非确定（atomics）；`deterministic` 位掩码中 CONTACTS 位在 main 无实现；RUN_TO_RUN 只保同架构；rollout 级保证被官方明文否认（HF blog 限定句）。
4. **可微性缺口**：MJWarp 不可微（issue #500）；Newton 宣称可微的部分目前主要落在 SemiImplicit/Featherstone 等"basic diffsim"路径（Newton docs solvers/index）。
5. **固定缓冲预算纪律**：`nconmax`/`naconmax`/`njmax`/`nccdmax`/`nvmax` 溢出 = UB；溢出的 rollout 不得用于验证或 benchmark（HF blog）；`warn_overflow` 的 GPU 打印会串行化线程、伤害吞吐。
6. **float32**：与 MuJoCo float64 的数值差异是官方承认的迁移风险面（solver 参数、小摩擦敏感，MJWarp docs FAQ）。
7. **编译与缓存工程**：首跑分钟级 kernel 编译；升级需清缓存；模块重载破坏 graph capture；运行期改捕获常量需内部 API `mark_modified()`（Warp docs code_generation）。
8. **接触语义差异**：每 geom 对恒报一个接触（传感视角的 spurious contact）；box-box 与 MuJoCo 接触数不同（4 vs 8 两档）；`PLANE<>MESH`/HFieldCCD 计数与 MuJoCo 不一致（MJWarp docs）。
9. **渲染低保真**：单命中 raycaster，无 GI/PBR/高阶 SH（MJWarp docs）——作为"评测观察源"时必须按低保真传感器对待。
10. **warmstart 语义偏差**：MJWarp 恒用 `qacc_warmstart` 初始化，MuJoCo 会比较 `qacc_smooth` vs `qacc_warmstart`——CPU/GPU parity 检查时这是已知偏差源（MJWarp docs, Differences）。

---

## 4. 与 OmniWorld 的关系深化（World Contract：identity / time / ownership / confidence）

座位裁定（②适配器候选、引擎↔仿真器、仿真器=外部真值权威）已在事实卡 §6 与闭环笔记 §2，此处不重复；以下按契约四维讲"这个座位如何落地、哪里要小心"。

**time（③ 单一 tick 单调时钟域）。** MJWarp 的推进语义与契约时间域天然同构：`mjw.step` 是固定 timestep 的单调推进，积分器（Euler/implicitfast/discrete…）决定离散化语义；CUDA Graph 重放 = "同一 tick 函数、不同输入缓冲"的硬件级实现——这恰好是本仓库"tick 即版本"抽象在 GPU 侧的对应物。工程要点：timestep 必须在 CPU 基线与 `put_model` 之前统一设定，否则"every later measurement inherits the mismatch"（HF blog 的 parity 纪律）——与本仓库"延迟数字必须标注模式与环境元数据"是同一类纪律。另外 MuJoCo 的命令式管线语义（`mj_step` 后导出量对应上一状态，§2.4b）意味着**状态戳语义要在适配器里显式对齐**：镜像回来的 qpos/qvel 属于"刚完成的 tick"，传感器/派生量需要一次 forward 才一致。

**identity（② runtime 唯一铸造）。** 差异大于相似。MJWarp 的实体 id（bodyid/geomid/treeid）是 **MJCF 编译期产生的稳定索引**，不是运行时铸造的、也不做关联判定——它的"同一性"由模型文件静态决定。对 OmniWorld 的含义：仿真器侧 id 只能作为 truth world 中实体的**外部别名**进入 Proposal 五元组的 `target`，引擎仍按契约②自己铸造单调 id、自己做关联判定——闭环笔记的"仿真器是外部真值权威、引擎不写仿真器"在此表现为 id 体系的单向引用。Newton 侧的 world 索引分组（−1 全局世界，§2.8c）反而是可借鉴的**批次隔离原语**：它让"同一 Model 里 N 个互不作用的平行世界"成为一等语义，与闭环测量的"多 episode 并行重放"需求同形（若做 J2 spike，这是最自然的并行形状）。

**ownership（④ 单一作者 / ⑤ Proposal 写路径）。** MuJoCo 管线是"每字段单一作者"的活标本：每个 stage 函数独占一批 `mjData` 字段（§2.4b 的 stage 清单即所有权图谱），文档明说"entirely imperative, nothing happens automatically"——没有隐藏写路径。非拥有者的注入点是显式且分相的：`callback.control`（control 相）、per-model Python callbacks（`mjw.Model.callback`：control/passive/act_dyn/act_gain/act_bias/sensor/contactfilter，MJWarp docs Differences）——这等价于契约⑤的"唯一写入口 + runtime 裁决"形状：**外部动作只能走 control 相进入，物理字段结构性不可写**。Newton 包装层的"接触不自动回流、须显式 `update_contacts()`"（§2.8d）则与"信念不自动回写真值"互为镜像——单向数据流在两侧都是明文设计。

**confidence（⑦ 单标量置信度、零裁决权力）。** MJWarp 没有 confidence 通道——它输出的是**真值**（在外部真值权威模型里，仿真器的输出按定义置信度为 1、不参与校准评分）。但有两个工程上可映射到契约⑨（tick 日志 / boundary_events）的自报健康面：`Data.overflow` 每世界 bitmask（§2.4a）与 `solver_niter`（每世界实际迭代数，solver 早退语义的一部分）——前者是"本 tick 该世界接触预算越界"的边界事件，后者是"求解未收敛程度"的隐式置信信号。若做适配器，这两个 bitmask/计数器应原样进 tick 日志（只加字段不改语义规则，与契约⑨ 的演进先例一致）。

**对 J2 的直接结论**：闭环笔记判据 J2（最小任务逐字节复现 episode）在 **CPU MuJoCo 后端上有官方文档级保证**（§2.7a，含 warmstart 保存与同版本同架构边界）；在 **GPU MJWarp 后端上默认不成立**（§2.7b），opt-in 确定性模式提供同架构 RUN_TO_RUN 档且有位级比对测试护栏（§2.7e）但 CONTACTS 位无实现（§2.7d）。spike 时第一步就该是：同 seed 双跑位级比对（默认 vs `DeterminismType.ALL`），并记录 CUDA Graph 重放路径下的结果——任何"GPU 批量重放可复现"的主张在此之前只能是 UNVERIFIED。

---

## 5. 未解决问题 + 证据缺口（UNVERIFIED 清单）

| # | 要点 | 状态与原因 |
|---|---|---|
| 1 | **`DeterminismType.CONTACTS` 的实现现状** | main 分支 `_sort_contacts` 是 `pass` 空桩（collision_driver.py:1154–1157，2026-11-05 取得），而类型 docstring 宣称 "stable contact ordering via geometric radix sort"（types.py:311）——**文档承诺与 main 实现不一致**。3.17.0 release tag 是否含实现**未核**（只核了 main）；3.17.0（2026-10-22 发布）与 main 的先后关系未比对。**UNVERIFIED** |
| 2 | **issue #562（GPU determinism）/ #500（differentiability）的当前进展** | 两 issue 的正文与状态未抓取，只有 docs 页对它们的指向。**UNVERIFIED** |
| 3 | **XPBD 论文的算法细节** | 仅核到书目（ACM DL MIG'16 doi、作者页 PDF 可达 HTTP 200）；PDF 在本环境无法解析，正文的 compliance/迭代机制未逐条核验。Newton 文档的分类（implicit/maximal/pairwise constraints）已核。**UNVERIFIED（论文细节层）** |
| 4 | **Warp `GPU_TO_GPU` 档在 MJWarp 中的暴露** | MJWarp 源码只见 `RUN_TO_RUN` 的使用（constraint.py:3128 等）；是否可配置 `GPU_TO_GPU` 未核。**UNVERIFIED** |
| 5 | **事实卡 UNVERIFIED 项**（MJWarp Windows 官方支持、8 GiB 显存可行性、本机驱动版本、示例资产逐项许可） | 链接事实卡 §4/§7，本次未重核、未运行 `runtime_probe.exe`（未跑的检查不报告为已跑）。 |
| 6 | **70x/100x 性能声称** | NVIDIA 宣传口径，与本仓库工作负载无关；本仓库纪律"没有 benchmark 数据不宣称性能"——链接事实卡 §2.2，不重复论证。 |
| 7 | **mjlab（arXiv:2601.22074）** | NVIDIA 博客参考文献给出该 arXiv 号，未抓取验证。**UNVERIFIED** |
| 8 | **MJWarp 夜间基准的发布形态与数据** | 事实卡 §1 从 README 转述"每晚跑并公布"；本次未打开其基准页核对指标定义与硬件标注。**UNVERIFIED** |
| 9 | **开源问题** | 无——本报告机制性陈述全部来自一手来源（文档/源码/论文/官方博客），未发现来源矛盾；唯一文档-实现不一致已列入 #1。 |

**重查触发**（与事实卡 §7 表对齐）：MJWarp 3.18+ 或 Newton 1.8 正式版发布时重查 #1/#2；闭环 spike 启动时实测 #3（同 seed 位级比对）；若决定走 JAX 路径则重查 #4 与 graph-conditional 分支的语义差异。

---

## 6. 来源清单

全部检索日期 **2026-11-05**（UTC；源码行号指 main 分支该日快照）。

**论文原文**
- VBD：Chen, Liu, Yang, Yuksel. *Vertex Block Descent*. SIGGRAPH 2024. https://arxiv.org/abs/2403.06321 （摘要抓取）
- XPBD：Macklin, Müller, Chentanez. *XPBD: Position-Based Simulation of Compliant Constrained Dynamics*. MIG '16. 作者页 PDF https://mmacklin.com/xpbd.pdf （可达性核验：HTTP 200，application/pdf，2.0 MB）；书目 https://dl.acm.org/doi/10.1145/2994258.2994272

**官方技术文档 —— NVIDIA Warp（docs v1.19.0）**
- 文档索引/目录：https://nvidia.github.io/warp/stable/
- kernel 编程模型（basics）：https://nvidia.github.io/warp/stable/user_guide/basics.html
- 代码生成（codegen/缓存/AOT）：https://nvidia.github.io/warp/stable/user_guide/programming_model/code_generation.html
- 确定性执行：https://nvidia.github.io/warp/stable/user_guide/execution_and_performance/deterministic_execution.html
- 可微性（Tape/adjoint）：https://nvidia.github.io/warp/stable/user_guide/differentiability.html

**官方技术文档 —— MuJoCo / MJWarp**
- MuJoCo 计算章（管线四段/求解器理论/可复现性）：https://mujoco.readthedocs.io/en/stable/computation/index.html
- MJWarp 主文档页（定位/批尺寸/调优/FAQ/差异）：https://mujoco.readthedocs.io/en/latest/mjwarp

**官方技术文档 —— Newton（docs 1.7.0）**
- Overview：https://newton-physics.github.io/newton/stable/guide/overview.html
- Worlds（world 索引分组）：https://newton-physics.github.io/newton/stable/concepts/worlds.html
- Collisions and Contacts：https://newton-physics.github.io/newton/stable/concepts/collisions.html
- Solvers 总览：https://newton-physics.github.io/newton/stable/solvers/index.html
- SolverMuJoCo（wrap MJWarp）：https://newton-physics.github.io/newton/stable/solvers/mujoco.html
- FAQ：https://newton-physics.github.io/newton/stable/faq.html

**官方源码**（google-deepmind/mujoco_warp main 分支，raw.githubusercontent.com，2026-11-05 抓取本地快照核对行号）
- `mujoco_warp/_src/types.py`（DeterminismType:308–318）
- `mujoco_warp/_src/solver.py`（solve:4209、早退:4272–4291、enable_backward:39）
- `mujoco_warp/_src/island.py`（GPU DSU：34–71）
- `mujoco_warp/_src/forward.py`（step:3550–3564、_forward:3498–3534）
- `mujoco_warp/_src/collision_driver.py`（CONTACTS 钩子:1238–1239、_sort_contacts 空桩:1154–1157、tile_sort:660–686）
- `mujoco_warp/_src/constraint.py`（RUN_TO_RUN:3127–3129、CONSTRAINT:6348）
- `mujoco_warp/_src/determinism_test.py`（位级比对协议：28–48、417–448、773–856）
- 模块清单（GitHub contents API）：https://api.github.com/repos/google-deepmind/mujoco_warp/contents/mujoco_warp/_src

**官方博客公告**
- NVIDIA on Hugging Face（2026-09-23）：*How to Use NVIDIA Warp and MJWarp to Accelerate Robotics Simulation and Learning Workflows*：https://huggingface.co/blog/nvidia/how-to-use-nvidia-warp-and-mjwarp
- NVIDIA GTC 2025 公告 / Linux Foundation 公告：见事实卡 §8（2026-11-05 已核，本报告不重复引用）

**仓库内引用（不重核）**
- 事实卡：[docs/17-research/projects/newton-mujoco-warp.md](../projects/newton-mujoco-warp.md)
- 闭环座位与 J2 判据：[docs/17-research/2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) §2/§J2
- World Contract 权威文本：[docs/design/00-总体设计.md](../../design/00-总体设计.md) §2

---

## 版本事件

### 2026-11-05 · 首建
- 基于 Newton 1.7.0 docs / MJWarp 3.17.0 时点（main 分支源码 2026-11-05 快照）+ Warp docs v1.19.0 首建；§2 七个机制小节全部一手来源支撑；UNVERIFIED 九项见 §5。
