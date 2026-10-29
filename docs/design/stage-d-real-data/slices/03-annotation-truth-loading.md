# L2 · 切片 D3「标注装载为真值」详细设计

> **设计版本**：v1 · **批准方式**：owner 指示"接着最新的代码实现吧，D3 可以下载"（2026-10-26，含取数授权） · **批准日期**：2026-10-26
> 状态：**verifying**（实现 + 两套构建全量测试 + 双轴审查返工已完成；`done` 待 owner 确认，与票据同一状态）。本片**不引入任何链接期依赖**：标注走行式清单进引擎（零 JSON 解析器），取数与换算在 Node/PowerShell 侧。
> 上承：[阶段 D L1](../概要设计.md) §2 条 5/§3/§4 + [ADR-0013](../../../adr/0013-scoring-space.md) + [D1 note](../../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md) §6 + [D2 L2](./02-frame-source-supply-path.md)。票据：[03](../../../../.scratch/stage-d-real-data/issues/03-annotation-truth-loading.md)。

## 1. 程序描述

把**真实标注**变成逐 tick 真值，从而第一次让"真值与模型定义域相交"（problem-log 006/007/012 的根因）。机制只有一条：`TruthWorld` 从"生成器"扩展为"生成器 + 装载器"——真值权威、单写者、`friend` 通道**一字不动**（L1 §2 条 5）。

本片打通的是：`--frames <帧清单> --annotations <标注清单>` → 每 tick 的观察来自真实帧、真值来自真实标注，两者**同一格图像**（一一对应，不错位）。D2 那块"有观察无真值"的空白由此补上。

**为什么现在才有语义**：`matched_error` 比的是匹配主张与当前真值的位置距离。合成真值时代它是"模型对彩色方块的误差"；装载标注后它就是**检测器的定位误差**——`divergence` 与 ECE 的"正确"第一次落在模型自己的定义域里。判据 (b)/(c) 的面板因此从 D3 起才存在，D4 只负责评分空间的口径与分层。

**本片的刻意局限（报告必须写）**：
1. COCO 是**逐张独立图**，帧间无同一实体（公开标注无 track id，已核验）→ 每 tick 是**新场景**，`identity_stability` 在此面板上**无语义**；跨 tick 的实体 id 只保证单调不复用（契约②），不保证"同一个东西"。
2. 评分是**纯位置**的：`NearestPositionStrategy` 不看类别，适配器也不铸类别标签（`core/proposal.hpp`）。所以本面板测的是"位置主张的校准"，**不含类别正确性**——不要把它读成 mAP。
3. 数据不入库 → 本片交付的测试**不依赖数据集**（夹具程序内生成）；真实面板只是端到端演示与 regression 数字。

## 2. 数据结构

### 2.1 引擎侧（新增 `src/truth_world/annotation.hpp` / `.cpp`）

```cpp
struct Annotation {                  // 一个实例在一格画面里的标注
  std::uint64_t source_id = 0;       // 数据集标注 id：只做署名与取证，不是引擎身份（契约②）
  ClassTag class_tag;                // 数据集类别名（进真值供人读，评分不消费）
  float u_min = 0.0f, v_min = 0.0f;  // 归一化图像框，[0,1]，左上原点
  float u_max = 0.0f, v_max = 0.0f;
  bool crowded = false;              // 数据集 crowd 标记：口径规则 1 丢弃并计数
};
struct AnnotationFrame { std::vector<Annotation> boxes; };   // 一帧 = 一 tick
struct AnnotationSet {
  std::string dataset, license;      // 进工件的出处两行（许可红线要求）
  int width = 0, height = 0;         // 归一化所参照的画布（取证用）
  std::vector<std::string> categories;                        // 类别表（口径规则 4）
  std::vector<AnnotationFrame> frames;               // frames[k] = tick k+1
  std::size_t dropped_crowd = 0, dropped_degenerate = 0, dropped_over_cap = 0;  // 口径计数
  const AnnotationFrame* find(Tick t) const;         // t-1 越界返回 nullptr
};
// 解析 + 施加口径规则（§4）。每一种失败都抛具名错误（std::runtime_error，含路径/行号/键名）。
AnnotationSet load_annotation_set(const std::string& manifest_path);

class TruthWorld {                                        // 既有类，只加一个方法
 public:
  bool load_annotations(WorldState& ws, const AnnotationSet&, Tick target);
};
```

