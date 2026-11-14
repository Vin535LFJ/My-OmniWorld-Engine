# OpenUSD 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建

## 0. 一句话

OpenUSD 是 Pixar 创造、现由 AOUSD（Linux Foundation 联合开发基金会下的开放非营利组织）推动标准化的 3D **场景描述 + 组合（composition）+ 交换**技术：它用层（Layer）/组合弧/覆盖（override）提供非破坏性资产组织，用 Stage 提供组合求值——**它是"数据模型 + 文件语义"，不是活世界运行时**（该结论来自 [2026-09-21 方向决策](../2026-09-21-direction-decision.md) §4.1，本卡以官方文档逐条复核成立）。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| 官方定义："USD is an efficient, scalable system for authoring, reading, and streaming time-sampled scene description for interchange between graphics applications." | github.com/PixarAnimationStudios/OpenUSD（dev/README.md） |
| intro 定位："the first publicly available software that addresses the need to robustly and scalably interchange and augment arbitrary 3D scenes that may be composed from many elemental assets"；支持"assembly and organization of any number of assets into virtual sets, scenes, shots, and worlds"、"transmitting them from application to application, and non-destructively editing them (as overrides), with a single, consistent API" | openusd.org/release/intro.html |
| 组合能力："a freely combinable set of Composition Arcs that can be used to package, aggregate, vary, and override primitive elements and assets, with a high-performance runtime evaluation engine, embodied in a compact scenegraph known as a Stage" | openusd.org/release/intro.html |
| 六种组合弧："subLayers, inherits, variantSets, references, payloads, specializes"；除 subLayers 外全部可 list-edit（可稀疏 prepend/append/remove/reset） | openusd.org/release/glossary.html |
| LayerStack 官方定义："The ordered set of layers resulting from the recursive gathering of all SubLayers of a Layer, plus the layer itself as first and strongest"；组合弧以 LayerStack（非单层）为目标 | openusd.org/release/glossary.html |
| 强度序官方口径："LIVERPS is an acronym for Local, Inherits, VariantSets, Relocates, References, Payload, Specializes"，是 opinion 与命名空间组合的根本次序；祖先引用弱于直接引用 | openusd.org/release/glossary.html |
| 覆盖机制："Opinions are the atomic elements that participate in Value Resolution"（按 LIVERPS 定序）；Over 是最弱 specifier，"Over is short for 'override' or 'compose over'"，为覆盖 opinion 提供投机性中立容器；Deactivate 是"non-destructive (and reversible) prim/subtree deletion"；list-editing 支持"non-destructively changing the large-scale structure of a scene as it flows down the pipeline" | openusd.org/release/glossary.html |
| 性能姿态："Speed is a primary, ongoing goal of the USD project at Pixar"；"has introduced parallel computation into all levels of the scene description and composition core"（Presto 血统）；usd-core 页："a scenegraph built on top of a multi-threaded 'composition engine'" | openusd.org/release/intro.html；pypi.org/project/usd-core |
| 官方性能度量：官方脚本对 4 个官方资产（Standard Shader Ball、Kitchen Set、ALab〔Animal Logic 制作、300+ 资产〕、Moore Lane）每资产跑 100 次迭代取 min/max，度量 open stage / 渲染首图 / close / Hydra 关停等；样例均值：Shader Ball Linux open 0.128s、首图 1.02s；ALab Linux 首图 6.03s；Moore Lane Linux 首图 7.30s；官方告诫"Interpret the metrics on this page with caution"（机器规格可变、GL 驱动 flush 时序等来源的波动） | openusd.org/release/ref_performance_metrics.html |
| 治理：AOUSD 是"an open, non-profit organization dedicated to promoting 3D data interoperability through OpenUSD"，隶属 Joint Development Foundation；创始成员 Pixar、Adobe、Apple、Autodesk、NVIDIA；AOUSD 自述 OpenUSD "far more than just a file format" | aousd.org |
| Core Spec 1.0 已批准（公告 2025-12-17），normative 覆盖六支柱：USDA 语法与基础数据类型、文件格式无关的组合输入组织模型、组合算法 normative 规范、stage population/遍历/value resolution、USDA/USDC/USDZ 格式、合规验证 rubric；并提供合规测试用 sample implementations | aousd.org/blog/foundations-of-open-3d-development-introducing-aousd-core-specification-1-0；aousd.org/news/core-spec-announcement |
| 发行节奏（实测 GitHub releases API）：v26.03（2026-02-24）→ v26.05（2026-04-24）→ v26.08（2026-07-20）→ **v26.11.1（2026-10-21，当前 release）**；dev 分支文档版本号 27.02 | api.github.com/repos/PixarAnimationStudios/OpenUSD/releases；openusd.org/dev/release_schedule.html |

## 2. 边界与限制

### 2.1 官方承认的限制（原句为准）

