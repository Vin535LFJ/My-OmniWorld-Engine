# 04: 观察空间评分 + 判据 (c) 重测（D4）

**What to build:** 阶段 A 挂到现在的**判据 (c) 终于有可关闭的面板**。落地 [ADR-0013](../../../docs/adr/0013-scoring-space.md)：`enum class ScoreSpace { WorldXZ, ObservationUV }` 进 `core/types.hpp`，`matched_error` 按空间分派（经 `ViewCamera` 投影，不绕过相机），**默认 `WorldXZ` 使现有基线逐位不变**。然后在"真实帧 + 真实标注 + 观察空间"面板上重测 per-producer ECE 分离量，对照 0.30 阈值——**阈值不动**，达标就达标，不达标就给出口径原因。

**Blocked by:** 03（要有真值才能评分）

**Status:** verifying — 2026-10-27（L2：[`04-observation-space-scoring.md`](../../../docs/design/stage-d-real-data/slices/04-observation-space-scoring.md)；报告：[`2026-10-27-stage-d-d4-observation-space-scoring.md`](../../../benchmarks/regression/2026-10-27-stage-d-d4-observation-space-scoring.md)）
推进态记录：`pending → design-review`（L2 已产出）→ **`implementing`（owner 2026-10-27 批准："批准 L2，进入实现"；硬规则 1 的那一跳由 owner 触发）** → `verifying`（实现完成，两套构建全量测试绿，M1–M7 七个工件已产出并裁定；等 owner 确认后转 `done`）。

**流程：** 先出 L2 → owner 批准 → TDD 实现。**本片不需要 GPU 环境**（L1 §2 条 9 的结构解耦）。

