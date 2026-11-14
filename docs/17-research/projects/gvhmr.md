# GVHMR 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建

## 0. 一句话

GVHMR（浙大 zju3dv，SIGGRAPH Asia 2024）把单目视频变成**世界坐标系的人体运动序列**：逐帧在"重力-视线"（Gravity-View）坐标系里回归 SMPL-X 姿态，再用相机旋转转回世界系——它是"视频 → 世界坐标运动"这一步目前最有代表性的开源生产者，但代码与人体模型都是非商用研究许可。

## 1. 是什么 + 官方能力声称

| 事实 | 来源 |
| --- | --- |
| 全称 "World-Grounded Human Motion Recovery via Gravity-View Coordinates"，SIGGRAPH Asia 2024（Conference Track）；作者 Zehong Shen、Huaijin Pi、Yan Xia、Zhi Cen、Sida Peng、Zechen Hu、Hujun Bao、Ruizhen Hu、Xiaowei Zhou | arxiv.org/abs/2409.06662 |
| 官方仓库 `zju3dv/GVHMR`；仓库描述字段自称 "Code for ..., Siggraph Asia 2024, TPAMI 2026"（期刊版已被 TPAMI 接收）；GitHub API 快照（2026-11-05）：2256★ / 257 forks，license 字段 `NOASSERTION` | api.github.com/repos/zju3dv/GVHMR |
| 输入：单目视频（或帧文件夹 `demo_folder.py`）。预处理链 = 人体 bbox 跟踪 + 2D 关键点检测 + 图像特征提取 + 相对相机旋转估计（视觉里程计或陀螺仪） | zju3dv.github.io/gvhmr/ |
| 方法核心：Gravity-View 坐标系由世界重力方向与相机视线方向定义，天然重力对齐、逐帧唯一，降低图像→姿态映射的学习歧义；逐帧估计避免自回归方法的误差累积；估计结果经相机旋转变换回世界坐标系，形成全局运动序列 | arxiv.org/abs/2409.06662（摘要） |
| 输出：SMPL-X 姿态/形状（θ ∈ ℝ²¹ˣ³、10 维 β）+ 相机系轨迹 + 世界系轨迹；网络还输出手/脚的 stationary 概率，供后处理修正全局平移并用 CCD-IK 解局部姿态 | arxiv.org/html/2409.06662v1 |
| 模型前向官方口径：RTX 4090 上 1430 帧（约 45 秒）视频 280 ms，**不含**预处理 | zju3dv.github.io/gvhmr/ |
| 预处理官方口径：总计 46.0 s（YOLOv8 4.9 + ViTPose 20.0 + ViT 特征提取 10.1 + DPVO 11.0）——注意论文口径含 DPVO，与仓库 2025-03-08 起默认 SimpleVO 的现状不同 | arxiv.org/html/2409.06662v1（§4.4 Running Time） |
| 训练官方口径：AMASS / BEDLAM / H36M / 3DPW 从零训练，420 epochs、batch 256、2×RTX 4090 共 13 小时 | zju3dv.github.io/gvhmr/ |
| 官方声称"在 in-the-wild 基准上精度与速度均优于 SOTA"（vs WHAM、SLAHMR 等）；脚滑指标 FS：RICH 3.0 mm / EMDB 3.5 mm（论文表列 WHAM 3.3 / 4.4）；消融显示无后处理 FS 6.8 mm → 有后处理 3.0 mm | zju3dv.github.io/gvhmr/ · arxiv.org/html/2409.06662v1 |
| 官方在线 demo：Google Colab + HuggingFace Space（`LittleFrog/GVHMR`，README 直链） | github.com/zju3dv/GVHMR（README） |

## 2. 边界与限制

**官方承认的限制**（一手来源明说的才放这里）：

- 3DPW 上 PA-MPJPE 略逊 WHAM，作者归因于"预测 SMPL-X 参数而非 SMPL 可能引入误差"（论文 §4.3）。— arxiv.org/html/2409.06662v1
- 预处理依赖相机旋转估计（DPVO 或陀螺仪）；论文声称方法对相机旋转误差鲁棒，但依赖本身存在。— zju3dv.github.io/gvhmr/ · arxiv.org/html/2409.06662v1
- 预处理是运行时瓶颈：1430 帧约 46 s（论文 §4.4 口径，含 DPVO：4.9+20.0+10.1+11.0），比模型前向（280 ms）慢两个数量级。— arxiv.org/html/2409.06662v1（§4.4 Running Time）
- 测试脚本带后处理、训练不复用，因此"复现训练脚本时全局指标结果会与论文不同"（README 原文警示）。— github.com/zju3dv/GVHMR（README）
- 脚滑不是自动消失的：依赖 stationary 概率 + 全局平移修正 + CCD-IK 的后处理（消融 6.8 → 3.0 mm）。— arxiv.org/html/2409.06662v1
- 相机静态与否是用户决策：`-s` 跳过 VO（仅当已知相机静止），否则走 SimpleVO/DPVO。— github.com/zju3dv/GVHMR（README）

