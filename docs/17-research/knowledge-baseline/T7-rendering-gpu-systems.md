# T7 渲染管线与 GPU 系统 — 知识基线（2026-11-13）

> Trust label: [Research → 知识基线] —— 本 note 为方向评审（grill → ADR-0016）提供 T7 主题的知识地基：系统梳理实时渲染管线、Vulkan 离屏渲染、GPU 数据流、GPU 确定性/重放、GPU 可观测性与混合渲染架构的**原理与进度**。纪律：只调研不裁决；与 OmniWorld 的关联只写"关联性"（L4 表现后端、GPU Observability 横向层、契约⑨证据通道、位级复现 oracle），不写"应该做 X"。数字纪律：未一手核验的标 **未核**；媒体/第三方转述不得写成官方口径；仓库既有实测数字注明来自仓库 benchmarks 口径。检索/核验日期：2026-11-13（另注明者除外）。

---

## 1. 原理与技术路线分类

### 1.1 三种渲染机制：光栅化 / 路径追踪 / 神经渲染

**光栅化（rasterization）**——把几何基元（三角形）投影到屏幕，逐基元覆盖像素并着色。管线阶段：顶点变换 → 图元装配/裁剪 → 光栅化（覆盖判定）→ 片元着色 → 深度测试/混合 → 输出。核心特征是**代价与屏幕像素数和基元数近似线性**、高度并行、每帧结果由输入几何+着色参数完全决定。这带来两个对本项目重要的性质：
- **确定性友好**：固定管线状态下，逐像素的着色计算是局部、无跨像素归约的；深度测试的仲裁顺序由 API 规定（同一 primitive 序）。非确定性主要来自浮点插值/着色器编译差异与跨 draw 的混合顺序，而非算法结构本身（机制性陈述，属图形学通识；Vulkan 规范逐字依据本次未抓取，标 **未核**）。
- **帧预算可控**：draw call 数、overdraw、分辨率是三个主要旋钮。仓库六方向 note §D6 引用的 Saltline 性能纪律（材质烘焙成纹理缓存复用、Triplanar 免逐模型 UV、按材质合并几何把枪械压到 4–7 draw call）就是光栅化预算管理的实例（仓库已核口径，一手镜像证据见第一轮 note）。

**路径追踪（path tracing）**——从相机逐像素发射光线，按渲染方程对光传输做蒙特卡洛积分（多次弹射、随机采样），以大量样本换取物理正确的光照（软阴影、GI、焦散）。特征：
- 每像素代价 = 采样数 × 每采样求交/着色代价，**噪声随样本数平方根下降**，实时应用普遍 1–few spp + 时空去噪（denoiser）+ 复用（ReSTIR 类 reservoir 重采样是近年主线；机制描述属通识综述，具体算法论文本次未逐篇核验，标 **未核**）。
- **确定性天然受损**：蒙特卡洛采样序列、reservoir 合并、去噪器内的归约都引入顺序依赖；同 seed 可固定采样序列，但并行归约仍受 §1.5 的浮点问题约束。
- 硬件形态：RT core 加速 BVH 求交（Vulkan `VK_KHR_ray_tracing_pipeline` / RTX 扩展），本机 RTX 4060 Laptop（CC 8.9，Ada）含 RT core（硬件事实承仓库 D1 环境 note；具体 RT 吞吐数字 **未核**）。

**forward vs deferred vs 混合**（通识机制，规范/引擎文档逐字未抓，标 **未核**）：
- forward：每基元直接着色到最终颜色，光源数进每片元代价；MSAA 友好；适合光源少/材质简单的场景。
- deferred：GBuffer pass 写几何/材质属性，光照 pass 逐光源读 GBuffer；光源数与几何解耦，带宽/显存换灵活性；透明物体与 MSAA 需额外 pass。
- 现代工业线普遍混合：deferred/visibility buffer 打底 + 光追补 GI/反射/阴影（UE5 Lumen 即"屏幕空间 + 软件/硬件光追"混合，转述口径见 §2.1）+ 时域超分（TSR/DLSS 类）摊薄分辨率代价。
- 对 T7 的含义：**"渲染管线"不是单数**——一个 L4 后端内部可能是多 pass 异质组合，契约⑨的 render 段计量若要分解，粒度应到 pass 而非整帧（关联性陈述）。

**神经渲染（neural rendering）**——用学习到的表示替代/增强显式几何+着色。两条主线：
- **3D Gaussian Splatting（3DGS）**：场景 = 数百万各向异性 3D 高斯（位置/协方差/不透明度/球谐颜色），渲染 = 投影（splatting）+ tile 化排序 + 前到后 alpha 合成。它把 NeRF 的"逐光线查询 MLP"换成"显式基元 + 光栅化式合成"，从而在 1080p 达到实时（论文摘要一手口径：**≥30 fps @1080p**，arXiv 2308.04079，见 §3.2）。注意：3DGS 是**可微光栅化的近亲而非路径追踪**——tile 内按深度近似排序（非逐像素精确排序），个别视角有 popping 伪影；高斯椭球不是可碰撞的实体表面（机制细节据二手解读文献，原文 PDF 本次未抓取，标 **未核/转述**）。
- **扩散外观（diffusion appearance）**：视频扩散模型把结构化控制信号（pose/depth/法线 buffer）"画"成照片级 RGB。几何正确性由输入控制信号保证，模型只负责外观——这正是 Marionette 的 observation model 分工（一手 README + 论文，见 §1.7/§3.1）。代价：每 chunk 多步去噪，实时性远逊光栅化；确定性受采样噪声 + GPU 归约双重影响。

**取舍总表**（机制层对比，非性能实测）：

| 维度 | 光栅化 | 路径追踪 | 3DGS | 扩散外观 |
|---|---|---|---|---|
| 几何来源 | 显式 mesh | 显式 mesh/BVH | 优化出的高斯云 | 无（消费控制信号） |
| 每帧代价主项 | 像素×overdraw×draw call | spp×求交 | 高斯数×排序×合成 | 去噪步数×DiT 前向 |
| 实时性（消费级 GPU） | 高 | 中（需去噪/复用） | 高（论文 ≥30fps@1080p，一手摘要） | 低（离线/近线为主） |
| 位级确定性潜力 | 高（局部计算） | 低（MC+归约） | 中（排序确定但浮点合成顺序敏感） | 低（采样+归约） |
| 真值几何/碰撞 | 有 | 有 | **无**（外观基元） | 无 |
| 与契约 identity 的关系 | mesh 可挂 id | 同左 | 高斯无身份语义，需外部锚 | 同左 |

关联性（不裁决）：L4 表现后端候选谱系 = CPU 光栅器（现有参考组件）→ Vulkan 离屏光栅器（PLAN 族谱③座位）→ 3DGS（D5 重建链 VGGT→COLMAP→gsplat 的渲染端）→ Marionette 式混合（几何确定 + 外观生成）。"真值几何/碰撞"一行解释了为什么评测 oracle 只能建在显式几何层，而不能建在神经外观层。

### 1.2 帧预算分解：CPU 提交 / GPU 执行 / 同步

