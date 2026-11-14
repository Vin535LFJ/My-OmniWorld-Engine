# SAM 2 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建（2026-11-05 独立核查修订 3 处：§3 评估代码许可链接、§5 安装形态/PyPI、§5 行号引用）

## 0. 一句话

Meta FAIR 开源的**可提示视觉分割基础模型**（图像+视频统一，流式记忆架构，实时逐帧传播 masklet）——在我们生态里的角色候选是 **②适配器：真实视频半边的"分割观测生产者"**（Apache-2.0 代码与权重，无许可摩擦，但无官方 ONNX/C++ 路径，只能进程外接入）。

## 1. 是什么 + 官方能力声称

官方定位：**"a foundation model towards solving promptable visual segmentation in images and videos"**——图像被视为单帧视频，图像与视频用同一个模型；博客称其为"第一个图像与视频统一的实时可提示对象分割模型"。任务定义（PVS）：在**任意帧**上给提示（正/负点击、框、掩码）→ 当帧立即出掩码 → 自动向全视频传播得到 **masklet**（时空掩码）→ 可在任意帧继续补提示修正。

| 事实 | 来源 |
| --- | --- |
| 流式记忆架构："a simple transformer architecture with streaming memory for real-time video processing"；视频逐帧进 image encoder，与过往目标记忆做 cross-attention | arxiv.org/abs/2408.00714（摘要、§3、Fig.3） |
| 记忆库构成：FIFO 至多 N 个近期帧记忆 + 至多 M 个提示帧记忆（空间特征图），外加 object pointers（轻量向量，存高层语义）；近期帧记忆带时间位置编码 | arxiv.org/html/2408.00714v2（§Memory bank） |
| 记忆库默认尺寸：`num_maskmem=7`（源码注释"1 input frame + 6 previous frames"）、`max_obj_ptrs_in_encoder=16` | github.com/facebookresearch/sam2 → `sam2/modeling/sam2_base.py:28,67` |
| 遮挡头（occlusion head）：额外输出头预测目标当前帧是否可见——视频允许"无有效对象"的输出模式 | arxiv.org/html/2408.00714v2（§结果）；ai.meta.com/blog/segment-anything-2 |
| 歧义处理：每帧预测多个候选掩码；后续提示未消歧时，只传播预测 IoU 最高者 | arxiv.org/html/2408.00714v2（§decoder）；ai.meta.com/blog/segment-anything-2 |
| 模型尺寸（SAM 2.1，2024-09-29/30 发布；README 内两处日期相差一天，均为官方原文）：tiny 38.9M / small 46M / base+ 80.8M / large 224.4M 参数 | github.com/facebookresearch/sam2 → README §Model Description |
| 官方测速（A100，torch 2.5.1 + cuda 12.4，全模型编译）：91.2 / 84.8 / 64.1 / 39.5 FPS（tiny→large）；博客口径"约 44 FPS，感觉实时" | github.com/facebookresearch/sam2 → README；ai.meta.com/blog/segment-anything-2 |
| 质量：SA-V test J&F 76.5（tiny）→ 79.5（large，SAM 2.1）；比 SAM 少约 3× 交互，图像分割比 SAM 更准且快 6× | github.com/facebookresearch/sam2 → README；arxiv.org/abs/2408.00714（摘要） |
| 多对象：可同时跟踪多对象；2024-12-11 更新后逐对象**独立推理**（"如同为每个对象开独立 session"、共享 backbone 特征），允许跟踪开始后追加新对象 | github.com/facebookresearch/sam2 → RELEASE_NOTES.md（12/11/2024 条目） |
| SA-V 数据集：51K 视频、643K masklet、覆盖 47 国；标注不施加语义约束（整对象与部件均标） | github.com/facebookresearch/sam2 → `sav_dataset/README.md:5`；ai.meta.com/blog/segment-anything-2 |
| 官方建议用途（节选）：视频数据标注提效、生成视频模型的可控性输入、机器人/AR 的实时对象分割 | ai.meta.com/blog/segment-anything-2 |

## 2. 边界与限制

### 2.1 官方承认的限制（论文 Appendix C + 博客 Limitations 节）

