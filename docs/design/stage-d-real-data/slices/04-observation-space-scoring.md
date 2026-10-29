# L2 · 切片 D4「观察空间评分 + 判据 (c) 重测」详细设计

> **设计版本**：v1 · **批准方式**：owner 批准后进入 `implementing` · **批准日期**：—
> 状态：**design-review**（L2 已产出，等 owner 批准；`design-review → implementing` 这一跳只能由 owner 触发——`docs/agents/triage-labels.md` 硬规则 1，**本片未获批前不写实现代码**）
> 上承：[阶段 D L1](../概要设计.md) §2 条 6 / §5 / §6 / §7 R-D4·R-D5·R-D7 / §8 + [ADR-0013](../../../adr/0013-scoring-space.md) + [D3 L2](./03-annotation-truth-loading.md) + [研究笔记 2026-10-27](../../../17-research/2026-10-27-interactive-closed-loop-route.md) §6（实验 E1） + problem-log **015** / **016**。票据：[04](../../../../.scratch/stage-d-real-data/issues/04-observation-space-scoring.md)。
> 本片**不需要 GPU 环境**（L1 §2 条 9 的结构解耦）。

---

## 1. 程序描述

判据 (c)（"ECE 能判别接近校准的正常模型与注入错误置信度的模型"）从阶段 A 挂到阶段 C，其关闭条件被 problem-log 012 重新定位到"真实帧 + 真实标注"这个前提上。阶段 D 的 D2/D3 已经把那个前提造出来了（真实帧入环、真实标注逐 tick 成真值），**本片做最后两件事**：

1. **把"在哪里比"这件事显式化**（ADR-0013）：`ScoreSpace { WorldXZ, ObservationUV }` 进 `core/types.hpp`，`matched_error` 按空间分派，经 `ViewCamera` 的投影，**不绕过相机**；默认 `WorldXZ` 使阶段 A/B/C 的全部冻结基线逐位不变。
2. **在真实面板上重测分离量，并按 problem-log 015/016 的要求把口径补齐**：分离量必须与**覆盖率**、**每生产者样本量**、**分母**一起报；实验条件必须由工件里的阶段键自证；报告按**目标尺度分层**。

**本片刻意不做的三件事**（先说清楚，避免把"没做"读成"漏了"）：

- **不改裁决（adjudication）**。匹配（哪条主张配哪个真值）留在世界空间——改它会改账本的 verdict，而 `ledger_hash` 是冻结 oracle（见 §3 规则 R5）。
- **不改 ECE 的公式与分箱**。10 个置信度箱、`|correct − conf_sum|` 的聚合方式、`ece` / `ece:<producer>` 的键与值全部逐位不变（新增的分解键另算，见 §4）。
- **不改 `identity_stability` 的语义**。它在本面板本来就没有语义（每 tick 换场景），而 problem-log 015 点的"空集也算相等"是**另一个**面板的病态（见 §4.3）。

## 2. 数据结构与接口

### 2.1 `core/types.hpp`：两个新增值类型

```cpp
// Score space (ADR-0013): where a position claim is MEASURED.
//   WorldXZ      -- world x/z plane (v1 semantics; the frozen default)
//   ObservationUV-- the observation's own normalized image coordinates,
//                   obtained by projecting through ViewCamera
enum class ScoreSpace : std::uint8_t { WorldXZ, ObservationUV };

// ViewCamera MOVES here from observation/observation.hpp (verbatim, no
// semantic change). Rationale: it is now consumed by three seams -- the
// observation (producer), the perception adapters, and the metrics
// (scorer) -- and ADR-0013's own precedent says a value type shared by
// several seams belongs in core (BoundsPolicy / ExecProvider). Moving it
// avoids a metrics -> observation module edge.
struct ViewCamera { /* ... unchanged: pose, width, height, pixel_x/pixel_z,
                       world_x/world_z ... */ };
```

`observation/observation.hpp` 改为 `#include "core/types.hpp"`（它本来就有）并删除本地的 `ViewCamera` 定义——**纯搬移**，所有既有使用者（`frame_source.cpp` / `perception/adapter.hpp` / `driver.cpp`）经由既有 include 链继续拿到它，无需改写。

