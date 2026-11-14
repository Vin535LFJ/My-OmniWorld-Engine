# Nsight Systems / CUPTI 深度调研报告（W04 深度层）

> Trust label: [Research] —— 一等来源（论文原文 / 官方技术文档 / 官方源码 / 官方博客公告）；每条机制性陈述附 URL 与检索日期（2026-11-05，部分补充核验 2026-11-06，各处标注）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05（补充核验 2026-11-06）· 状态：首建 · 2026-11-06 独立核查修正 5 处（functionParams 不可改写、PER_THREAD 缓冲默认值分版本、HES 出处拆分、PCSampling3 结构名、多订阅者引文逐字化）
> 配套事实卡：[../projects/nsight-cupti.md](../projects/nsight-cupti.md)——许可/硬件/平台/边界等事实只链接不复述（许可→卡 §3，硬件门槛→卡 §4，能力边界清单→卡 §2）。

## 0. 摘要

**它是什么**：CUPTI（CUDA Profiling Tools Interface）是 NVIDIA 官方给**工具作者**用的 C API 库——它是 Nsight Systems、Nsight Compute、cuda-gdb 等工具观测 CUDA 程序的统一底座；Nsight Systems（nsys）则是把 CUPTI 信号与 OS 级采样拼成系统级时间线的**成品工具**。官方文档明确：*"Nsight Systems uses CUPTI for CUDA profiling"*（UserGuide，检索快照逐字，见 §5 条 2）。

**核心原理一句话**：观测分两路——**Callback API 在宿主线内同步拦截**每次 CUDA API 调用（进入/退出两个相位），**Activity API 异步把 GPU 侧事件（kernel、memcpy、synchronization）写进缓冲区**由 worker 线程批量交付；两条流水线靠 **correlationId** 对账，宿主自定义 id（如 tick 号）靠 **external correlation 的每线程 id 栈**注入。

**最关键的三个机制**：
1. **同步拦截（Callback API）**：订阅者注册一个回调函数，按 domain（RUNTIME_API/DRIVER_API/RESOURCE/NVTX…）在 API 调用的进入/退出点被调用——能拿到参数与返回值，但同一时刻全进程只允许一个订阅者。
2. **异步缓冲记录（Activity API）**：GPU 事件落进默认分配在**显存**的环形缓冲（默认 ~3 MB/缓冲，可容纳约 10 万条记录），CUPTI 内部 worker 线程按启发式周期把整块缓冲交付给工具——热路径便宜，代价是交付延迟与缓冲不足时**丢记录**（有计数器可查）。
3. **两级关联（correlationId + external correlation）**：correlationId 把"API 调用记录"与"它引发的 GPU activity"一对一挂钩；external correlation 把宿主侧任意 uint64 id（对我们就地映射为 tick 号）通过 per-thread push/pop 栈写进 activity 流，机器可 join。

**对本项目的意义**：D5 判据"拷贝次数 + 同步点"的两根主轴，在这套工具链里有**逐字对应的原始记录**（`CUPTI_ACTIVITY_KIND_MEMCPY` / `CUPTI_ACTIVITY_KIND_SYNCHRONIZATION` 表，事实卡 §5 已实测）；tick 号↔GPU 活动的挂钩有官方通道（CUSTOM 类 external correlation + NVTX range 投影）。时间线用于**解释与取证**，性能数字仍按仓库纪律只认 Release 构建 + `benchmarks/regression/`。

## 1. 项目定位与生态位

CUPTI 与 Nsight Systems 是**同一观测栈的两个抽象层**，不是两个并列工具：

```
[成品工具层]  Nsight Systems (nsys CLI / GUI)  ←  Nsight Compute  ←  cuda-gdb …
                 │ 注入 (CUDA_INJECTION64_PATH) / 捆绑 CUPTI 库
[观测基座层]  CUPTI（动态库，随 CUDA Toolkit 分发；工具作者的 C API）
                 │ 观察
[被观测对象]  CUDA 应用（runtime/driver API、kernel、memcpy、synchronization…）
```

- CUPTI 官方自述：*"provides C and Python APIs for building profiling and tracing tools for CUDA applications"*；*"Activity records describe executed work (for example, kernels and memory operations), while callbacks notify your tool when specific CUDA events occur, enabling low-overhead, event-driven trace collection."*（https://docs.nvidia.com/cuda/cupti/ ，v2026.4.0，2026-11-05）
- 关系判据（官方、非推断）：CUPTI 文档写明多订阅者冲突——旧版 *"Only a single subscriber can be registered at a time"*，且当应用被 **Nsight Systems / Nsight Compute / cuda-gdb / cuda-memcheck** 启动时，再订阅会返回 `CUPTI_ERROR_MULTIPLE_SUBSCRIBERS_NOT_SUPPORTED`（https://docs.nvidia.com/cupti/api/group__CUPTI__CALLBACK__API.html ，2026-11-06）——**这句话反过来证明 Nsight Systems 本身就是一个 CUPTI 订阅者**。Nsight Systems Release Notes 同向佐证：*"When profiling a framework or application that uses CUPTI, like some versions of TensorFlow(tm), Nsight Systems will not be able to trace CUDA"*（https://docs.nvidia.com/nsight-systems/ReleaseNotes/index.html ，检索快照，2026-11-06）。
- 生态位归属、D5 接口、竞品参照（Nsight Compute 的分工线）见事实卡 §6，不重复。

## 2. 原理详解

### 2.1 架构总览：注入、订阅、两条记录流水线

