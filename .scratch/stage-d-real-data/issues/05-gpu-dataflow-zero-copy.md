# 05: GPU 数据流与零拷贝（D5 · 主线）

**What to build:** 阶段 D 的主线，也是引擎最初的技术论点：**解码 → 推理 → 世界状态更新尽量全程显存驻留**，并且"最少拷贝、同步、延迟"变成有实测上界的判据，而不是一句口号。新增 `TransferCounters`（`host_to_device_bytes` / `device_to_host_bytes` / `copies` / `syncs`）并入现有 per-stage 采样载体（契约⑨），FFmpeg/NVDEC 解码只在**性能模式**注册，参考模式仍吃已解码帧序列。

**Blocked by:** 01（许可与环境结论 + **CPU 对照基线**）、02（帧供给通路）

**Status:** implementing — 2026-10-27（owner 已批准 L2；L2：[`05-gpu-dataflow-zero-copy.md`](../../../docs/design/stage-d-real-data/slices/05-gpu-dataflow-zero-copy.md)；报告：实现后产出）
推进态记录：`pending → design-review`（L2 已产出）→ **`design-review → implementing`**（owner 2026-10-27 批准；硬规则 1 要求这一跳由 owner 触发）。**第二道门（Gate 0 环境三件套）仍在推进中**（L2 §2：cuDNN 9 + CUDA 12.8+ / LGPL FFmpeg / 可解码样本）；未就绪前只做 L2 §9 第 1–7 项（不需要 GPU 的接线与测试），**不出任何性能数字**。环境未就绪时本片挂起，**不阻塞 D1–D4**（已全部落地）。

**流程：** 先出 L2 → owner 批准 → TDD 实现。

- [ ] 可选依赖接入：FFmpeg / Video Codec SDK 默认 OFF 的构建级证明（OFF 不 include、不链接、不感知）
- [ ] NVDEC 解码路径入性能模式；解码器输出**不进入任何哈希**（跨驱动/版本不保证一致）
- [ ] `TransferCounters` 并入逐阶段采样；**参考模式下恒为 0，并作为断言**（这是"零拷贝主张只在性能模式成立"的可检验表达）
- [ ] 出口判据 (c) 实测并冻结数值：跨设备拷贝次数与字节数上界 + 相对 CPU 基线下降百分比 + 每帧 p95 延迟预算（形式见 L1 §8，数值本片定）
- [ ] 每个 GPU 组件的容差窗口按原则声明（实测最大抖动 × 2，ADR-0009 / L1 Round 1 #4）
- [ ] slice20 测试：拷贝计数的行为断言 + 性能模式容差复现；不划算就如实报告（L1 §7 R-D6）
- [ ] 全量 ctest 在**每一种构建配置**下都跑（AGENTS 硬规则 #4）

## Comments

（执行时填写）

- 2026-10-27 · **环境复核（D1 结论仍然成立）**：`nvcc --version` → 12.6（可用）；`nvidia-smi` → 560.94 / RTX 4060 Laptop / 8 GiB（可用）；`cudnn*.h` 与 `cudnn64_9.dll` **两个都空**（挡 CUDA EP）；`which ffmpeg` **空**（挡 NVDEC）；无可解码样本。**Gate 0 未通过，本片实现挂起**。
- 2026-10-27 · **L2 已产出**：[`05-gpu-dataflow-zero-copy.md`](../../../docs/design/stage-d-real-data/slices/05-gpu-dataflow-zero-copy.md)。需要 owner 拍板的**三处**（其中两处是对上游文档的修正）：
  1. **判据 (c) 的口径修正**（L2 §7）：L1 §8 原文"拷贝次数与字节数……相对 CPU 基线下降 ≥ 50%"**字面上不可执行**——CPU 路径的设备拷贝恒为 0，从 0 不可能再降 50%。L2 拆成两个基线：**B0**（CPU 全路径，判延迟 ≥ 50% 下降）、**B1**（GPU 但有 host 往返，判拷贝/字节 ≥ 50% 下降），零拷贝路径必须同时赢过两者。**判据形式变了、阈值没变**；若 owner 认为这属于 L1 变更而非 L2 细化，需要回写 L1 §8。
  2. **Video Codec SDK 不由本仓库链接**（L2 §6，对票据措辞的偏差）：走 FFmpeg 的 nvdec 封装后，SDK 只在 FFmpeg 内部被用到 → 产品路径上**少一个专有依赖**，许可义务收敛为 D1 §4 的三条 FFmpeg 参数（禁 `--enable-gpl` / 禁 `--enable-nonfree` / 动态链接）。
  3. **不采用"先落地 `TransferCounters` 空壳"的分阶段方案**（L2 §11 条目 8）：没有第一个写入者的计数器是无消费者的结构（Speculative Generality），且"参考模式为 0"的断言在 GPU 路径存在之前是恒真的同义反复。**本片按整体落地，Gate 0 是唯一的门。**
