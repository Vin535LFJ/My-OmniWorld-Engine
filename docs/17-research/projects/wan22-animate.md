# Wan2.2-Animate 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建（同日按独立核查修订两处措辞，见文末修订记录）
> 路线级上下文（三层拆解 / 算力账 / SMPL-X 许可红线 / 运动数据层 / P2 决定性实验）见 [2026-10-27-character-animation-route.md](../2026-10-27-character-animation-route.md)，本卡只引用不复制。

## 0. 一句话

阿里 Wan 团队的开源角色动画模型：给定**角色图 + 参考视频**，一个模型统一完成"角色动画"（让图里的角色复现视频中的动作与表情）与"角色替换"（把视频中的人物换成角色图中的角色，并复现场景光照与色调）两个任务；代码与权重均 Apache-2.0。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
|---|---|
| 2025-09-19 发布 Wan2.2-Animate-14B："an unified model for character animation and replacement with holistic movement and expression replication"，同时发布权重与推理代码，提供 wan.video / ModelScope Studio / HuggingFace Space 在线试用 | github.com/Wan-Video/Wan2.2（README News）；huggingface.co/Wan-AI/Wan2.2-Animate-14B |
| 两个任务同一模型：animation 模式 = 模型生成角色图的视频，复现输入视频中的人类动作；replacement 模式 = "The model replaces the character image with the input video"（把输入视频中的人物替换为角色图角色） | github.com/Wan-Video/Wan2.2（Run Wan-Animate）；huggingface.co/Wan-AI/Wan2.2-Animate-14B |
| 输入输出（官方 CLI 口径）：输入 = 参考视频 + 角色图，先经 `wan/modules/animate/preprocess/preprocess_data.py` 预处理（动画模式加 `--retarget_flag --use_flux`，替换模式加 `--replace_flag`），再以 `generate.py --task animate-14B` 推理；替换模式推理加 `--replace_flag --use_relighting_lora` | github.com/Wan-Video/Wan2.2（Preprocessing / Run in animation mode / Run in replacement mode） |
| 输入输出（diffusers 管线口径）：`WanAnimatePipeline(image, pose_video, face_video, background_video?, mask_video?, prompt?, …)`，`mode="animate"`（默认）/`"replace"`；替换模式额外要求背景视频与掩码视频（黑=保留原内容、白=生成新角色）；输出为视频帧序列（`WanPipelineOutput.frames`） | huggingface.co/docs/diffusers/main/api/pipelines/wan（WanAnimatePipeline） |
| 技术声称（论文摘要）：统一框架；修改输入范式以区分"参考条件"与"生成区域"；用空间对齐的骨架信号复现身体动作、用源图提取的隐式面部特征重演表情；为替换模式开发辅助 Relighting LoRA，在保持角色外观一致的同时施加环境光照与色调；声称 SOTA | arxiv.org/abs/2509.14055（Wan-Animate: Unified Character Animation and Replacement with Holistic Replication） |
| **变体与参数量：Animate 只有 14B 一个变体**。官方模型下载表列出 Wan2.2 家族 = T2V-A14B / I2V-A14B / TI2V-5B / S2V-14B / Animate-14B 五个；**不存在 Animate-5B**——5B 是家族内 TI2V（高压缩 VAE，T2V+I2V）的参数量，与 Animate 无关 | github.com/Wan-Video/Wan2.2（Model Download 表） |
| 2026-08-07 发布后继 **Wan-Animate-2**（`Wan-AI/Wan2.2-Animate-2-14B`）：端到端直接消费驱动视频、"eliminating intermediate motion extractors"（不再需要外部预处理）；新增文本驱动视角控制；含 Base 与 Distillation 两套权重；另有 Wan-Animate-2-Lite（"reduces inference latency to real-time thresholds for streaming"） | huggingface.co/Wan-AI/Wan2.2-Animate-2-14B |
| 推断（非官方声明）：Animate-14B 官方文档未标注其内部是 MoE 还是 dense；diffusers 中 `WanAnimatePipeline` 只有单个 `transformer`（无 `transformer_2`），而 Wan2.2 的 MoE 双专家管线（WanPipeline 等）有 `transformer_2`——形态上更像单模型 | huggingface.co/docs/diffusers/main/api/pipelines/wan（类签名对比） |

