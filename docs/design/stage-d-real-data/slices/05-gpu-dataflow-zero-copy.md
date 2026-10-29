# L2 · 切片 D5「GPU 数据流与零拷贝」详细设计

> **设计版本**：v1 · **批准方式**：owner 批准后进入 `implementing` · **批准日期**：2026-10-27
> 状态：**implementing**（owner 2026-10-27 批准本 L2；`design-review → implementing` 这一跳由 owner 触发——`docs/agents/triage-labels.md` 硬规则 1）。
> **owner 决议（2026-10-27，三条全采纳）**：① §7 的判据 (c) **两基线修正**获批，并已回写 [L1 §8](../概要设计.md) 判据 (c)（L1 升 v1.1，阈值 50% 不变）；② §11 条目 2 的偏差成立（**Video Codec SDK 不由本仓库链接**，产品路径只依赖 LGPL FFmpeg 一个可选依赖）；③ §11 条目 8 成立（**不落地 `TransferCounters` 空壳**，本片按整体落地）。
> ⚠️ **Gate 0 环境三件套仍在推进中**（§2）：未就绪前只做 §9 第 1–7 项（不需要 GPU 的接线与测试），**不出任何性能数字**。
> 上承：[阶段 D L1](../概要设计.md) §2 条 3 / §6 / §7 **R-D1**·**R-D6** / §8 判据 (c) + [ADR-0009](../../../adr/0009-reproducibility-dual-mode.md)（双模式与容差）+ [ADR-0012](../../../adr/0012-onnxruntime-optional-dependency.md)（可选依赖的构建级证明）+ [D1 环境与许可核验](../../../17-research/2026-10-26-stage-d-environment-and-licenses.md) §1·§2·§3·§4·§8 + [D2 L2](./02-frame-source-supply-path.md)（帧供给接缝）。票据：[05](../../../../.scratch/stage-d-real-data/issues/05-gpu-dataflow-zero-copy.md)。
> ⚠️ **本片是全阶段唯一依赖 GPU 的切片。环境未就绪，本 L2 只冻结设计，不产出数字**（§2）。L1 §2 条 9 的架构解耦就是为此：D2–D4 已经交付，D5 被卡只卡自己。

---

## 1. 程序描述

引擎最初的技术论点：**解码 → 推理 → 世界状态更新尽量全程显存驻留**，并且"最少拷贝、同步、延迟"是**有实测上界的判据**，不是口号。本片把这句话变成三件可执行的东西：

1. **`TransferCounters`**（`host_to_device_bytes` / `device_to_host_bytes` / `copies` / `syncs`）并入逐阶段计量载体（契约⑨），**参考模式下恒为 0 并作为断言**——这是"零拷贝主张只在性能模式成立"的可检验形态；
2. **NVDEC 解码路径只注册在性能模式**，解码输出**不进入任何哈希**（跨驱动/版本不保证一致，ADR-0009 只承诺本仓库拥有的字节）；
3. **判据 (c) 的实测协议与冻结数值**：跨设备拷贝次数/字节数上界、相对基线的下降、每帧 p95 延迟预算。

**本片不做什么**（前置声明，防止范围蔓延）：不做 TensorRT EP、不做 NVENC 编码、不做 GPU 渲染器、不做多路并发解码的吞吐调优（会话数上限只核验、不优化）。

---

## 2. 环境状态与 Gate 0（开工门）

**2026-10-27 复核（D1 结论仍然成立）**：

| 项 | 命令 | 实测 | 判定 |
|---|---|---|---|
| CUDA 工具链 | `nvcc --version` | 12.6（V12.6.20） | ✅ 可用 |
| 驱动 / 设备 | `nvidia-smi` | 560.94 · RTX 4060 Laptop · 8 GiB | ✅ 可用 |
| **cuDNN 9** | `dir "…\CUDA\*\include\cudnn*.h"`、`dir C:\Windows\System32\cudnn64_9.dll` | **两个都空** | ❌ **缺**（挡 CUDA EP） |
| **FFmpeg** | `which ffmpeg` | 空 | ❌ **缺**（挡 NVDEC） |
| **可解码样本** | — | 无 | ❌ **缺**（挡一切实测） |