- [x] `ScoreSpace` 类型 + `matched_error` 分派；容差语义不变（同一个 `kCorrectTol`）→ L2 §2.1 / §3.1 / R4　**证据**：报告 §1（slice19 双向差异用例）、`src/core/types.hpp` / `src/metrics/metrics.cpp`
- [x] **加法式硬约束**：`slice11/12/16` 字面量与 600/42 基线必须继续逐位通过；变红按回归缺陷处理，**不得更新基线** → L2 R1 + §7 测试 3/4；**工件级门槛主动降级**为"既有键逐项相同 + 新键纯追加"（L2 §9 偏差 1，如实登记）　**证据**：报告 §2（`world_hash 574361e62eec92b9` / `ledger_hash 459ff85132cc103e` / `ledger_entries 9746` 一致；D3 已发布数字在 D4 二进制上逐位复现）
- [x] 判据 (c) 重测：诚实 vs 注入错误置信度，在真实标注面板上的 per-producer ECE 分离量实测 → L2 §5.1（面板/生产者/配置写死）+ §5.4（M1–M7 测量矩阵）+ §5.2（裁定规则，阈值 0.30 不动）　**证据**：报告 §3（M1 separation **0.305180 ≥ 0.30 → 达标**）、§4（M2 空间对照 0.301171）、§5（M4–M7 分层）、§6（M3 世界模型对照）
- [x] **指标口径前置核对**（problem-log 015）：分离量必须与**覆盖率**（applied / 全部信念）一起报。`divergence` 只在匹配上的主张上求均值，分母会随退化收缩——研究笔记 E1 实测：时滞让覆盖率掉 31%，`divergence` 却**不涨反跌**。只报 ECE 分离量时必须显式说明该口径的局限 → L2 §4.1：新增 `coverage` / `coverage:<producer>` / `claims:<producer>` / `divergence:<producer>` / `divergence_n` / `divergence_n:<producer>`（加法式，不动既有键）　**证据**：报告 §3 伴随量表（`coverage 0.33553719008264465`、`divergence_n 406` 与 `(308+98)/(1010+200)` 自洽）、§6（M3 实测复现稀释病态：聚合 `divergence` 0.052053 → 0.013005）
- [x] **实验条件自证**（problem-log 016）：工件里的阶段键（`detect:` / `predict:`）必须与实验设计一致。省略 `--models` **不等于**关闭世界模型——CLI 默认带 `zero_delta` + `linear_extrapolation`，且它们**读真值**，会稀释 divergence → L2 §5.1：主测量用 `--models ""`（纯感知），另跑 `--models zero_delta` 作对照；工件阶段键列入必报项　**证据**：报告 §7（M1/M2/M4–M7 只有 `detect:*`、无 `predict:*`；M3 有 `predict:zero_delta`）
- [x] 报告按**目标尺度分层**（uv 容差对大小目标难度不同），避免重演"聚合掩盖结构"（problem-log 005）→ L2 §5.3：**数据侧子面板**（`tools/stratify_panel.mjs`），因为引擎的契约里没有"目标大小"字段，给 `Entity`/`Proposal` 加字段会改冻结哈希；层内匹配与全面板逐字相同，不扰动匹配　**证据**：报告 §5（small 0.357171 / **large 0.220196 < 0.30 → 如实登记"判别力在目标尺度上不均匀"**；工具自证保留框 1 219 = 工件 `boxes_kept`）
- [x] 观察空间评分的**适用边界**写进代码注释与报告：只用于同一观察序列内的校准比较，不得支撑跨场景定位主张（ADR-0013 / L1 §7 R-D7）→ L2 §3.3 末段 + §9 偏差 4（四条注释的落点）　**证据**：报告 §9；`src/metrics/metrics.cpp` 分派处注释
- [x] slice19 测试：两个评分空间的行为差异可断言 + 各自确定性成对；默认空间零变化 → L2 §7（10 组用例；OFF 17→18 项、ON 19→20 项）　**证据**：报告 §1（OFF **18/18** 绿、ON **20/20** 绿）
- [x] 判据 (c) 裁定：达标 → 更新 PLAN 与 L0 §2 ⑦ 追加说明；未达标 → 如实 PARTIAL + 关闭路径 → L2 §5.2 裁定规则 + §5.5 关闭路径（先写好三条候选，避免临场发挥）+ §9 偏差 5（两条路都要出报告）　**证据**：报告 §3（**PASS**）+ §11（13 条偏差登记，含 large 层塌陷的机制与未修复声明）；`PLAN.md` 阶段 D 行 + `docs/design/00-总体设计.md` L0 §2 ⑦ 已同步

## Comments

（执行时填写）