### 2.2 `src/metrics/metrics.hpp`：评分上下文

```cpp
// Everything that decides HOW a position claim is measured. The score space
// is a run-level policy; the camera is the observation's own (contract 8).
struct ScoreContext {
  ScoreSpace space = ScoreSpace::WorldXZ;
  ViewCamera view;  // the camera of the observation being scored this tick
};

class Metric {
 public:
  virtual void feed(const WorldState&, const BeliefLedger&, Tick,
                    const ScoreContext& ctx = ScoreContext{}) = 0;
  ...
};
class MetricsModule {
 public:
  void feed(const WorldState&, const BeliefLedger&, Tick,
            const ScoreContext& ctx = ScoreContext{});
  ...
};
```

**默认实参 = `ScoreContext{}`（WorldXZ + 无相机）**——这正是既有行为，所以 `slice05` 等既有单测的调用点与断言**一个字都不用改**（唯一的签名变更在 `Metric::feed` 的虚函数表上，测试里那个自定义 Metric 需要跟着改一行覆写）。

**相机为什么必须进评分器**：ObservationUV 的定义就是"经相机投影"（ADR-0013）。评分器拿不到相机就只能另算一套 uv 映射——那正是"绕过相机"，也是 ADR-0013 明令禁止的。

### 2.3 `driver.hpp`：配置与工件

- `RunConfig` 新增 `ScoreSpace score_space;`（**无默认值**，由 CLI 层填——L1 §6 的纪律）。
- `RunReport` 新增 `ScoreSpace score_space = ScoreSpace::WorldXZ;`（默认值合法：报告侧只需要"默认值即 WorldXZ"这一条不变式）。
- CLI 新增 `--score-space world|observation`（默认 `world`；未知值 → 具名错误，与 `--mode` / `--ep` 同族）。
- 工件 `config` 节新增一行 `"score_space": "world" | "observation"`（**始终输出**：工件必须自述用的是哪个空间，否则指标不可解释）。

## 3. 评分空间语义

### 3.1 分派规则

```cpp
// metrics.cpp
bool matched_error(const BeliefEntry& e, const WorldState& ws,
                   const ScoreContext& ctx, double& err);
```

- **WorldXZ**（逐字不变）：`err = sqrt(Δx² + Δz²)`，Δ 取自主张位置与当前真值位置。
- **ObservationUV**：把主张与真值**各自**投到相机的像素栅格上，再用相机自己的逆映射换算成归一化坐标后比距离：

  ```cpp
  const float u_p = cam.world_x(cam.pixel_x(p.x));   // 相机自己的正/逆映射
  const float v_p = cam.world_z(cam.pixel_z(p.z));
  const float u_t = cam.world_x(cam.pixel_x(t.x));
  const float v_t = cam.world_z(cam.pixel_z(t.z));
  err = sqrt((u_p - u_t)² + (v_p - v_t)²);
  ```

  `ViewCamera` 的正映射是 `pixel_x(px) = floor(x * width)`，逆映射是 `world_x(px) = (px + 0.5) / width`——**这正是 RT-DETR 适配器把框心当 x/z 用的那套恒等归一化投影**（`src/perception/rtdetr.cpp:172-178`），所以"经相机"在这里不是新几何，而是**同一投影的显式往返**。

### 3.2 五条规则（写死，实现照抄）

