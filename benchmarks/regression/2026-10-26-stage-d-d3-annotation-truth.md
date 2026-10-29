# Regression: 阶段 D 切片 D3 — 真实标注成为逐 tick 真值（2026-10-26）

> D3 全部能力与性能主张的**唯一证据来源**。设计：[L2 D3](../../docs/design/stage-d-real-data/slices/03-annotation-truth-loading.md)（owner 2026-10-26 指示"接着最新的代码实现吧，D3 可以下载"，含取数授权）；票据：[03-annotation-truth-loading](../../.scratch/stage-d-real-data/issues/03-annotation-truth-loading.md)；上承：[阶段 D L1](../../docs/design/stage-d-real-data/概要设计.md) §2 条 5/§3、[ADR-0013](../../docs/adr/0013-scoring-space.md)、[D2 L2](../../docs/design/stage-d-real-data/slices/02-frame-source-supply-path.md)、[D1 note](../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md) §6。

## Claim under test

**真值权威不变的前提下，真实标注能否成为逐 tick 真值**，从而第一次造出"真值与模型定义域相交"（problem-log 006/007/012 三轮的共同根因）。要证五件事：

1. **单写者与契约④⑤零改动**：真值内容多一种来源，作者仍只有 `TruthWorld` 一个；
2. **真实观察与真实真值同框**：第 k tick 的帧与第 k tick 的标注描述同一张图；
3. **真实数据的确定性**：同一帧序列 + 同一标注 + 同 seed → 哈希逐位复现，且面板本身可重建；
4. **不变式不退化**：不装载标注时阶段 A/B/C 冻结基线逐位不变；
5. **标注口径可见**：过滤规则、丢弃计数、许可与署名进工件，可被复核（L1 §7 R-D4）。

**明确不在本片主张之内**：判据 (b)/(c) 的分离量与阈值裁定（D4）、任何 GPU/零拷贝主张（D5）、类别正确性（评分是纯位置的）、跨场景定位。

## Environment

| Item | Value |
|---|---|
| OS / CPU | Windows 11 build 26200 · i9-14900HX · RTX 4060 Laptop 8 GiB（**本片完全不使用 GPU**，D2→D4 结构解耦，L1 §2 条 9） |
| Compiler | MSVC 19.29.30133 (VS2019, x64) + CMake 3.20 + Ninja · Node **v20.19.5** · Windows PowerShell 5.1 |
| ORT | `onnxruntime-win-x64-gpu-cuda12-1.31.0`（gitignored），**只用 CPU EP** |
| 数据集 | COCO val2017 子集 200 张（`tools/fetch_coco_subset.mjs` 取数，**数据不入库**） |
| Build ①（回归门 + 零依赖证明） | `build.bat --test` → `build/`，ORT **OFF** |
| Build ②（**本片全部延迟数字来自此构建**） | `build-ort-c5/`：Release + `OW_WITH_ONNXRUNTIME=ON`，`environment.build = "release"` |
| Commit | `b471ea6`（D1+D2 落地）之上的一批未提交改动；D3 实现与本报告同批 |

## 复现命令

```bat
cmd //c build.bat --test                                   rem OFF 17/17
cmake --build build-ort-c5 && ctest --test-dir build-ort-c5 rem ON  19/19
build\omniworld_loop.exe --ticks 600 --seed 42 --out base600.json   rem 冻结基线

node tools/fetch_coco_subset.mjs          rem 稀疏取数（Range 读中央目录）+ SHA256 固定
node tools/fetch_coco_subset.mjs --verify rem 只校验，不下载
node tools/coco_to_panel.mjs              rem 解码/缩放 → data/panel/{frames.txt,truth.txt}

build-ort-c5\omniworld_loop.exe --frames data/panel/frames.txt --annotations data/panel/truth.txt ^
  --ticks 200 --adapters rtdetr_v2_s --models zero_delta --mode reference --ep cpu --out panel.json
build-ort-c5\omniworld_loop.exe --frames data/panel/frames.txt --annotations data/panel/truth.txt ^
  --ticks 200 --adapters blob_detect --models zero_delta --out off_panel.json   rem 真帧 + 内建检测器
```

## Results

