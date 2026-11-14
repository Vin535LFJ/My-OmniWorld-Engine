# 智驾视觉方案现状调研：从环视渲染到世界模型（2026-11-08 检索）

> **Trust label: [Research → 方向裁决输入]**——本文为 owner 2026-11-08 提问（"把四周视频实时渲染到场景，本地能否实现"）的证据综述。**只调研、不裁决**：三条定位的取舍留给 grill → ADR-0015。
> **方法与 provenance**：主对话串行 WebSearch 取证 + **5 次逐页复核**（Inria LICENSE.md、arxiv 2308.01898 / 2503.20523 / 2208.14437 / 2203.17270 摘要逐句引用，2026-11-08）；**AVM 簇由后台研究代理完成并返回**（3.76M tokens / 51 次工具调用 / 19 分钟；期间因 provider 并发限制与过载两次重启）——该代理把 TI 白皮书 SPRY270A/SPRACB4、nuScenes 论文 PDF（Table 2）、WoodScape 论文 PDF（相机配置逐句）下载核验，本 note §1/§7 的 AVM 与数据集口径以它为准。**不是所有 URL 都做了逐页复核**：检索结果摘要与逐页复核分别标注；涉及逐字条款/精确数字而未逐页核对的，一律 **UNVERIFIED**。
> **与既有知识库的分工**：VGGT / Cosmos-3 / Isaac Sim / Genie-4 / OpenUSD / Rerun / Newton 的事实卡与深度报告**只链接不复述**（去重纪律见 [`projects/README.md`](projects/README.md)、[`deep/README.md`](deep/README.md)）。
> **治理背景**：本文踩三条红线（见 [CURRENT.md](../../CURRENT.md) "不碰"清单：多相机与位姿估计、3D 反投影与标定求解、GPU 渲染器）+ [PLAN.md](../../PLAN.md) ⑤"AD/ADAS 作为产品方向"让渡行（复议条件"证据变化才复议（Gate/ADR）"）——**本文就是那个证据**，未裁决前不动任何代码。

## 0. 结论摘要（先看这五条）

1. **owner 所述"四周视频实时渲染到场景"在量产车上是成熟技术**：环视 AVM/3D-AVM。3D 视图的真身是**预定义 bowl 曲面 + 双 LUT**（标定时一次生成 mesh table + blending LUT，逐帧纹理映射复用），**不是真三维重建**（TI SPRY270A/SPRACB4，PDF 逐句核验）；实测口径：4 路鱼眼合成 880×1080 鸟瞰 **30 fps**、bowl mesh 刷新 21 ms @ 750 MHz DSP（GPU 纹理渲染）。本地复现可行（有 MIT 开源实现），但缺多相机标定素材。
2. **智驾感知的"世界表达"（BEV/Occupancy/在线建图）是感知中间态，不是可编辑可复现的持久世界状态**——这是与 World Contract 的本质差别，也是 OmniWorld 楔子的证据（§2）。
3. **研发侧"视频 → 重建 → 自由视角渲染 → 闭环仿真"是当前工业与学术的主攻线**：UniSim（单条 log → 闭环多传感器仿真，CVPR 2023 最佳论文，Waabi）、GAIA-2（多相机可控生成世界模型，Wayve）、Waymo World Model（2026-02 官宣，基于 Genie 3）——**"视频源 = 真实世界 → 仿真世界"已是学界/工业界的显式方向**（§3/§4）。
4. **owner 的"相通性"命题在研究抽象层成立、在工程产物层不成立**：驾驶 VLA（LINGO-2 闭环上路）、具身 VLA（GR00T N1/π0）、机器人世界模型（1X）与驾驶世界模型（GAIA/Waymo）共用"视频→结构化状态→动作"骨架；但安全框架（ISO 26262/SOTIF）、实时口径、数据格式、评测协议两域割裂（§5）——**割裂正是机会位**。
5. **本地 8 GB 可行性分层明确**：AVM 拼接可行（缺多相机素材）；VGGT→gsplat 离线短片段可行（卡已核：≤20 帧 / 5.58 GB 档；gsplat==1.3.0 已核）；**重建结果实时渲染可行、流式实时重建不现实**（VGGT 无增量接口）；生成式世界模型不可行（Cosmos 16 GB / Genie 闭源）。**素材与许可决定形态**：四大驾驶数据集全部非商业（§7）——当前整个"视频→场景"链路只能是非商业本地研究形态。

## 1. 量产车上的环视 AVM / 3D-AVM

> 本节由后台研究代理完成（51 次工具调用），关键文档（TI SPRY270A/SPRACB4 白皮书 PDF、Kia 车主手册、OpenCV 官方文档）逐页核验；检索日期 2026-11-08。

**原理链**（四步，每步机制落到一手来源）：

