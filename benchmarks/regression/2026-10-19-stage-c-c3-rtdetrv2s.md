# Regression: Stage C 切片 C3 — RT-DETRv2-S 真实模型入环（2026-10-19）

> 本文件是 C3 全部性能与能力主张的**唯一证据来源**（AGENTS 规则：没有 benchmark 数据不宣称性能）。
> 设计文本与判据见 [L2 C3](../../docs/design/stage-c-ai-in-loop/slices/03-rtdetrv2s-adapter.md)；票据见 [03-rtdetrv2s-adapter](../../.scratch/stage-c-ai-in-loop/issues/03-rtdetrv2s-adapter.md)。

## Claim under test

真实 AI 模型第一次进入闭环：RT-DETRv2-S（ONNX）以**单文件 Perception Adapter** 接入同一 `observe(Observation) → Proposal[]` 契约，内部双 provider（CPU EP 参考路径 / CUDA EP 性能路径，`--ep` 运行时选择）。要证的四件事：① 参考路径**逐位确定性**；② 预处理/后处理链路**语义正确**（不是"能跑"而是"检出的东西是对的"）；③ 核心零 per-model 改动（适配器不变式）；④ CUDA EP 接线状态与本机阻塞**如实标注**。

## Environment（完整元数据）

| Item | Value |
|---|---|
| OS | Windows 11 build 26200 |
| CPU | Intel i9-14900HX (24C/32T), 31.8 GiB RAM |
| GPU | RTX 4060 Laptop 8 GiB — **本轮不可用**（CUDA EP 载入失败，见 Results §5） |
| CUDA 侧 | CUDA toolkit 12.6.20 存在；**零 cuDNN**；ORT 1.31 CUDA EP 需 CUDA 12.8+ / cuDNN 9（[research note](../../docs/17-research/2026-10-16-onnx-runtime-integration.md)） |
| Compiler | MSVC 19.29.30133 (VS2019, x64) + CMake 3.20 + Ninja |
| ORT | `onnxruntime-win-x64-gpu-cuda12-1.31.0`（解压于 gitignored `third_party/`） |
| 模型 | `models/rtdetrv2-s-onnx.onnx` 82,572,357 B（`tools/fetch_models.mjs` 拉取，来源 onnx-community/rtdetr_r18vd，Apache-2.0） |
| Commit | 基线 `1aeacfb` + C3 未提交工作树（本报告与代码同批提交） |
| Build ①（回归门） | `build.bat --test` → `build/`，`OW_WITH_ONNXRUNTIME=OFF`（Debug，默认参考构建） |
| Build ② | `build-ort-rel/`：`-DCMAKE_BUILD_TYPE=Release -DOW_WITH_ONNXRUNTIME=ON`（**全部延迟数字来自此构建**） |
| Build ③ | `build-ort/`：ON + Debug（首轮实现期验证，仅 slice13） |

> ⚠️ 上一会话记录的"单次推理 ≈570 ms"来自 **Debug** 构建（`build-ort/`），当时未标注构建类型。本轮用 Release 重测，见 Results §4——两版几乎同值，原因也已定位（瓶颈不在本项目的 `/O2` 覆盖范围）。

## 复现命令

```bat
rem 回归门（ORT 缺席的默认构建）
cmd //c build.bat --test

rem ON 构建（独立目录，不改 build.bat）+ Release 全量套件
call "...\vcvarsall.bat" x64
cmake -B build-ort-rel -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DOW_WITH_ONNXRUNTIME=ON -DOW_ONNXRUNTIME_ROOT="<repo>\third_party\onnxruntime-win-x64-gpu_cuda12-1.31.0"
cmake --build build-ort-rel && ctest --test-dir build-ort-rel

rem 真实照片检查（资产可选，见 Results §3 的复现说明）
set OW_RTDETR_TEST_IMAGE=<repo>\models\photo_640x480.rgb
build-ort-rel\ow_tests.exe slice13

rem 端到端 + CUDA EP
build-ort-rel\omniworld_loop.exe --ticks 5 --seed 42 --width 64 --height 64 ^
  --adapters rtdetr_v2_s --models zero_delta --mode performance --bounds none --ep cpu --out perf.json
build-ort-rel\omniworld_loop.exe --ticks 1 --adapters rtdetr_v2_s --models zero_delta --ep cuda
```