**怎么做**。CUPTI 是随 Toolkit 分发的动态库（Windows 在 `<CUDA>/extras/CUPTI/`，本机实物 `cupti64_2024.3.0.dll`），初始化是惰性的：*"CUPTI initializes lazily the first time an application invokes a CUPTI function."*（https://docs.nvidia.com/cupti/main/main.html ，2026-11-06）。集成方式有两种，官方 sample 直接演示了第二种：
1. **进程内直连**：工具/应用自己链接 CUPTI 并调用 API（本仓库将来若做"程序内自观测"即此路）。
2. **注入（injection）**：CUDA 运行时在初始化时按环境变量加载注入库——官方 `cupti_trace_injection` sample README 逐字：*"When CUDA_INJECTION64_PATH is set to a shared library, at initialization, CUDA will load the shared object and call the function named 'InitializeInjection'. CUDA application needs not to be modified."*；NVTX 侧对应 `NVTX_INJECTION64_PATH` 指向 CUPTI 库本身；Windows 构建该 sample 用 **Microsoft Detours**（README 步骤 1.3）——与 nsys 的 `--injection-use-detours` 文档（"Use detours for injection. If false, process injection will be performed by windows hooks"，https://docs.nvidia.com/nsight-systems/UserGuide/index.html ，2026-11-06）互相印证注入是 nsys 的采集入口。**被观测程序零修改**。

数据流分两条独立流水线，最后在同一时间基（ns 时间戳）汇合：
- **拦截流水线**（Callback API）：API 调用点同步触发，工具可查参数/返回值——§2.2；
- **记录流水线**（Activity API）：GPU 事件异步落缓冲，worker 线程批量交付——§2.3；
- 汇合点：correlationId / external correlation / 统一 ns 时间戳（§2.4），工具侧（Nsight Systems）再拼装成时间线（§2.6）。

**为什么这样设计**。把"能修改目标行为的钩子"（callback）与"只读旁路记录"（activity）拆开，是观测工具的经典分层：拦截层按需开销大（每个 API 调用都要过钩子），记录层摊销后开销小（写缓冲 + 批量交付）。工具按需启用其一或两者，Nsight Systems 两者都用（API 行 + GPU 行同屏）。

**代价**。注入路径依赖 CUDA 运行时的加载钩子与环境变量约定（`CUDA_INJECTION64_PATH`），意味着观测能力受限于官方约定的注入点；进程内直连则受单订阅者仲裁限制（§2.2 的代价）。此外 CUPTI 文档区分"tracing"与"profiling"：后者 *"may require replaying kernels or even the full application multiple times to gather all desired metrics under controlled conditions"*（https://docs.nvidia.com/cuda/cupti/ ，2026-11-05）——本报告聚焦 tracing（Nsight Systems 用途）。

### 2.2 Callback API：宿主线上的同步拦截点

**怎么做**。订阅模型三步（https://docs.nvidia.com/cupti/api/group__CUPTI__CALLBACK__API.html ，2026-11-06）：
1. `cuptiSubscribe(&subscriber, CUpti_CallbackFunc, void *userdata)`——*"Initialize a callback subscriber with a callback function and user data."*，注意 *"This function does not enable any callbacks."*（注册≠启用）；
2. `cuptiEnableCallback / cuptiEnableDomain / cuptiEnableAllDomains` 按 domain+cbid 选择性启用——粒度可以细到单个 API；
3. 回调签名 `void (*)(void *userdata, CUpti_CallbackDomain domain, CUpti_CallbackId cbid, const void *cbdata)`，domain 决定 `cbdata` 的实际类型：`DRIVER_API/RUNTIME_API → CUpti_CallbackData`、`RESOURCE → CUpti_ResourceData`、`SYNCHRONIZE → CUpti_SynchronizeData`、`NVTX → CUpti_NvtxData`。

`CUpti_CallbackData` 的关键载荷（https://docs.nvidia.com/cupti/api/structCUpti__CallbackData.html ，2026-11-06）：
- `callbackSite`：`CUPTI_API_ENTER = 0` / `CUPTI_API_EXIT = 1`（本机 CUDA 12.6 头文件 `cupti_callbacks.h:88-98` 逐字一致；**无 QUEUED 相位**——见 §5 条 4）；
- `functionParams`：*"Pointer to the arguments passed to the runtime or driver API call."*——官方文档仅称其为指向实参的指针，**未陈述可改写**；本机 12.6 头文件声明为 `const void *`（`cupti_callbacks.h:339`）；本机全部 33 个 sample 中该字段仅见只读取用（`callback_timestamp.cu:127-128` 读 memcpy 参数的 count/kind；`cupti_nvtx.cu:241/248/253` 为注释掉的只读示例），`callback_event.cu` 中 0 命中——**无任何官方改写演示**（2026-11-06 grep 实测）；
- `functionReturnValue`：*"This field is only valid within the exit::CUPTI_API_EXIT callback."*；
- `correlationData`：*"Pointer to data shared between the entry and exit callbacks of a given runtime or driver API function invocation."*——同一调用的 ENTER→EXIT 配对机制（例如在 ENTER 记起点、EXIT 算时长）；
- `symbolName`：仅对 launch 类回调给出内核名；
- 生命周期红线：*"The callback data is valid only within the invocation of the callback function that is passed the data."*——要留就得深拷贝；`functionName` 例外（*"a global constant"*）。