| # | 规则 | 理由 |
|---|---|---|
| **R1** | **默认 = WorldXZ**，`RunConfig` / CLI / `ScoreContext` 的默认值都是它 | 阶段 A/B/C 的 `world_hash` / `ledger_hash` 与 `slice11/12/16` 字面量是 oracle（ADR-0013 Consequences 第 1 条）。默认空间零变化是可断言的不变式，不是"应该不会变" |
| **R2** | **必须经相机**：uv 只能由 `ViewCamera::pixel_x/pixel_z` + `world_x/world_z` 得到，metrics 里**不得**另写一套 `x → u` 的映射 | ADR-0013 明令"不绕过相机"。另写一套映射 = 投影语义有两个来源，日后必然分叉 |
| **R3** | **视野外不评分**：任一位置不在相机的世界窗口 `[0,1]²` 内 → 该对**不具可比性**——ECE 里**留在分母、不给正确性分**；divergence 的均值里**排除** | 相机的 `pixel_x` 会**钳制**到边框。视野外的真值若照样投影，会被钉在边框像素上，与一个同样在边框的主张"距离为零"→ **钳制会伪造出"正确"**。这是本片必须防的假阳性，不是理论洁癖 |
| **R4** | **容差语义不变**：两个空间用**同一个** `kCorrectTol = 0.1f` | 本引擎里世界 x/z **就是**归一化画布坐标（D3 L2 §3 条 5），所以 0.1 世界单位与 0.1 归一化 uv 是同一个物理量，不是"两套容差"。两个空间的**误差值**只差在像素量化上（每轴 ≤ 1 px，即 ≤ 1/width 与 1/height 的归一化量），因此分离量应当几乎相同——**实测差多少要写进报告** |
| **R5** | **裁决不动**：匹配（哪条主张配哪个真值）仍在世界空间 | 改匹配 = 改账本 verdict = 改 `ledger_hash` = 动冻结 oracle。ADR-0013 改的是"错误在哪儿量"，不是"谁配谁" |

**R3 的"不给正确性分"为什么不是"移出分母"**：移出分母 = 分母收缩 = problem-log 015 的 M1 病态（分母会随退化收缩，空集甚至算"完美校准"）。留在分母里当"不正确"是**保守**的方向：它不会让指标变好看。真正的解封办法是把 R3 的触发量报出来——见 §4 的 `divergence_n`（分母 = 可评分的匹配条目数，与"匹配条目数"之差就是被 R3 挡掉的量）。

### 3.3 两个空间的关系（写进代码注释与报告）

在本引擎的当前相机（单位正方形 ↔ 像素的**恒等**归一化投影）下：

- 两个空间是**同一个平面**上的两套刻度：世界刻度（连续）与像素刻度（量化到 1 px 栅格）。
- 因此 `ObservationUV` 与 `WorldXZ` 的**误差值**相差不超过一个像素的归一化量；分离量应当几乎相同。
- **它们真正不同的地方不是数值，而是语义**：观察空间明确回答"在这张图里，这两个位置是不是同一个点"，世界空间回答"在这张图的坐标系里，这两个位置差多远"。当相机不再是恒等（真实相机位姿、非俯视）时，世界刻度会随场景旋转/缩放而变，观察刻度不会——**这正是 ADR-0013 想要的比较空间**。
- **适用边界（ADR-0013 / L1 §7 R-D7，必须同时进代码注释与报告）**：观察空间评分**只用于同一观察序列内的校准比较**；**不得**用它支撑任何跨场景的位置/定位主张。

## 4. 指标口径

### 4.1 新增（加法式，既有键一律不动）

| 键 | 含义 | 为什么需要 |
|---|---|---|
| `coverage` | 匹配上的主张 / 全部主张（聚合） | problem-log 015 的核心：**均值必须连同分母一起报** |
| `coverage:<producer>` | 同上，逐生产者 | 神谕稀释是**逐生产者**的现象（S1 里 71% 的账本条目来自读真值的模型） |
| `claims:<producer>` | 该生产者的主张条数 | ECE 的**样本量**目前不可见；分离量的可信度取决于两侧各有多少条 |
| `divergence:<producer>` | 逐生产者的定位误差均值 | 聚合均值会被神谕稀释（S1：`divergence` 被拉到 0.0107） |
| `divergence_n` | 聚合 divergence 的**分母**（可评分的匹配条目数） | R3 挡掉多少条从此可见；也是"均值 + 分母"纪律的落点 |
| `divergence_n:<producer>` | 同上，逐生产者 | 同上 |

**空集约定（与 `identity_stability` 相反，且是刻意的）**：账本为空时 `coverage = 0.0`（"没有任何信念锚到真值"），不是 1.0。`identity_stability` 在空账本上给 1.0 是它的既有语义，本片不动（见 §4.3），但**新键不继承那个约定**。

