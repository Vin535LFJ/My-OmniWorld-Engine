# VGGT 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05（含同日第二次核验的更正，见 §2.3）· 状态：首建

## 0. 一句话

VGGT（CVPR 2025 Best Paper，Meta AI × Oxford VGG）是一个 **~1B 参数的前馈 Transformer**：输入一个场景的 1～数百张视图，**一次前馈**同时输出相机内外参、深度图、3D 点图与跨帧 3D 点轨迹——它是"几何观测生产者"，不是交互式世界模型。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
| --- | --- |
| 官方定位：feed-forward neural network，从"one, a few, or hundreds of views"在数秒内直接推断场景全部关键 3D 属性：相机外参+内参、点图（point maps）、深度图（depth maps）、3D 点轨迹（3D point tracks） | github.com/facebookresearch/vggt（README §Overview）；arxiv.org/abs/2503.11651（摘要） |
| 论文声称：图像重建 <1 秒；在相机参数估计、多视图深度估计、稠密点云重建、3D 点跟踪等多个 3D 任务上达到 state-of-the-art | arxiv.org/abs/2503.11651（摘要） |
| 论文声称：预训练 VGGT 作特征骨干可显著提升下游任务（非刚性点跟踪、前馈新视角合成） | arxiv.org/abs/2503.11651（摘要） |
| CVPR 2025 Best Paper Award（官方 Updates 记录，2025-06-13） | github.com/facebookresearch/vggt（README §Updates） |
| 零样本单视图重建：模型无需复制单图为图对，可直接从单视图 token 推断 3D 结构（官方称未为此任务训练） | github.com/facebookresearch/vggt（README §Zero-shot Single-view Reconstruction） |
| 四个预测头可独立调用：`camera_head`（外参/内参，OpenCV 约定，camera-from-world）、`depth_head`（深度+置信度）、`point_head`（点图+置信度）、`track_head`（给定 query points 的轨迹+可见性/置信度分数） | github.com/facebookresearch/vggt（README §Detailed Usage） |
| 架谱系：PoseDiffusion / VGGSfM / CoTracker 的工作延续（官方"Research Progression"表） | github.com/facebookresearch/vggt（README §Research Progression） |
| 训练代码已发布（2025-07-06，`training/` 目录，含自定义数据集微调示例）；VGGT-500M / VGGT-200M 未发布（Checklist 未勾选） | github.com/facebookresearch/vggt（README §Updates、§Checklist） |

## 2. 边界与限制

### 2.1 官方承认的限制

| 限制 | 来源 |
| --- | --- |
| 不支持鱼眼/全景图像；极端输入旋转下重建性能退化；小幅非刚性运动可处理，但**大幅非刚性形变场景会失败**（论文 §5 Discussions） | arxiv.org/html/2503.11651v1 |
| 全局自注意力在大 token 数下**内存高度密集**：峰值显存随帧数增长至 ~40 GB@200 帧（论文 Table 9；作者提及 tensor parallelism 类缓解） | arxiv.org/html/2503.11651v1 |
| 官方显存基准（单卡 H100 + flash attention v3，仅测特征骨干——论文措辞 feature backbone，历史版 README 措辞 aggregator；基准分辨率 336×518）：1 帧 1.88 GB → 10 帧 3.63 GB → 50 帧 11.41 GB → 200 帧 40.63 GB，200 帧 8.75 s；camera head ≈ +5% 耗时/+2% 显存，DPT head ≈ 0.03 s、0.2 GB/帧 | arxiv.org/html/2503.11651v1（Table 9 及正文） |
| **该基准表已从 main README 删除**（提交 9e4fa662a8，信息 "cache"，父提交 44b3afb；该提交新增公告日期为 2026-05-15；核查员记提交日期为 2026-05-16，精确提交时间戳本卡未独立核验）。数字现仅存于论文 Table 9 与历史版 README（@44b3afbd18，同表） | github.com/facebookresearch/vggt（commit 9e4fa662a8 diff 及 commit 页、44b3afbd18） |
| 官方承认旧实现缺陷（2026-05-15 公告）："implementation issue that was keeping redundant intermediate tensors in memory"；修复后同显存预算可跑**约 2-3× 输入帧**。Table 9 的数字是**修复前基准**，不代表当前 main 代码实测行为 | github.com/facebookresearch/vggt（main README Updates @2026-11-05；commit 9e4fa662a8 diff） |
| 单目深度估计性能**作者自己未定量测试**（仅社区成员在 issue #36 提供与 DepthAnything v2 / MoGe 的对比） | github.com/facebookresearch/vggt（README §Zero-shot Single-view Reconstruction） |
| 不想要的像素（反光/天空/水面）需手动置 0/1 掩蔽，"精确分割掩码不必要，简单包围盒掩码即可"——反证该类内容会干扰重建 | github.com/facebookresearch/vggt（README §Detailed Usage） |
| 商用检查点与原始检查点是**分别发布的重训产物**，官方只称"性能相似"（Co3D AUC@30: 90.37 vs 89.98），并请用户报告显著性能差异 | github.com/facebookresearch/vggt（README §Updates 2025-07-29、§License） |