## Results

### §1 构建矩阵与测试套件

| 构建 | 套件 | 结果 |
|---|---|---|
| OFF（默认，`build/`，Debug） | ctest 14 项 | **14/14 绿** |
| ON（`build-ort-rel/`，Release） | ctest 15 项（含 ow_slice13） | **15/15 绿** |
| ON（`build-ort/`，Debug） | ow_slice13 | 绿（首轮实现期） |

**"15/15"不含真实照片断言**——ctest 环境里没有 `OW_RTDETR_TEST_IMAGE`，该检查打印 `real-photo check skipped` 后正常通过（有意设计：测试不引用会消失的资产）。§3 的照片数字来自**显式设置该环境变量**的单独运行，命令见「复现命令」。因此套件绿 ≠ 链路语义正确，两件事分别取证。

参考模式基线字节不变：OFF 套件含 `ow_slice11` 的 600/42 字面量断言 `world_hash == 574361e62eec92b9` / `ledger_hash == 459ff85132cc103e`，ON 构建同样通过 → **引入 ORT 与适配器不改变参考模式产物字节**。

**过程中修掉的两处真实断裂**（都不是"代码写完就完事"能发现的）：

1. **`ow_slice03` 在 ON 构建下红**：注册表测试写死 `names.size() == 2`，而 `OW_WITH_ONNXRUNTIME=ON` 会多注册 `rtdetr_v2_s`。此前 ON 构建**只跑过 slice13**，全量套件在 ON 下首次运行才暴露。改法：按构建配置断言**精确集合**（`kExpected = ON ? 3 : 2`，且逐项核对名字），不是放宽成 `>= 2`。→ problem-log 010。
2. **CUDA EP 缺 DLL**：CMake 的 POST_BUILD 只拷 `onnxruntime.dll`，未拷 `onnxruntime_providers_shared.dll` → `AppendExecutionProvider_CUDA` 直接失败。现改为拷贝 `lib/` 下全部 `*.dll`（CPU-only 的 ORT 包自然只拷到一个）。

### §2 预处理对齐：一次被实测证伪的"想当然"

L2 §3 原设计 = nearest 缩放 + CHW float /255。实现期先按 ImageNet mean/std 归一化（RT-DETR 论文语境的默认动作），实测 **logits 全为负**（区间 [-9.57, -0.38]，`raw>0 count=0`）——一个框都不该有信心，这是链路错的信号而非"场景没物体"的信号。

撤回并核对一手来源：拉取基座仓库 `PekingU/rtdetr_r18vd` 的 `preprocessor_config.json`：

| 键 | 值 | 结论 |
|---|---|---|
| `do_resize` | 640×640 | 与 L2 一致 |
| `resample` | 2（bilinear） | **替换** nearest |
| `do_rescale` | 1/255 | 一致 |
| `do_normalize` | **false** | ImageNet mean/std 是**惰性 kwargs**，本 checkpoint 不用 |
| `align_corners` | false（HF 默认） | 双线性用半像素中心实现 |

改后（Release，`OW_RTDETR_DEBUG=1`）同一批输入的 logits 区间从"全负"变为真实照片上 `raw>0 count=5`。→ **预处理归适配器所有（阶段 A slice03 的裁决），但"所有"不等于"自定义"：必须与产出该 ONNX 的参考处理器同语义，否则检出结果不可解释。** L2 §3 的 nearest 选型记录为偏差（L2 偏差记录 #4）。

### §3 能力面：真实照片 vs 合成场景（决定性对照）

同一构建、同一模型、同一阈值 0.3，两类输入的差距是**数量级的**：