### 4.2 不动（逐位）

- `divergence`（聚合均值）、`ece`、`ece:<producer>`、`identity_stability`：**值与定义逐位不变**。ECE 的分箱（10 箱）与聚合式（`|correct − conf_sum|` 求和 / 总数）不动；新增的分解键是**并行的另一套累加**，不改既有累加的顺序，因此既有键的浮点结果逐位不变。
- `slice16` 冻结的三个字面量（`kEceHonest` / `kEceInjected` / `kEceAggregate`）**必须继续逐位通过**。

### 4.3 `identity_stability` 为什么不动（逐条核对 problem-log 015 的三条建议）

| problem-log 015 的建议 | 本片处置 | 理由 |
|---|---|---|
| 加覆盖率指标 | **做** | §4.1 的 `coverage` / `coverage:<producer>` / `claims:<producer>` |
| divergence 按 producer 分解 | **做** | §4.1 的 `divergence:<producer>` / `divergence_n:<producer>` |
| 空集 ≠ 稳定（排除空-空配对，或只在有 track id 的数据上用） | **不做代码改动** | ① 改既有键 = 改 `slice12`/`slice16` 的冻结语义，违反 R1；② 该病态在**本面板不发生**——真实面板每 tick 都有匹配（E1 实测每非空 tick 匹配数 2.00–2.41），本面板的 `identity_stability = 0` 是**另一个原因**（每 tick 换场景、无 track id）；③ 空-空配对在"有真值有匹配"的面板上压根不出现。**真正的修复需要带 track id 的数据源**（许可问题见 D3 报告 §9 条目 5），属 Phase 4。本片的处置是**报告里写明该指标在本面板无语义、不得作为佐证** |

## 5. 判据 (c) 的测量协议与裁定规则

### 5.1 面板与配置（写死，测量照此执行）

- **面板**：D3 的 200 帧 COCO 面板（`data/panel/frames.txt` + `data/panel/truth.txt`，640×480，真值 = 真实标注，口径规则见 D3 报告 §8 附页，**本片沿用不重定义**）。
- **两个生产者**：`rtdetr_v2_s`（诚实侧：真实检测器，自报置信度）与 `blob_detect_overconfident`（注入侧：固定声称 0.99，`src/perception/blob_detect_overconfident.cpp`）。**两侧必须同时在环**，否则测的是"某个模型 vs 没有模型"。
- **世界模型**：主测量用 `--models ""`（**纯感知**，problem-log 016 的纪律：省略 `--models` ≠ 关闭）；另跑一次 `--models zero_delta`（D3 发布配置）**用于对照**，不用于裁定。
- **评分空间**：`--score-space observation`（主）与 `--score-space world`（对照）。
- **尺度分层**：全面板 + 两个子面板（§5.3）。

### 5.2 裁定规则（阈值不动：0.30）

- **主判据**：全面板 + 观察空间 + 纯感知配置下，
  `separation = | ece:rtdetr_v2_s − ece:blob_detect_overconfident |`，
  **≥ 0.30 → 达标**；否则 → **如实 PARTIAL** + 关闭路径（§5.5）。
- **必须同时报告的伴随量**（缺一不可，problem-log 015）：
  1. `coverage` / `coverage:<producer>`（分母侧的真相），
  2. `claims:<producer>`（两侧各自的样本量），
  3. `divergence` / `divergence:<producer>` / `divergence_n`（误差侧，且带分母），
  4. 工件里的**阶段键**（`detect:<name>` / `predict:<name>`）——实验条件的自证（problem-log 016）。
- **世界空间对照值必须一并报**：两个空间的分离量若相差超过像素量化上界（每轴 1 px 的归一化量），报告必须给出解释，不得只报好看的那个。
- **尺度分层必须一并报**（§5.3）：任一尺度层的分离量 < 0.30 时，主判据仍按全面板裁定，但报告**必须**写明"判别力在目标尺度上不均匀"——**不允许只报聚合值**（problem-log 005 的同族坑）。
- **不得**把"数值上有分离"直接读成"判据达标"（problem-log 012 的纪律）：报告必须同时回答"分离来自哪里"——诚实侧自身的 ECE 是不是也小？在本面板上"正确"是**纯位置**判定（D3 报告 §9 条目 4），所以诚实侧的 ECE 天然不为 0，这一条必须写在裁定旁边。
- **不把裁定写进测试**（problem-log 012）：`slice19` 只冻结面板参数与字面量，**不断言"分离量 ≥ 0.30"**，否则未达标的判据会在每次 CI 里显示为绿灯。