### 2.2 我们的推断（非官方声称，不作为事实引用）

- 论文 Table 9 / 历史版 README 的显存表是 **2026-05-15 显存修复前**的基准（H100 + FA3、仅特征骨干）；本仓 4060 8 GB 上的实际帧数上限**必须本地实测**。修复前基准下 ≤20 帧（5.58 GB）是唯一可能档位；官方称修复后同预算约 2-3× 帧数（见 §2.1），8 GB 可容纳帧数相应上调——但这是官方在自有环境的主张，跨 GPU 外推仍不成立。
- VGGT 是**每场景一次前馈**的离线/分段批处理形态，无增量式/流式接口（README 未提供）；要进实时环需自行做滑窗重推理，代价未验证。
- 官方仓库未见 ONNX 导出或非 PyTorch 运行时支持（基于已核验的 README 全文与 requirements）；能否导出 ONNX 供本仓 ON 构建（`OW_WITH_ONNXRUNTIME=ON`）使用 **UNVERIFIED**，未发现官方路径。
- 官方称"数百视图数秒内"，与"实时"是两回事：50 帧 1.04 s、200 帧 8.75 s 是吞吐数字（且为修复前基准），不含 I/O 与可视化（官方另注明可视化慢是第三方渲染所致）。

### 2.3 更正记录（2026-11-05 第二次核验）

独立核查员对本卡首版提出三条修正；逐条对一手来源复核，**三条全部成立**，已修正 §2.1 / §2.2 / §4 / §5 / §7。关键证据：

1. 当前 main README（2026-11-05 经 raw.githubusercontent.com 与 github.com 仓库页**两个独立路径**抓取）**无** "Runtime and GPU Memory" 节；Updates 首两条为 2026-05-18（VGGT-Omega 发布）与 2026-05-15（显存修复）。
2. 提交 9e4fa662a8（信息 "cache"，父提交 44b3afb，touch README.md +6−16 与 vggt/models/aggregator.py +14−7）删除该节、加入 2026-05-15 修复公告、并将 aggregator 改为仅缓存第 4/11/17/23 层（`cached_layer_indices`，其余层条目为 None）。
3. 论文 Table 9 与已删 README 表数字逐项一致（时间 0.04/0.05/0.07/0.11/0.14/0.31/1.04/3.12/8.75 s；显存 1.88/2.07/2.45/3.23/3.63/5.58/11.41/21.15/40.63 GB），且补充了 README 未载细节（336×518 分辨率、feature backbone-only、camera/DPT head 开销）。
4. **首版错误原因**：首版检索时经网页读取服务抓到的 main README 是**过期快照**（含已删节、缺 2026 条目），与上述四条取证路径矛盾——已按当前状态更正。同批引用自该快照的其余事实（2025 年 Updates 各条、Checklist 未勾选 VGGT-500M/200M、Zero-shot 节、License 节）已在当前 main README 逐一复核仍存在；LICENSE.txt 与两张 HF 卡未重抓（核查员未质疑、无变更迹象）。

## 3. 许可（代码 / 权重 / 数据 三分）

**结论先行：仓库明确区分"原始检查点"与"允许商用的重发布检查点"，两者许可不同，逐项核验如下。**

| 对象 | 许可 | 要点 | 来源 |
| --- | --- | --- | --- |
| 代码（整个仓库） | **VGGT License v1**（2025-07-29，自定义 Meta 协议，非 OSI 开源许可） | 允许商用（**排除军事用途**）；再分发衍生作品必须同样采用本协议（§1.b.i）；含 Acceptable Use Policy（禁止军事/战争/关键基础设施等用途）；加州法管辖；Meta 可随时修订 | github.com/facebookresearch/vggt（LICENSE.txt 全文） |
| 权重·原始检查点 `facebook/VGGT-1B` | **CC-BY-NC-4.0**（HF 卡 license tag 实读） | **非商业**；权重为 `model.pt`（README 提供手动下载路径） | huggingface.co/facebook/VGGT-1B（卡 front-matter `license: cc-by-nc-4.0`） |
| 权重·商用重发布检查点 `facebook/VGGT-1B-Commercial` | HF tag **`vggt-aup-license`**（即 VGGT License） | **允许商用、排除军事**（卡文原话："licensed for commercial use, with the exception of military applications"）；**gated**：需提交联系信息表单，LLaMA 式自动审批（README 原话）；Safetensors 格式、1B params、F32 | huggingface.co/facebook/VGGT-1B-Commercial（卡页）；github.com/facebookresearch/vggt（README §Updates 2025-07-29） |
| 数据 | **不适用**：仓库不分发训练数据集，只发布训练代码与微调示例 | 论文/仓库未随代码附训练数据许可清单 | github.com/facebookresearch/vggt（README §Updates 2025-07-06） |