- 2026-10-27 · **前置口径核对（来自研究笔记 E1，零核心改动的实测）**：[`docs/17-research/2026-10-27-interactive-closed-loop-route.md`](../../../docs/17-research/2026-10-27-interactive-closed-loop-route.md) 在 D3 的 200 帧面板上做了"观察时滞 → 信念误差"的三组对照（S1 面板对照 / S2 CLI 默认 / S3 纯感知），产出三条对本片有约束力的结论：① **判据 (c) 用的 ECE 会随"输入是否对得上真值"漂 0.199 → 0.294**，所以分离量必须与覆盖率一起报；② **`divergence` 在本面板会被读真值的世界模型稀释**（S1 的 71% 账本条目来自神谕项），若本片沿用 S1 配置复现 D3 的数字，须声明该口径；③ **`identity_stability` 在本面板恒为 0**（每 tick 换场景、无 track id），且其语义已查明 ≡ "相邻两 tick 都零匹配"的频率，**不可作为本片的佐证指标**。本片若引入覆盖率指标，属**加法式**新增（不动既有字段与字面量）。
- 2026-10-27 · **L2 已产出**：[`docs/design/stage-d-real-data/slices/04-observation-space-scoring.md`](../../../docs/design/stage-d-real-data/slices/04-observation-space-scoring.md)。六条需要 owner 拍板的设计决定：
  1. **R3 视野外不评分**：相机的 `pixel_x/pixel_z` 会**钳制**到边框——视野外的真值若照样投影，会与边框上的主张"距离为零"→ **钳制伪造出"正确"**。故视野外的对：ECE 留在分母不给分、divergence 均值排除，`divergence_n` 让排除量可见。
  2. **R5 裁决不动**：匹配留在世界空间——改它会改账本 verdict，动 `ledger_hash` 这个冻结 oracle。ADR-0013 改的是"错误在哪儿量"，不是"谁配谁"。
  3. **工件级门槛主动降级**（L2 §9 偏差 1）：新增 6 组指标键 + `config` 一行 → **默认工件不再是逐字节不变**。仍冻结的是 600/42 哈希、`slice11/12/16` 字面量、既有指标键的值与相对顺序。
  4. **尺度分层放数据侧**（L2 §5.3）：引擎契约里没有"目标大小"，给 `Entity`/`Proposal` 加字段会改冻结哈希 → 用**子面板**（帧的子序列，真值块重编号），层内真值/主张/匹配与全面板逐字相同，不扰动匹配；层间是不同图像，层间比较是生态学的，报告要写明。
  5. **主测量用 `--models ""`（纯感知）**，`--models zero_delta` 只作对照——后者是逐生产者指标"与世界模型集合无关"这一待验证命题的对照，不预设成立。
  6. **裁定规则**：主判据 = 全面板 + 观察空间 + 纯感知下的 `|ece:rtdetr_v2_s − ece:blob_detect_overconfident| ≥ 0.30`；阈值不动。尺度分层与世界空间对照必须一并报（不得只报聚合值）。**裁定不写进测试**（problem-log 012）。未达标时的三条关闭路径已先写好（L2 §5.5）。