| 输入 | 检出 | 明细（Release 实测；`cls` = 模型 argmax 的类索引，标签取自模型自身 `config.json` 的 `id2label`） |
|---|---|---|
| 真实照片 640×480（`models/photo_640x480.rgb`） | **5 框，max conf 0.950** | box0 cls=65 **remote** cx0.169/cy0.198/w0.212/h0.092 conf **0.939**；box1 cls=15 **cat** cx0.256/cy0.551/w0.480/h0.871 conf **0.947**；box2 cls=15 **cat** cx0.770/cy0.412/w0.460/h0.724 conf **0.950**；box3 cls=65 **remote** cx0.550/cy0.275/w0.058/h0.231 conf **0.784**；box6 cls=57 **sofa** cx0.500/cy0.497/w1.000/h0.991 conf **0.809** |
| 合成场景 64×64（8 个实体，footprint 0.02） | **1 框** | 退化整帧框 cls=63 **laptop** cx0.500/cy0.499/w1.000/h1.000 conf **0.518** |
| 空白帧 64×64（纯背景） | **1 框** | 退化整帧框 cls=62 **tvmonitor** cx0.500/cy0.499/w1.000/h1.000 conf **0.330** |

照片内容本轮由 agent 直接目视核对（`models/photo_source.png` 可读）：**红色沙发上躺着两只虎斑猫，沙发边有两个白色遥控器**。模型自报的 5 框与之**逐一对应**——两只猫（0.947/0.950，大框、位置分居左右）、沙发（0.809，整幅 w1.0/h0.99）、两个遥控器（0.939 横置左上、0.784 竖置中部窄框 w0.058）。类别、位置、尺度三者全对。

**这才是 C3 的验收证据**："能跑"只证明 ABI 通了；类别与几何同时对上，才证明预处理、输出名探测、boxes/logits 分类、argmax、cxcywh→世界坐标映射整条链路语义正确。附带一条：合成/空白帧的退化框自报成 laptop/tvmonitor——模型在分布外把"整幅矩形"往屏幕类上猜，这正是 §G1 那条 AC 为什么站不住脚的具体形态。

合成场景的"唯一输出是整帧退化框"是**分布外（out-of-domain）行为**，不是 bug：纯色背景 + 8 个 2×2 色块，放大到 640×640 后没有任何 COCO 统计特征。两张表放一起，就是本切片最有信息量的结论：**模型能力边界由输入分布决定，与引擎契约无关**。

**资产复现说明**：`photo_640x480.rgb` 是 gitignored 的二进制派生物，真实照片许可由 owner 决定，不入库。生成工具已入库为 [tools/png_to_rgb.mjs](../../tools/png_to_rgb.mjs)（纯 Node，无第三方依赖），`node tools/png_to_rgb.mjs models/photo_source.png models/photo_640x480.rgb` 复现的字节与实测所用文件 **SHA256 一致**（`8050ada414034a490fc2bd6ed13a8d78b7d2ce7d896ba4fcd4651317b9471efe`）。未设 `OW_RTDETR_TEST_IMAGE` 时该检查打印"skipped"后正常通过——测试不引用会消失的资产。

### §4 确定性（两条独立轴）

| 轴 | 证据 |
|---|---|
| 适配器内 | 同一 Observation 两次 `observe()` → Proposal 逐字段相等（`slice13` 断言，含 size/producer/tick/confidence/x/z）；debug 打印两次完全同值（合成场景 0.518 两次、空白帧 0.330 两次） |
| 端到端 | 同配置两次运行（3 ticks / seed 600 / CPU EP）→ `world_hash=2c231ac4a54ff077`、`ledger_hash=4b8618489c28b186`、`entries=27` **逐位一致**（墙钟不同：1.906 s vs 本次——时间不进哈希，契约③） |

机制：`SetIntraOpNumThreads(1)` + `ORT_ENABLE_ALL` 固定图优化 + CPU EP。

### §5 延迟（Release `build-ort-rel/`，CPU EP 单线程）

| 运行 | ticks | detect:rtdetr_v2_s | 逐 tick 均值 |
|---|---|---|---|
| performance / seed 42 | 5 | p50 **518.23 ms** / p95 **726.62 ms** | 2850.053 / 5 = **570.0 ms** |
| reference / seed 600 | 3 | p50 531.36 ms / p95 821.15 ms | 1906.133 / 3 = **635.4 ms** |
| （对照）Debug `build-ort/` | 3 | — | 1710.5 / 3 = **570.2 ms** |

三点必须如实说明：

