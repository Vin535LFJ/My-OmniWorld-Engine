# CURRENT — 本周唯一焦点

> 只回答一个问题：**现在做什么。** 背景与全貌见 [`PLAN.md`](PLAN.md)。

> ⚠️ **2026-10-28 审计更正（读完下面第一段之前先看这条）**——下面 2026-10-27 的状态陈述有三处已被后续核验推翻或降级，**原文保留不删**，以本条为准：
> 1. **D4 的"判据 (c) 已关闭"降级为"待重验"**：真实面板的图像转换把位图上下翻转、标注未同步变换（已复现：首帧与正确解码有 917,082/921,600 字节不同）。`0.305180` 是错误管线的真实输出，不再构成"图像与标注对应正确"的证据。
> 2. **"驱动 560.94 = 天花板、需要 ≥570.x" 不是已成立结论**：CUDA 官方发布说明给的是 12.x 族驱动 **≥525 且 <580**（minor version compatibility 允许同族更旧驱动，带功能限制），ONNX Runtime 文档**没有任何驱动最低版本条目**；真正缺失的模块/符号仍未定位。
> 3. **`p50 205 ms` 不是 CPU EP 回落**：`src/perception/rtdetr.cpp` 在 CUDA EP 初始化失败时直接抛错，**没有 CPU 回退**；该数字是故障路径耗时，任何口径下都不得引用。
>
> 其余降级项（判据主口径、世界模型评分时刻）与底座取舍的证据留在审计记录中（未入库）。

> **2026-11-14 方向裁决（[ADR-0016](docs/adr/0016-direction-v2-world-state-runtime.md) / [ADR-0017](docs/adr/0017-worldfoundry-differential.md)，owner grill 三轮终裁）**：定位 v2（对外=可测量的世界状态运行时，工程属性=GPU 数据流纪律不变）；首战 B'=程序化真值世界工厂×生成式模型闭环考场（**阶段 E**，五条验收判据含录制-回放位级锚定）；智驾①+③组合、三条红线条件解封、本机 spike 硬门排 E 首份报告后；动画侧项目现状确认；WF 双轨+探针止损（1 issue+至多 1 小 PR）。**阶段 D 焦点不变**；B' 前置研究（8GB 标的核验 ForgeWM 优先 / 生成器契约 L1 共创）走 Intelligence 轨与 D5 并行，**代码切片 D6 收口后开工**。

**阶段 D 进行中：D1（环境与依赖核验）、D2（帧序列供给通路）、D3（标注装载为真值）、D4（观察空间评分 + 判据重测）已完成（2026-10-27）。判据 (c) 已在真实面板上关闭：观察空间 + 纯感知配置下分离量 `0.305180 ≥ 0.30`（阈值未动）——⚠️ 但判别力在目标尺度上不均匀（大目标层塌陷到 0.220196）。当前 = D5「GPU 数据流与零拷贝」：L2 已由 owner 批准（2026-10-27）→ `implementing`；Gate 0 实测中——G0-3 ✅、G0-2 能力面 ✅（NVDEC 真解出 RGB8，且与 CPU 解码逐字节不同）但许可面发现 `--enable-version3` 缺口、G0-1 被实测推翻：装完 cuDNN 9 仍打不开 CUDA EP，真正的天花板是驱动 560.94 = CUDA 12.6，需要 ≥570.x 驱动 + CUDA 12.8 运行时。**

## 当前状态

