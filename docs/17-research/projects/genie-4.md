# Genie 4 / Project Genie 项目卡

> Trust label: [Research] —— 一等来源（官方文档/官方仓库 README·LICENSE·公告/论文原文/官方 API 参考）；每条事实附 URL 与检索日期（2026-11-05）；无法核验的标 **UNVERIFIED**，不作为决策依据。
> 最后核验：2026-11-05 · 状态：首建
>
> 定位说明：全景报告 [`docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md`](../2026年实时世界运行时与生成式世界模型技术全景报告.md) 只写到 Genie 3（其 §"交互式神经渲染与动态可控演化：Google DeepMind Genie 3"），本卡是 Genie 4 的补充卡——Genie 3 的技术细节只链接不复制（纪律见 [`projects/README.md`](README.md) §5）。

## 0. 一句话

Google DeepMind 于 2026-10-27 公告的第四代交互式生成世界模型（研究预览）：文本+图像提示生成可实时导航、实时自然语言修改、含原生音频与对话的写实世界，Gemini 家族（Omni / 3.8 Flash / Argon）在环协同；**闭源、无公开权重、无公开 API**，当前仅 trusted testers + 候名单访问。

## 1. 是什么 + 官方能力声称

官方博文《Genie 4: A new generation of world simulation》发布于 **2026-10-27**（页面元数据 `article:published_time: 2026-10-27T16:13:00+00:00`；作者 Raia Hadsell、Diego Rivas、Hang Qi；最后修改 2026-10-30）。同日官方模型页上线，标题 "Genie 4"、描述 "Our most realistic and interactive world model yet"。

| 事实 | 来源 |
| --- | --- |
| 定位："our new world model, which unlocks unprecedented realism and improves real-time interactive simulation"；模型页："Use text and image prompts to generate photorealistic worlds you can explore, shape, and interact with in real-time" | deepmind.google/blog/genie-4-a-new-generation-of-world-simulation/ · deepmind.google/models/genie/ |
| 原生多模态："Genie 4 is natively multimodal, enabling audio-like music, ambient noises, and interactive dialogue to be simultaneously generated with the world" | deepmind.google/blog/genie-4-…/ |
| 实时交互控制面：官方展示视频均为"real-time interactions"，用户用 "standard keyboard controls for navigation and camera control, and text prompts to steer the scene as it evolves" | deepmind.google/blog/genie-4-…/ |
| **自然语言实时修改**："you can use natural language to modify a simulation in real-time — like changing the weather, moving the camera, or introducing a new person or object — and Gemini will contextually adapt your prompt to match the surrounding scene. Between your inputs, Gemini also monitors and steers the scene to keep the world progressing cohesively." | deepmind.google/blog/genie-4-…/ |
| **物理与动态人物**："Genie 4 simulates real-world physics scenarios, such as aerodynamics, collisions, and more."；"The most dramatic leap is how Genie 4 simulates people, creating characters that move naturally, respond organically to their environments, and can be interactively steered in real-time." | deepmind.google/blog/genie-4-…/ |
| **原生音频**："Genie 4 is our first real-time world model to generate audio natively."；环境音（"Every world carries the ambient sound of its own setting"）+ 实时交互对话（"interactive dialogue that adapts on the fly. Characters converse and react to your inputs or events in the world, all in real-time"） | deepmind.google/blog/genie-4-…/ |
| Gemini 家族协同："Gemini Omni anchors the simulations in real-world appearance and behavior, while other Gemini models provide the complex reasoning needed to help maintain a simulation's coherence and continuity" | deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/ |
| **Gemini 3.8 Flash in-loop**："Gemini 3.8 Flash serves as a guiding agent in the loop: as Genie 4 generates a world in real-time, Gemini continuously observes the scene, breaks down goals into a sequence of steps, and reviews incoming frames to track the progress and verify that each step succeeded. As the world shifts or the user intervenes, Gemini adapts its plan in the moment" | deepmind.google/blog/genie-4-…/ |
| 机器人方向：实验性组合 "Gemini 4 Argon"（多步任务分解，步骤以 text captions 流式注入 Genie 4 渲染）+ Gemini Robotics；演示为 Apptronik Apollo 人形机器人下棋；模型页称 "This unlocks an infinite playground for robotics simulations" | deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/ |
| 图像输入：参考图可作首帧（官方示例 prompt："Use the reference image as the exact first frame and visual basis for the scene"） | deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/ |
| 实测场景（trusted testers）：影视 previsualization "draft mode"（实时跑多个 take、现场调机位/灯光/台词）、新闻（单张档案照片秒级变环境仿真）、游戏原型（"from an initial idea to an explorable prototype in seconds… before writing a single line of engine code"）、企业 "zero-to-one" 场景生成 | deepmind.google/blog/genie-4-…/ |
| 安全与溯源：全部视频与音频输出含 SynthID 水印；上线前内部安全评估；"strict safety guardrails that are consistent with our safety policies across other models" | deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/ |
| 官方亲承混合路线方向："Future work: Towards hybrid world models — combine the logical, structured reasoning of code and language models with the rich, sensory representation of pixel-level simulation"；"Genie generates the immediate physical reality — rendering how the world looks, sounds, and responds, pixel by pixel" | deepmind.google/blog/genie-4-…/ |