- 2026-10-27 · **零拷贝的准确含义**（L2 §3）：**1080p 帧体一次都不跨设备**；必须跨的只有检测输出（`[1,300,4]` + `[1,300,80]` float32，KB 量级）的**一次** D2H。收益来自**减少往返次数**而不是带宽——D1 §1 实测 H2D 12.81 GB/s，1080p 单程约 0.49 ms。报告不得把它写成"带宽更快"。
- 2026-10-27 · **R3 是护栏不是证据**（L2 §5/§11 条目 4）："参考模式 `copies == 0`"在 D5 落地之前**恒真**（根本没有 GPU 路径），它防的是"GPU 路径悄悄进参考模式"；报告不得把它写成"已证明零拷贝"。
- 2026-10-27 · **owner 批准 L2，三条决议全采纳**（`design-review → implementing` 由 owner 触发）：
  1. **判据 (c) 的两基线修正获批**（L2 §7）：B0 判延迟、B1 判拷贝/字节，零拷贝路径 Z 必须同时赢过两者；**阈值 50% 不变**。已回写 [L1 §8](../../../docs/design/stage-d-real-data/概要设计.md)（L1 升 v1.1——§8 标题本即标注"数值形式待 owner 冻结时确认"，故属未冻结要素的确认，不另立 ADR）。
  2. **Video Codec SDK 不由本仓库链接**（L2 §11 条目 2）成立：产品路径只依赖 LGPL FFmpeg 一个可选依赖，许可义务收敛为 D1 §4 的三条 FFmpeg 参数。
  3. **不落地 `TransferCounters` 空壳**（L2 §11 条目 8）成立：本片按整体落地，Gate 0 是唯一的门。
- 2026-10-27 · **Gate 0 三件套全推进**（owner："全推进"）。逐项实测见下方「Gate 0 逐项实测」小节。

## Gate 0 逐项实测（2026-10-27）

### G0-3 可解码样本 —— ✅ 通过

| 项 | 值 |
|---|---|
| 文件 | `data/sample/sintel_trailer-480p.mp4`（**不入库**） |
| 来源 | `download.blender.org/durian/trailer/sintel_trailer-480p.mp4`（目录列表大小逐项吻合） |
| 大小 / SHA256 | 4 372 373 B / `b670602fa00934ca27c4351bb0efe7ea7a07fae57284e44226025eeed7c51254` |
| 流 | H.264 High **854×480 yuv420p 24 fps 1253 帧** + AAC LC，容器 mp4/isom，52.2 s |
| 许可 | **CC BY 3.0**（Blender Foundation / Durian Open Movie；**允许商用**，需署名） |
| 重建 | `node tools/fetch_video_sample.mjs`（清单 `tools/fetch_video_sample.manifest.json`；`--verify` 只校验哈希） |

### G0-2 FFmpeg + NVDEC —— ⚠️ **能力面通过，许可面暴露上游判据缺口**

取 **BtbN FFmpeg-Builds** 的 `ffmpeg-N-128100-gceb466cbcd-win64-lgpl`（发布标签 `autobuild-2026-10-26-14-25`）。下载产物 SHA256 `a46195b14ea18385570a9b154868813495195f199b9c886d67b16d474c883b34`，与 GitHub Release API 公布的 digest **逐位吻合**（分段并发下载没有改变字节）。

能力实测（本机 RTX 4060 Laptop / 驱动 560.94）：

