#pragma once
// LoopDriver — the orchestrator (L0 sec 3.2 sequence diagram is the authority
// for the order). It is the only component that knows every other one; all
// cross-module dependencies flow into it. Wall-clock latencies are diagnostic
// only (contract 3) and never enter any hash.
#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace ow {

// ADR-0009 mode switch (stage B slice B1): reference stays the byte-exact
// oracle; performance is the tolerance + latency-budget regime. v1 both run
// the same code path — the artifact labels the difference honestly.
enum class RunMode { Reference, Performance };
// ExecProvider lives in core/types.hpp (shared with the perception seam).

// Per-tick microsecond samples keyed by stage (raw evidence kept; p50/p95 is
// computed on top with the pre-rebuild runner's rank-linear interpolation).
using StageSamples = std::map<std::string, std::vector<double>>;

struct Percentiles {
  double p50 = 0.0;
  double p95 = 0.0;
};

struct StageLatency {
  double render_ms = 0.0;
  double perceive_ms = 0.0;
  double adjudicate_ms = 0.0;
  double metrics_ms = 0.0;
};

struct TickLog {
  Tick tick = 0;
  std::size_t applied = 0;    // Matched
  std::size_t announced = 0;  // NewEntity
  std::size_t rejected = 0;   // Rejected (with reason, in the ledger)
  std::size_t boundary_events = 0;  // contract 9 additive field (F1 grouping)
  StageLatency latency;
};

struct RunConfig {
  Tick ticks = 0;
  std::uint64_t seed = 0;
  int width = 0;   // NO semantic defaults here: the CLI parse layer fills all.
  int height = 0;
  float footprint = 0.0f;
  std::vector<std::string> perception_adapters;
  std::vector<std::string> world_models;
  // Stage D slice D2: empty = observations are rendered from the truth world;
  // non-empty = a frame-sequence manifest supplies them instead. The loop still
  // owns the tick (a source never sets it), and the two sources never mix.
  std::string frames;
  // Stage D slice D3: empty = truth is the seeded kinematic world; non-empty =
  // an annotation manifest supplies it (requires `frames`: annotated truth is
  // only meaningful against the matching real observation). Like the frame
  // stream, this one is loaded by TruthWorld -- the single writer is unchanged.
  std::string annotations;
  // Stage D slice D4: the space positional claims are judged in (ADR-0013).
  // No default: the CLI fills it, like mode / bounds / ep.
  ScoreSpace score_space;
  // The command line that produced this run. Provenance for the reader: the
  // artifact has to say how it was made, not only what it measured.
  std::string command_line;
  RunMode mode;        // no default member value; parse_config sets it
  BoundsPolicy bounds;  // no default member value; parse_config sets it
  ExecProvider ep;      // no default member value; parse_config sets it
};

struct EnvironmentMeta {
  std::string os;
  std::string arch;
  std::string compiler;
  std::string build;
  std::string git_commit;
  std::string cpp_standard;
  std::size_t cpu_logical_cores = 0;
};

struct RunReport {
  Tick ticks_run = 0;
  double total_seconds = 0.0;  // diagnostic
  double hz = 0.0;             // diagnostic (wall clock)
  std::map<std::string, double> metrics;
  std::uint64_t world_hash = 0;   // deterministic byte-level identity
  std::uint64_t ledger_hash = 0;  // deterministic
  std::size_t ledger_entries = 0;
  std::size_t applied = 0, announced = 0, rejected = 0;
  StageLatency latency_sum;
  std::size_t adapter_faults = 0;  // caught exceptions (isolation proof)
  // First caught fault as "<name>: <what>". The isolation layer swallows the
  // exception to keep ticks running, so this field is the only place the reason
  // survives (C3: a CUDA-EP-unavailable adapter otherwise reported faults=1
  // with no explanation anywhere in the artifact).
  std::string first_fault;
  RunMode mode = RunMode::Reference;
  ExecProvider ep = ExecProvider::CpuEp;
  BoundsPolicy bounds{BoundsPolicy::Kind::None, 0.0f};  // scene that produced it
  // Stage D slice D4: the space the positional claims were judged in. Provenance
  // for the reader: it never enters world_hash / ledger_hash.
  ScoreSpace score_space = ScoreSpace::WorldXZ;
  // The command line that produced this report (same provenance rules).
  std::string command_line;
  // B3: raw per-stage microsecond samples + percentiles. Diagnostic only:
  // they never enter world_hash / ledger_hash.
  StageSamples stage_samples;
  std::map<std::string, Percentiles> stage_percentiles;
  std::size_t boundary_events = 0;  // contract 9 aggregate
  // Stage D slice D3: annotation provenance and protocol counters. Set only
  // when truth was loaded from a manifest, and the artifact grows its
  // "annotation_truth" section only then -- the default artifact stays
  // byte-identical (the frozen stage A/B/C baselines are judged at byte level).
  // Provenance for the reader, never an input to any hash.
  bool annotations_loaded = false;
  std::string annotation_dataset;
  std::string annotation_license;
  std::size_t annotation_frames = 0;   // tick blocks the manifest declares
  std::size_t annotation_boxes = 0;    // boxes that survived the protocol
  std::size_t dropped_crowd = 0;       // rule 1
  std::size_t dropped_degenerate = 0;  // rule 2
  std::size_t dropped_over_cap = 0;    // rule 3
};

class LoopDriver {
 public:
  explicit LoopDriver(int scene_entities = 8) : scene_entities_(scene_entities) {}
  RunReport run(const RunConfig&);

 private:
  int scene_entities_;
};

// CLI parsing as a library function so it is testable: returns an error string
// for unknown options or bad values (fail-fast), or the parsed config.
// artifact_path is set by --out (empty = caller prints text only).
std::variant<RunConfig, std::string> parse_config(int argc, const char* const* argv,
                                                  std::string& artifact_path);

EnvironmentMeta collect_environment();

// Deterministic JSON serialization of the run report (no wall clock, no
// random fields): two calls on the same report are byte-identical.
std::string run_report_json(const RunReport&);

// Percentiles per stage (rank-linear interpolation, pre-rebuild runner
// 口径); empty samples report 0. Sorted by map key = deterministic.
std::map<std::string, Percentiles> percentiles(const StageSamples&);

// Flat "<stage>.p50"/"<stage>.p95" keys for latency-budget windows (the
// budget mount point consumed with the tolerance comparator).
std::map<std::string, double> latency_keys(const RunReport&);

}  // namespace ow