### 5.3 目标尺度分层（数据侧，零核心改动）

**为什么分层放在数据侧**：引擎的契约里**没有"目标大小"这个概念**——真值实体没有尺寸字段（渲染用的是全局 `footprint`），主张也没有尺寸字段。要在引擎里按目标尺度分层，就得给 `Entity` 或 `Proposal` 加字段——那会改 `world_hash` / `ledger_hash`（R1 禁止）。因此分层用**子面板**做。

**规则（确定性，写死）**：

1. 每帧的尺度 = 该帧**保留框**（非 crowd、可见面积 ≥ `kAnnotationMinArea`）的**归一化面积中位数**；面积 = `(u_max − u_min) × (v_max − v_min)`。
2. 阈值 = 全面板各帧尺度的**中位数**。
3. `small` = 帧尺度 ≤ 阈值；`large` = 帧尺度 > 阈值。**保留框为空的帧**（本面板 0 帧）不进任何层，报告写明数量。
4. 子面板 = 原面板帧的**子序列**（保持原顺序），真值清单的 `tick=` 块**重新编号为 1..N**（D3 规则：`tick` 必须从 1 起连续），块内容与头部（`dataset/license/width/height/categories`）逐字保留。
5. 工具：`tools/stratify_panel.mjs`（入库；产物在 gitignored 的 `data/` 下）。脚本必须**自证**它认定的保留框数与工件 `annotation_truth.boxes_kept` 一致，不一致就报错退出。

**为什么子面板是诚实的**：子面板里的每一帧，其真值、主张、匹配结果与全面板**逐字相同**（帧文件与标注块都没动），只是"跑哪些帧"变了。所以分层测量**不扰动匹配**，与"按真值过滤帧"的做法有本质区别（后者会让原本匹配的主张变成无匹配，是另一种污染）。

**必须写进报告的限制**：分层是**帧级**的（按该帧的典型目标尺度），不是逐目标的；两层是**不同的图像**，因此层间比较是**生态学**的（不同样本），不是配对的。层内的分离量是各自总体上的有效 ECE。

### 5.4 测量矩阵（每格一个工件，文件名带配置）

| # | 面板 | 生产者 | 世界模型 | 空间 | 用途 |
|---|---|---|---|---|---|
| M1 | 全 200 帧 | rtdetr + overconfident | `--models ""` | observation | **主判据** |
| M2 | 全 200 帧 | 同上 | `--models ""` | world | 空间对照（两空间分离量之差要报） |
| M3 | 全 200 帧 | 同上 | `--models zero_delta` | observation | 对照 D3 发布配置（验证"逐生产者指标与世界模型集合无关"这一**待验证**命题，不预设它成立） |
| M4 | small 子面板 | 同上 | `--models ""` | observation | 尺度分层（小） |
| M5 | small 子面板 | 同上 | `--models ""` | world | 小尺度的空间对照 |
| M6 | large 子面板 | 同上 | `--models ""` | observation | 尺度分层（大） |
| M7 | large 子面板 | 同上 | `--models ""` | world | 大尺度的空间对照 |

每个工件必须记录：命令行、环境元数据、阶段键、全部指标键。

### 5.5 若未达标的关闭路径（先写好，避免临场发挥）

如实 PARTIAL 时，报告必须给出一条**具体的**关闭路径（不是"以后再测"），候选按优先级：

1. **换目标尺度**（分层显示分离量随尺度塌陷）→ 需要带 track id 且许可可用的数据集（KITTI 类多为非商业，许可问题见 D1 note）→ 属 Phase 4 的数据侧立项。
2. **扩面板**（样本量不足导致分离量不稳）→ 扩到更多图 / 更多数据集，许可准入规则沿用 D3 的收紧口径。
3. **改判据的形式**（例如把"位置正确"换成"IoU 正确"）→ **需要新 ADR**（会动契约 FR5 的评分口径），本片不做。