- **镜头切换**处可能分割失败；**拥挤场景、长时间遮挡、超长视频**中可能跟丢或混淆对象；官方缓解手段是**任意帧补提示**（交互修正，非自动恢复）。
- **细结构 + 快速运动**的对象跟踪不准（如骑手视频）。
- **外观相似的邻近对象**会混淆（例：多个相同的杂耍球）。
- **多对象逐对象独立处理**、仅共享逐帧 embedding、无对象间通信——多对象时效率"下降明显"。
- 帧间预测可能**不稳定（抖动）**：训练未惩罚时间抖动，**时间平滑性无保证**（博客原话）。
- 输出**无语义标签**：SA-V 明确"不施加语义约束"，masklet 不带类别。

### 2.2 我们的推断（不与上文混写）

- **输出语义 = 掩码 ≠ 身份跟踪**：`obj_id` 是**调用方管理的 client-side id**（源码注释直说 "mapping between client-side object id and model-side object index"，`sam2/sam2_video_predictor.py:84`）；遮挡头只回答"目标在不在"，模型侧没有检测/再识别机制——对象消失再出现不会自动重新关联。注意：**"无再识别"是我们对官方材料的综合推断**，官方没有用这个措辞（其局限清单里"跟丢/混淆"与遮挡头语义共同支持它）。见 `#unverified`。
- **identity 是外部赋予的，不是世界原生的**：SAM 2 只对被提示过的对象维护记忆，对"世界里有哪些对象"不做任何主张。这与 World Contract 的 identity/ownership/confidence 视角正交——分割观测入库时，identity 必须由引擎侧分配并保持稳定；SAM 2 的跟丢事件应转化为引擎侧 confidence 衰减/观测缺失，而不是身份切换。这使它成为演示"为什么 identity 归引擎管"的现成案例。
- **显存行为**：官方未公布每模型显存数字（见 §4）。从代码看：`init_state` 一次性加载整段视频帧（`sam2_video_predictor.py:51-59`），逐帧输出按对象存放在 `storage_device`（`offload_state_to_cpu` 时为 CPU RAM，否则为 GPU）——显存压力主要来自**帧序列预载 + 特征/输出缓存**，即"随帧数增长"（owner 内部笔记 `2026-10-27-character-animation-route.md` 的判断与此一致，但那是内部判断，非官方数据）。

## 3. 许可（代码 / 权重 / 数据 三分）

| 部分 | 许可 | 证据 |
| --- | --- | --- |
| **代码**（sam2 主体 + demo 前后端 + 训练代码） | **Apache-2.0** | 仓库 `LICENSE` 文件逐字为 Apache License 2.0 全文（已下载核验）；README §License："The SAM 2 model checkpoints, SAM 2 demo code (front-end and back-end), and SAM 2 training code are licensed under Apache 2.0" |
| **权重**（SAM 2 与 SAM 2.1 全部四个 checkpoint） | **Apache-2.0** | 同上 README §License；官方博客："we're sharing the code and model weights with a permissive Apache 2.0 license"；HF 模型卡元数据 `License: apache-2.0`（交叉验证） |
| **数据**（SA-V 视频与标注） | **CC BY 4.0** | `sav_dataset/README.md:162`："The videos and annotations in SA-V Dataset are released under CC BY 4.0"；官方博客同 |
| 评估代码（SA-V 评测） | BSD-3 | `sav_dataset/README.md:162`："The evaluation code is licensed under the BSD 3 license"；该行链接的 `sav_dataset/LICENSE` 已下载核对——全文即 "BSD License / For SAM 2 Eval software / Copyright (c) Meta Platforms" 三条款 BSD 正文，与 README 表述一致（2026-11-05 核验）；官方博客同为 BSD-3 |
| demo 内嵌字体（Inter / Noto Color Emoji） | SIL OFL 1.1 | README §License |
| 第三方组件 | cc_torch（连通域后处理，`LICENSE_cctorch`）；SA-V 评测改编自 VOS-Benchmark / DAVIS | README §Contributors；`sav_dataset/README.md:164` |