## 2. 边界与限制

### 2.1 官方承认的限制（博文 "Understanding current limitations" + 模型页 Limitations 六条，原文照录要点）

1. **相干环境 / 视觉漂移**："visual details and object permanence can still shift or drift over time—especially as a simulation runs longer, after multiple complex prompt edits, or when navigating throughout the scene."
2. **多智能体交互**："Accurately modeling complex interactions between users, multiple independent agents, and shared environments is an ongoing research challenge."
3. **真实地点精度**："Genie 4 is currently unable to simulate real-world locations with perfect accuracy."
4. **文本渲染**："Clear and legible text is often only generated when it's in the input world description."
5. **交互时长**："The model can support a few minutes of continuous interaction, rather than extended sessions."
6. **音频准确度**："Real-time audio accuracy remains an area of ongoing research and improvement."

总体定性："Genie 4 remains an early, evolving area of research" / "it remains an early, experimental model"（博文）。

（来源均为 deepmind.google/blog/genie-4-a-new-generation-of-world-simulation/ 与 deepmind.google/models/genie/，检索 2026-11-05。）

### 2.2 我们的推断（标注为推断，非官方声称）

- **推断**：Genie 4 暴露的接口是"像素+音频流 + 键盘/文本控制"，官方无任何世界状态（几何/物理量/物体身份）导出的表述——其世界不可复现（自回归逐帧生成，"pixel by pixel"），与 OmniWorld 的 World Contract（identity / time / ownership / confidence，见 `docs/design/00-总体设计.md` §2）在"世界状态可测量"维度上不可互换。
- **推断**：官方漂移/时长限制恰是"生成式逐帧外推"的固有代价；Genie 3 博文已承认同一限制（"Limited interaction duration… a few minutes"），Genie 4 公告未宣称解决，只承认依旧。OmniWorld 的差异化空间正在于此：以持久结构化状态 + 置信度替代无界自回归外推。
- **推断**：官方**未公布** Genie 4 的分辨率/帧率/延迟数字（Genie 3 公告有明确 720p/24fps，Genie 4 无）——任何第三方引用的 Genie 4 性能数字都无官方依据，不得沿用 Genie 3 数字。

## 3. 许可（代码 / 权重 / 数据 三分）

| 维度 | 状态 | 依据 |
| --- | --- | --- |
| **代码** | 无开源代码。官方博文与模型页均无仓库/开源提及 | deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/（检索 2026-11-05） |
| **权重** | **无公开权重**。官方仅通过 Project Genie（网页体验）与 Gemini API / AI Studio 的 early access 提供；GitHub 与 Hugging Face 站内检索未发现官方权重仓库。Genie 系列历代（Genie 1/2/3）均无权重发布，Genie 4 延续 | 官方两页 + github.com / huggingface.co 站内检索（2026-11-05，检索性核验，非穷尽证明） |
| **数据** | 训练数据与配方完全未公开，无任何许可条款发布 | 官方两页无相关内容（检索 2026-11-05） |