**Gate 0 = 三件套就绪，缺一不可**：

| # | 动作 | 验收（可复跑） | 归属 |
|---|---|---|---|
| G0-1 | 装 CUDA **12.8+** 运行时 + **cuDNN 9**（ORT 1.31 的 CUDA EP 要求，D1 §2 路径 A） | `--ep cuda` 跑通、`first_fault` 为空 | **owner 环境操作** |
| G0-2 | 一份 **LGPL 构建**的 FFmpeg（**禁 `--enable-gpl` / `--enable-nonfree`**、动态链接；含 `--enable-cuda-nvcc` 与 nvdec） | `ffmpeg -version` 三个构建参数进报告（D1 §4） | owner / 构建期 |
| G0-3 | 一个**许可可入库外**的可解码视频样本（不入 git，与数据集同规则） | 能解出与 `--frames` 面板同构的 RGB 帧 | owner |

**纪律**：G0-1/G0-2/G0-3 未同时满足之前，**只做 §4/§5/§6 的接线，不出任何性能数字**；也不拿别人机器的数字填表（D1 §8、C3 报告 §9 的先例）。**对照实验**（§7 的 B0/B1）全部要求同机同面板同模型。

---

## 3. 架构：一条流水线、三处接缝

```
         ┌──────────── 性能模式（GPU 驻留）────────────┐
视频文件 → NVDEC 解码 → [device NV12] → device 转换 → [device RGB8 张量]
                                                       ↓ IOBinding（无 H2D）
                                              ORT CUDA EP 推理 → [device boxes]
                                                       ↓ 一次 D2H（小，~KB）
                                              后处理 → Proposal[]  → 裁决/账本（host）
         └──────────────────────────────────────────────┘

         ┌──────────── 参考模式（不变，零外部依赖）──────┐
帧清单 → FileFrameSource → [host RGB8] → ORT CPU EP → Proposal[]
         └──────────────────────────────────────────────┘
```

三处接缝：**帧供给**（`FrameSource`，D2 已冻结）、**设备边界**（`DeviceBridge`，本片新增，唯一允许调用 `cudaMemcpy*` / 同步的地方）、**适配器**（`PerceptionAdapter`，只加一个带默认值的能力查询）。

**零拷贝的准确含义**：**1080p 帧体一次都不跨设备**。必须跨的只有检测输出（`[1,300,4]` + `[1,300,80]` 的 float32，量级 KB），它跨一次 D2H。这是 D1 §1 的算术结论——收益来自**减少往返次数**，不是带宽（H2D 实测 12.81 GB/s，1080p 单程约 0.49 ms）。

---

## 4. 数据结构与接口

### 4.1 `TransferCounters`

```cpp
// 跨设备边界的实测计量。计数在 DeviceBridge 里发生，不在调用方。
struct TransferCounters {
  std::uint64_t host_to_device_bytes = 0;
  std::uint64_t device_to_host_bytes = 0;
  std::uint64_t copies = 0;  // 任何 cudaMemcpy* 调用（不论方向、不论大小）
  std::uint64_t syncs = 0;   // 任何流同步 / 设备同步
};
```

- 位置：`src/core/types.hpp`（与 `Observation`/`ScoreContext` 同族——参考模式也要能编译它，所以**不得** include 任何 CUDA 头）；
- 载体：并入 `RunReport` 的逐阶段计量（契约⑨ 的"只加字段不改语义"），工件里以 `transfers` 节输出；
- **参考模式不发射该节**（与 `annotation_truth` 同一形态），默认工件因此保持既有键序列。

### 4.2 `DeviceFrame`（观察的设备视图）

