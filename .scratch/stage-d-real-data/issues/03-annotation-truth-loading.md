# 03: 标注装载为真值（D3）

**What to build:** 真实标注成为逐 tick 真值，而**真值权威与单写者一字不动**。`TruthWorld` 新增 `load_annotations(WorldState&, const AnnotationSet&)`：把数据集标注（归一化图像框 + 可选 3D/标定）转成引擎内的实体集合，仍由 TruthWorld 写入 WorldState。`world_state.hpp` 的 `friend class TruthWorld` 机制保留——这是刻意的：抽真值接口会动契约④⑤，需要 ADR，本片不做（L1 §2 条 5）。

**Blocked by:** 02（要有真实观察序列，标注才有对应的帧）

**Status:** verifying — 2026-10-26（L2：[`03-annotation-truth-loading.md`](../../../docs/design/stage-d-real-data/slices/03-annotation-truth-loading.md)；证据：[`benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md`](../../../benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md)）
推进态记录：`pending → implementing`（owner 2026-10-26 指示，含取数授权）→ `verifying`（两套构建全量绿 + 双轴审查采纳完毕）。**`done` 留待 owner 确认**，因为两条 AC 是以替代证据达成的：AC4 的"新旧二进制逐字节对照"未跑（worktree 被拦，见报告 §2 ⚠️ 与 §9 条目 7），AC6"git diff = 单文件"不成立（登记为 L2 §9 偏差 2/3）。

**流程：** 先出 L2 → owner 批准 → TDD 实现。

- [x] `AnnotationSet` 数据形态 + 一个数据集的取数脚本（**数据不入库**：脚本 + SHA256 + 许可记录，照 `tools/fetch_models.mjs`）→ `src/truth_world/annotation.{hpp,cpp}`（行式清单，核心零 JSON 依赖）+ `tools/fetch_coco_subset.mjs`（HTTP Range 稀疏取数）+ `tools/coco_to_panel.mjs` + `tools/jpeg_to_rgb.ps1`；固定子集清单在 `tools/fetch_coco_subset.manifest.json`（200 张逐图 SHA256）
- [x] `TruthWorld::load_annotations`：装载为逐 tick 真值；单写者不变、契约④⑤零改动 → 签名多一个 `Tick` 参数（时间归主循环，L2 §9 偏差 1）；`WorldState` 只多一条 **friend-only** 私有原语 `reset_scene(Tick)`，公开读接口与 `friend` 机制未动（偏差 3）
- [x] **标注口径写死并记录**：遮挡/截断/丢弃规则、每帧实例上限、坐标归一化约定 → L2 §4 七条规则 = `annotation.cpp` 的注释编号；报告附 §8 口径附页。**实测更正**：公开 `instances_val2017.json` 的字段只有 `area,bbox,category_id,id,image_id,iscrowd,segmentation`——**没有 `truncated`/`occluded`/track id**，所以"按遮挡过滤"一条也不加（加了就是虚构）
- [x] 回归门：不装载标注时，阶段 A/B/C 全部基线（600/42 哈希 + `slice11/12/16` 字面量）逐位不变 → 600/42 实测 `574361e62eec92b9` / `459ff85132cc103e` 不变；`annotation_truth` 节只在装载时出现。**逐字节新旧对照未跑**（worktree 被权限拦下）：以哈希级 + 套件级 + 键发射序列结构级三条替代，见报告 §2 与 §9 条目 7
- [x] slice18 测试：装载行为断言（实体数/位置/类别）+ 确定性成对；装载失败给具名错误而非静默跳过 → 另加：跨场景替换后 id 仍单调不复用（契约②）、三种丢弃计数、CLI 两道守卫、`truth_load` 阶段键
- [~] git diff = 单文件 + build/文书 → **不成立，如实登记**：新模块 2 文件 + `truth_world.{hpp,cpp}` + `world_state.{hpp,cpp}`（1 条私有原语）+ `driver.{hpp,cpp}` + `apps` usage + 3 个 tools。L2 §9 偏差 2/3

## Comments