## 6. 边界情形

| 情形 | 行为 |
|---|---|
| `--score-space` 未知值 | 具名错误（与 `--mode` / `--ep` 同族），列出可选值 |
| 未传 `--score-space` | 默认 `world`（R1） |
| 观察空间 + 相机分辨率非法（width/height ≤ 0） | 具名错误（`std::invalid_argument`，含"score space observation_uv"字样）：观察空间没有相机就没有投影可言。正常路径不可能发生（渲染器与帧源都保证正分辨率） |
| 真值或主张位置在 `[0,1]²` 之外 | R3：不具可比性——ECE 留在分母不给分，divergence 均值排除，`divergence_n` 可见其数 |
| 主张没有位置（`fields.transform` 缺省） | 与现状一致：不产生误差，不进 divergence 均值，留在 ECE 分母 |
| 账本为空 | `coverage = 0.0`、`divergence = 0.0`、`ece = 0.0`（既有语义），新增键按 §4.1 的空集约定 |
| `--score-space observation` + `--bounds wall` | **允许**：墙把位置钳在 `[margin, 1−margin] ⊂ [0,1]`，即视野内。注意默认（`--bounds none`）的合成面板真值会**漂出视野**（`WorldState::step()` 不钳制），因此**合成面板在观察空间下是退化面板**——本片的测量面板是真实面板，不受此影响；该事实写进报告 |

## 7. 测试计划（slice19，OFF / ON 两种构建都注册；夹具程序内生成，不碰数据集）

1. **两个空间的行为差异可断言**：
   - 真值 `(0.5, 0.5)`、相机 64×64、主张 `(0.505, 0.5)` → WorldXZ 误差 `0.005`；ObservationUV 误差 `0`（同一像素，量化把它抹平了）→ **两空间不等**，且方向是"观察空间更宽容"。
   - 真值 `x = 0.504`、主张 `x = 0.496` → WorldXZ `0.008`；ObservationUV `0.015625`（跨了像素边界，量化把它放大了）→ **两空间不等**，方向相反。两条一起断言，防止实现只对其中一个方向成立。
   - 两者完全重合时（误差 0）两空间相同。
2. **R3**：真值在 `[0,1]²` 之外 → WorldXZ 照常给误差；ObservationUV **不评分**：ECE 分母不减、正确性不加分；`divergence_n` 不含它。
3. **默认空间零变化**：`slice05` 的三个精确字面量（`divergence == 0.1875`、诚实组 `ece == 0.0`、注入组 `ece == 0.125`）在默认上下文下逐位不变；`slice11/12/16` 的既有断言全绿。
4. **冻结新键的 600/42 值**：`coverage` / `coverage:<producer>` / `claims:<producer>` / `divergence:<producer>` / `divergence_n` / `divergence_n:<producer>` 在 600/42 面板上的值冻结为字面量（实现时测得，写进 `slice19`，与 `slice16` 同族）——它们从此是这批新键的 oracle。
5. **覆盖率语义**：混合账本（匹配 + 宣布 + 拒绝）→ `coverage` 与 `coverage:<producer>` 的精确值；空账本 → `0.0`（不是 1.0）。
6. **逐生产者 divergence**：两个生产者的误差不同 → `divergence:<p>` 各自的值；`divergence_n:<p>` 是各自的分母。
7. **确定性成对**：同一账本在**每个空间**各喂两次 → 报告逐位相同；空间之间 → 指标不同但 **`world_hash` / `ledger_hash` 不变**（评分空间不写真值、不改账本——R5 的可断言形态）。
8. **CLI**：`--score-space world` / `observation` 解析正确；未知值 → 错误串；缺省 → `world`。
9. **工件**：`config` 节含 `"score_space"`；既有键的**值与相对顺序**不变（新增键是追加，不是替换）。
10. 全量：`build.bat --test`（OFF）+ ON 构建全量 ctest（AGENTS 硬规则 #4）。测试项数：OFF 17 → **18**，ON 19 → **20**。