**我们的推断**（非官方声称，不作为引用依据）：

- 论文**没有专门的 Limitations/失败案例分析章节**：任务焦点里问的"官方承认的遮挡/快速运动失败模式"在论文原文中**不存在**——遮挡与快速运动的鲁棒性边界官方未承认、未否认，属 **UNVERIFIED**。
- demo 输出的 bbox 缓存形状为 (L,4)（单条轨迹），暗示 demo 链路按单主体组织；多人在场时的行为 README/论文摘要均未声明——多人能力未验证。
- ONNX/C++ 集成路径官方完全未提供；栈是 Python + PyTorch + 自研渲染，适配只能以独立 Python 环境形态存在。
- 6 GB 显存（社区自测）若属实，则本机 8 GB（4060 Laptop）可跑 demo——待实测，见 §4/§7。

## 3. 许可（代码 / 权重 / 数据 三分）

| 层 | 许可状态 | 来源 |
| --- | --- | --- |
| **代码** | ZJU CAD&CG 3D Vision Group **自定义许可**（非 OSI 标准许可）：使用/复制/修改/分发**仅限教育、研究、非营利目的**；衍生作品必须开源且禁商用；保留版权/专利/商标/署名声明；须引用其论文；**商用需联系 xwzhou@zju.edu.cn**。GitHub license 字段为 `Other / NOASSERTION`，与"非标准许可"互证 | github.com/zju3dv/GVHMR（LICENSE）· api.github.com/repos/zju3dv/GVHMR |
| **权重** | `gvhmr_siga24_release.ckpt` 经 Google Drive 分发；INSTALL.md 仅有整包条款 "By downloading, you agree to the corresponding licences"，**无独立的权重许可文本** → 权重授权条款**UNVERIFIED**（不明 ≠ 宽松） | github.com/zju3dv/GVHMR（docs/INSTALL.md） |
| **人体模型 / 数据** | SMPL-X 与 SMPL 须在 MPI 官网（smpl-x.is.tue.mpg.de / smpl.is.tue.mpg.de）注册并同意许可后下载（`SMPLX_{GENDER}.npz`、`SMPL_{GENDER}.pkl`）；SMPL-X 官网自述 "available for research purposes"，商用询价走 Max Planck Innovation（smpl@max-planck-innovation.de）。预处理训练/评测数据（AMASS/BEDLAM/H36M/3DPW/EMDB/RICH）作者声明"因原数据集许可限制不能重分发"，下载即同意原数据集条款、仅限研究用途 | smpl-x.is.tue.mpg.de/ · github.com/zju3dv/GVHMR（docs/INSTALL.md） |

> 与角色动画路线已登记的许可红线一致（SMPL-X 非商用）：本卡不重述，见 `docs/17-research/2026-10-27-character-animation-route.md` §5。GVHMR 在其上**多加一层**：代码本身也是非商用。

## 4. 硬件要求

| 项 | 数值 | 来源 |
| --- | --- | --- |
| 训练硬件 | 2×RTX 4090、420 epochs、13 小时、batch 256 | zju3dv.github.io/gvhmr/ |
| 推理速度声明的环境 | RTX 4090（280 ms / 1430 帧，不含预处理） | zju3dv.github.io/gvhmr/ |
| 推理显存 | **官方无数字**。社区自测："tested, it requires 6gb vram"（issue #13 提问者自答，2024-09-17，非维护者发言）→ **非官方，UNVERIFIED** | github.com/zju3dv/GVHMR/issues/13 |
| 软件栈 | Python 3.10（conda）；主链路版本由 requirements.txt 逐项 pin：torch==2.3.0+cu121、torchvision==0.18.0+cu121、ultralytics==8.2.42（注释注明 YOLO）、timm==0.9.12（注释注明 HMR2.0a 特征提取）、numpy==1.23.5、lightning==2.3.0、pytorch3d-0.7.6（`py310_cu121_pyt230` 的 `linux_x86_64` wheel）、av==13.0.0；DPVO 可选步骤另需 Eigen 3.4.0、torch-scatter、numba、pypose 与 `CUDA_HOME=/usr/local/cuda-12.1` 本地编译 | github.com/zju3dv/GVHMR（requirements.txt · docs/INSTALL.md） |
| OS | README/INSTALL **未提及任何操作系统**；DPVO 编译步骤为 Linux 惯例，且 requirements.txt 的 pytorch3d 官方 wheel 仅 `linux_x86_64` → Linux 优先是我们的推断，Windows 跑通性 **UNVERIFIED** | github.com/zju3dv/GVHMR（README·INSTALL·requirements.txt） |

