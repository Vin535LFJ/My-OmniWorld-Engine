# NVIDIA Cosmos 3 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建
> 修订：2026-11-05 独立核查修正 6 处（§3 数据行引文与 HF 链接、§3 历史层补 predict2/transfer2.5、§4 Super TTFT 区间、§4 Jetson 命名、§5 license 依据与 pipeline 类名、§7-4/6）；其中 #1 核查员引的 80.70 ms 经复验为 Nano B300 数字，按一手来源改为 Super B300 176.24 ms。
> 修订：2026-11-05 深度调研（[deep/2026-11-05-cosmos-3.md](deep/2026-11-05-cosmos-3.md)）用 `pdftotext`（mingw64 poppler，此前会话未发现）全文解析技术报告（29,331,015 字节），**§7-3 已闭**：训练算力（Nano 31.05T tokens/1024×GB200、Super 17.86T/2048×GB200，pre-training 阶段）升为一手验证，MoT 参数结构（总参数 = 2× dense 主干）获官方句式证实。

## 0. 一句话

NVIDIA Cosmos 3 是 2026-05-31 在 GTC Taipei 发布的开放（OpenMDW-1.1）全模态世界基础模型家族——MoT（Mixture-of-Transformers）双塔架构统一了视觉推理/世界生成/动作预测，三基模 Super 64B / Nano 16B / Edge 4B 配套后训练工具链（Framework/Curator/Evaluator）；对本仓库它是**在别处运行的离线帧数据生产者（②适配器）+ Phase 4 世界模型评测的候选被评对象**，全家族在 OmniWorld 开发机上不可跑（官方仅 Linux，实测最小平台 Jetson Thor T2000 16 GB）。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| 发布于 "NVIDIA GTC Taipei—NVIDIA today launched NVIDIA Cosmos™ 3"，日期 May 31, 2026 | nvidianews.nvidia.com/news/nvidia-launches-cosmos-3-the-open-frontier-foundation-model-for-physical-ai |
| 架构声称："built on a breakthrough mixture-of-transformers architecture that combines vision reasoning, world generation and action prediction in a single system"；自称 "the world's first fully open omnimodel" | 同上 |
| 三个用途（新闻稿原文）：① "A vision language model that understands and reasons across modalities" ② "A world model or video foundation model that simulates physical environments and predicts future world states for training and evaluation" ③ "The backbone for world action models that help train robots to perform specific tasks" | 同上 |
| 官方口号："One model family that sees, reasons, simulates, and acts."；Cosmos 3 = "a suite of omnimodal world models built on a unified Mixture-of-Transformers architecture" | github.com/NVIDIA/Cosmos（README） |
| 架构细节：自回归 transformer（reasoning；causal self-attention、next-token prediction）+ diffusion transformer（generation；对 noisy image/video/audio/action tokens 全注意力）共享权重，统一 3D mRoPE 编码跨模态时空结构 | github.com/NVIDIA/Cosmos（docs/reference/models.md） |
| 双塔 I/O：**Reasoner**（text/vision → text：world understanding、grounding、task planning、embodied reasoning）；**Generator**（text/vision/audio/action → vision/audio/action：world simulation、future prediction、synthetic data、policy learning） | github.com/NVIDIA/Cosmos（README） |
| 三基模："Cosmos 3 ships as three base models"：Super 64B（"Highest quality; synthetic data generation; teacher for distillation"）、Nano 16B（"Balanced speed and quality; strong base model to post-train"）、Edge 4B（"Edge deployment; real-time robot policy and visual reasoning"） | github.com/NVIDIA/Cosmos（README） |
| Edge 采用 Nemotron 推理器："Cosmos 3 Edge: 4B parameter omni-model with NVIDIA Nemotron™-based reasoner" | nvidia.com/en-us/ai/cosmos |
| 10 个示例后训练检查点（cosmos3-examples 集合）：Sim-Auto（闭环驾驶仿真，多视角+LiDAR）、Transfer-Auto（最多 11 相机+LiDAR 的驾驶 SDG）、Sim-Bimanual（闭环动作条件自回归机器人策略仿真）、Sim-Transfer（depth/segmentation/edge/blur 条件迁移）、Super-Text2Image / -Image2Video（各含 4Step 蒸馏版，"17-25x faster"）、Nano-Policy-DROID（"Open SOTA DROID robot policy, runs on RTX Pro 6000"）、Edge-Policy-DROID | github.com/NVIDIA/Cosmos（README + docs/reference/models.md） |
| 工作流矩阵（11 种）：t2v、t2sv（带声音）、i2v、i2sv、v2v-transfer（edge/blur/depth/segmentation 条件）、v2v-predict（首 5 帧≈3 s 条件）、t2i、visual reasoning、forward dynamics（text/image/video/action→video）、inverse dynamics（text/video→action）、action policy（text/image/video→action+video） | github.com/NVIDIA/Cosmos（docs/reference/models.md） |
| 动作条件维度："camera motion (9D) · AV (9D) · egocentric (57D) · single-arm (10D: DROID/UR/Fractal/Bridge/UMI) · dual-arm (20D) · humanoid (29D: AgiBot)" | 同上 |
| 输出格式："JPG, MP4, AAC-in-MP4 (stereo 48 kHz), JSON actions, text" | 同上 |
| 生成参数：分辨率 256p(320×192)/480p(832×480)/720p(1280×720)；帧率 10/16/24/30 FPS；帧数 5–300（默认 189）；"Precision: BF16 tested"；"OS / GPU: Linux; Ampere, Hopper, Blackwell" | 同上 |
| Sim 检查点闭环能力：Sim-Auto "Synchronized multi-view video and LiDAR, generated autoregressively in a closed loop; 10–30 FPS, 480p or 720p"；Sim-Bimanual "up to 901 frames at 30 FPS, 480p"；"Sim-Transfer runs up to 8 parallel environments in real time on one GB200" | 同上 |
| 平台工具链：Cosmos Framework（"Train, optimize, and deploy physical AI models, SFT · LoRA · distillation · RL post-training"）、Cosmos Curator（"Distributed data curation: processing, annotation, filtering, dedup"）、Cosmos Evaluator（"Automated evaluation system for world generation & reasoning outputs"）、Cosmos Data Factory（官网："agentic blueprints"） | github.com/NVIDIA/Cosmos（README）+ nvidia.com/en-us/ai/cosmos |
| 评测套件："PAIBench, Physics-IQ, VLMEvalKit"（README）；COOKBOOK Evaluation 节列 PAI-Bench-G/PAI-Bench-C（生成/受控视频）、PhysicsIQ（物理）、UniGenBench（图像）、RBench（机器人）、TAR & VANTAGE（VLMEvalKit 推理评测） | github.com/NVIDIA/Cosmos（README + COOKBOOK.md） |
| 伴随发布："new datasets for robotics, physics, human motion, autonomous driving, warehouse safety and spatial reasoning, as well as new physical AI agent skills for neural scene reconstruction, defect-image generation and video augmentation" | nvidianews.nvidia.com（新闻稿） |
| Cosmos Coalition："Agile Robots, Black Forest Labs, Generalist, LTX, Runway and Skild AI"；行业用户 Agile Robots、Doosan Robotics、LG Electronics、Samsung Electronics、Skild AI（机器人）、Li Auto（AV）等 | 同上 |
| 与既有全景报告一致的部分：Cosmos-Predict 2.5（长序列预测）、Transfer 2.5（Sim-to-Real，深度/语义分割等空间信号引导）、Reason 2（长思维链）与 Cosmos 3 并存；动作维度 9D/10D/20D/29D/57D 与官方一致 | 见「2026年实时世界运行时与生成式世界模型技术全景报告」Cosmos 一节（docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md，引用不复制；该报告为二手汇编，维度数字经官方 models.md 复核一致） |