**为什么这样设计**。回调点直接焊在 CUDA runtime/driver 的 API 入口/出口上（这从 domain 定义 *"Each domain represents callback points for a group of related API functions or CUDA driver activity."* 与"回调内只能合法调用极少数同步函数"的约束可以反推——见下面"代价"），这带来两个能力：**在执行前后拿到完整参数/返回值**（旁路记录做不到），以及**零丢样**（每个调用必然触发，不像采样）。线程归属的官方明文在我抓到的页面没有直接陈述，但从三点可推断（标注**推断**）：① 回调发生在 ENTER/EXIT 相位之间，语义上内联于调用；② 官方文档警告回调内乱调 CUDA API *"may cause the application to hang"*（https://docs.nvidia.com/cupti/main/main.html ，2026-11-06）——只有回调占据应用线程才可能死锁自己；③ STATE domain 的个别回调被明文标注 *"can be invoked on CUPTI's internal worker thread"*（Callback API 页）——"个别"反衬其余默认就在应用线程。

**代价**。① **热路径税**：回调内联在每次 CUDA API 调用里，官方只说回调 *"should return quickly"*（Usage 页），无量化数字（**UNVERIFIED**，§5 条 1）；② **单订阅者仲裁**：*"Only a single subscriber can be registered at a time."*，且被 Nsight Systems 等工具启动时直接返回 `CUPTI_ERROR_MULTIPLE_SUBSCRIBERS_NOT_SUPPORTED`——**自研 CUPTI 工具与 nsys 采集不能同时挂同一进程**（CUDA 13.3 起的 V2 API 支持多订阅者：*"Multiple subscriber support requires CUDA 13.3+ and driver r610+."*，但范围受限——*"Multiple subscriber support applies only to the CUPTI Activity API. It is not applicable to CUPTI Profiling APIs (such as the Range Profiler or PM Sampling APIs)."*；与调试器共存仍不被支持：*"CUPTI still does not support tracing or profiling along with other NVIDIA Developer Tools like cuda-gdb or Compute Sanitizer."*，https://docs.nvidia.com/cupti/main/main.html ，2026-11-06 已下载全文 grep 原句核验）；③ **重入限制**：回调内只允许 `cudaDeviceSynchronize`、`cuStreamSynchronize` 等少数调用，其他 CUDA 调用可能挂死应用；从回调 flush activity 只允许在 RUNTIME/DRIVER API 回调里做（Usage 页）；④ 订阅者的 enable 函数族需要调用方自行串行化：*"a subscriber must serialize access to cuptiGetCallbackState, cuptiEnableCallback, cuptiEnableDomain, and cuptiEnableAllDomains"*（Callback API 页）。

### 2.3 Activity API：异步缓冲的 GPU 事件流水线

**怎么做**。三个环节（https://docs.nvidia.com/cupti/api/group__CUPTI__ACTIVITY__API.html ，2026-11-06；缓冲区细节引本机 CUDA 12.6 头文件 `cupti_activity.h`）：
1. **启用**：`cuptiActivityEnable(kind)` 选择记录种类。头文件编号逐字：`MEMCPY=1`、`MEMSET=2`、`KERNEL=3`、`DRIVER=4`、`RUNTIME=5`、`SYNCHRONIZATION=38`、`EXTERNAL_CORRELATION=39`（`cupti_activity.h:194/200/209/215/221/458/465`）。⚠️ `KERNEL` 与 `CONCURRENT_KERNEL` 是**两种不同的观测**：前者官方注释 *"may significantly change the overall performance characteristics of the application because all kernel executions are serialized on the GPU"*，后者 *"doesn't break kernel concurrency"*（`cupti_activity.h:202-206, 248-250`）——观测配置本身会改写并发结构，这是 Activity API 最重要的坑。
2. **缓冲**：工具通过 `cuptiActivityRegisterCallbacks` 注册两个回调——buffer request（CUPTI 要空缓冲）与 buffer completion（CUPTI 还整块缓冲）。缓冲池参数（头文件注释逐字）：单缓冲默认 *"3200000 (~3MB) which can accommodate profiling data up to 100,000 kernels, memcopies and memsets combined"*；每 context 缓冲池上限 250；预分配 3 块 *"to ping pong between these buffers (if possible)"*（本机 12.6 头文件 `cupti_activity.h:6639`）；每线程独立缓冲**按版本不同**：当前在线 CUPTI 文档 `CUPTI_ACTIVITY_ATTR_PER_THREAD_ACTIVITY_BUFFER` *"The default value is 1."*，而本机 CUDA 12.6 头文件同属性明文 *"The default value is 0."*（`cupti_activity.h:6690` 附近；紧邻的 `CUPTI_ACTIVITY_ATTR_MEM_ALLOCATION_TYPE_HOST_PINNED` 才是 *"The default value is 1."*）——**依赖默认值不可靠，按目标 CUDA 版本显式设定**。CUDA 12.0 Update 1 起 *"CUPTI allocates the profiling buffer in the device memory by default"*——trace 数据直接落在显存里，采集不占 PCIe 带宽。工具侧推荐缓冲 1–10 MB（Usage 页）。交付由 CUPTI 内部 worker 线程执行，头文件注释逐字：*"CUPTI creates a worker thread to minimize the perturbance for the application created threads. CUPTI offloads certain operations from the application threads to the worker thread, this includes synchronization of profiling resources between host and device, delivery of the activity buffers to the client..."*；可用 `cuptiActivityFlushPeriod(ms)` 覆盖启发式周期，且 *"Periodic flush can return only those activity buffers which are full and have all the activity records completed."*（`cupti_activity.h:7509-7527`）。
3. **解析**：拿到缓冲后用 `cuptiActivityGetNextRecord` 遍历，按 `kind` 分发到具体记录结构。`CUpti_ActivityMemcpy5` 字段逐字核验（`cupti_activity.h:2004-2121`）：`copyKind`（H2D/D2H/DtoD 等）、`bytes`、`start/end`（ns）、`deviceId/contextId/streamId`、`correlationId`、`runtimeCorrelationId`；`CUpti_ActivityAPI` 记录带 `start/end`（ns）与 `correlationId`（*"This ID matches the correlation ID of the associated kernel, memcpy, or memset activity record."*）。