### §1 构建矩阵（AGENTS 硬规则 #4：每种配置都跑全量）

| 构建 | 套件 | 结果 |
|---|---|---|
| OFF（默认，ORT 关闭） | ctest **17** 项（新增 ow_slice18） | **17/17 绿** |
| ON（Release + ORT） | ctest **19** 项 | **19/19 绿** |

slice18 覆盖：装载行为（实体数/位置=归一化框心/类别/速度零/tick 由调用方命名/`boundary_events==0`）、跨场景替换后 id 仍单调不复用（契约②）、口径规则 1–4 与三种丢弃计数、**截断框取可见中心 + 整帧被清空仍合法装载（且空帧不消耗 id）**、五类畸形清单各自的具名错误、确定性成对、入环（阶段键 `truth_load`、工件 `annotation_truth` 节、短序列先收尾、**无 `--frames` 时直连 API 也拒绝**）、默认路径不长出新节、CLI 两道守卫。

### §1.1 双轴审查（Standards + Spec）的采纳记录

两轴各跑一次独立审查（fixed point = `b471ea6`）。采纳并当场修完的（都已重跑两套构建 + 面板哈希复核）：

| 发现 | 处置 |
|---|---|
| `kAnnotationMinArea` 的注释写"≈1 px²"，实测 640×480 上是 **0.31 px²**；且 L2 说常量在 `.cpp`，实际在 `.hpp` | 注释与两处文档同步改正（口径不变，只是说法对齐实数） |
| 错误消息出现**双前缀**（`annotations: … line N: annotations: …`），`split_csv` 与 box 分支各一处 | 新增 `fail_header()` 统一"无行号缺陷"的消息形态，两处双前缀消除 |
| `--annotations` 无 `--frames` 只在 `parse_config` 挡；直连 `LoopDriver::run()` 会得到"渲染器把真值画给检测器看"的**自指面板**（天然全对） | `run()` 内也抛具名错误 + slice18 加用例（§9 条目 9） |
| 画布一致性检查钉在 `t == 1`，第 1 帧取不到就静默跳过 | 改为"首个成功到达的帧"检查一次（`canvas_checked` 标志） |
| 规则 2 后半条（钳制后可见中心）与"整帧被清空"两条**只有实现没有测试** | slice18 补两个用例（上表已记） |
| `parse_dimension` 的 1<<24 上界把合法超大值报成"需要正整数" | 拆成两条消息，各自说明真实原因 |
| `fetch_coco_subset.mjs` 序列化一个永不为 1 的 `rejected.unknown_category`（Speculative Generality）；`content-range` 正则裸取 `[1]` 会在非 Range 服务器上抛 TypeError | 删字段；改为具名错误"the bucket does not answer Range"。固定清单随之重生成（面板字节指纹不变） |
| `coco_to_panel.mjs` 两处各写一遍 `replace(/ /g,'_')`，其中 `categories.find(...).name` 可为 undefined → TypeError | 抽出 `classOf(a)`，缺类别抛带 annotation id 的具名错误 |
| `annotation.cpp` 头注释写"Reference mode only"像是一条被强制的规则，实际没有任何守卫拒绝 `--mode performance --annotations` | 改为陈述意图 + 说明性能模式为何可用（真值不来自解码器），不再冒充不变式 |

审查确认无需改动的判断：`RunReport` 把 7 个标注字段摊平（与 `first_fault`/`stage_percentiles` 同族先例，`run_report_json` 需要自足）；`reset_scene` 走 friend-only 通道（仓库标准压倒 baseline 的 Middle Man/Feature Envy 直觉，L1 §2 条 5 就要求 TruthWorld 兼任装载者）；测试夹具程序内生成（L2 §1 局限 3）。**一条如实保留的差距**：审查再次指出 AC"新旧二进制逐字节对照"未执行——见 §2 的 ⚠️ 与 §9 条目 7，替代证据三条，未装作达成。

### §2 回归门：不装载标注时基线逐位不变

