#include "metrics/metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace ow {
namespace {

constexpr float kCorrectTol = 0.1f;  // "positionally correct" bar (v1, fixed)

// Position error of a matched proposal against current truth, judged in the
// space the context names (ADR-0013). Returns false when the entry cannot
// carry a comparable position claim.
//
// R2: the uv ALWAYS comes from the camera (ViewCamera::pixel_x/world_x). A
// second mapping inside the metrics would be a bypass of the camera, and the
// two would silently disagree the first time the camera is not the identity.
//
// R3: a position outside the camera's world window is not comparable. The
// camera clamps to the border, so an off-frame truth would be projected onto
// the edge and could be declared "correct" at zero distance. Such a claim
// keeps its place in the ECE denominator but earns no correctness credit and
// leaves the divergence mean (visible afterwards as divergence_n).
//
// R4: both spaces judge "correct" with the SAME kCorrectTol. The world's
// x/z IS the normalized canvas coordinate, so 0.1 world units and 0.1
// normalized uv are one physical quantity, not two tolerances.
//
// Applicability boundary: ObservationUV answers "is this the same point in
// THIS image". It supports calibration comparison within one observation
// sequence and must not carry a cross-scene localization claim (ADR-0013,
// L1 sec 7 R-D7).
bool matched_error(const BeliefEntry& e, const WorldState& ws, const ScoreContext& ctx,
                   double& err) {
  if (e.verdict.kind != VerdictKind::Matched || !e.proposal.fields.transform)
    return false;
  const Entity* truth = ws.find(e.verdict.matched);
  if (truth == nullptr) return false;
  const Vec3& claim = e.proposal.fields.transform->position;
  const Vec3& t = truth->transform.position;
  if (ctx.space == ScoreSpace::WorldXZ) {
    const double dx = static_cast<double>(claim.x) - static_cast<double>(t.x);
    const double dz = static_cast<double>(claim.z) - static_cast<double>(t.z);
    err = std::sqrt(dx * dx + dz * dz);
    return true;
  }
  // ObservationUV: "the same pixel in this image". The v1 camera is an
  // identity normalized projection (world x/z IS the normalized canvas
  // coordinate), so the two spaces differ by at most one pixel of
  // quantization; the space exists to bound the comparison to one
  // observation sequence, not to invent a geometry.
  const ViewCamera& v = ctx.view;
  if (v.width <= 0 || v.height <= 0) return false;  // no camera, no projection
  if (!v.in_window(claim.x, claim.z) || !v.in_window(t.x, t.z)) return false;  // R3
  const double dx = static_cast<double>(v.world_x(v.pixel_x(claim.x))) -
                    static_cast<double>(v.world_x(v.pixel_x(t.x)));
  const double dz = static_cast<double>(v.world_z(v.pixel_z(claim.z))) -
                    static_cast<double>(v.world_z(v.pixel_z(t.z)));
  err = std::sqrt(dx * dx + dz * dz);
  return true;
}

class DivergenceMetric final : public Metric {
 public:
  const char* name() const override { return "divergence"; }
  void feed(const WorldState& ws, const BeliefLedger& l, Tick t,
            const ScoreContext& ctx) override {
    for (const BeliefEntry* e : l.at(t)) {
      double err = 0.0;
      if (matched_error(*e, ws, ctx, err)) {
        sum_ += err;
        n_ += 1.0;
        // Per-producer accumulators run beside the aggregate ones: the
        // aggregate keeps its own order, so its value stays bit-for-bit.
        Acc& a = per_[e->proposal.producer];
        a.sum += err;
        a.n += 1.0;
      }
    }
  }
  double value() const override { return n_ > 0.0 ? sum_ / n_ : 0.0; }
  // "divergence" (aggregate, unchanged) plus the decomposed view and the
  // denominators: a mean without its denominator cannot be read.
  void collect(std::map<std::string, double>& out) const override {
    out["divergence"] = value();
    out["divergence_n"] = n_;
    for (const auto& [producer, a] : per_) {
      out["divergence:" + producer] = a.n > 0.0 ? a.sum / a.n : 0.0;
      out["divergence_n:" + producer] = a.n;
    }
  }