## 5. 可集成接口

- **入口**（CLI，无库级 API 承诺）：`python tools/demo/demo.py --video=... -s`、`python tools/demo/demo_folder.py -f ... -d ... -s`；`-s` 静态相机捷径、`--use_dpvo` 切换 VO、`f_mm` 指定全画幅焦距（2025-03-08 新增）。— github.com/zju3dv/GVHMR（README）· tools/demo/demo.py
- **产物**（demo.py 实写）：`hmr4d_results`（torch pickle）含 `smpl_params_incam`、`smpl_params_global`、`K_fullimg`；预处理缓存 `bbx` / `vitpose` / `vit_features` / `slam`（SimpleVO 输出 (L,4,4)，DPVO 输出 (L,7)）；渲染三件套 = 相机系网格叠加视频 + 世界系地面渲染（模拟 24 mm 镜头）+ 横向拼接视频。— github.com/zju3dv/GVHMR（tools/demo/demo.py）
- **评测复现**：单命令测试 3DPW / RICH / EMDB（`gvhmr/test_3dpw` 等 override），checkpoint `inputs/checkpoints/gvhmr/gvhmr_siga24_release.ckpt`；训练 `python tools/train.py exp=gvhmr/mixed/mixed`。— github.com/zju3dv/GVHMR（README）
- **依赖链**（都是可替换的适配点）：YOLOv8x（检测）→ ViTPose-H（2D 关键点）→ HMR2/4D-Humans（图像特征）→ SimpleVO（内置默认）或 DPVO（外部）。— github.com/zju3dv/GVHMR（docs/INSTALL.md）
- **无 C++ API、无服务化控制面**；Blender 导出是社区 issue 行为（#10），非官方接口。— github.com/zju3dv/GVHMR/issues/10

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：②适配器（运动观测生产者）**，形态为独立 Python 环境的可选依赖，产出经运动数据层契约转接。理由：

1. **与既有指针材料的定位一致**：角色动画路线（2026-10-27 立项）已认定"运动数据层是唯一值得长期投资的资产"，其 P0 层候选（MediaPipe/DWPose）只有 2D 关键点；GVHMR 恰好补上"视频 → 世界坐标 3D 运动"这一级，且逐帧输出天然带时间对齐（30fps 重编码）、置信度（stationary 概率）等运动数据层要求的字段。链接不重述：`docs/17-research/2026-10-27-character-animation-route.md` §1/§6。
2. **与引擎叙事对齐**：GVHMR 属于"感知模型"座席——它的输出要落进 World Contract 的四要素（time：逐帧时间基准；identity：人体轨迹；ownership：预处理缓存单写者；confidence：stationary 概率与关键点置信度），是评测面板的天然观察源/真值生产者候选。
3. **许可决定形态**：代码（ZJU 非商用）+ 人体模型（MPI 研究许可）双层非商用，叠加权重条款不明（§3）→ 它永远不能进引擎核心、不能进可交付商用路径；与 ONNX Runtime 的"可选依赖、默认 OFF"地位同构（ADR-0012 先例），但比 ORT 更受限——**连产出物都要标注衍生许可**。
4. **不是其他归宿**：③技能项目（无需复刻其能力，不存在"我们实现一个 GVHMR"）；⑤明确让渡（与运动数据层直接相关，必须盯）；竞品参照（我们不做运动恢复，无竞争关系）；评测对象（可在侧项目内作为"从视频造 GT 运动"的受评生产者，但主归宿仍是适配器）。