## 2. 边界与限制

**官方承认的限制：**

- **输入不能是原始视频**：diffusers 文档明文 "Raw videos should not be used for inputs such as `pose_video`, which the pipeline expects to be preprocessed"；姿态/面部预处理脚本在原仓库，diffusers 侧 "Integration of these preprocessing steps into Diffusers is planned for a future release"（截至核验日尚未集成）。【huggingface.co/docs/diffusers/main/api/pipelines/wan】
- **不要搭配 Wan2.2 的 LoRA**：README 原文 "If you're using **Wan-Animate**, we do not recommend using LoRA models trained on `Wan2.2`, since weight changes during training may lead to unexpected behavior."【github.com/Wan-Video/Wan2.2】
- 官方未发布任何**量化**权重或低显存官方方案；低显存能力全部来自生态实现（见 §4）。【github.com/Wan-Video/Wan2.2；huggingface.co/Wan-AI/Wan2.2-Animate-14B】
- 生成内容的合规责任完全在使用者（"You are fully accountable for your use of the models…"），托管 API 侧输入输出均过内容审核（`IPInfringementSuspect` / `DataInspectionFailed`）。【huggingface.co/Wan-AI/Wan2.2-Animate-14B；alibabacloud.com/help/en/model-studio/wan-animate-move-api】

**我们的推断（不混入上栏）：**

- 生成式逐帧扩散模型 ⇒ **帧级确定性不成立**、单段延迟以分钟计，与引擎"位级确定性、没有 benchmark 不宣称性能"的纪律天然冲突；它只能作为**进程外的生成后端/评测对象**，不可能进 Grounding Loop。
- 官方无 Animate 专项显存数字（见 §4），但 14B 量级 + A14B 家族锚点 ⇒ **本机 4060 Laptop 8 GiB 不可行**，与路线笔记 §3/§4 的判断一致（算力 100% 花在 P2、租卡或托管）。
- 质量（身份一致性 / 姿态跟随 / 抖动）官方只给 SOTA 声称、未给可复现指标口径 ⇒ 沿用路线笔记 §7 的自测验收（PCK/MPJPE、身份余弦、时序抖动）。
- 后继 Wan-Animate-2 已"免预处理、蒸馏 10 步、Lite 实时流式"⇒ Wan2.2-Animate-14B 作为选型对象存在**代际贬值风险**（见 §7）。

## 3. 许可（代码 / 权重 / 数据 三分）

| 对象 | 条款 | 核验依据 |
|---|---|---|
| **代码**（github.com/Wan-Video/Wan2.2） | **Apache License 2.0**。已核验 LICENSE.txt 原文开头："Apache License / Version 2.0, January 2004" | raw.githubusercontent.com/Wan-Video/Wan2.2/main/LICENSE.txt（检索 2026-11-05） |
| **权重**（Wan-AI/Wan2.2-Animate-14B 及 -Diffusers） | **Apache 2.0**。模型卡 License Agreement 原文："The models in this repository are licensed under the Apache 2.0 License. We claim no rights over the your generated contents, granting you the freedom to use them while ensuring that your usage complies with the provisions of this license."（含生成内容不主张权利 + 使用者全责条款）。随仓库分发的 Relighting LoRA 同属该仓库发布物 | huggingface.co/Wan-AI/Wan2.2-Animate-14B（License Agreement 节，检索 2026-11-05） |
| **数据** | **官方未公开训练数据**，无数据许可可谈；模型卡只给规模声称（相对 Wan2.1 图像 +65.6%、视频 +83.2%）。**UNVERIFIED（不适用）**：不存在可引用的数据许可文本 | huggingface.co/Wan-AI/Wan2.2-Animate-14B（Introduction 节） |
| 后继 Wan-Animate-2 权重与代码 | 模型卡 badge 与 License 节均为 Apache 2.0（"This project is licensed under the Apache License 2.0."） | huggingface.co/Wan-AI/Wan2.2-Animate-2-14B（检索 2026-11-05） |
| **未核验项** | 官方预处理栈（`process_checkpoint` 内的姿态/人脸等检测器）的第三方组件许可**未逐项核验**——路线笔记 §5 的"仓库协议 ≠ 权重协议、必须逐个查 model card"纪律在此适用 | github.com/Wan-Video/Wan2.2（UserGuider 未逐项读取） |

