#include "loop/driver.hpp"

#include "adjudication/adjudication.hpp"
#include "belief/ledger.hpp"
#include "core/hash.hpp"
#include "metrics/metrics.hpp"
#include "observation/frame_source.hpp"
#include "observation/observation.hpp"
#include "perception/adapter.hpp"
#include "truth_world/annotation.hpp"
#include "truth_world/truth_world.hpp"
#include "world_model/world_model.hpp"
#include "world_state/world_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifndef OW_GIT_COMMIT
#define OW_GIT_COMMIT "unknown"
#endif

namespace ow {
namespace {

using SteadyClock = std::chrono::steady_clock;

std::uint64_t hash_world(const WorldState& ws) {
  Hasher h;
  h.value(ws.tick());
  for (const Entity& e : ws.entities()) {
    h.value(e.id);
    h.bytes(e.class_tag.value.data(), e.class_tag.value.size());
    h.value(e.transform.position);
    h.value(e.transform.orientation);
    h.value(e.velocity);
  }
  return h.result();
}

std::uint64_t hash_ledger(const BeliefLedger& l) {
  Hasher h;
  for (const BeliefEntry& e : l.all()) {
    h.bytes(e.proposal.producer.data(), e.proposal.producer.size());
    h.value(e.proposal.tick_observed);
    h.value(static_cast<std::uint8_t>(e.proposal.target.index()));
    if (const auto* id = std::get_if<EntityId>(&e.proposal.target)) h.value(*id);
    h.value(e.proposal.fields.transform.has_value());
    if (e.proposal.fields.transform) {
      h.value(e.proposal.fields.transform->position);
    }
    h.value(e.proposal.confidence);
    h.value(static_cast<std::uint8_t>(e.verdict.kind));
    h.value(e.verdict.matched);
    h.bytes(e.verdict.reason.data(), e.verdict.reason.size());
    h.value(e.recorded_at);
  }
  return h.result();
}

// The isolation layer keeps ticks running past an adapter fault; a bare
// counter is not diagnosable, so the first fault also carries its reason.
void record_fault(RunReport& rep, const char* who, const std::string& what) {
  ++rep.adapter_faults;
  if (rep.first_fault.empty()) {
    rep.first_fault = std::string(who) + ": " + what;
  }
}

std::string quote(std::string s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
  return out;
}

std::string to_hex(std::uint64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return {buf};
}

std::string num(int v) { return std::to_string(v); }

std::string num(std::uint64_t v) { return std::to_string(v); }

std::string num(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return {buf};
}

std::string list_of_strings(const std::vector<std::string>& xs) {
  std::string out = "[";
  for (std::size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) out += ", ";
    out += quote(xs[i]);
  }
  out += "]";
  return out;
}

std::string mode_name(RunMode m) {
  return m == RunMode::Reference ? "reference" : "performance";
}

std::string score_space_name(ScoreSpace s) {
  return s == ScoreSpace::ObservationUV ? "observation" : "world";
}

}  // namespace

EnvironmentMeta collect_environment() {
  EnvironmentMeta e;
#if defined(_WIN32)
  e.os = "windows";
#elif defined(__linux__)
  e.os = "linux";
#else
  e.os = "other";
#endif
#if defined(_M_X64) || defined(__x86_64__)
  e.arch = "x64";
#else
  e.arch = "other";
#endif
#if defined(_MSC_VER)
  e.compiler = "MSVC " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
  e.compiler = "GCC " + std::to_string(__GNUC__);
#else
  e.compiler = "unknown";
#endif
#if defined(_DEBUG)
  e.build = "debug";
#elif defined(NDEBUG)
  e.build = "release";
#else
  e.build = "unspecified";
#endif
  e.git_commit = OW_GIT_COMMIT;
  {
    // _MSVC_LANG is truthful under /std flags; __cplusplus needs /Zc:__cplusplus.
#if defined(_MSVC_LANG)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%dLL", static_cast<int>(_MSVC_LANG));
    e.cpp_standard = buf;
#else
    e.cpp_standard = std::to_string(static_cast<int>(__cplusplus)) + "LL";
#endif
  }
#if defined(_WIN32)
  e.cpu_logical_cores = std::thread::hardware_concurrency();
#else
  e.cpu_logical_cores = 0;  // unknown on this platform; labeled by 0
#endif
  return e;
}