结论：**代码与权重均为 Apache-2.0，商用/闭源集成无障碍**（对比：初代 SAM 权重用自定义 SAM License——同一项目族内许可可以逐代变化，重查触发见 §7）。

## 4. 硬件要求（显存/驱动/OS）

| 项 | 官方口径 | 来源 |
| --- | --- | --- |
| OS | **Linux**（INSTALL.md Requirements 首行）；**Windows 官方强烈建议 WSL2 + Ubuntu**——Windows 原生不在官方支持路径内 | github.com/facebookresearch/sam2 → INSTALL.md:5,8；README |
| Python / 框架 | Python ≥ 3.10；PyTorch ≥ 2.5.1 + torchvision ≥ 0.20.1（2.5.1 是全模型编译的最低版本） | INSTALL.md:5-6；RELEASE_NOTES.md |
| CUDA | 需与 PyTorch CUDA 版本匹配的 CUDA toolkit（默认安装路径通常为 12.1）；自定义 CUDA kernel（连通域后处理）需 nvcc，**构建失败可忽略**（`SAM2_BUILD_CUDA=0` 或默认容错），仅缺失填洞/去噪后处理，"大多数情况下不影响结果" | INSTALL.md:7,15-26 |
| GPU 内核 | 依赖 PyTorch SDPA/Flash Attention 内核；老 GPU 或不兼容 PyTorch 报 "No available kernel"，可改放宽设置绕过 | INSTALL.md（常见问题节） |
| 官方测速环境 | A100（torch 2.5.1, cuda 12.4，全模型编译） | README §Model Description |
| **显存** | **官方渠道（README、INSTALL.md、论文、博客、demo README；2026-11-05 逐项检索）未公布任何每模型显存占用数字。不编造。** 代码级显存控制手段：`init_state(..., offload_video_to_cpu, offload_state_to_cpu, async_loading_frames)`（`sam2_video_predictor.py:42-47`）把视频帧与推理状态挪到 CPU RAM | 本卡检索结论 + 源码 |

4060 Laptop 8GB 可行性：owner 内部笔记（`2026-10-27-character-animation-route.md`，"短片段够，显存随帧数增长"）判断可行——**非官方数据，未在本会话实测**。

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

- **Python API 是唯一官方程序形态**。官方安装方式是**仓库根目录** `pip install -e ".[notebooks]"`（发行名 `SAM-2`，`setup.py:11`；INSTALL.md:10-13）。⚠️ PyPI 上**没有**官方包：`pypi.org/pypi/SAM-2/json` 返回 **404**；PyPI 上的 `sam2`（author "Jorge Insua"，home_page 指向其个人 fork，4 个发布自 0.3.0 起）是**第三方上传，勿用**（2026-11-05 实测 pypi.org JSON API）：
  - 图像：`SAM2ImagePredictor`（`set_image` → `predict`），提示 = 点/框/掩码，支持自动掩码生成。
  - 视频：`build_sam2_video_predictor` → `init_state(视频)` → `add_new_points_or_box(state, frame_idx, obj_id, points/box)` / `add_new_mask` → `propagate_in_video(state)` 产出 `(frame_idx, object_ids, masks)` → `reset_state`。`obj_id` 由调用方指定（`sam2_video_predictor.py:161-167`）。
  - 会话模型：inference state 一次绑定一段视频；逐帧特征按需缓存（"visual features on a small number of recently visited frames"，`sam2_video_predictor.py:80-81`；`storage_device` 赋值在 :74-76）。模型层是流式的（逐帧处理），但 **Python API 的 `init_state` 以整段视频为输入**——不是逐帧推送的在线接口。
