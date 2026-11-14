# ADR-0017: WorldFoundry 差分——运行时层定位与贡献期权

Status: Accepted（owner grill 终裁 2026-11-14：Option C 双轨；与 ADR-0016 合并评审完成，Q4 按推荐）
Date: 2026-11-13（草案）/ 2026-11-14（Accepted）
Trust Label: [Architecture Decision — draft]

> 编号说明：活跃 ADR 至 0013；0014/0015 已归档（旧 planning-era）；0016 预留给方向重定义 ADR（见 research-queue）。本 ADR 取 0017。
> 触发：research-queue WorldFoundry 盯防条——"大更新发布后一周内产出差分 ADR"。更新已于 2026-11-07 发布（arXiv 2611.10771），截止 2026-11-20。
> 证据基线：[T8 评测生态知识基线](../17-research/knowledge-baseline/T8-evaluation-measurement-ecosystem.md)（arXiv 39 页 PDF 全文 + GitHub README + 官方文档站 5 页一手已核）。

## Context

WorldFoundry（OpenEnvision/NTU/Princeton，Apache-2.0）2026-11-07 大更新：270 模型条目 / 74 基准 / 62 注册指标，manifest 驱动执行 + 两级产物契约（GenerationRequest/Result + ArtifactRef URI/content hash）+ 三级 evidence scorecard（diagnostic / benchmark_comparable / exact_reproduction），128×A100/H100 80GB 验证。它是"基准之下的编排层"的最强占用者，与 OmniWorld"基准之下的运行时层"主张相邻。

**对上游结论的强制修正**：第二轮整合 note 曾写"其对比表自认没有系统完全支持闭环"。一手核验（PDF p.5 Table 1）表明 **WorldFoundry 自身行 Closed loop 标 ✓**，列定义为 "controls depend on prior outputs or environment feedback"；具身闭环路径真实存在（OpenVLA × LIBERO-10，但每任务仅 1 个记录 episode，179/520 步）。该表述作废，本 ADR 及后续引用一律改用下方"语义深度"口径。

## Problem

OmniWorld 的差异化主张（state-centric real-time loop vs model-centric batch orchestration）在 WorldFoundry 更新后是否仍成立？若成立，边界在哪；若不成立，是否转向为其生态贡献？

## 差分事实（全部一手已核，出处见 T8 §3a/§8）

| 维度 | WorldFoundry | OmniWorld | 差分性质 |
|---|---|---|---|
| 状态语义 | 无 canonical 状态契约；明文"保留模型原生语义"；state traces 仅为 ArtifactRef 产物类型 | 九条契约：身份铸造/单一 tick 域/单写者/Proposal 裁决/信念不回写真值/置信度一等公民 | **层差**：执行契约 vs 世界状态契约 |
| 闭环 | 有（控制依赖反馈）；具身 rollout + 交互会话；每任务 1 episode 的证据规模 | 仿真器=真值权威、引擎镜像 + 逐 tick 在环评分、真值/信念分离（BeliefView 为 Phase 4 首切片） | **语义深度差**：闭环存在 vs 闭环可测量/可归因 |
| 复现 | RunPlan 计划指纹 + 产物 content hash；不承诺执行位级确定 | 参考模式位级 oracle：同 seed 逐字节 episode（E1-5 world_hash/ledger_hash 实证） | **oracle 差**：计划级/产物级 vs 执行级 |
| 校准 | 无 ECE/置信度契约（仅集成 PAWBench 基准） | per-producer ECE 已实现，E1 验证为唯一可靠退化检出器（+47.8%） | 测量能力差 |
| 评分时机 | 产物中心、执行后离线评分 | 状态中心、逐 tick 在环评分 | 架构差 |
| 执行编排 | 强（manifest/五态就绪/六 surface/270 模型 runner 验证） | 弱（单机单循环，适配器机制已验证） | **对方强项，不与其竞争编排** |
| 实时 serving / 训练 | 均 future work（§6 原文） | 不训练（PLAN 红线）；实时帧预算是主张 | 双方都未兑现实时 serving |
| 硬件 | Linux + CUDA 12.8+ 统一环境 | Windows 本机 8GB 参考模式已证明，评测环零外部依赖 | 本机可行性差 |