补充：访问受 Google 账号/服务条款约束（waitlist 需 Google 账号登录）；SynthID 是内容溯源水印，**不是**许可条款。模型级使用许可（生成内容商用权等）**无公开文档** → **UNVERIFIED**。

## 4. 硬件要求

- **官方未发布任何本地推理硬件要求（显存/驱动/OS 均无官方来源）**：Genie 4 是 Google 云端托管服务——消费者侧通过 Project Genie 网页体验访问，开发者侧经 Gemini API / AI Studio 早期访问；客户端交互为浏览器 + 标准键盘（官方演示即网页内嵌实时交互）。来源：deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/，检索 2026-11-05。
- Genie 系列从未提供本地部署（Genie 3 为 limited research preview，来源：deepmind.google/blog/genie-3-a-new-frontier-for-world-models/，检索 2026-11-05）。
- 任何"本地运行 Genie 4 所需 GPU/显存"的说法都无官方依据 → **UNVERIFIED**（因为该需求本身不存在于官方文档，云端推理硬件规格 Google 未公布）。

## 5. 可集成接口

- **当前（2026-11-05）无公开 API**。官方口径分两段：(a) 现在："available as a research preview for trusted testers"；(b) 即将："will be coming soon starting with Google AI Ultra subscribers in Project Genie and early access partners of the Gemini API and Google AI Studio"。候名单入口在官方模型页 "Join Project Genie waitlist"。来源：deepmind.google/blog/genie-4-…/ · deepmind.google/models/genie/，检索 2026-11-05。
- **控制面**（预览内，官方描述的交互方式）：文本提示 + 图像提示（参考图作首帧）、标准键盘控制（导航/相机）、自然语言实时修改（Gemini 上下文适配 + 输入间隙自主 steering）、text caption 流式注入（Gemini 4 Argon 机器人演示的编程式雏形）。来源：同上。
- **导出 / 资产格式**：官方**零提及**（无 mesh / Gaussian / 视频下载 / 世界存档的任何说明）→ **UNVERIFIED**：不能假定存在任何导出能力。
- **对照**：与 World Labs World API（既有报告 §"空间智能与3D场景重建"）不同，Genie 4 截至 2026-11-05 **无公开编程接口**；所谓"Gemini API 集成"仅是对 early access partners 的预告，端点形态无文档（**UNVERIFIED**）。

## 6. 与 OmniWorld 的边界 + 族谱归宿

**归宿：竞品参照（主）+ 潜在生成后端适配器（②，远期、有条件）。**

理由：

1. **当前不存在适配器缝**：闭源、无权重、无公开 API、受邀访问（§3/§5）——与 NVIDIA Cosmos（开放权重 + 动作向量接口，见全景报告 §"NVIDIA Cosmos 3"）方向相反，不可能作为依赖或后端集成对象。指针材料对 Genie 3 的定位（"交互式自回归神经生成世界模型 / 前沿研究实验室模型"，全景报告对照表）由 Genie 4 延续并产品化（Project Genie / AI Ultra）。
2. **但它是最关键的竞品参照**：Genie 4 官方实测场景直接覆盖 OmniWorld 的目标域——agent/机器人训练环境、企业 "zero-to-one" 场景生成（§1），与"评测是第一个应用"（ADR-0008）同域竞争。它证明了实时生成世界的产品需求真实存在；同时其官方承认的短板（无世界状态导出、数分钟漂移、不可复现，§2）恰是 OmniWorld World Contract（identity/time/ownership/confidence）的差异化空间。**它强在"生成写实度"，弱在"可测量性"——OmniWorld 不与它比写实，只在可测量性上建立壁垒。**
3. **②适配器的触发路径**：若 Gemini API 公开 Genie 4（或后续代）端点且支持程序化动作/文本输入（不只键盘/人工 UI），OmniWorld 可把它当作**观察源**接入 Grounding Loop（视频流当观察、数据集真值当评分，与 `tools/coco_to_panel.mjs` 的真实数据入环路径同构）——即"生成后端适配器"：OmniWorld 提供世界契约与评分，Genie 提供像素。触发条件见 §7。
4. **明确排除**：不是③技能项目（无开源实现可研究）；不是⑤让渡对象（它不解决可测量世界状态问题）。**Do not copy**：其"世界=像素流"的世界观——OmniWorld 的世界状态必须可测量、可复现、可评分，这正是对 Genie 路线的反命题。
5. 与既有材料一致处：全景报告 §"神经生成与确定性运行时的双向融合演进机制" 论证的"生成式语义 + 确定性约束"混合架构，被 Genie 4 官方 "Future work: Towards hybrid world models" 一节独立印证（§1）——方向共识，但 Google 的混合是"Gemini 推理 + Genie 像素"，OmniWorld 的混合是"生成式感知 + 结构化状态"，出发点不同。

