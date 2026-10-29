# 01: 环境与依赖核验（D1）

**What to build:** 阶段 D 的第一块地基：**把"能不能做"变成有凭据的结论**，不动任何引擎代码路径。三件事——① GPU 环境就绪路径（本机 CUDA EP 因缺 cuDNN 9 / CUDA 12.8+ 连载入都失败，C3/C4 两次实测）；② NVDEC 能力实测（驱动版本、解码会话数上限、支持的格式与色度采样——GeForce 消费卡在这些点上有限制）；③ FFmpeg 与 NVIDIA Video Codec SDK 的许可与分发条款逐条核验，并记录**实际采用的构建参数**。产出：research note + 构建级证明（新可选依赖默认 OFF）。

**Blocked by:** 无

**Status:** done — 2026-10-26（证据：[`docs/17-research/2026-10-26-stage-d-environment-and-licenses.md`](../../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)；两条 AC 按 L2 §7 偏差延后，见下）

**流程：** 先出 L2 → owner 批准 → 执行（阶段 D 不再沿用阶段 C 的预授权链，除非 owner 另行授权）。

- [x] research note 落 `docs/17-research/` → [`2026-10-26-stage-d-environment-and-licenses.md`](../../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)：硬件/传输实测（H2D 12.81 GB/s、D2H 13.12 GB/s）+ cuDNN 缺口的精确形态 + NVDEC 能核验/不能核验的边界 + 数据集分级表
- [x] FFmpeg 许可核验 → **LGPLv2.1+**；`--enable-gpl` 会令 GPL 传染（"GPL applies to all of FFmpeg"）；LGPL 要求动态链接 + 源码可得 + 声明。**构建参数写死：不加 `--enable-gpl`/`--enable-nonfree`、动态链接**
- [x] NVIDIA Video Codec SDK 分发条款 → 允许"以目标码集成分发"；非开源许可 → **SDK 本体不进仓库**，只入库构建脚本 + 版本 + 声明
- [ ] 构建级证明：新可选依赖默认 OFF → **延后到 D5**（现在没有依赖可关，空开关属 Speculative Generality；L2 §7 偏差 2）
- [ ] **CPU 基线冻结** → **延后到 D5 的前置测量**（现在没有解码→推理路径可测；本片只交付**传输带宽基线**，L2 §7 偏差 1）
- [x] 默认 OFF 构建全量 ctest 仍绿（15 项），且阶段 A/B/C 基线字面量逐位不变

## Comments

- 2026-10-26 · **结论五条**（详见 note §0）：① CUDA 12.6 本身可用，挡路的是 **cuDNN 完全缺失**（无头文件、无 `cudnn64_9.dll`）；② 传输带宽实测到手，1080p 单程 ≈0.49 ms → **零拷贝收益在"减少往返与同步"，不在带宽**；③ FFmpeg 走 LGPL 可行但有两条硬约束（禁 `--enable-gpl`、必须动态链接）；④ NVIDIA SDK 允许目标码集成分发、不得 vendor 进仓库；⑤ **ADR-0013 解掉了数据集死结**——不再需要相机标定，COCO 标注已核验为 CC BY 4.0。
- 2026-10-26 · **数据集决定**：D2/D4 用 **COCO val2017 子集（≤200 张）**——判据 (c) 是逐帧校准问题，不需要时序；许可清晰、体积可控。数据不入库，只入库获取脚本 + 署名。MOT/KITTI 留作"需要时序时再核"，且标注为**未核验**。
- 2026-10-26 · **D5 的前置被重新定义**：不是"CUDA 能用"，而是 **cuDNN 9 就绪 + Video Codec SDK 解压 + 一个可解码样本**三件套。在此之前 D5 只能接线、不能出数字。
- 2026-10-26 · 两条 AC 延后是我写票时的排序错误（基线要有路径才可测、开关要有依赖才有意义），已在 L2 §7 登记并更正，**没有悄悄勾掉**。
