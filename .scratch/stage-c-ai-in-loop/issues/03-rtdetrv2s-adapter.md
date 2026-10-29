# 03: RT-DETRv2-S 适配器（C3）

**What to build:** 第一个真实 AI 在环——RT-DETRv2-S 以单文件 Perception Adapter 接入，内部双 provider（CPU EP 参考路径 / CUDA EP 性能路径，`--ep` 运行时选择）；检测框 → Proposal 五元组映射（target 用 NewEntity + transform + confidence 按置信度）；性能模式容差窗口声明（ADR-0009:16，原则 = 实测最大抖动 × 2）。

**Blocked by:** 01（ORT 依赖隔离）、02（多分辨率 Observation——模型输入 640×640）

**Status:** done — 2026-10-19（预授权执行；证据见 [C3 regression 报告](../../../benchmarks/regression/2026-10-19-stage-c-c3-rtdetrv2s.md) 与 [L2 偏差记录](../../../docs/design/stage-c-ai-in-loop/slices/03-rtdetrv2s-adapter.md)）

**流程：** 预授权执行（同 C1 授权链）。

- [x] 单文件适配器（`src/perception/rtdetr.cpp` 类）+ 编译期条件（ORT 存在才编）；核心零改动 → **零 per-model 代码成立；零核心行不成立**（3 处与模型无关的通用改动，L2 偏差 #1/#2/#6）
- [x] 参考路径（CPU EP）：逐字节复现保持；性能路径（CUDA EP）：容差窗口声明 + 延迟预算 → CPU EP 抖动实测 0（窗口退化为精确复现）；**CUDA EP 窗口本机无法标定，移交 C5**（本机缺 cuDNN 9）
- [x] 检测框 → Proposal 映射（类别标签、位置、置信度）经裁决入信念账本 → 位置 + 置信度入五元组；**类别标签按契约不进 Proposal**（`core/proposal.hpp:18` "adapters must not guess"，L2 偏差 #7）
- [x] git diff = 单文件 + build/文书（适配器不变式的 git 证据）→ 适配器单文件；另有 tests/CMake/.gitignore + 3 处通用核心改动 + `tools/png_to_rgb.mjs`（可复现性工具）
- [x] slice13 测试：参考路径确定性 + 性能路径容差 + 单文件不变式 → ON 构建全量 **15/15**、OFF 构建 **14/14**

## Comments

- 2026-10-17 · **实现 + 首轮实测**（Debug ON 构建）：CPU EP 推理跑通、确定性成对通过；预处理按 ImageNet 归一化写的第一版实测 logits 全负（[-9.57, -0.38]）→ 核对 `PekingU/rtdetr_r18vd` 的 `preprocessor_config.json` 后撤回，改回 ÷255 + bilinear(align_corners=false)（L2 偏差 #4）。
- 2026-10-19 · **收口重测（Release ON 构建 `build-ort-rel/`）**：真实照片 640×480 → **5 框，max conf 0.950**，模型自报类别 `remote/cat/cat/remote/sofa`（索引标签取自模型 `config.json` 的 `id2label`），与图像内容逐一对上——pre/post 链路语义正确性确立。
- 2026-10-19 · **L2 AC 证伪**：空白帧不是空列表，是 1 个整帧退化框（conf 0.330，自报 `tvmonitor`）；合成 8 块场景同理（0.518，`laptop`）。判据改为"有界 + 确定性"，不改场景迁就模型。→ problem-log 008。
- 2026-10-19 · **诊断通道补齐**：`--ep cuda` 曾只剩 `adapter_faults: 1`，原因全在 stderr。新增 `RunReport::first_fault` 进工件（B1 schema 附加字段）+ `quote()` 转义控制字符；复跑确认原因与修复指引已写入 JSON。→ problem-log 009。
- 2026-10-19 · **构建层两处修正**：① CMake 改为拷贝 `lib/*.dll`（缺 `onnxruntime_providers_shared.dll` 是 CUDA EP 失败的第一层原因）；② `ow_slice03` 的注册表计数断言按构建配置精确化（ON 下 3 项）——ON 构建首次跑全量套件才暴露。→ problem-log 010。
- 2026-10-19 · **延迟（Release，CPU EP 单线程，`observe()` 全程）**：5 ticks → p50 **518.23 ms** / p95 726.62 ms，逐 tick 均值 570.0 ms；p95 高企来自首 tick 的一次性会话创建（82 MB 模型加载）。**不是 CPU EP 最快配置**（单线程是确定性约束），不用于论证 GPU 收益。
- 2026-10-19 · **CUDA EP 状态**：接线完成、本机阻塞在依赖层（ORT 载入 `providers_cuda.dll` 报 Error 127；缺 cuDNN 9 / CUDA 12.8+）。owner 侧动作，装好后由 C5 标定窗口。
- 2026-10-19 · **可复现性**：真实照片为 gitignored 资产，解码工具入库为 `tools/png_to_rgb.mjs`（重跑输出与实测所用 `.rgb` SHA256 一致：`8050ada4…`）；未设 `OW_RTDETR_TEST_IMAGE` 时该检查打印 skipped 后正常通过。
- 2026-10-19 · 新坑：3 条（problem-log 008 / 009 / 010）。
