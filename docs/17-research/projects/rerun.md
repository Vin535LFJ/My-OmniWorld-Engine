# Rerun 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建

## 0. 一句话

Rerun 是"物理 AI 的数据层"：一个以 **entity path（身份）× timeline（时间）** 为双轴数据模型、用 Rust 写成、经 Apache Arrow 落盘（.rrd）的多模态时序记录/查询/可视化 SDK——本仓库自研静态查看器路线（`docs/design/ui-viewer/查看器设计.md`）的**对照参照**（买 vs 造的"买"侧样板），不是依赖候选。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
| --- | --- |
| 定位："Visualize, query, and stream to train on multimodal robotics data." / "The data layer for physical AI"；"Log, query, visualize, and stream to training on shared columnar storage built for multimodal data." | github.com/rerun-io/rerun (README) |
| 摄入多速率多模态数据（images, point clouds, transforms, time series, joint states, video），来源含 robot logs / sim，格式含 MCAP、rrd、LeRobot；viewer "renders everything in sync, in realtime"；数据 "queryable with dataframes or SQL"；"Built in Rust on column-chunk storage purpose-built for multi-rate physical data." | github.com/rerun-io/rerun (README) |
| 数据模型双轴之身份轴：`rr.log("path/to/points", ...)` 的 entity path 是层级标识（`/` 分隔，"all entities within Rerun have a unique entity path"） | rerun.io/docs/concepts/entity-path |
| 数据模型双轴之时间轴："Each piece of logged data is associated with one or more timelines."；三类索引（sequence / timestamp / duration）全部编码为 i64；另有 static data（"belongs to all timelines … and shadows any temporal data"） | rerun.io/docs/concepts/timelines |
| SDK 语言："SDKs in Python, Rust, and C++."（仓库树另有 `rerun_js` 与 web-viewer npm 包，属实验性，见 §5） | github.com/rerun-io/rerun (README) |
| 仓库活跃度：创建于 2022-04-08，~11,694★，2026-11-04 仍有更新；最新版本 0.42.0（PyPI 发布于 2026-11-04；crates.io 更新于 2026-11-04） | api.github.com/repos/rerun-io/rerun · pypi.org/project/rerun-sdk · crates.io/api/v1/crates/rerun |
| 商业分层：open-core——"Everything in this repository will stay open source and free (both as in beer and as in freedom)."；商业产品为 Rerun Hub（"a scalable catalog for robotic data"，面向团队） | github.com/rerun-io/rerun (README) · rerun.io/pricing |

## 2. 边界与限制

### 2.1 官方承认的限制

- **rrd 版本兼容是硬边界**："Legacy RRDs whose Version field is older than 0.23 are currently rejected."（RRF0/RRF1 FourCC 识别但拒绝，迁移需旧版 Rerun）；SDK `save` 模式 "only adjacent minor versions are guaranteed compatible"。官方的对策是承诺 0.23+ 可前向迁移（Sorbet in-memory migration + `rerun rrd migrate` CLI）。（rerun.io/docs/concepts/logging-and-ingestion/rrd-format · rerun.io/docs/reference/sdk/operating-modes）
- **framing 稳定但数据模型（Sorbet）可变**："The framing described in File layout is considered stable and we have no plans to change it."——注意这句话只承诺 framing，Sorbet 模式"may change"。（rerun.io/docs/concepts/logging-and-ingestion/rrd-format）
- **web viewer 限制**：拖放文件夹在 web 版不支持；`@rerun-io/web-viewer` API "This API is experimental, and may change between releases."；版本严格匹配（`@rerun-io/web-viewer@0.10.0` 只能连 0.10.0 SDK 的数据源）。（rerun.io/docs/getting-started/data-in/open-any-file · rerun.io/docs/howto/integrations/embed-web）
- **自定义可视化仅 Rust**：文档索引将 extend-ui（Custom visualizations）标注为 Rust only。（rerun.io/docs）
- **Linux/WSL2 环境成本**："Rerun should work out-of-the-box on Mac and Windows, but on Linux you need to first run" 一串 apt/dnf 依赖安装（libclang、GTK3、xcb、xkbcommon…）；WSL2 "graphics drivers won't work out of the box"。（rerun.io/docs/getting-started/install-rerun/troubleshooting）
- **gRPC catalog API 无稳定性承诺**：服务命名仍为 `rerun.cloud.v1alpha1.*`，文档未声明任何稳定契约。（rerun.io/docs/reference/grpc）
- **PyPI 包元数据无 license 字段**：`license`/`license_expression` 为 null、无 license classifier——许可主张只能落在官方 pricing 页与仓库 LICENSE 文件上（见 §3）。（pypi.org/pypi/rerun-sdk/json）

