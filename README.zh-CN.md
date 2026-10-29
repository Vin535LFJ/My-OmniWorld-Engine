# OmniWorld Engine

[中文说明 / Chinese README](README.zh-CN.md)

**OmniWorld 是一个 GPU 原生世界引擎（C++20）：让世界数据——视频解码、AI 推理、观测渲染、世界状态更新——以最少实际拷贝、最少同步、最低延迟穿过 GPU 系统。** 感知模型、世界模型、仿真器、渲染器、智能体共享同一个可测量的"活世界状态"（identity / time / ownership / confidence 契约）；评测 harness 是引擎的第一个应用（ADR-0008）。

## 文档地图（从哪看起）

| 顺序 | 文档 | 作用 | 读完你知道 |
|---|---|---|---|
| ① 5 分钟 | 本 README | 项目是什么 + 文档地图 | 项目一句话 + 去哪找什么 |
| ② 15 分钟 | [`PLAN.md`](PLAN.md) | **唯一必读**：定位 / 目标函数 / 里程碑路线图与 Gate 体系 / 操作规则 | 项目要做成什么、规矩是什么 |
| ③ 1 分钟 | [`CURRENT.md`](CURRENT.md) | 现在做什么 + 出口条件 | 今天该打开哪个文件 |
| ④ 20 分钟 | [`docs/design/00-总体设计.md`](docs/design/00-总体设计.md) | **当前权威设计（已冻结）**：World Contract 契约语义 + 总体架构 + 阶段划分 | 契约怎么定的、系统长什么样 |
| ⑤ 按需 | [`docs/design/`](docs/design/) 各阶段目录 | 概要设计（L1）/ 切片详细设计（L2）/ [`problem-log.md`](docs/design/problem-log.md) 踩坑记录 | 某一块具体怎么设计、踩过什么坑 |
| ⑥ 干活时 | [`.scratch/stage-d-real-data/`](.scratch/stage-d-real-data/) | **任务票据**：spec + 切片票 01—06（当前活跃 feature；阶段 A/B/C 已归档在各自目录） | 现在干哪个任务、验收标准是什么 |
| ⑦ 按需 | [`docs/adr/`](docs/adr/) | 每个重大决策"为什么"：0008 方向重开 / 0009 双模式 / 0010 白纸重建 / 0011 后视镜 / 0012 ORT 作为可选依赖 | 决策的来龙去脉 |
| ⑧ 按需 | [`GLOSSARY.md`](GLOSSARY.md) · [`benchmarks/regression/`](benchmarks/regression/) | 术语表 · 性能主张的唯一证据 | 概念的精确定义 / 数字出处 |
| ⑨ 学习 / 面试时 | [`docs/onboarding-and-interview-prep.md`](docs/onboarding-and-interview-prep.md) · [`docs/onboarding-dataflow.html`](docs/onboarding-dataflow.html) · [`docs/dashboard.html`](docs/dashboard.html) | **掌握指南** + **可交互的数据流转页** + **生成式数据看板**（都可在浏览器直接打开；看板由 `node tools/build_dashboard.mjs` 从真实运行工件重建，没有手抄数字） | 怎么学、怎么讲这个项目 |

## 当前状态

> "现在做什么"的**单一事实源**是 [`CURRENT.md`](CURRENT.md)。本节只给一句话定位与对外可见的构建状态，不重复 CURRENT 的内容。

