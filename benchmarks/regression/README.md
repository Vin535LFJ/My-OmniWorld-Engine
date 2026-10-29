# regression Benchmarks

全部**性能与指标主张的唯一证据来源**（AGENTS.md §项目事实 5）。每份报告一个日期一个主张：结论必须能追到**某次实际执行的运行**，且报告里写明复现命令。

报告正文里的"复现命令"块是可执行入口；`artifacts/` 放随报告留存的原始工件。

| 日期 | 报告 | 主张 |
|---|---|---|
| 2026-09-18 | [runtime_probe 基线](2026-09-18-runtime-probe-baseline.md) | 硬件能力探针读数 |
| 2026-09-28 | [Grounding Loop v0 — Gate 1 裁定](2026-09-28-grounding-loop-v0.md) | 阶段 A 对齐目标 |
| 2026-09-28 | [Phase 1 — 世界模型入环](2026-09-28-phase1-world-models.md) | 世界模型接入 |
| 2026-10-14 | [Stage-A Gate-1 对齐](2026-10-14-stage-a-gate1-alignment.md) | 重建引擎 vs pre-rebuild 基线 |
| 2026-10-19 | [C3 — RT-DETRv2-S 入环](2026-10-19-stage-c-c3-rtdetrv2s.md) | 第一个真实检测模型 |
| 2026-10-19 | [C4 — Depth-Anything-V2-Small 入环](2026-10-19-stage-c-c4-depth-anything.md) | 第二个模型家族 |
| 2026-10-19 | [阶段 C 收口](2026-10-19-stage-c-closeout.md) | 判据 (c) 重测 + 延迟预算汇总 |
| 2026-10-26 | [D3 — 真实标注成为逐 tick 真值](2026-10-26-stage-d-d3-annotation-truth.md) | 数据集标注当真值 |
| 2026-10-27 | [D4 — 观察空间评分与判据 (c) 重测](2026-10-27-stage-d-d4-observation-space-scoring.md) | 判据 (c) 在真实面板上达标（0.305180 ≥ 0.30） |

## 写新报告的规矩

- 结论先行：先写**主张是什么**，再写它被什么证据支持或推翻；
- **没跑的检查不写成跑了**；被环境拦下的检查如实标注"未执行"并给替代证据；
- 数值必须带**分母与口径**（problem-log 015）：均值不带分母的报法一律不合格；
- 阈值改动要显式说明是"阈值不动"还是"阈值改了"；
- 复现命令必须能从干净工作区跑通（数据不入库时写清取数步骤）。