### 2.2 我们的推断（不属官方声称）

- **离线回放成立（合理读法）**：官方列出的三种打开文件方式（CLI 参数 / 拖放 / 打开对话框）全部是本地路径，加载 .rrd/.rbl 无任何联网步骤描述；SDK `save` 模式明确 "Streams all logging data into an .rrd file on disk, which can then be loaded into a stand-alone viewer."。**native viewer 的本地回放不依赖网络**是文档读法；官方没有单独一页承诺"offline"这个词。web viewer 则不同：iframe 路线依赖托管在 app.rerun.io 的 WASM bundle，npm 自托管路线 bundle 随包走——"离线"只在 npm 自托管意义下成立（推断）。（rerun.io/docs/getting-started/data-in/open-any-file · rerun.io/docs/reference/sdk/operating-modes · rerun.io/docs/howto/integrations/embed-web）
- **viewer 依赖 GPU 环境**：官方排障页只讲 Wgpu 后端选择与驱动更新，未提及任何 native viewer 的软件渲染回退；容器/无 GPU CI 场景未见官方说明（推断：native viewer 需要可用 GPU 驱动）。（rerun.io/docs/getting-started/install-rerun/troubleshooting）
- **超长时序的回放性能未实测**：README 提到 "time-aware in-memory database" 配可调 buffer 以适配低内存机器，但我们的 600-tick 级别之外的真实规模（阶段 D 帧序列）无数据。（github.com/rerun-io/rerun README）

## 3. 许可（代码 / 权重 / 数据 三分）

| 层 | 结论 | 来源 |
| --- | --- | --- |
| **代码（仓库 + 三语言 SDK）** | **Apache-2.0 / MIT 双许可**。仓库含 LICENSE-APACHE 与 LICENSE-MIT（README badge 指向两者）；Rust crate 在 crates.io 标 "MIT OR Apache-2.0"；官方 pricing 页逐字："The Rerun SDK is open source under Apache-2.0 / MIT. Use it in research and production, on the laptop and in the cluster, with no asterisks."——该句覆盖全部三个 SDK（含 C++）。open-core 承诺："Everything in this repository will stay open source and free." | github.com/rerun-io/rerun · crates.io/api/v1/crates/rerun · rerun.io/pricing |
| **SDK 分发条款（逐项）** | Python：PyPI `rerun-sdk` 0.42.0，`requires_python ">=3.11"`，wheels：Windows x86-64 (151.6 MB) / manylinux_2_28 x86-64 (174.0 MB) / manylinux_2_28 ARM64 (169.2 MB) / macOS 11+ ARM64 (156.2 MB)；**PyPI 元数据 license 字段为 null**（无 classifier）——许可依据回到上行的官方声明与仓库 LICENSE。Rust：crates.io `rerun` 0.42.0，license "MIT OR Apache-2.0"。C++：GitHub release 的 `rerun_cpp_sdk.zip` 经 CMake FetchContent 分发，无独立条款文件被核验（依仓库双许可 + pricing 页 "The Rerun SDK" 句）。 | pypi.org/pypi/rerun-sdk/json · pypi.org/project/rerun-sdk · crates.io/api/v1/crates/rerun · rerun.io/docs/getting-started/install-rerun/cpp |
| **权重** | 不适用——Rerun 不是模型，不分发任何权重。 | （结构性事实） |
| **数据** | Rerun 不自带数据集；LeRobot 数据集目录、MCAP、.parquet 等是**可导入格式**（viewer 原生打开列表），非随包分发物。 | rerun.io/docs/getting-started/data-in/open-any-file |
| **商业层** | Rerun Hub：managed catalog / GPU streaming / SSO，"Hub is priced per-deployment based on team size and data scale."（contact-us 报价）；"The APIs are the same either way; Hub changes what runs underneath, not what you do."。公开渠道无 Hub 条款全文。 | rerun.io/pricing |