实时管线的延迟不是"GPU 跑多快"一个数，而是三段流水线之和：

1. **CPU 提交段**：应用逻辑 → 场景遍历/剔除 → 录制 command buffer → 驱动校验 → queue submit。Vulkan 相对 OpenGL 的核心卖点就是把这段变薄且可多线程并行录制（显式 API、SPIR-V 预编译着色器、驱动少做事——Khronos 官方对 Vulkan 设计目标的表述，转述自百科词条引用的官方口径，标 **转述**）。
2. **GPU 执行段**：vertex/compute/fragment 各 stage 实际执行。帧预算的算术：60 fps = 16.67 ms/帧、30 fps = 33.3 ms/帧（纯算术）。GPU 段内部还要分 pass（GBuffer/光照/后处理），延迟渲染（deferred）把几何与光照解耦：先写 GBuffer（位置/法线/albedo），再逐光源着色——代价是带宽与显存（多 render target），收益是光源数与几何复杂度解耦（通识机制）。
3. **同步/等待段**：CPU↔GPU 帧同步（fence）、GPU 内部 pass 依赖（barrier/semaphore）、读回（readback）造成的流水线气泡。**这一段最容易被忽视，也恰是仓库 D5 判据的主轴**：D1 环境 note 的结论"带宽不是瓶颈，往返次数与同步点才是"（30 fps 连续上载 1080p 仅需 186 MB/s，远低于实测 H2D 12.81 GB/s——仓库 benchmarks 口径）。

**预算分解示例**（示意算术，非实测；实测口径只认仓库 benchmarks）：

| 段 | 60fps（16.67ms）下的典型问题 | 观测手段 |
|---|---|---|
| CPU 提交 | 场景遍历/剔除超预算；驱动校验；单线程录制 | CPU sampling（nsys 需管理员，本机实测非管理员自动禁用） |
| GPU 执行 | 某 pass 超预算；overdraw；带宽饱和 | GPU metrics（SM 利用率/DRAM 带宽，nsys 采集侧）+ per-kernel 深钻归 Nsight Compute |
| 同步/等待 | fence 等待气泡；readback  stall；跨 API signal/wait 链 | CUPTI SYNCHRONIZATION 表 + queued/submitted 时间戳（需显式开启） |
| 数据搬运 | H2D/D2H 往返次数 | CUPTI MEMCPY 表（copyKind/bytes/start/end） |

**吞吐 vs 延迟**：流水线重叠（frames in flight）提高吞吐但增加输入→显示延迟；闭环评测关心的是**端到端延迟分解**而非吞吐。契约⑨已逐阶段计量 render/perceive/adjudicate/metrics（仓库闭环 note §6），闭环路线要求的是 perceive→decide→act→observe 的端到端分解——GPU 段的 queued/submitted/start/end 时间戳（CUPTI，§1.6）正是把"提交→执行"气泡显式化的原料。

**观测口径纪律**（承 nsight-cupti 卡 §2）：nsys 时间线回答"发生了什么、谁等谁"，是证据不是基准；性能主张只认 Release 构建 + `benchmarks/regression/`。

### 1.3 Vulkan 同步原语与离屏渲染

**执行模型**：应用把命令录进 command buffer，提交到 queue；GPU 异步执行。同步原语三件套（机制为 Vulkan 通识；规范逐字本次未抓取，标 **未核**，但 timeline/binary semaphore 语义在 CUDA 互操作官方文档中有交叉印证，见下）：
- **Fence**：GPU→CPU 信号，用于"这帧提交的工作完成了吗"（帧节流、readback 前等待）。
- **Semaphore**：GPU↔GPU（queue 间）依赖。binary = 1 bit 信号态；**timeline = 64-bit 计数器**，同一 semaphore 上按值定义执行序（此句在 CUDA Programming Guide 互操作节有官方逐字表述，已核）。跨 API（Vulkan↔CUDA）共享的正是 semaphore（§1.4）。
- **Barrier / event**：queue 内部的执行依赖 + 内存依赖 + image layout 转换（如 color attachment → shader read）。显式 barrier 是 Vulkan 把"驱动猜依赖"换成"应用声明依赖"的代价与收益所在。

**离屏渲染（offscreen）**：不创建 swapchain/窗口 surface，把 render pass 的输出附着到 `VkImage`（color attachment），结束后两条出路：
1. **readback**：`vkCmdCopyImageToBuffer` → host-visible 内存 → CPU 拿像素。代价 = 一次 D2H + 一次 fence 等待（仓库 D2H 实测 13.12 GB/s，benchmarks 口径）。适合"渲染→评测/落盘"的批式闭环。
2. **interop 零拷贝**：image/buffer 经 external memory 导出给 CUDA 直接消费（§1.4），省掉 D2H+H2D 往返。适合"渲染→GPU 推理"的在环管线。

**计算管线（compute pipeline）**——与图形管线并列的另一条 GPU 工作形态：无固定功能阶段（无光栅化/深度测试），`vkCmdDispatch` 启动 workgroup 网格，shader 自由读写 buffer/image（storage image）。要点（通识机制，规范逐字未抓，标 **未核**）：
- workgroup 内共享内存（shared memory）+ `barrier()` 同步是归约/扫描类算法的骨架——§1.5 的确定性归约（pairwise + `__syncthreads`/`__threadfence`）在 CUDA 侧的同构物。
- 图形↔计算同 queue 交错时用 barrier 声明依赖；异步计算 queue 可重叠执行（硬件调度决定实际并发，**不保证**——这正是"观测配置改写被测系统"之外的另一层调度不确定性）。
- CUDA 与 Vulkan compute 的分工：CUDA 生态（cuBLAS/cuDNN/Thrust/CUB、Warp、Nsight 工具链）在数值计算侧更厚；Vulkan compute 的优势是与图形资源零拷贝同域。CUDA↔Vulkan interop（§1.4）让两者可以各取所长：Vulkan 出几何/GBuffer，CUDA 跑推理/归约。
- 3DGS 参考实现即 CUDA 自定义光栅化 kernel（论文一手摘要称 fast visibility-aware rendering algorithm；kernel 细节转述见 §3.2）——"神经渲染"在工程上仍是手写 GPU kernel，落在 PLAN 技能阶梯"kernel 作者"方向（关联性）。

**版本现状**：Khronos 2024-12 发布 **Vulkan 1.4**（官方新闻，已核）：把 Roadmap 2022/2024 的成熟特性并入核心并强制——push descriptors、dynamic rendering local read、scalar block layout、maintenance1–6、8K/8 render target 下限，并对 **streaming transfers**（边渲染边大量上载数据）提出新的实现要求。最后一条与 D5 数据流直接相关：API 层面开始保证"流式上载不拖垮渲染"。PLAN 族谱③写的是 Vulkan 1.3 离屏渲染器（PBR/Post-Process/光追/3DGS），1.4 的强制特性集意味着新学可以直接按 1.4 基线（关联性陈述，非裁决）。另有 Vulkan SC（安全关键变体，要求行为确定、管线/着色器离线编译——转述自百科引 vulkan.org，**未核**），是"确定性渲染"在工业侧的存在性证据。

