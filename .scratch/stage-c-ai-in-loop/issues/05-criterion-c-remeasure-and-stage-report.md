# 05: 判据 (c) 重测 + 阶段 C 收口报告（C5）

**What to build:** 用真实模型场景重测判据 (c)：至少一个"接近校准”的真模型 vs 夸大置信度模型的 per-producer ECE 分离量（vs 0.30 旧阈值）——如实裁定达标/未达标；产出阶段 C regression 报告（环境元数据 + 模式标注 + 延迟分项 + 模型清单）。

**Blocked by:** 03、04（双真模型在场才有面板）

**Status:** done — 2026-10-19（阶段 C 收口完成；证据见 [阶段 C 收口报告](../../../benchmarks/regression/2026-10-19-stage-c-closeout.md) 与 [C5 L2 偏差记录](../../../docs/design/stage-c-ai-in-loop/slices/05-criterion-c-remeasure-and-stage-report.md)）

**流程：** 预授权执行（同 C1 授权链）。

- [x] per-producer ECE 分离量实测（真模型 vs 夸大；对照 0.30 阈值）→ P0 分离 **0.18890**、P1（真模型 vs 注入者）分离 **0.23165**，两者均 < 0.30；且注入者始终是 ECE 最低的那个
- [x] regression 报告落 `benchmarks/regression/`（环境元数据 + mode + 延迟 p50/p95 + 模型清单与许可）→ `2026-10-19-stage-c-closeout.md`
- [x] 判据 (c) 裁定：达标 → 更新 PLAN/问题日志；未达标 → 如实 PARTIAL + 关闭路径 → **PARTIAL**，关闭条件重新定位到 Phase 3（需要模型定义域与真值域相交），problem-log 012
- [x] 阶段 C 出口对照（PLAN §六 Phase 2 行）逐条自查；CURRENT → 阶段收口/下一阶段 → 五条：三达成、两 PARTIAL（缺 cuDNN / 缺可比真值）
- [x] **CUDA EP 容差窗口 + 延迟预算标定** → **保持 PARTIAL**（本机缺 cuDNN 9 / CUDA 12.8+，owner 确认此裁定）；CPU EP 侧完整交付并汇总于报告 §3
- [x] **判据 (c) 面板口径先定** → P2 深度生产者**排除**（实测 `ece:depth=0.6470`、`identity_stability=0`），真实模型席位以 RT-DETR 为准且仍不可解释
- [x] **owner 裁定项：CLI 错误语义** → 两项全采推荐并已实现 + 实测：G6 setup 异常 → 具名错误 + exit 1（原为 `0xC0000409` 静默 fastfail）；G7 性能模式 `faults>0` → exit 1（参考模式语义不变，工件照常写出）

## Comments

- 2026-10-19 · **TDD**：`slice16` 冻结 P0 面板（600 ticks / seed 42 / 64² / footprint 0.02 / reference）→ 字面量 `ece:blob_detect=0.19890412565780013`、`ece:blob_detect_overconfident=0.009999990463256836`、聚合 `0.4940744921482492`、`entries=9746`、基线哈希 `574361e62eec92b9`/`459ff85132cc103e`。**故意不断言"分离量 < 0.30"**——未达标的判据不能伪装成绿灯（L2 §6 测试 3）。
- 2026-10-19 · **判据 (c) 裁定 = PARTIAL（前提不成立，非进度不足）**：P1 真模型 vs 注入者分离量 **0.23165 < 0.30**；注入者恒为最低 ECE（0.010）；同一真模型跨面板 ECE 漂 **0.24–0.49**（5/42 → 0.3898、3/600 → 0.4866、20/42 → 0.2417）→ 它量的是偶然命中率，不是校准。关闭条件重新定位 Phase 3。→ problem-log 012。
- 2026-10-19 · **G6/G7 实现 + 四条实测**：`apps/omniworld_loop.cpp` 加 setup try/catch（`error: unknown perception adapter: does_not_exist`，**exit 1**，修前为 `0xC0000409` 零输出无工件）；性能模式 `faults>0` → **exit 1** 且工件仍写出（实测 CUDA EP 场景）；参考模式同场景仍 **exit 0**（语义未动）；无故障 performance → exit 0。
- 2026-10-19 · **回归门**：OFF 构建 **15/15**、ON 构建 **17/17**（新增 ow_slice16；ON 侧含 slice13/14/16）。
- 2026-10-19 · **环境事故（非代码）**：`build-ort-rel/` 被 Windows Search 索引器咬住，`mt.exe` 清单嵌入报 `c101008d` / `0x1f` 共享冲突；换全新目录同样发生、只是换目标文件，**重试第一次即通过**。本轮 ON 数字来自 `build-ort-c5/`；`build-ort*/` 合并为一条 ignore 规则；索引器排除属机器配置，登记为遗留 L5。
- 2026-10-19 · **SOP-7 收口动作**：收口报告落库；problem-log 012 新增（008 追加复发证据）；`interview-narrative.md` 新增**阶段 C** 5 条 STAR（**待 owner 用自己的话确认**）；PLAN 阶段 C 行 → 完成（含两条 PARTIAL）；README 中英 + onboarding 指南同步；`CURRENT.md` → 指向下一阶段。
- 2026-10-19 · 新坑：1 条（problem-log 012）。
