#pragma once
// Annotation truth (stage D slice D3): real dataset annotations become the
// per-tick truth, written by TruthWorld -- the single writer does not change
// (contract 4/5, L1 sec 2 item 5). This module parses a line manifest and
// applies the frozen loading protocol; it never touches WorldState itself.
//
// Why a line manifest and not the dataset's JSON: the core stays
// zero-dependency (no JSON parser enters the product path, same discipline as
// ADR-0012). Conversion from the dataset format belongs to tools/.
#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ow {

// Protocol constants (L2 sec 4). These are definitions, not knobs: a change
// here changes what "correct" means for every panel built on annotated truth,
// so it belongs in a slice, not on the command line.
//
// kAnnotationMinArea is in NORMALIZED units, so its physical size depends on
// the declared canvas: 1e-6 of a 640x480 canvas is ~0.31 px^2. It is stated in
// normalized terms on purpose -- the protocol must not depend on resolution
// (a 1px rule would silently redefine "correct" for every other canvas).
inline constexpr float kAnnotationMinArea = 1e-6f;
inline constexpr std::size_t kAnnotationPerFrameCap = 20;

struct Annotation {
  std::uint64_t source_id = 0;  // dataset id: provenance only, never an engine
                                // identity (contract 2: runtime is the only minter)
  ClassTag class_tag;
  // Normalized image box, [0,1], top-left origin, on the canvas declared by the
  // manifest header. Contract 8: the observation's own coordinate system.
  float u_min = 0.0f, v_min = 0.0f, u_max = 0.0f, v_max = 0.0f;
  bool crowded = false;  // dataset crowd flag; protocol rule 1 drops it
};

struct AnnotationFrame {
  std::vector<Annotation> boxes;  // source_id order (protocol rule 7)
};

struct AnnotationSet {
  std::string dataset;      // provenance lines: they go into the artifact
  std::string license;
  int width = 0;            // the canvas the normalization refers to
  int height = 0;
  std::vector<std::string> categories;  // declared class table (rule 4)
  std::vector<AnnotationFrame> frames;  // frames[k] belongs to tick k+1
  // Protocol counters: the honest denominator for every claim scored on this
  // set (L1 sec 7 R-D4 -- "the annotation did not say" must not read as
  // "the model is wrong").
  std::size_t dropped_crowd = 0;
  std::size_t dropped_degenerate = 0;
  std::size_t dropped_over_cap = 0;
  std::size_t boxes_kept() const;
  const AnnotationFrame* find(Tick t) const;  // nullptr = no frame for that tick
};

// Parse + apply the protocol. Every failure is a named error (path, line
// number, and the reason) -- a silently skipped line would quietly redefine
// the truth.
AnnotationSet load_annotation_set(const std::string& manifest_path);

}  // namespace ow