```cpp
// 帧在设备上的视图。data 是不透明设备指针：本头文件属于零依赖构建，
// 不得引入 CUDA 类型。
struct DeviceFrame {
  void* data = nullptr;  // CUdeviceptr，由 DeviceBridge 分配
  std::size_t bytes = 0;
  int device_id = -1;
  int pitch = 0;         // 行跨距，可以大于 width*3（对齐要求）
};
```

`Observation` 增加一个字段 `DeviceFrame device;`，并冻结一条规则：**`rgb` 与 `device` 至多一个被填充**。参考模式 `device.data == nullptr` 恒成立（结构上，不是纪律上：OFF 构建里没有任何代码会填它）。

契约⑧ 的语义未变（观察仍是"RGB 帧 + 元数据 + 可见性"），变的只是帧体在哪个内存里——与 ADR-0013 同一手法（"只加字段不改语义"）。`Observation` 不参与任何哈希，因此 `world_hash` / `ledger_hash` 不受影响（R1）。

### 4.3 `DeviceBridge`（唯一跨越设备边界的地方）

```cpp
class DeviceBridge {
 public:
  void upload(void* dst, const void* src, std::size_t bytes);
  void download(void* dst, const void* src, std::size_t bytes);
  void sync();
  const TransferCounters& counters() const;
};
```

**R4**：`cudaMemcpy` / `cudaMemcpyAsync` / `cudaDeviceSynchronize` / `cudaStreamSynchronize` **只允许出现在 `device_bridge.cpp` 里**。理由：计数必须计**真实**的跨界，而不是计"设计上以为会发生"的跨界；一个唯一咽喉点让"真实"可被审阅（源码级 grep 作为 review 动作，不进测试）。

### 4.4 `NvdecFrameSource`

实现 D2 已冻结的 `FrameSource` 接口（`next()` / `name()` / `frame_count()`），`next()` 填 `device`、不填 `rgb`。**不新增接口**——接缝在 D2 就已经留对了。

CLI 新增 `--video <path>`，与 `--frames <manifest>` **并列**：

| 参数 | 源 | 允许的模式 |
|---|---|---|
| `--frames`（D2） | `FileFrameSource`（已解码 raw） | 参考 + 性能 |
| `--video`（本片） | `NvdecFrameSource` | **仅性能模式**；参考模式下给具名错误 |
| 两者同时给 / 都不给 | — | 具名错误 |

**为什么用两个参数而不是让 `--frames` 认视频**：参考模式"解码器输出不进入任何哈希"这条规则就变成了一道**参数级**守卫，而不是一句注释。D3 的 `--annotations` 必须与 `--frames` 同传也是同一形态。

### 4.5 适配器的设备接受能力

```cpp
// 默认 false：既有的 CPU 适配器不需要改写任何一行。
virtual bool accepts_device_frames() const { return false; }
```

循环在把设备帧交给适配器之前检查它；`false` 则抛**具名错误**（不是静默回退到 host——静默回退会把"零拷贝没生效"藏起来，正是 problem-log 015 的同族病态）。D5 只让 `rtdetr_v2_s` 返回 `true`；`depth_anything_v2_s` 明确返回 `false`。

---

## 5. 规则（写死，实现照抄）