### 1.4 GPU 数据流：零拷贝各层路径与代价

从慢到快的路径谱系（机制依据：CUDA Programming Guide 官方互操作节，已核；带宽数字来自仓库 benchmarks 口径）：

| 路径 | 机制 | 代价/约束 |
|---|---|---|
| pageable H2D | `cudaMemcpy` 从可分页内存：驱动先拷进内部 pinned  staging 再 DMA | 两次拷贝 + 同步语义；仓库实测 H2D 64MiB p50 5.241ms / **12.81 GB/s**（D1 note，benchmarks 口径；该实测是否 pinned 见 §6） |
| pinned H2D | `cudaHostAlloc` 页锁定内存，DMA 直达 | 一次拷贝；分配贵、占用不可换页物理内存；异步 `cudaMemcpyAsync` 的前提 |
| unified memory | `cudaMallocManaged`，按页错误迁移 | 编程最简；首次访问触发 page fault + 迁移，延迟不可预测；对逐 tick 确定性延迟预算不友好（机制通识 + CUPTI 有 `--cuda-um-*-page-faults` 追踪选项佐证，仓库 deep 报告已核该选项存在） |
| **external memory（CUDA↔Vulkan interop）** | 同一块显存被两个 API 映射，**零拷贝** | 见下方机制细节；代价转移到同步与生命周期管理 |
| GPUDirect RDMA | 外设（采集卡/网卡）DMA 直写显存 | 需 Quadro/RTX 级 GPU + 平台支持（Holoscan 官方资料口径，转述已核于第三方 profile 页，标 **转述**） |

**CUDA↔Vulkan 互操作机制**（CUDA C++ Programming Guide "External resource interoperability / Vulkan interoperability" 节，官方文档搜索快照已核，URL 见 §7）：
1. **设备配对**：Vulkan physical device 与 CUDA device 必须按 **UUID 匹配**（`VkPhysicalDeviceIDProperties.deviceUUID` vs `cudaDeviceProp.uuid`），且该 physical device 所在 device group 只能含 1 个设备。
2. **内存导出/导入**：Vulkan 侧启用 `VK_KHR_external_memory`（+ Windows `VK_KHR_external_memory_win32` / Linux `_fd`），buffer/image 创建时声明 handle type，分配时挂 `VkExportMemoryAllocateInfo`；CUDA 侧 `cudaImportExternalMemory` → `cudaExternalMemoryGetMappedBuffer`（buffer）或 `cudaExternalMemoryGetMappedMipmappedArray`（image）。**映射的 offset/size/format 必须与 Vulkan 侧完全一致，否则 undefined behavior**（官方逐字）。
3. **句柄所有权语义不对称**（官方逐字，易错点）：Linux fd 导入后 CUDA 接管所有权，再用 fd = UB；Windows NT handle CUDA **不**接管，应用负责 `CloseHandle`，且 NT handle 持有资源引用，须先释放 handle 才能释放底层内存。dedicated 内存须设 `cudaExternalMemoryDedicated`。
4. **同步**：Vulkan semaphore 经 `VK_KHR_external_semaphore*` 导出，CUDA `cudaImportExternalSemaphore` 导入，`cudaSignalExternalSemaphoresAsync` / `cudaWaitExternalSemaphoresAsync` 在 stream 上信号/等待；**wait 必须在对应 signal 发出之后提交**（官方逐字）。binary 与 timeline semaphore 都支持。
5. 官方直言动机："Coupled execution of Vulkan graphics and compute workloads on the same hardware can maximize GPU utilization and **avoid unnecessary copies**"。

**"零拷贝"不是零代价**：省掉的是字节搬运，付出的是（a）跨 API 同步点（每个 signal/wait 对都是一次显式依赖，D5 判据的"同步次数"轴会计入）；（b）生命周期/所有权管理复杂度；（c）格式与布局约束。仓库 D5 的判据"拷贝次数 + 同步次数为主、字节数为辅"恰好同时覆盖收益与代价两侧（nsight-cupti 卡 §6 已论证证据通道）。Holoscan/DeepStream 这类现成管线框架的存在削弱"零拷贝需自研"前提（queue 10-28 更正，仓库口径；版本与许可现状见 §2.4）。

### 1.5 GPU 位级确定性：破坏源与重建手段

**为什么浮点归约/原子破坏位级一致**——根因是 IEEE 754 加法**不满足结合律**：`(a+b)+c ≠ a+(b+c)`（舍入误差随组合顺序变化）。GPU 把求和/累加分给成千上万线程，**完成顺序由 warp/block 调度决定，逐次运行不同**：
- `atomicAdd(float*)`：线程竞争提交顺序 → 每次运行舍入路径不同。PyTorch 官方 Reproducibility 笔记逐字："atomicAdd ... can lead to the order of additions being nondeterministic ... introduce different floating-point rounding errors on each evaluation"（官方文档文本，经镜像抓取已核）。**整数原子是确定的，浮点原子不是**（同一文档机制）。
- 并行 reduction：block  partial 的合并顺序依赖调度，即使不用原子。
- 库算法选择：cuDNN/cuBLAS 按启发式+运行时 benchmark 选 kernel，选择本身随时间/负载变化 → 不同算法不同位（PyTorch 文档要求 `cudnn.deterministic=True, benchmark=False` 的原因）。
- 编译器/精度：FMA 合并、TF32（Ampere+ 默认路径之一）、混合精度累加器宽度——同源码不同编译/不同 SKU 出不同位（通识 + FPNA 论文 §IV 佐证，已核）。
- 跨设备：PyTorch 官方逐字："results need not be reproducible between CPU and GPU executions, even when using identical seeds"，且跨 release/平台不保证。**位级确定性是"同机同配置"性质，不是跨平台性质**——这是 Marionette README 与 MJWarp 文档共同印证的一手结论（见下）。

**破坏源的实证样本（一手）**：
- **MJWarp 官方 FAQ 逐字**："Is MJWarp on GPU deterministic? **No.** There may be ordering or *small* numerical differences between results computed by different executions of the same code. This is characteristic of non-deterministic atomic operations on GPU. Set device to CPU with `wp.set_device("cpu")` for deterministic results."（mujoco 主仓 doc/mjwarp/index.rst，已核）。GPU 确定性开发追踪于 mujoco_warp issue #562。另：MJWarp 用 **float32** 而 MuJoCo 默认 float64 → 跨后端数值差异是表示层差异，与调度非确定性是两回事（同 FAQ，已核）。
- **Marionette README 逐字**："the digest is not portable, only the equality is. The same command with the same seed and the same weights produces a different video on a different machine — floating-point evaluation order is enough to move one `torch.multinomial` draw, and the motion diverges from there."（已核）——浮点微扰经**采样离散化**放大成轨迹分叉，是"位级漂移→语义漂移"的完整链条实例。

**破坏源分类表**（综合 FPNA 论文、PyTorch 官方笔记、MJWarp FAQ/PR、Marionette README；各源核验状态见 §7）：