`load_annotations` 返回 `false` = 标注序列没有该 tick 的数据（流末尾是返回值，不是异常）——与 D2 `FrameSource::next()` 同一条纪律。

**签名对 L1 §4 草案的偏离**：草案是 `load_annotations(WorldState&, const AnnotationSet&)`。本片加第三个参数 `Tick target`，理由与 D2"来源不写 tick"同源：**时间归主循环**（契约③）。不传 tick 就只能让 `AnnotationSet` 自己携带时间或由 TruthWorld 记住进度，两者都会造出第二个时钟。已按此实现，L1 §4 的草案不回改（草案留档）。

### 2.2 标注清单格式（行式，零依赖；与 D2 帧清单同族）

```
# annotation manifest
dataset=COCO val2017 subset (pinned: tools/fetch_coco_subset.manifest.json)
license=annotations CC BY 4.0 (COCO Consortium); images per-image CC, only BY / no-known-restrictions admitted
width=640
height=480
categories=person,bicycle,car
tick=1
box 0.41230 0.33110 0.45520 0.40210 person 1768 0
box ...
tick=2
```

- 头部键：`dataset` / `license` / `width` / `height`（正整数）/ `categories`（逗号分隔的类别表）。未知键 = 具名错误。
- 帧块头：`tick=N`，**必须从 1 起连续**（缺口 = 具名错误；与 D2 的"末尾正常收尾"区分开：缺口是数据坏了，末尾是数据用完）。空块合法（该帧全部框被口径规则丢弃）。
- `box u_min v_min u_max v_max <class> <source_id> <crowd 0|1>`：坐标归一化到 §2.2 头部声明的画布；换算（含 letterbox 反算）在取数侧完成，引擎不做几何。

### 2.3 `WorldState` 唯一的新增私有原语

```cpp
 private:
  friend class TruthWorld;
  void reset_scene(Tick t);   // 标注真值用：换掉当前场景，tick 由调用方命名
```
只清 `entities_`；**`next_id_` 不回退**（id 单调、永不复用 = 契约②），`boundary_events_` 不清零（累计语义不变）。没有公开写接口，没有第二个作者。

### 2.4 `RunConfig` / CLI / 工件

- `RunConfig` 新增 `std::string annotations;`（空 = 真值来自种子运动学世界），`parse_config` 里显式清空（默认只在 CLI 层的纪律）。
- CLI 新增 `--annotations <manifest>`；**`--annotations` 必须与 `--frames` 同传**（否则真值没有对应的观察，直接具名报错），且 `--bounds wall` 与标注真值互斥（§3 规则 4）。
- `RunReport` 新增标注字段（`dataset/license/frames/boxes/三种丢弃计数`），`run_report_json` **只在装载标注时**多输出一个 `"annotation_truth": {...}` 节 → 不传 `--annotations` 时工件**逐字节不变**（回归门的字节判据靠这条）。

## 3. 关键机制

