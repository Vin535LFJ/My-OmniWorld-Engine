#pragma once
// Core value types (data plus the small geometry the contract needs).
// Contract: docs/design/00-总体设计.md sec 2.
#include <cmath>
#include <cstdint>
#include <string>

namespace ow {

using Tick = std::uint64_t;      // monotonic tick domain; tick is the version
using EntityId = std::uint64_t;  // minted by the runtime; 0 = invalid; never reused

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Quat { float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f; };
struct Transform { Vec3 position; Quat orientation; };

// Camera merged INTO the observation (arch candidate #3, stage C slice C2):
// an observation carries HOW it was seen -- the pose and the projection
// between world (x/z plane, y up) and pixels. Resolution is the camera's own
// property, never a module constant (the legacy-224 bug class stays
// structurally removed).
//
// It lives in core (not observation/) because the scoring space needs it too:
// a value type shared by two seams belongs in core (ADR-0013).
struct ViewCamera {
  Transform pose;  // contract 8: the only second coordinate frame allowed
  int width = 0;
  int height = 0;

  // world (x, z) -> pixel column/row; out-of-window clamps to the border.
  int pixel_x(float wx) const {
    int p = static_cast<int>(std::floor(wx * static_cast<float>(width)));
    if (p < 0) p = 0;
    if (p > width - 1) p = width - 1;
    return p;
  }
  int pixel_z(float wz) const {
    int p = static_cast<int>(std::floor(wz * static_cast<float>(height)));
    if (p < 0) p = 0;
    if (p > height - 1) p = height - 1;
    return p;
  }
  // Inverse of the above (pixel indices are pixel-box centers).
  float world_x(int px) const {
    return (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
  }
  float world_z(int py) const {
    return (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
  }
  // The camera's world window. A position outside it has no pixel of its own:
  // pixel_x/pixel_z would clamp it onto the border, where it could sit at zero
  // distance from a claim that really is on the border. The window therefore
  // belongs to the camera, not to whoever asks about it.
  bool in_window(float wx, float wz) const {
    return wx >= 0.0f && wx <= 1.0f && wz >= 0.0f && wz <= 1.0f;
  }
};

// Three-state visibility (contract 8): seen / unseen / uncertain.
// Unseen is NOT empty and NOT zero — it means "not observed".
enum class Visibility : std::uint8_t { Seen, Unseen, Uncertain };

// Bounded scene policy (contract 9 additive field: boundary_events; stage B
// slice F2). Wall reflects position with mirrored velocity on the x/z axes;
// margin is the distance from each wall where reflection happens. No member
// default values: callers (CLI layer, tests) construct explicitly.
struct BoundsPolicy {
  enum class Kind { None, Wall };
  Kind kind;
  float margin;
};

// Execution provider for real-model adapters (stage C). Lives in core/types
// so perception can consume it without depending on loop (same reasoning as
// BoundsPolicy). TrtEp is reserved by the L1 (Round 1 #2) and NOT implemented
// -- see docs/17-research/2026-10-16-onnx-runtime-integration.md.
enum class ExecProvider { CpuEp, CudaEp };

// The space a positional claim is judged in (ADR-0013). It changes WHERE
// "correct" is decided, never what a producer may claim: the confidence
// semantics (contract 7) and the adjudication stay untouched.
//
//   WorldXZ       -- the error is a distance in the world's x/z plane.
//   ObservationUV -- both positions are projected through the camera that
//                    produced the observation and compared in the pixel
//                    plane: "correct" means "the same pixel in this image".
//
// Applicability boundary: ObservationUV supports calibration comparison
// WITHIN one observation sequence. It must not carry a cross-scene
// localization claim (ADR-0013, L1 sec 7 R-D7).
enum class ScoreSpace { WorldXZ, ObservationUV };

struct ClassTag { std::string value; };

}  // namespace ow