- **加载渠道**：本地 checkpoint（`.pt`，官方 dl.fbaipublicfiles.com 直链）或 Hugging Face `from_pretrained("facebook/sam2-hiera-large")`；HF 上另有 safetensors 版（0.2B 参数，F32）与 Transformers 原生集成（`Sam2Model` / `Sam2VideoModel`，模型卡示例含 streaming video inference——Meta 官方 org 卡片内容，本会话未运行验证）。
- **加速开关**：`vos_optimized=True`（torch.compile 全模型编译，主要 VOS 提速；**会引入小幅数值方差**，RELEASE_NOTES 明示）。
- **无官方 ONNX 导出、无官方 C++ API**：仓库全量文件树（GitHub API trees，655 文件，未截断）中 `onnx` 零命中；唯一 C++/CUDA 代码是后处理 kernel `sam2/csrc/connected_components.cu`。
- **服务形态**：官方 web demo 前后端已开源（`demo/`，`backend.Dockerfile` + `docker-compose.yaml`），可本地部署（SAM 2.1 亦上架 Amazon SageMaker JumpStart，2025-02-12 博客更新）。
- **输出资产格式**：掩码张量（视频端为 video 分辨率 logits，`post_process_masks` 后可用）；无掩码之外的导出格式（无类别、无轨迹文件、无场景图）。

## 6. 与 OmniWorld 的边界 + 族谱归宿

**族谱归宿：②适配器（分割观测生产者）**，理由：

1. **正好落在 Observation 契约的真实半边**。PLAN §一①：grounding loop 是合成半边，"真实半边由视频适配器回归"。SAM 2 是当前**许可最干净**（代码+权重 Apache-2.0）且**质量有公开口径**（J&F、SA-V）的逐帧掩码观测生产者——给真实帧序列生产"掩码观测"这类结构化观察，与现有 depth 适配器同族不同类（第二类真实观测）。
2. **接入形态与现有 ONNX 适配器不同，要提前想清楚**。阶段 C 的 build-ort 形态（ONNX Runtime）对 SAM 2 **走不通**：官方无 ONNX 导出、无 C++ API（§5，已核验）。可行形态是 Python 进程外适配器（子进程/本地服务，官方 demo 后端是现成参照），这决定了它的适配器切片比 depth_anything_v2 重——进程边界、生命周期（init_state 会话模型）、帧供给方式都要新设计。
3. **输出语义是 World Contract 的教学案例，不是威胁**。`obj_id` 是调用方赋予、无再识别、无类别（§2.2）——掩码入库时 identity/confidence/ownership 必须由引擎侧接管；"SAM 2 跟丢"应表现为引擎侧观测缺失 + confidence 衰减。这正是 problem-log 015"指标按 producer 分组发布"的又一论据：分割观测的质量要与引擎侧身份稳定性分开计量。
4. **不复制的东西**：其流式记忆架构是视频对象分割的研究对象，与本引擎的 World Contract 不在代码层面复用；SA-V/J&F 是社区对照口径，不改造为我们自己的 benchmark 主体（`benchmarks/regression/` 仍是我们性能主张的唯一证据来源）。
5. **不是其他归宿的原因**：非③技能项目（Python 库，没有"从应用层下到 API 层"的学习价值，不同于 Vulkan 渲染器）；非⑤让渡（真实观测半边用得上）；非竞品参照（它不主张世界状态/仿真/评测，与我们的目标函数不重叠）；非评测对象（它不产生动作、不预测世界演化）。
6. **旁支**：owner 的角色动画路线（独立项目，`2026-10-27-character-animation-route.md`）已把 SAM 2 排为 L-合成渲染层的分割组件——同一依赖两处使用，但引擎侧定位仍以本卡（②适配器）为准，两边互不隶属。

## 7. 未解决问题 + 重查触发条件

**未解决问题（UNVERIFIED / 开放）：**

- **每模型显存占用**：官方无数字（§4），本会话未实测。8GB 级 GPU 上的模型档位选择（tiny/small/base+/large）只能实测后定。
- **"无再识别"表述**：官方未用此措辞，是从"跟丢/混淆局限 + 遮挡头语义 + obj_id 为调用方 id"综合的推断（§2.2）——采纳为设计假设前应做一个小实验（对象消失再现的 masklet 行为）。
- **Transformers 集成的 streaming video inference**：来自 HF 官方模型卡示例，未运行验证其与官方 `SAM2VideoPredictor` 行为的一致性。
- **社区 ONNX 导出的忠实度**：存在第三方移植的可能性，但不在一等来源范围内，本卡不予采信。
- **Meta 官方 org 出现 EdgeTAM（端侧变体）**：仅在 HF 模型卡页面的关联链接中瞥见，本卡未核验其内容与许可——登记为线索，不是事实。