1. **单写者与契约④⑤零改动**（L1 §2 条 5 的代码落点）：新增的 `reset_scene` 与既有 `mint`/`step` 同一条 friend 通道，作者仍是 `TruthWorld` 一个；非拥有者的唯一写路径仍是 Proposal 五元组，真值永不回写。ADR-0013 已经写明"本 ADR 只改在哪里比，不改谁能写真值"——本片正是那句话的执行。**不动 L0 §2 任何文本**（④ 的"真值独占运动学与类别"仍成立：内容来源多一种，作者没多一个）。
2. **场景按 tick 替换，不是累加**：真实标注逐帧描述**不同场景**，累加会让不同图的实体在同一归一化画布里假性重叠、跨场景假匹配。所以标注模式下每 tick `reset_scene(t)` + 按该帧口径规则保留的框逐个 `spawn`。
3. **装载的是"位置 + 类别"，速度恒零**：标注只说"在哪里是什么"，不说怎么动。`velocity = Vec3{}`、`y = 0`（真值世界一贯的 y≡0 平面）。世界模型适配器仍从"上一状态"外推——它们对零速度真值的预测就是 `linear_extrapolation` 的常数外推，语义不变。
4. **不做边界反射**：`reset_scene` 不进 `step()`，因此不产生边界事件（`boundary_events` 恒 0，如实）。归一化坐标的定义域就是 [0,1]，不存在"越界需要反射"的世界；`--bounds wall` 的钳制会挪动真值位置，故与标注模式互斥（§2.4）。
5. **位置约定＝本引擎既有相机映射的不动点**（重要，写死）：`ViewCamera` 是单位正方形到像素的**恒等归一化投影**（`world_x(px) = (px+0.5)/width`，`rtdetr.cpp` 已据此把框心直接当 x/z），所以"归一化图像中心 (u,v) → 实体 (x=u, z=v)"与现有 `matched_error` 的世界 x/z **同一空间、同一数值**。→ **D4 的 `ScoreSpace::ObservationUV` 在当前恒等相机下与 `WorldXZ` 数值相等**；观察空间评分真正的分量是"标注真值使定义域相交"，不是新几何。这条结论进 regression 报告，避免把 D4 写成"发明了换算"。
6. **阶段计量如实改名**：标注模式下真值阶段不再是运动学步长 → 采样键 `truth_load`（与 D2 的 `frame_fetch`/`render` 二选一同构），`truth_step` 不出现；`log.boundary_events` 恒 0。
7. **tick 上限 = min(帧数, 标注帧数)**：任一序列先到末尾即正常收尾，`ticks_run` 如实（D2 已有机制，本片加标注侧的同一处理）。
8. **确定性**：清单文本字节固定 → `AnnotationSet` 逐位固定 → 真值实体序列、观察、账本、指标全部逐位可复现；`source_id` 只进 `Annotation`，**不进** `Entity`，因此不进 `world_hash`（引擎身份仍由 runtime 铸造）。
9. **标注模式下 `--seed` 不管任何东西**（实测事实，必须写清）：真值不来自 RNG，`build_demo_scene` 不执行，`rng_` 一步都不走。该模式的确定性完全由**两份清单的字节**承担——seed 仍在工件里如实报告，但它在那次运行里没有作用对象。这不是缺陷（合成模式的种子语义没变），但读工件的人需要知道"600/42 的可复现性"与"200 帧面板的可复现性"来源不同。

## 4. 标注口径（L1 §7 R-D4 要求的冻结规则）

**核验到的事实**（2026-10-26 用 HTTP Range 稀疏取出 `annotations/instances_val2017.json`，19 987 840 B，36 781 条标注 / 5 000 张图 / 80 类）：

- 公开 `instances` 标注的字段只有 `area, bbox, category_id, id, image_id, iscrowd, segmentation`。
- **没有 `truncated`、没有 `occluded`、没有 track id**（这三个都在别的子集或根本不存在）。→ 遮挡与截断**在这份真值里不可表达**，任何"按遮挡过滤"的规则都是虚构的。
- `iscrowd=1` 有 446/36 781 条；`bbox` 是原图像素 `[x, y, w, h]`，可以越出画面（存在负坐标与超出宽高的框）。
- `images[]` 每条自带 `license`（索引到 `licenses[]`）、`flickr_url`、`coco_url`、`width`、`height` → **逐图许可不需要额外的 image_info 包**（见 §5 的许可发现）。

**规则（引擎侧施加，计数进工件，常量冻结在 `annotation.cpp`）**：

| # | 规则 | 理由 |
|---|---|---|
| 1 | `crowd=1` 的框**丢弃**，计入 `dropped_crowd` | crowd 是"一群人/一堆物"的合并区域，检测器按实例输出，把它当单个真值必然判错——这是"标注没说"不是"模型错了" |
| 2 | 坐标钳进 [0,1] 后**可见面积为零**（或归一化面积 < `kAnnotationMinArea = 1e-6`）的框**丢弃**，计入 `dropped_degenerate`；保留的框其**中心按钳制后的可见部分计算** | 越出画面的框无法定位；亚像素目标不是可评分对象。**阈值故意用归一化单位**：在 640×480 画布上 1e-6 ≈ 0.31 px²，换成别的画布就换物理面积——口径必须与分辨率无关，否则"正确"的定义会随分辨率漂。规则 2 的后半条同理：检测器只可能定位看得见的那一块，真值中心也必须取可见质心，否则截断目标被系统性判偏 |