- **不做 rigging/执行语义**：intro："not a rigging system"——scenegraph 是"low-memory footprint, higher-latency data access"取向，而高性能 rigging 需要"high-memory footprint, low-latency access to data"；"OpenExec by itself is not a rigging system"。UsdSkel 文档设专节 **"What UsdSkel Is Not"**："UsdSkel does not signal an intent to provide general rigging and execution behaviors as core USD features. We strongly believe that such features are beyond the scope of USD's core concerns."（openusd.org/release/intro.html；openusd.org/release/api/_usd_skel__intro.html#UsdSkel_IsNot）
- **事件驱动/调度边界**：OpenExec 是"a value-driven computation system, as opposed to being event-driven"；"There is no event architecture included with OpenExec that triggers recomputation by, say, clicking on an object in the scene"；"scene authoring is the only 'event' that can induce recomputation"。求值计算"stateless and side-effect free"、"computations cannot modify the topology of the UsdStage"（不能增删 prim/属性/author 新值）、"OpenExec always observes the composed UsdStage"。（openusd.org/release/intro_to_openexec.html）
- **物理只到 schema 表示**：UsdPhysics "defines the physics-related prim and property schemas that together form a physics simulation representation"，且自我定位为"intended as a baseline initial extension to USD that enables the minimum set of common concepts required to represent rigid body physics"；官方页面明言"Future iterations and extensions will incrementally add capabilities as needs are identified"。（openusd.org/release/api/usd_physics_page_front.html）
- **Core Spec 范围排除域词汇**：Core Spec 1.0 "does not dictate the specific vocabulary of a medical textbook or legal contract (that's for domain-specific specs like Physics and Materials)"；1.1 目标才纳入"Animation Spline parity with OpenUSD v25.08+"与"Variable & Path Expressions"。（aousd.org/blog/foundations-of-open-3d-development-introducing-aousd-core-specification-1-0）
- **无 GUID 寻址**："No GUIDS"——用文本层级命名空间标识数据，引用的命名空间变了覆盖会"fall off"。（openusd.org/release/intro.html）
- **Python 精简发行**：usd-core "does not provide any of the optional plugins or imaging features from the full USD distribution"（无 usdview/渲染）。（pypi.org/project/usd-core）

### 2.2 我们的推断（非官方原句，明确标注）

- **不执行物理仿真**：由三件事合成——UsdPhysics 只定义 schema、其 Parsing Utilities 面向消费方（"to help achieve a consistent physics representation as defined in the USD stage"）、Core Spec 把 Physics 划为 domain-specific spec。注意：官方页面**没有**"does not perform simulation"这一明示句，此条为推断。（依据 §2.1 三条）
- **没有调度器/循环驱动概念**：OpenExec 只在"scene authoring"后失效重算；事件、循环、实时约束均在宿主侧。（依据 intro_to_openexec.html 的 value-driven 定性）
- **没有活世界状态语义**：官方概念集（Stage/Layer/Opinion/TimeCode/组合弧）不含**所有权（ownership）、置信度（confidence）、世界会话**的任何对应物；identity 层级命名空间有，time 有 TimeCode/TimeSample——World Contract 四元组的后两项是 OmniWorld 的独有增量。（对照 docs/design/00-总体设计.md §2 术语）
- **USD Fabric 不是 OpenUSD 的一部分**：全景报告 [OpenUSD/USD Fabric 一节](../2026年实时世界运行时与生成式世界模型技术全景报告.md) 描述的 USD Fabric 是 NVIDIA Omniverse 侧的运行时内存布局，非 Pixar/OpenUSD 官方声称；其数值主张（"<5ms"、百万实体 GPU 常驻）方向决策已核为**未证实**（官方文档被 WAF 阻挡；后继 `ovstage` 为 NVIDIA 专有许可）——见 [2026-09-21 方向决策](../2026-09-21-direction-decision.md) §2/§4.2，此处引用不复制。

## 3. 许可（代码 / 权重 / 数据 三分）

| 维度 | 事实 | 来源 |
|---|---|---|
| 代码 | **TOST 1.0**（Tomorrow Open Source Technology License 1.0）。LICENSE 自述："differs from the original Apache License 2.0 in the following manner. Section 6 ('Trademarks') is different"——即 Apache-2.0 派生、仅商标节不同，49,827 字节全文随仓库分发 | github.com/PixarAnimationStudios/OpenUSD（dev/LICENSE.txt） |
| 代码（PyPI 口径交叉验证） | usd-core 许可字段 `LicenseRef-TOST-1.0`，与仓库 LICENSE 一致 | pypi.org/project/usd-core |
| 权重 | **N/A**——OpenUSD 是软件/标准项目，无 ML 权重 | （结构事实） |
| 数据 | 官方性能资产 4 个（Shader Ball、Kitchen Set、ALab、Moore Lane）各自有分发许可；ALab 为 Animal Logic 制作。**其各自许可条款本卡未核验，UNVERIFIED**——引用性能数字无碍，重分发资产前必须查 | openusd.org/release/ref_performance_metrics.html |