**重查触发条件：**

1. 官方仓库出现 ONNX 导出、C++ API 或 ONNX Runtime 官方适配 → 立即重评接入形态（可能改写 §5/§6.2，进程外适配器可能降级为进程内 ORT 适配器）。
2. Meta 发布后继模型（SAM 3 / EdgeTAM 正式公告）或许可变更（权重族内已发生过 SAM License → Apache-2.0 的变化）→ 重核 §3。
3. 阶段 D 真实数据入环排到"掩码观测"切片、或角色动画路线开工 → 本卡从"研究"转"spike"，先跑 8GB 实测补 §4。

## 8. 来源清单

检索日期统一为 **2026-11-05**。

| # | 来源 | URL |
| --- | --- | --- |
| 1 | 官方仓库 README（raw main） | https://github.com/facebookresearch/sam2/blob/main/README.md |
| 2 | 官方 LICENSE（逐字 Apache 2.0，已下载比对） | https://github.com/facebookresearch/sam2/blob/main/LICENSE |
| 3 | INSTALL.md（Requirements / 常见问题） | https://github.com/facebookresearch/sam2/blob/main/INSTALL.md |
| 4 | RELEASE_NOTES.md（SAM 2 发布 2024-07-29、SAM 2.1 2024-09-30、vos_optimized 2024-12-11） | https://github.com/facebookresearch/sam2/blob/main/RELEASE_NOTES.md |
| 5 | sav_dataset/README.md（SA-V 规模与 CC BY 4.0） | https://github.com/facebookresearch/sam2/blob/main/sav_dataset/README.md |
| 5a | sav_dataset/LICENSE（BSD 许可全文，已下载核对） | https://github.com/facebookresearch/sam2/blob/main/sav_dataset/LICENSE |
| 6a | setup.py（发行名 NAME="SAM-2"，第 11 行） | https://github.com/facebookresearch/sam2/blob/main/setup.py |
| 12a | PyPI JSON API：SAM-2 返回 404；sam2 为第三方上传（author Jorge Insua） | https://pypi.org/pypi/SAM-2/json （404）；https://pypi.org/pypi/sam2/json |
| 6 | 源码 `sam2/sam2_video_predictor.py`（init_state/offload/obj_id 行号引用） | https://github.com/facebookresearch/sam2/blob/main/sam2/sam2_video_predictor.py |
| 7 | 源码 `sam2/modeling/sam2_base.py`（num_maskmem=7 等） | https://github.com/facebookresearch/sam2/blob/main/sam2/modeling/sam2_base.py |
| 8 | 仓库全量文件树（GitHub API trees，onnx 零命中核验） | https://api.github.com/repos/facebookresearch/sam2/git/trees/main?recursive=1 |
| 9 | 论文页（arXiv:2408.00714，v1 2024-08-01 / v2 2024-10-28） | https://arxiv.org/abs/2408.00714 |
| 10 | 论文全文 HTML（§3 PVS 任务、§Memory bank、Appendix C Limitations） | https://arxiv.org/html/2408.00714v2 |
| 11 | 官方博客（2024-07-29 发布，2025-02-12 SAM 2.1/SageMaker 更新；能力声称与 Limitations 节） | https://ai.meta.com/blog/segment-anything-2 |
| 12 | HF 官方模型卡 facebook/sam2-hiera-large（License: apache-2.0、0.2B params、Transformers 用法） | https://huggingface.co/facebook/sam2-hiera-large |
| 13 | 本仓库内部笔记（非一手，仅作推断依据）：角色动画路线的 SAM 2 定位与显存判断 | `docs/17-research/2026-10-27-character-animation-route.md` |
| 14 | 指针材料（定位依据，不复制）：族谱编号与 Observation 契约 | `PLAN.md` §一/§二；`docs/17-research/third-party-research-map.md`；`docs/17-research/research-queue.md` |

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-sam-2.md](deep/2026-11-05-sam-2.md)