> 与路线笔记 §5 的关系：该笔记把"Wan2.2 代码与权重"列为"Apache-2.0 可用"，本卡完成逐项核验，确认该结论；SMPL-X 红线不涉及本项目（Animate 不依赖 SMPL-X 网格）。

## 4. 硬件要求（显存/驱动/OS）

- **官方没有 Animate 专项显存数字**（两处独立核验）：
  1. 官方"Computational Efficiency"表（图片已下载读取）只有 TI2V-5B 与 T2V/I2V-A14B 行，**无 Animate 行**；【huggingface.co/Wan-AI/Wan2.2-Animate-14B → assets/comp_effic.png】
  2. README 中 Animate 的单卡/多卡命令**不带任何 VRAM 注记**，而 T2V/I2V 命令注明 "This command can run on a GPU with at least 80GB VRAM"、TI2V-5B 注明 "at least 24GB VRAM (e.g, RTX 4090 GPU)"——这两句**不适用于 Animate**。【raw.githubusercontent.com/Wan-Video/Wan2.2/main/README.md 行 150/234/271，行 337–392 无注记】
- **同族官方锚点**（效率表，Time/峰值显存；单卡数字已开 offload，多卡列为 FSDP 分片——官方参数说明原文见 §8 来源 2 行 435）：A14B 家族单卡 480P 峰值 **41.0–41.3 GB**、720P 峰值 **59.7–59.8 GB**（H20/A100/H100）；TI2V-5B 在 4090 上 720P 峰值 **~22.9 GB**。Animate 为 14B 级 ⇒ 只能据此**推断**其量级在数十 GB，非官方数字。**UNVERIFIED（Animate 专项）**：来源不可得——官方从未发布。
- **后继版本官方口径**（Wan-Animate-2 卡）："The default settings in this repository are tuned for **8× A800 GPUs** and support 720P video generation. We have also tested 480P generation on **2× A800 GPUs**."——佐证 Animate 系列是数据中心级工作负载（该声明属 2.x，对 1.0 仅作量级参照）。【huggingface.co/Wan-AI/Wan2.2-Animate-2-14B】
- **量化方案**：官方不发布量化权重；生态侧已核验的路径是 **DiffSynth-Studio**（ModelScope 社区团队官方项目，Wan 官方模型卡 Community Works 致谢）——其 README 记录 2025-07-28 起 "low VRAM layer-by-layer offload, **FP8 quantization**, sequence parallelism, LoRA training, full training"，支持表中含 `Wan-AI/Wan2.2-Animate-14B`（inference + low VRAM inference）与 `Wan-Animate-2-14B`（含 Distilled）；2026-08-19 起提供统一 `QuantizeConfig`（bitsandbytes / torchao / comfy-kitchen 后端，NF4/INT8 等）。**社区 GGUF/FP8 单文件量化构建广泛存在但本次未核验具体仓库，不作依据。**【github.com/modelscope/DiffSynth-Studio；huggingface.co/Wan-AI/Wan2.2-Animate-14B】
- **驱动/OS**：官方 requirements 仅约束 `torch >= 2.4.0`（另需 flash_attn，多卡需 FSDP+DeepSpeed Ulysses）；**官方无 Windows 支持声明**，日常生态默认 Linux + CUDA。**UNVERIFIED（Windows 可行性）**：官方文档未提及。多卡要求：README 给出 `torch.distributed.run --nproc_per_node 8`（FSDP + Ulysses）示例。【github.com/Wan-Video/Wan2.2】

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

