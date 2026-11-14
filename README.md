# OmniWorld Engine

[中文说明 / Chinese README](README.zh-CN.md)

**OmniWorld is a measurable world-state runtime (C++20): one canonical world state + World Contract (identity / time / ownership / confidence), where any world producer — perception models, generative world models, procedural generators, reconstruction models, physics simulators, renderers, agents — plugs in through the same contract, and the engine owns identity, time, ownership, confidence, and correctness measurement (per-tick in-loop scoring, byte-reproducible episodes).** It is "the runtime beneath the benchmarks" (ADR-0016). Its engineering discipline stays GPU-native (ADR-0008): moving video decode, AI inference, observation rendering, and world-state updates through GPU systems with the least practical copies, synchronization, and latency. The evaluation harness is the engine's first application.

## Document map (where to start)

| Step | Document | Role | After reading you know |
|---|---|---|---|
| ① 5 min | This README | What the project is + where everything lives | The one-liner + navigation |
| ② 15 min | [`PLAN.md`](PLAN.md) | **The one must-read**: identity / goal function / milestone roadmap & Gate system / operating rules | What we're building and the rules |
| ③ 1 min | [`CURRENT.md`](CURRENT.md) | The single current focus + exit criteria | What to open today |
| ④ 20 min | [`docs/design/00-总体设计.md`](docs/design/00-总体设计.md) | **Current authoritative design (frozen)**: World Contract semantics + overall architecture + phase plan | How the contract was decided, what the system looks like |
| ⑤ as needed | [`docs/design/`](docs/design/) stage folders | Stage designs (L1) / slice detailed designs (L2) / [`problem-log.md`](docs/design/problem-log.md) | How a given piece is designed, what broke |
| ⑥ when working | [`.scratch/stage-d-real-data/`](.scratch/stage-d-real-data/) | **Task tickets**: spec + slice tickets 01—06 (current active feature; stages A/B/C are archived in their own folders) | Which task is live, what its acceptance criteria are |
| ⑦ as needed | [`docs/adr/`](docs/adr/) | The "why" of every major decision: 0008 direction reopen / 0009 dual mode / 0010 blank-slate rebuild / 0011 rear-mirror / 0012 ONNX Runtime as an optional dependency | Decision history |
| ⑧ as needed | [`GLOSSARY.md`](GLOSSARY.md) · [`benchmarks/regression/`](benchmarks/regression/) | Glossary · the only evidence for performance claims | Precise definitions / where numbers come from |
| ⑨ when studying | [`docs/onboarding-and-interview-prep.md`](docs/onboarding-and-interview-prep.md) · [`docs/onboarding-dataflow.html`](docs/onboarding-dataflow.html) · [`docs/dashboard.html`](docs/dashboard.html) | **Study guide** + an **interactive data-flow page** + a **generated data dashboard** (both open in any browser; the dashboard is rebuilt from real run artifacts by `node tools/build_dashboard.mjs`, no hand-typed numbers) | How to learn this project and present it |

## Current state

> The single source of truth for "what we do now" is [`CURRENT.md`](CURRENT.md). This section only gives a one-line orientation plus the externally visible build state — it does not duplicate CURRENT.