## 2. 边界与限制

### 2.1 官方承认的限制（原文）

- 生成质量限制（models.md "Limitations" 节原文）："Cosmos 3 can produce artifacts in long, high-resolution, or physically complex outputs: temporal inconsistency, unstable camera/object motion, sound-video misalignment, action-state inconsistency, object morphing, inaccurate 3D structure, implausible dynamics. Physically grounded simulation, safety-critical control, and complex multi-agent behavior require additional validation, guardrails, and system-level safety analysis before deployment."（github.com/NVIDIA/Cosmos，docs/reference/models.md，2026-11-05）
- Edge 是功能子集：仅 256p/480p、帧数 50–150；t2sv/i2sv/v2v/t2i 均 "Not supported"（docs/reference/models.md I/O 表）
- 条件长度限制：v2v-predict 仅用 "first 5 frames used, up to ~3 s of conditioning"；Sim-Auto/Transfer-Auto 相机机位 "1-11 views (not including LiDAR), standard and fisheye; other rigs need post-training"（同上）
- 部署护栏：Guardrail 走门控仓库 nvidia/Cosmos-1.0-Guardrail，需在 HF 申请访问且 "Logging in alone does not grant access"（github.com/NVIDIA/Cosmos README）
- "Empty cells mean that combination has not been measured yet — not that it is unsupported"（docs/reference/inference_benchmarks.md，速度基准只覆盖已测组合）