| 项 | 命令 | 结果 |
|---|---|---|
| `cuda` hwaccel 存在 | `-hwaccels` | ✅ |
| CUVID 硬解存在 | `-decoders` | ✅ `h264_cuvid` / `hevc_cuvid` / `av1_cuvid` / `vp9_cuvid` … |
| **真的走硬件**（不是静默回落） | `-hwaccel cuda -hwaccel_output_format cuda -c:v h264_cuvid` | ✅ 日志 `Stream #0:0 -> #0:0 (h264 (h264_cuvid) -> rawvideo (native))` |
| 解出与 `--frames` 面板同构的 RGB8 | 同上 + `hwdownload,format=nv12` + `-pix_fmt rgb24` | ✅ 100 帧 → 122 976 000 B = 100 × 854×480×3 |
| 构建参数红线 | `-version` | **无 `--enable-gpl`**、**无 `--enable-nonfree`** ✅；**含 `--enable-version3`** ⚠️ |
| SDK 是否需要我们链接 | `-decoders` 里有 `h264_cuvid` | **不需要**——FFmpeg 内部用 `nv-codec-headers`，本仓库只依赖 FFmpeg（L2 §11 条目 2 由实测坐实） |

**发现 1（要回写 D1 §4 的三条参数）**：D1 的 FFmpeg 红线只列了 `--enable-gpl` / `--enable-nonfree` / 动态链接三项，**没覆盖 `--enable-version3`**。这个"lgpl"构建带 `--enable-version3`（为 `libzmq` / `librsvg` 这类 **LGPLv3+** 组件开的）。它**不是** GPL 传染，但许可口径从"LGPLv2.1+"变成"LGPLv2.1+/LGPLv3+ 混合"——**G0-2 的验收文本必须显式表态**，否则"三条参数全绿"是一句没有覆盖力的话。

**发现 2（R2 的直接证据，本片最有价值的一条实测）**：同一个文件、同一台机器、同样 100 帧，**CPU 解码与 NVDEC 解码的 RGB 字节不相同**：

| 量 | 值 |
|---|---|
| 逐字节不同的比例 | **54.21 %**（66 659 568 / 122 976 000） |
| 单通道最大差 | **4** |
| 平均绝对差 | **0.6285** |
| 完全一致的帧 | **13 / 100** |

→ L2 §5 的 **R2（解码器输出不进入任何哈希）**不再是"跨驱动/版本不保证一致"的推测，而是**本机两个解码器实测就不一致**。这条必须写进 regression 报告，并且它顺带解释了为什么 ADR-0009 把字节承诺限制在"本仓库拥有的东西"。

**发现 3（CLI 时序旁证，不是判据 (c) 的数字）**：`ffmpeg` CLI 解 100 帧 → CPU 0.292 s / NVDEC+hwdownload 0.496 s。**在 854×480 这个尺寸上硬件解码路径反而更慢**——正是 L1 §7 **R-D6**（零拷贝收益被小分辨率、单帧串行、同步点抵消）预测的形态。判据 (c) 仍以本片自建流水线 + `TransferCounters` 为准；若 Z 在低分辨率确实不划算，按 L2 §7"不达标的关闭路径"②如实报告并给出"从哪个分辨率起才划算"的曲线。

**发现 4（D1 §3 的"会话数上限未核验"补上一格）**：本机同驱动（560.94）并发拉起 `h264_cuvid` 解码实例，**4 / 8 / 16 路各跑一轮，失败数 0/4、0/8、0/16**（每路解 1 帧，stderr 全空）。
⚠️ **口径要写清**：测的是**并发会话能否建立**，不是长时间多路流式解码的吞吐或稳定性——会话生命周期只有单帧长度，GeForce 的历史上限针对的是**并发长会话**。这条把"完全未核验"推进到"≥16 路瞬时并发可用"，本片 §11 条目 6 的"只核验、不优化"仍然需要真实流水线才能收尾。

**产品路径的构建选择**：上面这个是**静态**构建（`ffmpeg.exe` 内含全部库），只能当开发期 CLI 工具；LGPL 的动态链接义务要求用 `win64-lgpl-shared`（DLL + 导入库 + 头文件），已在取。

### G0-1 cuDNN 9 + CUDA EP —— 进行中

已确认依赖闭包的精确形态（从 `onnxruntime_providers_cuda.dll` 的导入表实测，不是文档推断）：

| 需要的 DLL | 本机状态 |
|---|---|
| `cudart64_12.dll` / `cublas64_12.dll` / `cublasLt64_12.dll` / `cufft64_11.dll` / `nvrtc64_120_0.dll` | ✅ 全在 CUDA 12.6 toolkit |
| `nvcuda.dll` | ✅ 驱动 560.94 |
| **`cudnn64_9.dll`** | ❌ **唯一缺口**（实测确认，与 D1 §2 一致） |

**装载实测（2026-10-27）——G0-1 仍未关闭，而且缺的不是 cuDNN**：