**记录的时间戳有第三层**：kernel 记录除 `start/end` 外还有 `queued`（*"The timestamp when the kernel is queued up in the command buffer, in ns."*）与 `submitted`（*"The timestamp when the command buffer containing the kernel launch is submitted to the GPU, in ns."*）——但 *"This timestamp is not collected by default. Use API cuptiActivityEnableLatencyTimestamps() to enable collection."*（`cupti_activity.h:2914-2936`）。这一组时间戳正好度量"宿主发起 → GPU 开始执行"之间的流水线延迟，即"同步点/排队"判据的直接观测。

**为什么这样设计**。异步缓冲把"记录事件"与"处理事件"解耦：热路径只做内存写入（GPU 侧），格式化/落盘/展示全在 worker 线程批量进行——这是"低扰动"目标的直接工程化。每线程缓冲避免多线程应用在缓冲请求上串行排队（官方动机 *"reduce contention"*，Usage 页）；显存缓冲则把 trace 写入从 PCIe 挪到显存内部带宽。

**代价**。① **丢样风险**：缓冲不足时记录被丢弃，但**有账可查**——`cuptiActivityGetNumDroppedRecords`：*"Get the number of activity records that were dropped of insufficient buffer space."*（Activity API 页）；② **交付延迟**：周期 flush 只交"满且完整"的缓冲（头文件注释），尾部数据要靠显式 `cuptiActivityFlushAll` 收口；nsys 侧对应 `--cuda-flush-interval`（*"For collections over 30 seconds, an interval of 10 seconds is recommended."*，UserGuide）；③ **配置敏感**：KERNEL vs CONCURRENT_KERNEL、graph 的 node 粒度追踪 *"may cause significant runtime overhead"*（UserGuide）——观测配置参与塑造被测系统；④ HES（Hardware Event System）**两层归属不同**：CUPTI 层是 **opt-in** 功能（Usage 页原句 *"This is an opt-in feature in CUPTI, and the new CUPTI API, cuptiActivityEnableHWTrace enables the collection of kernel timestamps through hardware events."*），硬件缓冲 *"CUPTI uses a 128 MB buffer by default."*，不定期 flush 则丢数据（*"When the buffer reaches capacity, new records would be dropped."*——Usage 页，2026-11-06 已下载全文 grep 原句核验）；nsys 层在支持的系统上默认走 HES（Release Notes 原句 *"CUDA hardware trace, also called Hardware Event System (HES) trace, is the default CUDA trace method on supported systems."*，2026-11-06 下载全文核验；UserGuide 补充 "beginning with Blackwell" 与 `cuda-sw` 回退软件追踪的开关，§2.6）。

### 2.4 correlationId 与 external correlation：把 GPU 活动挂到宿主 id

**怎么做**——两级结构：

**第一级（CUPTI 内建，自动）**：correlationId。官方定义逐字：*"A correlation ID is an integer value that links a CUDA API call to the GPU activity records generated by that call."*，且 *"IDs are only generated when RUNTIME/DRIVER activity kinds are enabled"*（https://docs.nvidia.com/cupti/main/main.html ，2026-11-06）。头文件同义：kernel 的 correlationId *"identical to the correlation ID in the driver or runtime API activity record that launched the kernel"*（`cupti_activity.h:2888-2894`）；memcpy 同（2068-2081，且分 `correlationId`（driver API 侧）与 `runtimeCorrelationId`（runtime API 侧）两根）。用法：同时启用 API 与 GPU activity kinds，采集后 *"Match by correlation ID"*，再用 `contextId/streamId/deviceId` 精化，graph 场景再加 `graphId/graphNodeId`。无效值哨兵：`CUPTI_CORRELATION_ID_UNKNOWN`。

**第二级（宿主注入，按需）**：external correlation。机制是**每线程 id 栈**：`cuptiActivityPushExternalCorrelationId(kind, id)` 头文件逐字 *"This function notifies CUPTI that the calling thread is entering an external API region."*，`Pop` 对应离开（`cupti_activity.h:7463-7481`）；栈生效期间该线程产生的每条 CUDA activity 都会附生一条 `CUpti_ActivityExternalCorrelation` 记录，字段为 `externalKind` + `externalId: uint64_t` + `correlationId`（`cupti_activity.h:5746-5773`）。kind 枚举含 `OPENACC=2` 与 **`CUSTOM0=3 / CUSTOM1=4 / CUSTOM2=5`**，注释逐字 *"Custom correlation kinds are reserved for usage in external tools."*（`cupti_activity.h:5697-5735`）——**把 tick 号当 `externalId` 压栈，activity 流里就能逐条回查每个 kernel/memcpy 属于哪个 tick**。官方 sample `cupti_external_correlation` 完整演示：伪代码注释 *"cuptiActivityPushExternalCorrelationId() → ExternalAPI() → cuptiActivityPopExternalCorrelationId() —— All CUDA activity within this range will generate external correlation record which then can be used to correlate it with the external API"*（本机 `extras/CUPTI/samples/cupti_external_correlation/cupti_external_correlation.cu:11-16`），采集端把 `EXTERNAL_CORRELATION` 记录收进 `externalId → [correlationId...]` 的 map（同文件 210-227 行），setup 里先启用 `RUNTIME` kind 并注释 *"Enable CUDA runtime activity kinds for CUPTI to provide correlation ids."*（248 行）——再次印证第一级关联的前置条件。