### 2.2 我们的推断（非官方）

- **它不是确定性物理引擎**：输出是像素/音频/JSON 动作的生成结果，没有 identity/time/ownership/confidence 四要素中的任何一要素的显式作者权——与 World Contract 的"真值侧唯一作者"模型正交；闭环里它不能当真值权威（契约④），只能当被评对象或离线数据源。
- **Proposal 五元组装不下它的输出**：`Proposal` 只断言 transform + class_tag（[src/core/proposal.hpp:16](../../src/core/proposal.hpp#L16)），Cosmos 3 的 forward-dynamics 输出是视频帧——评测它需要专门的"评测桥"把像素级预测转成可裁决主张或独立的评测口径，这是缺口不是现成接口。
- **显存推断（非官方数字）**：16B BF16 权重 ≈ 32 GB、64B BF16 权重 ≈ 128 GB、4B ≈ 8 GB（按 BF16 ≈ 2 B/参数估算）；官方只给硬件档位与实测平台，未发布逐模型显存数字（见 §4/§7）。
- **本机不可跑的推论**：官方 OS 限 Linux + 本机 Windows；官方实测最小平台 Jetson Thor T2000（16 GB）+ 本机 RTX 4060 Laptop 8.00 GiB（既有材料 D1 实测）⇒ 全家族三基模均在官方支持线以下或之外。
- 第三方称 Nano 权重 ~32 GB BF16、Super 权重 ~120–130 GB 需张量并行——与我们上述估算一致，但**未从官方页面核验**，标 **UNVERIFIED**。（2026-11-05 增补：技术报告 §2.5 官方句式证实参数结构——"Edge is a 4B-parameter model built upon a dense 2B-parameter transformer, Nano is a 16B…dense 8B…, Super is a 64B…dense 32B"，即 MoT 总参数 = 2× dense 主干，BF16 权重估算由此成为参数量的算术推论；"需张量并行"仍无官方依据）

## 3. 许可（代码 / 权重 / 数据 三分）

| 层 | 许可 | 依据 |
|---|---|---|
| **代码**（NVIDIA/Cosmos 仓库、NVIDIA/cosmos-framework 仓库） | **OpenMDW-1.1**（完整许可文本随仓库分发，非引用链接）；COOKBOOK.md 文件头 `SPDX-License-Identifier: OpenMDW-1.1` | github.com/NVIDIA/Cosmos main LICENSE；github.com/NVIDIA/cosmos-framework main LICENSE（2026-11-05 抓取全文） |
| **权重** | **OpenMDW-1.1**——README："Source code and models are released under OpenMDW-1.1."；官网："Cosmos WFMs are available under the OpenMDW1.1 license from Linux Foundation." | github.com/NVIDIA/Cosmos（README）；nvidia.com/en-us/ai/cosmos |
| **数据**（Cosmos Datasets / Physical AI Datasets） | **UNVERIFIED**（逐数据集许可）——HF 全域不可达（见 §7），collection 内各数据集的许可标签未能直验。官方事实（2026-11-05 直接抓取官网 HTML 核验）：官网以列表条目链接形式指向两个 HF collection——"Cosmos Datasets → Open, large-scale synthetic datasets for training world models."（链接 huggingface.co/collections/nvidia/cosmos3-datasets）、"Physical AI Datasets → open, commercial-grade datasets for physical AI developers."（链接 huggingface.co/collections/nvidia/physical-ai）；**归属有一手依据（官网链接 URL），collection 内容与许可未验**。注意：新闻稿的 HF 表述（"download open models from Hugging Face"）仅覆盖模型，不含数据集 | nvidia.com/en-us/ai/cosmos（HTML 直抓） |

**历史层**：旧组织 github.com/nvidia-cosmos 经 GitHub API 全量核验（2026-11-05，`api.github.com/orgs/nvidia-cosmos/repos?per_page=100`）共 12 个仓库，含 cosmos-predict1/predict2/predict2.5、cosmos-transfer1/transfer2.5、cosmos-reason1/reason2、cosmos-rl、cosmos-xenna、cosmos-cookbook、cosmos-dependencies、.github，**全部标注 Apache-2.0**（cosmos-predict2 描述："Cosmos-Predict2 is a collection of general-purpose world foundation models for Physical AI that can be fine-tuned into customized world models for downstream applications."）；该组织 README 已公告 "The Cosmos project is now maintained at: 👉 https://github.com/NVIDIA/Cosmos"（HTML 页面仅置顶显示部分仓库，全量以 API 为准）。即：**1.x/2.x 时代代码仓库是 Apache-2.0，Cosmos 3 时代（NVIDIA/Cosmos）统一 OpenMDW-1.1**。引用旧仓库时需逐仓库确认（代码许可已核；旧代权重的 HF 模型卡许可仍需逐仓库直验）。

**OpenMDW-1.1 条款逐项（自 cosmos-framework LICENSE 全文逐字核验，与 openmdw.ai/license/1-1 一致，2026-11-05）**：

1. **定义**："Model Materials" = (1) 一个或多个机器学习模型（"including architecture and parameters"）+ (2) 全部相关工件（"including associated data, documentation and software"）——一个许可同时覆盖权重、代码、文档、数据。
2. **授权**："permission is hereby granted, free of charge, to deal in the Model Materials without restriction, including under all copyright, patent, database, and trade secret rights included or embodied therein" —— 免费且无限制，涵盖版权/专利/数据库/商业秘密；**无用途限制条款**（无禁止条款清单）。
3. **分发义务**（分发任何部分时）："you shall retain in your distribution (1) a copy of this agreement, and (2) all copyright notices and other notices of origin" —— 仅此两项，无单独署名/变更声明要求。
4. **专利/版权报复条款**："If you file, maintain, or voluntarily participate in a lawsuit ... asserting that the Model Materials directly or indirectly infringe any patent or copyright, then all rights and grants made to you hereunder are terminated, unless that lawsuit was in response to a corresponding lawsuit first brought against you."
5. **输出不受限**："This agreement does not impose any restrictions or obligations with respect to any use, modification, or sharing of any outputs generated by using the Model Materials." —— 对 OmniWorld 至关重要：**Cosmos 生成的帧数据可以按 D1 纪律入库（清单 + SHA256 + 许可记录），输出无许可负担**。
6. **担保与责任**："AS IS, WITHOUT WARRANTY OF ANY KIND"（含适销性/特定用途/不侵权/准确性）；用户自担（1）第三方权利清除（2）获取必要同意（3）尽调；提供方对一切索赔免责。
7. **接受方式与范围**："By exercising rights granted to you under this agreement, you accept and agree to its terms."（行使权利即接受）；许可文本无商标条款、无适用法/管辖条款（许可页面未含，非遗漏转述）。

**版本背景**：OpenMDW-1.1 由 Linux Foundation 于 2026-05-28 发布（"an updated open license framework designed specifically for AI model distributions"），NVIDIA 同日宣布将 OpenMDW 用于 "its Cosmos, Isaac GR00T, Ising, and Nemotron open model families"；OpenMDW 最初由 Linux Foundation 与 PyTorch Foundation 于 2025 年发起（linuxfoundation.org 新闻稿）。**与既有结论的衔接**：2026-10-27 交互闭环笔记核验的 Cosmos-Predict2 用的是 **NVIDIA Open Model License**（"允许商用与派生模型，但不是开源许可"，见该笔记 §5）——Cosmos 家族的许可是**逐代迁移**的：Cosmos 2 时代 NVIDIA Open Model License → Cosmos 3 时代 OpenMDW-1.1。引用任何一代都要看那一代的许可。

## 4. 硬件要求（显存/驱动/OS，来源+日期）

| 变体 | 参数 | 官方硬件档位（README 表） | 官方实测推理平台（inference_benchmarks.md） | COOKBOOK GPU 预算 |
|---|---|---|---|---|
| Cosmos3-Super | 64B | H200 / B200 / GB200（"Highest quality; synthetic data generation; teacher for distillation"） | Reasoner: RTX PRO 6000 Blackwell、H20、H100 NVL、H200 NVL、H200 141GB、B200、B300（vLLM，单卡 TTFT 0.18–1.24 s：B300 176.24 ms → H20 1241.58 ms，concurrency 1、Output 1/Video 1 FPS 口径，2026-11-05 复验） | 生成推理 `S`/`M`；蒸馏 `L` |
| Cosmos3-Nano | 16B | RTX Pro 6000 / H100 / B200（"Balanced speed and quality"） | Reasoner: RTX PRO 6000 Blackwell、H20、H100 NVL、**H100 80GB HBM3 SXM**、H200 NVL、H200 141GB、B200、B300 | 推理 `S`/`M`；Sim-Auto/Sim-Bimanual/Sim-Transfer 仿真 `M`；后训练 `M` |
| Cosmos3-Edge | 4B | Jetson AGX Orin / Thor / RTX Pro 6000（"Edge deployment"） | Reasoner: RTX PRO 4500/6000 Blackwell Server；**Jetson AGX Thor T5000 128GB / AGX Thor T4000 64GB / Thor T3000 32GB / Thor T2000 16GB、Jetson AGX Orin 64GB**（Transformers eager，端到端 3.2–11 s） | —（COOKBOOK 未单列） |

- **OS/GPU 官方口径**（2026-11-05）："OS / GPU: Linux; Ampere, Hopper, Blackwell"；"Precision: BF16 tested"（github.com/NVIDIA/Cosmos docs/reference/models.md）。
- **FP8 官方量化检查点**：Cosmos3-Nano-FP8、Cosmos3-Super-FP8 有独立 benchmark 页（"The FP8 pages cover the quantized Nano and Super checkpoints"；NIM 行为 FP8）（docs/reference/inference_benchmarks.md）。注：models.md 生成设置表写 "BF16 tested"——指原生检查点的测试精度；FP8 是官方另行发布的量化产物，两者不矛盾。
- **逐模型显存 GB 数字**：官方未发布模型卡级显存表（已核验的 GitHub 文档均无）；第三方称 Nano ~32 GB BF16 权重、Super ~120–130 GB 需 2×H200 级张量并行——**UNVERIFIED**（HF 不可达，见 §7）。
- **既有材料引用**（去重纪律）：上一代 Cosmos-Predict2-2B-Video2World 官方模型卡写明 **需 32.54 GB 显存**（HF 模型卡，2026-10-27 查）——结论已在 [docs/17-research/2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) §0/§5 核验，本卡引用不复制。
- **软件栈**：`uv venv --python 3.13`；diffusers（git 版）+ torch + transformers + cosmos_guardrail；推理后端官方支持 "Diffusers, Transformers, vLLM, vLLM-Omni, SGLang, TensorRT-LLM, and NIM"；"First run downloads the 16B checkpoint; diffusion steps are compute-heavy, so long step times are normal"（github.com/NVIDIA/Cosmos README，2026-11-05）。
- **对 OmniWorld 开发机的结论**（推断，事实依据如上）：Windows + RTX 4060 Laptop 8 GiB ⇒ **Cosmos 3 全家族（含 Edge）官方不支持、不可本地跑**；若未来要用，取离线路线（§6）。

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

| 接口面 | 内容 | 来源 |
|---|---|---|
| 推理后端 | Diffusers、Transformers、vLLM、vLLM-Omni、SGLang、TensorRT-LLM、NIM（七种官方支持） | github.com/NVIDIA/Cosmos（README） |
| 托管 API | "Developers can try Cosmos 3 on build.nvidia.com"；"deploy the models as NVIDIA NIM™ microservices"；云推理伙伴 Baseten、CoreWeave、Microsoft Azure、Nebius、Deep Infra、Classmethod | nvidianews.nvidia.com（新闻稿） |
| 下载渠道 | "download open models from Hugging Face, customize models and generate synthetic data with Hugging Face Diffusers and resources on GitHub"（新闻稿）；HF collections：`nvidia/cosmos3`（基模）与 `nvidia/cosmos3-examples`（后训练检查点） | 同上 + github.com/NVIDIA/Cosmos（README） |
| 仿真控制信号 | Transfer 生成接受 edge、blur、depth、segmentation、wsm（world-scenario map：车道线+物体长方体）控制视频；多流逐相机视图 + LiDAR 流；Sim-Bimanual 接受 "per-frame actions and camera movement (JSON)" | github.com/NVIDIA/Cosmos（docs/reference/models.md） |
| 动作接口 | 动作条件 9D 相机/9D AV/57D egocentric/10D 单臂（DROID/UR/Fractal/Bridge/UMI）/20D 双臂/29D 人形（AgiBot）；动作输出为 JSON actions | 同上 |
| 资产格式 | 输入 JPG/PNG/JPEG/WEBP、MP4；输出 JPG、MP4、AAC-in-MP4（stereo 48 kHz）、JSON actions、text | 同上 |
| 护栏 | nvidia/Cosmos-1.0-Guardrail（HF 门控仓库，需申请访问；`cosmos_guardrail` 进快速开始安装清单） | github.com/NVIDIA/Cosmos（README） |
| NVIDIA 生态集成 | Omniverse："Developers can input Omniverse simulations as instructional videos into Cosmos Transfer models"；NuRec 神经场景重建；Isaac Lab（"Train policies for physical AI at scale"）；TAO 7 微调；NIM 生产部署 | nvidia.com/en-us/ai/cosmos |
| 后训练工具链 | NVIDIA/cosmos-framework（"Our inference and training framework to run on the Cosmos Models"，GitHub API 2026-11-05 核验）；License 经 LICENSE 文件全文核验为 **OpenMDW-1.1**（NVIDIA 版权头 + 完整许可文本；GitHub API 的 license 字段显示 NOASSERTION/"Other"——OpenMDW 未入其 SPDX 识别库，以文件全文为准）；COOKBOOK recipes：SFT/LoRA 后训练（GPU 预算 M）、Action policy 后训练并闭环评测（DROID/LIBERO/RoboCasa，预算 L/XL）、4-step 蒸馏（L） | github.com/NVIDIA/cosmos-framework（LICENSE 全文 + API）；github.com/NVIDIA/Cosmos（COOKBOOK.md） |
| HF Diffusers pipeline 文档 | pipeline 类名 **Cosmos3OmniPipeline** 已由官方 README Quickstart 代码直验：`from diffusers import Cosmos3OmniPipeline` → `Cosmos3OmniPipeline.from_pretrained("nvidia/Cosmos3-Nano", torch_dtype=torch.bfloat16, device_map="cuda")` → `pipe(prompt=...).video`；**文档页与参数细节 UNVERIFIED**：HF 域名在本环境不可达（§7） | github.com/NVIDIA/Cosmos（README Quickstart） |

## 6. 与 OmniWorld 的边界 + 族谱归宿

**主归宿：②适配器——离线数据生产者（在别处运行）**
理由：
1. **算力硬墙**（§4）：官方 OS 限 Linux、GPU 限 Ampere/Hopper/Blackwell，实测最小平台 Jetson Thor T2000 16 GB；本机 Windows + 8 GiB 连 Edge 都不在支持线内。这与既有结论一致——[交互闭环路线笔记 §0.5](../2026-10-27-interactive-closed-loop-route.md) 已裁定 "Cosmos 在本机不可跑，它只能是离线数据生产者"，该笔记的结论基于 Cosmos-Predict2（32.54 GB）；本卡核验表明 **Cosmos 3 没有放宽这个约束**（Edge 4B 是新出现的最低档，但仍远高于本机）。
2. **许可刚好允许**（§3）：OpenMDW-1.1 无用途限制、**输出不受限**（"does not impose any restrictions ... with respect to any outputs"）⇒ Cosmos 在别处生成的帧可以按 D1 纪律入库：**只进脚本 + 固定清单 + SHA256 + 许可记录，不入库数据本身**。分发 Cosmos 代码/权重本身（如果哪天需要）义务仅两项：保留协议副本 + 版权/来源声明。
3. **不进产品路径**：与闭环笔记 §8.3 "不让 Cosmos 进产品路径" 一致；生成式模型与"参考模式零外部依赖"承诺（ADR-0012）正交。

**副归宿：评测对象（Phase 4 的外部参照与候选被评对象）**
理由：
1. PLAN §三 已注册期权分支："评测交互式世界模型时的默认接入路径——'MLPerf 之于测量'"（[PLAN.md:50](../../PLAN.md#L50)，见闭环笔记 §2.3）——研究队列已在盯 cosmos-evaluator 与 WorldFoundry 两个信号源。Cosmos 3 是当前**开放权重 + 全工作流（含 forward dynamics：text/image/video/action→video）**的最大被评对象候选；Genie 3（DeepMind）与 Marble（World Labs）不开放权重，评不了。
2. **注意评测口径差异**（重要）：NVIDIA 自带的 Cosmos Evaluator 评的是 "world generation & reasoning outputs"（生成视频质量，PAI-Bench/Physics-IQ 口径）；OmniWorld 评的是"世界数据穿系统的质量"（状态主张校准 ECE/divergence、闭环延迟预算、信念误差）。**同题异解，不是竞品**——它的评测套件是我们对标与差异化的参照（Q3 输入）。
3. **接口缺口明确**（§2.2）：`WorldModelAdapter::predict(Tick, const WorldState&) → Proposal[]` 的形状装不下像素输出；Cosmos 读真值状态也违反闭环笔记 §4 的"世界模型必须读信念"裁定。所以"评 Cosmos"不是把塞进现有适配器，而是 Phase 4 的独立评测桥切片（先例：D3 的 `--annotations requires --frames` 护栏思路）。

**③技能项目（不适用）**：Cosmos 3 不是可嵌入仓库的技能/库——它是模型家族 + DGX 级工具链，无 C++/库形态的接缝。
**⑤明确让渡**：不 vendor 任何 Cosmos 代码/权重；不模仿 Framework/Curator/Evaluator 的平台形态（依赖 NVIDIA 算力生态）；不做 Cosmos 训练（PLAN §五 训练边界红线）。
**竞品参照**：Genie 3（闭源前沿交互式世界模型）、World Labs Marble/Atlas（商业 API）——Cosmos 是三者中唯一开放许可（OpenMDW-1.1）+ 可下载权重的，这是我们把它列为"评测对象"而非前两者的唯一原因（对比见全景报告第四节的表，引用不复制）。

## 7. 未解决问题 + 重查触发条件

| # | 未解决问题 | 状态与原因 | 重查触发条件 |
|---|---|---|---|
| 1 | HF 模型卡级显存数字（如 Nano BF16 权重 GB、官方最低显存声明） | **UNVERIFIED**：huggingface.co 在本环境全域不可达——WebFetch 六次超时（含 raw README 路径）；`curl -v` 确认 DNS 解析异常（huggingface.co → 199.16.158.12，连接超时）；GitHub raw 与 nvidia.com 正常，属 HF 域名级阻断 | 任何 HF 可达的环境（代理/CI）；或 NVIDIA/Cosmos 仓库未来把显存表收进 docs/reference |
| 2 | Cosmos Datasets / Physical AI Datasets 逐数据集的许可标签 | **UNVERIFIED**：同上，HF 数据集页不可达；GitHub 与官网均未声明数据集许可 | 想把 Cosmos 生成的帧**或**其数据集样本入库时（D1 纪律要求逐数据集许可记录，硬前置） |
| 3 | 技术报告内容（训练算力、MoT 配置、评测细节） | **已闭（2026-11-05 深度调研）**：`pdftotext -layout`（mingw64 poppler）全文解析成功（604,895 字符，原始素材 `.scratch/cosmos3-deep/tr.txt`）。一手验证：Generator 预训练 "Cosmos3-Nano was trained on 31.05T tokens using 1024 NVIDIA GB200 GPUs, while Cosmos3-Super was trained on 17.86T tokens using 2048 NVIDIA GB200 GPUs"（技术报告 §4.2.1）；mid-training Nano 2.4T / Super 1.9T tokens（§4.2.2）；架构参数与机制详解见深度报告 §2 | 训练算力论证直接引技术报告 §4.2.1-4.2.2；深度原理问题读 [deep/2026-11-05-cosmos-3.md](deep/2026-11-05-cosmos-3.md) |
| 4 | HF Diffusers Cosmos3 pipeline 的文档与参数细节 | **UNVERIFIED**：HF 文档域不可达（GitHub raw 侧无对应文档）；pipeline 类名 Cosmos3OmniPipeline 已由官方 README Quickstart 直验（§5），未验的只是文档页与参数 | 写评测桥 spike 前；或 Diffusers GitHub 仓库里读 cosmos3 实现源码 |
| 5 | Cosmos3-Edge 模型卡的逐项许可字段（推断适用官网统一的 OpenMDW-1.1，未在 HF 页面直验） | 低风险（官网级声明覆盖 "Cosmos WFMs" 全家族） | 若 Edge 成为唯一候选运行目标时 |
| 6 | 旧代权重的 HF 模型卡许可（cosmos-predict2/transfer2.5 等） | **代码许可已闭**：cosmos-predict2 与 cosmos-transfer2.5 均在旧组织 nvidia-cosmos 下且为 Apache-2.0（GitHub API 2026-11-05 全量核验，§3 历史层）；**权重许可未闭**：旧代权重的 HF 模型卡许可仍需逐仓库直验（HF 不可达） | 引用旧代检查点或其产物入库前 |

**全局重查触发**：① Phase 4（世界模型/智能体应用）启动；② OpenMDW 出 1.2 或 NVIDIA 变更许可；③ 出现 ≥96 GB VRAM 的可访问 GPU 环境；④ HF 域名恢复可达（1/2/4/5 号问题一次性清账）。

## 8. 来源清单

**官方一手来源（全部 2026-11-05 检索）**：

1. https://nvidianews.nvidia.com/news/nvidia-launches-cosmos-3-the-open-frontier-foundation-model-for-physical-ai —— NVIDIA 新闻稿（发布日期/用途/变体/渠道/联盟）
2. https://www.nvidia.com/en-us/ai/cosmos/ —— NVIDIA 官网 Cosmos 产品页（定位/组件/硬件/许可声明/Omniverse 集成/数据集条目链接指向 HF collections；2026-11-05 以 HTML 直抓复核引文原句）
3. https://github.com/NVIDIA/Cosmos （经 https://raw.githubusercontent.com/NVIDIA/Cosmos/main/README.md 抓取）—— README（家族构成/双塔/硬件表/工具链/快速开始）
4. https://raw.githubusercontent.com/NVIDIA/Cosmos/main/LICENSE —— OpenMDW-1.1 完整许可文本
5. https://raw.githubusercontent.com/NVIDIA/Cosmos/main/docs/reference/models.md —— 模型参考（I/O 矩阵/动作维度/生成设置/Sim 检查点/Limitations 原文）
6. https://raw.githubusercontent.com/NVIDIA/Cosmos/main/docs/reference/inference_benchmarks.md —— 推理基准（各 GPU 实测平台/速度数据/FP8 检查点）
7. https://raw.githubusercontent.com/NVIDIA/Cosmos/main/COOKBOOK.md —— recipes 与 GPU 预算分档（S/M/L/XL）、评测套件清单
8. https://raw.githubusercontent.com/NVIDIA/cosmos-framework/main/LICENSE —— cosmos-framework 许可全文（OpenMDW-1.1，含 NVIDIA 版权头）
9. https://api.github.com/repos/NVIDIA/cosmos-framework —— 仓库元数据（"Our inference and training framework to run on the Cosmos Models"）
10. https://github.com/nvidia-cosmos —— 旧组织页（迁移公告 "The Cosmos project is now maintained at: 👉 https://github.com/NVIDIA/Cosmos"）
    - https://api.github.com/orgs/nvidia-cosmos/repos?per_page=100 —— 旧组织全量仓库清单 API（12 仓库全部 Apache-2.0，含 cosmos-predict2/cosmos-transfer2.5；HTML 页面为前端置顶显示，全量以 API 为准）
11. https://openmdw.ai/license/1-1 —— OpenMDW-1.1 官方许可文本页（与 #4 逐条一致）
12. https://www.linuxfoundation.org/press/linux-foundation-releases-openmdw-1.1-nvidia-adopts-openmdw-for-cosmos-isaac-gr00t-ising-and-nemotron-ai-model-families —— LF 新闻稿（2026-05-28 发布 1.1；NVIDIA 采纳范围）
13. https://research.nvidia.com/labs/cosmos-lab/cosmos3/technical-report.pdf —— 技术报告（**存在性已核验**：HTTP 200，29,331,015 字节；内容未解析，见 §7-3）

**既有仓库材料（引用不复制）**：

14. [docs/17-research/2026-10-27-interactive-closed-loop-route.md](../2026-10-27-interactive-closed-loop-route.md) §0/§5/§8 —— Cosmos-Predict2-2B 需 32.54 GB 显存（HF 模型卡，2026-10-27 查）、NVIDIA Open Model License 分档、"离线数据生产者"裁定
15. [docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md](../2026年实时世界运行时与生成式世界模型技术全景报告.md) —— Cosmos 3 一节（MoT/动作维度/Predict 2.5·Transfer 2.5·Reason 2 家族关系；二手汇编，关键数字经 #5 官方复核一致后采用）

**UNVERIFIED 相关（尝试但不可达，2026-11-05）**：

16. https://huggingface.co/nvidia/Cosmos3-Nano —— 六次访问超时（WebFetch + curl -v 诊断：DNS 解析异常、TCP 超时；§7-1/2/4/5 的 UNVERIFIED 均因此）
17. https://huggingface.co/collections/nvidia/cosmos3 、 https://huggingface.co/docs/diffusers/en/api/pipelines/cosmos3 —— 同上不可达

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-cosmos-3.md](deep/2026-11-05-cosmos-3.md)