| # | 规则 |
|---|---|
| **R1 加法式** | 参考模式**逐位不变**：600/42 两哈希（`574361e62eec92b9` / `459ff85132cc103e`）、`slice11/12/16` 字面量、既有指标键的值与顺序。默认工件不长出 `transfers` 节。 |
| **R2 解码输出不入哈希** | 解码帧、设备帧、CUDA 张量都不参与 `world_hash` / `ledger_hash`。理由：跨驱动/版本不保证一致，而 ADR-0009 只承诺本仓库拥有的字节。 |
| **R3 参考模式零跨界** | `transfers.copies == 0 && transfers.syncs == 0` 在参考模式下作为断言。**诚实说明**：D5 之前它恒真（根本没有 GPU 路径）——它防的是"GPU 路径悄悄进参考模式"，其价值在 D5 落地那一刻起才生效。 |
| **R4 唯一咽喉点** | 见 §4.3。 |
| **R5 容差窗口** | 每个 GPU 组件按"实测最大抖动 × 2"声明（L1 Round 1 #4），窗口写进 regression 报告；**不拿别机数字填表**。 |
| **R6 不划算就如实报** | 判据 (c) 以**拷贝字节数与次数**为主判据、延迟为辅（L1 §7 R-D6）。若零拷贝被小分辨率、单帧串行、同步点抵消，**如实写 PARTIAL**，不改判据去迁就实现（阶段 C 的先例）。 |

---

## 6. 构建：可选依赖的默认 OFF 证明

新增一个可选构建开关，与 ADR-0012 同构：

```cmake
option(OW_WITH_FFMPEG "FFmpeg (LGPL, dynamic) for NVDEC decoding" OFF)
```

**OFF 构建的三条证明**（与 C1 的做法一致，可判定）：

1. **不 include**：`ffmpeg` 相关头只出现在 `#ifdef OW_WITH_FFMPEG` 内；
2. **不链接**：`build/` 的链接行里没有 `avcodec` / `avformat` / `avutil`；
3. **不感知**：`--video` 在 OFF 构建里以具名错误拒绝（"built without FFmpeg"），而不是未定义符号或静默忽略。

**Video Codec SDK 不由本仓库链接**（**对票据措辞的偏差，见 §11 条目 2**）：FFmpeg 的 nvdec 封装已经通过 `nv-codec-headers` 在 FFmpeg 内部使用它，我们只依赖 FFmpeg 这一个可选依赖。这一条把 D1 §5 的"SDK 只允许目标码集成分发"从一个**需要处理**的约束降级为**不适用**——产品路径上少一个专有依赖，许可义务收敛为 D1 §4 的三条 FFmpeg 参数。

**全量 ctest 的配置义务**：FFmpeg 配置在 Gate 0 之前**不存在**（无法 configure，因为没有依赖）。因此"每一种构建配置都跑全量"（AGENTS 硬规则 #4）在这段时间里适用于 OFF 与 ON 两套；FFmpeg 配置一经建立，它立刻进入必跑清单。**不把"配置不存在"当成"配置跑过"**。

---

## 7. 判据 (c) 的测量协议（两个基线）

**这里必须先修正 L1 §8 (c) 的一处口径缺陷**：原文"拷贝次数与字节数……**相对 CPU 基线下降 ≥ 50%**"在字面上不可执行——CPU 路径的设备拷贝**恒为 0**，从 0 往下不可能再降 50%。**本片把"相对谁降"写死为两个基线**，两者都必须跑：

> ✅ **已回写**：owner 2026-10-27 批准本修正，[L1 §8](../概要设计.md) 判据 (c) 已同步为两基线形式（L1 升 v1.1，阈值 50% 不变）。本节是 L1 §8 的展开，**冲突时以 L1 为准**。

| 基线 | 流水线 | 它回答的问题 | 用于判定的量 |
|---|---|---|---|
| **B0** CPU 全路径 | FFmpeg **CPU 解码** + ORT **CPU EP** + host 全程 | "GPU 到底快不快" | **每帧 p95 延迟**（零拷贝路径相对 B0 下降 ≥ 50%） |
| **B1** GPU 但有往返 | NVDEC → **D2H 到 host** → ORT **CUDA EP**（隐式 H2D）| "零拷贝到底省了什么" | **H2D+D2H 字节数与 copies 数**（零拷贝路径相对 B1 下降 ≥ 50%） |
| **Z** 零拷贝 | NVDEC → device 转换 → ORT CUDA EP（IOBinding）→ 一次 D2H | 被测对象 | 四项计数 + p50/p95 |