**UNVERIFIED**：Rerun Hub 的法律条款文本（无公开文档，仅 pricing 页要点）；`rerun_cpp_sdk.zip` 是否附带与仓库一致的 LICENSE 文件（未下载核对，基于定价页声明与仓库构建流程推断）。

## 4. 硬件要求（显存/驱动/OS，来源+日期）

- **渲染后端**：viewer 基于 Wgpu——"The default backend is vulkan everywhere except on Mac where we use metal."；可用 `--renderer=<backend>`（vulkan/metal/gl；web 端 webgl/webgpu）或 `WGPU_BACKEND` 强制；"Make sure to keep your graphics drivers updated."。（rerun.io/docs/getting-started/install-rerun/troubleshooting，2026-11-05）
- **Web**："On the web we prefer WebGPU and fall back automatically to WebGL if no support for WebGPU was detected."（同上，2026-11-05）
- **显存下限**：**官方未公布任何最低显存/硬件规格数字**（troubleshooting 页无 minimum specs 一节）——不是"没有要求"，是"未发布"。
- **OS**：Mac/Windows 开箱即用；Linux 需先装 dev 库（libclang、GTK3、xcb、xkbcommon 等）；WSL2 需经 kisak-mesa PPA 更新 Mesa Vulkan 驱动，且 "the Mesa driver on WSL dispatches to the Windows host graphics driver"。Wayland 下偶发崩溃（workaround：unset `WAYLAND_DISPLAY`）。（同上，2026-11-05）
- **体量参考**：Python wheel 151.6–174 MB / 平台；C++ SDK 走 GitHub release zip。（pypi.org/project/rerun-sdk · rerun.io/docs/getting-started/install-rerun/cpp，2026-11-05）

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