## 4. 硬件要求（显存/驱动/OS，来源+日期）

- **OS 支持官方口径**："primarily developed on Linux platforms (CentOS 7), but is built, tested and supported on macOS and Windows"；可构建为嵌入 iOS/visionOS 应用的库；"can be built to run in a WebAssembly virtual machine for use on the web or in other runtime environments such as Node.js"。（github.com/PixarAnimationStudios/OpenUSD dev/README.md，2026-11-05）
- **必需依赖（官方）**：C/C++ 编译器、CMake、Intel TBB——全部 CPU 侧，无 GPU 库。（同上 README "Dependencies"）
- **GPU 定位**：核心 USD（usd-core）明确不含 imaging（§2.1），故核心库无 GPU 要求——**此为推断**（依据 = usd-core 排除声明 + 必需依赖清单，官方无单句声明）。GPU 出现在官方性能度量环境里是因为那些指标含渲染首图：Linux（AlmaLinux 9 / EPYC 9654 / NVIDIA L40S-24Q）、macOS（14.3 / M2 Ultra）、Windows（11 / EPYC 7763 / RTX A6000-24Q）。（openusd.org/release/ref_performance_metrics.html，2026-11-05）
- **显存/驱动要求**：官方核心文档无任何显存/驱动门槛声明； imaging/Hydra 侧按渲染器而定，本卡不展开。

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

| 接口面 | 事实 | 来源 |
|---|---|---|
| C++ 库 | 源码发行：GitHub release 仅有源码 tarball/zipball（实测 releases API，`browser_download_url` 计数为 **0**，无预编译二进制）；v26.11.1 tarball 实测下载 **163,501,208 bytes（156 MB）**；构建入口 build_usd.py（BUILDING.md） | api.github.com/repos/PixarAnimationStudios/OpenUSD/releases；github.com/PixarAnimationStudios/OpenUSD（README/BUILDING.md） |
| Python | **usd-core 26.11**（2026-10-21 发布），wheel：win_amd64 13.1 MB、manylinux x86_64 28.8 MB、aarch64 ~27.8 MB、macOS universal2 41.0–41.2 MB（16 个 wheel 合计 443.5 MB）；Python ≥3.11,<3.15；不含 imaging/usdview/插件 | pypi.org/project/usd-core |
| 资产格式 | .usda（ASCII）/ .usdc（二进制 crate）/ .usd（两者之一）/ .usdz（打包）——Core Spec 1.0 normative 覆盖 USDA/USDC/USDZ | aousd.org/blog/foundations-of-open-3d-development-introducing-aousd-core-specification-1-0 |
| 资产寻址 | "Asset resolution is the process by which an asset path is translated into the location of a consumable resource. USD provides a plugin point for asset resolution, the **ArResolver** interface"；插件可"provide USD with an asset's data without fetching the data to disk"；无插件时回退搜索路径默认 resolver | openusd.org/release/glossary.html |
| 组合/覆盖面 | 六组合弧 + Session Layer + 变体切换 + load/unload payload（打开 stage 后选择性加载） | openusd.org/release/glossary.html；openusd.org/release/intro.html |
| 计算面 | OpenExec：通用求值框架（值驱动、无副作用），随当前 release 文档发布（26.08/26.11 有文档、dev 27.02 有教程）——首入版本号 **UNVERIFIED**（§7.1） | openusd.org/release/intro_to_openexec.html；openusd.org/dev/api/page__execution__system__design.html |
| 渲染/查看 | Hydra/Storm/usdview 仅在完整发行版（非 usd-core） | pypi.org/project/usd-core；openusd.org/release/intro.html |
| 控制面 | **无**——纯库形态，无服务端、无 RPC、无遥测；进程内 API + 文件交换 | （§1/§2 事实的推论：README/PyPI 口径均为库发行） |
| 生态交换 | UsdPhysics schema（刚体/关节/碰撞等）作为外部仿真器的数据交换面 | openusd.org/release/api/usd_physics_page_front.html |

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：External reference（资产组织参照），不是引擎，不是竞品。**