常量位置更正（实现注记）：`kAnnotationMinArea` / `kAnnotationPerFrameCap` 落在 **`annotation.hpp`**（`inline constexpr`，让测试能按同一数字断言），不在 `.cpp`。
| 3 | 每帧保留上限 `kPerFrameCap = 20` 个实例，超出**丢弃**，计入 `dropped_over_cap` | 上限存在的目的：单帧极端拥挤画面不主导聚合指标。**取舍纪律：按 `source_id` 升序保留**（与目标大小无关，因此既不偏易也不偏难）；丢弃数必须出现在工件里 |
| 4 | 类别名不在清单头部 `categories=` 声明的类别表里 → **具名错误**，不静默跳过 | 类别表（数据集 `categories[]` 的 id→名）由取数侧写进清单，声明进头部就是让引擎能校验它；静默跳过等于偷偷改真值。同一帧内 `source_id` 重复也按损坏数据报错 |
| 5 | **遮挡/截断不做任何过滤**，并在报告写明"该数据源不可表达" | 事实核验（上方），造规则就是造假 |
| 6 | 归一化：`u = (x + w/2)/W` 等，坐标一律相对**装载画布**（清单头部 `width/height`）；取数侧 letterbox 到该画布时必须把填充偏移一并算进反算 | 主张与真值必须落在同一个归一化画布；填充不参与比较 |
| 7 | 帧内保留顺序 = `source_id` 升序，块序 = tick 升序 | 铸造顺序决定实体 id 顺序，必须与输入文件的物理顺序无关（确定性） |

真实面板实测的丢弃数与分布写进本片 regression 报告（判据 (b) 的口径附页，D4 沿用）。

## 5. 取数与面板（数据不入库）

**许可发现（比 D1 note 更严，需回写 D1 note）**：D1 只核验了"COCO 标注 CC BY 4.0"。实测 `instances_val2017.json` 的 `licenses[]`：COCO 的**图像**沿用 Flickr 逐图 CC 许可，且 val2017 的 5 000 张里 **3 475 张是 NonCommercial**（BY-NC-SA 1431 / BY-NC 630 / BY-NC-ND 1414），另有 BY-ND 246、BY-SA 417。作品集要公开展示，故面板的准入规则：

- **只允许** `license.id ∈ {4 (CC-BY), 7 (no known copyright restrictions), 8 (US government work)}` ——排除全部 NC（非商业）与 ND/SA（我们对图像做缩放派生、SA 会传染）。
- 标注本体 CC BY 4.0（COCO Consortium）；面板清单里逐图记录 `file_name / license id / flickr_url`，署名可查。
- 数据仍在 `data/`（本片新增 `.gitignore` 条目），**入库的只有**取数脚本 + 固定子集清单（image id + SHA256）+ 许可记录文本。

**取数：稀疏下载，不拉整包**。实测官方桶支持 HTTP Range：
- `annotations_trainval2017.zip`（252 907 541 B）只读中央目录（642 B）+ deflate 取出 `instances_val2017.json`（压缩 6 529 031 B → 19 987 840 B）。
- `zips/val2017.zip`（约 815 MB 的桶）只读中央目录 + 逐图 Range 取选中的 JPEG。
→ 200 张子集实际下载量 ≈ 6.5 MB + 约 40 MB，**远低于 owner 授权的 ~1 GB**；`image_info_val2017` 不需要（§4 事实）。

**子集选择规则（确定性，写死并入库）**：准入许可 → 非 crowd 框数在 [1, 20] → 按 `image_id` 升序取前 200 张；帧序 = `image_id` 升序 = tick 1..200。