| 判据 | 期望 | 实测 |
|---|---|---|
| `--ticks 600 --seed 42` 的 `world_hash` | `574361e62eec92b9` | **一致** |
| 同上的 `ledger_hash` | `459ff85132cc103e` | **一致** |
| `slice11/12/16` 冻结字面量 | 逐位通过 | **通过**（两套构建各自全量绿） |
| 默认工件是否新增节 | 不含 `annotation_truth` | **不含**（slice18 断言 + 工件 JSON 复核） |
| 默认工件键序与拼写 | 与 HEAD 完全相同 | **相同**：`run_report_json` 的键发射序列 diff 只多出 `annotation_truth` 及其 7 个子键，全部在 `if (r.annotations_loaded)` 分支内；既有键零增删零换序 |

⚠️ **口径说明（不是缺陷，但必须写）**：owner 授权的验收方式是"新旧二进制逐字节对照"，本次未能执行——`git worktree` 基线构建被工作流权限拦下（不擅自绕过）。上表用三条替代证据覆盖同一主张：哈希级（oracle 值不变）、套件级（冻结字面量）、结构级（键发射序列 diff + 条件分支）。若 owner 要求二进制级对照，需允许临时 worktree 或保留一份改动前的 exe。

### §3 真实数据的确定性（出口判据 (a) 的 D3 半边）

| 证据 | 结果 |
|---|---|
| 同一面板连跑两次（ON，Release，200 tick） | `world_hash=725d0db5f1fad19a`、`ledger_hash=bda9ba6213ae15e7`、`divergence`/`ece` 全部**逐位一致** |
| 面板重建（`coco_to_panel.mjs` 第二次跑，200 帧 RGB + 两份清单的合并 SHA256） | `986690cd…f367da87` → **一致**（GDI+ 解码 + 拉伸在本机逐位可复现） |
| 跨构建对照：OFF 构建（blob_detect）与 ON 构建（rtdetr_v2_s）同一面板 | `world_hash` **相同**（`725d0db5f1fad19a`）——真值与感知参与者无关，正是单写者的应有形态；`ledger_hash` 不同（生产者集合不同） |
| 数据侧幂等 | `fetch_coco_subset.mjs --verify`：200 张全部命中固定 SHA256，`downloaded=0` |

### §4 面板是什么（许可、体量、构成）

**取数方式**：官方桶支持 HTTP Range，于是**不拉整包**——`annotations_trainval2017.zip`（252 907 541 B）只读尾部 64 KiB 定位中央目录（642 B），再 deflate 取出 `annotations/instances_val2017.json`（压缩 6 529 031 B → **19 987 840 B**）；图像按 `http://images.cocodataset.org/val2017/<file_name>` 逐张 GET。

| 项 | 值 |
|---|---|
| 面板帧数 / 原始字节 | 200 帧 @ 640×480 RGB8 = 176 947 200 B（gitignored `data/`） |
| JPEG 原图合计 | 33.5 MB（200 张） |
| 非 crowd 标注框 | **1 219** 个（每帧中位数 4、均值 6.1、最大 20） |
| crowd 框 | **10** 个，分布在 10 帧——**故意写进清单并标 `crowd=1`**，让规则 1 在真实数据上真的开火 |
| 归一化面积分布（非 crowd） | min 2.1e-5 · p25 0.0036 · median 0.0173 · p75 0.0899 · max 1.0 |
| 小于 100 px² 的框 | 35 个（占 2.9%）——D4 按尺度分层时必须带上 |
| 拉伸造成的宽高比畸变 | min 0.288 · median 1.000 · max 2.243（面板取"整幅拉伸"，归一化坐标因此**无需重算**；畸变是模型侧混淆项，不是真值错误） |

**许可收紧（对 D1 结论的更正，见 §9）**：COCO 的**标注**是 CC BY 4.0，但**图像**逐张沿用 Flickr 许可。实测 val2017 的 5 000 张：BY-NC-SA 1 431 / BY-NC 630 / BY-NC-ND 1 414（**非商业共 3 475 张 = 69.5%**）、BY-SA 417、BY-ND 246、**BY 857**、无已知版权限制 5。面板准入规则：`license.id ∈ {4 (CC-BY), 7 (无已知版权限制), 8 (美国政府作品)}`——排除全部 NC，且排除 ND/SA（面板对图像做缩放派生）。扫描至第 1 194 张候选时凑满 200 张：**被许可规则拦下 988 张**，因非 crowd 框数 >20 拦下 6 张；最终面板全部落在 license 4。逐图许可与 `flickr_url` 记录在 `data/coco/LICENSE.tsv`（入库的是生成它的脚本与 `tools/fetch_coco_subset.manifest.json`——**只记选择**：image id / 许可 / `flickr_url` / 每图 SHA256 / **标注 id 列表**；bbox 几何一律不进 git，否则取数脚本自己就把"数据不入库"破坏了。重构为 id-only 后面板字节指纹不变：`986690cd…f367da87`）。

