// ow_tests — zero-dependency check harness. Usage: ow_tests <slice-name>
// Slice granularity in ctest: one test name per slice, so a failure names its slice.
#include "core/hash.hpp"
#include "core/rng.hpp"
#include "adjudication/adjudication.hpp"
#include "belief/ledger.hpp"
#include "loop/driver.hpp"
#include "metrics/metrics.hpp"
#include "tolerance/tolerance.hpp"
#include "observation/frame_source.hpp"
#include "observation/observation.hpp"
#include "perception/adapter.hpp"
#include "truth_world/annotation.hpp"
#include "truth_world/truth_world.hpp"
#include "world_model/world_model.hpp"
#include "world_state/world_state.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace {
int g_failures = 0;
}  // namespace

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      ++g_failures;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                              \
  } while (0)

using namespace ow;

namespace slice01 {

std::uint64_t hash_state(const WorldState& ws) {
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

void test_tick_advance_exact() {
  TruthWorld tw(42);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId id = tw.spawn(ws, ClassTag{"blob"},
                               Transform{Vec3{0.0f, 0.0f, 0.0f}, Quat{}},
                               Vec3{0.25f, 0.0f, -0.5f});
  tw.advance(ws, 10);
  CHECK(ws.tick() == 10);
  const Entity* e = ws.find(id);
  CHECK(e != nullptr);
  if (e != nullptr) {
    CHECK(e->transform.position.x == 2.5f);   // 0.25 * 10, exact in binary
    CHECK(e->transform.position.y == 0.0f);
    CHECK(e->transform.position.z == -5.0f);  // -0.5 * 10, exact in binary
  }
}

void test_determinism() {
  TruthWorld a(7);
  WorldState wa{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  a.build_demo_scene(wa, 8);
  a.advance(wa, 100);

  TruthWorld b(7);
  WorldState wb{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  b.build_demo_scene(wb, 8);
  b.advance(wb, 100);

  CHECK(hash_state(wa) == hash_state(wb));  // same seed -> identical world

  TruthWorld c(8);
  WorldState wc{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  c.build_demo_scene(wc, 8);
  c.advance(wc, 100);
  CHECK(hash_state(wa) != hash_state(wc));  // different seed -> different world
}

void test_id_minting() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId a = tw.spawn(ws, ClassTag{"a"}, Transform{}, Vec3{});
  const EntityId b = tw.spawn(ws, ClassTag{"b"}, Transform{}, Vec3{});
  const EntityId c = tw.spawn(ws, ClassTag{"c"}, Transform{}, Vec3{});
  CHECK(a != 0 && b != 0 && c != 0);
  CHECK(a < b && b < c);  // monotonic, never reused
  CHECK(ws.size() == 3);
  CHECK(ws.find(a) != nullptr);
  CHECK(ws.find(999) == nullptr);
}

void test_rng_stable() {
  Rng a(1);
  Rng b(1);
  for (int i = 0; i < 5; ++i) {
    CHECK(a.next() == b.next());  // same seed -> same sequence
  }
  Rng c(2);
  const std::uint64_t first_of_2 = c.next();
  Rng d(1);
  CHECK(d.next() != first_of_2);  // different seeds -> different first draw
}

void test_advance_backwards_is_ignored() {
  TruthWorld tw(3);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"x"}, Transform{}, Vec3{});
  tw.advance(ws, 10);
  tw.advance(ws, 3);  // backwards: defensive no-op, no crash, no state change
  CHECK(ws.tick() == 10);
}

int run() {
  test_tick_advance_exact();
  test_determinism();
  test_id_minting();
  test_rng_stable();
  test_advance_backwards_is_ignored();
  if (g_failures == 0) {
    std::printf("slice01: all tests passed\n");
    return 0;
  }
  std::printf("slice01: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice01

namespace slice02 {

// --- helpers -------------------------------------------------------------

// Expected palette color per the documented formula (tests recompute from
// the spec so the production formula gets pinned, not just exercised).
struct RGB { std::uint8_t r, g, b; };

RGB expected_color(const char* tag) {
  Hasher h;
  h.bytes(tag, std::strlen(tag));
  const std::uint64_t v = h.result();
  return {static_cast<std::uint8_t>(64 + (v & 0x7F)),
          static_cast<std::uint8_t>(64 + ((v >> 8) & 0x7F)),
          static_cast<std::uint8_t>(64 + ((v >> 16) & 0x7F))};
}

constexpr RGB kBg{0x10, 0x14, 0x18};

bool pixel_is(const Observation& o, int px, int py, RGB c) {
  const std::size_t idx = (static_cast<std::size_t>(py) * o.meta.width + px) * 3;
  return o.rgb[idx] == c.r && o.rgb[idx + 1] == c.g && o.rgb[idx + 2] == c.b;
}

bool frame_has_color(const Observation& o, RGB c) {
  for (std::size_t i = 0; i + 2 < o.rgb.size(); i += 3) {
    if (o.rgb[i] == c.r && o.rgb[i + 1] == c.g && o.rgb[i + 2] == c.b) return true;
  }
  return false;
}

std::uint64_t hash_observation(const Observation& o) {
  Hasher h;
  h.value(o.tick);
  h.bytes(o.meta.backend.data(), o.meta.backend.size());
  h.bytes(o.meta.device.data(), o.meta.device.size());
  h.bytes(o.meta.format.data(), o.meta.format.size());
  h.value(o.meta.width);
  h.value(o.meta.height);
  if (!o.rgb.empty()) h.bytes(o.rgb.data(), o.rgb.size());
  for (const auto& [id, v] : o.visibility) {
    h.value(id);
    h.value(static_cast<std::uint8_t>(v));
  }
  return h.result();
}

bool vis_is(const Observation& o, std::size_t i, EntityId id, Visibility v) {
  return o.visibility.size() > i && o.visibility[i].first == id &&
         o.visibility[i].second == v;
}

// --- behavior tests ------------------------------------------------------

void test_resolution_is_parameter() {
  TruthWorld tw(99);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId id =
      tw.spawn(ws, ClassTag{"blob"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
               Vec3{});
  ObservationGenerator gen;

  RenderConfig cfg8{8, 8, 0.25f};
  const Observation o8 = gen.render(ws, 5, cfg8, Transform{});
  CHECK(o8.meta.width == 8 && o8.meta.height == 8);
  CHECK(o8.rgb.size() == 8 * 8 * 3);
  CHECK(pixel_is(o8, 4, 4, expected_color("blob")));   // floor(0.5*8)=4, exact
  CHECK(pixel_is(o8, 0, 0, kBg));
  CHECK(vis_is(o8, 0, id, Visibility::Seen));

  RenderConfig cfg16{16, 16, 0.25f};
  const Observation o16 = gen.render(ws, 5, cfg16, Transform{});
  CHECK(o16.meta.width == 16 && o16.meta.height == 16);
  CHECK(o16.rgb.size() == 16 * 16 * 3);
  CHECK(pixel_is(o16, 8, 8, expected_color("blob")));  // floor(0.5*16)=8, exact
}

void test_three_state_visibility() {
  TruthWorld tw(5);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId a =
      tw.spawn(ws, ClassTag{"seen"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
               Vec3{});
  const EntityId b =
      tw.spawn(ws, ClassTag{"gone"}, Transform{Vec3{2.0f, 0.0f, 2.0f}, Quat{}},
               Vec3{});
  const EntityId c = tw.spawn(ws, ClassTag{"edge"},
                              Transform{Vec3{0.875f, 0.0f, 0.5f}, Quat{}},
                              Vec3{});
  ObservationGenerator gen;
  RenderConfig cfg{64, 64, 0.5f};
  const Observation o = gen.render(ws, 3, cfg, Transform{});

  CHECK(o.visibility.size() == 3);
  CHECK(vis_is(o, 0, a, Visibility::Seen));       // fully inside the window
  CHECK(vis_is(o, 1, b, Visibility::Unseen));     // fully outside
  CHECK(vis_is(o, 2, c, Visibility::Uncertain));  // straddles x=1 boundary

  // B is never drawn.
  CHECK(!frame_has_color(o, expected_color("gone")));
  // A covers columns 16..48 at row 32 (bbox [0.25,0.75] -> px 16..48).
  CHECK(pixel_is(o, 20, 32, expected_color("seen")));
  // C straddles: bbox [0.625,1.125] -> px 40..63; C is drawn after A (id order)
  // so its color wins in the overlap; column 63 shows C, column 20 shows A.
  CHECK(pixel_is(o, 63, 32, expected_color("edge")));
  CHECK(pixel_is(o, 20, 32, expected_color("seen")));
  CHECK(pixel_is(o, 0, 0, kBg));
}

void test_frame_metadata() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  ObservationGenerator gen;
  RenderConfig cfg{4, 4, 0.25f};
  const Observation o = gen.render(ws, 42, cfg, Transform{});
  CHECK(o.tick == 42);
  CHECK(o.meta.backend == "cpu");
  CHECK(o.meta.device == "host");
  CHECK(o.meta.format == "RGB8");
  CHECK(o.sensor_kind == "rgb_topdown");
}

// --- boundary cases ------------------------------------------------------

void test_invalid_config_throws() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  ObservationGenerator gen;
  bool caught = false;
  try {
    RenderConfig bad{0, 8, 0.25f};
    (void)gen.render(ws, 0, bad, Transform{});
  } catch (const std::invalid_argument&) {
    caught = true;
  }
  CHECK(caught);
}

void test_empty_world_is_legal() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  ObservationGenerator gen;
  RenderConfig cfg{4, 4, 0.25f};
  const Observation o = gen.render(ws, 0, cfg, Transform{});
  CHECK(o.visibility.empty());
  CHECK(pixel_is(o, 0, 0, kBg));
  CHECK(o.rgb.size() == 4 * 4 * 3);
}

void test_boundary_straddler_does_not_crash() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"liminal"},
           Transform{Vec3{1.0f, 0.0f, 0.5f}, Quat{}}, Vec3{});
  ObservationGenerator gen;
  RenderConfig cfg{64, 64, 0.5f};
  const Observation o = gen.render(ws, 0, cfg, Transform{});
  // bbox [0.75, 1.25] straddles the x=1 boundary -> Uncertain; writes clamped.
  CHECK(o.visibility.size() == 1);
  CHECK(vis_is(o, 0, 1, Visibility::Uncertain));
}

// --- determinism, PAIRED with behavior (problem-log 002) ----------------

void test_determinism_pairing() {
  TruthWorld tw(7);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
           Vec3{});
  tw.spawn(ws, ClassTag{"b"}, Transform{Vec3{0.875f, 0.0f, 0.25f}, Quat{}},
           Vec3{0.125f, 0.0f, 0.0f});
  tw.spawn(ws, ClassTag{"c"}, Transform{Vec3{2.0f, 0.0f, 2.0f}, Quat{}},
           Vec3{});

  ObservationGenerator gen;
  RenderConfig cfg{64, 64, 0.5f};
  const Observation o1 = gen.render(ws, 10, cfg, Transform{});
  const Observation o2 = gen.render(ws, 10, cfg, Transform{});
  CHECK(hash_observation(o1) == hash_observation(o2));  // same input -> identical

  // Progress assertion (would fail on a trivially-deterministic stub):
  const Observation o3 = gen.render(ws, 11, cfg, Transform{});
  CHECK(hash_observation(o1) != hash_observation(o3));
}