| 机制 | 做什么 | 来源 |
|---|---|---|
| 鱼眼标定 | 4 个 ~190° hFOV 鱼眼覆盖 360°（Valeo 综述 §III）；畸变模型三套可选：OpenCV `cv::fisheye`（Kannala-Brandt 多项式）、`omnidir`（Mei 统一球面模型）、NVIDIA DriveWorks 相机模型（Pinhole / F-Theta 逆多项式 / Stereographic，rig 配置文件多态初始化）+ 官方标定引擎（feature-based 自标定 pitch/yaw/roll/height） | [arxiv.org/html/2205.13281v2](https://arxiv.org/html/2205.13281v2)（Valeo 综述）、[docs.opencv.org fisheye](https://docs.opencv.org/4.x/db/d58/group__calib3d__fisheye.html)、[docs.opencv.org omnidir](https://docs.opencv.org/4.x/dd/d12/tutorial_omnidir_calib_main.html)、[DriveWorks 相机模型](https://developer.nvidia.com/docs/drive/drive-os/6.0.6/public/driveworks-nvsdk/group__cameramodel__group.html)、[DriveWorks 标定](https://developer.nvidia.com/docs/drive/drive-os/6.0.5/public/driveworks-nvsdk/dwx_camera_calibration.html) |
| 逆透视变换 IPM | 鱼眼畸变校正（LDC）+ 透视变换**合并成一张 LUT**（对畸变函数施加逆变换后同时估计四相机各自的透视矩阵）；平地假设，地面不平时产生伪影 | [TI SPRY270A](https://www.ti.com/lit/wp/spry270a/spry270a.pdf)（PDF 逐句 ✅）、Valeo 综述 |
| 光度对齐与拼接 | 不同相机 AE/AWB 差异 → 按颜色通道独立 tone mapping；重叠区 alpha-blending（blending LUT 标定时一次生成）；光度统计在 DSP 上每帧仅 3% 算力（错帧一帧收集以省带宽） | TI SPRY270A ✅ |
| **3D 视图合成** | **预定义 bowl 曲面**（车近处平面、远处上翘）+ mesh table 预映射到鱼眼像素 + blending LUT——启动时一次生成、逐帧纹理映射复用（GPU）；可任意切视点。**是"车模 + 曲面投影"，非真三维重建** | [TI SPRACB4](https://www.ti.com/lit/pdf/spracb4) ✅、Kia 车主手册（3D 视图多角度，≤10 km/h 激活：[kia.com](https://www.kia.com/content/dam/kia2/in/en/content/seltos-manual/topics/chapter6_12_2.html)） |

**鬼影（ghost/moving object）**：量产管线（TI）**无专项处理**——依赖重叠区 alpha 混合权重；机理与缓解在学术侧：3D bowl 拓扑本身缓解 2D 拼接的边缘拉伸/鬼影（[github.com/nick8592/Open-3D-Surround-View](https://github.com/nick8592/Open-3D-Surround-View) README 以此为切换 3D 的动机）、深度估计把 bowl 表面在近旁车辆处前移（Valeo 综述）、运动物体检测（FisheyeMODNet，环视鱼眼 MOD，1 TFLOPS 嵌入式 15 fps：[arxiv.org/abs/1908.11789](https://arxiv.org/abs/1908.11789)）。标题明确的"AVM ghost removal"论文**未找到**（专利层为主）→ **UNVERIFIED**。
**AI 增强（已核的公开证据）**：OmniDet（IROS 2021，Valeo）未矫正鱼眼上一次输出 6 任务、距离图 3D 空间后处理得环视几何（[arxiv.org/abs/2102.07448](https://arxiv.org/abs/2102.07448)）；FisheyeMultiNet 4 相机共享编码器嵌入式 15 fps（综述 §IV-D）；NVIDIA DRIVE Labs 6 相机 360° 检测跟踪 + 唯一 ID + TTC（[官方博客 2019](https://developer.nvidia.com/blog/drive-labs-tracking-objects-with-surround-camera-vision/)）；JLR 透明引擎盖为概念车静态投影（[官方新闻稿 2014](https://media.jlr.com/corporate/news/2014/04/land-rover-unveils-pioneering-discovery-vision-concept)）。**"周围车辆行人实时投影渲染进量产 AVM"无一手量产文档 → UNVERIFIED**（最接近的是 OmniDet 后处理与 FPD/FPNet 泊车 BEV 检测线：[arxiv.org/abs/2212.04111](https://arxiv.org/abs/2212.04111)）。
**实时性（已核数字）**：4 路合成 880×1080 鸟瞰 **30 fps**、管线内一帧错帧设计（SPRY270A ✅）；bowl mesh 刷新 **21 ms @ 750 MHz DSP**（GPU 双线程并行化，SPRACB4 ✅）；OmniDet Jetson AGX TensorRT FP16 **60 fps**（Table II）。**端到端 ms 延迟无任何一手来源 → UNVERIFIED**。
**开源可参考**：[nick8592/Open-3D-Surround-View](https://github.com/nick8592/Open-3D-Surround-View)（**MIT**，Blender 4 鱼眼全管线：标定含 ISO 8855 外参、2D BEV LUT ~74 FPS、3D bowl 修鬼影、OpenGL/GLSL）；[neozhaoliang/surround-view-system-introduction](https://github.com/neozhaoliang/surround-view-system-introduction)（**MIT**，真实车辆 Python：棋盘格标定 → IPM → 混合权重 → 实时）。工业级 C++ 完整链开源项目**未发现 → UNVERIFIED**。
**对本项目**：AVM 就是"视频→可测量世界状态"的**最小量产实例**——标定 LUT + blending LUT 即 identity/ownership 的几何承诺，30fps/21ms 给出时间轴基准；**confidence 被藏在标定时刻的一次性假设里**（平地/静态世界/固定位姿），鬼影与近旁伪影正是该假设失效的表现——综述指向"深度自适应 bowl 形状"，恰是 World Contract 显式替代静态先验的切入口。

## 2. 智驾感知的世界表达（BEV / Occupancy / 在线建图）

| 工作 | 机制（逐页复核 ✅） | 来源（检索 2026-11-08） |
|---|---|---|
| **LSS**（ECCV 2020，NVIDIA） | Lift：每像素深度分布 × 图像特征 → 视锥特征；Splat：pillar pooling 投到 BEV 网格；Shoot：运动探针出轨迹 | [arxiv.org/abs/2008.05711](https://arxiv.org/abs/2008.05711) |
| **BEVFormer**（ECCV 2022） | 网格状 BEV query；**spatial cross-attention** 跨相机采样；**temporal self-attention** 递归融合历史 BEV；nuScenes test 56.9% NDS | [arxiv.org/abs/2203.17270](https://arxiv.org/abs/2203.17270)（摘要逐句 ✅） |
| **Occupancy** | Tesla AI Day 2022 提出：稠密体素占用+语义替代 3D 框；学术基准 Occ3D（NeurIPS 2023，[arxiv.org/abs/2304.14365](https://arxiv.org/abs/2304.14365)）与 OpenOccupancy（ICCV 2023）；综述 [arxiv.org/abs/2405.02595](https://arxiv.org/abs/2405.02595) | 检索摘要（Occ3D/OpenOccupancy 未逐页） |
| **MapTR**（ICLR 2023 Spotlight） | 在线矢量化建图：地图元素 = 点集合 + **置换等价**建模；纯相机输入；25.1 FPS @ RTX 3090 | [arxiv.org/abs/2208.14437](https://arxiv.org/abs/2208.14437)（摘要逐句 ✅；注意：常被误记的 2301.05736 不是 MapTR） |

**许可（复查项）**：BEVFormer 存在 `zhiqi-li/BEVFormer` 与 `fundamentalvision/BEVFormer` 两个官方仓库名，许可口径冲突（arxiv 页备注 CC BY 4.0 vs 搜索摘要 Apache-2.0）→ **UNVERIFIED**；MapTR `hustvl/MapTR` Apache-2.0（搜索摘要，未逐页）→ **UNVERIFIED**。任何复用前逐仓库核对 LICENSE。
**对 World Contract 的意义**：BEV 特征图/体素/矢量线是**感知中间态**——无身份持久性、无置信度语义外泄、不可复现（依赖模型权重与随机性）。它们证明"多相机 → 结构化状态"是工业共识，但产物不是契约④⑤意义上的世界状态。**这个落差就是楔子。**

## 3. 驾驶场景重建与渲染（研发侧）

**3DGS 本体**（逐页复核 ✅，[arxiv.org/abs/2308.04079](https://arxiv.org/abs/2308.04079) + [Inria LICENSE.md](https://github.com/graphdeco-inria/gaussian-splatting/blob/main/LICENSE.md)，2026-11-08）：各向异性 3D 高斯 + tile-based 可微光栅化（非光线步进）+ 自适应密度控制（分裂/克隆/剪枝）；1080p 实时（≥30–100+ fps，论文口径）。**许可：研究/评估免费，商用需 Inria 明示同意，不可转授**（LICENSE.md 逐字 ✅）。算法本身无专利壁垒 → 独立实现合法——**gsplat（Apache-2.0，本仓已核 gsplat==1.3.0）正是此路径**（[research-queue gsplat 行](research-queue.md)）。

驾驶场景变体（论文+仓库存在性核验；**各仓库 LICENSE 未逐页 → UNVERIFIED**）：

| 工作 | 机制一句话 | 来源 |
|---|---|---|
| Street Gaussians（ECCV 2024，理想×浙大） | 背景 + 跟踪前景车辆（各自可优化位姿）的点基高斯分解 | [arxiv.org/abs/2401.01339](https://arxiv.org/abs/2401.01339) |
| DrivingGaussian（CVPR 2024） | 首个环视动态驾驶场景复合高斯框架（静态背景 + 多动态物分解） | [arxiv.org/abs/2312.07920](https://arxiv.org/abs/2312.07920)、[github.com/VDIGPKU/DrivingGaussian](https://github.com/VDIGPKU/DrivingGaussian) |
| OmniRe（2024） | 设备 log → 高保真动态城市数字孪生的完整 3DGS 系统 | [arxiv.org/abs/2408.16760](https://arxiv.org/abs/2408.16760) |
| PVG（IJCV 2026 扩展） | 周期振动时间建模，**无需物体位姿/跟踪**处理动态，实时渲染 | [arxiv.org/abs/2311.18561](https://arxiv.org/abs/2311.18561)、[github.com/fudan-zvg/PVG](https://github.com/fudan-zvg/PVG) |

**NeRF 对照线**：**UniSim**（CVPR 2023 最佳论文，Waabi）——摘要逐句 ✅："**单条 log → 闭环多传感器仿真**"，neural feature grids 重建静态背景+动态演员，LiDAR+相机新视角合成，**演员可增删/重摆**，闭环评测安全关键场景（[arxiv.org/abs/2308.01898](https://arxiv.org/abs/2308.01898)，2026-11-08）；MARS（实例可编辑模块化，[arxiv.org/abs/2307.15058](https://arxiv.org/abs/2307.15058)）；EmerNeRF（NVIDIA，静态/动态自监督分解，开源 [github.com/NVlabs/EmerNeRF](https://github.com/NVlabs/EmerNeRF)）。**UniSim/Waymo sensor-sim 未开源；EmerNeRF 已开源；MARS 受限**（检索摘要口径，2026-11-08）。

**用途链（论文口径）**：重建结果 → 传感器仿真 / 闭环仿真评测（UniSim 摘要 ✅）/ 安全关键场景生成（GAIA-2 摘要 ✅）。
**对本项目**：这条线正是"视频 → 重建 → 渲染"的研究形态；仓库已核验的 VGGT→COLMAP→gsplat 链路（[projects/vggt.md](projects/vggt.md) §5）与 OmniRe/Street Gaussians 的"log → 数字孪生"同构，差别在动态物体建模（它们有跟踪位姿先验，VGGT 只给几何+轨迹）。

## 4. 闭环仿真与数据闭环

| 项 | 现状（检索 2026-11-08） | 来源 |
|---|---|---|
| **CARLA** | 0.10.0（2024-12），迁移到 **UE 5.5**；代码 MIT、资产 CC-BY（官方 issue #7984 口径） | [carla.org](https://carla.org)、[github.com/carla-simulator/carla](https://github.com/carla-simulator/carla) |
| **NeuroNCAP**（CVPR 2024） | NeRF 类照片级**闭环安全测试**：对端到端智驾软件注入安全关键事件 | [arxiv.org/abs/2404.07762](https://arxiv.org/abs/2404.07762)、[github.com/atonderski/neuro-ncap](https://github.com/atonderski/neuro-ncap) |
| **Waymax**（Waymo） | JAX 编写的轻量多智能体行为仿真，基于 Waymo Open Motion Dataset，GPU/TPU in-graph 加速 | [github.com/waymo-research/waymax](https://github.com/waymo-research/waymax) |
| **NAVSIM** | 数据驱动非反应式仿真：开环评测的易用性 + 闭环启发的 PDMS 指标；v2 = 伪仿真 | [arxiv.org/abs/2406.15349](https://arxiv.org/abs/2406.15349)、[github.com/autonomousvision/navsim](https://github.com/autonomousvision/navsim) |
| **NVIDIA 线** | Cosmos Sim-Auto（卡已核：官方最低平台 16 GB Linux → 不在本机 8 GB 线内）+ Isaac Sim（卡已核 16 GB） | [projects/cosmos-3.md](projects/cosmos-3.md)、[projects/isaac-sim.md](projects/isaac-sim.md) |
| **Waymo World Model** | 2026-02-06 官宣：生成式驾驶仿真前沿模型，**基于 Google DeepMind Genie 3** 技术，photorealistic 交互式 3D 驾驶环境（含龙卷风级罕见场景）；官方博客为唯一一手来源，**无独立论文 → 技术细节 UNVERIFIED** | [waymo.com/blog/2026/02](https://waymo.com/blog/2026/02/the-waymo-world-model-a-new-frontier-for-autonomous-driving-simulation) |

**格局判断**：闭环仿真已分化为三路线——规则引擎式（CARLA）、数据驱动轻量式（Waymax/NAVSIM）、神经重建/生成式（UniSim/NeuroNCAP/Waymo WM/Cosmos）。第三条线全部需要 16 GB+ 或闭源——**本地 8 GB 在这条线上只能做"离线重建 + 回放渲染 + 开环评测"**。
**Genie-4 交叉印证**：Waymo WM 基于 Genie 3，与 [projects/genie-4.md](projects/genie-4.md) §6"官方混合世界模型方向"互证——生成式仿真与确定性测量的分工正在成为共识（§5）。

## 5. 与具身智能视觉 / VLA 的相通性（owner 命题的证据检验）

> 命题（owner 2026-11-08 原话）："跟本项目想法是一致的，都是把视频源理解成真实仿真世界，跟具身智能的视觉、VLA 是相通的。"**按纪律正反证据并列，结论不预设。**

### 5.1 支持证据

| 证据 | 机制口径 | 来源（检索 2026-11-08） |
|---|---|---|
| **GAIA-1**（Wayve） | **video + text + action 输入 → 生成驾驶场景**：世界模型直接以视频为世界源、以动作为条件 | [arxiv.org/abs/2309.17080](https://arxiv.org/abs/2309.17080) |
| **GAIA-2**（Wayve） | latent diffusion 世界模型统一**多相机**视角生成；条件化 = ego 动力学 + agent 配置 + 环境因素 + 路面语义（摘要逐句 ✅） | [arxiv.org/abs/2503.20523](https://arxiv.org/abs/2503.20523) |
| **LINGO-2**（Wayve） | **首个公开道路闭环测试的 vision-language-action 驾驶模型**：视觉→语言→行为单模型 | [wayve.ai/press/introducing-lingo-2](https://wayve.ai/press/introducing-lingo-2) |
| **VLA 迁移综述** | Wayve 自己发表"Vision-Language-Action Models for Autonomous Driving"综述（2025-12）——领域自认 VLA 范式向驾驶迁移 | wayve.ai（检索摘要） |
| **UniAD**（CVPR 2023 最佳论文） | 感知/跟踪/建图/预测/规划统一网络，规划导向——驾驶被当作端到端智能体问题 | [arxiv.org/abs/2212.10156](https://arxiv.org/abs/2212.10156)、[github.com/OpenDriveLab/UniAD](https://github.com/OpenDriveLab/UniAD) |
| **GR00T N1**（NVIDIA） | 人形 VLA 基础模型：**System1/System2 双系统**，训练数据含**自我视角人类视频**——视频=技能世界源 | [arxiv.org/abs/2503.14734](https://arxiv.org/abs/2503.14734) |
| **π0**（Physical Intelligence） | VLM 骨干 + flow matching 动作专家；开源 openpi | [arxiv.org/abs/2410.24164](https://arxiv.org/abs/2410.24164) |
| **1X World Model** | 人形机器人世界模型挑战：**视频采样 + 动作预测**双赛道——视频当世界模型训练/评测源 | [arxiv.org/abs/2510.07092](https://arxiv.org/abs/2510.07092) |
| **Waymo World Model × Genie 3** | 驾驶仿真直接采用通用世界模型技术栈——两域技术融合的显式案例 | §4 来源 |

### 5.2 限定/反驳证据

| 反例 | 机制 | 来源 |
|---|---|---|
| **安全框架不对称** | 驾驶受 ISO 26262（S/E/C 风险基线）+ SOTIF/ISO 21448 + UL 4600 约束，ML 的非确定性冲击经典假设；操纵臂机器人**无对应功能安全框架** | [arxiv.org/html/2606.07437v1](https://arxiv.org/html/2606.07437v1)、[Martin et al. (Springer)](https://ivds.dependability.org/wg10.4/ivdswiki/images/6/62/SPRINGER16..pdf)（2026-11-08） |
| **实时口径不同级** | 驾驶 = 硬实时 10–100 Hz 规划环 + 毫秒级确定性延迟 + ASIL 兜底架构；机器人操纵常 1–20 Hz/事件触发，世界模型当"策略改进的学习仿真器"用 | [the-autonomous 安全架构报告 2nd ed.](https://www.the-autonomous.com/wp-content/uploads/2025/09/the-autonomous-safetyarchitecture-fullreport-2nd-edition-2025.pdf)、操纵世界模型综述（检索摘要，2026-11-08） |
| **表达产物割裂** | AD 的世界状态产物（BEV 特征/体素/矢量图）与具身的（策略输入嵌码/场景表示）没有共享标准——两域各有一套世界模型综述（驾驶 [arxiv.org/abs/2403.02622](https://arxiv.org/abs/2403.02622)；操纵侧另立），无跨域协议；与队列已记录的 WBench"no shared protocol"同构 | 上述综述 + [research-queue.md](research-queue.md) WorldMark 行 |
| **工程现实差异** | VGGT 每场景一次前馈、无增量接口（深度报告 §2.3 已核）——"视频→世界状态"在两域的实时闭环都未解决；Cosmos/Isaac 16 GB 把具身仿真挡在本地之外（卡已核） | [deep/2026-11-05-vggt.md](deep/2026-11-05-vggt.md)、[projects/cosmos-3.md](projects/cosmos-3.md)、[projects/isaac-sim.md](projects/isaac-sim.md) |

### 5.3 裁定建议（证据综述，非裁决）

**命题在研究抽象层成立**：视频→结构化状态→动作的骨架在两域同构，且 2023–2026 工业界在**显式收敛**（Wayve 把驾驶当世界模型/VLA 问题做；Waymo 用 Genie 3 做驾驶仿真；GR00T/1X 把视频当技能世界源）。
**命题在工程产物层不成立**：安全等级、实时口径、数据格式、评测协议四项两域割裂。
**对 OmniWorld 的含义**：割裂不是坏消息——"可测量的共享世界状态层"恰好站在割裂带上（World Contract 四语义两域通用，评测 harness 两域可复用）；但"相通"是**抽象层叙事**，不能直接当成工程可复用性的证据——A 面板（COCO）与 B 面板（驾驶 log）的评分口径迁移需要实证，不是叙述。

## 6. 本地 8 GB 可行性对照表

| 层 | 显存/算力 | 本机可行性 | 依据 |
|---|---|---|---|
| AVM 环视拼接 | 传统 CV，GPU 纹理即可（TI 报告） | **可行**；缺多相机标定素材 | §1 |
| BEV/Occupancy 推理 | 单模型推理 8 GB 内可运行（量级推断） | 推理可行（未实测）；训练不可行 | §2；⚠️ 推断标 UNVERIFIED |
| VGGT 前馈几何 | ≤20 帧 / 5.58 GB（修复前基准档） | **可行（短片段离线）** | [projects/vggt.md](projects/vggt.md) §2.2（已核） |
| VGGT→COLMAP→gsplat 训练 | gsplat==1.3.0，小场景短片段 | **可行（离线）**；训练规模上限未实测 | [research-queue.md](research-queue.md) gsplat 行（已核） |
| 3DGS 回放渲染 | 1080p ≥30–100+ fps（论文口径，普通场景） | **实时可行** | §3（arxiv 2308.04079 ✅） |
| 流式实时重建 | VGGT 无增量/流式接口，滑窗重推理代价未验证 | **不现实（v1 不承诺）** | [deep/2026-11-05-vggt.md](deep/2026-11-05-vggt.md) §2.3（已核） |
| 生成式世界模型 | Cosmos 16 GB（Linux）/ Genie 闭源 | **不可行** | [projects/cosmos-3.md](projects/cosmos-3.md)、[projects/genie-4.md](projects/genie-4.md)（已核） |

## 7. 数据素材与许可（两条路线共同的先决条件）

| 数据集 | 配置 | 许可 | 来源（检索 2026-11-08） |
|---|---|---|---|
| **WoodScape**（Valeo，ICCV 2019） | **4 鱼眼环视**（1MP 30fps 未压缩视频、10 万+ 帧标注、9 任务；标定文件已随仓库发布）——AVM 复现首选 | **代码 MIT；数据 Proprietary**（README 原文 ✅，指向 Google Drive 条款文档——**条款原文 UNVERIFIED**，本环境抓不了 Drive；**此前搜索摘要口径"CC BY-NC-SA"有误，已纠正**） | [arxiv.org/abs/1905.01489](https://arxiv.org/abs/1905.01489)（PDF 相机配置逐句 ✅）、[github.com/valeoai/woodscape](https://github.com/valeoai/woodscape) |
| **SynWoodScape**（CARLA 合成伴随集） | 与 WoodScape 同配置：**4 鱼眼 × 190°**，80k 图、10+ 任务标注（含**像素级光流/深度真值**——可用于拼接几何的定量评测，真实数据没有这些真值） | **UNVERIFIED**（无官方仓库可查 LICENSE，仅 arXiv Comments 区 Google Drive 样例链接） | [arxiv.org/abs/2203.05056](https://arxiv.org/abs/2203.05056) |
| **FPD**（Fisheye Parking Dataset，IEEE T-IV 2022） | 鱼眼泊车 BEV 3D 检测 + 实时抗畸变多任务框架 FPNet | 公开性 **UNVERIFIED** | [arxiv.org/abs/2212.04111](https://arxiv.org/abs/2212.04111) |
| **nuScenes**（Motional） | **6 个定向针孔相机**（70°/110° FOV，12Hz，1600×900）+ LiDAR + 雷达——**不适合 AVM 拼接复现**（有覆盖缺口、非鱼眼；定位是 BEV 检测/融合），重建线可用 | 官方 FAQ 已核 ✅："非商业免费、商业需联系授权"；**具体协议名 UNVERIFIED**（terms 页 JS 渲染抓取失败） | [arxiv.org/abs/1903.11027](https://arxiv.org/abs/1903.11027)（PDF Table 2 ✅）、[devkit FAQ](https://github.com/nutonomy/nuscenes-devkit/blob/master/docs/faqs.md) |
| **Waymo Open Dataset** | 高保真 log（4DGS 线主力） | 自有**非商业**条款（SDK 代码 Apache-2.0，数据非商业） | [waymo.com/open/about](https://waymo.com/open/about) |
| **Argoverse 2** | 环视 log + SWF 流（重建线常用） | **CC BY-NC-SA 4.0**（代码 MIT） | [argoverse.org/av2.html](https://argoverse.org/av2.html) |
| Sintel trailer（已入库工具） | 动画 854×480，非行车 | CC BY 3.0 ✅（D5 已核） | [tools/fetch_video_sample.mjs](../../tools/fetch_video_sample.mjs) |
| COCO 子集（已入库工具） | 非行车，观察空间评分已用 | CC BY 4.0 ✅（D1 已核） | [tools/fetch_coco_subset.mjs](../../tools/fetch_coco_subset.mjs) |

**关键判断**：两条路线素材都能以非商业/研究许可获得——**AVM 复现锚点 = WoodScape（真实量产同构配置）+ SynWoodScape（像素级真值补定量评测）**；重建渲染锚点 = Argoverse 2/Waymo log。但整链许可收口是 **NC/Proprietary**（素材 × VGGT checkpoint NC）→ **"视频→场景渲染"当前只能定位为非商业研究/作品集形态**，且**数据不可再分发**（WoodScape 专有条款）——与仓库"数据不入库、脚本+清单入库"纪律完全一致。商业主张需要另行许可路径（自采数据 + gsplat 替代 Inria 实现）。
**AVM 特有约束**：单目行车记录仪**做不了** AVM（需要同步多相机+标定）——公开数据集是唯一本地路径。

## 8. 方向裁决输入（证据综述，非裁决——留给 grill → ADR-0015）

| 定位选项 | 证据面 | 代价/风险 |
|---|---|---|
| **① 通用"视频→场景渲染"能力进场，AD 视频是第一个数据域** | VGGT→gsplat 链路已核且触发条件命中（gsplat 队列行）；评测 harness/契约零改动（素材与 D2/D4 同构：log 当观察、重建当真值候选）；**字面上不触碰⑤**（做的不是"AD 产品"而是引擎能力）；与 UniSim/OmniRe 同构的研究位 | 素材全 NC → 成果形态是非商业研究/作品集；动态物体建模（OmniRe 类）超出 VGGT 输出，需再调研；不碰清单三条仍需 ADR 解封 |
| **② 直接以智驾产品定位** | §3/§4 的 SOTA 收敛（重建→仿真评测是工业主攻线）；GAIA-3/waymo WM 验证"评测是落地形态" | **重开⑤让渡**（owner 亲自定的决策）；16 GB+ 竞争对手压顶（Cosmos/Isaac/Waymo WM）；本地 8 GB 只能做离线小场景，产品叙事撑不住；与目标函数（磨练技术≳敲门砖≫创业）的创业层错位 |
| **③ 归族谱③技能项目座位** | 族谱③原文首选候选就是"离屏 Vulkan 观察渲染器"；3DGS/Vulkan 学习价值与敲门砖叙事契合；不动主线排期 | 不解决"第一应用"的悬而未决；gsplat 外部工具链与 C++ 主线的接缝无人定义 |

**证据倾向（置信度低→中，最终裁决归 owner）**：①+③ 组合最贴既有治理——主线以"视频→场景渲染的**可测量评测**"吸收（第一应用叙事延续 ADR-0008"评测是第一个应用"），渲染器实现学习归③座位；②需要的能力（16 GB+ 生成式仿真）与 owner 资源现实冲突最大。**grill 需要回答的核心问题**：如果做①，第一块可测量里程碑是什么（建议候选：一段 Argoverse 2 log → VGGT → gsplat → 自由视角渲染序列 + PSNR/SSIM/延迟进回归报告）；"不碰"清单三条如何按 ADR 解封；AVM 路线是否降为②数据准备的一部分。

## 9. UNVERIFIED 汇总与复查触发

1. **WoodScape 数据专有条款原文**（Google Drive 协议文档本环境不可抓取；README 只标 "Proprietary"）——复查触发：素材下载前人工读条款；商用前必须全文确认。
2. **nuScenes 具体协议名**（非商业免费已核 ✅，协议名未核；官方 terms 页 JS 渲染 + SSL 拒绝）——复查触发：素材下载前。
3. **SynWoodScape 许可与官方下载主渠道**（无官方仓库）——复查触发：采用前。
4. **"周围车辆行人实时投影渲染进量产 AVM"**（透明底盘动态版）：无一手量产文档（JLR 为概念车静态投影；特斯拉 High-Fidelity Park Assist 无公开技术文档）——复查触发：AVM 路线立项时。
5. **AVM 专项 ghost-removal 论文**（鬼影证据只到"bowl 拓扑缓解 + 运动物体检测"层；Eker 2022 PDF 损坏未核）——复查触发：AVM 路线立项时。
6. **工业级 C++ 开源 AVM 完整链**（现仅 Python 教学级 MIT×2）——复查触发：AVM 路线立项时。
7. **AVM 端到端 ms 延迟**（无一手来源；仅有 30fps/21ms/一帧错帧口径）——复查触发：spike 设计前。
8. BEVFormer（双仓库名许可冲突：CC BY 4.0 vs Apache-2.0）/ MapTR / DrivingGaussian / PVG / Street Gaussians / OmniRe 各仓库 LICENSE——复查触发：任何代码复用前。
9. BEV/Occupancy 推理在 8 GB 的实测——复查触发：spike 设计前。
10. Waymo World Model 技术细节（无论文）与 Tesla Occupancy 官方一手文本——复查触发：各自技术报告/文档发布时。

## 10. 来源清单（检索日期均为 2026-11-08）

逐页复核（✅）：[Inria LICENSE.md](https://github.com/graphdeco-inria/gaussian-splatting/blob/main/LICENSE.md)、[arxiv 2308.01898](https://arxiv.org/abs/2308.01898)（UniSim）、[arxiv 2503.20523](https://arxiv.org/abs/2503.20523)（GAIA-2）、[arxiv 2208.14437](https://arxiv.org/abs/2208.14437)（MapTR）、[arxiv 2203.17270](https://arxiv.org/abs/2203.17270)（BEVFormer）。
检索摘要引用：[arxiv 2008.05711](https://arxiv.org/abs/2008.05711)（LSS）、[arxiv 2304.14365](https://arxiv.org/abs/2304.14365)（Occ3D）、[ICCV 2023 OpenOccupancy](https://openaccess.thecvf.com/content/ICCV2023/papers/Wang_OpenOccupancy_A_Large_Scale_Benchmark_for_Surrounding_Semantic_Occupancy_Perception_ICCV_2023_paper.pdf)、[arxiv 2405.02595](https://arxiv.org/abs/2405.02595)（occupancy 综述）、[arxiv 2401.01339](https://arxiv.org/abs/2401.01339)（Street Gaussians）、[arxiv 2312.07920](https://arxiv.org/abs/2312.07920)（DrivingGaussian）、[arxiv 2408.16760](https://arxiv.org/abs/2408.16760)（OmniRe）、[arxiv 2311.18561](https://arxiv.org/abs/2311.18561)（PVG）、[arxiv 2307.15058](https://arxiv.org/abs/2307.15058)（MARS）、[github.com/NVlabs/EmerNeRF](https://github.com/NVlabs/EmerNeRF)、[carla.org](https://carla.org)、[arxiv 2404.07762](https://arxiv.org/abs/2404.07762)（NeuroNCAP）、[github.com/waymo-research/waymax](https://github.com/waymo-research/waymax)、[arxiv 2406.15349](https://arxiv.org/abs/2406.15349)（NAVSIM）、[arxiv 2309.17080](https://arxiv.org/abs/2309.17080)（GAIA-1）、[wayve.ai/press/introducing-lingo-2](https://wayve.ai/press/introducing-lingo-2)（LINGO-2）、[arxiv 2212.10156](https://arxiv.org/abs/2212.10156)（UniAD）、[arxiv 2503.14734](https://arxiv.org/abs/2503.14734)（GR00T N1）、[arxiv 2410.24164](https://arxiv.org/abs/2410.24164)（π0）、[arxiv 2510.07092](https://arxiv.org/abs/2510.07092)（1X WM）、[arxiv 2403.02622](https://arxiv.org/abs/2403.02622)（驾驶世界模型综述）、[argoverse.org/av2.html](https://argoverse.org/av2.html)、[waymo.com/open/about](https://waymo.com/open/about)、[waymo.com/blog/2026/02](https://waymo.com/blog/2026/02/the-waymo-world-model-a-new-frontier-for-autonomous-driving-simulation)、[arxiv 2606.07437](https://arxiv.org/html/2606.07437v1)、[the-autonomous 安全架构报告](https://www.the-autonomous.com/wp-content/uploads/2025/09/the-autonomous-safetyarchitecture-fullreport-2nd-edition-2025.pdf)、[Martin et al.](https://ivds.dependability.org/wg10.4/ivdswiki/images/6/62/SPRINGER16..pdf)。
AVM 簇（后台代理逐页/逐 PDF 核验）：[TI SPRY270A](https://www.ti.com/lit/wp/spry270a/spry270a.pdf)、[TI SPRACB4](https://www.ti.com/lit/pdf/spracb4)、[arxiv 2205.13281](https://arxiv.org/html/2205.13281v2)（Valeo 环视综述）、[OpenCV fisheye](https://docs.opencv.org/4.x/db/d58/group__calib3d__fisheye.html)、[OpenCV omnidir](https://docs.opencv.org/4.x/dd/d12/tutorial_omnidir_calib_main.html)、[DriveWorks 相机模型](https://developer.nvidia.com/docs/drive/drive-os/6.0.6/public/driveworks-nvsdk/group__cameramodel__group.html)、[DriveWorks 标定](https://developer.nvidia.com/docs/drive/drive-os/6.0.5/public/driveworks-nvsdk/dwx_camera_calibration.html)、[DriveWorks Samples](https://developer.nvidia.com/docs/drive/drive-os/6.0.10/public/driveworks-nvsdk/nvsdk_dw_html/dwx_samples_section.html)、[arxiv 2102.07448](https://arxiv.org/abs/2102.07448)（OmniDet）、[arxiv 1908.11789](https://arxiv.org/abs/1908.11789)（FisheyeMODNet）、[DRIVE Labs 环视跟踪博客](https://developer.nvidia.com/blog/drive-labs-tracking-objects-with-surround-camera-vision/)、[JLR 透明引擎盖新闻稿](https://media.jlr.com/corporate/news/2014/04/land-rover-unveils-pioneering-discovery-vision-concept)、[Kia SVM 车主手册](https://www.kia.com/content/dam/kia2/in/en/content/seltos-manual/topics/chapter6_12_2.html)、[arxiv 2212.04111](https://arxiv.org/abs/2212.04111)（FPD/FPNet）、[github.com/nick8592/Open-3D-Surround-View](https://github.com/nick8592/Open-3D-Surround-View)（MIT）、[github.com/neozhaoliang/surround-view-system-introduction](https://github.com/neozhaoliang/surround-view-system-introduction)（MIT）、[github.com/valeoai/woodscape](https://github.com/valeoai/woodscape)、[arxiv 2203.05056](https://arxiv.org/abs/2203.05056)（SynWoodScape）、[arxiv 1903.11027](https://arxiv.org/abs/1903.11027)（nuScenes 论文）、[nuScenes devkit FAQ](https://github.com/nutonomy/nuscenes-devkit/blob/master/docs/faqs.md)。
仓库内证据（只链接）：[projects/vggt.md](projects/vggt.md)、[projects/cosmos-3.md](projects/cosmos-3.md)、[projects/isaac-sim.md](projects/isaac-sim.md)、[projects/genie-4.md](projects/genie-4.md)、[deep/2026-11-05-vggt.md](deep/2026-11-05-vggt.md)、[research-queue.md](research-queue.md)、[third-party-research-map.md](third-party-research-map.md)、[PLAN.md](../../PLAN.md) §二、[CURRENT.md](../../CURRENT.md)。