### §5 引擎侧口径计数与真实面板的一致性

面板 `truth.txt` 声明 1 229 个框（1 219 + 10 crowd），装载后工件报：

```json
"annotation_truth": {
  "dataset": "COCO val2017 subset (200 images, pinned in tools/fetch_coco_subset.manifest.json)",
  "license": "annotations CC BY 4.0 (COCO Consortium); images per-image CC, admitted license ids 4 …",
  "frames": 200, "boxes_kept": 1219,
  "dropped_crowd": 10, "dropped_degenerate": 0, "dropped_over_cap": 0
}
```

三个计数与面板统计**逐项吻合**（crowd 10=10、cap 0=0，因取数侧刻意选了 ≤20 框的图；退化 0 因归一化在源像素上做，无越界框）。这就是 R-D4 要的东西：**"标注没说"与"模型错了"在工件里是可区分的两行**。

### §6 逐阶段延迟（ON / Release / CPU EP，参考模式；微秒，p50/p95）

| 阶段 | p50 | p95 | 说明 |
|---|---|---|---|
| `truth_load` | **0.6** | 1.1 | 场景替换 + 逐个铸造（中位 4 实体/tick）；`truth_step` **不出现**=运动学没跑 |
| `frame_fetch` | **596.7** | 790.1 | 921 600 B/帧裸 RGB 读盘，约 1.5 GB/s |
| `detect:rtdetr_v2_s` | **522 541.8** | 541 866.0 | CPU EP；端到端 1.9 Hz 由它决定 |
| `adjudicate` | 1.2 | 9.9 | |
| `metrics` | 10.9 | 20.5 | |
| `predict:zero_delta` | 2.5 | 5.8 | |

⚠️ `TickLog::latency` 的四段粗粒度标签里，`render_ms` 在有 `--frames` 的面板上覆盖的是"装载真值 + 取帧"（历史字段名，语义未改）。D5 引入真解码器时这个标签必须一起清算，不在本片偷偷改名。

### §7 指标第一次落在模型自己的定义域里

真实面板（200 帧、2 229 条账本记录、applied 1 527 / announced 702 / rejected 0 / faults 0）：

| 指标 | 值 | 现在的语义 |
|---|---|---|
| `divergence` | **0.010747** | 匹配主张与真值中心的归一化距离＝**检测器定位误差**（约帧宽 1.07%）。D2 时这个数字没有语义 |
| `ece` | 0.363534 | 聚合值（跨生产者） |
| `ece:rtdetr_v2_s` | **0.198829** | 真实检测器在真实标注上的校准误差——判据 (b)/(c) 面板的"诚实一侧"基准，**D4 的分离量从这里起算** |
| `ece:zero_delta` | 0.5 | 世界模型对"逐 tick 换场景"的外推必然失败；这是面板构造的性质，不是它的缺陷 |
| `identity_stability` | 0 | **在本面板无语义**：每 tick 是新场景（公开 COCO 实例标注无 track id，已核验），匹配 id 集合几乎不会跨 tick 相同。契约② 不被破坏——id 仍单调、不复用，只是不声称跨帧同一实体 |

`--adapters blob_detect` 的同一面板：`applied=2 634 / announced=204 / divergence=0.003576 / ece=0.497181`（内建色块检测器对真实照片的性质做机械匹配，数字只用于证明通路与 per-producer 分解，不作任何模型主张）。

### §8 标注口径附页（判据 (b)/(c) 报告必须沿用）

冻结规则与代码落点一一对应（`src/truth_world/annotation.cpp` 注释按规则编号点名）：