| 接口 | 形态 | 核验要点 | 来源 |
|---|---|---|---|
| **官方 CLI** | `generate.py --task animate-14B`；单卡或 `torch.distributed.run`（FSDP + Ulysses）多卡 | 输入是预处理产物目录 `src_root_path`（pose/face/… 视频），替换模式加 `--replace_flag --use_relighting_lora` | github.com/Wan-Video/Wan2.2 |
| **Diffusers** | `WanAnimatePipeline`（2025-11-13 集成，PR #12526；checkpoint `Wan-AI/Wan2.2-Animate-14B-Diffusers`） | `mode="animate"/"replace"`；`segment_frame_length=77`（分段长度）；`prev_segment_conditioning_frames`（1 或 5，5 更连贯但更耗显存）；CFG 默认关（`guidance_scale=1.0`），可开以支持负向提示与表情控制；支持 `load_lora_weights()`。注意：**预处理不在 diffusers 内**，需原仓库脚本 | huggingface.co/docs/diffusers/main/api/pipelines/wan；github.com/Wan-Video/Wan2.2 |
| **ComfyUI（原生）** | 2025-09-23 官方博客宣布原生支持（需 ComfyUI v0.3.60+，workflow 走模板 JSON，替换工作流引用 Relighting LoRA 文件）；Kijai WanVideoWrapper 为官方模型卡致谢的替代实现 | 公告为功能发布性质（原文开头："Hi community! We're excited to announce that WAN2.2 Animate & Qwen-Edit 2509 are now natively supported in ComfyUI!"），未给显存/节点技术细节 | blog.comfy.org/p/wan22-animate-and-qwen-image-edit-2509；huggingface.co/Wan-AI/Wan2.2-Animate-14B |
| **托管 API（阿里云百炼 Model Studio）** | `wan2.2-animate-move`（animate 任务）与 `wan2.2-animate-mix`（replace 任务），两个独立 endpoint | 仅异步（`X-DashScope-Async: enable`）；`parameters.mode` 必填 `wan-std`（更快更便宜）/ `wan-pro`（更平滑更高质量）；输入 = 公网 `image_url` + `video_url`；限制：图 200–4096px、≤5 MB（JPG/JPEG/PNG/BMP/WEBP），视频 200–2048px、≤200 MB、**时长 2–30 秒**（MP4/AVI/MOV）；输出 = MP4(H.264) `video_url`，**任务 ID 与结果 URL 24 小时有效**；按**输出视频秒数**计费、失败不计费 | alibabacloud.com/help/en/model-studio/wan-animate-move-api；alibabacloud.com/help/en/model-studio/wan-animate-mix-api |
| **在线 demo** | wan.video、ModelScope Studio、HuggingFace Space | 官方发布公告列明 | github.com/Wan-Video/Wan2.2 |
| **资产格式** | 输入：角色图 + 参考视频 → 预处理产物（pose 视频 / face 视频；替换再加 background / mask 视频）；输出：MP4（H.264，托管侧）或帧序列（diffusers 侧 `export_to_video`） | mask 语义：黑=保留原内容、白=生成新角色 | huggingface.co/docs/diffusers/main/api/pipelines/wan；alibabacloud.com/help/en/model-studio/wan-animate-move-api |

## 6. 与 OmniWorld 的边界 + 族谱归宿

**结论：②适配器候选（生成式表现后端）成立，但现阶段是"外部参照/评测对象"，转正为适配器需侧项目立项 + 另立 ADR。**