| 步骤 | 结果 |
|---|---|
| cuDNN 9.6.0.74 win_amd64 wheel 取回 | 502 799 547 B，**sha256 与 PyPI `digests.sha256` 逐位吻合**（`6a646630…1e7659`）。取数过程连踩三个工具坑，见 problem-log 018 |
| 解出的 DLL | `cudnn64_9.dll` + `cudnn_ops / cnn / graph / heuristic / adv / engines_*64_9.dll`（合计 ≈ 0.9 GB） |
| `dumpbin /dependents onnxruntime_providers_cuda.dll` | 静态依赖只有 **cublasLt64_12 / cublas64_12 / cudart64_12 / nvcuda / onnxruntime_providers_shared + VC++ 运行库**；**cuDNN 不在导入表里**（ORT 运行期动态装载） |
| VC++ 运行库 | `MSVCP140 / MSVCP140_ATOMIC_WAIT / VCRUNTIME140 / VCRUNTIME140_1` **四个都在 System32**，不是缺件 |
| cuDNN + CUDA 12.6 进 PATH，跑 `--ep cuda --adapters rtdetr_v2_s` | **仍然失败**：`first_fault = "… CUDA EP unavailable (OrtSessionOptionsAppendExecutionProvider_Cuda: Failed to load shared library)"` |
| 但**故障形态变了** | D1 §2 记的是 **Error 127（模块找到、过程找不到）**；现在是**模块找不到**。装载确实向前一步，缺口不在同一处 |
| **真正的天花板** | `nvidia-smi` 与 `runtime_probe` 一致报告 **Driver 560.94 / CUDA 12.6**（driver 与 runtime 同值）。ORT 1.31 的 CUDA EP 要求 **CUDA 12.8+**——D1 §2 路径 A 的原文是"CUDA **12.8+** 运行时 **+** cuDNN 9"，**两件是一套，不是二选一** |

**结论（改变 D5 的开工前提）**：**只补 cuDNN 打不开 CUDA EP。** 缺的是**驱动**：560.94 的上限就是 12.6，装 12.8 运行库也要 ≥570.x 驱动配合。G0-1 的动作因此从"装 cuDNN"升级为"**升驱动 + 装 CUDA 12.8 运行时**（cuDNN 9 已完成）"。
**没有查清的部分，如实登记**：具体卡在哪个模块**没有被钉死**（126 只说明"某个模块没找到"）。下一步是逐个核对 ORT 运行期动态装载的 DLL 名，而不是继续猜。
⚠️ **本次跑出的 `detect:rtdetr_v2_s p50 205 ms` 是 CPU EP 回落的数字**，不得被引用成任何 GPU 主张，也不得与 C3 的 518 ms 混为同一个量（构建/线程配置不同）。

### 回归确认（2026-10-27，批准落地之后重跑）

本轮只动文档与取数工具，未动 C++；仍按 AGENTS 硬规则 #4 把**每一种构建配置**跑全：

| 配置 | 结果 |
|---|---|
| `build/`（默认 ORT **OFF**，参考模式零外部依赖） | **18/18 绿** |
| `build-ort-c5/`（Release + ORT **ON**） | **20/20 绿** |
| 600/42 冻结基线（`build/omniworld_loop.exe --ticks 600 --seed 42`） | `world_hash 574361e62eec92b9`、`ledger_hash 459ff85132cc103e`、`entries=9746` —— **逐位不变** |

### 实现期要顺手修掉的既有缺陷（2026-10-27 发现，不是本片新增）

- **`apps/omniworld_loop.cpp:69-73` 的 usage 文本漏了四个已发布的开关**。实测对照（`grep` 两侧再求差集）：`src/loop/driver.cpp` 解析 **15** 个开关，usage 只写 **11** 个，缺的是 **`--ep`、`--score-space`、`--bounds`、`--margin`**。现象：`omniworld_loop.exe --help` 打出的用法里**没有这四项**，而它们都是真的在工作的开关（`--score-space` 是 D4 的主入口，`--ep cuda` 是 G0-1 的验收命令——**验收命令本身不在 usage 里**）。
  **本片加 `--video` 时必须把 usage 与 driver 的解析表对齐，并加一条测试断言"每个被解析的开关都出现在 usage 里"**——否则这类漂移会一直复发（usage 文本与解析表是两个真相源，且没有任何机制让它们同步）。修法建议：把开关表收成一张表，usage 与解析共用它。
