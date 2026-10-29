# 02: 帧序列供给通路（D2）

**What to build:** 真实世界的帧第一次变成 `Observation`。参考模式只吃**已解码的 raw 帧序列**（字节固定 → 逐位可复现），解码器输出不进入任何哈希。新增 `FrameSource` 接缝（与 `PerceptionAdapter` 同族的自注册形态），`FileFrameSource` 是参考模式唯一允许的实现：读清单（宽/高/色彩布局/帧率/起始 tick）+ 逐帧喂 `Observation`，并如实填写 `sensor_kind`（`rgb_video`）与 `FrameMeta` 的 backend/device/format/dims。

**Blocked by:** 01（依赖核验；本片的参考模式实现其实不依赖 GPU，可在 D1 的许可结论出来后独立开工）

**Status:** done — 2026-10-26（L2：[`02-frame-source-supply-path.md`](../../../docs/design/stage-d-real-data/slices/02-frame-source-supply-path.md)）

**流程：** 先出 L2 → owner 批准 → TDD 实现。

- [x] `FrameSource` 接缝 → **有意不做自注册**（只有一个实现，注册表属 Speculative Generality）；改为工厂 `make_file_frame_source`，解码器实现出现时再抽（L2 §7 偏差 2）
- [x] `FileFrameSource`：行式清单（`width`/`height`/`rgb`）+ 每行一个 raw RGB8 帧；参考模式零新依赖
- [x] `Observation.sensor_kind = "rgb_video"`、`FrameMeta{backend:cpu, device:host, format:RGB8, dims}` 如实标注
- [x] 确定性证据：同一序列两次读取逐字节相等；同配置两次运行哈希逐位一致（见 Comments）
- [x] 现有两个真实模型适配器**不改一行**就能吃真实帧 → 实测：猫照 3 帧喂 RT-DETR，`detect p50 487 ms`、5 框/tick
- [x] slice17：读取行为 + 流结束收尾 + 确定性 + 具名错误（缺 width / 帧尺寸不符）
- [x] git diff = 新接缝文件 + **2 处通用核心改动**（`RunConfig.frames` + 循环内来源选择），已在 L2 §7 偏差 1 如实登记

## Comments

- 2026-10-26 · **实现**：`src/observation/frame_source.{hpp,cpp}`（清单解析、路径按清单目录解析、尺寸/字节数校验、`next()` 不写 tick）+ `RunConfig.frames` + `--frames` + 循环内来源选择。`ticks_run` 改为**实际完成数**（流先结束时如实反映，不再是 `cfg.ticks`）。
- 2026-10-26 · **一个测试抓到的细节**：EOF 探测曾被记成一次 `frame_fetch` 采样，导致样本数比帧数多 1。改为只在成功取帧时记录——否则"每帧耗时"的样本量会骗人。
- 2026-10-26 · **端到端实测**（Release `build-ort-c5/`，猫照 640×480 当 3 帧序列）：
  `--frames ... --ticks 10 --adapters rtdetr_v2_s` → `ticks=3`（流先结束）、`faults=0`、`entries=39`（15 检测 + 24 世界模型）、`applied=30 announced=9`；阶段键出现 `frame_fetch`（p50 **574 µs**/帧）且 **`render` 消失**；`detect:rtdetr_v2_s` p50 **487 ms**。
- 2026-10-26 · **本片的刻意局限（必读）**：真实观察 + **合成真值**，两者不对应 → 该面板的 `divergence`/`ece` **没有语义**，不能作为任何主张。直到 D3 把标注装载成真值。
- 2026-10-26 · **回归**：默认 OFF **16/16**、ON **18/18**（新增 ow_slice17）；600/42 基线哈希与 `slice11/12/16` 字面量逐位不变。
- 2026-10-26 · 新坑：0 条（清单解析与文件读取的单点错误都在测试里当场暴露，没有复发型教训）。