理由：

1. **形态匹配**：Animate 是"视频→视频"生成后端，其控制面（pose/face/mask）恰好消费路线笔记 §6"运动数据层"的产物——运动数据层是公共资产，Animate 是它最典型的下游消费者之一。这与地图中 OpenVLA→"Brain adapter" 的"观察/动作接口"定位同构：我们研究的是**接口契约**，不是模型本身。
2. **许可无硬门**：代码与权重双 Apache-2.0（§3），是路线笔记 §5 各候选中少数许可全绿的生成层组件（对照：MusePose/MimicMotion 权重协议未查清前不得进可交付路径）。若侧项目商用，SMPL-X 红线不适用于此路线 C。
3. **引擎边界清晰**：引擎核心（World Contract / Grounding Loop / GPU 数据流）不生成视频；AGENTS.md 的"不碰"清单仍然有效。Animate 的任何接入都发生在**侧项目**（视频→动作→角色→成片），不在阶段 D 路线上——与路线笔记 §8"不在本阶段、接入需另立 ADR"一致。
4. **评测对象的用法**：托管 API（`wan2.2-animate-move`/`-mix`，按输出秒计费、2–30 秒片段）是 P2"决定性实验"的低成本替代路径——免 8×A800、免 Kaggle 权重搬运，且 wan-std/wan-pro 两档天然构成质量/成本对照实验。
5. **不复制清单**：权重、预处理栈、生成管线代码一律不进引擎；ComfyUI/diffusers 实现不迁移；其"统一符号表示"论文概念只作参照阅读。

**竞品参照**（同为 P2 候选、许可待逐项核验）：MimicMotion、MusePose（见路线笔记 §1/§5）；本卡不展开。

## 7. 未解决问题 + 重查触发条件

**未解决问题：**

1. **Animate-14B 的官方显存数字缺失**（§4 两处核验均无）——决定"租卡 vs 托管 API"前需在目标卡上实测，或直接用托管 API 绕开。
2. **代际问题**：Wan-Animate-2（2026-08-07，端到端免预处理、蒸馏 10 步、Lite 实时流式）大概率取代 Wan2.2-Animate 成为 P2 主选；本卡首建时对 2.x 只做了定位级核验（许可/发布内容/硬件默认配置），**输入输出契约、diffusers 集成质量（PR #14412）、社区量化生态均未逐项核验**。其模型卡引用块的 arXiv 号显示为占位符（badge 写 2608.06009、引用写 "TODO.06009"，互相矛盾）——**UNVERIFIED**，论文号待其正式挂出后确认。
3. **预处理栈的第三方组件许可**（姿态/人脸检测器权重）未逐项核验（§3）。
4. **托管 API 单价**未核验：API 页只说"按输出秒计费"并链接到独立计价页，价格数字未取。**UNVERIFIED**。
5. 官方 README 提示"若使用 Wan-Animate 不建议配 Wan2.2 训练的 LoRA"——加速 LoRA（如 LightX2V 系）能否安全用于 Animate 属同类问题，官方未给答案。

**重查触发条件：**

- 侧项目 P2 实验启动前（模型选型 + 显存实测 + 许可终审）——必然触发 1/3/4；
- 决定采用 Wan-Animate-2 时——对 2.x 做一次与本品同规格的逐项核验（或直接为它建新卡 `wan-animate-2`）；
- Wan-Animate-2 的 Diffusers/ComfyUI 集成进入正式 release 或其预处理彻底消失（影响接口层结论 §5）；
- 任一许可条款变更（Apache-2.0 → 附加条款/改名）或权重仓被下架/替换；
- 决定商用或对外分发产物时——重读生成内容合规条款与托管 API 审核规则（§3/§2）。

## 8. 来源清单（检索日期均为 2026-11-05）