- **Logging API**：`rr.log(entity_path, archetype)` + `rr.set_time("frame", sequence=…)`；批量路径 `send_columns`（"full control over the logged timelines"）；`__` 前缀实体路径保留给 SDK。（rerun.io/docs/concepts/entity-path · rerun.io/docs/concepts/timelines）
- **SDK operating modes（5 + sinks）**：`spawn` / `connect_grpc` / `serve_grpc` / `save` / `stdout`，`set_sinks` 可并发多路（如写盘同时推 viewer）；无模式时数据在内存 buffer 等待。（rerun.io/docs/reference/sdk/operating-modes）
- **gRPC 控制面（3 服务）**：`MessageProxyService`（"carries logged data between SDKs and Viewers"）、`ViewerControlService`（远程控制 live viewer，"is what the MCP server uses"）、`RerunCloudService`（catalog + 查询，`rerun.catalog.CatalogClient` 所用）。默认端口：viewer/proxy 127.0.0.1:9876，`rerun server` catalog 127.0.0.1:51234。proto 在仓库 `crates/store/re_protos/proto`；全服务器带 gRPC reflection。（rerun.io/docs/reference/grpc）
- **查询/导出（数据出）**：dataframes API + SQL（README "queryable with dataframes or SQL"）；文档域 `/docs/getting-started/data-out` 与 `/docs/howto/query-and-transform/get-data-out`。（github.com/rerun-io/rerun README · rerun.io/docs）
- **资产/录制格式**：**.rrd**（录制）与 **.rbl**（blueprint），"Both refer to the exact same on-disk format"。rrd 结构：12 字节 StreamHeader（FourCC `RRF2` + version + encoding options）→ 16 字节帧头 + protobuf 载荷的线性消息流（End / SetStoreInfo / ArrowMsg / BlueprintActivationCommand）→ 可选 footer（Arrow RecordBatch 清单，含每 chunk 字节偏移/长度/统计/schema 哈希 → **随机访问**；EOF "FOOT" 尾标 + xxh32 CRC）。数据本体："Every ArrowMsg carries a single chunk — an Apache Arrow RecordBatch with Rerun-specific schema metadata."；LZ4 仅按消息、仅在节省 ≥10% 时启用。（rerun.io/docs/concepts/logging-and-ingestion/rrd-format）
- **导入格式（viewer 原生打开）**：.rrd/.rbl、.mcap、.urdf、网格（dae/gltf/glb/obj/ply/stl）、图像（png/exr/jpeg/webp/…）、.rvl 深度、.mp4 视频、音频（wav/mp3/flac/…）、.ply 点云、.parquet、LeRobot 数据集目录；可经 importer 扩展新类型"without needing to modify the Rerun codebase itself"。（rerun.io/docs/getting-started/data-in/open-any-file）
- **Web/JS**：npm `@rerun-io/web-viewer` 与 `@rerun-io/web-viewer-react`（WASM bundle 随包自托管可行；iframe 路线走 `app.rerun.io`）；实验性 API、SDK 版本严格匹配。（rerun.io/docs/howto/integrations/embed-web）
- **MCP server**：viewer reference 面板列表含 MCP server，经 `ViewerControlService` 控制（细节未展开核验，见 §7）。（rerun.io/docs/reference/viewer/overview · rerun.io/docs/reference/grpc）

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：竞品参照（External reference）——买 vs 造查看器的"买"侧样板。** 理由：

1. **数据模型同构是研究价值所在**：Rerun 的 entity path（身份）× timeline（时间）× static/数据分层，与 World Contract 的 identity / time / ownership / confidence 四轴正交可比——它验证了"身份+时间双轴 + 静态数据 shadow 时序数据"这类设计在多模态高吞吐下成立。这属于**读它的文档就够了**的参照，不需要引入代码。
2. **自研静态查看器路线不可被它替代**：`docs/design/ui-viewer/查看器设计.md` 的目标是"产出可见性"，交付物是数据**内嵌**的静态 HTML（浏览器直开、无服务器），且明确排除实时推送、GPU 渲染、Vulkan 后端。Rerun 恰好要装 150–174 MB 的 SDK、要 GPU 驱动环境（Wgpu/Vulkan/Metal）、有 web 版本严格匹配与 experimental API——三项都与我们"零外部依赖（ADR-0012 的构建级证明）、位级可复现工件"的纪律相反。rrd 是二进制 Arrow 容器，**进不了 `benchmarks/regression/` 的字节级回归证据链**；我们的 JSON 工件才是一等公民。
3. **不是 ②适配器候选**（现阶段）：当前阶段没有图像/点云级回放需求，L2 时间线工件（每 tick 实体快照 + TickLog + 指标）用静态 HTML 已覆盖；为"看"引入一个重型原生应用违背最小依赖。若闭环路线（`docs/17-research/2026-10-27-interactive-closed-loop-route.md`）落地、出现真实帧序列/点云的富可视化需求，导出侧 adapter（timeline 工件 → `rr.log`）才值得 spike——见 §7 触发条件。
4. **不是评测对象**：Rerun 不生成世界、不评世界模型，它是记录/可视化基础设施——与评测轴正交。
5. **与族谱既有条目的衔接**：Rerun 原生导入 LeRobot 数据集目录与 MCAP——map 中 LeRobot 行（"Future app/reference"）的产物若未来需要可视化，Rerun 是现成通道而非新造轮子。

**Map 建议行**（六列，Boundary 取既有词汇 `External reference`）见提交结果 `proposedMapRow`。

## 7. 未解决问题 + 重查触发条件