- 2026-10-27 · **一个必须先说清的事实**：本引擎的相机是单位正方形 ↔ 像素的**恒等**归一化投影（D3 L2 §3 条 5），所以两个评分空间是同一平面上的两套刻度（连续 vs 量化到 1 px 栅格），**误差值只差不超过一个像素**。`ObservationUV` 的价值不是"发明了新几何"，而是**把比较明确限定在同一观察序列内**（ADR-0013 / R-D7），以及在相机不再是恒等时不需要再动契约。报告里不得把它写成换算创新。
- 2026-10-27 · **owner 批准**："批准 L2，进入实现（推荐）" → 推进态 `design-review → implementing`（硬规则 1 的那一跳由 owner 触发）。实现按 L2 §7 的 10 组用例走 TDD：先红（`slice19` 未注册 → 断言不成立），再实现 `src/core/types.hpp` / `src/observation/observation.hpp` / `src/metrics/metrics.{hpp,cpp}` / `src/loop/driver.{hpp,cpp}`。
- 2026-10-27 · **实现要点**：`ViewCamera` 纯搬移到 `core/types.hpp`（避免新增 `metrics → observation` 模块边）；`ScoreSpace` + `ScoreContext` 进核心；`matched_error` 按空间分派，观察空间**只经 `ViewCamera` 的 `pixel_x/pixel_z` + `world_x/world_z`** 取 uv（R2），视野外不评分（R3），容差仍是同一个 `kCorrectTol`（R4）；裁决与账本不动（R5）。新增 6 组指标键（`coverage` / `coverage:<p>` / `claims:<p>` / `divergence:<p>` / `divergence_n` / `divergence_n:<p>`）**并行**于既有累加，既有聚合的累加顺序不动 → 既有键逐位不变。
- 2026-10-27 · **测量与裁定**：M1–M7 七个工件（`data/panel/d4_m*.json`，gitignored）按 L2 §5.4 的矩阵产出。主判据 M1 = **0.305180 ≥ 0.30 → 达标**；M2（世界空间对照）0.301171，两空间差 4.0099e-3 **在像素量化上界 5.2083e-3 之内**（每个生产者恰 1 条主张跨过容差边界，差值恰为 1/N）；**大目标层塌陷到 0.220196 < 0.30**，已按 L2 §5.2 要求写明"判别力在目标尺度上不均匀"（机制：绝对容差 vs 目标尺度，大框定位相对误差更小 → 诚实侧正确率上升 → 分离量压缩），**不掩盖**。
- 2026-10-27 · **D3 报告遗留的"新旧二进制逐字节对照"在本片被补上（较弱但可执行的形态）**：用 D4 二进制重跑 D3 的发布配置，`world_hash` / `ledger_hash` / `ledger_entries` / `divergence` / `ece` / `ece:rtdetr_v2_s` / `identity_stability` **与 D3 报告逐位一致**（报告 §2 表）。
- 2026-10-27 · **M3 对照的实测结论**：逐生产者指标（`ece:<p>` / `divergence:<p>` / `divergence_n:<p>` / `coverage:<p>` / `claims:<p>`）在"纯感知"与"+zero_delta"两种配置下**逐位相同**，而聚合 `divergence` 从 0.052053 **塌陷**到 0.013005、聚合 `coverage` 从 0.3355 **虚高**到 0.6690 —— problem-log 015 的神谕稀释病态在真实面板上被复现，且**只在聚合键上可见**。这就是本片新增分解键的全部理由。
- 2026-10-27 · **诚实侧 ECE 不为 0**（0.1998）：本面板的"正确"是**纯位置**判定（不判类别），因此诚实侧天然不为 0。分离量 0.305 不是"完美模型 vs 坏模型"，而是"尚有 0.20 定位误差的诚实侧"与"0.99 置信度对 0.49 正确率的注入侧"之差（problem-log 012 的纪律：必须回答"分离来自哪里"）。
- 2026-10-27 · **遗留（不掩盖）**：① 大目标层 0.220196 的修复方向未定（自适应容差要动 `kCorrectTol` 语义 → 需新 ADR；换带 track id 的数据集 → Phase 4 数据侧立项）；② 面板整幅拉伸的畸变未做分层（模型侧混杂项）；③ R3 在本面板触发 0 次（真值位置恒在 `[0,1]²` 内），只有 slice19 用例在行使它。

- 2026-10-27 · **工件发射器曾丢一个分隔符，而全部既有断言都没抓到**：`config.command_line` 加入后，`"score_space"` 与 `"command_line"` 之间少了逗号 ⇒ 产出的工件**不是合法 JSON**（任何解析器都读不了），而既有测试只做**子串查找**，逐条通过。是「用解析器打开工件」这一步发现的。已修，并新增 **JSON 语法守卫**：`slice19` 内自带严格 JSON 校验器，**先用反例自证校验器本身有效**（缺分隔符 / 尾逗号 / 尾随垃圾 / 非法转义都判否），再断言工件可解析（含带反斜杠的路径）。登记为报告 §11 条目 13——**测试全绿 ≠ 工件可用**，证据文件必须真能被工具读一次。
- 2026-10-27 · **工件重跑（补 `command_line` 字段）**：`config.command_line`（L2 §5.4 要求工件记录命令行）加入后，ON 构建先前那次编译失败（手写 bat 的输出重定向把 `vcvarsall` 的环境挡掉了——**problem-log 013 的同族坑：脚本生成脚本的转义层**），修好后 **ON 20/20 绿**。M1–M7 与 D3 复现、600/42 基线**全部用重建后的二进制重跑**，逐项比对：**所有指标值、哈希、计数与此前逐位相同**，仅新增 `config.command_line` 一行。修复后**十个工件全部重新产出**（600/42 基线、D3 复现、M1–M7、确定性复核），并逐个用解析器打开确认**均为合法 JSON**。冻结基线（`build/`，OFF）同样重跑，`world_hash 574361e62eec92b9` / `ledger_hash 459ff85132cc103e` / 9 746 条**未变**。