- 官方许可公告原文（2025-07-29）："only the newly released checkpoint **VGGT-1B-Commercial** is licensed for commercial usage — **the original checkpoint remains non-commercial**."
- **对 OmniWorld 的直接含义**（推断）：VGGT License 的再分发条款（衍生作品须同协议）+ gated 商用检查点 ⇒ 检查点与代码**都不能进 OmniWorld 发行物**，只能作为"用户自行下载、外部运行"的适配器依赖——这与参考模式零外部依赖承诺（ADR-0012）一致。

## 4. 硬件要求（显存/驱动/OS）

| 维度 | 事实 | 来源 |
| --- | --- | --- |
| GPU（dtype） | bfloat16 需 Ampere 及以上（Compute Capability ≥ 8.0），否则回退 float16 | github.com/facebookresearch/vggt（README §Quick Start） |
| 显存（官方基准，**2026-05-15 显存修复前**；单卡 H100 + flash attention v3，仅特征骨干/aggregator，基准分辨率 336×518） | 1 帧 1.88 GB / 2 帧 2.07 / 4 帧 2.45 / 8 帧 3.23 / 10 帧 3.63 / 20 帧 5.58 / 50 帧 11.41 / 100 帧 21.15 / 200 帧 40.63 GB；200 帧耗时 8.75 s。**该表已从 main README 移除**（commit 9e4fa662a8），现存于论文 Table 9 与历史版 README，不代表当前 main 代码实测行为 | arxiv.org/html/2503.11651v1（Table 9）；github.com/facebookresearch/vggt（历史版 README @44b3afbd18） |
| 显存修复（官方公告 2026-05-15） | 旧实现"保留冗余中间张量"；修复后**同显存预算约 2-3× 输入帧**；aggregator 改为仅缓存第 4/11/17/23 层输出（`cached_layer_indices=(4,11,17,23)`，其余层条目为 None） | github.com/facebookresearch/vggt（main README Updates；commit 9e4fa662a8 diff） |
| 软件栈 | requirements.txt 锁定 `torch==2.3.1`、`torchvision==0.18.1`、`numpy==1.26.1`，另 Pillow / huggingface_hub / einops / safetensors 不锁版本；官方建议先装 torch==2.3.1 + torchvision==0.18.1 避免 CUDA 不匹配 | github.com/facebookresearch/vggt（requirements.txt、docs/package.md） |
| OS / 驱动 | **UNVERIFIED**：官方渠道未声明 Windows 原生支持或最低驱动版本；未发现官方 Windows 安装说明。CUDA 依赖经由 PyTorch 2.3.1 官方轮子间接确定 | （检索范围内无该信息；GitHub issues 未逐一排查） |
| 8 GB 显存可用性（本仓环境） | **UNVERIFIED（推断）**：修复前基准下 ≤20 帧（5.58 GB）是唯一可能落入 8 GB 的档位；官方称 2026-05-15 修复后同预算约 2-3× 帧数（~40-60 帧量级成为候选），跨 GPU 外推 + 各预测头额外开销未实测 | 本卡 §2.2 推断；github.com/facebookresearch/vggt（main README Updates） |

## 5. 可集成接口（API / 导出 / 控制面 / 资产格式）