- **白纸共创重建**（ADR-0010）：旧实现已作废（`git tag pre-rebuild` 可查）。按"设计先行"流程重建——L0 总体设计（契约 9 条语义 + 架构 + 阶段表）与阶段 A / B / C 概要设计（L1）**均已冻结**；切片逐片交付。
- **阶段 A、B、C 均已完成**（核心引擎重建、双模式框架、AI 入环）。阶段 C 把两个真实预训练模型——RT-DETRv2-S（稀疏框）与 Depth-Anything-V2-Small（稠密深度）——送过同一条 `observe() → Proposal[]` 契约，C4 核心零新增行；期间参考模式的逐字节 oracle 与"默认构建零外部依赖"始终成立（[C3](benchmarks/regression/2026-10-19-stage-c-c3-rtdetrv2s.md) / [C4](benchmarks/regression/2026-10-19-stage-c-c4-depth-anything.md) / [阶段收口](benchmarks/regression/2026-10-19-stage-c-closeout.md)）。
- **阶段 D「真实数据入环」进行中**：D1 核验环境与每个新依赖的许可（[研究笔记](docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)），D2 让真实已解码帧成为观察源，D3 让**真实标注成为逐 tick 真值**——单写者与契约④⑤一字不动（`TruthWorld::load_annotations` + 行式清单 + 冻结的装载口径）（[L1](docs/design/stage-d-real-data/概要设计.md) / [D3 报告](benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md)）。200 帧真实 COCO + RT-DETRv2-S 的首批实测：`divergence = 0.0107`（就是检测器自己的定位误差）、`ece:rtdetr_v2_s = 0.1988`。**数据集字节一律不入库**：入库的是取数脚本、固定子集清单（image id + SHA256）与逐图许可记录，所以本阶段的"clone 即跑"多一步取数。
- **两条判据如实保持 PARTIAL**：① CUDA EP 延迟预算（本机缺 cuDNN 9 / CUDA 12.8+）；② 判据 (c)——真实模型**在合成世界里**坐不进"接近校准"席位，因为 COCO 对象与一平面彩色方块定义域不相交。阶段 D 的存在就是为了造出那个相交；在标注面板上测分离量是切片 D4 的事。
- 当前可构建代码：`runtime_probe`（硬件探针，独立）、`ow_core`（契约 / 真值世界 / 观察 / 信念账本 / 裁决 / 指标 / 容差 / 主循环）、`omniworld_loop`（闭环 CLI）、`ow_tests`（检查框架）。
- `build.bat --test` 注册 **17** 个测试（默认构建，零外部依赖）；`-DOW_WITH_ONNXRUNTIME=ON` 时注册 **19** 个（多出两片真实模型 + 冻结面板）。两种构建下参考模式基线哈希完全一致。
- ONNX Runtime 是**可选**依赖、默认 OFF——这个默认值就是"参考模式零外部依赖"承诺的**构建级证明**（ADR-0012）。
- 旧基线（重建对照）：Gate 1 三判据 1106.7 Hz / 适配器零核心改动 / ECE 分离 0.9617。
- 开发流程 = AI Hero 主流程：`grill-with-docs → to-spec → to-tickets → implement+tdd → code-review → retro`（技能映射见 ADR-0010）；日常操作手册见 [`docs/agents/workflow-manual.md`](docs/agents/workflow-manual.md)。

## 构建与运行

```bash
build.bat --test                # Windows 唯一可靠入口（MSVC 环境 + configure + build + ctest，17 个测试）
build\runtime_probe.exe --json  # 硬件能力探针（CPU / CUDA / Vulkan）
build\omniworld_loop.exe --ticks 600 --seed 42   # 闭环；--mode / --ep / --out 切双模式与落工件

# 真实数据入环（阶段 D）：真实帧当观察源、真实标注当真值
node tools/fetch_coco_subset.mjs && node tools/coco_to_panel.mjs   # 约 40 MB，写进 gitignored 的 data/
build\omniworld_loop.exe --frames data/panel/frames.txt --annotations data/panel/truth.txt --ticks 200
```

真实模型适配器需要显式开 ON 构建（`-DOW_WITH_ONNXRUNTIME=ON -DOW_ONNXRUNTIME_ROOT=...`，并 `node tools/fetch_models.mjs` 取模型）；默认构建完全不感知它们。命令与证据见 [ADR-0012](docs/adr/0012-onnxruntime-optional-dependency.md) 与上面的 C3 报告。

## 规则

- **设计先行**：任何代码切片动工前，必须有经 owner 批准的详细设计（L2）。
- 事实与主张分离；没有 benchmark 数据不宣称性能；方向/契约级决策落 ADR。
- 代码未跑通前不加新文档；新想法进研究队列，不进 sprint。

## 语言

`PLAN.md` / `CURRENT.md` / 设计文档中文优先（术语保留英文）；研究报告中文。