| 未解决问题 | 重查触发条件 |
| --- | --- |
| C++ SDK 的编译器/平台矩阵未核验（install 页未列，指向 ref.rerun.io 的 Build & Distribution，本次未抓取） | 真要写 C++ 导出 adapter 前 |
| 显存/长时序（数万 tick、真实帧序列）回放性能无官方数字也无实测 | 阶段 D 真实数据入环、评估富可视化需求时 |
| timeline 工件 → .rrd 的转换成本（Arrow RecordBatch 映射 + Sorbet schema） | 闭环路线落地且"图像/点云级回放"成为真实需求时（先 spike 后 ADR） |
| Rerun Hub 条款全文（仅 pricing 页要点，contact-us 报价） | 若考虑团队/多机部署（当前单人仓库不适用） |
| gRPC catalog API 处于 `v1alpha1`、无稳定性承诺 | 0.4x → 0.5x 大版本升级时复查 breaking 情况 |
| MCP server 能力范围（仅确认存在与所用服务，未逐项核验工具清单） | 若评估"agent 驱动查看器"方向 |

## 8. 来源清单

检索日期均为 **2026-11-05**。

| # | 来源（域名+路径） | 用途 |
| --- | --- | --- |
| 1 | https://github.com/rerun-io/rerun | README：定位、SDK 语言、open-core、能力声称 |
| 2 | https://raw.githubusercontent.com/rerun-io/rerun/main/README.md | README 许可/Hub 句子原文核对 |
| 3 | https://api.github.com/repos/rerun-io/rerun | 仓库元数据（创建日期、stars、活跃度、spdx_id） |
| 4 | https://rerun.io/docs/concepts/entity-path | 数据模型身份轴 |
| 5 | https://rerun.io/docs/concepts/timelines | 数据模型时间轴 |
| 6 | https://rerun.io/docs/concepts/logging-and-ingestion/rrd-format | rrd 格式全节 |
| 7 | https://rerun.io/docs/concepts/logging-and-ingestion/recordings | 录制/逻辑录制/recording ID |
| 8 | https://rerun.io/docs/getting-started/data-in/open-any-file | 本地打开（离线回放依据）、导入格式 |
| 9 | https://rerun.io/docs/getting-started/install-rerun/troubleshooting | GPU/驱动/OS 要求 |
| 10 | https://rerun.io/docs/getting-started/install-rerun/cpp | C++ SDK 分发（FetchContent zip） |
| 11 | https://rerun.io/docs/reference/sdk/operating-modes | 5 种 operating modes、save→rrd、版本兼容警告 |
| 12 | https://rerun.io/docs/reference/grpc | gRPC 三服务、端口、proto 位置、reflection |
| 13 | https://rerun.io/docs/howto/integrations/embed-web | web viewer npm/iframe、实验性、版本匹配 |
| 14 | https://rerun.io/pricing | Apache-2.0/MIT SDK 声明、Rerun Hub 商业分层 |
| 15 | https://pypi.org/project/rerun-sdk/ 与 https://pypi.org/pypi/rerun-sdk/json | Python 包版本/平台/元数据（license 字段 null） |
| 16 | https://crates.io/api/v1/crates/rerun | Rust crate 版本与 "MIT OR Apache-2.0" |
| 17 | https://rerun.io/docs | 文档索引（结构、MCP server、extend-ui Rust only） |

仓库内引用材料（去重纪律：只链不重述）：

- `docs/17-research/third-party-research-map.md` — 六列表结构与既有 Boundary 词汇（`External reference` 等）。
- `docs/17-research/research-queue.md` — 阶段框架（ADR-0010）与闭环路线登记。
- `docs/design/ui-viewer/查看器设计.md` — 自研静态查看器路线（时间线工件 + 内嵌静态 HTML；明确排除实时推送/GPU 渲染/Vulkan）。
- `docs/17-research/2026-10-27-interactive-closed-loop-route.md` — 闭环路线（§6.3 与 §7 触发条件所指）。

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-rerun.md](deep/2026-11-05-rerun.md)