- **Python API**（唯一一等编程接口）：`VGGT.from_pretrained("facebook/VGGT-1B")` 加载；`model.aggregator(images)` → token 列表，随后按需过四个头（camera/depth/point/track）；工具函数 `pose_encoding_to_extri_intri`（OpenCV 约定外内参）、`unproject_depth_map_to_point_map`。输入经 `load_and_preprocess_images` 预处理：`target_size = 518`，尺寸取 14 的倍数，crop/pad 两种模式。〔来源：github.com/facebookresearch/vggt README §Detailed Usage；vggt/utils/load_fn.py〕⚠️ **2026-05-15 公告所对应的提交（9e4fa662a8）起**，aggregator 返回的中间层 token 列表改为仅第 4/11/17/23 层有值、其余条目为 None——直接消费中间层输出的下游代码需适配。〔来源：github.com/facebookresearch/vggt commit 9e4fa662a8 diff〕
- **导出·COLMAP**：`demo_colmap.py` 将预测直接写成 COLMAP 格式（`sparse/cameras.bin`、`images.bin`、`points3D.bin`），可选 bundle adjustment（`--use_ba`，可调 `--max_query_pts` / `--query_frame_num`）——官方公告（2025-06-02）明确"可直接用于 gsplat 或其他 NeRF/高斯泼溅库"，并给出 gsplat==1.3.0 的训练示例命令。〔来源：github.com/facebookresearch/vggt README §Exporting to COLMAP Format、§Integration with Gaussian Splatting、§Updates〕
- **可视化控制面**：Gradio 网页 demo（可上传图像/视频，Hugging Face Spaces 有在线版）；Viser 3D 查看器（`demo_viser.py --image_folder ...`，`--use_point_map` 切换点图来源）。〔来源：同上 README §Interactive Demo〕
- **权重格式**：原始检查点 `model.pt`（torch pickle）；商用检查点为 **Safetensors**（1B、F32）。〔来源：github.com/facebookresearch/vggt README；huggingface.co/facebook/VGGT-1B-Commercial 卡页〕
- **打包方式**：`pip install -e .` 本地可编辑安装（docs/package.md）；**无官方 C++/ONNX/非 PyTorch 运行时接口**（已核验的 README 全文未提及；能否自行导出 ONNX **UNVERIFIED**）。〔来源：github.com/facebookresearch/vggt docs/package.md〕
- **评测入口**：Co3D 相机位姿评测代码在 `evaluation` 分支（2025-05-03 公告）。〔来源：github.com/facebookresearch/vggt README §Updates〕

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：②适配器（几何观测生产者）——Adapter only。** 理由：

1. **产物对位**：VGGT 的四类输出（外内参、深度+置信度、点图+置信度、轨迹+可见性/置信度）正是几何观测的原料，且自带逐像素置信度——与 World Contract 的 confidence 语义天然同构。它站在**观察源/伪真值生产**一侧，与 Brain adapter（OpenVLA，动作侧）对偶：一个消费世界状态产生动作，一个消费图像产生几何观测。
2. **不能进核心**：纯 Python/PyTorch 栈（torch 2.3.1），违背参考模式零外部依赖承诺；且 VGGT License 再分发条款 + 原检查点 CC-BY-NC 使其**只能外部运行、数据入环**（§3）。正确姿势与本仓 COCO 入环脚本同构：`数据不入库，产出（几何观测 JSON/数组）入库`。
3. **不是竞品/评测对象**：VGGT 无交互、无动作接口、无世界状态——它不进"评测对象"名单（MineWorld/Matrix-Game 类才有该资格，见 research-queue 第三方交互世界模型条目）；但它的**输出可以作为评测管线的伪真值生产者**（为真实帧序列补相机/深度真值），这是"评测是第一个应用"（ADR-0008）语境下的正用。
4. **Do not copy**：SfM/BA 管线（`demo_colmap.py --use_ba` 背后的 VGGSfM 系实现）、训练栈、模型本体——一概不搬进仓库。
5. **前置条件（触发才做）**：阶段 D 真实数据入环需要几何伪真值、且 8 GB 显存实测能跑目标帧数时，才立项适配器切片；商用检查点需过 gated 审批（提交表单），研究用途可直接用 CC-BY-NC 原检查点（非商用前提成立）。

## 7. 未解决问题 + 重查触发条件

| 未解决问题 | 重查触发条件 |
| --- | --- |
| 4060 8 GB 上实际可跑帧数（Table 9 是 2026-05-15 修复前基准：H100+FA3+仅特征骨干；官方称修复后同预算约 2-3× 帧数） | 适配器立项前实测一次（起步档位可按 20-50 帧设计，以实测为准） |
| VGGT-Omega（2026-05-18 发布，官方称 "The next step of VGGT"，vggt-omega.github.io）是否取代 VGGT 成为适配器锚定对象——本卡**未深查**其许可/权重/接口 | 适配器立项前先调研 VGGT-Omega；或其发布独立许可/权重清单时 |
| 能否导出 ONNX / 有无非 PyTorch 运行时（DINOv2 backbone + 自定义注意力，导出难度未知） | 阶段 C 想把它接进 `OW_WITH_ONNXRUNTIME=ON` 构建时；先查官方 issue 区与 training 分支 |
| VGGT-1B-Commercial 的 HF LICENSE 文件内容与 GitHub LICENSE.txt 是否逐字一致（**UNVERIFIED**，见下） | 走完 gated 审批、真正下载该检查点时，本地 diff 一次 |
| 两个检查点除重训外的行为差异（官方仅称"性能相似"） | 首次实测时对同帧序列各跑一遍，比对置信度分布 |
| VGGT-500M / VGGT-200M 发布（Checklist 未勾选） | 仓库 Checklist 勾选或 Releases 出现新检查点 |
| 许可再变更（Meta 保留随时修订权，VGGT License §8） | 任何把它引入交付物的时刻，重读 LICENSE.txt |