- **阶段 A 完成**（6 切片 + Gate-1 对齐报告：[`benchmarks/regression/2026-10-14-stage-a-gate1-alignment.md`](benchmarks/regression/2026-10-14-stage-a-gate1-alignment.md)，判据 (c) PARTIAL 如实标注）。
- **阶段 B 完成**（5 切片 + W02 收口：双模式 CLI / 容差比较器自证 / per-producer ECE / 有界场景；F1/F2 兑现）。
- **阶段 C 完成（5/5，2026-10-19）**：L1 冻结 → C1 依赖隔离（ORT 可选、默认 OFF = 构建级证明，[ADR-0012](docs/adr/0012-onnxruntime-optional-dependency.md)）→ C2 多分辨率 + Camera 合并 → C3 [RT-DETRv2-S](benchmarks/regression/2026-10-19-stage-c-c3-rtdetrv2s.md)（真实照片 5 框 max 0.950，类别/位置/尺度全对上）→ C4 [DA-V2-Small](benchmarks/regression/2026-10-19-stage-c-c4-depth-anything.md)（稠密深度图，核心零新增行）→ C5 [阶段收口](benchmarks/regression/2026-10-19-stage-c-closeout.md)。
- **两条 PARTIAL 的准确表述**：① 性能模式延迟预算——CPU EP 侧实测齐备（RT-DETR 518 ms / DA-V2 1252 ms，Release 单线程），CUDA EP 侧缺 cuDNN 9 / CUDA 12.8+ 未测；② 判据 (c)——真模型 vs 注入者分离量 0.232 仍 < 0.30，且同一真模型 ECE 跨面板漂 0.24–0.49，**根因是模型定义域与本世界真值不相交**，不是实现进度（problem-log 012）。
- 代码现状（阶段 C 收口时）：ctest **15/15** 绿（默认 OFF 构建，参考模式零外部依赖）；**ON 构建 17/17** 绿（含 slice13/14/16）；600/42 参考哈希 `574361e62eec92b9` / `459ff85132cc103e`（引入 ORT 与两个真模型后逐位不变）；P0 面板 ECE 已作为字面量冻结进 `slice16`。
- CLI 语义（C5 经 owner 批准）：setup 期异常 → 具名错误 + exit 1（原为静默 fastfail）；性能模式有适配器故障 → exit 1（工件仍写出，`first_fault` 带原因）；参考模式语义不变。
- **阶段 D L1 已冻结（2026-10-19）**：主线 = GPU 数据流（零拷贝）；真实帧序列作为观察源、真实标注经 `TruthWorld::load_annotations` 成为真值（单写者与契约④⑤不动）；评分空间新增观察归一化坐标（**加法式**，默认仍世界 x/z）→ 决策记录 [ADR-0013](docs/adr/0013-scoring-space.md)。
- **D1 完成（2026-10-26）**：环境与许可核验 → [`docs/17-research/2026-10-26-stage-d-environment-and-licenses.md`](docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)。结论：CUDA 12.6 可用、**cuDNN 完全缺失**（挡路的是它）；H2D 实测 **12.81 GB/s**（1080p 单程 ≈0.49 ms → 零拷贝收益在**减少往返与同步**，不在带宽）；FFmpeg 走 LGPL 可行（禁 `--enable-gpl`、必须动态链接）；NVIDIA SDK 只允许目标码集成分发、**不 vendor 进仓库**；**ADR-0013 解掉了数据集死结**（不再需要标定）→ COCO 标注 **CC BY 4.0** 已核验，D2/D4 用它。
- **D2 完成（2026-10-26）**：真实帧第一次作为观察源入环。新接缝 `src/observation/frame_source.{hpp,cpp}`（行式清单 + raw RGB8），CLI `--frames`。**端到端实测**：猫照 640×480 当 3 帧序列喂 RT-DETR → `ticks=3`（流先结束）、`faults=0`、`entries=39`（15 检测 + 24 世界模型）、阶段键出现 `frame_fetch`（p50 574 µs/帧）且 `render` 消失、`detect p50 487 ms`。**刻意局限：真实观察 + 合成真值不对应，该面板的 divergence/ECE 无语义，直到 D3。**
- **D3 已落地（2026-10-26；票据 `verifying`，`done` 待 owner 确认）**：真实标注成为逐 tick 真值，**单写者与契约④⑤零改动**。新模块 `src/truth_world/annotation.{hpp,cpp}`（行式清单 + 冻结装载口径）+ `TruthWorld::load_annotations(ws, set, tick)` + `WorldState` 一条 friend-only 场景替换原语；CLI `--annotations`（必须与 `--frames` 同传）。取数不拉整包：HTTP Range 读中央目录，标注 19 987 840 B / 图像 200 张 33.5 MB（**数据不入库**，入库的是 `tools/fetch_coco_subset.mjs` + 固定子集清单 + 每图 SHA256 + 逐图许可）。**端到端实测**（Release ON，CPU EP，200 帧）：`entries=2229`（applied 1527 / announced 702 / rejected 0 / faults 0）、`divergence=0.010747`、`ece:rtdetr_v2_s=0.198829`、口径计数 `dropped_crowd=10`（与面板逐项吻合）；两次跑哈希逐位一致，面板重建后字节指纹一致，OFF/ON 两构建 `world_hash` 相同（真值与感知参与者无关）。**许可更正**：COCO 图像逐张 Flickr 许可，val2017 的 **69.5% 是非商业**——面板准入收紧为 BY / 无已知限制 / 政府作品（[D1 note §6.1](docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)）。证据：[D3 regression 报告](benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md)。
- **D4 已落地（2026-10-27；票据 `verifying`，`done` 待 owner 确认）**：**判据 (c) 关闭**。`ScoreSpace { WorldXZ, ObservationUV }` + `ScoreContext` 进 `core/types.hpp`（`ViewCamera` 从 `observation/` 纯搬移进来，避免新增 `metrics → observation` 边）；`matched_error` 按空间分派，观察空间**只经 `ViewCamera`** 取 uv，视野外不评分（R3，钳制会伪造"正确"），容差仍是同一个 `kCorrectTol`（R4），**裁决与账本不动**（R5）。CLI `--score-space world|observation`；工件 `config.score_space` 自述。**实测（M1–M7 七个工件）**：主判据 M1（200 帧 + 观察空间 + `--models ""` 纯感知）`ece:rtdetr_v2_s=0.19981957184206142` vs `ece:blob_detect_overconfident=0.5050000095367432` → **separation 0.305180 ≥ 0.30 → 达标**；世界空间对照 0.301171（两空间差 4.0e-3，在像素量化上界 5.2e-3 之内，每个生产者恰 1 条主张跨过容差边界）；**⚠️ 大目标层塌陷到 0.220196 < 0.30**（机制：绝对容差 vs 目标尺度）→ 如实登记"判别力在目标尺度上不均匀"，修复方向未定。**口径同测**：`coverage 0.33553719008264465`、`divergence_n 406`（与 `(308+98)/(1010+200)` 自洽）；M3（+`zero_delta`）证明逐生产者指标**逐位不变**而聚合 `divergence` 塌陷 0.052053→0.013005、`coverage` 虚高 0.3355→0.6690（problem-log 015 的神谕稀释病态复现）。证据：[D4 regression 报告](benchmarks/regression/2026-10-27-stage-d-d4-observation-space-scoring.md)。
- **本面板仍不能主张的三件事**：① 评分是**纯位置**的（不看类别，不是 mAP）；② 帧间无实体同一性（公开 COCO 实例标注无 track id）→ `identity_stability` 在此**无语义**，实测 0 是构造性质不是缺陷；③ 诚实侧 ECE 天然不为 0（0.1998）——"正确"只判位置，分离量不是"完美模型 vs 坏模型"。
- 代码现状：默认 OFF **18/18**、ON **20/20**（新增 ow_slice19）；600/42 基线哈希与 `slice11/12/16` 字面量逐位不变，且不装载标注时工件**不长出 `annotation_truth` 节**（键发射序列 diff 为证）；D3 已发布的全部数字在 D4 二进制上**逐位复现**（同配置重跑对照）。
- 旧基线（重建对照）：Gate 1 三判据 1106.7 Hz / 适配器零核心改动 / ECE 分离 0.9617。

