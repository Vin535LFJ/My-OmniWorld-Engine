# W04 · 生态调研（ecosystem survey）

> **用途**：把"搜集全网相关项目、丰富背景知识、供以后参考"规格化成一个可重复的 loop，为方向评审、立项与 ADR 提供一手证据。
> **核心原则**：**调研为决策服务，不是无限收集。** 一个项目一张稳定的项目卡；每条事实可溯源；核验不了就标 UNVERIFIED——"seen" 与 "suspected" 必须分得开。
> **不重复权威**：定位与族谱归宿的权威在 [`PLAN.md`](../PLAN.md) §二（想法族谱）；研究主题队列在 [`docs/17-research/research-queue.md`](../docs/17-research/research-queue.md)；本规格只写**活动本身**。
> **当前状态**：规格完成；首轮 Tier 1（11 张卡）已按本规格执行（2026-11-05，产物在 [`docs/17-research/projects/`](../docs/17-research/projects/)）。

---

## Trigger（三合一）

1. **方向级评审前**——方向重开 / 产品定位需要一手证据时（先例：2026-11-05，Genie 4 之后的 A/B 抉择）。
2. **队列触发条件到期**——`research-queue.md` 某条目的 When/trigger 到期时（本规格吸收原"研究队列巡检"候选 loop）。
3. **新项目进入视野**——owner 输入或外部材料出现新名字，且它与族谱②③⑤可能相关。

## Checkpoint（1 个）

**种子清单批准点**——去重盘点后，把候选项目分成 Tier 1（决策关键）/ Tier 2（补全）报 owner，批准后才执行调研。产出落地后 owner 抽查 map diff 即可，无第二检查点。

## 输入

- `docs/17-research/third-party-research-map.md`（项目级权威索引）
- `docs/17-research/research-queue.md`（主题队列与触发条件）
- `docs/17-research/` 既有日期笔记（**去重对象**）
- owner 的方向问题清单（决策输入摘要要回答什么）

## 步骤

### 1. 状态核验（W00）

```bash
bash scripts/state-check.sh
```
同惯例：退出码 0，或非 0 项逐条记录。

### 2. 盘点去重

对照三个去处（map / queue / 已有笔记）给每个候选项目标注 **[已有 / 部分有 / 全新]**；只对缺口立项。
**引用而不复制**：已有笔记里的结论一律链接过去，不重述（同一事实两处存放必然漂移）。

### 3. 种子清单分层 → owner 批准（Checkpoint）

Tier 1 = 决策关键（评审要用）；Tier 2 = 补全（登记进 queue，触发再做）。

### 4. 逐项目调研 → 项目卡

工具无关：并行研究代理或串行皆可（`.agents/skills/research/` 口径：一等来源、每条 claim 可溯源、后台执行不阻塞对话）。
**项目卡固定结构**（Trust label 头 + 0–8 节）见 [`docs/17-research/projects/README.md`](../docs/17-research/projects/README.md)。

### 5. 落盘

`docs/17-research/projects/<slug>.md`，**稳定文件名**（不带日期——项目卡是活的参考件，日期只进卡内"最后核验"行）。
事实带 URL + 检索日期；核验不了标 **UNVERIFIED**；版本事件、许可变更用日期小节**追加**（先例：D1 note §6.1），不删原文。

### 6. 聚合

- `third-party-research-map.md` 加行（一行一项目，Boundary 列按卡 §6 填；已有行只补链接不重复）。
- `research-queue.md` 状态同步；Tier 2 登记为新行待触发。
- GLOSSARY 不加项目名条目（术语表收领域词汇，不收项目名）。

### 7. 巡检（吸收"研究队列巡检"候选）

触发条件到期、或卡内事实年龄 > 90 天的条目重核验；更正用日期小节追加，保留原文与更正的因果可见。

### 8. 汇报

本次 diff 摘要（新卡 / 更新行 / 更正）+ **决策输入摘要**：对照 owner 的问题逐条给证据，每条结论标注依据的卡与来源；UNVERIFIED 的事实只进"证据缺口"小节，不进结论；推荐倾向 + 置信度，注明裁决归 owner。

## 输出

- 项目卡 ×N（`docs/17-research/projects/`）
- `third-party-research-map.md` / `research-queue.md` 行级更新
- 决策输入摘要（带日期，落 `docs/17-research/`）

## 完成判据（Definition of Done）

- [ ] 每张卡有 Trust label 头；每条事实有 URL+检索日期或 UNVERIFIED 标注
- [ ] 每张卡对既有笔记只引用不复制（抽查可查）
- [ ] map 新行都链到卡；queue 状态与卡一致
- [ ] 决策输入摘要的每条结论标注依据；UNVERIFIED 不进结论
- [ ] 执行者读完无需再问任何问题（本规格的自足性标准）

## 反模式

| 反模式 | 为什么 |
|---|---|
| 把"GitHub 搜不到"当差异化证据 | 搜索盲区 ≠ 市场空白；要找的是"谁在做、做到哪"的一手证据 |
| 把二手转述当事实 | 博客/AI 转述会丢失限定条件；只有一等来源可引用 |
| 把"官方博客宣称"当"已实测" | 声称与可复现之间隔着整个工程；卡的 §2 必须把两者分开 |
| 用无限调研推迟决策 | sweep 的出口是"决策输入摘要"，不是"更全的清单" |
| 重建已有笔记 | 同一事实两处存放必然漂移；引用 |
| 卡片文件名带日期 | 项目卡是活的参考件；带日期的一次性报告放 `docs/17-research/` 顶层 |