**为什么这样设计**。两级解耦各有道理：第一级由 CUPTI 全自动维护，保证任何工具都能把 API 调用与其 GPU 效果对上账，不要求被测程序配合；第二级 opt-in，把"宿主世界的编号体系"（OpenACC/MPI/任意框架，乃至我们的 tick）注入到 CUPTI 的时间线里而不污染前者的语义。`externalId` 选 `uint64_t`、kind 预留 `CUSTOM0-2` 给外部工具，是官方给"非 OpenACC 体系"留的口子。线程局部栈的取舍：id 跟随线程而非全局变量，天然适配"一个宿主线内嵌套 region"的模式（与 NVTX push/pop 语义同构），但**跨线程提交的工作**需要每个工作线程各自 push（本机 sample 是单线程演示；跨线程形态官方未展开——见 §5 条 5）。

**代价**。① 第一级要求 RUNTIME/DRIVER activity 开启——多一类记录流，采集量上升；② 第二级每条 CUDA activity 附生一条 EXTERNAL_CORRELATION 记录（activity 流再膨胀）；③ push/pop 是 per-thread 栈，宿主侧若有"多线程共享一个 tick"的形态，得自行设计 id 分配约定（这层是使用约定，非工具缺陷）；④ NVTX 路线（第三条通道）给人看不进 SQL：Nsight Systems 会 *"project these ranges onto the GPU timeline, allowing you to see what GPU activity was launched within each CPU range"*（https://docs.nvidia.com/nsight-systems/UserGuide/index.html ，2026-11-06），投影依赖 nsys 的 NVTX 注入采集（NVTX README：*"The CUPTI API supports recording traces of NVTX calls"*，github.com/NVIDIA/NVTX，事实卡 §1 已核）。三条通道分工（NVTX 投影给人看 / external correlation 给机器 join / correlationId 是 CUPTI 内部对账）承事实卡 §5，此处补足机制出处。

### 2.5 PC Sampling：GPU 硬件级的 warp 程序计数器采样

**怎么做**。官方一句话定位：*"Sampling of the warp program counter and warp scheduler state (stall reasons)"*（https://docs.nvidia.com/cuda/cupti/ ，2026-11-05）。硬件按周期采样在跑 warp 的 PC 与调度器状态，产出 `(pcOffset, stallReason, samples)` 记录——本机 CUDA 12.6 头文件 `CUpti_ActivityPCSampling3` 字段逐字：*"Number of times the PC was sampled with the stallReason in the record."* + `stallReason` + `pcOffset`（`cupti_activity.h:4420-4444`；无后缀的 `CUpti_ActivityPCSampling` 是弃用版本，位于 `cupti_activity_deprecated.h:4309`）。可配置项（https://docs.nvidia.com/cupti/api/group__CUPTI__PCSAMPLING__API.html ，2026-11-06，引述逐字）：
- 采样周期：*"Valid values for the sampling periods are between 5 to 31 both inclusive. This will set the sampling period to (2^samplingPeriod) cycles."*（默认按 SM 数由 CUPTI 定）；
- 两级缓冲：SW 缓冲 *"DEFAULT - 1 MB, which can accommodate approximately 5500 PCs with all stall reasons"*、HW 缓冲 *"DEFAULT - 512 MB. If sampling period is too less, HW buffer can overflow and drop PC data"*；存储成本模型官方给出：*"1 PC with 1 stall reason = 32 Bytes"*；
- 两种收集模式：`CONTINUOUS`（默认，*"Kernels are not serialized in this mode."*）与 `KERNEL_SERIALIZED`（*"Kernels are serialized in this mode."*）；
- 取数：`cuptiPCSamplingGetData` *"never does device synchronization"*，但在 continuous 模式 *"after every module load-unload-load"、serialized 模式 *"after every kernel ends"* 需要显式取数。

硬件门槛（官方 sample 逐字）：*"PC sampling is supported on devices with compute capability 5.2 or 6.0 and higher."*（本机 `extras/CUPTI/samples/pc_sampling/pc_sampling.cu:5-6, 157`）。

**为什么这样设计**。PC sampling 是**统计采样而非逐事件插桩**：观测装置长在 SM 的调度器路径上，每个采样周期记一次"所有 warp 在哪、为什么停"，从而以近常数开销把"时间花在哪条指令、哪种 stall"的归因变成可能——这是 Nsight Compute per-kernel 深钻（Source 页的 stall 归因）的底层来源。周期用 2 的幂（2^5=32 到 2^31 周期）是硬件计数器的自然选择：一个移位寄存器即可实现，无需软件介入热路径。

**代价**。① **硬件门槛**：CC 5.2/6.0+ 才有该采样单元；本机 RTX 4060 Laptop（CC 8.9）在支持范围内（事实卡 §4 已链 D1 实测）；② **丢数据形态**：周期太小 → HW 512 MB 缓冲溢出丢 PC 数据（官方明文）；③ **模式取舍**：serialized 模式为了每 kernel 收集干净而破坏并发——又见"观测改写被测系统"；④ 用途边界：它回答"kernel 内部瓶颈在哪"，不回答"系统级谁等谁"——与 Nsight Systems 的分工见事实卡 §6。

### 2.6 Nsight Systems 时间线组装：从注入到 SQLite