## 下一件事 = D5 实现（Gate 0 三件套实测中）

- **票据**：[`.scratch/stage-d-real-data/issues/05-gpu-dataflow-zero-copy.md`](.scratch/stage-d-real-data/issues/05-gpu-dataflow-zero-copy.md)（`implementing`）· **L2**：[`docs/design/stage-d-real-data/slices/05-gpu-dataflow-zero-copy.md`](docs/design/stage-d-real-data/slices/05-gpu-dataflow-zero-copy.md)（v1，**owner 2026-10-27 批准 → `implementing`**）
- **批准时一并采纳的三条决议**：① 判据 (c) 拆成 **B0（延迟基线）/ B1（拷贝基线）**，**已回写 [L1 §8](docs/design/stage-d-real-data/概要设计.md)**（L1 升 v1.1，阈值 50% 不变）；② **Video Codec SDK 不由本仓库链接**——已实测坐实（FFmpeg 的 `h264_cuvid` 自带硬解，我们只依赖 FFmpeg 一个可选依赖）；③ 不落地 `TransferCounters` 空壳，本片按整体落地。
- **Gate 0 实测进度（2026-10-27；逐项证据在票据里）**：
  - **G0-3 ✅** Sintel trailer 480p（H.264 High · 854×480 · yuv420p · 24 fps · 1253 帧，**CC BY 3.0**，不入库）→ 重建 `node tools/fetch_video_sample.mjs`（入库的是清单 + SHA256 + 许可）。
  - **G0-2 能力面 ✅ / 许可面 ⚠️**：BtbN `win64-lgpl` 的 `h264_cuvid` **确实走硬件**（日志 `h264 (h264_cuvid) -> rawvideo`），解出与 `--frames` 面板同构的 RGB8。两条发现：**`--enable-version3` 不在 D1 §4 的三条红线里**（该构建含 LGPLv3+ 组件；不是 GPL 传染，但验收口径必须补这一条）；**CPU 解码与 NVDEC 解码逐字节不同**（54.21% 的字节不同、单通道最大差 4、仅 13/100 帧一致）→ **R2"解码器输出不入哈希"从推测升级为实测**。
  - **G0-1 ⚠️ 前提被实测推翻（原判断"只差 cuDNN"是错的）**：cuDNN 9.6.0.74 已取回并**与 PyPI 发布的 sha256 逐位吻合**、`cudnn64_9.dll` 等 8 个库已就位、`dumpbin /dependents` 证明 cuDNN 根本不在 CUDA provider 的静态导入表里（运行期动态装载）、四个 VC++ 运行库全在 System32——**但 `--ep cuda` 仍然不可用**。故障形态从 D1 §2 的 Error 127（缺过程）变成"模块找不到"，装载前进了一步；真正的天花板是 **`nvidia-smi` 与 `runtime_probe` 同报 Driver 560.94 / CUDA 12.6**，而 ORT 1.31 要求 **CUDA 12.8+**。**D1 §2 路径 A 的原文是"12.8+ 运行时 + cuDNN 9"两件一套**，此前被读成了"缺 cuDNN"。→ **G0-1 升级为：≥570.x 驱动 + CUDA 12.8 运行时（cuDNN 已完成）。** 具体卡在哪个模块**尚未钉死**，如实登记在票据。
  - ⚠️ **不得引用的数字**：`--ep cuda` 那次跑出的 `detect:rtdetr_v2_s p50 205 ms` 是 **CPU EP 回落**的结果，与 C3 的 518 ms 也不是同一口径（线程/构建配置不同）。