int run() {
  test_resolution_is_parameter();
  test_three_state_visibility();
  test_frame_metadata();
  test_invalid_config_throws();
  test_empty_world_is_legal();
  test_boundary_straddler_does_not_crash();
  test_determinism_pairing();
  if (g_failures == 0) {
    std::printf("slice02: all tests passed\n");
    return 0;
  }
  std::printf("slice02: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice02

namespace slice03 {

// --- helpers (self-contained per slice; duplicating the tiny color math
// is deliberate so each slice's expectations stand alone) ----------------

Observation blank_frame(int w, int h) {
  Observation o;
  o.tick = 0;
  o.meta.width = w;
  o.meta.height = h;
  o.rgb.assign(static_cast<std::size_t>(w) * h * 3, 0);
  for (int i = 0; i < w * h; ++i) {
    o.rgb[i * 3 + 0] = kBackgroundRgb[0];
    o.rgb[i * 3 + 1] = kBackgroundRgb[1];
    o.rgb[i * 3 + 2] = kBackgroundRgb[2];
  }
  return o;
}

void paint_blob(Observation& o, int px0, int py0, int px1, int py1,
                unsigned char r, unsigned char g, unsigned char b) {
  for (int py = py0; py <= py1; ++py) {
    for (int px = px0; px <= px1; ++px) {
      const std::size_t idx =
          (static_cast<std::size_t>(py) * o.meta.width + px) * 3;
      o.rgb[idx] = r;
      o.rgb[idx + 1] = g;
      o.rgb[idx + 2] = b;
    }
  }
}

bool proposal_eq(const Proposal& a, const Proposal& b) {
  if (a.producer != b.producer || a.tick_observed != b.tick_observed ||
      a.target.index() != b.target.index() || a.confidence != b.confidence)
    return false;
  if (a.fields.class_tag.has_value() != b.fields.class_tag.has_value())
    return false;
  if (a.fields.transform.has_value() != b.fields.transform.has_value())
    return false;
  if (a.fields.transform) {
    if (a.fields.transform->position.x != b.fields.transform->position.x)
      return false;
    if (a.fields.transform->position.z != b.fields.transform->position.z)
      return false;
  }
  return true;
}

// --- behavior tests ------------------------------------------------------

void test_registry_lookup() {
  // The registry only ever gains an entry through an optional build: C3/C4's
  // OW_WITH_ONNXRUNTIME adds rtdetr_v2_s and depth_anything_v2_s. Assert the
  // exact set per config, so neither an unexpected entry nor a silently
  // missing adapter goes unnoticed (problem-log 010).
  constexpr std::size_t kExpected = OW_WITH_ONNXRUNTIME ? 4 : 2;
  const auto names = perception::names();
  CHECK(names.size() == kExpected);
  if (names.size() < kExpected) return;
  CHECK(names[0] == "blob_detect");  // std::map order = sorted = deterministic
  CHECK(names[1] == "blob_detect_overconfident");
#if OW_WITH_ONNXRUNTIME
  CHECK(names[2] == "depth_anything_v2_s");  // sorted: 'd' before 'r'
  CHECK(names[3] == "rtdetr_v2_s");
#endif
  CHECK(perception::make("blob_detect") != nullptr);
  CHECK(perception::make("blob_detect_overconfident") != nullptr);
  CHECK(perception::make("does_not_exist") == nullptr);
}

void test_blob_centroid_exact() {
  Observation o = blank_frame(8, 8);
  o.tick = 9;
  paint_blob(o, 2, 4, 3, 5, 200, 60, 90);  // 2x2 at cols 2..3, rows 4..5
  auto ad = perception::make("blob_detect");
  const auto ps = ad->observe(o);
  CHECK(ps.size() == 1);
  if (ps.empty()) return;
  const Proposal& p = ps[0];
  CHECK(p.producer == "blob_detect");
  CHECK(p.tick_observed == 9);
  CHECK(std::holds_alternative<NewEntity>(p.target));
  CHECK(!p.fields.class_tag.has_value());  // adapters describe, never mint class
  CHECK(p.fields.transform.has_value());
  // centroid of the 2x2 block: px mean 2.5 -> x = (2.5+0.5)/8 = 0.375 (exact);
  // py mean 4.5 -> z = 0.625 (exact).
  CHECK(p.fields.transform->position.x == 0.375f);
  CHECK(p.fields.transform->position.z == 0.625f);
  CHECK(p.confidence > 0.0f && p.confidence <= 1.0f);
}

void test_two_blobs_two_proposals() {
  Observation o = blank_frame(16, 16);
  paint_blob(o, 2, 2, 2, 2, 255, 128, 0);
  paint_blob(o, 12, 12, 13, 13, 0, 128, 255);
  auto ad = perception::make("blob_detect");
  const auto ps = ad->observe(o);
  CHECK(ps.size() == 2);
  for (const auto& p : ps) {
    CHECK(p.producer == "blob_detect");
    CHECK(p.tick_observed == o.tick);
    CHECK(std::holds_alternative<NewEntity>(p.target));
  }
}

void test_overconfident_contrast() {
  Observation o = blank_frame(8, 8);
  paint_blob(o, 2, 2, 3, 3, 200, 60, 90);
  const auto honest = perception::make("blob_detect")->observe(o);
  const auto loud = perception::make("blob_detect_overconfident")->observe(o);
  CHECK(honest.size() == 1 && loud.size() == 1);
  if (honest.empty() || loud.empty()) return;
  CHECK(loud[0].confidence == 0.99f);  // always max-claims
  CHECK(honest[0].confidence < 0.99f && honest[0].confidence >= 0.70f);
}

void test_validate_proposal() {
  Proposal ok;
  ok.producer = "t";
  ok.confidence = 0.5f;
  CHECK(!validate_proposal(ok).has_value());
  Proposal p = ok;
  p.producer = "";
  CHECK(validate_proposal(p).has_value());
  p = ok;
  p.confidence = 1.5f;
  CHECK(validate_proposal(p).has_value());
  p.confidence = -0.1f;
  CHECK(validate_proposal(p).has_value());
  p.confidence = std::numeric_limits<float>::quiet_NaN();
  CHECK(validate_proposal(p).has_value());
}

void test_determinism_pairing() {
  Observation o = blank_frame(8, 8);
  paint_blob(o, 2, 4, 3, 5, 200, 60, 90);
  auto ad = perception::make("blob_detect");
  const auto a = ad->observe(o);
  const auto b = ad->observe(o);
  CHECK(a.size() == b.size());
  bool all_eq = a.size() == b.size();
  for (std::size_t i = 0; i < a.size(); ++i) all_eq = all_eq && proposal_eq(a[i], b[i]);
  CHECK(all_eq);
  paint_blob(o, 6, 6, 6, 6, 200, 60, 90);  // mutate -> proposals must change
  const auto c = ad->observe(o);
  CHECK(c.size() != a.size());
}

int run() {
  test_registry_lookup();
  test_blob_centroid_exact();
  test_two_blobs_two_proposals();
  test_overconfident_contrast();
  test_validate_proposal();
  test_determinism_pairing();
  if (g_failures == 0) {
    std::printf("slice03: all tests passed\n");
    return 0;
  }
  std::printf("slice03: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice03

namespace slice04 {

// --- helpers (duplicated from slice01 on purpose: per-slice local) -------

std::uint64_t hash_state_s04(const WorldState& ws) {
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

Proposal pos_proposal(const char* producer, Tick t, float x, float z,
                      float conf) {
  Proposal p;
  p.producer = producer;
  p.tick_observed = t;
  p.target = NewEntity{};
  Transform tr;
  tr.position = Vec3{x, 0.0f, z};
  p.fields.transform = tr;
  p.confidence = conf;
  return p;
}

Proposal id_proposal(const char* producer, Tick t, EntityId id, float conf) {
  Proposal p;
  p.producer = producer;
  p.tick_observed = t;
  p.target = id;
  Transform tr;
  tr.position = Vec3{0.0f, 0.0f, 0.0f};
  p.fields.transform = tr;
  p.confidence = conf;
  return p;
}

struct Scene {
  TruthWorld tw;
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  EntityId e1, e2;
  Scene() : tw(1) {
    e1 = tw.spawn(ws, ClassTag{"a"},
                  Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}}, Vec3{});
    e2 = tw.spawn(ws, ClassTag{"b"},
                  Transform{Vec3{0.25f, 0.0f, 0.25f}, Quat{}}, Vec3{});
  }
};

// --- behavior tests ------------------------------------------------------

void test_schema_rejection_carries_reason() {
  Scene s;
  NearestPositionStrategy strat;
  Proposal bad = pos_proposal("p", 0, 0.5f, 0.5f, 1.5f);
  const auto v = strat.resolve(bad, s.ws);
  CHECK(v.kind == VerdictKind::Rejected);
  CHECK(!v.reason.empty());
  Proposal no_producer = pos_proposal("", 0, 0.5f, 0.5f, 0.5f);
  const auto v2 = strat.resolve(no_producer, s.ws);
  CHECK(v2.kind == VerdictKind::Rejected);
  CHECK(!v2.reason.empty());
}

void test_explicit_target_match_and_unknown() {
  Scene s;
  NearestPositionStrategy strat;
  const auto hit = strat.resolve(id_proposal("p", 0, s.e1, 0.5f), s.ws);
  CHECK(hit.kind == VerdictKind::Matched && hit.matched == s.e1);
  const auto miss = strat.resolve(id_proposal("p", 0, 999, 0.5f), s.ws);
  CHECK(miss.kind == VerdictKind::Rejected && !miss.reason.empty());
}

void test_position_association() {
  Scene s;
  NearestPositionStrategy strat;  // tol = 0.1
  const auto near = strat.resolve(pos_proposal("p", 0, 0.52f, 0.5f, 0.5f), s.ws);
  CHECK(near.kind == VerdictKind::Matched && near.matched == s.e1);
  const auto far = strat.resolve(pos_proposal("p", 0, 0.9f, 0.9f, 0.5f), s.ws);
  CHECK(far.kind == VerdictKind::NewEntity && far.matched == 0);
}

void test_missing_position_rejected() {
  Scene s;
  NearestPositionStrategy strat;
  Proposal p;
  p.producer = "p";
  p.confidence = 0.5f;
  p.target = NewEntity{};  // no transform claimed
  const auto v = strat.resolve(p, s.ws);
  CHECK(v.kind == VerdictKind::Rejected);
  CHECK(v.reason.find("position") != std::string::npos);
}

void test_tie_breaks_by_id_order() {
  TruthWorld tw(7);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.25f, 0.0f, 0.0f}, Quat{}}, Vec3{});
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.75f, 0.0f, 0.0f}, Quat{}}, Vec3{});
  NearestPositionStrategy strat(0.5f);  // big tolerance
  const auto v = strat.resolve(pos_proposal("p", 0, 0.5f, 0.0f, 0.9f), ws);
  CHECK(v.kind == VerdictKind::Matched);
  CHECK(v.matched == 1);  // id order: first entity wins the tie
}

void test_strategy_is_replaceable_no_core_change() {
  struct AlwaysReject final : AssociationStrategy {
    AdjudicationResult resolve(const Proposal&, const WorldState&) const override {
      return {VerdictKind::Rejected, 0, "double"};
    }
  } always_reject;
  Scene s;
  const auto v =
      always_reject.resolve(pos_proposal("ok", 0, 0.5f, 0.5f, 0.5f), s.ws);
  CHECK(v.kind == VerdictKind::Rejected && v.reason == "double");
}

void test_truth_never_written() {
  Scene s;
  const std::uint64_t before = hash_state_s04(s.ws);
  NearestPositionStrategy strat;
  (void)strat.resolve(pos_proposal("p", 0, 0.52f, 0.5f, 0.5f), s.ws);
  (void)strat.resolve(pos_proposal("p", 0, 0.9f, 0.9f, 0.5f), s.ws);
  (void)strat.resolve(id_proposal("p", 0, 999, 0.5f), s.ws);
  const std::uint64_t after = hash_state_s04(s.ws);
  CHECK(before == after);  // adjudication must not mutate truth, ever
}

void test_ledger_append_only() {
  BeliefLedger ledger;
  BeliefEntry e1{pos_proposal("p", 0, 0.1f, 0.1f, 0.3f),
                 {VerdictKind::Matched, 1, ""}, 1};
  BeliefEntry e2{pos_proposal("p", 0, 0.2f, 0.2f, 0.4f),
                 {VerdictKind::NewEntity, 0, ""}, 1};
  ledger.record(e1);
  ledger.record(e2);
  BeliefEntry e3{pos_proposal("p", 0, 0.3f, 0.3f, 0.5f),
                 {VerdictKind::Rejected, 0, "r"}, 2};
  ledger.record(e3);
  CHECK(ledger.all().size() == 3);
  CHECK(ledger.at(1).size() == 2);
  CHECK(ledger.at(2).size() == 1);
  CHECK(ledger.at(99).empty());
  // append-only: earlier entries unchanged byte-for-byte after later appends
  CHECK(ledger.all()[0].proposal.confidence == 0.3f);
  CHECK(ledger.all()[0].verdict.matched == 1);
  CHECK(ledger.all()[0].recorded_at == 1);
}

void test_determinism_pairing() {
  Scene s;
  NearestPositionStrategy strat;
  const Proposal p = pos_proposal("p", 0, 0.52f, 0.5f, 0.5f);
  const auto a = strat.resolve(p, s.ws);
  const auto b = strat.resolve(p, s.ws);
  CHECK(a.kind == b.kind && a.matched == b.matched && a.reason == b.reason);
  // progress: a different claim associates to the OTHER entity
  const auto c = strat.resolve(pos_proposal("p", 0, 0.28f, 0.25f, 0.5f), s.ws);
  CHECK(c.matched != 0 && c.matched != s.e1);
}