**怎么做**——四步流水线（每步有官方出处）：
1. **注入采集**：nsys 启动目标进程时注入 CUDA injection 库（Windows 用 Detours 或 windows hooks，`--injection-use-detours`，UserGuide 2026-11-06）；注入库作为 CUPTI 订阅者开启 API + GPU activity 采集（官方 UserGuide 载 *"Nsight Systems uses CUPTI for CUDA profiling, including to collect the CUDA memory usage by the application processes"*——检索快照逐字，直接抓取该句位于截断点之后，见 §5 条 2）。NVTX 采集走同一注入框架的 NVTX 通道（`NVTX_INJECTION64_PATH` 指向 CUPTI 库，trace_injection README；NVTX README："Nsight Systems logs NVTX calls and shows them on a timeline alongside driver/OS/hardware events"，github.com/NVIDIA/NVTX）。
2. **多源汇流**：时间线上的每个"行"来自一个采集器——CUDA（注入 CUPTI）、NVTX（同上）、OS runtime（`--osrt-threshold` 控制）、CPU sampling / context switch（需管理员，非管理员自动禁用——事实卡 §2 本机实测）、Python GIL、DX12/Vulkan 等。各采集器记录都带 ns 级时间戳，落在同一条流上。
3. **落盘**：采集过程中数据流式写入 **`.qdstrm` 中间文件**，运行结束后 *"This version of Nsight Systems will attempt to automatically convert the .qdstrm file to a .nsys-rep file with the same name after the run finishes"*（UserGuide 检索快照，2026-11-06；`nsys import` 可手工转换，*"Use the same nsys version that generated the .qdstrm file to convert it."*——格式与版本绑定，事实卡 §5 实测 `.nsys-rep`+`.qdstrm` 双产物）。
4. **表化导出**：`nsys export --type sqlite|hdf|arrow|parquetdir|...`（官方文档；事实卡 §5 实测 SQLite）；导出的表名直接沿用 CUPTI activity kind 命名——本机实测 grep 到 `CUPTI_ACTIVITY_KIND_MEMCPY / RUNTIME / SYNCHRONIZATION / CUDA_EVENT` 表（事实卡 §5）。

**为什么这样设计**。"多采集器 + 单一时间线 + 后处理导出"是三笔交易的合成：
- **注入而非重链接/改代码**：被测程序零修改，任何 CUDA 程序立即可测（trace_injection README 的承诺 *"CUDA application needs not to be modified"* 的成品化）；
- **中间格式 `.qdstrm` 与报告 `.nsys-rep` 分离**：采集器崩溃/没跑完报告时原始流还在，可事后 `nsys import` 补报告——把"采集稳定性"与"报告生成"解耦；
- **导出选 SQLite/HDF/Arrow 等通用格式**：官方把"进数据管线"当一级用途（`nsys export --type` 的选项即证据），所以"SQL join 自家 tick 表"不需要逆向任何格式。

**代价**。① **默认减配**：*"By default, Nsight Systems skips CUDA APIs that are not critical for performance analysis."*，全量要 `--cuda-trace-all-apis` 且 *"This feature may cause significant runtime overhead."*（UserGuide，2026-11-06）——默认时间线上的 API 序列是官方筛过的子集；② **逐项开销警告贯穿选项表**：`--cuda-memory-usage`、`--cuda-um-*-page-faults`、graph node 粒度、`--cudabacktrace` 都标 *"significant runtime overhead"*；graph 的图级追踪则是默认且 *"reduces overhead"*，NVTX 投影的 graph 模式 *"adds no overhead"*——同一时间线上不同行的代价差几个量级；③ **权限门槛**：CPU context switch/sampling 需管理员（事实卡 §2 实测：非管理员自动禁用并 warning）；Windows CLI 要 administrator（事实卡 §4 链 InstallationGuide）；④ **版本耦合**：`.qdstrm`→`.nsys-rep` 要求同版本 nsys（上文官方明文），归档 trace 时必须连版本号一起存；⑤ 时间线上多源时钟如何对齐（CPU 采样时钟 vs CUDA 时间戳）在已抓取的官方页面中**没有机制说明**（**UNVERIFIED**，§5 条 3）。

## 3. 能力边界与工程现实

机制性限制（官方承认，逐条有出处）与事实卡互链，此处只列卡 §2 装不下的**机制成因**：

| 边界 | 机制成因（本报告 §2 出处） | 对工程动作的含义 |
|---|---|---|
| **同一进程只一个 CUPTI 订阅者**（旧版；V2 多订阅者仅限 Activity API，且不能与 cuda-gdb/Compute Sanitizer 共存） | §2.2：单订阅者仲裁 + `CUPTI_ERROR_MULTIPLE_SUBSCRIBERS_NOT_SUPPORTED` | "nsys 采集"与"程序内自研 CUPTI 观测"**二选一**，不能叠加；若做程序内观测，构建须默认 OFF（ADR-0012 模式，事实卡 §6） |
| **观测配置改写被测系统** | §2.3：`KERNEL` kind 强制 GPU 上 kernel 串行（官方注释逐字）；graph node 粒度/all-API 追踪标 significant overhead | 对照实验时先固定采集配置；发现"性能形态变了"先查 activity kind 选择 |
| **采集有丢样** | §2.3：缓冲不足丢记录，`cuptiActivityGetNumDroppedRecords` 可查 | 用 trace 做计数证据（memcpy/sync 次数）时，**丢记录数必须一起读出**，否则计数不可信 |
| **尾部数据不即时** | §2.3：周期 flush 只交"满且完整"的缓冲 | 取证前显式收口（程序内：`cuptiActivityFlushAll`；nsys：`--cuda-flush-interval` + 正常退出） |
| **PC sampling 门槛与丢样** | §2.5：CC 5.2/6.0+；HW 缓冲溢出丢数据 | per-kernel 深钻归 Nsight Compute；本引擎现阶段无此需求 |
| **时间线是子集视图** | §2.6：默认跳过"not critical"的 API；时钟对齐机制官方未说明 | 时间线回答"发生了什么、谁等谁"；不承载性能主张（承事实卡 §2 推断） |