- ⚠️ **命名冲突要防**：本仓库有两个 "(c)"——Gate-1 的判据 (c)（ECE 分离量，D4 已关闭）与 [L1 §8](docs/design/stage-d-real-data/概要设计.md) 的 (c) 行（GPU 数据流）。**D5 报告必须写全名**，不得只写"判据 (c) 达标"。
- **D4 留给 D5 的前置观察项**：① 大目标层分离量塌陷到 0.220196 的**修复方向未定**（自适应容差要动 `kCorrectTol` 语义 → 需新 ADR）；② 面板整幅拉伸的畸变（median 1.000 / 最差 2.243）未做分层，是模型侧混杂项；③ `identity_stability` 在无 track id 的面板上无语义，**不得**作为任何主张的佐证。
- **三条硬约束**：① 加法式，`slice11/12/16` 字面量与 600/42 哈希逐位不变；② 数据不入库（面板重建：`node tools/fetch_coco_subset.mjs && node tools/coco_to_panel.mjs`；样本重建：`node tools/fetch_video_sample.mjs`）；③ Gate 0 未齐之前**只接线、不出数字**（D1 §8）。
- 切片链：D1 ✅ → D2 ✅ → D3 ✅ → D4 ✅ → **D5 GPU 数据流与零拷贝（`implementing`，Gate 0 实测中）** → D6 收口。
- **另有一份待批 L2 不属本链**：[`docs/design/ui-viewer/查看器设计.md`](docs/design/ui-viewer/查看器设计.md)（另一 session 于 2026-10-21 写的逐 tick 时间线 + 查看器页，状态"待 owner 批准"）。它与 D1–D6 无冲突，**未获批准前不动**。
- **另有一个已立项的独立项目，不属本引擎路线**：[角色动画路线（2026-10-27）](docs/17-research/2026-10-27-character-animation-route.md)——owner 同日批准立项，只登记在研究队列。**不占阶段 D 的资源，也不要把它任何组件引进产品路径**；侧项目定仓库后该文件整体迁出。
- **另有一项已裁决的方向研究（不属本链）**：[智驾视觉方案调研（2026-11-08）](docs/17-research/2026-11-08-autonomous-driving-visual-survey.md)——**2026-11-14 已裁决（ADR-0016 D5；原在途"ADR-0015"编号作废，并入 0016）**：选项①（通用视频→重建→自由视角渲染能力，AD 视频为第一数据域、非产品方向）+③（离屏 Vulkan 技能座位）组合，②否决；三条红线**条件解封**（消费外部位姿/标定/点图产物；Vulkan 只进性能模式；自建标定求解器仍封）。里程碑（Argoverse2→VGGT-Ω→gsplat→自由视角渲染+PSNR/SSIM/延迟）排 **E 首份报告之后**，前置硬门=本机 8GB 实测 spike（Ω 官方表 25 帧 7.80GB=8GB 的 97.5%，旧 5.58GB 档作废）。**spike 与 L1/L2 批准前仍不动代码**（PLAN 规则 #6）。