1. `crowd=1` 丢弃并计数——合并区域不是实例，按单实例评分必然判错；
2. 坐标钳进 [0,1] 后可见面积 < `kAnnotationMinArea = 1e-6`（**归一化单位**，在 640×480 上 ≈ 0.31 px²）的框丢弃并计数；保留框的中心取钳制后的可见部分。阈值用归一化单位是刻意的：口径不能随分辨率改定义。slice18 有专门的截断用例（`box -0.10 …` → 中心 0.15 而不是数据集的 0.10）；
3. 每帧上限 20 个实例，按 `source_id` 升序保留（与目标大小无关 → 既不偏易也不偏难），超出计数；
4. 类别名必须在清单头部 `categories=` 表内，否则**具名错误**（不静默跳过）；同帧 `source_id` 重复按损坏数据报错；
5. **遮挡与截断在本数据源不可表达**：实测公开 `instances_val2017.json` 的标注字段只有 `area, bbox, category_id, id, image_id, iscrowd, segmentation`——没有 `truncated`/`occluded`/track id（这三个在别的子集或根本不存在）。任何"按遮挡过滤"的规则都是虚构的，故一条也不加；
6. 归一化一律相对清单头部声明的画布；整幅拉伸使归一化坐标不变，因此无重算、无填充偏移；
7. 帧内顺序 = `source_id` 升序，块序 = tick 升序（铸造顺序与文件物理顺序解耦）。

### §9 偏差、遗留与如实限制

| # | 条目 |
|---|---|
| 1 | **对 D1 结论的更正**：D1 note §6 写"COCO 标注 CC BY 4.0 ✅"，未量化图像许可。实测 69.5% 的 val2017 图像是 NonCommercial。已回写 D1 note 并在本片收紧准入（§4）。 |
| 2 | **签名偏离 L1 草案**：`load_annotations(WorldState&, const AnnotationSet&, Tick)` 多一个 tick 参数——时间归主循环（契约③），否则标注集会带着第二个时钟。已记 L2 §9 偏差 1。 |
| 3 | **不是"单文件"切片**：`WorldState` 多一条 friend-only 私有原语 `reset_scene(Tick)`。公开读接口、`friend` 机制、契约文本三者零改动；核心确有 1 行新增，如实标注（L2 §9 偏差 2）。 |
| 4 | 评分是**纯位置**的：关联策略不看类别，适配器也不铸类别标签。本面板的"正确"＝定位正确，**不是 mAP**，不可与 COCO 官方检测结果横向比较。 |
| 5 | 帧间无实体同一性（数据集形态决定，非引擎限制）。需要跨帧同一实体时得换带 track id 的数据源，那会重新引入许可问题（KITTI 类多为非商业）。 |
| 6 | 面板取整幅拉伸（median 畸变 1.000，最差 2.243）。这是模型侧混淆项，会压低真实检测器成绩；D4 若要横向比较需分层或改 letterbox。 |
| 7 | 逐字节新旧二进制对照未做（worktree 被权限拦下），替代证据见 §2 表。 |
| 8 | **标注模式下 `--seed` 没有作用对象**：真值不来自 RNG，`build_demo_scene` 不执行。该面板的确定性由两份清单的字节承担（§3 的三重证据即此）。工件仍如实报 seed——读的人需要知道它与 600/42 的可复现性**来源不同**。 |
| 9 | **"标注真值必须配真实帧"这条在两层各守一次**：`parse_config` 之外，`LoopDriver::run()` 也抛具名错误。理由：绕过 CLI 直接调 API 会得到"渲染器把真值画出来给检测器看"的自指面板——那种面板**天然全对**，是最该禁止的一种测量。slice18 有这一条的用例。 |
| 10 | **本片不碰 GPU**：D3 全程 CPU，CUDA/cuDNN 仍阻塞（D1 结论不变）。D5 的主线状态不受本片影响。 |

**下一片**：D4（观察空间评分 `ScoreSpace::ObservationUV` + 判据 (b)/(c) 在真实标注面板上重测）。§7 的两个数（诚实 0.198829 vs 注入错误置信度一侧待测）就是 D4 的起点；阈值 0.30 不动。