- 2026-10-26 · **实现**：`load_annotation_set()` 解析 + 施加口径（规则 1 crowd / 2 钳制后可见面积 / 3 每帧上限按 `source_id` 升序 / 4 类别表校验 / 7 顺序），五类畸形输入各自抛具名错误（含路径 + 行号）；`load_annotations` 走 `reset_scene` + 逐个 `spawn`，位置 = 钳制后可见框心，速度零、y=0；`--annotations` 必须与 `--frames` 同传，且与 `--bounds wall` 互斥（墙会把真值钳进 margin 内）。
- 2026-10-26 · **取数**：`annotations_trainval2017.zip` 252 907 541 B → 只读尾部 64 KiB + 中央目录 642 B + 目标条目 deflate 块 6 529 031 B，得 `instances_val2017.json` **19 987 840 B**；图像逐张 GET，**200 张 = 33.5 MB**。owner 授权的是 ~1 GB，实付约 40 MB。
- 2026-10-26 · **许可收紧（对 D1 结论的更正）**：实测 val2017 图像的 Flickr 许可分布——非商业合计 **3 475 / 5 000 = 69.5%**，另有 BY-ND 246、BY-SA 417。面板准入改为 `license.id ∈ {4 BY, 7 无已知限制, 8 政府作品}`；已回写 [D1 note §6.1](../../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)。逐图 `flickr_url` + SHA256 由脚本写 `LICENSE.tsv`。
- 2026-10-26 · **端到端实测**（Release `build-ort-c5/`，200 帧真实面板，`--adapters rtdetr_v2_s --models zero_delta --mode reference --ep cpu`）：
  `entries=2229`（applied 1527 / announced 702 / rejected 0 / **faults 0**）、`divergence=**0.010747**`（=检测器定位误差，帧宽约 1.07%）、`ece=0.363534`、`ece:rtdetr_v2_s=**0.198829**`、`ece:zero_delta=0.5`；口径计数 `dropped_crowd=10 / degenerate=0 / over_cap=0` 与面板统计逐项吻合；阶段 p50：`truth_load 0.6 µs` · `frame_fetch 596.7 µs` · `detect 522.5 ms`。
- 2026-10-26 · **确定性三重证据**：① 同面板两次跑 `world_hash=725d0db5f1fad19a` / `ledger_hash=bda9ba6213ae15e7` 逐位一致；② 面板重建后 200 帧 RGB + 两份清单的合并 SHA256 一致（GDI+ 解码拉伸逐位可复现）；③ OFF 构建（blob_detect）与 ON 构建（rtdetr）同一面板 **`world_hash` 相同**、`ledger_hash` 不同——真值与感知参与者无关，正是单写者的应有形态。
- 2026-10-26 · **本面板仍不可主张**：评分纯位置（不看类别，不是 mAP）；帧间无实体同一性 → `identity_stability=0` 是构造性质不是缺陷；D3 只给出判据 (b) 的"诚实一侧"基准，分离量属 D4。
- 2026-10-26 · **回归**：默认 OFF **17/17**、ON **19/19**（新增 ow_slice18）；600/42 基线哈希与 `slice11/12/16` 字面量逐位不变。
- 2026-10-26 · **未做到的验收方式（如实）**：票据默认的"新旧二进制逐字节对照"没跑——`git worktree` 基线构建被工作流权限拦下，未擅自绕过。替代证据见报告 §2（哈希级 + 套件级 + 结构级三条）。owner 若要二进制级对照，需允许临时 worktree。
- 2026-10-26 · **双轴审查（Standards + Spec）后返工**：9 项采纳并当场修完，明细见报告 §1.1。要点是三件语义级的：① `--annotations` 无帧时**只在 CLI 挡**不够——直连 `LoopDriver::run()` 会得到"渲染器把真值画给检测器看"的自指面板（天然全对），现在引擎里也抛；② 画布一致性检查原先钉在 `t == 1`，改为"首个成功到达的帧"；③ 规则 2 的"可见中心"与"整帧清空"两条**只有实现没有测试**，已补用例（含"空帧不消耗 id"）。修完重跑：OFF 17/17、ON 19/19、面板两次跑 `world_hash`/`ledger_hash`/`metrics`/`annotation_truth` 与返工前逐项相同。
- 2026-10-26 · 新坑：2 条 → problem-log **013**（`printf` 转义吃掉 `\v`/`\U`，两次踩在同一个 bat 生成上）、**014**（清单相对路径按清单目录解析，子目录前缀不能省）。