 private:
  struct Acc {
    double sum = 0.0;
    double n = 0.0;
  };
  double sum_ = 0.0;
  double n_ = 0.0;
  std::map<std::string, Acc> per_;
};

// Coverage (problem-log 015): how much of what a producer claimed actually
// landed on the truth. A separation mean reported without its denominator can
// fall while coverage collapses -- so coverage travels with it.
//
// The empty ledger gives 0.0, deliberately the opposite of
// identity_stability's 1.0: "no belief anchored to the truth" is not
// "perfectly stable".
class CoverageMetric final : public Metric {
 public:
  const char* name() const override { return "coverage"; }
  void feed(const WorldState&, const BeliefLedger& l, Tick t,
            const ScoreContext&) override {
    for (const BeliefEntry* e : l.at(t)) {
      Row& r = rows_[e->proposal.producer];
      r.claims += 1.0;
      if (e->verdict.kind == VerdictKind::Matched) r.matched += 1.0;
    }
  }
  double value() const override {
    double claims = 0.0;
    double matched = 0.0;
    for (const auto& [producer, r] : rows_) {
      claims += r.claims;
      matched += r.matched;
    }
    return claims > 0.0 ? matched / claims : 0.0;
  }
  void collect(std::map<std::string, double>& out) const override {
    out["coverage"] = value();
    for (const auto& [producer, r] : rows_) {
      out["coverage:" + producer] = r.claims > 0.0 ? r.matched / r.claims : 0.0;
      out["claims:" + producer] = r.claims;
    }
  }

 private:
  struct Row {
    double claims = 0.0;
    double matched = 0.0;
  };
  std::map<std::string, Row> rows_;
};

class EceMetric final : public Metric {
 public:
  const char* name() const override { return "ece"; }
  void feed(const WorldState& ws, const BeliefLedger& l, Tick t,
            const ScoreContext& ctx) override {
    for (const BeliefEntry* e : l.at(t)) {
      const float c = e->proposal.confidence;
      const int bin = std::min(9, static_cast<int>(std::floor(c * 10.0f)));
      Bin& b = bins_[e->proposal.producer][bin];  // producer-keyed rows
      b.n += 1.0;
      b.conf_sum += static_cast<double>(c);
      double err = 0.0;
      const bool correct = matched_error(*e, ws, ctx, err) &&
                           err <= static_cast<double>(kCorrectTol);
      if (correct) b.correct += 1.0;
    }
  }
  double value() const override {
    double total = 0.0;
    double ece = 0.0;
    for (const auto& [producer, bs] : bins_) {
      for (const Bin& b : bs) {
        total += b.n;
        ece += std::abs(b.correct - b.conf_sum);
      }
    }
    return total > 0.0 ? ece / total : 0.0;
  }
  // Aggregate key "ece" plus one decomposed key per producer ("ece:<name>").
  void collect(std::map<std::string, double>& out) const override {
    out["ece"] = value();
    for (const auto& [producer, bs] : bins_) {  // map order = deterministic
      double n = 0.0;
      double ece = 0.0;
      for (const Bin& b : bs) {
        n += b.n;
        ece += std::abs(b.correct - b.conf_sum);
      }
      if (n > 0.0) out["ece:" + producer] = ece / n;
    }
  }

 private:
  struct Bin {
    double n = 0.0;
    double conf_sum = 0.0;
    double correct = 0.0;
  };
  std::map<std::string, std::array<Bin, 10>> bins_;
};

class IdentityStabilityMetric final : public Metric {
 public:
  const char* name() const override { return "identity_stability"; }
  void feed(const WorldState&, const BeliefLedger& l, Tick t,
            const ScoreContext&) override {
    std::vector<EntityId> cur;
    for (const BeliefEntry* e : l.at(t)) {
      if (e->verdict.kind == VerdictKind::Matched) {
        cur.push_back(e->verdict.matched);
      }
    }
    std::sort(cur.begin(), cur.end());
    if (have_prev_) {
      ++transitions_;
      if (cur == prev_) ++stable_;
    }
    prev_ = std::move(cur);
    have_prev_ = true;
  }
  double value() const override {
    // No adjacent pair observed yet: declare full stability (L2 sec 5).
    return transitions_ > 0.0 ? stable_ / transitions_ : 1.0;
  }

 private:
  std::vector<EntityId> prev_;
  bool have_prev_ = false;
  double transitions_ = 0.0;
  double stable_ = 0.0;
};

}  // namespace

MetricsModule::MetricsModule() {
  register_metric(std::make_unique<DivergenceMetric>());
  register_metric(std::make_unique<EceMetric>());
  register_metric(std::make_unique<IdentityStabilityMetric>());
  register_metric(std::make_unique<CoverageMetric>());
}

void MetricsModule::register_metric(std::unique_ptr<Metric> m) {
  metrics_.push_back(std::move(m));
}

void MetricsModule::feed(const WorldState& ws, const BeliefLedger& l, Tick t,
                         const ScoreContext& ctx) {
  for (const auto& m : metrics_) m->feed(ws, l, t, ctx);
}

std::map<std::string, double> MetricsModule::report() const {
  std::map<std::string, double> out;
  for (const auto& m : metrics_) m->collect(out);
  return out;
}

}  // namespace ow