## 7. 未解决问题 + 重查触发条件

**未解决问题**（截至 2026-11-05）：

- Genie 4 的分辨率 / 帧率 / 延迟官方数字：公告未公布（**UNVERIFIED**，官方无来源；不可沿用 Genie 3 的 720p/24fps）。
- 交互时长具体上限：官方仅"a few minutes"，无数值（**UNVERIFIED**）。
- Gemini API / AI Studio 的 Genie 4 端点形态、定价、速率限制：无文档（**UNVERIFIED**）。
- 模型级使用许可条款（生成内容商用权、水印与使用政策细节）：无文档（**UNVERIFIED**）。
- 是否有论文/技术报告：截至检索日未发现官方论文（Genie 3 亦无论文先例；此为"未发现"，非否定性结论）。

**重查触发条件**：

1. Gemini API 文档（ai.google.dev）出现 Genie / world model 端点 → **立即重查 §5/§6，评估②适配器**。
2. Project Genie 向 AI Ultra 订阅者开放或候名单开放 → 重验实际交互 UI、会话时长与导出能力。
3. 官方公布分辨率/帧率/时长数字或技术报告 → 更新 §1/§2/§7。
4. DeepMind 发布 Genie 5 / 后续代 → 本卡追加日期小节（不删原文，README 纪律 4）。
5. 常规节奏：每季度重查一次（Genie 3→4 间隔约 15 个月，但发布节奏不可预测）。

## 8. 来源清单

检索日期统一为 **2026-11-05**。

| # | 来源 | URL | 用途 |
| --- | --- | --- | --- |
| 1 | Google DeepMind 官方博文《Genie 4: A new generation of world simulation》（2026-10-27，作者 Raia Hadsell、Diego Rivas、Hang Qi） | https://deepmind.google/blog/genie-4-a-new-generation-of-world-simulation/ | §0/§1/§2.1/§3/§4/§5/§6/§7 主要事实 |
| 2 | Google DeepMind 官方模型页《Genie 4》（Project Genie waitlist、六条结构化限制、Gemini 协同） | https://deepmind.google/models/genie/ | §1/§2.1/§3/§4/§5 |
| 3 | Google DeepMind 官方博文《Genie 3: A new frontier for world models》（2025-08-05，代际基线：720p/24fps/数分钟一致性、无原生音频、limited research preview） | https://deepmind.google/blog/genie-3-a-new-frontier-for-world-models/ | §0/§2.2/§4/§6 代际对照 |
| 4 | GitHub / Hugging Face 站内检索（关键词 "google-deepmind genie 4 world model weights repository"，限定 github.com、huggingface.co） | https://github.com · https://huggingface.co | §3 权重缺失的检索性核验（非穷尽证明） |
| 5 | 仓库内指针材料（既有结论引用，不复制）：全景报告 Genie 3 章节、third-party-research-map | `docs/17-research/2026年实时世界运行时与生成式世界模型技术全景报告.md`、`docs/17-research/third-party-research-map.md` | §6 定位一致性 |

**矛盾记录**：WebSearch 首次摘要曾显示 Genie 4 公告日期为 "Oct 27, 2025" 且链接指向 Genie 3 博文 URL——经直接抓取官方博文元数据（`article:published_time: 2026-10-27T16:13:00+00:00`）与模型页演示视频文件名时间戳（`261027`）双重核验，**确认发布日期为 2026-10-27**；搜索摘要错误，不采信。

> 深度报告（W04 深度层，2026-11-05）：[deep/2026-11-05-genie-4.md](deep/2026-11-05-genie-4.md)