int run() {
  test_schema_rejection_carries_reason();
  test_explicit_target_match_and_unknown();
  test_position_association();
  test_missing_position_rejected();
  test_tie_breaks_by_id_order();
  test_strategy_is_replaceable_no_core_change();
  test_truth_never_written();
  test_ledger_append_only();
  test_determinism_pairing();
  if (g_failures == 0) {
    std::printf("slice04: all tests passed\n");
    return 0;
  }
  std::printf("slice04: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice04

namespace slice05 {

// --- helpers -------------------------------------------------------------

BeliefEntry matched_at(float x, float z, float conf, EntityId truth_id,
                       Tick t) {
  Proposal p;
  p.producer = "test";
  p.tick_observed = t;
  p.target = NewEntity{};
  Transform tr;
  tr.position = Vec3{x, 0.0f, z};
  p.fields.transform = tr;
  p.confidence = conf;
  return BeliefEntry{std::move(p),
                     AdjudicationResult{VerdictKind::Matched, truth_id, {}}, t};
}

struct World2 {
  TruthWorld tw{1};
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  EntityId e1, e2;
  World2() {
    e1 = tw.spawn(ws, ClassTag{"a"},
                  Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}}, Vec3{});
    e2 = tw.spawn(ws, ClassTag{"b"},
                  Transform{Vec3{0.75f, 0.0f, 0.25f}, Quat{}}, Vec3{});
  }
};

// --- behavior tests ------------------------------------------------------

void test_defaults_present() {
  MetricsModule m;
  const auto r = m.report();
  CHECK(r.count("divergence") == 1);
  CHECK(r.count("ece") == 1);
  CHECK(r.count("identity_stability") == 1);
}

void test_divergence_exact() {
  World2 w;
  BeliefLedger l;
  // errors: |0.625-0.5|=0.125 and |0.75-0.5|=0.25 (both binary-exact)
  l.record(matched_at(0.625f, 0.5f, 0.875f, w.e1, 7));
  l.record(matched_at(0.75f, 0.5f, 0.875f, w.e1, 7));
  MetricsModule m;
  m.feed(w.ws, l, 7);
  const auto rep = m.report();
  CHECK(rep.at("divergence") == 0.1875);
}

void test_ece_discrimination_exact() {
  // Honest: conf 0.875 with acc 7/8 -> ECE 0. Overconfident: same errors with
  // conf 1.0 -> ECE 0.125. Gate-1c analog: the lie must surface, exactly.
  BeliefLedger honest, loud;
  for (int i = 0; i < 7; ++i) {  // 7 correct (err 0)
    honest.record(matched_at(0.5f, 0.5f, 0.875f, 1, 3));
    loud.record(matched_at(0.5f, 0.5f, 1.0f, 1, 3));
  }
  // 1 wrong (err 0.5 > tol 0.1)
  honest.record(matched_at(0.0f, 0.5f, 0.875f, 1, 3));
  loud.record(matched_at(0.0f, 0.5f, 1.0f, 1, 3));

  World2 w;
  MetricsModule ma, mb;
  ma.feed(w.ws, honest, 3);
  mb.feed(w.ws, loud, 3);
  const double ea = ma.report().at("ece");
  const double eb = mb.report().at("ece");
  CHECK(ea == 0.0);     // honest calibration: exact zero
  CHECK(eb == 0.125);   // the overconfidence tax, exact
  CHECK(eb > ea);
}

void test_identity_stability() {
  World2 w;
  {
    BeliefLedger l;  // tick1 {1,2}, tick2 {1,2}: perfectly tracked
    l.record(matched_at(0.5f, 0.5f, 0.9f, 1, 1));
    l.record(matched_at(0.75f, 0.25f, 0.9f, 2, 1));
    l.record(matched_at(0.5f, 0.5f, 0.9f, 1, 2));
    l.record(matched_at(0.75f, 0.25f, 0.9f, 2, 2));
    MetricsModule m;
    m.feed(w.ws, l, 1);
    m.feed(w.ws, l, 2);
    CHECK(m.report().at("identity_stability") == 1.0);
  }
  {
    BeliefLedger l;  // tick1 {1,2}, tick2 {1}: one entity lost -> 0
    l.record(matched_at(0.5f, 0.5f, 0.9f, 1, 1));
    l.record(matched_at(0.75f, 0.25f, 0.9f, 2, 1));
    l.record(matched_at(0.5f, 0.5f, 0.9f, 1, 2));
    MetricsModule m;
    m.feed(w.ws, l, 1);
    m.feed(w.ws, l, 2);
    CHECK(m.report().at("identity_stability") == 0.0);
  }
}

void test_metric_is_pluggable() {
  struct Const42 final : Metric {
    const char* name() const override { return "forty_two"; }
    void feed(const WorldState&, const BeliefLedger&, Tick,
              const ScoreContext&) override {}
    double value() const override { return 42.5; }
  };
  MetricsModule m;
  m.register_metric(std::make_unique<Const42>());  // core untouched
  const auto r = m.report();
  CHECK(r.count("forty_two") == 1);
}

void test_determinism_pairing() {
  World2 w;
  BeliefLedger l;
  l.record(matched_at(0.625f, 0.5f, 0.875f, w.e1, 1));
  MetricsModule a, b;
  a.feed(w.ws, l, 1);
  b.feed(w.ws, l, 1);
  CHECK(a.report() == b.report());
  l.record(matched_at(0.0f, 0.5f, 0.875f, w.e1, 2));  // mutate -> must differ
  MetricsModule c;
  c.feed(w.ws, l, 1);
  c.feed(w.ws, l, 2);
  CHECK(!(c.report() == a.report()));
}

void test_empty_ledger_safe() {
  World2 w;
  BeliefLedger l;  // nothing recorded
  MetricsModule m;
  m.feed(w.ws, l, 1);
  const auto r = m.report();
  CHECK(r.at("divergence") == 0.0);
  CHECK(r.at("ece") == 0.0);
  CHECK(r.at("identity_stability") == 1.0);  // declared v1 semantic
}

int run() {
  test_defaults_present();
  test_divergence_exact();
  test_ece_discrimination_exact();
  test_identity_stability();
  test_metric_is_pluggable();
  test_determinism_pairing();
  test_empty_ledger_safe();
  if (g_failures == 0) {
    std::printf("slice05: all tests passed\n");
    return 0;
  }
  std::printf("slice05: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice05

namespace slice06 {

RunConfig base_cfg() {
  RunConfig cfg{};
  cfg.ticks = 50;
  cfg.seed = 7;
  cfg.width = 32;
  cfg.height = 32;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  return cfg;
}

void test_world_model_registry() {
  const auto names = world_model::names();
  CHECK(names.size() == 2);
  CHECK(names[0] == "linear_extrapolation");
  CHECK(names[1] == "zero_delta");
  CHECK(world_model::make("zero_delta") != nullptr);
  CHECK(world_model::make("linear_extrapolation") != nullptr);
  CHECK(world_model::make("nope") == nullptr);
}

void test_models_predict_exactly() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId e1 = tw.spawn(ws, ClassTag{"a"},
                               Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
                               Vec3{0.25f, 0.0f, 0.0f});
  auto lin = world_model::make("linear_extrapolation");
  const auto lp = lin->predict(4, ws);
  CHECK(lp.size() == 1);
  if (!lp.empty()) {
    CHECK(std::holds_alternative<EntityId>(lp[0].target));
    CHECK(std::get<EntityId>(lp[0].target) == e1);
    CHECK(lp[0].fields.transform.has_value());
    CHECK(lp[0].fields.transform->position.x == 0.75f);  // 0.5+0.25 exact
    CHECK(lp[0].confidence == 0.5f);                     // v0.1 as-built habit
    CHECK(lp[0].tick_observed == 4);
  }
  auto zero = world_model::make("zero_delta");
  const auto zp = zero->predict(4, ws);
  CHECK(zp.size() == 1);
  if (!zp.empty()) {
    CHECK(zp[0].fields.transform->position.x == 0.5f);  // stays put
  }
}

void test_loop_end_to_end() {
  LoopDriver loop(8);
  const RunReport r = loop.run(base_cfg());
  CHECK(r.ticks_run == 50);
  CHECK(r.ledger_entries > 0);
  CHECK(r.metrics.count("divergence") == 1);
  CHECK(r.metrics.count("ece") == 1);
  CHECK(r.metrics.count("identity_stability") == 1);
  CHECK(r.applied > 0);
  CHECK(r.applied + r.announced + r.rejected == r.ledger_entries);
}

void test_loop_determinism_pairing() {
  LoopDriver loop(8);
  const RunReport a = loop.run(base_cfg());
  const RunReport b = loop.run(base_cfg());
  CHECK(a.world_hash == b.world_hash);
  CHECK(a.ledger_hash == b.ledger_hash);
  CHECK(a.metrics == b.metrics);  // doubles compared exactly
  RunConfig other = base_cfg();
  other.seed = 8;
  const RunReport c = loop.run(other);
  CHECK(c.world_hash != a.world_hash);  // progress: seed actually matters
}

void test_adapter_fault_isolation() {
  struct ThrowingPerception final : PerceptionAdapter {
    const char* name() const override { return "throwing_test"; }
    std::vector<Proposal> observe(const Observation&) override {
      throw std::runtime_error("adapter boom");
    }
  };
  perception::reg("throwing_test", []() -> std::unique_ptr<PerceptionAdapter> {
    return std::make_unique<ThrowingPerception>();
  });
  RunConfig cfg = base_cfg();
  cfg.perception_adapters = {"blob_detect", "throwing_test"};
  LoopDriver loop(8);
  const RunReport r = loop.run(cfg);
  CHECK(r.ticks_run == 50);       // kept ticking...
  CHECK(r.adapter_faults >= 50);  // ...while counting the fault every tick
  CHECK(r.ledger_entries > 0);    // other adapters unaffected
}

int run() {
  test_world_model_registry();
  test_models_predict_exactly();
  test_loop_end_to_end();
  test_loop_determinism_pairing();
  test_adapter_fault_isolation();
  if (g_failures == 0) {
    std::printf("slice06: all tests passed\n");
    return 0;
  }
  std::printf("slice06: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice06

namespace slice07 {

RunConfig mini_cfg() {
  RunConfig cfg{};
  cfg.ticks = 8;
  cfg.seed = 7;
  cfg.width = 32;
  cfg.height = 32;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  return cfg;
}

void test_parse_defaults() {
  const char* argv[] = {"prog"};
  std::string out;
  const auto parsed = parse_config(1, argv, out);
  CHECK(std::holds_alternative<RunConfig>(parsed));
  if (std::holds_alternative<RunConfig>(parsed)) {  // CLI-layer defaults
    const RunConfig& c = std::get<RunConfig>(parsed);
    CHECK(c.mode == RunMode::Reference);
    CHECK(c.ticks == 2000);
    CHECK(c.width == 64 && c.height == 64);
    CHECK(c.footprint == 0.02f);
    CHECK(c.perception_adapters.size() == 2);
    CHECK(c.world_models.size() == 2);
    CHECK(out.empty());
  }
}

void test_parse_mode_and_values() {
  const char* argv[] = {"prog", "--mode", "performance", "--ticks", "100",
                        "--seed", "9", "--adapters", "blob_detect", "--out", "x.json"};
  std::string out;
  const auto parsed = parse_config(11, argv, out);
  CHECK(std::holds_alternative<RunConfig>(parsed));
  if (std::holds_alternative<RunConfig>(parsed)) {
    const RunConfig& c = std::get<RunConfig>(parsed);
    CHECK(c.mode == RunMode::Performance);
    CHECK(c.ticks == 100);
    CHECK(c.seed == 9);
    CHECK(c.perception_adapters.size() == 1);
    CHECK(out == "x.json");
  }
}

void test_parse_fail_fast() {
  std::string out;
  const char* bad_mode[] = {"prog", "--mode", "bogus"};
  const auto e1 = parse_config(3, bad_mode, out);
  CHECK(std::holds_alternative<std::string>(e1));
  if (std::holds_alternative<std::string>(e1)) {
    CHECK(std::get<std::string>(e1).find("mode") != std::string::npos);
  }
  const char* unknown[] = {"prog", "--wat"};
  const auto e2 = parse_config(2, unknown, out);
  CHECK(std::holds_alternative<std::string>(e2));
}

void test_json_mode_and_env_fields() {
  LoopDriver loop(4);
  RunReport r = loop.run(mini_cfg());
  const std::string j = run_report_json(r);
  CHECK(j.find("\"mode\": \"reference\"") != std::string::npos);
  CHECK(j.find("\"acceleration\": \"none\"") != std::string::npos);
  CHECK(j.find("\"os\": \"windows\"") != std::string::npos);
  CHECK(j.find("\"compiler\": \"MSVC ") != std::string::npos);
  CHECK(j.find("\"git_commit\": ") != std::string::npos);
  CHECK(j.find("\"world_hash\": \"") != std::string::npos);
}

void test_json_is_deterministic() {
  LoopDriver loop(4);
  const RunReport r = loop.run(mini_cfg());
  CHECK(run_report_json(r) == run_report_json(r));  // byte-identical (no wall clock)
}

void test_modes_share_one_path_v1() {
  LoopDriver loop(4);
  const RunReport a = loop.run(mini_cfg());
  RunConfig perf = mini_cfg();
  perf.mode = RunMode::Performance;
  const RunReport b = loop.run(perf);
  CHECK(a.world_hash == b.world_hash);    // v1: same path, honest labeling
  CHECK(a.ledger_hash == b.ledger_hash);
  const std::string jb = run_report_json(b);
  CHECK(jb.find("\"mode\": \"performance\"") != std::string::npos);
  CHECK(jb.find("no GPU components yet") != std::string::npos);
}

int run() {
  test_parse_defaults();
  test_parse_mode_and_values();
  test_parse_fail_fast();
  test_json_mode_and_env_fields();
  test_json_is_deterministic();
  test_modes_share_one_path_v1();
  if (g_failures == 0) {
    std::printf("slice07: all tests passed\n");
    return 0;
  }
  std::printf("slice07: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice07

namespace slice08 {

// Test-local noise stub: a world-model adapter with a parameterized x-axis
// offset (precedent: slice04 AlwaysReject / slice06 ThrowingPerception — it
// never enters src/, it is the tolerance window's own proof instrument).
struct NoisyPredictor final : WorldModelAdapter {
  explicit NoisyPredictor(float eps) : eps_(eps) {}
  const char* name() const override { return "noise_stub"; }
  std::vector<Proposal> predict(Tick t, const WorldState& ws) override {
    std::vector<Proposal> out;
    for (const Entity& e : ws.entities()) {
      Proposal p;
      p.producer = "noise_stub";
      p.tick_observed = t;
      p.target = e.id;
      Transform tr;
      tr.position = Vec3{e.transform.position.x + eps_,
                         e.transform.position.y,
                         e.transform.position.z};
      p.fields.transform = tr;
      p.confidence = 0.5f;
      out.push_back(std::move(p));
    }
    return out;
  }

 private:
  float eps_;
};

float kEpsSmall = 0.02f;
float kEpsBig = 0.2f;
double kDivTol = 0.05;  // calibrated once against the real deltas below

void test_comparator_pure_function() {
  const std::map<std::string, double> a{{"divergence", 0.1}, {"ece", 0.2}};
  const std::map<std::string, double> b{{"divergence", 0.11}, {"ece", 0.2}};
  const std::vector<ToleranceWindow> w{{"divergence", 0.02}, {"ece", 0.02}};
  const ToleranceVerdict v = compare_within_tolerance(a, b, w);
  CHECK(v.pass);
  CHECK(v.deltas.size() == 2);
  CHECK(v.deltas[0].first == "divergence");
  CHECK(v.deltas[0].second == 0.11 - 0.1);  // same expression, same double
  CHECK(v.deltas[1].second == 0.0);

  // Boundary: delta exactly == window passes (binary-exact values).
  const std::map<std::string, double> c{{"d", 0.25}};
  const std::map<std::string, double> d{{"d", 0.5}};
  const ToleranceVerdict vb =
      compare_within_tolerance(c, d, {{"d", 0.25}});
  CHECK(vb.pass);

  // Missing key: sentinel -1.0 and fail.
  const std::map<std::string, double> e{{"other", 1.0}};
  const ToleranceVerdict vm = compare_within_tolerance(c, e, {{"d", 1.0}});
  CHECK(!vm.pass);
  CHECK(vm.deltas[0].second == kMissingDelta);

  // Empty windows: nothing to compare.
  const ToleranceVerdict ve = compare_within_tolerance(a, b, {});
  CHECK(ve.pass);
  CHECK(ve.deltas.empty());

  // NaN metric values fail their window without a special case.
  const std::map<std::string, double> n{{"d", std::nan("")}};
  const ToleranceVerdict vn =
      compare_within_tolerance(c, n, {{"d", 1.0}});
  CHECK(!vn.pass);
}

void test_stub_registration() {
  CHECK(world_model::make("noise_stub_small") != nullptr);
  CHECK(world_model::make("noise_stub_big") != nullptr);
  auto small = world_model::make("noise_stub_small");
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
           Vec3{});
  const auto ps = small->predict(3, ws);
  CHECK(ps.size() == 1);
  if (!ps.empty()) {
    CHECK(ps[0].producer == "noise_stub");
    CHECK(ps[0].fields.transform->position.x ==
          0.5f + kEpsSmall);  // offset visible
  }
}

RunConfig tolerance_cfg() {
  RunConfig cfg{};
  cfg.ticks = 50;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {};  // mechanism isolation: no perception noise
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  return cfg;
}

void test_self_proof_both_directions() {
  LoopDriver loop(8);
  const RunReport base = loop.run(tolerance_cfg());

  RunConfig small = tolerance_cfg();
  small.world_models = {"noise_stub_small"};
  const RunReport rs = loop.run(small);

  RunConfig big = tolerance_cfg();
  big.world_models = {"noise_stub_big"};
  const RunReport rb = loop.run(big);

  const std::vector<ToleranceWindow> window{{"divergence", kDivTol}};
  const ToleranceVerdict within =
      compare_within_tolerance(base.metrics, rs.metrics, window);
  CHECK(within.pass);  // small noise: inside the declared window

  const ToleranceVerdict beyond =
      compare_within_tolerance(base.metrics, rb.metrics, window);
  CHECK(!beyond.pass);  // big noise: outside, and the deltas say why
  CHECK(!beyond.deltas.empty());
  CHECK(beyond.deltas[0].first == "divergence");
  CHECK(beyond.deltas[0].second > kDivTol);
}

void test_determinism_pairing() {
  const std::map<std::string, double> a{{"d", 0.1}};
  const std::map<std::string, double> b{{"d", 0.11}};
  const std::vector<ToleranceWindow> w{{"d", 0.02}};
  const ToleranceVerdict v1 = compare_within_tolerance(a, b, w);
  const ToleranceVerdict v2 = compare_within_tolerance(a, b, w);
  CHECK(v1.pass == v2.pass && v1.deltas == v2.deltas);

  LoopDriver loop(4);
  RunConfig small = tolerance_cfg();
  small.ticks = 4;
  small.world_models = {"noise_stub_small"};
  const RunReport r1 = loop.run(small);
  const RunReport r2 = loop.run(small);
  CHECK(r1.metrics == r2.metrics);  // same noise, same metrics: loop-side determinism
}

int run() {
  world_model::reg("noise_stub_small", []() -> std::unique_ptr<WorldModelAdapter> {
    return std::make_unique<NoisyPredictor>(kEpsSmall);
  });
  world_model::reg("noise_stub_big", []() -> std::unique_ptr<WorldModelAdapter> {
    return std::make_unique<NoisyPredictor>(kEpsBig);
  });
  test_comparator_pure_function();
  test_stub_registration();
  test_self_proof_both_directions();
  test_determinism_pairing();
  if (g_failures == 0) {
    std::printf("slice08: all tests passed\n");
    return 0;
  }
  std::printf("slice08: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice08

namespace slice09 {

void test_percentile_known_samples() {
  const StageSamples s{{"x", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}}};
  const auto p = percentiles(s);
  CHECK(p.count("x") == 1);
  CHECK(p.at("x").p50 == 5.5);  // rank 4.5 -> exact
  // Rank-linear interpolation written out so the double matches bit-for-bit.
  CHECK(p.at("x").p95 == 9.0 + (0.95 * 9.0 - 8.0) * (10.0 - 9.0));
  const StageSamples one{{"y", {2.5}}};
  const auto p1 = percentiles(one);
  CHECK(p1.at("y").p50 == 2.5);
  CHECK(p1.at("y").p95 == 2.5);  // single sample: the value itself
  const StageSamples none{{"z", {}}};
  const auto p0 = percentiles(none);
  CHECK(p0.at("z").p50 == 0.0);
  CHECK(p0.at("z").p95 == 0.0);
}

RunConfig lat_cfg() {
  RunConfig cfg{};
  cfg.ticks = 20;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  return cfg;
}

void test_samples_and_keys() {
  LoopDriver loop(4);
  const RunReport r = loop.run(lat_cfg());
  CHECK(r.stage_samples.count("truth_step") == 1);
  CHECK(r.stage_samples.count("render") == 1);
  CHECK(r.stage_samples.count("detect:blob_detect") == 1);
  CHECK(r.stage_samples.count("predict:linear_extrapolation") == 1);
  CHECK(r.stage_samples.count("adjudicate") == 1);
  CHECK(r.stage_samples.count("metrics") == 1);
  for (const auto& [k, v] : r.stage_samples) {
    CHECK(v.size() == static_cast<std::size_t>(20));  // one sample per tick
  }
  CHECK(r.stage_percentiles.size() == r.stage_samples.size());
  const auto keys = latency_keys(r);
  CHECK(keys.count("render.p50") == 1);
  CHECK(keys.count("detect:blob_detect.p95") == 1);
}

void test_budget_mount() {
  LoopDriver loop(4);
  const RunReport r = loop.run(lat_cfg());
  const auto keys = latency_keys(r);
  // Note: truth_step (one advance of 8 entities) often measures 0.0us at this
  // machine's timer resolution — a heavy stage demonstrates the mount instead.
  const double v = keys.at("render.p50");
  CHECK(v > 0.0);  // filling a 64x64 frame costs real microseconds
  // Budget semantics: compare the report's keys against a declared budget map.
  const std::map<std::string, double> budget{{"render.p50", v}};
  const std::vector<ToleranceWindow> generous{{"render.p50", v}};
  CHECK(compare_within_tolerance(keys, budget, generous).pass);  // delta 0
  const std::map<std::string, double> zero_budget{{"render.p50", 0.0}};
  const std::vector<ToleranceWindow> zero{{"render.p50", 0.0}};
  CHECK(!compare_within_tolerance(keys, zero_budget, zero).pass);  // v > 0
  const std::vector<ToleranceWindow> ghost{{"nope", 1.0}};
  const auto vm = compare_within_tolerance(keys, keys, ghost);
  CHECK(!vm.pass);
  CHECK(vm.deltas[0].second == kMissingDelta);
}

void test_json_and_wallclock_regression() {
  LoopDriver loop(4);
  const RunReport a = loop.run(lat_cfg());
  const RunReport b = loop.run(lat_cfg());
  CHECK(a.world_hash == b.world_hash);    // timing never leaks into hashes
  CHECK(a.ledger_hash == b.ledger_hash);
  const std::string j = run_report_json(a);
  CHECK(j.find("\"stage_percentiles\"") != std::string::npos);
  CHECK(j.find("\"render\"") != std::string::npos);
  CHECK(j.find("\"p50\"") != std::string::npos);
  CHECK(run_report_json(a) == run_report_json(a));  // same report: byte-identical
}

int run() {
  test_percentile_known_samples();
  test_samples_and_keys();
  test_budget_mount();
  test_json_and_wallclock_regression();
  if (g_failures == 0) {
    std::printf("slice09: all tests passed\n");
    return 0;
  }
  std::printf("slice09: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice09

namespace slice10 {

BeliefEntry ece_entry(const char* producer, EntityId target_id, double x,
                      double z, float conf, Tick t) {
  Proposal p;
  p.producer = producer;
  p.tick_observed = t;
  p.target = target_id;
  Transform tr;
  tr.position = Vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)};
  p.fields.transform = tr;
  p.confidence = conf;
  return BeliefEntry{std::move(p),
                     AdjudicationResult{VerdictKind::Matched, target_id, {}}, t};
}

// Feed n proposals: `correct_first` right (position = truth) then the rest
// wrong (position = truth + 0.5 on x, beyond the 0.1 correctness bar).
void feed_pattern(MetricsModule& m, const WorldState& ws, const char* producer,
                  int correct_first, int wrong_after, float conf) {
  BeliefLedger l;
  for (int i = 0; i < correct_first; ++i) {
    l.record(ece_entry(producer, 1, 0.5, 0.5, conf, 1));
  }
  for (int i = 0; i < wrong_after; ++i) {
    l.record(ece_entry(producer, 1, 1.0, 0.5, conf, 1));
  }
  m.feed(ws, l, 1);
}

void test_decomposed_exact_values() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
           Vec3{});

  MetricsModule m;  // two producers, same accuracy structure, different claims
  feed_pattern(m, ws, "honest", 7, 1, 0.875f);   // conf == accuracy
  feed_pattern(m, ws, "loud", 7, 1, 1.0f);       // overclaims the same errors

  const auto r = m.report();
  CHECK(r.count("ece") == 1);
  CHECK(r.count("ece:honest") == 1);
  CHECK(r.count("ece:loud") == 1);
  CHECK(r.at("ece:honest") == 0.0);    // calibrated: exact zero
  CHECK(r.at("ece:loud") == 0.125);    // |7/8 - 1.0|, exact
  CHECK(r.at("ece") == 0.0625);        // (0 + 0.125) / 2, equal counts
}

void test_aggregate_between_decomposed() {
  LoopDriver loop(4);
  RunConfig cfg{};
  cfg.ticks = 30;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {};  // perception-only: the aggregate then sits between the two detectors
  cfg.mode = RunMode::Reference;
  const auto r = loop.run(cfg).metrics;
  CHECK(r.count("ece") == 1);
  CHECK(r.count("ece:blob_detect") == 1);
  CHECK(r.count("ece:blob_detect_overconfident") == 1);
  const double hi = r.at("ece:blob_detect");
  const double lo = r.at("ece:blob_detect_overconfident");
  const double agg = r.at("ece");
  CHECK(hi > lo + 0.05);  // honest detector under-claims vs over-claiming one
  CHECK(agg >= lo && agg <= hi);  // aggregate stays inside the decomposed range
}

int run() {
  test_decomposed_exact_values();
  test_aggregate_between_decomposed();
  if (g_failures == 0) {
    std::printf("slice10: all tests passed\n");
    return 0;
  }
  std::printf("slice10: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice10

namespace slice11 {

BoundsPolicy no_bounds() { return BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}; }
BoundsPolicy wall(float m) { return BoundsPolicy{BoundsPolicy::Kind::Wall, m}; }

RunConfig bounds_cfg(BoundsPolicy b, Tick ticks = 50) {
  RunConfig cfg{};
  cfg.ticks = ticks;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {};
  cfg.mode = RunMode::Reference;
  cfg.bounds = b;
  return cfg;
}

void test_none_mode_baseline_hashes() {
  LoopDriver loop(8);
  RunConfig cfg = bounds_cfg(no_bounds(), 600);
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  const RunReport r = loop.run(cfg);
  CHECK(r.world_hash == 0x574361e62eec92b9ULL);   // alignment baseline, frozen
  CHECK(r.ledger_hash == 0x459ff85132cc103eULL);  // ... byte-for-byte
  CHECK(r.boundary_events == 0);
}

void test_wall_keeps_entities_in_bounds() {
  TruthWorld tw(3);
  WorldState ws(wall(0.02f));
  tw.build_demo_scene(ws, 8);
  tw.advance(ws, 200);
  for (const Entity& e : ws.entities()) {
    CHECK(e.transform.position.x >= 0.02f);
    CHECK(e.transform.position.x <= 0.98f);
    CHECK(e.transform.position.z >= 0.02f);
    CHECK(e.transform.position.z <= 0.98f);
  }
  CHECK(ws.boundary_events() > 0);
}

void test_single_reflection_exact() {
  TruthWorld tw(1);
  WorldState ws(wall(0.02f));
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.95f, 0.0f, 0.5f}, Quat{}},
           Vec3{0.05f, 0.0f, 0.0f});
  tw.advance(ws, 1);  // 0.95 + 0.05 crosses 0.98 -> mirrored
  const Entity* e = ws.find(1);
  CHECK(e != nullptr);
  if (e != nullptr) {
    CHECK(e->transform.position.x ==
          0.98f - ((0.95f + 0.05f) - 0.98f));  // same expr, same double
    CHECK(e->velocity.x == -0.05f);            // mirrored
    CHECK(ws.boundary_events() == 1);          // exactly one reflection
  }
}

void test_clamp_for_extreme_speed() {
  TruthWorld tw(1);
  WorldState ws(wall(0.02f));
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
           Vec3{2.5f, 0.0f, 0.0f});
  tw.advance(ws, 50);
  for (const Entity& e : ws.entities()) {
    CHECK(e.transform.position.x >= 0.02f);
    CHECK(e.transform.position.x <= 0.98f);
  }
  CHECK(ws.boundary_events() >= 1);  // reflected (and clamped), never dead
}

void test_wall_determinism_and_json() {
  LoopDriver loop(8);
  const RunReport a = loop.run(bounds_cfg(wall(0.02f)));
  const RunReport b = loop.run(bounds_cfg(wall(0.02f)));
  CHECK(a.world_hash == b.world_hash);
  CHECK(a.ledger_hash == b.ledger_hash);
  CHECK(a.metrics == b.metrics);
  CHECK(a.boundary_events > 0);
  const RunReport n = loop.run(bounds_cfg(no_bounds()));
  CHECK(n.world_hash != a.world_hash);  // different physics, different world
  const std::string j = run_report_json(a);
  CHECK(j.find("\"bounds\": \"wall\"") != std::string::npos);
  CHECK(j.find("\"boundary_events\"") != std::string::npos);
}

void test_parse_bounds() {
  std::string out;
  const char* argv[] = {"prog", "--bounds", "wall", "--margin", "0.02"};
  const auto parsed = parse_config(5, argv, out);
  CHECK(std::holds_alternative<RunConfig>(parsed));
  if (std::holds_alternative<RunConfig>(parsed)) {
    const RunConfig& c = std::get<RunConfig>(parsed);
    CHECK(c.bounds.kind == BoundsPolicy::Kind::Wall);
    CHECK(c.bounds.margin == 0.02f);
  }
  const char* bad[] = {"prog", "--bounds", "wall", "--margin", "0.9"};
  const auto err = parse_config(5, bad, out);
  CHECK(std::holds_alternative<std::string>(err));
  const char* bad2[] = {"prog", "--bounds", "bogus"};
  CHECK(std::holds_alternative<std::string>(parse_config(3, bad2, out)));
}

int run() {
  test_none_mode_baseline_hashes();
  test_wall_keeps_entities_in_bounds();
  test_single_reflection_exact();
  test_clamp_for_extreme_speed();
  test_wall_determinism_and_json();
  test_parse_bounds();
  if (g_failures == 0) {
    std::printf("slice11: all tests passed\n");
    return 0;
  }
  std::printf("slice11: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice11

namespace slice12 {

ObservationGenerator make_gen() {
  return ObservationGenerator{};  // renders at caller-requested resolutions
}

void test_camera_merged_into_observation() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{0.25f, 0.0f, 0.5f}, Quat{}},
           Vec3{});
  const RenderConfig cfg{32, 32, 0.02f};
  const Transform pose{Vec3{0.0f, 0.0f, 0.0f}, Quat{}};
  const Observation obs = make_gen().render(ws, 3, cfg, pose);
  CHECK(obs.view.width == 32);          // resolution rides on the camera
  CHECK(obs.view.height == 32);
  CHECK(obs.meta.width == obs.view.width);   // dims cannot disagree
  CHECK(obs.meta.height == obs.view.height);
  // Projection round-trip: world -> pixel -> world (pixel box centers).
  const float wx = obs.view.world_x(obs.view.pixel_x(0.25f));
  const float wz = obs.view.world_z(obs.view.pixel_z(0.5f));
  CHECK(wx - 0.25f < 0.05f && wx - 0.25f > -0.05f);  // one pixel of rounding
  CHECK(wz - 0.5f < 0.05f && wz - 0.5f > -0.05f);
}

void test_multi_resolution_render() {
  TruthWorld tw(1);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId id =
      tw.spawn(ws, ClassTag{"m"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}},
               Vec3{});
  ObservationGenerator gen;
  const Transform pose{Vec3{0.0f, 0.0f, 0.0f}, Quat{}};
  const int widths[] = {32, 64, 518, 640};  // DA-V2-S / RT-DETRv2-S inputs
  for (int w : widths) {
    const RenderConfig cfg{w, w, 0.02f};
    const Observation obs = gen.render(ws, 1, cfg, pose);
    CHECK(obs.rgb.size() ==
          static_cast<std::size_t>(w) * w * 3);  // buffer matches camera
    const Entity* e = ws.find(id);
    CHECK(e != nullptr);
    if (e != nullptr) {
      // The entity's footprint lands at the frame center at every resolution.
      const int cx = obs.view.pixel_x(e->transform.position.x);
      const int cz = obs.view.pixel_z(e->transform.position.z);
      CHECK(cx >= w / 2 - 2 && cx <= w / 2 + 2);  // footprint ~0.02 -> 1-2 px
      CHECK(cz >= w / 2 - 2 && cz <= w / 2 + 2);
    }
  }
}

void test_none_mode_reference_hashes_unchanged() {
  // Camera merge must not move one byte: 600/42 default scene baseline.
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 600;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport r = loop.run(cfg);
  CHECK(r.world_hash == 0x574361e62eec92b9ULL);
  CHECK(r.ledger_hash == 0x459ff85132cc103eULL);
}

void test_visibility_is_resolution_independent() {
  TruthWorld tw(5);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  const EntityId inside = tw.spawn(
      ws, ClassTag{"in"}, Transform{Vec3{0.5f, 0.0f, 0.5f}, Quat{}}, Vec3{});
  const EntityId outside = tw.spawn(
      ws, ClassTag{"out"}, Transform{Vec3{2.0f, 0.0f, 2.0f}, Quat{}}, Vec3{});
  ObservationGenerator gen;
  const Transform pose{Vec3{0.0f, 0.0f, 0.0f}, Quat{}};
  for (int w : {32, 64, 640}) {
    const Observation obs =
        gen.render(ws, 1, RenderConfig{w, w, 0.02f}, pose);
    bool found_in = false;
    bool found_out = false;
    for (const auto& [id, v] : obs.visibility) {
      if (id == inside) found_in = (v == Visibility::Seen);
      if (id == outside) found_out = (v == Visibility::Unseen);
    }
    CHECK(found_in);   // three-state verdicts are geometric, not resolution-dependent
    CHECK(found_out);
  }
}

void test_render_determinism_pairing() {
  TruthWorld tw(9);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.build_demo_scene(ws, 6);
  const RenderConfig cfg{96, 96, 0.05f};
  const Transform pose{Vec3{0.0f, 0.0f, 0.0f}, Quat{}};
  const Observation a = make_gen().render(ws, 11, cfg, pose);
  const Observation b = make_gen().render(ws, 11, cfg, pose);
  CHECK(a.view.width == b.view.width);    // camera fields compared too
  CHECK(a.rgb.size() == b.rgb.size());
  bool rgb_eq = a.rgb.size() == b.rgb.size();
  for (std::size_t i = 0; i < a.rgb.size(); ++i) {
    rgb_eq = rgb_eq && a.rgb[i] == b.rgb[i];
  }
  CHECK(rgb_eq);
}

int run() {
  test_camera_merged_into_observation();
  test_multi_resolution_render();
  test_none_mode_reference_hashes_unchanged();
  test_visibility_is_resolution_independent();
  test_render_determinism_pairing();
  if (g_failures == 0) {
    std::printf("slice12: all tests passed\n");
    return 0;
  }
  std::printf("slice12: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice12

namespace slice13 {

#if OW_WITH_ONNXRUNTIME

bool prop_eq(const Proposal& a, const Proposal& b) {
  if (a.producer != b.producer || a.tick_observed != b.tick_observed) {
    return false;
  }
  if (a.target.index() != b.target.index() || a.confidence != b.confidence) {
    return false;
  }
  if (a.fields.transform.has_value() != b.fields.transform.has_value()) {
    return false;
  }
  if (a.fields.transform) {
    if (a.fields.transform->position.x != b.fields.transform->position.x) {
      return false;
    }
    if (a.fields.transform->position.z != b.fields.transform->position.z) {
      return false;
    }
  }
  return true;
}

Observation synthetic_scene_obs() {
  TruthWorld tw(3);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.build_demo_scene(ws, 8);
  ObservationGenerator gen;
  return gen.render(ws, 5, RenderConfig{64, 64, 0.02f}, Transform{});
}

Observation blank_obs() {
  Observation o;
  o.tick = 1;
  o.view.width = 64;
  o.view.height = 64;
  o.rgb.assign(64 * 64 * 3, 0);
  for (std::size_t i = 0; i < 64 * 64; ++i) {
    o.rgb[i * 3 + 0] = kBackgroundRgb[0];
    o.rgb[i * 3 + 1] = kBackgroundRgb[1];
    o.rgb[i * 3 + 2] = kBackgroundRgb[2];
  }
  return o;
}

void test_rtdetr_smoke_and_determinism() {
  auto ad = perception::make("rtdetr_v2_s");
  CHECK(ad != nullptr);
  if (ad == nullptr) return;
  ad->set_execution_provider(ExecProvider::CpuEp);
  const Observation obs = synthetic_scene_obs();
  // L2 §6 test 1 asks for a recorded cost, not just "it ran": time both calls
  // so the one-time session build stays visible separately from steady state.
  const auto t0 = std::chrono::steady_clock::now();
  const auto a = ad->observe(obs);
  const auto t1 = std::chrono::steady_clock::now();
  const auto b = ad->observe(obs);
  const auto t2 = std::chrono::steady_clock::now();
  const auto ms = [](std::chrono::steady_clock::time_point x,
                     std::chrono::steady_clock::time_point y) {
    return std::chrono::duration<double, std::milli>(y - x).count();
  };
  std::printf("slice13: observe #1 %.1f ms (includes session build), #2 %.1f ms\n",
              ms(t0, t1), ms(t1, t2));
  CHECK(a.size() == b.size());
  bool eq = a.size() == b.size();
  for (std::size_t i = 0; i < a.size(); ++i) eq = eq && prop_eq(a[i], b[i]);
  CHECK(eq);  // CPU EP single-thread: reference-path determinism
  std::printf("slice13: detections=%zu on the 8-entity synthetic scene\n",
              a.size());
  for (const Proposal& p : a) {
    CHECK(p.confidence >= 0.3f);          // threshold applied
    CHECK(p.fields.transform.has_value());
    CHECK(p.fields.transform->position.x >= 0.0f);
    CHECK(p.fields.transform->position.x <= 1.0f);  // world-clamped
  }
}

void test_rtdetr_blank_frame_is_bounded() {
  auto ad = perception::make("rtdetr_v2_s");
  if (ad == nullptr) return;
  // Deviation from the C3 L2 AC "blank frame -> empty list". Measured
  // 2026-10-19 (Release): a flat background frame yields ONE spurious query at
  // sigmoid 0.330 (raw logit -0.707, self-reported class tvmonitor), i.e. above
  // the reference 0.3 threshold. A COCO-trained detector gives no
  // zero-false-positive guarantee on an out-of-domain frame -- the AC assumed a
  // property the model does not have (problem-log 008). The bound is the
  // measured behaviour plus slack, so a runaway regression (e.g. sigmoid
  // applied twice -> ~300 boxes) still fails loudly.
  const auto a = ad->observe(blank_obs());
  const auto b = ad->observe(blank_obs());
  std::printf("slice13: blank frame -> %zu spurious box(es)\n", a.size());
  CHECK(a.size() <= 5);
  CHECK(a.size() == b.size());  // and still deterministic
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
    CHECK(prop_eq(a[i], b[i]));
  }
}

// Real-photo check: proves the preprocessing/postprocessing actually match the
// reference processor (a wrong pipeline still "runs" and returns plausible-
// looking boxes). The asset is gitignored and optional -- set
// OW_RTDETR_TEST_IMAGE to a raw RGB file whose basename carries the
// dimensions, e.g. models/photo_640x480.rgb (a PNG decoded by the temp dev
// helper; any 8-bit RGB/RGBA PNG works).
void test_rtdetr_real_photo() {
  const char* path = std::getenv("OW_RTDETR_TEST_IMAGE");
  if (path == nullptr || *path == '\0') {
    std::printf("slice13: OW_RTDETR_TEST_IMAGE unset -> real-photo check skipped\n");
    return;
  }
  const std::string p(path);
  const std::size_t dot = p.find_last_of('.');
  const std::size_t end = dot == std::string::npos ? p.size() : dot;
  const std::size_t xpos = p.find_last_of('x', end);
  const std::size_t us = xpos == std::string::npos ? std::string::npos
                                                  : p.find_last_of('_', xpos);
  if (xpos == std::string::npos || us == std::string::npos) {
    std::printf("slice13: cannot parse '<W>x<H>' from '%s' -> skipped\n", path);
    return;
  }
  const int W = std::atoi(p.substr(us + 1, xpos - us - 1).c_str());
  const int H = std::atoi(p.substr(xpos + 1, end - xpos - 1).c_str());
  if (W <= 0 || H <= 0) {
    std::printf("slice13: bad dimensions in '%s' -> skipped\n", path);
    return;
  }
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    std::printf("slice13: cannot open '%s' -> skipped\n", path);
    return;
  }
  Observation o;
  o.tick = 1;
  o.view.width = W;
  o.view.height = H;
  o.rgb.resize(static_cast<std::size_t>(W) * H * 3);
  const std::size_t got = std::fread(o.rgb.data(), 1, o.rgb.size(), f);
  std::fclose(f);
  CHECK(got == o.rgb.size());

  auto ad = perception::make("rtdetr_v2_s");
  if (ad == nullptr) return;
  const auto det = ad->observe(o);
  float best = 0.0f;
  for (const Proposal& q : det) best = std::max(best, q.confidence);
  std::printf("slice13: real photo %dx%d -> %zu detection(s), max conf %.3f\n",
              W, H, det.size(), static_cast<double>(best));
  CHECK(!det.empty());  // a natural photo does contain objects
  CHECK(best >= 0.5f);  // and the model is confident about them
  for (const Proposal& q : det) {
    CHECK(q.fields.transform.has_value());
    CHECK(q.confidence >= 0.3f);
  }
}

// L2 §6 test 5: the provider hook must not disturb adapters that never own a
// GPU path. Blob adapters inherit the default no-op, and the loop now hands
// set_execution_provider to EVERY adapter -- so a CudaEp request has to leave
// their output untouched, or the interface extension broke the invariant.
void test_blob_adapter_unaffected_by_provider() {
  auto ad = perception::make("blob_detect");
  CHECK(ad != nullptr);
  if (ad == nullptr) return;
  const Observation obs = synthetic_scene_obs();
  const auto before = ad->observe(obs);
  ad->set_execution_provider(ExecProvider::CudaEp);
  const auto after = ad->observe(obs);
  bool eq = before.size() == after.size();
  for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
    eq = eq && prop_eq(before[i], after[i]);
  }
  CHECK(!before.empty());  // progress assertion: the bounded check below is
  CHECK(eq);               // meaningless if the adapter stopped detecting
  std::printf("slice13: blob_detect unchanged by set_execution_provider (%zu)\n",
              before.size());
}

void test_rtdetr_end_to_end_loop() {
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 5;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"rtdetr_v2_s"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport r = loop.run(cfg);
  CHECK(r.ticks_run == 5);   // the real adapter never halts the loop
  CHECK(r.adapter_faults == 0);
  CHECK(r.first_fault.empty());  // no swallowed fault (problem-log 009)
  // L2 §6 test 4 also asks for "时间字段有值": a real model must show up in the
  // latency record, not run invisibly.
  CHECK(r.latency_sum.render_ms > 0.0);
  CHECK(r.latency_sum.perceive_ms > 0.0);
  std::printf("slice13: e2e entries=%zu perceive_ms=%.1f (real detections over 5 ticks)\n",
              r.ledger_entries, r.latency_sum.perceive_ms);
}

#endif  // OW_WITH_ONNXRUNTIME

int run() {
#if OW_WITH_ONNXRUNTIME
  test_rtdetr_smoke_and_determinism();
  test_rtdetr_blank_frame_is_bounded();
  test_rtdetr_real_photo();
  test_blob_adapter_unaffected_by_provider();
  test_rtdetr_end_to_end_loop();
#endif
  if (g_failures == 0) {
    std::printf("slice13: all tests passed\n");
    return 0;
  }
  std::printf("slice13: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice13

namespace slice14 {

#if OW_WITH_ONNXRUNTIME

constexpr int kMaxDepthProposals = 32;  // 8x8 grid, strictly-above-median bound

bool same_props(const std::vector<Proposal>& a, const std::vector<Proposal>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].producer != b[i].producer || a[i].tick_observed != b[i].tick_observed) {
      return false;
    }
    if (a[i].target.index() != b[i].target.index()) return false;
    if (a[i].confidence != b[i].confidence) return false;
    if (!a[i].fields.transform || !b[i].fields.transform) return false;
    const Vec3& pa = a[i].fields.transform->position;
    const Vec3& pb = b[i].fields.transform->position;
    if (pa.x != pb.x || pa.y != pb.y || pa.z != pb.z) return false;
  }
  return true;
}

Observation depth_scene_obs() {
  TruthWorld tw(3);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  tw.build_demo_scene(ws, 8);
  ObservationGenerator gen;
  return gen.render(ws, 5, RenderConfig{64, 64, 0.02f}, Transform{});
}

// Reads a raw RGB8 file whose basename carries "<W>x<H>" (same asset convention
// as slice13's photo check; produced by tools/png_to_rgb.mjs). Returns false
// when the env var is unset -- the check is opt-in, never a hidden dependency.
bool load_photo(const char* env_name, Observation& out) {
  const char* path = std::getenv(env_name);
  if (path == nullptr || *path == '\0') return false;
  const std::string p(path);
  const std::size_t dot = p.find_last_of('.');
  const std::size_t end = dot == std::string::npos ? p.size() : dot;
  const std::size_t xpos = p.find_last_of('x', end);
  const std::size_t us =
      xpos == std::string::npos ? std::string::npos : p.find_last_of('_', xpos);
  if (xpos == std::string::npos || us == std::string::npos) return false;
  const int W = std::atoi(p.substr(us + 1, xpos - us - 1).c_str());
  const int H = std::atoi(p.substr(xpos + 1, end - xpos - 1).c_str());
  if (W <= 0 || H <= 0) return false;
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return false;
  out.tick = 1;
  out.view.width = W;
  out.view.height = H;
  out.rgb.resize(static_cast<std::size_t>(W) * H * 3);
  const std::size_t got = std::fread(out.rgb.data(), 1, out.rgb.size(), f);
  std::fclose(f);
  return got == out.rgb.size();
}

// observe() that reports a throw as a named failure. An uncaught exception
// here would kill the process with no message at all -- the exact failure mode
// problem-log 009 is about, and this adapter's model I/O has never been
// verified against the real file.
bool try_observe(PerceptionAdapter& ad, const Observation& obs,
                 std::vector<Proposal>& out) {
  try {
    out = ad.observe(obs);
  } catch (const std::exception& e) {
    std::printf("slice14: %s observe threw: %s\n", ad.name(), e.what());
    CHECK(false);
    return false;
  }
  return true;
}

void test_depth_smoke_and_bound() {
  auto ad = perception::make("depth_anything_v2_s");
  CHECK(ad != nullptr);
  if (ad == nullptr) return;
  ad->set_execution_provider(ExecProvider::CpuEp);
  const Observation obs = depth_scene_obs();
  std::vector<Proposal> a, b;
  const auto t0 = std::chrono::steady_clock::now();
  if (!try_observe(*ad, obs, a)) return;
  const auto t1 = std::chrono::steady_clock::now();
  if (!try_observe(*ad, obs, b)) return;
  const auto t2 = std::chrono::steady_clock::now();
  const auto ms = [](std::chrono::steady_clock::time_point x,
                     std::chrono::steady_clock::time_point y) {
    return std::chrono::duration<double, std::milli>(y - x).count();
  };
  std::printf("slice14: observe #1 %.1f ms (includes session build), #2 %.1f ms\n",
              ms(t0, t1), ms(t1, t2));
  std::printf("slice14: proposals=%zu on the 64x64 synthetic scene\n", a.size());
  // L2 §3.2: the reduction is bounded by construction, and it is deterministic.
  CHECK(a.size() <= kMaxDepthProposals);
  CHECK(same_props(a, b));
  for (const Proposal& p : a) {
    CHECK(std::holds_alternative<NewEntity>(p.target));
    CHECK(p.fields.transform.has_value());
    CHECK(p.confidence >= 0.0f && p.confidence <= 1.0f);
    CHECK(p.fields.transform->position.x >= 0.0f);
    CHECK(p.fields.transform->position.x <= 1.0f);
    CHECK(p.fields.transform->position.z >= 0.0f);
    CHECK(p.fields.transform->position.z <= 1.0f);
    CHECK(!p.fields.class_tag.has_value());  // adapters must not mint classes
  }
}

// Flat frame: what the model does with it is NOT predictable (problem-log 008
// says a uniform input does not entitle us to expect a uniform output -- a ViT
// can manufacture structure out of positional encoding, and measured on 2026-10-19
// it does: a pure-gray frame yields 32 proposals). So this asserts only what
// the reducer guarantees: a bounded count and determinism.
void test_depth_flat_frame_is_bounded() {
  auto ad = perception::make("depth_anything_v2_s");
  if (ad == nullptr) return;
  Observation o;
  o.tick = 1;
  o.view.width = 64;
  o.view.height = 64;
  o.rgb.assign(64 * 64 * 3, 0x80);
  std::vector<Proposal> a, b;
  if (!try_observe(*ad, o, a)) return;
  if (!try_observe(*ad, o, b)) return;
  std::printf("slice14: uniform gray frame -> %zu proposal(s)\n", a.size());
  CHECK(a.size() <= kMaxDepthProposals);
  CHECK(same_props(a, b));
}

// Real-image check (L2 §6 test 3). What is asserted here is polarity-free:
// a natural photo must NOT collapse to a degenerate map, and the upper and
// lower halves must rank apart. The DIRECTION (which half is nearer) is a
// measured fact recorded in the C4 report, not an assumption baked in here --
// DA-V2's output is relative and its sign convention was unverified at design
// time (L2 §9 R-C4b).
void test_depth_real_photo_ordering() {
  Observation o;
  if (!load_photo("OW_DA_TEST_IMAGE", o)) {
    std::printf("slice14: OW_DA_TEST_IMAGE unset/skipped\n");
    return;
  }
  auto ad = perception::make("depth_anything_v2_s");
  if (ad == nullptr) return;
  std::vector<Proposal> a;
  if (!try_observe(*ad, o, a)) return;
  std::printf("slice14: real photo %dx%d -> %zu depth proposal(s)\n",
              o.view.width, o.view.height, a.size());
  CHECK(!a.empty());
  CHECK(a.size() <= kMaxDepthProposals);
  float upper = 0.0f, lower = 0.0f;
  int nu = 0, nl = 0;
  for (const Proposal& p : a) {
    const float z = p.fields.transform->position.z;
    if (z < 0.5f) { upper += p.confidence; ++nu; } else { lower += p.confidence; ++nl; }
  }
  std::printf("slice14: above-midline cells=%d mean conf %.3f | below=%d mean %.3f\n",
              nu, nu ? upper / nu : 0.0f, nl, nl ? lower / nl : 0.0f);
  CHECK(nu > 0 && nl > 0);           // the map is not one flat band
  CHECK(upper != lower);             // ... and the two halves rank apart
}

void test_depth_end_to_end_loop() {
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 3;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"rtdetr_v2_s", "depth_anything_v2_s"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport r = loop.run(cfg);
  CHECK(r.ticks_run == 3);
  CHECK(r.adapter_faults == 0);
  CHECK(r.first_fault.empty());
  CHECK(r.latency_sum.perceive_ms > 0.0);
  // Two different output families (sparse boxes + dense map) in ONE panel is
  // the actual claim of this slice: both must reach the ledger side by side.
  CHECK(r.metrics.count("ece:rtdetr_v2_s") > 0);
  CHECK(r.metrics.count("ece:depth_anything_v2_s") > 0);
  std::printf("slice14: e2e entries=%zu (two model families in one panel)\n",
              r.ledger_entries);
}

#endif  // OW_WITH_ONNXRUNTIME

int run() {
#if OW_WITH_ONNXRUNTIME
  test_depth_smoke_and_bound();
  test_depth_flat_frame_is_bounded();
  test_depth_real_photo_ordering();
  test_depth_end_to_end_loop();
#endif
  if (g_failures == 0) {
    std::printf("slice14: all tests passed\n");
    return 0;
  }
  std::printf("slice14: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice14

namespace slice17 {

// Frame supply (stage D D2). Every fixture is generated here on the fly: the
// test must not depend on a gitignored asset (C3's lesson), and a raw RGB8
// sequence is trivial to synthesise.
std::filesystem::path seq_dir() {
  auto d = std::filesystem::temp_directory_path() / "ow_slice17_seq";
  std::filesystem::create_directories(d);
  return d;
}

// Writes n frames and a manifest; returns the manifest path as a string.
std::string write_sequence(int w, int h, int n, int base) {
  const auto d = seq_dir();
  std::string man = "width=" + std::to_string(w) + "\nheight=" + std::to_string(h) +
                    "\nrgb=rgb8\n# generated by slice17\n";
  for (int i = 0; i < n; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%03d.rgb", i);
    const std::string p = (d / name).string();
    std::FILE* f = std::fopen(p.c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) return std::string();
    const std::vector<std::uint8_t> px(
        static_cast<std::size_t>(w) * h * 3,
        static_cast<std::uint8_t>(base + i * 10));
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
    man += name;
    man += "\n";
  }
  const std::string mp = (d / "manifest.txt").string();
  std::FILE* f = std::fopen(mp.c_str(), "wb");
  CHECK(f != nullptr);
  if (f == nullptr) return std::string();
  std::fwrite(man.data(), 1, man.size(), f);
  std::fclose(f);
  return mp;
}

void test_reads_frames() {
  const std::string man = write_sequence(8, 6, 3, 10);
  if (man.empty()) return;
  auto src = make_file_frame_source(man);
  CHECK(src != nullptr);
  if (src == nullptr) return;
  CHECK(src->frame_count() == 3);
  for (int i = 0; i < 3; ++i) {
    Observation o;
    const bool ok = src->next(o);
    CHECK(ok);
    CHECK(o.view.width == 8);
    CHECK(o.view.height == 6);
    CHECK(o.sensor_kind == "rgb_video");
    CHECK(o.meta.format == "RGB8");
    CHECK(o.rgb.size() == 8u * 6u * 3u);
    // uniform frame: every byte equals base + i*10
    const std::uint8_t want = static_cast<std::uint8_t>(10 + i * 10);
    bool uniform = !o.rgb.empty();
    for (std::uint8_t b : o.rgb) uniform = uniform && (b == want);
    CHECK(uniform);
    CHECK(o.tick == 0);  // the source must not own time
  }
  Observation extra;
  CHECK(!src->next(extra));  // end of stream is a value, not an exception
}

void test_loop_stops_at_end_of_stream() {
  const std::string man = write_sequence(8, 6, 4, 30);
  if (man.empty()) return;
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 10;  // more ticks than frames
  cfg.seed = 42;
  cfg.width = 8;
  cfg.height = 6;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  cfg.frames = man;
  const RunReport r = loop.run(cfg);
  CHECK(r.ticks_run == 4);        // the stream, not the requested 10
  CHECK(r.adapter_faults == 0);
  CHECK(r.stage_samples.count("frame_fetch") == 1);
  CHECK(r.stage_samples.at("frame_fetch").size() == 4);
  CHECK(r.stage_samples.count("render") == 0);  // the synthetic path was not used
  std::printf("slice17: loop consumed %llu frames, faults=0\n",
              static_cast<unsigned long long>(r.ticks_run));
}

void test_determinism() {
  const std::string man = write_sequence(8, 6, 3, 50);
  if (man.empty()) return;
  auto a = make_file_frame_source(man);
  auto b = make_file_frame_source(man);
  if (!a || !b) return;
  Observation oa, ob;
  while (a->next(oa) && b->next(ob)) {
    CHECK(oa.rgb == ob.rgb);
    CHECK(oa.view.width == ob.view.width);
  }
}

void test_errors_are_named() {
  const auto d = seq_dir();
  // missing width=
  const std::string bad = (d / "bad_manifest.txt").string();
  if (std::FILE* f = std::fopen(bad.c_str(), "wb")) {
    const std::string text = "height=6\nframe_000.rgb\n";
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
  }
  bool threw = false;
  try {
    make_file_frame_source(bad);
  } catch (const std::exception& e) {
    threw = true;
    CHECK(std::string(e.what()).find("width") != std::string::npos);
  }
  CHECK(threw);

  // frame with the wrong byte count
  const std::string man = write_sequence(8, 6, 1, 70);
  if (man.empty()) return;
  const std::string shortf = (d / "frame_000.rgb").string();
  if (std::FILE* f = std::fopen(shortf.c_str(), "wb")) {
    const std::uint8_t one[3] = {1, 2, 3};
    std::fwrite(one, 1, sizeof(one), f);
    std::fclose(f);
  }
  auto src = make_file_frame_source(man);
  if (!src) return;
  threw = false;
  try {
    Observation o;
    src->next(o);
  } catch (const std::exception& e) {
    threw = true;
    const std::string msg = e.what();
    CHECK(msg.find("expected") != std::string::npos);
  }
  CHECK(threw);
}

int run() {
  test_reads_frames();
  test_loop_stops_at_end_of_stream();
  test_determinism();
  test_errors_are_named();
  if (g_failures == 0) {
    std::printf("slice17: all tests passed\n");
    return 0;
  }
  std::printf("slice17: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice17

namespace slice15 {

void test_parse_ep() {
  std::string out;
  const char* argv[] = {"prog", "--ep", "cpu"};
  const auto a = parse_config(3, argv, out);
  CHECK(std::holds_alternative<RunConfig>(a));
  if (std::holds_alternative<RunConfig>(a)) {
    CHECK(std::get<RunConfig>(a).ep == ExecProvider::CpuEp);
  }
  const char* argv2[] = {"prog", "--ep", "cuda"};
  const auto b = parse_config(3, argv2, out);
  CHECK(std::holds_alternative<RunConfig>(b));
  if (std::holds_alternative<RunConfig>(b)) {
    CHECK(std::get<RunConfig>(b).ep == ExecProvider::CudaEp);
  }
  const char* argv3[] = {"prog"};
  const auto c = parse_config(1, argv3, out);
  if (std::holds_alternative<RunConfig>(c)) {
    CHECK(std::get<RunConfig>(c).ep == ExecProvider::CpuEp);  // CLI default
  }
  const char* bad[] = {"prog", "--ep", "trt"};
  CHECK(std::holds_alternative<std::string>(parse_config(3, bad, out)));
}

int run() {
  test_parse_ep();
  if (g_failures == 0) {
    std::printf("slice15: all tests passed\n");
    return 0;
  }
  std::printf("slice15: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice15

namespace slice16 {

// C5: the frozen criterion-(c) panel. These literals are the 2026-10-19
// measurement of the default panel at 600 ticks / seed 42. They pin two things
// at once: that the per-producer decomposition exists and DIFFERS (the
// aggregate-only failure of problem-log 005), and that its exact values
// reproduce. What is deliberately NOT asserted is the 0.30 verdict -- encoding
// "we are PARTIAL" as a test would turn a failed criterion into a green light
// (L2 §6 test 3).
constexpr double kEceHonest = 0.19890412565780013;
constexpr double kEceInjected = 0.009999990463256836;
constexpr double kEceAggregate = 0.4940744921482492;
// Values are bit-exact on this toolchain; the epsilon exists so a future FP
// reassociation surfaces as a named failure rather than an unexplained
// equality miss. CHECK prints the condition text only, so the delta is
// reported by the success line below.
constexpr double kEps = 1e-12;

void test_frozen_panel_reproduces() {
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 600;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  // The panel the CLI defaults to (driver.cpp parse_config), spelled out here
  // because RunConfig itself carries no defaults (L1 §6). Verified identical:
  // the CLI run in the C5 report reproduces these same literals.
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport r = loop.run(cfg);

  CHECK(r.world_hash == 0x574361e62eec92b9ULL);
  CHECK(r.ledger_hash == 0x459ff85132cc103eULL);
  CHECK(r.ledger_entries == 9746);
  CHECK(r.adapter_faults == 0);
  CHECK(r.first_fault.empty());

  const bool has_honest = r.metrics.count("ece:blob_detect") == 1;
  const bool has_injected = r.metrics.count("ece:blob_detect_overconfident") == 1;
  CHECK(has_honest);
  CHECK(has_injected);
  if (!has_honest || !has_injected) return;
  const double honest = r.metrics.at("ece:blob_detect");
  const double injected = r.metrics.at("ece:blob_detect_overconfident");
  CHECK(std::fabs(honest - kEceHonest) < kEps);
  CHECK(std::fabs(injected - kEceInjected) < kEps);
  CHECK(std::fabs(r.metrics.at("ece") - kEceAggregate) < kEps);
  CHECK(honest != injected);  // decomposition is not collapsed into one pool
  // Facts only: the 0.30 comparison belongs in the report, not in test output
  // (L2 §6 test 3 -- a verdict must not be able to read as a green light).
  std::printf("slice16: honest ECE %.6f | injected ECE %.6f | separation %.6f\n",
              honest, injected, honest - injected);
}

int run() {
  test_frozen_panel_reproduces();
  if (g_failures == 0) {
    std::printf("slice16: all tests passed\n");
    return 0;
  }
  std::printf("slice16: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice16

namespace slice18 {

// Annotation truth (stage D D3). Fixtures are written here on the fly: the
// dataset is gitignored, so a test that needed it would not exist on a fresh
// clone (slice17's rule, unchanged).
const std::filesystem::path& ann_dir() {
  static auto d = [] {
    auto p = std::filesystem::temp_directory_path() / "ow_slice18_ann";
    std::filesystem::create_directories(p);
    return p;
  }();
  return d;
}

std::string ann_header() {
  return "# annotation manifest (fixture)\n"
         "dataset=fixture subset\n"
         "license=CC0 (fixture)\n"
         "width=8\nheight=6\n"
         "categories=person,bicycle,car\n";
}

// Writes the body onto the standard header; returns the manifest path.
std::string write_ann(const std::string& body, const char* name = "truth.txt") {
  const std::string text = ann_header() + body;
  const std::string p = (ann_dir() / name).string();
  std::FILE* f = std::fopen(p.c_str(), "wb");
  CHECK(f != nullptr);
  if (f == nullptr) return std::string();
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  return p;
}

// Writes exactly what is given (for cases whose header must differ).
std::string write_raw(const std::string& text, const char* name) {
  const std::string p = (ann_dir() / name).string();
  std::FILE* f = std::fopen(p.c_str(), "wb");
  CHECK(f != nullptr);
  if (f == nullptr) return std::string();
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  return p;
}

bool near(float a, float b) { return std::abs(a - b) < 1e-6f; }

RunConfig fixture_cfg(const std::string& frames, const std::string& ann) {
  RunConfig cfg{};
  cfg.ticks = 10;  // more ticks than the fixtures hold: the streams decide
  cfg.seed = 42;
  cfg.width = 8;
  cfg.height = 6;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  cfg.frames = frames;
  cfg.annotations = ann;
  return cfg;
}

// 1: loading behaviour -- positions are the normalized box centers, ids keep
// minting across scene swaps, velocity is zero, and the caller owns the tick.
void test_loading_behaviour() {
  const std::string man = write_ann(
      "tick=1\n"
      "box 0.10 0.20 0.30 0.40 person 11 0\n"
      "box 0.50 0.50 0.60 0.70 car 12 0\n"
      "tick=2\n"
      "box 0.00 0.00 0.20 0.20 bicycle 21 0\n");
  if (man.empty()) return;
  AnnotationSet set;
  try {
    set = load_annotation_set(man);
  } catch (const std::exception& e) {
    ++g_failures;
    std::printf("FAIL slice18: unexpected load error: %s\n", e.what());
    return;
  }
  CHECK(set.frames.size() == 2);
  CHECK(set.dataset == "fixture subset");
  CHECK(set.categories.size() == 3);
  CHECK(set.boxes_kept() == 3);

  TruthWorld truth(7);
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  CHECK(truth.load_annotations(ws, set, 1));
  CHECK(ws.tick() == 1);  // named by the caller, not carried by the data
  CHECK(ws.size() == 2);
  CHECK(ws.boundary_events() == 0);  // no kinematics, so no reflections
  if (ws.size() == 2) {
    const Entity& a = ws.entities()[0];
    const Entity& b = ws.entities()[1];
    CHECK(near(a.transform.position.x, 0.2f));  // (0.10 + 0.30) / 2
    CHECK(near(a.transform.position.z, 0.3f));  // (0.20 + 0.40) / 2
    CHECK(a.transform.position.y == 0.0f);      // the truth plane stays y == 0
    CHECK(a.velocity.x == 0.0f && a.velocity.y == 0.0f && a.velocity.z == 0.0f);
    CHECK(a.class_tag.value == "person");
    CHECK(a.id == 1 && b.id == 2);
    const EntityId last_of_frame1 = b.id;
    CHECK(truth.load_annotations(ws, set, 2));
    CHECK(ws.tick() == 2);
    CHECK(ws.size() == 1);  // the scene is replaced, not accumulated
    CHECK(!truth.load_annotations(ws, set, 3));  // end of truth is a value
    CHECK(ws.size() == 1);  // a failed load changes nothing
    if (ws.size() == 1) {
      // Contract 2: ids stay monotonic across scene swaps (never reused).
      CHECK(ws.entities()[0].id == last_of_frame1 + 1);
    }
  }
}

// 2: the frozen protocol (L2 sec 4) drops with counters, never silently.
void test_protocol_rules() {
  const std::string man = write_ann(
      "tick=1\n"
      "box 0.10 0.10 0.20 0.20 person 11 1\n"       // rule 1: crowd -> dropped
      "box -0.50 -0.50 -0.10 0.50 person 12 0\n"    // rule 2: outside -> dropped
      "box 0.40 0.40 0.4000001 0.4000001 car 13 0\n"  // rule 2: sub-pixel
      "box 0.60 0.60 0.90 0.90 bicycle 14 0\n");
  if (man.empty()) return;
  AnnotationSet set = load_annotation_set(man);
  CHECK(set.frames.size() == 1);
  CHECK(set.frames[0].boxes.size() == 1);
  CHECK(set.dropped_crowd == 1);
  CHECK(set.dropped_degenerate == 2);
  CHECK(set.dropped_over_cap == 0);
  CHECK(set.boxes_kept() == 1);
  if (!set.frames[0].boxes.empty()) {
    // Truncation: the kept box is the visible part, unclamped here.
    CHECK(near(set.frames[0].boxes[0].u_min, 0.6f));
    CHECK(near(set.frames[0].boxes[0].v_max, 0.9f));
  }

  // Rule 3: cap per frame, kept by ascending source_id (size-neutral choice).
  std::string many = "tick=1\n";
  for (int i = 0; i < static_cast<int>(kAnnotationPerFrameCap) + 5; ++i) {
    const float u = 0.05f + 0.01f * static_cast<float>(i);
    many += "box " + std::to_string(u) + " 0.10 " + std::to_string(u + 0.02f) +
            " 0.20 person " + std::to_string(100 + i) + " 0\n";
  }
  const std::string cap_man = write_ann(many, "cap.txt");
  if (cap_man.empty()) return;
  const AnnotationSet cap = load_annotation_set(cap_man);
  CHECK(cap.frames[0].boxes.size() == kAnnotationPerFrameCap);
  CHECK(cap.dropped_over_cap == 5);
  if (cap.frames[0].boxes.size() == kAnnotationPerFrameCap) {
    CHECK(cap.frames[0].boxes.front().source_id == 100);
    CHECK(cap.frames[0].boxes.back().source_id ==
          100 + static_cast<std::uint64_t>(kAnnotationPerFrameCap) - 1);
  }

  // Rule 4: a class outside the declared table is an error, not a skip.
  const std::string bad_cls = write_ann(
      "tick=1\nbox 0.10 0.10 0.20 0.20 airplane 11 0\n", "bad_class.txt");
  if (bad_cls.empty()) return;
  bool threw = false;
  try {
    load_annotation_set(bad_cls);
  } catch (const std::exception& e) {
    threw = true;
    CHECK(std::string(e.what()).find("airplane") != std::string::npos);
  }
  CHECK(threw);
}

// 3/5: every malformed input is a named error (path + reason), never a silent
// narrowing of the truth.
void test_errors_are_named() {
  struct Case {
    const char* name;
    std::string body;
    const char* needle;
  };
  const Case cases[] = {
      {"gap.txt", "tick=1\nbox 0.1 0.1 0.2 0.2 person 11 0\ntick=3\n", "tick"},
      {"dup.txt", "tick=1\nbox 0.1 0.1 0.2 0.2 person 11 0\n"
                  "box 0.3 0.3 0.4 0.4 person 11 0\n",
       "source_id"},
      {"fields.txt", "tick=1\nbox 0.1 0.1 0.2 person 11 0\n", "box"},
      {"crowd.txt", "tick=1\nbox 0.1 0.1 0.2 0.2 person 11 2\n", "crowd"},
      {"order.txt", "box 0.1 0.1 0.2 0.2 person 11 0\n", "tick"},
      {"no_categories.txt", "", "categories"},  // raw body below
  };
  for (const Case& c : cases) {
    const std::string body =
        c.body.empty() ? "dataset=fixture\nlicense=CC0\nwidth=8\nheight=6\n"
                         "tick=1\nbox 0.1 0.1 0.2 0.2 person 11 0\n"
                       : c.body;
    const std::string full =
        c.body.empty() ? body : ann_header() + body;
    const std::string p = write_raw(full, c.name);
    if (p.empty()) continue;
    bool threw = false;
    try {
      load_annotation_set(p);
    } catch (const std::exception& e) {
      threw = true;
      const std::string msg = e.what();
      CHECK(msg.find(c.needle) != std::string::npos);
      CHECK(msg.find(p) != std::string::npos);  // names the file it failed on
    }
    CHECK(threw);
  }

  // An unknown header key is an error: a key nobody reads is a lie about format.
  const std::string bogus = write_ann("tick=1\nunits=mm\n", "bogus_key.txt");
  if (!bogus.empty()) {
    bool threw = false;
    try {
      load_annotation_set(bogus);
    } catch (const std::exception& e) {
      threw = true;
      CHECK(std::string(e.what()).find("units") != std::string::npos);
    }
    CHECK(threw);
  }
}

// 2b: the two protocol behaviours the real panel happens not to exercise --
// a truncated box (rule 2's visible-center half) and a frame that empties out.
void test_truncation_and_empty_frame() {
  const std::string trunc = write_ann(
      "tick=1\nbox -0.10 0.20 0.30 0.40 person 11 0\n", "truncated.txt");
  if (!trunc.empty()) {
    const AnnotationSet t = load_annotation_set(trunc);
    CHECK(t.frames[0].boxes.size() == 1);
    CHECK(t.dropped_degenerate == 0);
    if (!t.frames[0].boxes.empty()) {
      const Annotation& b = t.frames[0].boxes[0];
      CHECK(near(b.u_min, 0.0f));  // clamped into the canvas
      CHECK(near(b.u_max, 0.30f));
      TruthWorld tw(1);
      WorldState s{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
      CHECK(tw.load_annotations(s, t, 1));
      if (s.size() == 1) {
        // The truth center is the VISIBLE center (0 + 0.3)/2, not the dataset's
        // (which would be 0.1): a detector can only be scored on what it saw.
        CHECK(near(s.entities()[0].transform.position.x, 0.15f));
        CHECK(near(s.entities()[0].transform.position.z, 0.3f));
      }
    }
  }

  const std::string emptied = write_ann(
      "tick=1\nbox 0.10 0.10 0.20 0.20 person 11 1\n"  // crowd -> dropped
      "tick=2\nbox 0.10 0.10 0.20 0.20 person 21 0\n",
      "all_crowd.txt");
  if (emptied.empty()) return;
  const AnnotationSet set = load_annotation_set(emptied);
  CHECK(set.frames[0].boxes.empty());       // whole frame emptied by the protocol
  CHECK(set.dropped_crowd == 1);
  TruthWorld tw(1);
  WorldState s{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  CHECK(tw.load_annotations(s, set, 1));    // legal: zero entities, not an error
  CHECK(s.size() == 0);
  CHECK(s.tick() == 1);
  CHECK(tw.load_annotations(s, set, 2));
  CHECK(s.size() == 1);
  CHECK(s.entities()[0].id == 1);  // the emptied tick consumed no id at all
}

// 4: determinism pair (problem-log 002 discipline) -- same bytes in, same
// hashes out.
void test_determinism() {
  const std::string ann = write_ann(
      "tick=1\nbox 0.10 0.20 0.30 0.40 person 11 0\n"
      "box 0.50 0.50 0.60 0.70 car 12 0\n"
      "tick=2\nbox 0.00 0.00 0.20 0.20 bicycle 21 0\n"
      "tick=3\nbox 0.30 0.30 0.40 0.40 person 31 0\n");
  const std::string frames = slice17::write_sequence(8, 6, 3, 20);
  if (ann.empty() || frames.empty()) return;
  LoopDriver loop(8);
  const RunReport a = loop.run(fixture_cfg(frames, ann));
  const RunReport b = loop.run(fixture_cfg(frames, ann));
  CHECK(a.ticks_run == 3);
  CHECK(a.world_hash == b.world_hash);
  CHECK(a.ledger_hash == b.ledger_hash);
  // The deterministic surface only: stage_percentiles are wall clock and
  // never were part of the byte-exact claim (ADR-0009, contract 3).
  CHECK(a.metrics == b.metrics);
}

// 6: in the loop -- observation and truth come from the same real frame, and
// the truth stage is named for what actually ran.
void test_in_loop() {
  const std::string ann = write_ann(
      "tick=1\nbox 0.10 0.20 0.30 0.40 person 11 0\n"
      "box 0.50 0.50 0.60 0.70 car 12 0\n"
      "tick=2\nbox 0.00 0.00 0.20 0.20 bicycle 21 0\n"
      "tick=3\nbox 0.30 0.30 0.40 0.40 person 31 0\n");
  const std::string frames = slice17::write_sequence(8, 6, 3, 20);
  if (ann.empty() || frames.empty()) return;
  LoopDriver loop(8);
  const RunReport r = loop.run(fixture_cfg(frames, ann));
  CHECK(r.ticks_run == 3);
  CHECK(r.adapter_faults == 0);
  CHECK(r.boundary_events == 0);
  CHECK(r.annotations_loaded);
  CHECK(r.annotation_dataset == "fixture subset");
  CHECK(r.annotation_frames == 3);
  CHECK(r.annotation_boxes == 4);  // 2 + 1 + 1 boxes, none dropped
  CHECK(r.dropped_crowd == 0);
  CHECK(r.stage_samples.count("truth_load") == 1);
  CHECK(r.stage_samples.count("truth_step") == 0);  // no kinematics ran
  CHECK(r.stage_samples.at("truth_load").size() == 3);
  CHECK(r.stage_samples.count("render") == 0);
  const std::string json = run_report_json(r);
  CHECK(json.find("\"annotation_truth\"") != std::string::npos);
  std::printf("slice18: %llu ticks on annotated truth, %llu boxes\n",
              static_cast<unsigned long long>(r.ticks_run),
              static_cast<unsigned long long>(r.annotation_boxes));
  // The shorter stream wins, and it is a normal finish (D2 semantics).
  const std::string few = write_ann("tick=1\nbox 0.10 0.20 0.30 0.40 person 1 0\n",
                                    "one_frame.txt");
  if (!few.empty()) {
    const RunReport s = loop.run(fixture_cfg(frames, few));
    CHECK(s.ticks_run == 1);
    CHECK(s.adapter_faults == 0);
  }

  // Annotated truth without a frame stream would render the truth it is
  // supposed to be scored against -- correct by construction, so it must not
  // run at all, even through the direct API (bypassing parse_config).
  {
    const std::string solo = write_ann(
        "tick=1\nbox 0.10 0.20 0.30 0.40 person 1 0\n", "no_frames.txt");
    if (!solo.empty()) {
      RunConfig cfg = fixture_cfg(std::string(), solo);
      bool threw = false;
      try {
        loop.run(cfg);
      } catch (const std::exception& e) {
        threw = true;
        CHECK(std::string(e.what()).find("--frames") != std::string::npos);
      }
      CHECK(threw);
    }
  }

  // Truth canvas and observation canvas must be the same image plane.
  const std::string wide = write_raw(
      "dataset=fixture subset\nlicense=CC0\nwidth=9\nheight=6\n"
      "categories=person\n"
      "tick=1\nbox 0.10 0.20 0.30 0.40 person 1 0\n",
      "wide_canvas.txt");
  if (!wide.empty()) {
    bool threw = false;
    try {
      loop.run(fixture_cfg(frames, wide));
    } catch (const std::exception& e) {
      threw = true;
      CHECK(std::string(e.what()).find("canvas") != std::string::npos);
    }
    CHECK(threw);
  }
}

// 7: the regression gate -- nothing changes when annotated truth is not asked
// for. The frozen literals live in slice11/12/16; the claim here is that the
// default artifact does not grow a section.
void test_default_path_unchanged() {
  RunConfig cfg = fixture_cfg(std::string(), std::string());
  cfg.ticks = 20;
  LoopDriver loop(8);
  const RunReport r = loop.run(cfg);
  CHECK(r.ticks_run == 20);
  CHECK(!r.annotations_loaded);
  CHECK(r.stage_samples.count("truth_step") == 1);
  CHECK(r.stage_samples.count("truth_load") == 0);
  CHECK(run_report_json(r).find("annotation_truth") == std::string::npos);
}

// 8: CLI surface -- annotated truth needs its matching frame sequence, and
// wall bounds would move the truth.
void test_cli_guards() {
  std::string out;
  const char* only_ann[] = {"omniworld_loop", "--annotations", "truth.txt"};
  auto a = parse_config(3, only_ann, out);
  CHECK(std::holds_alternative<std::string>(a));
  if (std::holds_alternative<std::string>(a)) {
    CHECK(std::get<std::string>(a).find("--frames") != std::string::npos);
  }
  const char* wall[] = {"omniworld_loop",   "--frames",      "f.txt",
                        "--annotations",     "truth.txt",
                        "--bounds",          "wall"};
  auto b = parse_config(7, wall, out);
  CHECK(std::holds_alternative<std::string>(b));
  const char* ok[] = {"omniworld_loop", "--frames", "f.txt", "--annotations",
                      "truth.txt"};
  auto c = parse_config(5, ok, out);
  CHECK(std::holds_alternative<RunConfig>(c));
  if (std::holds_alternative<RunConfig>(c)) {
    CHECK(std::get<RunConfig>(c).annotations == "truth.txt");
    CHECK(std::get<RunConfig>(c).frames == "f.txt");
  }
}

int run() {
  try {
    test_loading_behaviour();
    test_protocol_rules();
    test_truncation_and_empty_frame();
    test_errors_are_named();
    test_determinism();
    test_in_loop();
    test_default_path_unchanged();
    test_cli_guards();
  } catch (const std::exception& e) {
    // An escaping exception aborts the process on MSVC with no report at all
    // (problem-log 011), so a failure here must still name itself.
    ++g_failures;
    std::printf("FAIL slice18: unexpected exception escaped: %s\n", e.what());
  }
  if (g_failures == 0) {
    std::printf("slice18: all tests passed\n");
    return 0;
  }
  std::printf("slice18: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice18

namespace slice19 {

// Strict JSON syntax check. The artifact is the vehicle for every claim in this
// repo, so "it is parseable JSON" has to be asserted rather than assumed: a
// missing separator in the hand-rolled emitter produced a file that read fine
// as text (every substring check passed) and was not JSON at all.
struct JsonSyntax {
  const std::string& s;
  std::size_t i = 0;

  explicit JsonSyntax(const std::string& text) : s(text) {}

  void ws() {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
      ++i;
    }
  }
  bool lit(const char* w) {
    const std::size_t n = std::strlen(w);
    if (s.compare(i, n, w) != 0) return false;
    i += n;
    return true;
  }
  bool str() {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') return true;
      if (c == '\\') {
        if (i >= s.size()) return false;
        const char e = s[i++];
        if (std::strchr("\"\\/bfnrt", e) != nullptr) continue;
        if (e != 'u') return false;
        for (int k = 0; k < 4; ++k) {
          if (i >= s.size() ||
              std::isxdigit(static_cast<unsigned char>(s[i++])) == 0) {
            return false;
          }
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return false;  // a raw control character is not legal in a JSON string
      }
    }
    return false;
  }
  bool num() {
    const std::size_t start = i;
    if (i < s.size() && s[i] == '-') ++i;
    if (i >= s.size() ||
        std::isdigit(static_cast<unsigned char>(s[i])) == 0) {
      return false;
    }
    if (s[i] == '0') {
      ++i;
    } else {
      while (i < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
      }
    }
    if (i < s.size() && s[i] == '.') {
      ++i;
      if (i >= s.size() ||
          std::isdigit(static_cast<unsigned char>(s[i])) == 0) {
        return false;
      }
      while (i < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
      }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      ++i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
      if (i >= s.size() ||
          std::isdigit(static_cast<unsigned char>(s[i])) == 0) {
        return false;
      }
      while (i < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
      }
    }
    return i > start;
  }
  bool value() {
    ws();
    if (i >= s.size()) return false;
    switch (s[i]) {
      case '{': return object();
      case '[': return array();
      case '"': return str();
      case 't': return lit("true");
      case 'f': return lit("false");
      case 'n': return lit("null");
      default: return num();
    }
  }
  bool object() {
    ++i;  // '{'
    ws();
    if (i < s.size() && s[i] == '}') {
      ++i;
      return true;
    }
    for (;;) {
      ws();
      if (!str()) return false;
      ws();
      if (i >= s.size() || s[i] != ':') return false;
      ++i;
      if (!value()) return false;
      ws();
      if (i >= s.size()) return false;
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == '}') {
        ++i;
        return true;
      }
      return false;
    }
  }
  bool array() {
    ++i;  // '['
    ws();
    if (i < s.size() && s[i] == ']') {
      ++i;
      return true;
    }
    for (;;) {
      if (!value()) return false;
      ws();
      if (i >= s.size()) return false;
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == ']') {
        ++i;
        return true;
      }
      return false;
    }
  }
};

bool json_is_wellformed(const std::string& text) {
  JsonSyntax p{text};
  if (!p.value()) return false;
  p.ws();
  return p.i == text.size();
}

// Scoring space (stage D slice D4, ADR-0013). Two things are asserted here:
// that the two spaces really behave differently (a dispatch that only holds
// for one direction would still pass a one-sided test), and that the default
// space changes nothing -- the frozen oracle path stays bit-for-bit.
//
// What is deliberately NOT asserted: the criterion-(c) verdict. A verdict in
// a test turns a failed criterion into a green light (problem-log 012).
//
// The functions below are grouped by topic, so their order is not the L2 sec 7
// item order; every comment names its item, because the mapping is what
// matters here.

ViewCamera camera(int w, int h) { return ViewCamera{Transform{}, w, h}; }

ScoreContext score_ctx(ScoreSpace space, const ViewCamera& view) {
  ScoreContext c;
  c.space = space;
  c.view = view;
  return c;
}

BeliefEntry entry_at(float x, float z, float conf, EntityId truth_id, Tick t,
                     VerdictKind kind = VerdictKind::Matched,
                     const std::string& producer = "test") {
  Proposal p;
  p.producer = producer;
  p.tick_observed = t;
  p.target = NewEntity{};
  Transform tr;
  tr.position = Vec3{x, 0.0f, z};
  p.fields.transform = tr;
  p.confidence = conf;
  return BeliefEntry{std::move(p),
                     AdjudicationResult{kind, truth_id, {}}, t};
}

// One truth entity at a known position -- the whole fixture of this slice.
struct World1 {
  TruthWorld tw{1};
  WorldState ws{BoundsPolicy{BoundsPolicy::Kind::None, 0.0f}};
  EntityId e1 = 0;
  explicit World1(float x, float z) {
    e1 = tw.spawn(ws, ClassTag{"a"}, Transform{Vec3{x, 0.0f, z}, Quat{}}, Vec3{});
  }
};

// L2 sec 7 item 1: the two spaces differ, in both directions. The v1 camera is an identity
// normalized projection, so the only difference is one pixel of quantization:
// it can hide a small error (same pixel) or expose a slightly larger one
// (a pixel boundary between the two positions).
//
// Every number below is binary-exact, so the comparisons are exact too (the
// same discipline as slice05's 0.1875).
void test_spaces_differ_both_ways() {
  const ViewCamera v = camera(64, 64);
  {
    // truth 0.5 (pixel 32), claim 0.5078125 (pixel 32): same pixel.
    World1 w(0.5f, 0.5f);
    BeliefLedger l;
    l.record(entry_at(0.5078125f, 0.5f, 0.875f, w.e1, 1));
    MetricsModule mw, mo;
    mw.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
    mo.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));
    // Same pixel -> the observation space forgives the offset.
    CHECK(mw.report().at("divergence") == 0.0078125);
    CHECK(mo.report().at("divergence") == 0.0);
    CHECK(mw.report().at("divergence") != mo.report().at("divergence"));
  }
  {
    // truth 0.4998779296875 (pixel 31), claim 0.5 (pixel 32): the boundary
    // falls between two positions that are almost the same in world space.
    World1 w(0.4998779296875f, 0.5f);
    BeliefLedger l;
    l.record(entry_at(0.5f, 0.5f, 0.875f, w.e1, 1));
    MetricsModule mw, mo;
    mw.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
    mo.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));
    // Across a pixel boundary -> the observation space is the stricter one.
    CHECK(mw.report().at("divergence") == 0.0001220703125);
    CHECK(mo.report().at("divergence") == 0.015625);
    CHECK(mw.report().at("divergence") != mo.report().at("divergence"));
  }
  {
    // Coincident positions: both spaces agree (zero error).
    World1 w(0.5f, 0.5f);
    BeliefLedger l;
    l.record(entry_at(0.5f, 0.5f, 0.875f, w.e1, 1));
    MetricsModule mw, mo;
    mw.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
    mo.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));
    CHECK(mw.report().at("divergence") == 0.0);
    CHECK(mo.report().at("divergence") == 0.0);
  }
}

// L2 sec 7 item 2: R3 -- a truth outside the camera window is not comparable. The camera
// clamps to the border, so scoring it would fake a correct hit.
void test_out_of_window_is_not_scored() {
  const ViewCamera v = camera(64, 64);
  World1 w(1.4f, 0.5f);  // outside [0,1]^2
  BeliefLedger l;
  l.record(entry_at(1.4f, 0.5f, 0.875f, w.e1, 1));
  MetricsModule mw, mo;
  mw.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
  mo.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));

  // World space: scored as usual (zero distance, so "correct").
  CHECK(mw.report().at("divergence") == 0.0);
  CHECK(mw.report().at("divergence_n") == 1.0);
  CHECK(mw.report().at("ece") == 0.125);  // 1 correct, confidence 0.875

  // Observation space: not comparable -> no divergence sample, no correctness
  // credit -- but the claim stays in the ECE denominator.
  CHECK(mo.report().at("divergence") == 0.0);
  CHECK(mo.report().at("divergence_n") == 0.0);
  CHECK(mo.report().at("ece") == 0.875);  // 0 correct, confidence 0.875
  CHECK(mo.report().at("claims:test") == 1.0);

  // The other half of R3: the truth is INSIDE the window but the claim is not.
  // A claim off the frame would be clamped onto the border and could sit at
  // zero distance from a truth that happens to be on the border -- the same
  // fake-correctness the rule exists to prevent.
  {
    World1 w2(0.5f, 0.5f);  // inside
    BeliefLedger l2;
    l2.record(entry_at(1.4f, 0.5f, 0.875f, w2.e1, 1));  // claim outside
    MetricsModule m;
    m.feed(w2.ws, l2, 1, score_ctx(ScoreSpace::ObservationUV, v));
    CHECK(m.report().at("divergence") == 0.0);
    CHECK(m.report().at("divergence_n") == 0.0);
    CHECK(m.report().at("ece") == 0.875);  // 0 correct, still in the denominator
    CHECK(m.report().at("claims:test") == 1.0);
  }
}

// L2 sec 7 item 5: coverage -- matched / all claims, per producer and in aggregate. The
// empty ledger is 0.0, deliberately NOT 1.0 ("no belief anchored to the truth"
// is not "perfectly stable").
void test_coverage_semantics() {
  World1 w(0.5f, 0.5f);
  {
    BeliefLedger l;
    l.record(entry_at(0.5f, 0.5f, 0.9f, w.e1, 1, VerdictKind::Matched, "a"));
    l.record(entry_at(0.5f, 0.5f, 0.9f, 0, 1, VerdictKind::NewEntity, "a"));
    l.record(entry_at(0.5f, 0.5f, 0.9f, 0, 1, VerdictKind::Rejected, "b"));
    MetricsModule m;
    m.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, camera(64, 64)));
    const auto r = m.report();
    CHECK(r.at("claims:a") == 2.0);
    CHECK(r.at("claims:b") == 1.0);
    CHECK(r.at("coverage:a") == 0.5);
    CHECK(r.at("coverage:b") == 0.0);
    CHECK(r.at("coverage") == 1.0 / 3.0);
  }
  {
    BeliefLedger l;  // nothing claimed at all
    MetricsModule m;
    m.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, camera(64, 64)));
    const auto r = m.report();
    CHECK(r.at("coverage") == 0.0);
    CHECK(r.at("divergence") == 0.0);
    CHECK(r.at("divergence_n") == 0.0);
    CHECK(r.at("ece") == 0.0);
    CHECK(r.at("identity_stability") == 1.0);  // existing semantics, unchanged
  }
}

// L2 sec 7 item 6: per-producer divergence and its own denominator.
void test_per_producer_divergence() {
  const ViewCamera v = camera(64, 64);
  World1 w(0.5f, 0.5f);
  BeliefLedger l;
  l.record(entry_at(0.625f, 0.5f, 0.875f, w.e1, 1, VerdictKind::Matched, "a"));
  l.record(entry_at(0.75f, 0.5f, 0.875f, w.e1, 1, VerdictKind::Matched, "b"));
  MetricsModule m;
  m.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
  const auto r = m.report();
  CHECK(r.at("divergence") == 0.1875);  // (0.125 + 0.25) / 2
  CHECK(r.at("divergence:a") == 0.125);
  CHECK(r.at("divergence:b") == 0.25);
  CHECK(r.at("divergence_n") == 2.0);
  CHECK(r.at("divergence_n:a") == 1.0);
  CHECK(r.at("divergence_n:b") == 1.0);
}

// L2 sec 7 item 7: determinism, and the space never touches the truth or the ledger.
void test_determinism_and_hash_invariance() {
  const ViewCamera v = camera(64, 64);
  World1 w(0.5f, 0.5f);
  BeliefLedger l;
  l.record(entry_at(0.505f, 0.5f, 0.9f, w.e1, 1));
  MetricsModule a1, a2, b1, b2;
  a1.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
  a2.feed(w.ws, l, 1, score_ctx(ScoreSpace::WorldXZ, v));
  b1.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));
  b2.feed(w.ws, l, 1, score_ctx(ScoreSpace::ObservationUV, v));
  CHECK(a1.report() == a2.report());  // same space, same numbers
  CHECK(b1.report() == b2.report());
  CHECK(a1.report().at("divergence") != b1.report().at("divergence"));

  // The scoring space changes the metrics only: the ledger is written by the
  // adjudicator in world space, so the hash cannot move (R5).
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 30;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport rw = loop.run(cfg);
  cfg.score_space = ScoreSpace::ObservationUV;
  const RunReport ro = loop.run(cfg);
  CHECK(rw.world_hash == ro.world_hash);
  CHECK(rw.ledger_hash == ro.ledger_hash);
  CHECK(rw.ledger_entries == ro.ledger_entries);
  CHECK(rw.metrics.at("divergence") != ro.metrics.at("divergence"));
}

// L2 sec 7 item 3: the default space changes nothing. The frozen literals of slice05 /
// slice16 must reproduce bit-for-bit through the new dispatch.
void test_default_space_unchanged() {
  World1 w(0.5f, 0.5f);
  {
    BeliefLedger honest, loud;
    for (int i = 0; i < 7; ++i) {
      honest.record(entry_at(0.5f, 0.5f, 0.875f, w.e1, 3));
      loud.record(entry_at(0.5f, 0.5f, 1.0f, w.e1, 3));
    }
    honest.record(entry_at(0.0f, 0.5f, 0.875f, w.e1, 3));
    loud.record(entry_at(0.0f, 0.5f, 1.0f, w.e1, 3));
    MetricsModule ma, mb;
    ma.feed(w.ws, honest, 3);
    mb.feed(w.ws, loud, 3);
    CHECK(ma.report().at("ece") == 0.0);
    CHECK(mb.report().at("ece") == 0.125);
  }
  {
    World1 w2(0.5f, 0.5f);
    BeliefLedger l;
    l.record(entry_at(0.625f, 0.5f, 0.875f, w2.e1, 7));
    l.record(entry_at(0.75f, 0.5f, 0.875f, w2.e1, 7));
    MetricsModule m;
    m.feed(w2.ws, l, 7);
    CHECK(m.report().at("divergence") == 0.1875);
  }
}

// L2 sec 7 item 4: the new keys on the frozen 600/42 panel. These literals are the oracle
// for the additive keys: from here on they must not move.
//
// They were measured on the 2026-10-27 build (the D4 report quotes them);
// the existing keys are checked here too, because the new ones must not have
// dragged them along.
//
// The comparisons are exact, not "within a tolerance": the artifact prints
// doubles with %.17g, which round-trips, so a tolerance would hide exactly
// the kind of drift this oracle exists to catch.
void test_new_keys_frozen_on_600_42() {
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 600;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect", "blob_detect_overconfident"};
  cfg.world_models = {"zero_delta", "linear_extrapolation"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  const RunReport r = loop.run(cfg);
  // The oracle itself must not have moved.
  CHECK(r.world_hash == 0x574361e62eec92b9ULL);
  CHECK(r.ledger_hash == 0x459ff85132cc103eULL);
  CHECK(r.ledger_entries == 9746);

  // Existing keys: unchanged, bit-for-bit.
  CHECK(r.metrics.at("divergence") == 0.016946168653567753);
  CHECK(r.metrics.at("ece") == 0.4940744921482492);
  CHECK(r.metrics.at("ece:blob_detect") == 0.19890412565780013);
  CHECK(r.metrics.at("identity_stability") == 0.988313856427379);

  // New keys: coverage and its per-producer split.
  CHECK(r.metrics.at("coverage") == 1.0);
  CHECK(r.metrics.at("coverage:blob_detect") == 1.0);
  CHECK(r.metrics.at("coverage:blob_detect_overconfident") == 1.0);
  CHECK(r.metrics.at("coverage:linear_extrapolation") == 1.0);
  CHECK(r.metrics.at("coverage:zero_delta") == 1.0);
  // New keys: claim counts (the ECE sample size per producer).
  CHECK(r.metrics.at("claims:blob_detect") == 73.0);
  CHECK(r.metrics.at("claims:blob_detect_overconfident") == 73.0);
  CHECK(r.metrics.at("claims:linear_extrapolation") == 4800.0);
  CHECK(r.metrics.at("claims:zero_delta") == 4800.0);
  // New keys: per-producer divergence and its denominators.
  CHECK(r.metrics.at("divergence:blob_detect") == 0.0039453243150487528);
  CHECK(r.metrics.at("divergence:blob_detect_overconfident") == 0.0039453243150487528);
  CHECK(r.metrics.at("divergence:linear_extrapolation") == 0.03428777965576546);
  CHECK(r.metrics.at("divergence:zero_delta") == 0.0);
  CHECK(r.metrics.at("divergence_n") == 9746.0);
  CHECK(r.metrics.at("divergence_n:blob_detect") == 73.0);
  CHECK(r.metrics.at("divergence_n:blob_detect_overconfident") == 73.0);
  CHECK(r.metrics.at("divergence_n:linear_extrapolation") == 4800.0);
  CHECK(r.metrics.at("divergence_n:zero_delta") == 4800.0);
}

// L2 sec 7 item 8: the CLI surface.
void test_cli_score_space() {
  std::string out;
  const char* argv[] = {"prog", "--score-space", "observation"};
  const auto parsed = parse_config(3, argv, out);
  CHECK(std::holds_alternative<RunConfig>(parsed));
  if (std::holds_alternative<RunConfig>(parsed)) {
    CHECK(std::get<RunConfig>(parsed).score_space == ScoreSpace::ObservationUV);
  }
  const char* world[] = {"prog", "--score-space", "world"};
  const auto w = parse_config(3, world, out);
  CHECK(std::holds_alternative<RunConfig>(w));
  if (std::holds_alternative<RunConfig>(w)) {
    CHECK(std::get<RunConfig>(w).score_space == ScoreSpace::WorldXZ);
  }
  const char* bad[] = {"prog", "--score-space", "bogus"};
  const auto e = parse_config(3, bad, out);
  CHECK(std::holds_alternative<std::string>(e));
  if (std::holds_alternative<std::string>(e)) {
    const std::string msg = std::get<std::string>(e);
    CHECK(msg.find("--score-space") != std::string::npos);
    CHECK(msg.find("world|observation") != std::string::npos);
  }
  const char* none[] = {"prog", "--ticks", "10"};
  const auto d = parse_config(3, none, out);
  CHECK(std::holds_alternative<RunConfig>(d));
  if (std::holds_alternative<RunConfig>(d)) {
    CHECK(std::get<RunConfig>(d).score_space == ScoreSpace::WorldXZ);
  }
}

// L2 sec 7 item 9: the artifact states the space it scored in, the command line that made
// it, and the existing keys keep their values and their relative order.
void test_artifact_reports_space() {
  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 20;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  cfg.command_line = "omniworld_loop --ticks 20 --seed 42";
  const std::string jw = run_report_json(loop.run(cfg));
  CHECK(jw.find("\"score_space\": \"world\"") != std::string::npos);
  CHECK(jw.find("\"command_line\": \"omniworld_loop --ticks 20 --seed 42\"") !=
        std::string::npos);
  cfg.score_space = ScoreSpace::ObservationUV;
  const std::string jo = run_report_json(loop.run(cfg));
  CHECK(jo.find("\"score_space\": \"observation\"") != std::string::npos);
  // Existing keys keep their relative order (the metrics map is sorted).
  const std::size_t div = jw.find("\"divergence\"");
  const std::size_t ece = jw.find("\"ece\"");
  const std::size_t ids = jw.find("\"identity_stability\"");
  CHECK(div != std::string::npos && ece != std::string::npos &&
        ids != std::string::npos);
  CHECK(div < ece);
  CHECK(ece < ids);
}

// The artifact must be parseable JSON. The emitter is hand-rolled string
// concatenation, and a dropped separator there is invisible to a substring
// check -- it produced a file that read fine as text and that no JSON tool
// could load.
void test_artifact_is_parseable_json() {
  // The validator itself is checked first: a checker that accepts everything
  // would make the artifact assertion below meaningless.
  CHECK(!json_is_wellformed("{\"a\": 1 \"b\": 2}"));   // missing separator
  CHECK(!json_is_wellformed("{\"a\": 1,}"));           // trailing comma
  CHECK(!json_is_wellformed("{\"a\": 1} trailing"));   // trailing garbage
  CHECK(!json_is_wellformed("{\"a\": \"C:\\x\"}"));    // illegal escape
  CHECK(json_is_wellformed("{\"a\": [1, 2], \"b\": {\"c\": null}}"));

  LoopDriver loop(8);
  RunConfig cfg{};
  cfg.ticks = 20;
  cfg.seed = 42;
  cfg.width = 64;
  cfg.height = 64;
  cfg.footprint = 0.02f;
  cfg.perception_adapters = {"blob_detect"};
  cfg.world_models = {"zero_delta"};
  cfg.mode = RunMode::Reference;
  cfg.bounds = BoundsPolicy{BoundsPolicy::Kind::None, 0.0f};
  cfg.ep = ExecProvider::CpuEp;
  // A path with backslashes: the quoting has to survive them.
  cfg.command_line = "C:\\ow\\omniworld_loop.exe --ticks 20 --seed 42";
  CHECK(json_is_wellformed(run_report_json(loop.run(cfg))));
}

int run() {
  try {
    test_spaces_differ_both_ways();
    test_out_of_window_is_not_scored();
    test_coverage_semantics();
    test_per_producer_divergence();
    test_determinism_and_hash_invariance();
    test_default_space_unchanged();
    test_new_keys_frozen_on_600_42();
    test_cli_score_space();
    test_artifact_reports_space();
    test_artifact_is_parseable_json();
  } catch (const std::exception& e) {
    ++g_failures;
    std::printf("FAIL slice19: unexpected exception escaped: %s\n", e.what());
  }
  if (g_failures == 0) {
    std::printf("slice19: all tests passed\n");
    return 0;
  }
  std::printf("slice19: %d check(s) FAILED\n", g_failures);
  return 1;
}

}  // namespace slice19

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: ow_tests <slice-name>\n");
    return 1;
  }
  if (std::strcmp(argv[1], "slice01") == 0) return slice01::run();
  if (std::strcmp(argv[1], "slice02") == 0) return slice02::run();
  if (std::strcmp(argv[1], "slice03") == 0) return slice03::run();
  if (std::strcmp(argv[1], "slice04") == 0) return slice04::run();
  if (std::strcmp(argv[1], "slice05") == 0) return slice05::run();
  if (std::strcmp(argv[1], "slice06") == 0) return slice06::run();
  if (std::strcmp(argv[1], "slice07") == 0) return slice07::run();
  if (std::strcmp(argv[1], "slice08") == 0) return slice08::run();
  if (std::strcmp(argv[1], "slice09") == 0) return slice09::run();
  if (std::strcmp(argv[1], "slice10") == 0) return slice10::run();
  if (std::strcmp(argv[1], "slice11") == 0) return slice11::run();
  if (std::strcmp(argv[1], "slice12") == 0) return slice12::run();
  if (std::strcmp(argv[1], "slice13") == 0) return slice13::run();
  if (std::strcmp(argv[1], "slice14") == 0) return slice14::run();
  if (std::strcmp(argv[1], "slice15") == 0) return slice15::run();
  if (std::strcmp(argv[1], "slice16") == 0) return slice16::run();
  if (std::strcmp(argv[1], "slice17") == 0) return slice17::run();
  if (std::strcmp(argv[1], "slice18") == 0) return slice18::run();
  if (std::strcmp(argv[1], "slice19") == 0) return slice19::run();
  std::printf("unknown slice: %s\n", argv[1]);
  return 1;
}