- **绝对上界**（判据 (c) 的前半句）：Z 的每帧 `copies` 与 `device_to_host_bytes` 必须冻结为**字面量**写进 regression 报告——上界不是一个百分比，是一个数；
- **同一性要求**：B0 / B1 / Z 必须同机、同视频、同模型、同分辨率、同帧数。跨机比较不算数；
- **每帧口径**：p95 按**帧**而不是按 tick 报（流可能提前结束，D2 已确立 `ticks_run` 语义）；
- **不达标的关闭路径**（先写好，避免临场发挥）：① 若往返次数下降但延迟不降，判 (c) 的**延迟半边** PARTIAL 并给出同步点分解；② 若小分辨率下零拷贝不划算，**如实报告**并给出"从哪个分辨率起才划算"的实测曲线；③ 不改 50% 这个阈值去迁就实现（同判据 (b) 的纪律）。

---

## 8. 容差窗口

性能模式本来就按 ADR-0009 容差复现，本片为**两个新组件**补窗口：

| 组件 | 抖动来源 | 窗口 |
|---|---|---|
| NVDEC 解码 | 驱动/固件版本 | **不需要**——解码输出不入哈希、不入判定（R2）。只记录 `ffmpeg -version` 与驱动版本作为环境元数据 |
| CUDA EP 推理 | cuDNN 算法选择、原子加顺序、并行归约 | 同一输入跑 **N ≥ 5** 次，取每个指标键的**实测最大抖动 × 2**，逐键写进报告 |

**参考模式不受影响**：CPU EP 实测抖动为 0（C3 报告），窗口退化为逐位精确——本片不得为此改动参考模式的比较方式。

---

## 9. 测试计划（slice20，默认两套构建都要绿）

**可以在 Gate 0 之前写、也必须绿的**（不需要 GPU）：

1. `TransferCounters` 零值语义：参考模式报告 `copies == 0 && syncs == 0`；
2. 默认工件**不长出** `transfers` 节（键发射序列 diff，与 D3 的 `annotation_truth` 同法）；
3. `--video` 在 OFF 构建给具名错误，不是崩溃、不是静默忽略；
4. `--video` 与 `--frames` 同时给 / 都不给 → 两条具名错误；`--video` + `--mode reference` → 具名错误；
5. `Observation` 的 `rgb` / `device` 互斥规则（构造两个非法组合各断言一次）；
6. `accepts_device_frames()` 默认 `false`，设备帧交给它 → 具名错误（不是回退）；
7. 既有全量套件保持绿（R1 的成对纪律）。

**只能在 Gate 0 之后写、也只能在 FFmpeg 配置里绿的**（§6 的配置义务）：

8. `NvdecFrameSource::next()` 的帧数 / 元数据 / 末帧 `false`；
9. 一次真实流水线的 `copies` / `bytes` 行为断言（不是"看起来对"，是计数等于 §7 冻结的字面量）；
10. 性能模式容差复现（同一视频跑两遍，指标在窗口内）。

**0 条断言涉及判据 (c) 的裁定本身**——裁定不写进测试（problem-log 012）。

---

## 10. 与票据验收对照

| 票据 AC | 落点 |
|---|---|
| 可选依赖接入：FFmpeg / Video Codec SDK 默认 OFF 的构建级证明 | §6（**SDK 不链接**，偏差见 §11 条目 2）+ 测试 3 |
| NVDEC 解码路径入性能模式；解码输出不入哈希 | §4.4 + R2 + 测试 3/4/8 |
| `TransferCounters` 并入逐阶段采样；参考模式恒 0 并断言 | §4.1 + R3 + 测试 1/2 |
| 出口判据 (c) 实测并冻结数值 | §7（两个基线的修正）+ 测试 9 |
| 每个 GPU 组件的容差窗口按原则声明 | §8 |
| slice20 测试：拷贝计数行为断言 + 性能模式容差复现 | §9 第 8–10 项 |
| 全量 ctest 在每一种构建配置下都跑 | §6 末段（配置义务的准确表述）+ §9 |