1. **`stage = observe()` 全程**：双线性预处理 + ORT 推理 + 后处理，不是纯推理时间。
2. **p95 高于 p50 不是模型抖动**：首 tick 含一次性会话创建（加载 82 MB 模型）。同一输入的输出抖动为 **0**（§4）——**延迟抖动 ≠ 输出抖动**，ADR-0009 的容差窗口是关于后者的。
3. **Debug 与 Release 同值（570.2 vs 570.0）**：本项目 `/O2` 只影响预处理/后处理循环，推理在 ORT 预编译 DLL 内；且参考路径**故意**单线程（确定性优先）。所以这个数字**不是 CPU EP 的最快配置**，不能拿来论证"CPU 太慢所以要上 GPU"——它是"确定性参考路径的代价"。

### §6 CUDA EP：接线完成，本机阻塞，故障原因现在进工件

`--ep cuda` 两层阻塞，本轮把第一层修掉了：

| 层 | 现象 | 状态 |
|---|---|---|
| 分发层 | `Error loading onnxruntime_providers_cuda.dll`（`providers_shared.dll` 未拷到 exe 目录） | **已修**（CMake 拷贝 `lib/*.dll`） |
| 依赖层 | 修复后 ORT 走到载入 `providers_cuda.dll`，报 `Error 127`（过程找不到）→ `Failed to load shared library` | **本机阻塞**：需 cuDNN 9 + CUDA 12.8+（owner 侧动作） |

工件侧的关键变化：`RunReport::first_fault` 新增后，同一次运行的 JSON 里现在写着

```
"adapter_faults": 1,
"first_fault": "rtdetr_v2_s: rtdetr_v2_s: CUDA EP unavailable (OrtSessionOptionsAppendExecutionProvider_Cuda: Failed to load shared library); needs CUDA 12.8+/cuDNN 9 (see docs/17-research/2026-10-16-onnx-runtime-integration.md)"
```

修复前只有 `adapter_faults: 1`，原因只存在于 ORT 自己打到 stderr 的日志里，工件（= 可交接证据）里一个字都没有。→ problem-log 009。（`first_fault` 里 `rtdetr_v2_s:` 出现两次：driver 统一加生产者前缀 + 适配器消息自带名字，属可读性小瑕疵，保留自带名是因为单测路径没有 driver 前缀。）

### §7 容差窗口声明（L1 Round 1 #4）

原则：窗口 = 目标机连续两次独立运行观测到的最大输出抖动 × 2。

- **CPU EP 参考路径**：实测抖动 **0**（§4 两轴）→ 窗口为**退化的 0**，即该路径按**逐位精确**复现，不适用容差比较（B2 容差比较器对它恒等通过）。
- **CUDA EP 性能路径**：**本机无法标定**（EP 载入失败）。诚实结论 = 不声明数值窗口，把标定明确移交：owner 装 cuDNN 9 / CUDA 12.8+ 后由 **C5** 按同一原则实测并写入报告。**不做的是**：拿别人机器的数字填表，或把 CPU EP 的 0 窗口当成 CUDA EP 的承诺。

会话建立的一次性开销另有一组测试内直测值（Release，无 debug 打印）：`observe #1 = 844.6 ms`（含首次会话创建）vs `observe #2 = 557.9 ms`（稳态）——与 §5 里 p95 > p50 的成因一致，直接量出来了。

### §8 错误路径实测：三条出口的"可见性"完全不同（C3 撞出的 CLI 缺口）

同一类故障（模型/适配器不可用），三条路径的可诊断性差别极大——这决定了 #10/#11 两条偏差为什么这样落：

| 场景 | 实测结果 | 工件 | 判定 |
|---|---|---|---|
| `--adapters does_not_exist`（setup 期抛错） | **exit `0xC0000409`（fastfail）、stdout/stderr 零输出** | **不写** | ❌ 完全不可诊断（C1/C2 遗留缺口，非 C3 引入） |
| `OW_RTDETR_MODEL=C:\nope\missing.onnx`（observe 期抛错） | exit 0、`faults=1`、ticks 跑完 | `first_fault` = `... model load failed at 'C:\nope\missing.onnx' (...File doesn't exist); run: node tools/fetch_models.mjs` | ✅ 原因 + 期望路径 + 修复命令齐全（实测） |
| `--ep cuda`（EP 不可用） | exit 0、`faults=1` | `first_fault` = CUDA EP unavailable + 指向 research note | ⚠️ 原因可见，但**退出码为 0**（fail-fast 语义在经 loop 时不成立，见 §9） |