| 层 | 破坏源 | 机制 | 重建手段 | 实证样本 |
|---|---|---|---|---|
| 原语 | 浮点原子（atomicAdd float） | 提交顺序随调度 | 固定序 gather / 排序+分段归约 | PyTorch 非确定算子清单（官方） |
| 原语 | 并行归约树 | partial 合并顺序随 block 调度 | pairwise/tree + threadfence、CUB、两 pass | FPNA 论文 6 实现对照（一手） |
| 分配 | 竞争抢槽（atomic 计数器分行/分槽） | 行位置逐次不同 | count→scan→emit | MJWarp PR #1422（一手） |
| 库 | cuDNN/cuBLAS 运行时算法选择 | benchmark 本身随负载 | deterministic=True + benchmark=False + workspace 钉死 | PyTorch 官方笔记 |
| 编译/精度 | FMA 合并、TF32、累加器宽度 | 同源码不同位 | 显式精度开关、编译选项入 run record | FPNA §IV（TF32 细节标未核） |
| 采样 | RNG 未播种 / 浮点微扰移动离散抽样 | 一次 draw 偏移→轨迹分叉 | 全链播种 + 同机限定 | Marionette torch.multinomial 例（一手） |
| 环境 | 驱动/库/SKU 变更 | kernel 选择与实现变化 | 环境指纹钉死；跨机降级逻辑等价 | PyTorch"跨 release/平台不保证"（官方） |
| 编码 | 视频编码器不承诺位稳定 | 容器字节≠张量内容 | 比较解码帧而非文件字节 | Marionette stage2 比较口径（一手） |

**重建手段（按层次）**：
1. **固定归约顺序**：FPNA 论文（arXiv 2408.05148，已核）系统比较了 CUDA 求和实现——atomicAdd 累加（SPA/AO）非确定；**pairwise/tree reduction + `__threadfence` + 最后 block 收尾（SPTR/SPRG）、两 pass + CPU 收尾（TPRC）、CUB 库归约（CU）均构造性确定**。关键结果：确定性与非确定性实现的性能差在 V100 上 <0.2%、GH200 上至 7.8%（论文一手数字）——**"确定性必然大幅牺牲性能"在归约这一层不成立**；但单线程串行求和（AO 的极端形态）慢约 2 个数量级。
2. **消除竞争分配**：把 atomic 抢槽改为 count→scan→emit（先计数、前缀和定偏移、再写入），行位置逐次一致。MJWarp PR #1422 正是此模式：稳定接触排序 + count-scan-emit 约束行分配 + 固定列索引 gather 替代 solver 原子累加，`opt.deterministic` opt-in 后 qacc/qpos/qvel 在 1000 个 graph-captured step 上逐位一致（PR 描述一手，已核；**是否已合并未核**）。代价：det=on 的 step 吞吐 −31%（1 world）至 −81%（8192 worlds）（RTX PRO 6000 / CUDA 13.3，PR 作者实测口径，转述已核于 PR 文本）。对照：Warp 通用确定性改写（NVIDIA/warp#1355）在同模型上 ~37× 减速 + 丢 graph capture + 44GB 额外 scratch（256 worlds，PR 内实测）——**领域专用确定性路径比通用改写便宜一个量级**（该 PR 文本一手结论）。
3. **框架级开关**：`torch.use_deterministic_algorithms(True)`、`cudnn.deterministic/benchmark`、`CUBLAS_WORKSPACE_CONFIG`、TF32 显式关闭、全 RNG 播种（PyTorch 官方笔记，已核；TF32 细节属通识补充，标 **未核**）。
4. **环境钉死**：GPU SKU/驱动/CUDA/库版本全部进 run record；跨 SKU 位级一致一般不可达，降级为"逻辑等价 + ε"（FPNA 论文 + PyTorch 文档共同支持的分级观）。

**分级表述**（本 note 的综合，非某单一官方口径）：同机同配置位级可复现 = 可工程达成（MJWarp det 模式、Marionette stage1 byte-identical 为存在性证明）；跨机/跨 SKU 位级 = 一般不可达；CPU↔GPU 位级 = 不可达（官方文档明示）。关联性：位级复现 oracle 的适用范围声明必须带"同机同配置"限定词；契约⑨的证据通道要能记录环境指纹（承 nsight-cupti deep 报告 §4 confidence 轴的"证据元数据"清单）。

### 1.6 GPU 可观测性：Nsight/CUPTI 与 tick↔GPU 活动关联

机制全貌已在仓库 deep 报告逐字核验，此处只给骨架（引用不复制，详见 [deep/2026-11-05-nsight-cupti.md](../deep/2026-11-05-nsight-cupti.md)）：
- **两条记录流水线**：Callback API 在宿主线内同步拦截 CUDA API（ENTER/EXIT）；Activity API 异步把 GPU 事件（kernel/memcpy/synchronization）写进显存环形缓冲，worker 线程批量交付。nsys 本身就是一个 CUPTI 订阅者（单订阅者仲裁 → nsys 与程序内自观测互斥）。
- **三根关联通道**：① correlationId（CUPTI 内建，API 调用↔GPU 执行对账）；② **external correlation**（per-thread push/pop 栈，`CUSTOM0-2` kind 官方预留给外部工具，`externalId: uint64_t` 可直接放 tick 号 → activity 流逐条回查归属）；③ NVTX range 投影到 GPU timeline（给人看）。
- **本机实测已成立**（2026-11-05，仓库口径）：nsys 2024.4.2 采集 → SQLite 导出含 `CUPTI_ACTIVITY_KIND_MEMCPY / RUNTIME / SYNCHRONIZATION / CUDA_EVENT` 表——恰为 D5"拷贝次数+同步点"判据两根主轴的原始记录；"GPU 时间线表化后 SQL join 自家 tick 表"路线在参数化数据上成立。
- **观测的代价与坑**（deep 报告已核）：`KERNEL` kind 强制 GPU 串行（应用 `CONCURRENT_KERNEL`）；缓冲不足丢记录（`cuptiActivityGetNumDroppedRecords` 必须随证据读出）；默认跳过"not critical" API；queued/submitted 时间戳需显式 `cuptiActivityEnableLatencyTimestamps()`——这组时间戳正是 §1.2"提交→执行"气泡的直接观测。
- **未决**：external correlation 在 nsys 导出件中的落表形态未实测（API 层已核，承卡 §7.5）；多源时钟对齐机制官方未说明（deep §5.3）。

**工具分工与旁证**：
- Nsight Systems（系统级"谁等谁"）vs Nsight Compute（per-kernel 微架构深钻）是分工关系，升级工具的参照线见 nsight-cupti 卡 §6；PC Sampling（warp PC + stall reason 采样，CC 5.2/6.0+，本机 8.9 在范围内）是后者的底层来源（deep 报告 §2.5 已核）。
- 管线框架自带观测的旁证：Holoscan 的 data flow tracking 报告 operator 间延迟（官方博客/profile 页口径），其 4.2 栈内直接捆绑 nsys 2026.2.1（官方博客已核）——"GPU 管线框架与 CUPTI 证据链共生"是工业现状，不是本仓库独创需求（关联性）。
- Vulkan 侧观测：nsys `--trace` 支持 `vulkan, vulkan-annotations`（本机 2024.4.2 help 逐字，仓库卡 §1 已核）——离屏 Vulkan 渲染器一旦立项，同一套 nsys 采集即可覆盖 graphics API 行 + CUDA 行 + NVTX tick range，无需第二套工具（关联性）。