std::string run_report_json(const RunReport& r) {
  const EnvironmentMeta env = collect_environment();
  std::string out;
  out += "{\n";
  out += "  \"config\": {\n";
  out += "    \"mode\": " + quote(mode_name(r.mode)) + ",\n";
  out += "    \"bounds\": " +
         quote(r.bounds.kind == BoundsPolicy::Kind::Wall ? "wall" : "none") +
         ",\n";
  out += "    \"bounds_margin\": " +
         num(static_cast<double>(r.bounds.margin)) + ",\n";
  out += "    \"ep\": " +
         quote(r.ep == ExecProvider::CudaEp ? std::string("cuda")
                                            : std::string("cpu")) + ",\n";
  out += "    \"score_space\": " + quote(score_space_name(r.score_space)) + ",\n";
  out += "    \"command_line\": " + quote(r.command_line) + "\n";
  out += "  },\n";
  out += "  \"environment\": {\n";
  out += "    \"os\": " + quote(env.os) + ",\n";
  out += "    \"arch\": " + quote(env.arch) + ",\n";
  out += "    \"compiler\": " + quote(env.compiler) + ",\n";
  out += "    \"build\": " + quote(env.build) + ",\n";
  out += "    \"git_commit\": " + quote(env.git_commit) + ",\n";
  out += "    \"cpp_standard\": " + quote(env.cpp_standard) + ",\n";
  out += "    \"cpu_logical_cores\": " + num(env.cpu_logical_cores) + "\n";
  out += "  },\n";
  out += "  \"metrics\": {";
  bool first = true;
  for (const auto& [k, v] : r.metrics) {
    out += (first ? "\n" : ",\n");
    first = false;
    out += "    " + quote(k) + ": " + num(v);
  }
  out += (first ? "" : "\n  ") + std::string("},\n");
  out += "  \"stage_percentiles\": {";
  first = true;
  for (const auto& [stage, p] : r.stage_percentiles) {
    out += (first ? "\n" : ",\n");
    first = false;
    out += "    " + quote(stage) + ": {\"p50\": " + num(p.p50) +
           ", \"p95\": " + num(p.p95) + "}";
  }
  out += (first ? "" : "\n  ") + std::string("},\n");
  out += "  \"world_hash\": " + quote(to_hex(r.world_hash)) + ",\n";
  out += "  \"ledger_hash\": " + quote(to_hex(r.ledger_hash)) + ",\n";
  out += "  \"ledger_entries\": " + num(r.ledger_entries) + ",\n";
  out += "  \"applied\": " + num(r.applied) + ",\n";
  out += "  \"announced\": " + num(r.announced) + ",\n";
  out += "  \"rejected\": " + num(r.rejected) + ",\n";
  out += "  \"adapter_faults\": " + num(r.adapter_faults) + ",\n";
  out += "  \"first_fault\": " + quote(r.first_fault) + ",\n";
  // Emitted only for an annotated-truth run: the default artifact keeps its
  // exact byte layout, so the frozen stage A/B/C baselines stay comparable.
  if (r.annotations_loaded) {
    out += "  \"annotation_truth\": {\n";
    out += "    \"dataset\": " + quote(r.annotation_dataset) + ",\n";
    out += "    \"license\": " + quote(r.annotation_license) + ",\n";
    out += "    \"frames\": " + num(r.annotation_frames) + ",\n";
    out += "    \"boxes_kept\": " + num(r.annotation_boxes) + ",\n";
    out += "    \"dropped_crowd\": " + num(r.dropped_crowd) + ",\n";
    out += "    \"dropped_degenerate\": " + num(r.dropped_degenerate) + ",\n";
    out += "    \"dropped_over_cap\": " + num(r.dropped_over_cap) + "\n";
    out += "  },\n";
  }
  out += "  \"boundary_events\": " + num(r.boundary_events) + ",\n";
  out += "  \"mode\": " + quote(mode_name(r.mode)) + ",\n";
  out += "  \"acceleration\": " +
         quote(r.mode == RunMode::Performance
                   ? std::string("none (v1: no GPU components yet)")
                   : std::string("none")) + "\n";
  out += "}\n";
  return out;
}