| # | 来源 | URL |
|---|---|---|
| 1 | Wan2.2 官方仓库 README（News / Model Download / Run Wan-Animate / 效率表参数说明） | https://github.com/Wan-Video/Wan2.2 |
| 2 | 同仓库 README 原文（80GB/24GB 注记行、Nov 13 2025 diffusers 集成行、LoRA 警告行） | https://raw.githubusercontent.com/Wan-Video/Wan2.2/main/README.md |
| 3 | 同仓库 LICENSE.txt（Apache License 2.0 原文） | https://raw.githubusercontent.com/Wan-Video/Wan2.2/main/LICENSE.txt |
| 4 | Wan-AI/Wan2.2-Animate-14B 模型卡（能力声称 / License Agreement / Community Works / 效率表） | https://huggingface.co/Wan-AI/Wan2.2-Animate-14B |
| 5 | 官方效率表图片（comp_effic.png，本次下载并逐行读取，无 Animate 行） | https://huggingface.co/Wan-AI/Wan2.2-Animate-14B/resolve/main/assets/comp_effic.png |
| 6 | Diffusers Wan 管线文档（WanAnimatePipeline 签名 / 两模式 / 预处理要求 / Key Parameters） | https://huggingface.co/docs/diffusers/main/api/pipelines/wan |
| 7 | Wan-Animate 论文（arXiv:2509.14055，摘要原文） | https://arxiv.org/abs/2509.14055 |
| 8 | ComfyUI 官方博客：Wan2.2 Animate & Qwen-Image-Edit 2509 原生支持（2025-09-23） | https://blog.comfy.org/p/wan22-animate-and-qwen-image-edit-2509 |
| 9 | ComfyUI 官方博客：Wan Animate 2 上线（2026-08-08） | https://blog.comfy.org/p/wan-animate-2-is-now-available-in |
| 10 | Wan-AI/Wan2.2-Animate-2-14B 模型卡（发布记录 / 端到端声称 / 8×A800 默认 / Apache-2.0 / Diffusers 集成） | https://huggingface.co/Wan-AI/Wan2.2-Animate-2-14B |
| 11 | 阿里云百炼 Model Studio：wan2.2-animate-move API 参考 | https://www.alibabacloud.com/help/en/model-studio/wan-animate-move-api |
| 12 | 阿里云百炼 Model Studio：wan2.2-animate-mix API 参考 | https://www.alibabacloud.com/help/en/model-studio/wan-animate-mix-api |
| 13 | DiffSynth-Studio README（FP8/量化/低显存 offload 更新记录；Wan 支持表含 Wan2.2-Animate-14B 与 Animate-2-14B） | https://github.com/modelscope/DiffSynth-Studio |

**检索方法备注**：所有 HF 页面经网页抓取工具取得正文；效率表图片经 `curl`（raw.githubusercontent.com）下载后逐行读取；README 关键行经 `grep -n` 核验（80GB=行 150/271、24GB=行 234、Animate 命令=行 337–392 无 VRAM 注记）。arXiv:2509.00836 曾被误指向本项目，实际为无关论文（机械臂运动规划），已排除。

## 修订记录

- **2026-11-05（独立核查修订，两处均与一手来源核实后照改）**：
  1. §4 同族锚点括注：原"均已开 offload"失实——官方参数说明（README"Computational Efficiency"节）14B 仅单卡列开 `--offload_model True --convert_model_dtype`，多卡列为 FSDP 分片（`--ulysses_size 4/8 --dit_fsdp --t5_fsdp`，无 offload）；卡内引用的锚点数字均取自单卡列，数值不变。
  2. §5 ComfyUI 行：删除"按其自述是 'high-level feature announcement'"——博客原文逐词不含 "high-level"，该措辞系抓取摘要器的转述被误标为博客自述；改为引用博客开头原句并保留实质结论（公告为功能发布性质，未给显存/节点技术细节）。

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-wan22-animate.md](deep/2026-11-05-wan22-animate.md)