**UNVERIFIED 清单**（检索日 2026-11-05）：
1. `huggingface.co/facebook/VGGT-1B-Commercial` 仓库内 LICENSE 文件的逐字内容——该 HF 仓库的 raw/api/tree 端点在本环境多次连接超时（WebFetch connect timeout ×4、curl HTTP:000 ×2，仅卡片页面可经网页读取服务到达）。卡上 HF tag `vggt-aup-license` 与卡文"Refer to the LICENSE file for full terms"只证明该文件存在，不证明其内容；本卡对商用检查点许可的描述以 GitHub 侧 LICENSE.txt 全文 + 两张卡页陈述为准。
2. Windows 原生支持与最低驱动版本——官方渠道未声明（§4）。

## 8. 来源清单

检索日期统一为 **2026-11-05**。

| # | 来源 | URL |
| --- | --- | --- |
| 1 | GitHub README（能力/更新公告/COLMAP 导出/gsplat 集成/许可声明；⚠️ 2026-11-05 第二次核验：当前版本**已无** §Runtime and GPU Memory、Updates 含 2026 条目——首版经网页读取服务抓到的是过期快照，见 §2.3） | https://raw.githubusercontent.com/facebookresearch/vggt/main/README.md |
| 2 | VGGT License v1 全文（代码许可，2025-07-29 版） | https://raw.githubusercontent.com/facebookresearch/vggt/main/LICENSE.txt |
| 3 | 原始检查点卡 `facebook/VGGT-1B`（license: cc-by-nc-4.0；pipeline_tag: image-to-3d） | https://huggingface.co/facebook/VGGT-1B/raw/main/README.md |
| 4 | 商用检查点卡 `facebook/VGGT-1B-Commercial`（tag `vggt-aup-license`；gated；"commercial use, with the exception of military applications"；Safetensors 1B F32） | https://huggingface.co/facebook/VGGT-1B-Commercial |
| 5 | 论文摘要页（SOTA 声称、<1 s、特征骨干用途；CVPR 2025） | https://arxiv.org/abs/2503.11651 |
| 6 | 论文 HTML 全文 §5 Discussions（官方限制：鱼眼/全景、极端旋转、非刚性形变、内存、BA 取舍）+ Table 9（修复前基准表，数字与历史版 README 一致；336×518、feature backbone-only、head 开销） | https://arxiv.org/html/2503.11651v1 |
| 7 | 预处理源码（`target_size = 518`、14 的倍数、crop/pad） | https://raw.githubusercontent.com/facebookresearch/vggt/main/vggt/utils/load_fn.py |
| 8 | requirements.txt（torch==2.3.1 等） | https://raw.githubusercontent.com/facebookresearch/vggt/main/requirements.txt |
| 9 | 包安装文档（`pip install -e .`） | https://raw.githubusercontent.com/facebookresearch/vggt/main/docs/package.md |
| 10 | 历史版 README @44b3afbd18（含已删除的 Runtime and GPU Memory 表，"aggregator on a single NVIDIA H100" + Flash Attention 3 说明） | https://raw.githubusercontent.com/facebookresearch/vggt/44b3afbd18/README.md |
| 11 | 提交 9e4fa662a8 diff（删 Runtime 节、加 2026-05-15 修复公告、aggregator `cached_layer_indices=(4,11,17,23)`） | https://github.com/facebookresearch/vggt/commit/9e4fa662a8.diff |
| 12 | 提交 9e4fa662a8 页（信息 "cache"，父提交 44b3afb；README.md +6−16、aggregator.py +14−7） | https://github.com/facebookresearch/vggt/commit/9e4fa662a8 |
| 13 | main README 仓库页复核（2026-11-05 第二独立路径；Updates 含 2026-05-18 VGGT-Omega 发布与 2026-05-15 显存修复） | https://github.com/facebookresearch/vggt |

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-vggt.md](deep/2026-11-05-vggt.md)