**边界一句话**：核心零依赖承诺不动；GVHMR 只存在于适配器层的独立 Python 环境，其输出以结构化序列（SMPL-X 参数 + 全局轨迹 + 置信度）过契约进入世界状态；SMPL-X 与代码的非商用许可决定其产出不进任何商用交付路径。

## 7. 未解决问题 + 重查触发条件

| # | 未解决问题 | 触发条件 |
| --- | --- | --- |
| 1 | 8 GB（4060 Laptop）实跑显存与耗时：官方无数字，社区自测 6 GB | 侧项目 P0 需要世界坐标运动时，本机实测 demo 并记录（照 AGENTS.md 纪律：数字必须带环境元数据） |
| 2 | Windows 跑通性（README/INSTALL 无 OS 声明，DPVO 需 Linux 式编译环境） | 本机实测；SimpleVO 默认路径（无 DPVO）能否在 Windows 跑通是第一问 |
| 3 | 权重独立许可条款缺失 | 侧项目产出要公开发布或商用前：邮件作者确认，或改用许可干净的替代并记录 |
| 4 | 遮挡/快速运动/多人的实际失败边界（官方未承认、未系统测试） | 侧项目出现对应素材时以实测代替引用；多人需求出现时优先验证 |
| 5 | TPAMI 2026 期刊版与会议版的差异（仅仓库描述字段提及） | 期刊版正式发表/仓库更新 README 时复查（架构、许可或权重可能变） |
| 6 | HF Space `LittleFrog/GVHMR` 账号归属（README 官方直链，但账号名非 zju3dv） | 想引用 HF demo 的在线行为作证据之前 |

## 8. 来源清单

检索日期均为 **2026-11-05**：

1. 官方仓库 README — https://github.com/zju3dv/GVHMR （取文：https://raw.githubusercontent.com/zju3dv/GVHMR/main/README.md）
2. 官方仓库 LICENSE — https://raw.githubusercontent.com/zju3dv/GVHMR/main/LICENSE
3. 安装文档 docs/INSTALL.md — https://raw.githubusercontent.com/zju3dv/GVHMR/main/docs/INSTALL.md
4. 依赖清单 requirements.txt（主链路版本 pin）— https://raw.githubusercontent.com/zju3dv/GVHMR/main/requirements.txt
5. demo 脚本 tools/demo/demo.py — https://raw.githubusercontent.com/zju3dv/GVHMR/main/tools/demo/demo.py
6. GitHub API 仓库元数据（描述/许可字段/星数快照）— https://api.github.com/repos/zju3dv/GVHMR
7. 论文摘要页 — https://arxiv.org/abs/2409.06662
8. 论文 HTML 全文（方法/指标/运行时 §4.4/结论细节）— https://arxiv.org/html/2409.06662v1
9. 官方项目页（模型前向 280 ms/训练口径）— https://zju3dv.github.io/gvhmr/
10. Issue #13（显存社区自测）— https://github.com/zju3dv/GVHMR/issues/13
11. Issue #10（Blender 导出，社区行为）— https://github.com/zju3dv/GVHMR/issues/10
12. SMPL-X 官网（注册与许可口径）— https://smpl-x.is.tue.mpg.de/
13. 官方 README 直链的在线 demo：Colab https://colab.research.google.com/drive/1N9WSchizHv2bfQqkE9Wuiegw_OT7mtGj · HuggingFace Space https://huggingface.co/spaces/LittleFrog/GVHMR （均经 README 引用，未单独访问核验内容）

## 修正记录

- **2026-11-05（独立核查后修正，仍为首建日）**：① §4 软件栈行——原写"主链路版本未逐项公布"不成立，requirements.txt 已逐项 pin 主链路（torch==2.3.0+cu121、ultralytics==8.2.42、timm==0.9.12、numpy==1.23.5、lightning==2.3.0、pytorch3d-0.7.6 等）；pytorch3d 官方 wheel 仅 `linux_x86_64`，补强 OS 行的 Windows 存疑（与 Linux 优先推断一致，非矛盾）。② §1/§2 的预处理 46 s 来源由项目页更正为论文 §4.4 Running Time（项目页全文无 "46"；论文口径含 DPVO 4.9+20.0+10.1+11.0，与仓库默认 SimpleVO 现状不同），项目页仅支撑 280 ms 口径。两条修正均经一手来源（requirements.txt 原文、论文 HTML、项目页全文检索）复核确认，无与一手来源矛盾之处。

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-gvhmr.md](deep/2026-11-05-gvhmr.md)