## 8. 与票据验收对照

| 票据 AC | 落点 |
|---|---|
| `ScoreSpace` 类型 + `matched_error` 分派；容差语义不变（同一个 `kCorrectTol`） | §2.1 / §3.1 / §3.2 R4 |
| **加法式硬约束**：`slice11/12/16` 字面量与 600/42 基线逐位通过 | §3.2 R1 + §7 测试 3/4 + §9 偏差 1（工件不再是逐字节相同，如实登记） |
| 判据 (c) 重测：per-producer ECE 分离量实测 | §5.1 / §5.2 / §5.4（M1–M7） |
| **指标口径前置核对**：分离量与覆盖率一起报 | §4.1（`coverage` / `coverage:<producer>` / `claims:<producer>` / `divergence_n`）+ §5.2 伴随量清单 |
| **实验条件自证**：工件阶段键与实验设计一致 | §5.1（`--models ""`）+ §5.2 伴随量 4 + §7 测试 8/9 |
| 报告按目标尺度分层 | §5.3（数据侧子面板）+ §5.2（分层必须一并报） |
| 观察空间评分的适用边界写进代码注释与报告 | §3.3（末段）+ §9 偏差 4（代码注释落点） |
| slice19：两空间行为差异可断言 + 各自确定性成对；默认空间零变化 | §7 测试 1/2/3/7 |
| 判据 (c) 裁定：达标 → 更新 PLAN 与 L0 §2 ⑦；未达标 → 如实 PARTIAL + 关闭路径 | §5.2（裁定规则）+ §5.5（关闭路径）+ §9 偏差 5（收口动作） |

## 9. 偏差与扩展记录

1. **工件不再是"逐字节不变"，如实登记**：D3 建立的回归门是"默认工件逐字节不变"（工件里多一个节都不行）。本片新增 6 组指标键 + `config` 节一行 `score_space`，**默认工件必然改变**。仍然冻结的是：**600/42 的 `world_hash` / `ledger_hash`**、**`slice11/12/16` 的全部字面量**、既有指标键的**值与相对顺序**。工件级证据从"逐字节"降级为"**既有键逐项相同 + 新键纯追加**"——这是本片**主动**降低的一档门槛，理由与代价写在这里，不装作没发生。
2. **`ViewCamera` 从 `observation/` 搬到 `core/`**：纯搬移（ADR-0013 的"多接缝共享的值类型属于 core"先例）。动机是避免新增 `metrics → observation` 模块边（否则 metrics 会顺带依赖渲染器的头）。既有使用者无需改写（include 链不变）。
3. **`Metric::feed` 增加一个带默认实参的参数**：既有单测的调用点不动（少一次大面积改写），但虚函数签名变了 → `slice05` 里那个自定义 Metric 的覆写要改一行。默认实参的语义在头文件里写明（"默认 = WorldXZ，不需要相机"）。
4. **代码注释落点**：R2（必须经相机）、R3（视野外不评分，因为钳制会伪造正确）、R4（容差语义不变）、**适用边界**（只用于同一观察序列内的校准比较）四条，写在 `src/metrics/metrics.cpp` 的分派处与 `core/types.hpp` 的 `ScoreSpace` 定义处——**注释写"为什么"，不写"是什么"**。
5. **收口动作（裁定后执行）**：达标 → `PLAN.md` 阶段 D 行 + `L0 §2 ⑦` 追加说明（"置信度语义不变，变的是'正确'在哪个度量空间里判定"，ADR-0013 Consequences 第 4 条）+ 阶段 D regression 报告；未达标 → 如实 PARTIAL + 关闭路径（§5.5）+ 同样的报告。**两条路都要出报告**。
6. **本片不碰 GPU**：全程 CPU（ON 构建 + RT-DETR CPU EP）。CUDA/cuDNN 仍阻塞 D5，不受本片影响（L1 §2 条 9）。
7. **不引入任何新依赖**：分层脚本是 Node（与既有 tools 同族），引擎侧零新依赖。