其余边界（采集范围、平台、Profiling API 弃用、CUPTI Python 仅 Linux 等）→ 事实卡 §2；许可与再分发 → 事实卡 §3；硬件门槛 → 事实卡 §4。

## 4. 与 OmniWorld 的关系深化（World Contract 视角）

以 [World Contract 四轴](../../design/00-总体设计.md)（identity / time / ownership / confidence）逐轴映射：

- **identity——tick 号进 GPU 时间线的官方通道**。`externalId` 是 `uint64_t`、`CUSTOM0-2` kind 官方声明 *"reserved for usage in external tools"*（§2.4，头文件行号已核）——tick 号直接压栈即成结构化事实，`externalId → [correlationId...] → kernel/memcpy 记录` 的 join 链全程官方 API，无需文本解析。GUI 侧平行通道：每 tick 一个 NVTX push/pop range（payload 带 tick 号），nsys 把 range 投影到 GPU timeline（§2.6）。注意两通道粒度不同：external correlation 记录精确到"该线程压栈区间内产生的每条 activity"，NVTX 投影是"区间套区间"的视觉归属——取证用前者，讲解用后者。
- **time——三组时间戳恰好覆盖"同步点"判据的三个相位**。Activity 记录的 `start/end`（ns）给"GPU 上实际发生"；`queued/submitted`（需 `cuptiActivityEnableLatencyTimestamps` 显式开启）给"宿主排队 → 命令缓冲提交"的流水线延迟（§2.3）；SYNCHRONIZATION activity（kind 38，事实卡 §5 实测 SQLite 表）给"同步调用本身"。D5 判据"拷贝次数 + 同步点为主、字节数为辅"的三种原料在这套记录里**全部原子存在**，改造前后对比 = 同配置两次采集的同表差分。⚠️ 引擎自身的位级确定性（同 seed 逐字节复现）与观测时间戳无关：nsys 数字是**证据**不是基准，性能主张仍只认 Release 构建 + `benchmarks/regression/`（AGENTS.md 纪律，承事实卡 §2 推断）。
- **ownership——观测是读操作，但订阅权本身是互斥资源**。CUPTI 不写任何世界状态（World Contract 的 ownership 不受污染）；但单订阅者仲裁（§2.2）意味着"谁挂上观测"是一次性独占决定：nsys 采集期间引擎不能同时自观测，反之亦然。这决定了接线顺序：**开发期取证用 nsys（外部、零代码），运行时自观测（若做）默认 OFF 且与 nsys 互斥**——与事实卡 §6"默认交付不携带 CUPTI 运行库"的判断在机制层面对上了。
- **confidence——trace 证据要带"证据的元数据"**。Activity API 有明确的丢样计数器（§2.3），nsys 采集有明确的配置面（`--cuda-trace-all-apis`、`--cuda-graph-trace`、`--cuda-flush-interval`、activity kind 子集）与权限降级（非管理员丢 CPU sampling）。按本仓库"数字必须标注模式与环境元数据"的纪律，trace 证据的产出记录应包含：nsys 版本（`.qdstrm` 转换绑版本，§2.6）、采集配置 flag、管理员与否、丢记录数（程序内路径）。"改造前后逐 tick 的 memcpy/同步计数对比"要成立，前提是两次采集**配置逐项相同**且丢样数都为零或同量级。

## 5. 未解决问题 + 证据缺口

**UNVERIFIED 清单**（沿用事实卡 §7 未决项中与本报告机制层相关的，注明本报告新增项）：

1. **CUPTI 追踪开销的官方量化数字**——已抓取的 CUPTI 页面（landing/usage/callbacks/activity/pcsampling）均只有定性措辞（"low-overhead"、"should return quickly"、"significant runtime overhead" 针对特定选项），无百分比。承事实卡 §7.1。若做 CUPTI 直连，先实测带/不带采集的 tick 时延差。
2. **"Nsight Systems uses CUPTI for CUDA profiling" 原句的核验方式**——句子出现在官方 UserGuide（搜索索引快照逐字返回该句），但直接抓取 HTML 时页面截断于该句之前、PDF 超过抓取上限（10.9 MB）。已按官方页面文本采信并注明核验路径；如需逐字落 PDF 页码，抓 UserGuide PDF 分节版。
3. **Nsight Systems 多源时钟对齐机制**（CPU 采样时钟、ETW/OS runtime 事件与 CUPTI ns 时间戳如何统一时间基）——官方文档（已抓取范围）无机制说明。时间线的跨行相对次序按"统一 ns 时间戳"理解（各记录自带 ns 字段），对齐细节 **UNVERIFIED**。
4. **`CUPTI_API_QUEUED` 相位**——在线文档的 `CUpti_ApiCallbackSite` 只列 ENTER/EXIT，本机 CUDA 12.6 头文件（`cupti_callbacks.h:88-98`）也只有这两个值；更高版本（CUDA 13+）是否新增 QUEUED 相位**未核验**（不臆测）。
5. **external correlation 在 nsys 导出件中的落表形态**——CUPTI API 层已逐字核验（本机头文件 + 官方 sample），但"nsys 采集外部应用的 CUSTOM0 externalId 后，SQLite 里 EXTERNAL_CORRELATION 数据如何呈现（独立表还是折叠进各 activity 表）"未实测（承事实卡 §7.5）。D5 接入 tick↔GPU 关联时跑一次端到端验证。
6. **跨线程 external correlation 的官方指引**——push/pop 语义明文是 per-thread（"calling thread"，头文件 7463-7481），官方 sample 为单线程；"工作线程各自 push 什么 id"这类多线程形态官方未展开，属使用约定层面（本报告 §2.4 已按机制如实陈述，不引申）。