结论：**会话惰性建立是实测后的选择**——照 L2 §5 字面在构造期抛错，会把一个"有原因的失败"换成"静默 fastfail"。缺口本身（`main()` 无 try/catch）列为 owner 裁定项 G6。

### §9 fail-fast 与隔离层的互斥（待裁定）

L2 §3 要求 `--ep cuda` 在 EP 不可用时"fail-fast 报错而非静默降级"；契约同时要求"单适配器故障不得 halt 循环"。**直接调 `observe()` 时前者成立**（抛 `std::runtime_error`，slice13 之外可复现）；**经 `omniworld_loop` 时二者互斥**——异常必然被隔离层接住，表现为 ticks 正常 + `faults=1` + `first_fault` 带原因 + **exit 0**。本轮保留隔离层语义，不改退出码（属 B1 CLI 语义）；是否加非零退出码或 `--strict` 开关 = owner 裁定项 G7。**两条都不选**：既不静默降级到 CPU EP（那是另一种"假绿"），也不擅自改 CLI 退出语义。

## 票据验收裁定

| 票据 AC | 裁定 | 依据 |
|---|---|---|
| 单文件适配器 + 编译守卫 | ✅ PASS | `src/perception/rtdetr.cpp` 一个文件（275 行），`#if OW_WITH_ONNXRUNTIME` 守卫，CMake 条件 `target_sources` |
| 核心零改动 | ⚠️ **PARTIAL（如实标注）** | **零 per-model 代码**成立（注册表是唯一接入点，driver 无 `if (rtdetr)` 分支）；**零核心行**不成立：3 处与模型无关的通用改动（`ExecProvider` 移入 core/types、接口加默认 no-op 虚函数、`first_fault`）。详见 L2 偏差记录 #1/#2/#6 |
| 参考路径确定性 | ✅ PASS | §4 两轴逐位证据 |
| 性能路径容差 + 延迟预算 | ⚠️ **PARTIAL** | CPU EP 有延迟实测（§5）与 0 窗口声明；CUDA EP 窗口与预算**推迟到 C5**（本机阻塞，§6/§7） |
| 检出 → Proposal 映射 | ✅ PASS（**类别标签一项按契约让位**） | §3 真实照片 5 框类别/位置/尺度全对上；框→世界 x/z 经 ViewCamera 契约直映。票据原文写"类别标签、位置、置信度"，但 `core/proposal.hpp:18` 的契约是 **adapters must not guess**（适配器不铸 `class_tag`）→ 类别只留在适配器内（debug 打印），不进 Proposal。契约优先于清单措辞，记为 L2 偏差 #7 |
| CUDA EP 接线 + 阻塞如实标注 | ✅ PASS | §6 两层阻塞分离清楚，修复一层，另一层移交；失败路径的可见性见 §8/§9 |
| slice13 测试（确定性 + 单文件不变式） | ✅ PASS | ON 构建 15/15；§6 测试 5 由 `test_blob_adapter_unaffected_by_provider` **直接**证明（blob 收到 CudaEp 后 7 条 Proposal 逐字段不变），不再靠"OFF 套件也绿"推理；§6 测试 1 的"耗时记录"与测试 4 的"时间字段有值"同样落成断言/输出 |

**C3 verdict：真实模型首次在环成立——链路语义正确性由真实照片 5 框（max 0.950）确立，确定性由两轴逐位证据确立，延迟有 Release 实测；CUDA EP 接线到位但本机阻塞（cuDNN 缺口，非代码问题）；性能路径容差窗口按原则移交 C5，不虚构数值。**

## Findings 与排期

