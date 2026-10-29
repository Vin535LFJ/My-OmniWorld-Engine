# Spec — 阶段 D「真实数据入环」

Status: ready-for-agent（D1 可领）

> 来源：L1 已冻结（[`docs/design/stage-d-real-data/概要设计.md`](../../docs/design/stage-d-real-data/概要设计.md)，Round 1 四问 + Round 2 四问 + 结构确认两项，2026-10-19）+ PLAN §六 Phase 3 行 + L0 §2 契约⑦⑧ + [ADR-0009](../../docs/adr/0009-reproducibility-dual-mode.md) + [ADR-0012](../../docs/adr/0012-onnxruntime-optional-dependency.md) + [ADR-0013](../../docs/adr/0013-scoring-space.md)。工具无关：任何 AI 按本 spec + L1 + L0 可领任务。

## Problem Statement

引擎至今只吃自己画出来的世界：观察是内存里光栅化的彩色方块，真值是种子确定性运动学。这验证了契约，但有两件事证明不了——① **"最少拷贝、同步、延迟"这个最初的技术论点**（ADR-0008 把定位复位为 GPU 原生世界引擎）在合成世界里无从谈起，因为没有真实的解码→推理数据流；② **判据 (c) 关不掉**：阶段 A/B/C 三轮诊断收敛到"真值与模型定义域不相交"（problem-log 006/007/012），COCO 检测器在彩色方块场景里没有可对应的对象。

## Solution

按 L1 §5 六片推进：**D1 环境与依赖核验 → D2 帧序列供给通路 → D3 标注装载为真值 → D4 观察空间评分 + 判据 (c) 重测 → D5 GPU 数据流与零拷贝（主线）→ D6 阶段收口**。

**结构性决定（L1 §2 条 9，owner 已确认）**：D2→D3→D4 全程可在 CPU 上完成，判据 (c) 的关闭尝试**不再等 CUDA 环境**；只有主线 D5 依赖环境。副作用是阶段 D 可能出现"判据 (b) 已达、主线 (c) 未完"的中间态——这是有意的。

## User Stories

1. As the engine owner, I want real frames to become Observations through the same contract, so that the engine demonstrably serves real data, not only synthetic worlds.
2. As the engine owner, I want real annotations to become truth via TruthWorld (single writer preserved), so that belief can be scored against something the model's own domain contains.
3. As the engine owner, I want position correctness measurable in the observation's own normalized space, so that video without camera calibration can still be scored (ADR-0013).
4. As the engine owner, I want the frozen stage A/B/C baselines to stay byte-identical, so that adding a second score space never invalidates the oracle.
5. As the engine owner, I want per-stage transfer counters (copies, syncs, bytes across the device boundary), so that "least copies/synchronization/latency" becomes a measurable claim instead of a slogan.
6. As the engine owner, I want decode → inference to stay resident on the GPU in performance mode, so that the spine claim (zero-copy) is actually tested.
7. As the engine owner, I want the reference build to remain zero-external-dependency, so that clone-and-verify still works without FFmpeg, NVDEC, or ONNX Runtime.
8. As the engine owner, I want every new dependency's license verified and recorded before it enters the product path, so that the AGPL/GPL red line and the public-portfolio gate both hold.
9. As a portfolio viewer, I want a video-pipeline engineering note with environment metadata and measured breakdown, so that GPU claims are evidence-backed.
10. As the engine owner, I want criterion (c) finally closed or honestly reported with the reason, so that a three-stage-old debt gets its definitive answer.

## Implementation Decisions

- **真值权威不变**：`TruthWorld` 新增 `load_annotations(...)`；单写者与 `friend` 机制原样保留；不抽真值接口（那会动契约④⑤）。
- **评分空间加法式**：`enum class ScoreSpace { WorldXZ, ObservationUV }` 进 `core/types.hpp`，默认 `WorldXZ`；`matched_error` 按空间分派；容差语义不变。
- **解码归性能模式**：参考模式只吃已解码 raw 帧序列（字节固定 → 逐位可复现）；解码器输出不进入任何哈希。
- **新依赖默认 OFF**：FFmpeg / NVIDIA Video Codec SDK 与 `OW_WITH_ONNXRUNTIME` 同构（可选构建 + 构建级证明）。FFmpeg 取 LGPL 构建、不启用 `--enable-gpl`；许可条款由 D1 逐条核验后记录实际构建参数。
- **数据集不入库**：只入库获取脚本 + SHA256 + 逐条许可记录（照 `tools/fetch_models.mjs`）。"clone 即跑"对本阶段部分失效，如实标注。
- **出口判据形式冻结、数值待实测**：跨设备拷贝次数/字节数上界 + 相对 CPU 基线下降百分比 + 每帧 p95 延迟预算；数值由 D1/D5 实测后写入 regression 报告。判据 (b) 的 0.30 阈值**不动**。

## Testing Decisions

- 行为 + 确定性断言成对（problem-log 002）；模型/数据侧断言只断边界，不断"零"（008）。
- **回归门**：阶段 A/B/C 的冻结基线（600/42 哈希 + `slice11/12/16` 字面量）必须在新增观察空间评分后**逐位不变**；变红按回归缺陷处理，不更新基线。
- 每一种构建配置都跑全量 ctest（AGENTS 硬规则 #4；阶段 C 的 ON 构建教训）。
- 真实数据的确定性证据：同一帧序列 + 同一标注 + 同 seed → 观察/账本/指标三处哈希逐位一致。
- 判据 (c) 报告需按目标尺度分层（uv 容差对大小目标难度不同），避免重演"聚合掩盖结构"（005）。

## Out of Scope（本阶段不做）

GPU 渲染器 / Vulkan 观察后端、NVENC 编码路径、多相机与位姿估计、3D 反投影与标定求解、跨场景绝对定位主张、训练世界模型、把数据集入库。
