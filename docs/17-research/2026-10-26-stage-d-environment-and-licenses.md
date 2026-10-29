# 阶段 D 环境与依赖核验（2026-10-26）

> D1 的唯一产物。每条结论后面写明**命令**或**一手来源**，可被下一个人复跑/复查。**未核验**的地方一律标注，不拿经验帖当依据。
> 上承：[阶段 D L1](../design/stage-d-real-data/概要设计.md) §2 条 8 + [D1 L2](../design/stage-d-real-data/slices/01-environment-and-dependency-verification.md)；票据：[D1](../../.scratch/stage-d-real-data/issues/01-environment-and-dependency-verification.md)。

## 0. 结论摘要（先看这五条）

1. **CUDA 本身可用**：驱动与运行时都是 12.6，设备 RTX 4060 Laptop（CC 8.9 / 24 SM / 8 GiB / 空闲 6.93 GiB）。挡住 CUDA EP 的不是 CUDA，是 **cuDNN 9 完全缺失**（整机找不到头文件，也找不到 `cudnn64_9.dll`）。
2. **传输带宽实测到手**：64 MiB 的 H2D p50 **5.241 ms / 12.81 GB/s**，D2H **5.117 ms / 13.12 GB/s**。换算：一帧 1080p RGB（6.22 MB）单程约 **0.49 ms**——这就是"少一次往返"的值，D5 的零拷贝判据终于有了分母。
3. **FFmpeg 走 LGPL 是安全的，但有两条硬约束**：不能启用 `--enable-gpl`（一旦启用，"GPL applies to all of FFmpeg"，传染到我们的二进制），且 LGPL 构建必须**动态链接 + 源码可得 + 保留声明**。
4. **NVIDIA Video Codec SDK 的许可允许以目标码集成分发**，但它不是开源许可，且只允许"随产品以 object code 分发"——**SDK 本体不进仓库**，只在构建期引入。
5. **数据集的门槛被 ADR-0013 解掉了**：评分改在观察归一化空间后**不再需要相机标定**，所以候选集从"KITTI 类"扩大到"任何带 2D 框的数据集"。已核验：**COCO 的标注是 CC BY 4.0**。

## 1. 硬件与传输实测（可复跑）

```bat
build\runtime_probe.exe --json
```

| 项 | 实测值 |
|---|---|
| OS / CPU | Windows 11 build 26200 · Intel i9-14900HX（24 物理核 / 32 逻辑核）· 31.80 GiB |
| GPU | NVIDIA GeForce RTX 4060 Laptop，compute capability **8.9**，24 SM，8.00 GiB（空闲 6.93 GiB） |
| CUDA 驱动 / 运行时 | **12.6 / 12.6** |
| H2D（64 MiB） | p50 **5.241 ms** → **12.81 GB/s**（best 12.93） |
| D2H（64 MiB） | p50 **5.117 ms** → **13.12 GB/s**（best 13.14） |
| Vulkan | 可用（loader 存在，1 个物理设备；本项目阶段 D 不用） |

**派生数字（D5 会用到）**：

| 计算 | 结果 |
|---|---|
| 1080p RGB 帧 = 6.22 MB ÷ 12.81 GB/s | **≈ 0.49 ms / 次 H2D** |
| 30 fps 连续上载 | 6.22 MB × 30 = **186 MB/s**（远低于 12.81 GB/s，带宽不是瓶颈，**往返次数与同步点才是**） |
| 若每帧多一次"GPU→CPU→GPU"往返 | 每帧多 **≈ 0.98 ms**，30 fps 下相当于每帧预算的 3% |

> 这条派生说明零拷贝的真实收益不在带宽，而在**消除同步点与多余往返**——这决定了 D5 的判据必须以"拷贝次数 + 同步次数"为主、"字节数"为辅。

## 2. cuDNN 缺口：精确形态

```bat
dir "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\*\include\cudnn*.h"
dir C:\Windows\System32\cudnn64_9.dll
```

两条命令都是空——**整机没有任何 cuDNN**（头文件与运行时都缺）。这与 C3/C4 的运行时表现一致：`AppendExecutionProvider_CUDA` 报 `Error 127`（找不到过程/依赖），随后被隔离层记为 `faults=1` 并写进 `first_fault`。

**修复路径（择一，均需 owner 动手）**：

