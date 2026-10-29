#include "truth_world/truth_world.hpp"

#include <utility>

namespace ow {

EntityId TruthWorld::spawn(WorldState& ws, ClassTag tag, Transform t,
                           Vec3 velocity) {
  return ws.mint(std::move(tag), t, velocity);
}

void TruthWorld::advance(WorldState& ws, Tick target) {
  while (ws.tick() < target) {
    ws.step();
  }
  // target < current tick: defensive no-op (L2 sec 5).
}

void TruthWorld::build_demo_scene(WorldState& ws, int n) {
  for (int i = 0; i < n; ++i) {
    Transform t;
    t.position = Vec3{rng_.next_unit(), 0.0f, rng_.next_unit()};
    const Vec3 v{(rng_.next_unit() - 0.5f) * 0.1f, 0.0f,
                 (rng_.next_unit() - 0.5f) * 0.1f};
    spawn(ws, ClassTag{"target"}, t, v);
  }
}

bool TruthWorld::load_annotations(WorldState& ws, const AnnotationSet& set,
                                  Tick target) {
  const AnnotationFrame* frame = set.find(target);
  if (frame == nullptr) return false;
  ws.reset_scene(target);
  for (const Annotation& a : frame->boxes) {
    Transform t;
    // The protocol (annotation.cpp) already clamped and dropped, so this is
    // the visible center on the observation canvas -- the same unit square the
    // ViewCamera maps pixels onto, which is why the existing position scoring
    // applies unchanged (L2 sec 3 item 5).
    t.position = Vec3{0.5f * (a.u_min + a.u_max), 0.0f, 0.5f * (a.v_min + a.v_max)};
    spawn(ws, a.class_tag, t, Vec3{});
  }
  return true;
}

}  // namespace ow