**结论（事实层）**：WorldFoundry 统一了执行与证据，没统一世界状态语义；"canonical 状态 + 真值/信念分离 + 逐 tick 在环评分 + 位级复现 episode"在本轮核验后仍无人提供。运行时层缺口成立，但论据必须打**语义深度**，不能再打"闭环有无"。

## Options

| Option | Pros | Cons | Risks |
|---|---|---|---|
| A 维持自建运行时层，WF 作上游候选池/适配目标 | 缺口经一手核验仍成立；位级 oracle + per-producer ECE 是已有实证资产 | 需求侧仍是推断（09-21 负面证据义务）；对方演进接口已预留 | WF 补上状态语义+在环评分则窗口收窄 |
| B 转向为 WF 贡献闭环运行时后端 | 贡献接口已预留（callable policy adapter / OpenVLA worked example）；Gate 2 外部信号成本可能更低 | 放弃本体主张；Linux+CUDA 12.8+ 环境与本机 Windows 8GB 不匹配；贡献被收录不受控 | 对方 roadmap 不一定收录；技能阶梯收益缩水 |
| C 双轨：自建维持 + 低成本贡献探针 | 保留期权；探针（issue/PR 欢迎信号）本身是 Gate 2 证据 | 分散精力 | 探针无回应时须有明确止损 |

## Decision（owner 终裁 2026-11-14：Option C）

**C（双轨），以 A 为主轴**：

1. 定位措辞更新为："基准之下的**世界状态运行时**——canonical 状态契约 + 真值/信念分离 + 逐 tick 在环评分 + 位级复现 episode"。不再使用"对方无闭环"类表述。
2. WF 270 模型目录作为 8GB 入环标的筛选的候选池（消费其 catalog YAML：许可/硬件/接口三件）。
3. 贡献探针限于低成本信号收集（WF 社区对"闭环运行时后端/状态契约"类 issue 的 maintainer 回应），不投入实现，除非 grill 裁决转向 B。**探针止损（owner 2026-11-14 批准）：B' 首份报告产出后提 1 个 issue + 视回应至多投 1 个小 PR；maintainer 无回应或 roadmap 不收录即停，不再投入。**
4. 本 ADR 不改变 ADR-0008 引擎身份；与 ADR-0016（方向重定义）已于 2026-11-14 合并评审完毕（Q1=A）。

## Consequences

- research-queue WorldFoundry 盯防条的差分义务由本 ADR 草案履行；Accepted 需 owner 批准。
- 第二轮整合 note 的"对比表自认闭环无完全支持"表述已加修正附记；引用该 note 时须连修正一起读。
- 盯防触发器（任一出现即窗口收窄信号，重审本 ADR）：
  1. WF 契约族新增身份/所有权/置信度字段（盯 evaluation/api semver 与 catalog YAML）；
  2. WF 提供逐 tick 在环评分或真值/信念分离结构；
  3. WF serving engines 落地并承诺延迟预算/确定性执行（其 §6 已把 reproducibility 列为优化验收维度）；
  4. WF 训练扩展落地（versioned dataset manifests / checkpoint lineage）；
  5. 评测团队公开采用 WF 作闭环默认路径（Gate 2 Track A 证据被收割）；
  6. OpenWorldLib memories 模块演化为跨模型共享状态。
- 证伪条件（转向 B）：触发器 1+2 同时出现；或首战 B' 报告无法展示"只有 OmniWorld 能测的指标"（09-21 风险 #3 判据）。

## Benchmark / Evidence

- T8 知识基线 §3a（WF 精读：PDF 全文 + 官方文档 5 页）、§8（逐维对比/互补面/风险面）。
- 仓库 E1 实测（闭环 note §6–7）：覆盖率 -31% / per-producer ECE +47.8% / divergence 反降 / identity_stability 反升 → M1/M2/M3 定案；E1-5 位级哈希证据。
- 外部同向证据：WRBench（状态核缺失）、MemoBench（motion-gated）、WROP（生成器真值契约，CC BY-NC 4.0）。

## Follow-up

- ~~owner grill~~ ✅ 2026-11-14 终裁 Option C（合并评审，ADR-0016 Q4）；探针止损条款已入 Decision 第 3 条。
- WF GitHub 代码/catalog/issues 逐页核验（T8 §6 待核项 1）——贡献探针的前置。
- 8GB 入环标的核验（ForgeWM 优先，T2 §6）consumes WF catalog；与 ADR-0016 Follow-up 1 同一任务。