| 路径 | 动作 | 风险 |
|---|---|---|
| **A（推荐）** | 装 CUDA **12.8+** 运行时 + **cuDNN 9**，匹配 ORT 1.31 的 CUDA EP | 需下载安装；版本必须成对（C1 research note 已核验 1.31 的要求） |
| B | 改用与 CUDA 12.6 匹配的 ORT 版本 | 需核验"ORT 版本 ↔ CUDA/cuDNN 版本"对应表，且可能牵连模型导出/算子支持；换 ORT 会动摇 C3/C4 的全部实测数字，代价大于收益 |

**结论**：走 A。在 A 完成前，`--ep cuda` 的失败形态已经是**可诊断的**（非零退出 + `first_fault`），不是静默降级。

## 3. NVDEC 能力：能核验什么、不能核验什么

| 项 | 状态 |
|---|---|
| NVIDIA 驱动存在、设备可用 | ✅ 已实测（probe） |
| NVDEC 硬件存在于该 GPU | ✅ 架构事实（CC 8.9 的 GeForce 含 NVDEC），但**本机无 SDK 头文件/库，未跑过任何解码** |
| 并发解码会话数上限 | ❌ **未核验**：GeForce 消费卡的会话数限制只能实测（SDK 就绪后跑多路并发解码） |
| 支持的编码/色度采样（4:2:0 / 4:4:4） | ❌ **未核验**：同上，需 SDK + 样本 |
| FFmpeg 是否在 PATH | ❌ 不在（`which ffmpeg` 为空）——阶段 D 需要自带或自建 |

**结论**：D5 开工的前置不是"CUDA 能用"，而是"**cuDNN 9 就绪 + Video Codec SDK 解压 + 一个可解码的样本**"。这三件没到齐之前，D5 只能写接线，不能出数字。

## 4. FFmpeg 许可（一手来源，已核验）