- **Blank-slate co-designed rebuild** (ADR-0010): the old implementation is abandoned (see `git tag pre-rebuild`). Design-first throughout — L0 overall design plus stage A / B / C L1 high-level designs are all **frozen**; slices deliver one by one.
- **Stages A, B and C are complete** (core engine rebuild, dual-mode framework, AI in the loop). Stage C put two real pretrained models -- RT-DETRv2-S (sparse boxes) and Depth-Anything-V2-Small (dense depth) -- through the same `observe() → Proposal[]` contract with zero new core lines in C4, while the reference mode's byte-exact oracle and its zero-dependency default build held throughout ([C3](benchmarks/regression/2026-10-19-stage-c-c3-rtdetrv2s.md) / [C4](benchmarks/regression/2026-10-19-stage-c-c4-depth-anything.md) / [stage close-out](benchmarks/regression/2026-10-19-stage-c-closeout.md)).
- **Stage D (real data in the loop) is in progress**: D1 verified the environment and every new dependency's licence ([note](docs/17-research/2026-10-26-stage-d-environment-and-licenses.md)), D2 made real decoded frames an observation source, and D3 made **real annotations the per-tick truth** while the single writer and contracts 4/5 stayed untouched -- `TruthWorld::load_annotations` + a line manifest + a frozen loading protocol ([L1](docs/design/stage-d-real-data/概要设计.md) / [D3 report](benchmarks/regression/2026-10-26-stage-d-d3-annotation-truth.md)). D4 then **closed criterion (c)**: an additive `ScoreSpace { WorldXZ, ObservationUV }` puts the comparison inside the observation's own normalized coordinates (default stays world x/z, so every frozen baseline is bit-for-bit unchanged), and on the 200 real COCO frames the per-producer ECE separation between the honest detector and an injected-overconfidence producer measured **0.3052 >= 0.30** -- with the caveat that the separation is **not uniform across target scale** (it collapses to 0.2202 on large targets) ([D4 report](benchmarks/regression/2026-10-27-stage-d-d4-observation-space-scoring.md)). **Dataset bytes never enter git**: the repo carries the fetch script, the pinned subset (image ids + SHA256) and the per-image licence record, so "clone and run" now has a fetch step for this stage.
- **One criterion remains honestly PARTIAL**: the CUDA EP latency budget (this machine lacks cuDNN 9 / CUDA 12.8+). Criterion (c) is closed on the real annotated panel; what is still open is the GPU data path (slice D5) and, on the measurement side, the large-target stratum where the separation falls below the threshold.
- Buildable code today: `runtime_probe` (standalone hardware probe), `ow_core` (contract / truth world / observation / belief / adjudication / metrics / tolerance / loop), `omniworld_loop` (the grounding loop CLI), `ow_tests` (the check harness).
- `build.bat --test` registers **18** tests (default build, zero external dependencies). With `-DOW_WITH_ONNXRUNTIME=ON` it registers **20** (adds the two real-model slices). Reference-mode baseline hashes are identical in both builds.
- ONNX Runtime is an **optional** dependency, OFF by default — that default is the build-level proof of the reference mode's zero-dependency promise (ADR-0012).
- Pre-rebuild baseline (alignment target): Gate 1 — 1106.7 Hz / adapter integration with zero core changes / ECE separation 0.9617.
- Dev process = the AI Hero main flow: `grill-with-docs → to-spec → to-tickets → implement+tdd → code-review → retro` (skill mapping in ADR-0010). Day-to-day operating manual: [`docs/agents/workflow-manual.md`](docs/agents/workflow-manual.md).

## Build & run

```bash
build.bat --test                    # Windows: the only reliable entry (MSVC env + configure + build + ctest, 18 tests)
build\runtime_probe.exe --json      # hardware capability probe (CPU / CUDA / Vulkan)
build\omniworld_loop.exe --ticks 600 --seed 42   # the grounding loop; --mode/--ep/--score-space/--out for dual mode + artifacts

# real data in the loop (stage D): decoded frames as observations, annotations as truth
node tools/fetch_coco_subset.mjs && node tools/coco_to_panel.mjs   # ~40 MB, written to gitignored data/
node tools/stratify_panel.mjs --artifact <json>   # split the panel by target scale
build\omniworld_loop.exe --frames data/panel/frames.txt --annotations data/panel/truth.txt --ticks 200
```

Real-model adapters need an explicit ON build (`-DOW_WITH_ONNXRUNTIME=ON -DOW_ONNXRUNTIME_ROOT=...` plus `node tools/fetch_models.mjs`); the default build never sees them. Commands and evidence: [ADR-0012](docs/adr/0012-onnxruntime-optional-dependency.md) and the C3 report above.

## Rules

- **Design first**: no code slice starts without an owner-approved detailed design (L2).
- Facts vs claims; no performance claims without benchmark data; direction/contract-level decisions land as ADRs.
- No new documents before code runs; new ideas go to the research queue, never into a sprint.