std::variant<RunConfig, std::string> parse_config(int argc, const char* const* argv,
                                                  std::string& artifact_path) {
  RunConfig cfg{};
  artifact_path.clear();
  // CLI-layer defaults: the ONLY place defaults are allowed (L1 sec 6).
  cfg.ticks = 2000;
  cfg.seed = 7;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.02f};
  cfg.ep = ExecProvider::CpuEp;  // CLI default: reference semantics (Round 2 #6)
  cfg.frames.clear();            // empty = observations come from the truth world
  cfg.annotations.clear();       // empty = truth comes from the seeded world
  cfg.score_space = ScoreSpace::WorldXZ;  // ADR-0013: the frozen oracle path
  // The artifact records how it was made, not only what it measured.
  for (int i = 0; i < argc; ++i) {
    if (i > 0) cfg.command_line += ' ';
    const std::string a{argv[i]};
    cfg.command_line += (a.find(' ') == std::string::npos) ? a : ("\"" + a + "\"");
  }

  auto need_value = [&](int& i) -> const char* {
    if (i + 1 >= argc) return nullptr;
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ticks") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--ticks needs a value"};
      cfg.ticks = static_cast<Tick>(std::strtoull(v, nullptr, 10));
    } else if (a == "--seed") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--seed needs a value"};
      cfg.seed = std::strtoull(v, nullptr, 10);
    } else if (a == "--width") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--width needs a value"};
      cfg.width = std::atoi(v);
    } else if (a == "--height") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--height needs a value"};
      cfg.height = std::atoi(v);
    } else if (a == "--footprint") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--footprint needs a value"};
      cfg.footprint = static_cast<float>(std::atof(v));
    } else if (a == "--adapters") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--adapters needs a value"};
      cfg.perception_adapters.clear();
      std::string cur;
      for (const char* p = v; *p != '\0'; ++p) {
        if (*p == ',') {
          if (!cur.empty()) cfg.perception_adapters.push_back(cur);
          cur.clear();
        } else {
          cur.push_back(*p);
        }
      }
      if (!cur.empty()) cfg.perception_adapters.push_back(cur);
    } else if (a == "--models") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--models needs a value"};
      cfg.world_models.clear();
      std::string cur;
      for (const char* p = v; *p != '\0'; ++p) {
        if (*p == ',') {
          if (!cur.empty()) cfg.world_models.push_back(cur);
          cur.clear();
        } else {
          cur.push_back(*p);
        }
      }
      if (!cur.empty()) cfg.world_models.push_back(cur);
    } else if (a == "--mode") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--mode needs a value"};
      const std::string m{v};
      if (m == "reference") {
        cfg.mode = RunMode::Reference;
      } else if (m == "performance") {
        cfg.mode = RunMode::Performance;
      } else {
        return std::string{"unknown --mode '" + m + "' (reference|performance)"};
      }
    } else if (a == "--bounds") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--bounds needs a value"};
      const std::string b{v};
      if (b == "none") {
        cfg.bounds.kind = BoundsPolicy::Kind::None;
      } else if (b == "wall") {
        cfg.bounds.kind = BoundsPolicy::Kind::Wall;
      } else {
        return std::string{"unknown --bounds '" + b + "' (none|wall)"};
      }
    } else if (a == "--margin") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--margin needs a value"};
      cfg.bounds.margin = static_cast<float>(std::atof(v));
    } else if (a == "--ep") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--ep needs a value"};
      const std::string e{v};
      if (e == "cpu") {
        cfg.ep = ExecProvider::CpuEp;
      } else if (e == "cuda") {
        cfg.ep = ExecProvider::CudaEp;
      } else {
        return std::string{"unknown --ep '" + e + "' (cpu|cuda)"};
      }
    } else if (a == "--frames") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--frames needs a value"};
      cfg.frames = v;  // stage D D2: frame-sequence manifest instead of rendering
    } else if (a == "--annotations") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--annotations needs a value"};
      cfg.annotations = v;  // stage D D3: annotated truth instead of kinematics
    } else if (a == "--score-space") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--score-space needs a value"};
      const std::string s{v};
      if (s == "world") {
        cfg.score_space = ScoreSpace::WorldXZ;
      } else if (s == "observation") {
        cfg.score_space = ScoreSpace::ObservationUV;
      } else {
        return std::string{"unknown --score-space '" + s + "' (world|observation)"};
      }
    } else if (a == "--out") {
      const char* v = need_value(i);
      if (v == nullptr) return std::string{"--out needs a value"};
      artifact_path = v;
    } else {
      return std::string{"unknown option '" + a + "'"};
    }
  }
  if (cfg.bounds.kind == BoundsPolicy::Kind::Wall &&
      !(cfg.bounds.margin > 0.0f && cfg.bounds.margin < 0.5f)) {
    return std::string{"wall bounds need 0 < margin < 0.5"};
  }
  if (!cfg.annotations.empty()) {
    if (cfg.frames.empty()) {
      return std::string{
          "--annotations requires --frames: annotated truth is only meaningful "
          "against the real frames it describes"};
    }
    if (cfg.bounds.kind == BoundsPolicy::Kind::Wall) {
      return std::string{
          "--annotations is incompatible with --bounds wall: reflection clamps "
          "positions into [margin, 1-margin] and would move loaded truth"};
    }
  }
  return cfg;
}

