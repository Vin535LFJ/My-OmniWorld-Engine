#pragma once
// Truth World: deterministic kinematics; the single writer of WorldState.
// Stage D slice D3 extends it from generator to generator + loader: external
// annotations can become the truth, and still only TruthWorld writes (contract
// 4/5, L1 sec 2 item 5).
#include "core/rng.hpp"
#include "core/types.hpp"
#include "truth_world/annotation.hpp"
#include "world_state/world_state.hpp"

namespace ow {

class TruthWorld {
 public:
  explicit TruthWorld(std::uint64_t seed) : rng_(seed) {}

  EntityId spawn(WorldState& ws, ClassTag tag, Transform t, Vec3 velocity);
  void advance(WorldState& ws, Tick target);  // step until tick == target
  void build_demo_scene(WorldState& ws, int n);

  // Load tick `target` of an annotation set as the live scene. The tick is a
  // parameter, not a field of the data: the loop owns time, so a second source
  // of ticks cannot drift (same rule as FrameSource::next, slice D2). Returns
  // false when the set has no frame for that tick -- the end of real data is a
  // value, not an exception. Positions are the normalized box centers on the
  // observation canvas (contract 8), y stays 0, velocity stays 0: an annotation
  // says where and what, never how it moves.
  bool load_annotations(WorldState& ws, const AnnotationSet& set, Tick target);

 private:
  Rng rng_;
};

}  // namespace ow