**工具链（两个脚本，一条流水线）**：
- `tools/fetch_coco_subset.mjs`：稀疏取标注 JSON → 施加选择规则 → 逐图取 JPEG（校验 SHA256，首跑 pin 进 `tools/fetch_coco_subset.manifest.json`）→ 写 `data/coco/` + `LICENSE.tsv`（逐图许可 + `flickr_url` 署名）+ `SHA256SUMS.txt`。**入库的固定清单只记"选了哪些"**：image id / file_name / 许可 id / `flickr_url` / 原图宽高 / 每图 SHA256 / **标注 id 列表**——**不含任何 bbox 几何**。理由：标注文件本身已按 SHA256 固定，几何随时可从取回的文件重建；把 1 219 个框抄进 git 等于变相把数据集入库（§2 条 7）。
- `tools/coco_to_panel.mjs`（内部调用 `tools/jpeg_to_rgb.ps1`，用 Windows 自带的 `System.Drawing` 解码/缩放 JPEG → 裸 RGB8，**零新增依赖**；GDI+ 的 LockBits 是 BGR 底朝上，脚本先按行翻成顶朝上、Node 再换通道序）→ 写 `data/panel/frames/*.rgb` + `frames.txt`（D2 清单，条目相对清单目录，故含 `frames/` 前缀）+ `truth.txt`（§2.2 标注清单）+ `panel_stats.json`（选择与丢弃统计，报告直接引用）。**归一化实测走"整幅拉伸"**：源像素 → 归一化后拉伸到画布，归一化坐标**逐字不变**，所以既不需要 letterbox 反算，也不可能算错——代价是宽高比会形变（median 1.000，最差 2.243），已作为**模型侧混淆项**记进报告 §4，不进真值。

## 6. 边界情形

| 情形 | 行为 |
|---|---|
| 清单缺失/不可读 | `load_annotation_set` 抛具名错误（含路径）→ G6 的 exit 1 |
| 缺 `dataset`/`license`/`width`/`height`、值非正、未知键 | 具名错误（说明缺哪个键/哪个值） |
| `tick=` 不从 1 起连续 / 重复 / 无帧块 | 具名错误（含实际遇到的 tick） |
| `box` 行字段数不对、坐标 NaN/Inf、crowd 非 0\|1 | 具名错误（含行号与该行原文） |
| 类别名不在数据集类别表 | 具名错误（规则 4） |
| 某帧所有框被丢弃 | 合法：装载 0 个实体，`dropped_*` 计数说明原因 |
| `--annotations` 而无 `--frames` | **两道**：`parse_config` 具名配置错误；`LoopDriver::run()` 也抛具名错误——直接调 API 会得到"渲染器把真值画给检测器看"的自指面板（天然全对），必须在引擎里禁止 |
| `--annotations` + `--bounds wall` | 具名配置错误（规则 4：钳制会挪动真值） |
| 标注帧数 < 帧序列长度 | 标注末尾即收尾，`ticks_run` 如实；不报错 |
| 帧序列分辨率 ≠ 清单 `width/height` | 装载期具名错误（观察与真值必须同一画布；防错位） |

## 7. 测试计划（slice18，OFF/ON 两种构建都注册；夹具程序内生成，不碰数据集）

1. **装载行为**：3 帧夹具 → 断言每 tick 实体数、位置 = 归一化框心（逐位）、类别标签、速度零、`ws.tick()` 被调用方命名、`boundary_events == 0`。
2. **口径规则**：夹具含 crowd 框 / 越界框 / 退化框 / 第 21 个实例 / 未知类别 → 前三类按规则丢弃且**计数正确**，未知类别**抛具名错误**。
3. **单写者与 id 纪律**：跨 tick 场景替换后 `next_id_` 仍单调（后一帧的 id 严格大于前一帧，无复用）。
4. **确定性成对**：同一清单跑两次 → `world_hash`/`ledger_hash` 逐位相等（problem-log 002 纪律）。
5. **错误路径**：tick 缺口、box 行字段数、缺头部键、尺寸与观察画布不符 → 各自具名错误。
6. **入环**：帧清单 + 标注清单一起跑 → `ticks_run == min(两者)`、阶段键含 `truth_load` 不含 `truth_step`、工件含 `annotation_truth` 节且计数与夹具一致。
7. **回归门（硬判据）**：不传 `--annotations` 时 600/42 的 `world_hash=574361e62eec92b9` / `ledger_hash=459ff85132cc103e` 与 `slice11/12/16` 字面量逐位不变；且默认工件**不含** `annotation_truth` 节（字节级比较新旧输出）。
8. 全量：`build.bat --test`（OFF）+ ON 构建全量 ctest（AGENTS 硬规则 #4）。