std::map<std::string, Percentiles> percentiles(const StageSamples& samples) {
  std::map<std::string, Percentiles> out;
  for (const auto& [stage, xs] : samples) {  // map order = deterministic
    Percentiles p;
    if (!xs.empty()) {
      std::vector<double> s = xs;
      std::sort(s.begin(), s.end());
      const auto at = [&](double q) -> double {
        if (s.size() == 1) return s[0];
        const double rank = q * static_cast<double>(s.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(std::floor(rank));
        const double frac = rank - static_cast<double>(lo);
        if (lo + 1 >= s.size()) return s[lo];
        return s[lo] + frac * (s[lo + 1] - s[lo]);
      };
      p.p50 = at(0.5);
      p.p95 = at(0.95);
    }
    out[stage] = p;
  }
  return out;
}

std::map<std::string, double> latency_keys(const RunReport& r) {
  std::map<std::string, double> out;
  for (const auto& [stage, p] : r.stage_percentiles) {
    out[stage + ".p50"] = p.p50;
    out[stage + ".p95"] = p.p95;
  }
  return out;
}

RunReport LoopDriver::run(const RunConfig& cfg) {
  RunReport rep;

  TruthWorld truth(cfg.seed);
  WorldState ws(cfg.bounds);  // bounds injected at construction (L1 sec 4)
  // Stage D D3: annotated truth replaces the scene every tick, so the seeded
  // demo scene is not built -- an empty start is the honest state, and building
  // it would put entities in the world that no frame shows.
  const bool annotated = !cfg.annotations.empty();
  // Enforced here, not only in parse_config: a direct API caller could ask for
  // annotated truth without a frame stream, and then the renderer would draw
  // the very entities the truth names -- a panel that is correct by
  // construction, which is the worst kind of measurement to believe.
  if (annotated && cfg.frames.empty()) {
    throw std::invalid_argument(
        "annotations: annotated truth requires --frames (the manifest supplies "
        "the observations its boxes describe)");
  }
  const AnnotationSet ann =
      annotated ? load_annotation_set(cfg.annotations) : AnnotationSet{};
  if (!annotated) truth.build_demo_scene(ws, scene_entities_);
  if (annotated) {
    rep.annotations_loaded = true;
    rep.annotation_dataset = ann.dataset;
    rep.annotation_license = ann.license;
    rep.annotation_frames = ann.frames.size();
    rep.annotation_boxes = ann.boxes_kept();
    rep.dropped_crowd = ann.dropped_crowd;
    rep.dropped_degenerate = ann.dropped_degenerate;
    rep.dropped_over_cap = ann.dropped_over_cap;
  }

  ObservationGenerator gen;
  NearestPositionStrategy strategy(0.1f);
  BeliefLedger ledger;
  MetricsModule metrics;
  const RenderConfig rcfg{cfg.width, cfg.height, cfg.footprint};

  std::vector<std::unique_ptr<PerceptionAdapter>> perceivers;
  for (const std::string& n : cfg.perception_adapters) {
    auto a = perception::make(n);
    if (a == nullptr) {
      throw std::invalid_argument("unknown perception adapter: " + n);
    }
    a->set_execution_provider(cfg.ep);  // stage C: provider reaches adapters here
    perceivers.push_back(std::move(a));
  }
  std::vector<std::unique_ptr<WorldModelAdapter>> models;
  for (const std::string& n : cfg.world_models) {
    auto m = world_model::make(n);
    if (m == nullptr) {
      throw std::invalid_argument("unknown world model: " + n);
    }
    models.push_back(std::move(m));
  }

  StageLatency lat_sum;
  StageSamples samples;  // per-tick microseconds keyed by stage (raw evidence)
  // Stage D D2: one source per run, chosen once. A frame source and the
  // synthetic renderer never mix within a tick.
  std::unique_ptr<FrameSource> frame_src =
      cfg.frames.empty() ? nullptr : make_file_frame_source(cfg.frames);
  const auto us = [](SteadyClock::time_point a, SteadyClock::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
  };
  const auto t_start = SteadyClock::now();
  Tick completed = 0;
  bool canvas_checked = false;  // one image plane check per run (D3)
  for (Tick t = 1; t <= cfg.ticks; ++t) {
    TickLog log;
    log.tick = t;

    const auto s0 = SteadyClock::now();
    if (annotated) {
      // Annotated truth is a stream, not a simulation: no integration, no
      // reflection, so no boundary events (log.boundary_events stays 0 as-is).
      if (!truth.load_annotations(ws, ann, t)) break;  // truth stream ended
    } else {
      const std::size_t bounds_before = ws.boundary_events();
      truth.advance(ws, t);
      log.boundary_events = ws.boundary_events() - bounds_before;
      rep.boundary_events += log.boundary_events;
    }
    const auto s1 = SteadyClock::now();
    samples[annotated ? "truth_load" : "truth_step"].push_back(us(s0, s1));
    Observation obs;
    bool stream_alive = true;
    if (frame_src) {
      stream_alive = frame_src->next(obs);
      if (stream_alive) obs.tick = t;  // the loop owns time (contract 1)
      // One image plane, two producers: the normalized boxes in the annotation
      // manifest are only comparable to what the detector saw if both refer to
      // the same canvas. Checked on the first frame that actually arrives -- a
      // frame manifest has fixed dimensions, so once is enough.
      if (stream_alive && annotated && !canvas_checked) {
        canvas_checked = true;
        if (obs.view.width != ann.width || obs.view.height != ann.height) {
          throw std::invalid_argument(
              "annotations: canvas mismatch -- frames are " +
              std::to_string(obs.view.width) + "x" + std::to_string(obs.view.height) +
              ", the truth manifest declares " + std::to_string(ann.width) + "x" +
              std::to_string(ann.height));
        }
      }
    } else {
      obs = gen.render(ws, t, rcfg, Transform{});
    }
    const auto s2 = SteadyClock::now();
    if (!stream_alive) break;  // end of data is a normal finish, not a fault
    // Stage D D4: scoring in the observation space needs the camera of the
    // observation being scored. Without one there is nothing to project, and
    // silently judging in world space would mislabel the run.
    if (cfg.score_space == ScoreSpace::ObservationUV &&
        (obs.view.width <= 0 || obs.view.height <= 0)) {
      throw std::invalid_argument(
          "score space observation_uv (--score-space observation): the "
          "observation carries no camera (resolution " +
          std::to_string(obs.view.width) + "x" + std::to_string(obs.view.height) +
          "), so there is nothing to project");
    }
    // Record successful sourcing only: the terminal EOF probe is a stream check,
    // not a stage, and counting it would make the sample count lie by one.
    samples[frame_src ? "frame_fetch" : "render"].push_back(us(s1, s2));

    std::vector<Proposal> proposals;
    for (auto& a : perceivers) {
      const auto ta = SteadyClock::now();
      try {  // isolation (slice 03 AC): a faulting adapter never halts ticks
        auto ps = a->observe(obs);
        proposals.insert(proposals.end(), std::make_move_iterator(ps.begin()),
                         std::make_move_iterator(ps.end()));
      } catch (const std::exception& e) {
        record_fault(rep, a->name(), e.what());
      } catch (...) {
        record_fault(rep, a->name(), "unknown exception");
      }
      const auto tb = SteadyClock::now();
      samples[std::string("detect:") + a->name()].push_back(us(ta, tb));
    }
    for (auto& m : models) {
      const auto tm = SteadyClock::now();
      try {
        auto ps = m->predict(t, ws);
        proposals.insert(proposals.end(), std::make_move_iterator(ps.begin()),
                         std::make_move_iterator(ps.end()));
      } catch (const std::exception& e) {
        record_fault(rep, m->name(), e.what());
      } catch (...) {
        record_fault(rep, m->name(), "unknown exception");
      }
      const auto tn = SteadyClock::now();
      samples[std::string("predict:") + m->name()].push_back(us(tm, tn));
    }
    const auto s3 = SteadyClock::now();

    for (const Proposal& p : proposals) {
      const AdjudicationResult v = strategy.resolve(p, ws);
      switch (v.kind) {
        case VerdictKind::Matched: ++log.applied; break;
        case VerdictKind::NewEntity: ++log.announced; break;
        case VerdictKind::Rejected: ++log.rejected; break;
      }
      ledger.record(BeliefEntry{p, v, t});
    }
    const auto s4 = SteadyClock::now();

    // The claims of this tick are judged in the configured space, through the
    // camera that produced the observation they came from (ADR-0013).
    ScoreContext score_ctx;
    score_ctx.space = cfg.score_space;
    score_ctx.view = obs.view;
    metrics.feed(ws, ledger, t, score_ctx);
    const auto s5 = SteadyClock::now();
    samples["adjudicate"].push_back(us(s3, s4));
    samples["metrics"].push_back(us(s4, s5));

    // StageLatency (4 coarse segments) is derived from the fine samples;
    // nothing is instrumented twice.
    log.latency.render_ms = (us(s0, s2)) / 1000.0;
    log.latency.perceive_ms = us(s2, s3) / 1000.0;
    log.latency.adjudicate_ms = us(s3, s4) / 1000.0;
    log.latency.metrics_ms = us(s4, s5) / 1000.0;
    lat_sum.render_ms += log.latency.render_ms;
    lat_sum.perceive_ms += log.latency.perceive_ms;
    lat_sum.adjudicate_ms += log.latency.adjudicate_ms;
    lat_sum.metrics_ms += log.latency.metrics_ms;

    rep.applied += log.applied;
    rep.announced += log.announced;
    rep.rejected += log.rejected;
    ++completed;
  }
  const auto t_end = SteadyClock::now();

  rep.stage_samples = samples;
  rep.stage_percentiles = percentiles(samples);

  rep.mode = cfg.mode;
  rep.bounds = cfg.bounds;
  rep.ep = cfg.ep;
  rep.score_space = cfg.score_space;
  rep.command_line = cfg.command_line;
  rep.ticks_run = completed;  // a frame stream may end before cfg.ticks
  rep.total_seconds = std::chrono::duration<double>(t_end - t_start).count();
  rep.hz = rep.total_seconds > 0.0
               ? static_cast<double>(completed) / rep.total_seconds
               : 0.0;
  rep.latency_sum = lat_sum;
  rep.metrics = metrics.report();
  rep.world_hash = hash_world(ws);
  rep.ledger_hash = hash_ledger(ledger);
  rep.ledger_entries = ledger.all().size();
  return rep;
}

}  // namespace ow