---

## 11. 风险、偏差与不做的事

| # | 条目 |
|---|---|
| 1 | **本片在 Gate 0 之前无法产出任何数字**（§2）。这是环境事实，不是进度：D1 §8 已经写明"三件没到齐之前，D5 只能写接线，不能出数字"。 |
| 2 | **偏差：Video Codec SDK 不由本仓库链接**（票据原文写"FFmpeg / Video Codec SDK"两个依赖）。走 FFmpeg 的 nvdec 封装后，SDK 只在 FFmpeg 内部被用到，我们只依赖 FFmpeg 一个可选依赖。好处：产品路径上少一个专有依赖，§4（LGPL）的三条参数成为唯一的许可义务。 |
| 3 | **偏差：修正 L1 §8 (c) 的口径**（§7）。"相对 CPU 基线下降 50%"对设备拷贝不可执行（CPU 基线为 0），本片拆成 B0（延迟）与 B1（拷贝）两个基线。**判据形式变了，阈值没变**。**owner 2026-10-27 批准，并已回写 L1 §8**（L1 升 v1.1，§8 标题本即标注"数值形式待 owner 确认"，故属该未冻结要素的确认，不另立 ADR）。 |
| 4 | **R3 在 D5 落地前恒真**（§5）。它是护栏，不是证据；报告不得把它写成"已证明零拷贝"。 |
| 5 | **`Observation` 增加一个 `DeviceFrame` 字段**。契约⑧ 语义不变（只加字段），`Observation` 不入哈希，因此冻结基线不受影响。 |
| 6 | **NVDEC 的会话数上限与色度采样仍未核验**（D1 §3）：只能在 Gate 0 之后实测。本片只核验、不优化。 |
| 7 | **不做**：TensorRT EP、NVENC、GPU 渲染器、Vulkan 后端、多路并发解码吞吐优化、统一内存（managed memory）——最后一条如果将来要做，需要单独立项（它会改"谁拥有这块内存"的语义）。 |
| 8 | **不采用"先落地 `TransferCounters` 空壳"的分阶段方案**：没有第一个写入者的计数器是无消费者的结构（Speculative Generality），而"参考模式为 0"的断言在 GPU 路径存在之前是恒真的同义反复。本片按整体落地，Gate 0 是唯一的门。 |
| 9 | **命名冲突**：仓库里有两个 "(c)"——Gate-1 的判据 (c)（ECE 分离量，D4 已关闭）与本 L1 §8 的 (c) 行（GPU 数据流）。**D5 的 regression 报告必须写全名**，不得只写"判据 (c) 达标"，否则两份证据会互相指代。 |
| 10 | **R-D6 已经有一条先验观测**（票据 Gate 0 小节，2026-10-27）：`ffmpeg` CLI 在本机解同一支 854×480 样本 100 帧，**CPU 解码 0.292 s，NVDEC + hwdownload 0.496 s**——硬件路径在小尺寸上更慢。这不是判据 (c) 的数字（没有计数器、没有同一条流水线），但它是 §7"不达标关闭路径"②的**先验形态**：实现时预期要按分辨率曲线来报，别假设"GPU 一定快"。 |

---

## 12. Gate 0 之后的开工顺序（给实现者的第一小时）

1. 复核 §2 的三条命令，把输出贴进票据（**这是证据，不是仪式**）；
2. 建 FFmpeg 配置（`build-ffmpeg/`），跑一次 `--video` 的最小通路（1 帧也行），确认 `copies > 0` 且 `transfers` 节出现——**先证明计数器活着，再谈减少**；
3. 按 §9 第 1–7 项补测试（大部分在 Gate 0 之前就该写好）；
4. 再按 §7 跑 B1 → 得到"有往返"的基线，最后跑 Z。
