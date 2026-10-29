# L2 · 切片 D2「帧序列供给通路」详细设计

> **设计版本**：v1 · **批准方式**：owner 指示"继续完成接下来的任务，先把计划都实现了"（2026-10-26） · **批准日期**：2026-10-26
> 状态：**implementing**。本片**不引入任何外部依赖**——参考模式只吃已解码的 raw 帧序列（L1 §2 条 4）。
> 上承：[阶段 D L1](../概要设计.md) §3/§4 + [D1 note](../../../../docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)（数据集与许可结论）。票据：[02](../../../../.scratch/stage-d-real-data/issues/02-frame-source-supply-path.md)。

## 1. 程序描述

让**真实世界的帧**第一次成为 `Observation`。新增 `FrameSource` 生产者接缝，参考模式只实现"文件帧序列"一种；解码器实现留到 D5 且只在性能模式注册。两个现有真实模型适配器**一行不改**就要能吃真实帧——这是"家族无关"在数据维度的检验。

**本片的刻意局限（必须写在报告里）**：D2 只有"真实观察 + 合成真值"，两者**不对应**。所以本片的指标没有语义，`divergence`/`ECE` 在 D2 面板上不能作为任何主张——直到 D3 把标注装载成真值。

## 2. 数据结构

```cpp
// src/observation/frame_source.hpp
class FrameSource {                       // 生产者，不是适配器
 public:
  virtual ~FrameSource() = default;
  // 填 width/height/view/meta/sensor_kind/rgb；**不填 tick**——时间归主循环所有
  // （契约① tick 即版本）。流结束返回 false，不用异常控制流程。
  virtual bool next(Observation& out) = 0;
  virtual const char* name() const = 0;
};
std::unique_ptr<FrameSource> make_file_frame_source(const std::string& manifest_path);
```

- **清单格式（行式，零依赖）**：`key=value` 头部 + 每行一个帧文件路径。键：`width` / `height` / `rgb`（只接受 `rgb8`）/ `tick_start`（可选，默认 1）。`#` 开头为注释。
- **帧文件**：裸 RGB8，`width*height*3` 字节，行优先、左上原点——与 `Observation::rgb` 完全同构（`tools/png_to_rgb.mjs` 可直接产出）。
- `RunConfig` 新增 `std::string frames;`（空 = 合成世界；无默认值的纪律不变——默认在 `parse_config`）。
- CLI 新增 `--frames <manifest>`。

## 3. 关键机制

- **观察来源二选一，只在 setup 分叉一次**：`src = cfg.frames.empty() ? nullptr : make_file_frame_source(...)`；每 tick `src ? src->next(obs) : gen.render(...)`。核心只多两个分支点，与适配器选择同级。
- **时间归主循环**：`next()` 不写 `obs.tick`，由 `obs.tick = t` 统一赋值——避免"来源的时间"和"循环的时间"漂移（契约①）。
- **流长度即 tick 上限**：`cfg.ticks` 与帧数取小；流先结束时正常收尾（不是错误）。
- **确定性**：帧文件字节固定 → 观察逐位可复现；解码器输出永远不进哈希（那不是本片的路径，但要在文档里写死）。
- **元数据如实标注**：`sensor_kind = "rgb_video"`，`FrameMeta{backend:"cpu", device:"host", format:"RGB8", width, height}`。

## 4. 边界情形

| 情形 | 行为 |
|---|---|
| 清单缺失/不可读 | 构造时抛错（含路径），走 G6 的具名错误出口 |
| `width`/`height` 缺失或非正 | 构造时抛错，说明缺哪个键 |
| 帧文件大小 ≠ `w*h*3` | 读入时抛错（含实际字节数与期望） |
| 帧文件不存在 | 读入时抛错（含路径） |
| `rgb=` 非 `rgb8` | 构造时抛错（当前只支持 RGB8） |
| 帧数少于 `--ticks` | 流结束即收尾，`ticks_run` 如实反映 |
| `sensor_kind` 与合成世界混用 | 本片不混用：`--frames` 与合成世界互斥（同一 tick 只有一个来源） |

## 5. 测试计划（slice17，OFF/ON 两种构建都注册）

1. **读取行为**：程序内生成 3 帧临时序列 → 校验宽高、`rgb` 字节内容、`sensor_kind`、`tick` 由循环赋值。
2. **确定性配对**：同一序列跑两次 → 逐字段相等（problem-log 002 的成对纪律）。
3. **错误路径**：缺 `width`、帧文件尺寸不符、帧文件缺失 → 各自抛出具名错误（断言消息含关键信息）。
4. **入环**：`LoopDriver` 用帧序列跑（内建检测器）→ `ticks_run == 帧数`、`adapter_faults == 0`、时间字段有值；`sensor_kind` 如实为 `rgb_video`。
5. **不退化**：默认（无 `--frames`）路径的 600/42 基线哈希与 `slice11/12/16` 字面量逐位不变。
6. **适配器零改动**：本片不改 `perception/*`；`ow_slice13/14` 必须仍绿（ON 构建）。

## 6. 与票据验收对照

| 票据 AC | 落点 |
|---|---|
| `FrameSource` 接缝 + 自注册 | §2：**有意不做自注册**——只有一个实现，注册表属 Speculative Generality；改为工厂函数，第二个实现（解码器）出现时再抽注册 |
| `FileFrameSource` raw RGB8 + 清单 | §2 |
| `sensor_kind` / `FrameMeta` 如实标注 | §3 |
| 确定性证据 | §5 测试 2 |
| 现有两个真实模型适配器不改一行吃真实帧 | §5 测试 6 + 端到端（模型跑真实帧见 D4 面板） |
| slice17 行为 + 确定性成对 | §5 |
| git diff = 新接缝 + build/文书 | §7 偏差 1 记录的**核心改动**（driver 两个分支点） |

## 7. 偏差与扩展记录

1. **核心有改动，如实标注**：`RunConfig.frames` + `run()` 里的来源选择 = 2 处通用改动（与 C3 的 3 处同类：都不是 per-model 代码）。"单文件不变式"针对的是**适配器**，帧供给是新的生产者接缝，本片不可能零核心改动——不装作零。
2. **不做自注册**：L1 §4 草案写的是"与 PerceptionAdapter 同族"，但注册表的价值在于多实现按名解析；现在只有一个实现，工厂函数更小。等解码器实现出现再抽。
3. **`next()` 不写 tick**：L1 草案签名是 `next(Observation&)`，本片明确"tick 由循环赋值"，理由见 §3。