## 8. 与票据验收对照

| 票据 AC | 落点 |
|---|---|
| `AnnotationSet` 数据形态 + 取数脚本（数据不入库，脚本 + SHA256 + 许可记录） | §2.1 / §5 |
| `TruthWorld::load_annotations`，单写者不变、契约④⑤零改动 | §2.1 / §3 条 1（签名多一个 `Tick`，见偏差 1） |
| 标注口径写死并记录（遮挡/截断/丢弃/上限/归一化） | §4（含"遮挡与截断在本数据源不可表达"的核验结论） |
| 回归门：不装载时 A/B/C 基线逐位不变 | §3 条 4/6/8 + §7 测试 7 |
| slice18：行为断言 + 确定性成对 + 装载失败具名错误 | §7 |
| git diff = 单文件 + build/文书 | 见偏差 2（本片按设计是"一个新模块 + WorldState 一条私有原语 + loop 一处分支"，不是单文件） |

## 9. 偏差与扩展记录

1. **`load_annotations` 比 L1 草案多一个 `Tick` 参数**：理由见 §2.1（时间归主循环，避免第二个时钟）。L1 草案留档不改。
2. **不是"单文件"切片**：`WorldState` 必须多一条 friend-only 场景替换原语（§2.3），否则真实标注无法按 tick 描述世界。这是核心改动，如实标注（与 D2 偏差 1 同类，不装作零改动）。契约文本、公开读接口、`friend` 机制三者均未动。
3. **口径规则由引擎施加，不由取数脚本预过滤**：R-D4 要的是"口径可见、可测、进工件"，规则藏在 Node 里就等于没有。取数侧只做几何归一化。
4. **不做真值接口抽取**（L1 §2 条 5 否决项，这里再次不动）：`friend class TruthWorld` 原样。
5. **准入许可比 D1 结论更严**（§5）：D1 note 只写了"标注 CC BY 4.0 + 逐图署名"，实测图像许可 3 475/5 000 是 NC。执行时收紧为"只用 BY / 无已知版权限制 / 美国政府作品"，并回写 D1 note。
6. **不做逐帧 3D/标定**：ADR-0013 之后归一化画布够用；L1 §2.1 注释里"可选 3D/标定"字段本片不实现（无消费者，契约① 的 usage-driven 纪律）。
7. **稀疏下载代替整包**：票据与 D1 note 都按"约 1 GB"预算写；实测 Range 读取把量级降到约 40 MB。副作用：取数脚本自带 zip 中央目录解析（约 120 行，零依赖），失败要大声（zip64/无 EOCD → 具名错误）。
8. **`AnnotationSet` 多了 `categories=` 头部键**：L1 §4 草案没有它，而规则 4 的"未知类别 = 具名错误"只有在类别表随清单一起到达时才是引擎侧可执行的。表由取数侧从数据集 `categories[]` 抄来；缺失或命中不到都报错。
9. **入库清单只记选择、不记几何**（见 §5）：固定子集清单存 image id + 许可 + SHA256 + **标注 id 列表**，bbox 一律留在取回的文件里。否则"数据不入库"会被自己写的脚本破坏。
10. **"必须配真实帧"在 CLI 与 `run()` 各守一次**（审查后返工）：只守 `parse_config` 会被直连 API 绕过，而绕过后得到的是"渲染器把真值画出来给检测器看"的自指面板——**那种面板天然全对**，是最不该出现的测量形态，所以引擎层必须自己拒绝。画布一致性检查同时从"钉死 tick==1"改为"首个成功到达的帧查一次"。