- **既有定位（引用不复制）**：方向决策 §4.1 已裁定——"OpenUSD 是'数据模型 + 文件语义'，不是运行时；它对 OmniWorld 的价值是（Exploration 级别的）单向导出目标，不是竞争对象"；反模式 #2 设永久边界"重造 OpenUSD/Omniverse → 研究/评测侧；OpenUSD 仅单向导出"；research-queue 记"OpenUSD one-way export…explicitly not compliance"，优先级 Low、最早阶段 C 后。本卡官方复核（§2）全部支持该裁定：官方自己声明不做 rigging/执行/事件，Core Spec 把 Physics 划出去，value-driven 无调度——运行时语义恰是它让渡出去的空间。
- **与 World Project 的关系**：OpenUSD 的 layer/variant/override/sparse-over 是行业既成的"资产组织心智"，OmniWorld 的 World Project（资产/工作区组织）应以此为**对照系**吸收其非破坏编辑思想；但 Stage 是无主的组合结果，无 ownership/confidence 语义（§2.2）——World Contract 后两元不向它对齐，也不指望它提供。
- **②适配器候选**：阶段 C 后可做 **usd 单向导出适配器**（World Contract → USD 文件），usd-core 以可选依赖接入（默认 OFF），复制 ONNX Runtime 的构建隔离模式（ADR-0012：可选依赖默认 OFF 即承诺）。只出不出进；不做 USD 导入器、不做组合引擎、不寻求 AOUSD 合规认证。
- **⑤明确让渡**：3D 场景交换文件标准、组合算法、域 schema（物理/材质）归 AOUSD/Pixar 生态——OmniWorld 不定义 3D 交换格式，不在该层竞争。
- **评测对象**：不是——它不是世界模型/智能体/运行时产品，无闭环可测；未来仅可能作为评测资产封装的导出格式出现。
- **竞品参照**：仅在"World Project 组织思想"的窄意义上参照；工业闭环运行时赛道的真正对位物是 Omniverse Mega/ovstage（NVIDIA 专有），已在方向决策 §4.2 判定"这条赛道没有 OmniWorld 的位置"——与本卡互补，不重复。

## 7. 未解决问题 + 重查触发条件

1. **OpenExec 首入版本**：方向决策称"USD 26.08 附带 OpenExec"；本卡实测 GitHub v26.08 release body 为空（仅 13 字符），官方 release notes 页未定位到——**UNVERIFIED**。重查触发：需在文档中引用具体版本时，查 openusd.org/dev/release_schedule.html 或 AOUSD 公告。
2. **usd-core 在 Windows/MSVC 环境的实际运行时行为与磁盘/内存占用未实测**（本卡仅核 PyPI 发布口径：win wheel 13.1 MB）。重查触发：导出适配器立项（阶段 C）时实测。
3. **Core Spec 1.1 进度**（Animation Spline parity with v25.08+、Variable & Path Expressions、Compliance Rubric 改进）。重查触发：AOUSD 发布 1.1 公告。
4. **官方性能资产许可**（ALab 等）未核验。重查触发：需要重分发测试资产或构建离线评测包时。
5. **usd-core 纳入构建体系的成本**（build.bat 之外独立目录 vs. vcpkg 等包管理器，参照 build-ort 模式）未评估。重查触发：适配器进入实现排期。

## 8. 来源清单（全部检索于 2026-11-05）

官方文档（openusd.org）：
- https://openusd.org/release/intro.html
- https://openusd.org/release/glossary.html
- https://openusd.org/release/intro_to_openexec.html
- https://openusd.org/release/api/usd_physics_page_front.html
- https://openusd.org/release/api/_usd_skel__intro.html （"What UsdSkel Is Not"节）
- https://openusd.org/release/ref_performance_metrics.html
- https://openusd.org/dev/release_schedule.html
- https://openusd.org/dev/api/page__execution__system__design.html

官方仓库（github.com/PixarAnimationStudios/OpenUSD）：
- https://github.com/PixarAnimationStudios/OpenUSD （dev/README.md，经 raw.githubusercontent.com 核验）
- https://raw.githubusercontent.com/PixarAnimationStudios/OpenUSD/dev/LICENSE.txt （TOST 1.0 全文）
- https://api.github.com/repos/PixarAnimationStudios/OpenUSD/releases （版本与无二进制 assets 实测）
- v26.11.1 源码 tarball 实测下载：163,501,208 bytes（命令 `curl -o usd_src.tar.gz https://api.github.com/repos/PixarAnimationStudios/OpenUSD/tarball/v26.11.1` + `ls -la`/`du -h`）

标准组织（aousd.org）：
- https://aousd.org/
- https://aousd.org/blog/foundations-of-open-3d-development-introducing-aousd-core-specification-1-0 （Core Spec 1.0，公告 2025-12-17）
- https://aousd.org/news/core-spec-announcement

发行（pypi.org）：
- https://pypi.org/project/usd-core/ （26.11，2026-10-21 发布）

内部指针（引用不复制）：
- docs/17-research/2026-09-21-direction-decision.md §2、§4.1、§4.2、§10（OpenUSD 定位、USD Fabric 未证实项、反模式边界）
- docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md（OpenUSD/USD Fabric 一节）
- docs/17-research/research-queue.md（OpenUSD one-way export 条目）

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-openusd.md](deep/2026-11-05-openusd.md)