## 出口判据（阶段 A / B 均已达成；阶段 C 两条 PARTIAL 见收口报告）

参考模式逐字节不变（全量 ctest 绿）+ 性能模式容差自证（噪声 stub：窗内过、超窗红）。阶段 D 的出口判据形式已冻结在 [L1 §8](docs/design/stage-d-real-data/概要设计.md)，数值待 D1/D5 实测。

## 不碰

**许可未核验通过的新依赖一律不进产品路径**（AGPL/GPL 红线；FFmpeg 取 LGPL 构建、不启用 `--enable-gpl`，D1 逐条核验）；TensorRT EP（预留枚举）、NVENC 编码路径、GPU 渲染器与 Vulkan 观察后端（**条件解封**：族谱③技能座位、只进性能模式，参考模式 CPU 光栅器仍是唯一位级 oracle，进产品路径前按证据回归，ADR-0016 D5）、训练模型、付费大算力、多相机与位姿估计 / 3D 反投影与标定求解（**条件解封**：仅消费外部重建生产者输出的位姿/标定/点图产物，经 Proposal 裁决；**自建多相机标定求解器仍封**，ADR-0016 D5）、数据集入库；sprint 外新想法进 `docs/17-research/research-queue.md`；**无批准的 L2 不写代码**（PLAN 规则 #6）。
