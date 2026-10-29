// omniworld_loop — stage-A closed loop CLI (reference mode, byte-exact).
// Stage B: --mode reference|performance; --out writes the deterministic JSON
// run report (mode + environment metadata, per ADR-0009).
#include "loop/driver.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {

// Everything after config parsing, in one place so main() can give ANY failure
// a message and an exit code. Returns 0 on a qualified run, 1 on a performance
// run that had to absorb an adapter fault, 2 when the artifact could not be
// written.
int run_and_report(const ::ow::RunConfig& cfg, const std::string& artifact_path) {
  ::ow::LoopDriver loop(8);
  const ::ow::RunReport r = loop.run(cfg);
  const std::string json = ::ow::run_report_json(r);
  if (!artifact_path.empty()) {
    if (FILE* f = std::fopen(artifact_path.c_str(), "wb")) {
      std::fwrite(json.data(), 1, json.size(), f);
      std::fclose(f);
      std::printf("artifact: %s\n", artifact_path.c_str());
    } else {
      std::fprintf(stderr, "error: cannot write %s\n", artifact_path.c_str());
      return 2;
    }
  }

  std::printf("mode=%s\n", r.mode == ::ow::RunMode::Reference ? "reference" : "performance");
  std::printf("ticks=%llu seconds=%.6f hz=%.1f\n",
              static_cast<unsigned long long>(r.ticks_run), r.total_seconds,
              r.hz);
  std::printf("world_hash=%016llx ledger_hash=%016llx entries=%zu\n",
              static_cast<unsigned long long>(r.world_hash),
              static_cast<unsigned long long>(r.ledger_hash),
              r.ledger_entries);
  std::printf("applied=%zu announced=%zu rejected=%zu faults=%zu\n",
              r.applied, r.announced, r.rejected, r.adapter_faults);
  std::printf("divergence=%.6f ece=%.6f identity_stability=%.6f\n",
              r.metrics.at("divergence"), r.metrics.at("ece"),
              r.metrics.at("identity_stability"));
  std::printf("latency_ms: render=%.3f perceive=%.3f adjudicate=%.3f metrics=%.3f\n",
              r.latency_sum.render_ms, r.latency_sum.perceive_ms,
              r.latency_sum.adjudicate_ms, r.latency_sum.metrics_ms);
  // C5 G7: the isolation layer keeps ticks running past an adapter fault (that
  // is a contract property, not a bug), so in performance mode a run where a
  // participant silently failed is not a qualified measurement -- say so with
  // the exit code. The artifact is already written, with first_fault as the
  // reason. Reference mode keeps its existing semantics.
  if (cfg.mode == ::ow::RunMode::Performance && r.adapter_faults > 0) {
    std::fprintf(stderr, "error: %zu adapter fault(s); first_fault: %s\n",
                 r.adapter_faults, r.first_fault.c_str());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string artifact_path;
  const auto parsed = ::ow::parse_config(argc, argv, artifact_path);
  if (const std::string* err = std::get_if<std::string>(&parsed)) {
    std::fprintf(stderr, "error: %s\n", err->c_str());
    std::fprintf(stderr,
                 "usage: omniworld_loop [--ticks N] [--seed S] [--width W] "
                 "[--height H] [--footprint F] [--adapters a,b] [--models m,n] "
                 "[--frames manifest] [--annotations manifest] "
                 "[--mode reference|performance] [--out path]\n"
                 "exit: 0 ok | 1 setup failure or faulted performance run | "
                 "2 bad usage or unwritable artifact\n");
    return 2;
  }
  const ::ow::RunConfig cfg = std::get<::ow::RunConfig>(parsed);

  // C5 G6: failures used to escape main() and abort the process with no message
  // and no artifact -- measured: exit 0xC0000409, empty stdout/stderr.
  try {
    return run_and_report(cfg, artifact_path);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "error: unknown failure\n");
    return 1;
  }
}