| # | 发现 | 归属 |
|---|---|---|
| G1 | 模型在分布外合成帧上恒定输出整帧退化框 → L2 的"空帧→空列表"AC 假设了模型不具备的性质 | 已改判据（problem-log 008）；C4 的 L2 写 AC 时先问"这条是契约属性还是模型能力属性" |
| G2 | 隔离层吞异常导致工件无诊断 | 已修（`first_fault`），并**回写权威文本**：L0 §2 ⑨ 追加字段说明 + B1 L2 schema 行（problem-log 009） |
| G3 | 可选依赖会让"恰好 N 项"型断言在另一配置下失效 | 已修（problem-log 010）；C4 再注册一个适配器时同一测试会再变（→ 4） |
| G4 | cuDNN 9 / CUDA 12.8+ 缺失 = 唯一挡住性能路径的环境项 | owner 侧；装好后 C5 立即标定 CUDA EP 窗口 |
| G5 | 真实照片为 gitignored 资产、许可由 owner 定 | 工具已入库保证可复现；如需长期固化，请 owner 指定一张许可明确的图 |
| **G6** | **CLI 的 setup 期异常无出口**：`main()` 无 try/catch → 未知适配器名 = **exit 0xC0000409、零输出、无工件**（实测）。C1/C2 遗留，非 C3 引入，但 C3 因"要不要构造期抛错"撞上了它 | **待 owner 裁定**（改 CLI 错误语义 = B1 范围）；C3 侧已按实测选择惰性加载并记录为 L2 偏差 #10 |
| **G7** | **fail-fast 与隔离层在经 loop 时互斥**：`--ep cuda` 失败表现为 ticks 正常 + `faults=1` + `first_fault` + **exit 0** | **待 owner 裁定**（非零退出码？`--strict`？）；本轮不改退出码语义，也不静默降级，记为 L2 偏差 #11 |

## 收口评审（Standards + Spec 两轴）与本轮修正

按 AGENTS 规则 #5 对实际 diff 做两轴审查，**下列发现已在收口前修掉**（其余转为上面的 G6/G7 或 L2 偏差记录）：

| 轴 | 发现 | 处置 |
|---|---|---|
| Standards | 4 处 catch 块重复"计数 + 首因"逻辑（Duplicated Code） | 抽出 `record_fault(rep, who, what)`，两处 catch 各一行 |
| Standards | 测试名 `..._is_empty` 与实际断言（有界）相反（Mysterious Name） | 改名 `test_rtdetr_blank_frame_is_bounded` |
| Standards | 测试注释里的置信度/logit 是 Debug 旧值（0.41 / −0.36），与正文实测 0.330 / −0.707 矛盾 | 更正为 Release 实测值并标注构建 |
| Standards | 契约⑨ 追加字段未回写权威文本；B1 schema 行未更新 | 两处已回写（G2） |
| Standards | `ExecProvider`/EP 命名与 GLOSSARY 里 Adapter 的 `_Avoid_: provider` 撞词 | GLOSSARY 新增 **Execution Provider / EP** 词条，明确"EP ≠ 接入形态"并给出 _Avoid_ |
| Spec | §6 测试 5（blob 不受 provider 扩展影响）无测试，只有推理 | 新增 `test_blob_adapter_unaffected_by_provider`（实测 7 条 Proposal 不变） |
| Spec | §6 测试 1"耗时记录"、测试 4"时间字段有值"未落地 | 测试内直测两次 observe + 断言 `latency_sum.*_ms > 0` 与 `first_fault` 为空 |
| Spec | §5 行 2（构造期抛错）、§3（fail-fast）与实现不符且**未登记** | 补 L2 偏差 #10/#11，附 §8 的实测出口对比表 |
| Spec | §5 行 4（上采样核）与 #4 不同步；#2"测试零改动"与 #8② 自相矛盾；#1 与 L1 §3 的张力未承认 | L2 内三处措辞更正 |
| Spec | `tools/png_to_rgb.mjs`、第 4 个测试、debug 打印、畸形观察抛错均"未被告知" | 统一登记为 L2 偏差 #9（明示扩展 + 理由） |
| 链接 | C3 L2 的票据链接 3 级深度错误（同类 bug 已在 e69035f 修过两批）；stage-B slice01 也残留一处 | 两处均修为 4 级，并全量扫过本次改动文件的相对链接 |