**证据缺口→行动**（对应重查触发条件）：D5 GPU 数据流切片动工时，§5.5 的端到端验证与 §4 confidence 轴的"证据元数据"清单应一起落地；在此之前本报告不支撑任何 CUPTI 直连决策。

## 6. 来源清单

检索/实测日期：2026-11-05（landing 页、overview、产品页）；2026-11-06（补充：API 模块页、Usage 页、UserGuide、samples 与本机头文件）。标注 *快照* 者为搜索引擎返回的官方页面原文片段（直接抓取截断，见 §5.2）。

**官方在线文档：**

1. CUPTI 总览（v2026.4.0，Last updated Nov 02, 2026）：https://docs.nvidia.com/cuda/cupti/ （2026-11-05）
2. CUPTI Usage（main.html，v2026.4.0）：https://docs.nvidia.com/cupti/main/main.html （2026-11-06；lazy init、correlation 定义、buffer 推荐 1–10 MB、多订阅者演进、HES 128 MB、回调内 CUDA 调用限制；2026-11-06 二次核验：下载全文 grep 原句——"Multiple subscriber support applies only to the CUPTI Activity API." / "This is an opt-in feature in CUPTI..." / "CUPTI uses a 128 MB buffer by default." / "When the buffer reaches capacity, new records would be dropped."）
3. CUPTI Callback API 模块页：https://docs.nvidia.com/cupti/api/group__CUPTI__CALLBACK__API.html （2026-11-06）
4. CUPTI Activity API 模块页：https://docs.nvidia.com/cupti/api/group__CUPTI__ACTIVITY__API.html （2026-11-06）
5. `CUpti_CallbackData` 结构页：https://docs.nvidia.com/cupti/api/structCUpti__CallbackData.html （2026-11-06）
6. CUPTI PC Sampling API 模块页：https://docs.nvidia.com/cupti/api/group__CUPTI__PCSAMPLING__API.html （2026-11-06）
7. CUPTI Overview（12.8 版存档，API 清单与平台）：https://docs.nvidia.com/cupti/overview/overview.html （2026-11-06）
8. Nsight Systems User Guide：https://docs.nvidia.com/nsight-systems/UserGuide/index.html （2026-11-05/06；`--cuda-*` 选项逐字、NVTX 投影、qdstrm 转换、`--injection-use-detours`；"uses CUPTI" 句为 *快照*）
9. Nsight Systems Release Notes：https://docs.nvidia.com/nsight-systems/ReleaseNotes/index.html （CUPTI 冲突句为 *快照*，2026-11-06；2026-11-06 二次核验：下载全文 grep 原句——"CUDA hardware trace, also called Hardware Event System (HES) trace, is the default CUDA trace method on supported systems."）

**官方源码（本机 CUDA Toolkit v12.6 实物，全部实际打开核验）：**

10. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/include/cupti_activity.h`——activity kind 编号（194/200/209/215/221/458/465）、KERNEL 串行警告（202-206）、CONCURRENT_KERNEL（248-250）、Memcpy5 字段（2004-2121）、kernel correlationId（2888-2894）、queued/submitted（2914-2936）、external correlation kind（5697-5735）、`CUpti_ActivityExternalCorrelation`（5746-5773）、worker 线程与 flush period（7509-7527）、push external id 语义（7463-7481）
11. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/include/cupti_callbacks.h`——`CUPTI_API_ENTER/EXIT`（88-98）
12. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/samples/cupti_external_correlation/cupti_external_correlation.cu`——push/pop 用法、采集端 map 组装、RUNTIME kind 前置（11-16、210-227、247-255）
13. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/samples/cupti_trace_injection/README.txt`——`CUDA_INJECTION64_PATH`/`NVTX_INJECTION64_PATH` 注入机制、Detours、零修改承诺
14. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/extras/CUPTI/samples/pc_sampling/pc_sampling.cu`——CC 5.2/6.0+ 门槛（5-6、157）
15. CUPTI samples 目录清单（本机 12.6 实存：`callback_event`、`cupti_correlation`、`cupti_nvtx`、`pc_sampling*` 等 33 个）

**官方仓库（事实卡已核，本报告引用）：**

16. NVTX README：https://github.com/NVIDIA/NVTX （"The CUPTI API supports recording traces of NVTX calls"、"Nsight Systems logs NVTX calls and shows them on a timeline"——事实卡 §1 已逐字核验）

**仓库内引用（引用不复制）：**

17. docs/17-research/projects/nsight-cupti.md（事实卡：许可 §3、硬件 §4、边界 §2、实测 §5、归属 §6）
18. docs/17-research/2026-10-26-stage-d-environment-and-licenses.md（D1 笔记：本机硬件/带宽实测 §1）
19. AGENTS.md（性能主张纪律）；docs/design/00-总体设计.md §2（World Contract）