来源：[ffmpeg.org/legal.html](https://ffmpeg.org/legal.html)。

| 事实 | 原文/含义 | 对本项目的要求 |
|---|---|---|
| 默认许可 | FFmpeg 是 **LGPLv2.1+** | 可用，但受 LGPL 义务约束 |
| `--enable-gpl` | 启用即"invokes GPL"；用到 GPL 部分则 **"If those parts get used the GPL applies to all of FFmpeg"** | **构建参数里禁止 `--enable-gpl`**；同理禁止 `--enable-nonfree` |
| LGPL 义务 | 需**动态链接**、**源码可得**、保留版权与许可声明 | 我们的二进制必须动态链接 FFmpeg，随附声明，且提供获取 FFmpeg 源码的方式 |
| 是否传染我们的代码 | LGPL 不传染（与 GPL 不同） | 引擎自身代码不受影响——但**这是"必须动态链接"换来的**，静态链接触发 LGPL 的重新链接义务 |

**结论**：可行，且与 ADR-0012 同构——FFmpeg 关在默认 OFF 的可选构建里。构建参数写死为："LGPL 构建、不加 `--enable-gpl`/`--enable-nonfree`、动态链接"。D5 的 L2 必须把这三个参数连同 `ffmpeg -version` 输出一起写进报告，否则无法证明用的是 LGPL 构建。

## 5. NVIDIA Video Codec SDK（一手来源，已核验）

来源：[NVIDIA Video Codec SDK License Agreement](https://developer.nvidia.com/nvidia-video-codec-sdk-license-agreement)、[SDK 页面](https://developer.nvidia.com/video-codec-sdk)。

- 允许"distribute the Software and derivative works thereof **(in object code only) as integrated in Licensee software products**"——即可以随应用分发运行时**目标码**。
- 明确的是**目标码 + 集成分发**，**不是**开源许可；没有授予以源码形式再分发 SDK 本体的权利。
- 未发现禁止开源项目使用的条款，但这不是 OSI 许可，因此：

| 决定 | 理由 |
|---|---|
| **SDK 本体不进仓库** | 与数据集同理：不入库、不进 git，构建期由开发者本地解压 |
| 仓库只放**构建脚本 + 版本记录 + 许可声明** | 可复现与合规同时满足 |
| 对外工件如实标注依赖了 NVIDIA 专有 SDK | 作品集 Gate 要求依赖透明 |

## 6. 数据集候选（按"是否已核验"分级）

前提变化：**ADR-0013 之后不需要相机标定**——评分在观察归一化空间完成，只要有 2D 框 + 帧序列即可。

| 数据集 | 形态 | 标注 | 许可（核验状态） | 时间结构 | 判断 |
|---|---|---|---|---|---|
| **COCO val2017** | 5000 张静态图 | 2D 框 + 分割 + 类别 | ✅ **标注 CC BY 4.0**（[COCO terms](https://cocodataset.org/#termsofuse)；标注属 COCO Consortium）**；图像来自 Flickr，各自许可不同，需逐图署名** | 无（逐帧） | ✅ **D2/D4 首选**：判据 (c) 是逐帧校准问题，不需要时序；许可清晰、体积可控（val 约 1 GB） |
| MOT17 / MOT20 | 视频序列 | 2D 框 + 跟踪 id | ❓ 未核验（社区普遍记作非商业） | 有 | 仅当需要跟踪连续性时再核；**若为非商业则不得进公开作品集** |
| KITTI | 视频帧 + 3D | 2D/3D 框、标定 | ❓ 未核验 | 有 | ADR-0013 后不再需要标定，优先级下降 |

**决定**：D2/D4 用 **COCO val 的子集**（建议 ≤200 张，够统计又够快），获取脚本入库、数据不入库、报告附署名与许可说明。

### 6.1 D3 实测更正（2026-10-26）：图像许可比本表更严，且体量比"约 1 GB"小两个数量级

本表把图像许可写成"各自许可不同，需逐图署名"，语气像手续问题。D3 取数时把 `instances_val2017.json` 里的 `licenses[]` × `images[].license` 实测展开，**性质是红线级别**：

| Flickr 许可 | id | val2017 张数 | 面板准入 |
|---|---|---|---|
| BY-NC-SA | 1 | 1 431 | ❌ 非商业 |
| BY-NC | 2 | 630 | ❌ 非商业 |
| BY-NC-ND | 3 | 1 414 | ❌ 非商业 |
| BY | 4 | 857 | ✅ |
| BY-SA | 5 | 417 | ❌ 派生（缩放）会触发 ShareAlike |
| BY-ND | 6 | 246 | ❌ 派生（缩放）被禁止 |
| 无已知版权限制 | 7 | 5 | ✅ |
| 美国政府作品 | 8 | 0（val 内未出现） | ✅ |

**非商业合计 3 475 / 5 000 = 69.5%**。公开作品集不能靠"我们不商用这份面板"来兜底，所以准入规则收紧为 **id ∈ {4, 7, 8}**，逐图署名与 `flickr_url` 由 `tools/fetch_coco_subset.mjs` 写进 `LICENSE.tsv`（数据不入库，入库的是脚本 + 固定子集清单 + 每图 SHA256）。实测落点：D3 面板 200 张全部 license 4。

第二条更正：**"val 约 1 GB"不需要付**。官方桶（S3）应答 HTTP Range，`annotations_trainval2017.zip` 252 907 541 B 只读尾部 64 KiB + 中央目录 642 B + 目标条目的 deflate 块（6 529 031 B）就拿到全部标注；图像按 `val2017/<file_name>` 逐张 GET。200 张面板实际下载 **约 40 MB**。→ D5 若要用视频样本，同一手法可用，不必先承诺大带宽。

## 7. 对阶段 D 的四条影响

1. **D5 的判据分母有了**：12.81 GB/s + 每次往返 ≈0.49 ms（1080p）→ "下降 ≥50%" 可以落在**往返次数**上，而不是虚的百分比。
2. **D5 的真正前置是环境三件套**（cuDNN 9 / Video Codec SDK / 可解码样本），不是"只要有 CUDA"。
3. **FFmpeg 的构建参数必须进报告**，否则许可主张不可证。
4. **D2/D4 不必等 GPU**：COCO 子集 + raw 帧序列 + 现有两个模型，全程 CPU 可完成——L1 §2 条 9 的结构解耦成立。

## 8. 待核验清单（交给 D5 / owner）

| # | 待核验 | 归属 |
|---|---|---|
| 1 | cuDNN 9 / CUDA 12.8+ 安装（路径 A） | owner 环境操作 |
| 2 | NVDEC 并发会话数上限、支持的色度采样 | D5（需 SDK + 样本） |
| 3 | ORT 版本 ↔ CUDA/cuDNN 对应表（仅当走路径 B） | 若 A 不可行再核 |
| 4 | MOT17/KITTI 的确切许可（仅当需要时序数据） | D4/D5 之前 |
| 5 | Video Codec SDK 版本与驱动的最低要求对应关系 | D5 |