关联性：GPU Observability 是四层架构的横向层（owner 已确认骨架）；tick↔GPU 关联 = 契约⑨的证据通道；对 T7 而言，渲染后端无论换成什么，"每 tick 的 memcpy/sync 计数 + 延迟分段"都应由同一套工具链产出，才谈得上后端间可比。

### 1.7 混合渲染架构：状态 → 确定性几何 → 生成式外观（Marionette 式）

**原理**（Marionette 论文 arXiv 2608.14530 + 官方 README，一手已核；编号经搜索确认存在）：把世界模型分解为 p(video) = ∫ p(obs|s)·p(s'|s,control) ds——**动力学在显式状态里预测，几何由零参数渲染器闭式计算，外观由神经模型合成**。三段实例：
1. **显式状态**：276D 关节化世界状态（双实体的 root 位移/相对关节位置/6D 旋转/武器子状态）。几何、接触、遮挡是状态的属性，"exact by construction rather than learned approximately"（README 逐字）。
2. **零参数 graphics bridge**：6D 旋转 Gram–Schmidt 正交化 → root delta 旋到世界系 → 累加恢复绝对轨迹 → 放置关节 → 确定性 follow-camera → 光栅化成 pose-control video（三通道几何 buffer：height / per-joint identity / inverse depth）。实现是 **moderngl/EGL（OpenGL 离屏）**，不是 Vulkan（README 仓库结构，已核）。每一步闭式几何运算 → 不漂移、不幻觉、不需要数据。控制作用于状态层的 action token，覆写一个 token 即改变行为且其余逐帧不变（`check_counterfactual.py` 断言，README）。
3. **observation model**：control-conditioned 视频扩散（Wan2.2-Fun-5B 底座），只被要求"画给定的几何"，不负责记东西在哪；长时程用 chunk-relay，外观跨 chunk 传递，几何/动力学始终由状态层管辖。

**管线形态**（据论文 §3 与 README 结构绘制）：

```
action tokens ──► ActionGPT(决策) ──► PoseGPT(动画) ──► s_t ∈ R^276（显式状态）
                                                          │
                                     零参数 bridge（闭式几何，moderngl/EGL 离屏）
                                     FK → root 积分 → 关节放置 → follow-camera
                                     → 光栅化 pose-control video
                                       （3 通道：height / joint identity / inv-depth）
                                                          │
                              control-conditioned 视频扩散（Wan2.2-Fun-5B）
                              + 首帧参考（外观身份） + chunk-relay（长时程）
                                                          │
                                                      RGB 观测
控制注入点 = 覆写 action token（状态层）；修复注入点 = 状态层规则（terrain collider 等）
位级边界：状态+bridge 同机同 seed byte-identical；外观层只承诺解码帧级一致
```

**为什么这个切分有效**（论文论证，一手）：神经生成模型擅长外观与感知合理性，弱于长时程精确记账与离散逻辑（redstone 例子：灯亮不亮由逻辑状态决定，不由邻近像素决定）——"Work that must be exact is executed by a tool, not generated"。错误因此被限制在状态层，且**可以在状态层修复**（terrain collider / separation cap 两条规则改善穿地与漂移，不动 observation model）。评测也分两层：状态层用物理量（穿地/foot-skate/间距，单位米），观测层用 FVD——长时程随机 rollout 不能用单参考逐帧精度评分（论文 §4.2 方法论，与仓库 E1"指标会骗人"教训同构）。

**确定性画像**（README 一手，T7 核心证据）：固定 seed 后 stage1（含 bridge 渲染）**byte-identical**；stage2 在 162 个解码帧上逐帧一致（比较解码像素而非文件字节，因为视频编码器不承诺位稳定）；**跨机器 digest 不可移植**（§1.5 已引）。即：混合架构把"位级可复现"的边界清晰地画在几何/状态层，外观层只承诺同机同 seed 的帧级一致。

**与既有路线的关系**（只写关联性）：六方向 note §D6 已把"Marionette 式确定性几何渲染+生成式外观"登记为 L4 后端候选架构；其 bridge 的"零参数、闭式、可断言"性质与 CPU 光栅器参考组件的角色同构——参考组件天然是"确定性几何层"的现成雏形；其状态层评测（米制物理量）与契约的真值/信念分离、逐 tick 账本可比。新颖性边界承六方向 note §3：混合架构本身已被 Marionette/MASS/StatePlay 占用，不属可主张创新。

---

## 2. 进度快照（截至 2026-11）

### 2.1 Vulkan / 实时渲染工业线
- **Vulkan 1.4**（2024-12 发布，Khronos 官方新闻已核）：streaming transfers 实现要求、push descriptors / dynamic rendering local read / scalar block layout 强制、maintenance6 入核、8K/8 RT 下限。2026-01 工作组路线图把 VRS（可变率着色）列为强制（**转述**，百科引 IT之家，官方路线图原文未抓）。生态信号：Blender 5.3（2026-11）把 Linux/Windows 默认图形后端从 OpenGL 切到 Vulkan（**转述**，百科引快科技）。
- **UE5**：Nanite（虚拟化微多边形几何，免手工 LOD）+ Lumen（全动态 GI/反射，免烘焙）是当前工业实时渲染的标志性组合；本次仅获中文二手解读，Epic 官方文档未抓取——机制描述标 **转述/未核**。与 T7 的关联仅在"几何细节虚拟化 + 光照实时化"的方向参照，UE5 不在仓库依赖候选内（PLAN 无座位）。
- **本机座位**：PLAN 族谱③ = Vulkan 1.3 离屏渲染器与效果栈（PBR/Post-Process/光追/3DGS），EXPLORATION 行"离屏 Vulkan 观察渲染器替换 CPU 光栅器"；BACKLOG 明确"Vulkan 完整渲染器"不做。

### 2.2 神经渲染落地状态
- **3DGS**：2023-08 SIGGRAPH（TOG 42(4)）提出，摘要一手口径 ≥30fps@1080p；生态已爆炸式分化——第三方追踪目录 awesome-gaussians（2026-11 仍在更新，已核其存在与分类）列出 Applications 500+ / Dynamic Scene 194 / SLAM 75 / Compression 201 等类目（目录计数为第三方口径）。**进入仿真器内核的信号**：MJWarp 官方 FAQ 载明其 batch renderer 支持 "3D Gaussian Splatting (3DGS) diffuse rendering with physical occlusion"（单命中 raycaster、lambertian、无 GI/PBR/SH>0——官方一手，已核）。仓库侧：D5 已核 VGGT→COLMAP→gsplat 链（≤20 帧 5.58GB 档，原权重 CC-BY-NC，承六方向 note §D5）。
- **扩散外观**：Marionette（2026-08-13 代码/资产发布，08-17 论文上 arXiv）三段管线推理代码全开放；README roadmap 显示权重"uploading"、训练代码未发布（截至本次检索快照）。底座 Wan2.2-Fun-5B-Control 为 Apache-2.0（README 载明）。
- **混合架构占位**：Marionette / MASS / StatePlay 已在架构层占用"状态+生成混合"主张（六方向 note §3 打折表，仓库已核）。

### 2.3 GPU 确定性工程进展
- **MJWarp**：官方 FAQ 明示 GPU 默认非确定（已核，§1.5）；确定性 PR #1422（opt-in `opt.deterministic`，接触排序+count-scan-emit+固定 gather）截至检索快照仍在 review/迭代（后续 #1425 降开销），**合并状态未核**。Warp 上游通用确定性（NVIDIA/warp#1355）存在但在 MJWarp solver 上代价过大（§1.5 数字）。
- **归约层**：FPNA 论文（2024-08，v3 2024-10）已给出"确定性归约几乎免费"的跨 GPU 实证（V100/GH200/Mi250X，一手）。
- **渲染层**：Marionette stage1 byte-identical（同机同 seed）是"确定性几何桥"的公开存在性证明（README 一手）。

### 2.4 管线框架与观测工具链版本/许可
- **Holoscan SDK**：GitHub README 逐字 Apache-2.0（官方仓库，已核）。**版本口径冲突**：仓库 queue（10-28）记"Holoscan 5.0"；本次第三方 profile 页（IntelligentHQ，2026-10-09）称最新为 4.6.0（2026-09-03 发布）、文档版 4.6——两者必有一处过时或口径不同，**5.0 与 4.6 均标未核（本次未抓 GitHub releases 页）**。官方博客（2026-05-13，4.2 发布文，已核）：GPU-resident graph 执行支持完整 DAG（"no CPU scheduling"以降 jitter）、SCHED_DEADLINE 实时调度 benchmark、栈内 NSight Systems 2026.2.1。
- **DeepStream**：仓库口径"9.2 / Apache-2.0"（queue 10-28），本次未复核，标 **未核**；且 DeepStream SDK 整体的许可形态（SDK 本体 vs 开源组件）历史上存在专有 EULA 与开源部分并存的情况，"整体 Apache-2.0"表述需对照官方许可原文重验（见 §6）。
- **Nsight/CUPTI（本机）**：nsys 2024.4.2 + CUDA 12.6，实测链已留档（仓库口径）；CUPTI 库可再分发条款、NVTX Apache-2.0 with LLVM Exceptions 等许可结论承卡 §3，不重述。

---

## 3. 关键文献精读

### 3.1 Marionette: Predicting World States, Rendering Geometry, Painting Appearance（arXiv 2608.14530，2026-08）
- **问题**：像素/潜空间自回归的游戏世界模型把 pose/几何/遮挡/身份/控制效果全部交给生成序列隐式维持，长时程误差复合，一致性与可控性脆弱。
- **方法**：显式 276D 关节状态 + 两段动力学（ActionGPT 决策 / PoseGPT 动画，论文 HTML 读得参数量约 2.5M / 25M，**HTML 渲染有重复噪声，数字待 PDF 复核**）+ 零参数确定性 bridge（FK/地形碰撞/光栅化，moderngl/EGL）+ control-conditioned 视频扩散（Wan2.2-Fun-5B）+ chunk-relay 长时程。
- **关键结果**（口径区分）：① 控制有因果权威——覆写 action token 改变行为且其余逐帧不变（README 断言脚本，一手）；② 长时程失效在状态层可测可修（自由 rollout 漂移/穿地，状态层规则修复而不动外观模型；论文摘要含具体数字但 HTML 渲染 garbled，**精确数值未核**）；③ 外观经状态路由无可测 fidelity 损失（FVD 对比，同上**数值未核**）；④ 复现性：同机同 seed stage1 byte-identical、stage2 162 解码帧一致、跨机 digest 不可移植（README 逐字，一手）。
- **局限**：单游戏单场景（双实体格斗）、权重/资产 non-commercial research only 且不可再分发（WildWorld 派生，README LICENSE 表）、训练代码未发布、observation 权重 10.0GB + 底座 ~23GB 另行下载、两阶段两套 Python 环境不能共进程。
- **与 OmniWorld 的关联**：L4 混合后端候选架构的公开样板；其"位级边界画在几何层"的复现性声明与位级复现 oracle 的分层设计直接可比；bridge 的三通道几何 buffer（height/identity/inverse depth）是"契约 identity 进渲染控制信号"的现成编码实例；状态层米制评测与真值/信念分离同构。**不主张架构创新**（六方向 note §3 已打折）。

### 3.2 3D Gaussian Splatting for Real-Time Radiance Field Rendering（arXiv 2308.04079，SIGGRAPH 2023 / TOG 42(4)）
- **问题**：辐射场方法高质量但训练/渲染昂贵；快速方法牺牲质量；无界完整场景 1080p 无人做到实时（摘要一手）。
- **方法**（摘要一手 + 机制细节据二手解读，标转述）：SfM 稀疏点初始化 3D 高斯；各向异性协方差优化 + 自适应密度控制（clone/split/prune） interleaved；快速 visibility-aware tile 光栅化（16×16 tile、GPU radix sort 按深度+tile 键排序、前到后 alpha 合成、饱和早停）。
- **关键结果**：摘要一手 = SOTA 视觉质量 + 竞争训练时间 + **≥30fps@1080p 实时**；流传的"134/135 FPS、6 分钟训练"等具体数字来自二手解读（CSDN），原文 PDF 未抓，标 **转述/未核**。
- **局限**：tile 级近似排序 → 视角相关 popping；高斯云无实体表面/碰撞语义；每场景优化（非通用先验）；存储随高斯数膨胀（压缩是独立研究类目）。
- **与 OmniWorld 的关联**：D5 重建链（VGGT→COLMAP→gsplat）的渲染端原理；MJWarp batch renderer 已内置 3DGS diffuse（§2.2）说明它正进入仿真真值可视化路径；对 L4 而言是"真实世界输入→自由视角表现"的候选后端，但其无身份/无碰撞性质意味着契约语义必须由引擎侧外挂（关联性，不裁决）。

### 3.3 Impacts of floating-point non-associativity on reproducibility for HPC and deep learning applications（arXiv 2408.05148，2024-08，v3 2024-10）
- **问题**：浮点非结合律（FPNA）+ GPU 并行归约顺序不定，如何量化其对 HPC/深度学习可复现性的影响，确定性替代的真实代价是多少。
- **方法**：同一求和任务的 6 种 CUDA/HIP 实现对照（AO/SPA 原子非确定 vs SPTR/SPRG/TPRC/CU 构造性确定），V100/GH200/Mi250X 三平台；100 万 FP64 元素 ×10000 次重复测变异分布；PyTorch 非确定算子清单核对。
- **关键结果**（论文一手）：确定性归约性能损失 V100 <0.2%、GH200 至 7.8%；AO（单线程原子串行）慢约 2 个数量级；SPA 的变异分布收敛到依赖 GPU 家族的正态，**AO 的分布非正态**（"高斯噪声"假设不成立）；PyTorch 文档化的非确定算子（index_add_/scatter_add_/bincount/ctc_loss/interpolate 等）根因即 atomicAdd。
- **局限**：只覆盖归约这一原语；跨 SKU 位级一致仍不可达；论文自身承认高层框架文档与实际行为存在出入（scatter_reduce 确定性模式报错的实测记录）。
- **与 OmniWorld 的关联**：位级复现 oracle 的"确定性代价"文献锚点——与 MJWarp det 模式 −31%~−81% 的**系统级**代价对照，说明代价高度依赖非确定性出现在哪一层（归约原语层几乎免费，接触排序+约束分配层昂贵）；对 GPU Observability 横向层，它给出"变异分布形态"这一比 max-diff 更细的证据维度。

---

## 4. 开放问题

1. **混合渲染的 oracle 分层**：几何层位级 + 外观层帧级（同机）是 Marionette 的实证边界；OmniWorld 若引入生成式外观，契约⑨的"位级可复现 episode"主张应落在哪一层、外观层用什么等价性判据（解码帧一致？感知哈希？ε 逻辑等价）——本 note 只登记问题，不裁决。
2. **3DGS 渲染的确定性画像**：radix sort 键含浮点深度，排序稳定性与 alpha 合成累加顺序是否位级稳定？未见官方/论文陈述，需 spike（同机双跑位级比对）。
3. **interop 零拷贝在本机的净收益**：WDDM + 8GB 显存下，external memory 路径省掉的 D2H/H2D 往返（每次 ~0.49ms@1080p，仓库口径）是否被跨 API 同步点与格式约束吃掉？D5 判据（拷贝+同步计数）可直接测量，尚无数据。
4. **external correlation 端到端**：tick 号经 CUSTOM0 压栈后在 nsys SQLite 导出件中的落表形态未实测（承卡 §7.5，API 层已核）。
5. **跨平台位级漂移**：同 seed 同权重跨机器 digest 不可移植（Marionette 一手）；驱动/CUDA 升级是否破坏同机位级（MJWarp PR 的 graph capture 敏感性提示风险）——环境指纹应包含哪些字段才算"同配置"？
6. **确定性 vs 吞吐的预算**：MJWarp det=on −31%~−81% 与归约层 <8% 的反差说明需要逐层审计非确定性来源；渲染管线（barrier/混合/去噪）各层的确定性代价尚无系统文献。
7. **Holoscan 版本口径冲突**（5.0 vs 4.6）与 DeepStream 许可形态（§2.4）——影响"零拷贝需自研"前提的强度评估。
8. **Vulkan SC 的确定性条款**：安全关键变体对"行为确定"的规范级要求具体到什么程度（编译离线化之外）？若属实，是渲染确定性的工业规范参照（目前仅转述）。

## 5. 仓库已有资产引用

| 路径（相对 docs/17-research/） | 可复用结论 |
|---|---|
| projects/nsight-cupti.md | CUPTI/nsys 能力与边界、许可三分（CUPTI 目标码可分发/NVTX Apache-2.0+LLVM）、本机实测链（SQLite MEMCPY/SYNCHRONIZATION 表）、tick 关联三通道、族谱归宿=External reference |
| deep/2026-11-05-nsight-cupti.md | Callback/Activity 双流水线机制、correlationId/external correlation 逐字核验（头文件行号）、KERNEL 串行坑、丢样计数器、queued/submitted 时间戳、证据元数据清单 |
| projects/rerun.md | entity path × timeline 与契约 identity/time 同构；Wgpu 默认 Vulkan 后端；Apache-2.0/MIT；rrd 进不了字节级回归证据链（自研静态查看器路线的对照） |
| 2026-10-26-stage-d-environment-and-licenses.md | 本机 RTX 4060 Laptop 8GiB/CC 8.9/CUDA 12.6；H2D 12.81 GB/s、D2H 13.12 GB/s、1080p 单程 ≈0.49ms、30fps 仅需 186MB/s（benchmarks 口径）——"往返与同步才是瓶颈"的分母 |
| 2026-11-13-six-direction-innovation-integration.md | §D6：D5 零拷贝进行中、Holoscan/DeepStream Apache-2.0 口径（queue 10-28）、Saltline 性能纪律（4–7 draw call 等）、Marionette 为 L4 候选架构、GPU 确定性测量 niche；§3 新颖性打折表 |
| 2026-10-27-interactive-closed-loop-route.md | 契约⑨逐阶段计量（render/perceive/adjudicate/metrics）；闭环端到端延迟分解需求；E1 指标教训（M1 成对发布/M2 空集≠稳定/M3 per-producer） |
| projects/newton-mujoco-warp.md | MJWarp 确定性/重放语义登记为未决（本 note §1.5 已用官方 FAQ 升级为已核）；J2 同 seed 双跑位级比对 spike 楔子；MJCF 共享使后端可互换 |
| PLAN.md（族谱③/EXPLORATION/BACKLOG 行） | Vulkan 1.3 离屏渲染器=技能项目座位；离屏 Vulkan 观察渲染器替换 CPU 光栅器=EXPLORATION；"Vulkan 完整渲染器"不做；CPU 光栅器=参考模式组件+未来 GPU 渲染对照 |
| 2026-11-13-interest-landscape-and-redefinition.md | Saltline 一手镜像证据与"一次性 demo 无沉淀"教训（§3.4）；GPU 数据流纪律行 |

## 6. 待核验清单（本机 RTX 4060 Laptop 8GiB 约束下）

| # | 待核验项 | 现状 | 核验动作 |
|---|---|---|---|
| 1 | MJWarp GPU 默认非确定 | **已核**（官方 FAQ 逐字，本 note §1.5）；det PR #1422 合并状态**未核** | 查 PR 状态与 release notes；J2 spike：同 seed 双跑位级比对（CPU 基线 vs GPU det=on/off） |
| 2 | 跨平台位级漂移 | Marionette README 一手证实跨机不可移植；本机→他机漂移幅度无数据 | 同 seed 双机比对（若有第二台设备）；环境指纹字段清单先行 |
| 3 | H2D 12.81 GB/s 的内存类型口径 | 仓库 D1 note 未明示 pageable/pinned（本 note 未重读实测源码） | 查 benchmarks 源码确认；pinned/pageable 分列重测 |
| 4 | Holoscan 现行版本（5.0 vs 4.6 冲突） | 两口径均**未核** | 抓 GitHub releases 页一手确认 |
| 5 | DeepStream 9.2 版本与"Apache-2.0"许可形态 | 仓库 queue 口径，本次**未核** | 抓官方 DeepStream 页与 LICENSE/EULA 原文，区分 SDK 本体与开源组件 |
| 6 | 8GB 显存下的混合渲染可行性 | Marionette observation 权重 10.0GB（>8GB VRAM）+ 底座 ~23GB 下载；本机跑全管线**大概率不可行**（推断，未实测） | 若评估 L4 生成式后端：先核量化/小变体是否存在；几何 bridge（moderngl/EGL）单独可跑性另测 |
| 7 | 3DGS 渲染位级稳定性 | 无官方陈述，**未核** | gsplat 同机同 seed 双跑位级比对 spike |
| 8 | external correlation → nsys SQLite 落表 | API 层已核、导出层**未核**（承卡 §7.5） | 带 CUSTOM0 externalId 的端到端采集一次 |
| 9 | Vulkan 1.4 本机驱动支持 | 驱动 12.6 时代的 Vulkan 版本**未核** | `vulkaninfo` 本机实测（RTX 4060 + 现行驱动应支持 1.3+，推断） |
| 10 | UE5 Nanite/Lumen 机制细节 | 仅二手中文解读，**转述/未核** | 如需引用，抓 Epic 官方文档 |
| 11 | Marionette 论文精确数值（FVD/漂移米数/参数量） | arXiv HTML 渲染 garbled，**未核** | 抓 PDF 复核后再引用数字 |
| 12 | Vulkan SC 确定性条款原文 | 百科转述，**未核** | 抓 vulkan.org SC 页 |

## 7. 来源与核验状态

| # | 来源 URL | 关键内容 | 状态 |
|---|---|---|---|
| 1 | https://github.com/AlayaLab/Marionette/blob/main/README.md | 276D 状态/零参数 bridge（moderngl/EGL）/Wan2.2-Fun-5B 底座/双许可（代码 Apache-2.0，权重资产 research-only 不可再分发）/复现性声明（stage1 byte-identical、162 帧、跨机 digest 不可移植）/权重 10.0GB+底座 23GB | 已核（2026-11-13 搜索抓取全文） |
| 2 | https://arxiv.org/abs/2608.14530（经 arxiv-org 镜像 HTML 检索快照） | Marionette 论文存在性、三段分解、状态层/观测层双层评测、redstone 论证 | 已核（编号与摘要）；正文数值 **未核**（HTML garbled） |
| 3 | https://arxiv.org/abs/2308.04079 | 3DGS 摘要：≥30fps@1080p、三要素（高斯表示/密度控制/visibility-aware 渲染）、TOG 42(4) 2023、作者 Kerbl 等 | 已核（2026-11-13 webfetch 摘要页） |
| 4 | https://blog.csdn.net/ktigerhero3/article/details/168824474 等 CSDN 解读 | 3DGS 134/135FPS、训练时长、tile 16×16/radix sort/clone-split-prune 机制细节 | **转述/未核**（二手解读，原文 PDF 未抓） |
| 5 | https://docs.nvidia.com/cuda/archive/13.4.2/cuda-programming-guide/04-special-topics/graphics-interop.html | CUDA↔Vulkan 互操作全机制：UUID 配对/external memory/semaphore 导入导出/句柄所有权/映射一致性 UB/timeline vs binary semaphore/扩展清单 | 已核（官方文档搜索快照全文） |
| 6 | https://arxiv.org/html/2408.05148v3 | FPNA 论文：6 种归约实现确定性分类、V100<0.2%/GH200≤7.8% 代价、AO 慢 2 个数量级、变异分布非正态（AO）、PyTorch 非确定算子根因 | 已核（arXiv HTML 检索全文） |
| 7 | https://android.xilinx.googlesource.com/platform/external/pytorch/+/60e2baf5e056b63414a31e40ac7a1024ed0b0ce0/docs/source/notes/randomness.rst | PyTorch 官方 Reproducibility 笔记文本：atomicAdd 非确定逐字、跨平台/跨 release/CPU-GPU 不保证、cudnn 开关 | 已核（官方文档镜像；canonical docs.pytorch.org 页未直接抓） |
| 8 | https://raw.githubusercontent.com/google-deepmind/mujoco/main/doc/mjwarp/index.rst | MJWarp FAQ："GPU deterministic? No"逐字、issue #562、float32 vs float64、batch renderer（单命中 raycaster、3DGS diffuse with occlusion、无 GI/PBR/SH>0） | 已核（官方仓库 raw 检索全文） |
| 9 | https://github.com/google-deepmind/mujoco_warp/pull/1422 | det=on 三件套（接触排序/count-scan-emit/固定 gather）、1000 step 位级一致、−31%~−81% 吞吐代价、Warp#1355 对照（37×/+44GB/丢 capture） | 已核（PR 文本检索全文）；**合并状态未核**；数字为 PR 作者实测口径（RTX PRO 6000，非本机） |
| 10 | https://www.vulkan.org/news/auto-23155-a676f167a3982c6a4f6d36a46284cad8 | Vulkan 1.4 官方发布：streaming transfers、push descriptors/dynamic rendering local read/scalar block layout 强制、maintenance6、8K/8RT | 已核（Khronos 官方新闻检索全文） |
| 11 | https://baike.baidu.com/item/Vulkan/17543632 | Vulkan 1.4 时间线（2024-12）、2026 路线图 VRS 强制、Blender 5.3 默认 Vulkan、Vulkan SC 确定性要求 | **转述**（百科聚合，原始出处 IT之家/快科技/vulkan.org 未逐一抓） |
| 12 | https://github.com/nvidia-holoscan/holoscan-sdk | "Holoscan SDK is licensed under the Apache License, Version 2.0" | 已核（官方 README 搜索快照） |
| 13 | https://nvidia-holoscan.github.io/blog/2026-05-13-holoscan-sdk-4.2/ | 4.2：GPU-resident DAG、SCHED_DEADLINE benchmark、栈内 nsys 2026.2.1、event-based scheduler | 已核（官方博客检索全文） |
| 14 | https://www.intelligenthq.com/nvidia-ecosystem/products/holoscan/ | Holoscan 4.6.0（2026-09-03）为最新、平台/驱动要求、GPUDirect RDMA 需 Quadro/RTX | **转述/未核**（第三方 profile；与仓库 5.0 口径冲突，待 releases 页裁决） |
| 15 | https://github.com/longxiang-ai/awesome-gaussians | 3DGS 生态类目计数（Applications 500+ 等）、持续更新 | 已核（目录存在与分类）；计数为第三方口径 |
| 16 | UE5 Nanite/Lumen 中文解读（CSDN 多篇，URL 见检索记录） | Nanite 虚拟微多边形/免 LOD、Lumen 动态 GI/反射 | **转述/未核**（Epic 官方文档未抓） |
| 17 | 仓库内部：projects/nsight-cupti.md、deep/2026-11-05-nsight-cupti.md、projects/rerun.md、2026-10-26-stage-d-environment-and-licenses.md、2026-11-13-six-direction-innovation-integration.md、PLAN.md 等（§5 表） | 本机实测数字、CUPTI 机制逐字核验、许可结论、座位与判据 | 已核（仓库既有核验链，引用不重测） |

> 数字纪律备忘：本 note 中 12.81/13.12 GB/s、0.49ms、186MB/s 均为**仓库 benchmarks 口径**（D1 note 实测）；≥30fps@1080p 为 3DGS 论文摘要一手；<0.2%/7.8%/2 个数量级为 FPNA 论文一手（V100/GH200 平台，非本机）；−31%~−81%/37×/44GB 为 MJWarp PR 作者实测口径（RTX PRO 6000，非本机）；134/135FPS、Holoscan 4.6/5.0、UE5 机制细节、Vulkan 路线图均为转述或未核。