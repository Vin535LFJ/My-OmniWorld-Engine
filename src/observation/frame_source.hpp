#pragma once
// Frame supply (stage D slice D2): the SECOND producer of Observations.
// ObservationGenerator renders from the truth world; a FrameSource hands over
// already-decoded frames. The reference path only ever uses FileFrameSource, so
// no decoder and no external dependency is involved here -- and decoder output
// never enters a hash (ADR-0009 keeps byte-exactness to what this project owns).
#include "observation/observation.hpp"

#include <memory>
#include <string>

namespace ow {

class FrameSource {
 public:
  virtual ~FrameSource() = default;
  // Fills width/height/view/meta/sensor_kind/rgb. Does NOT set tick: time
  // belongs to the loop (contract 1, tick is the version), so a source cannot
  // drift the clock. Returns false at end of stream -- end of data is not an
  // error, so it is a return value rather than an exception.
  virtual bool next(Observation& out) = 0;
  virtual const char* name() const = 0;
  virtual std::size_t frame_count() const = 0;
};

// Manifest format (line-oriented, no parser dependency):
//   width=640
//   height=480
//   rgb=rgb8            # only RGB8 today; anything else is rejected
//   frame_000.rgb       # one path per line, resolved against the manifest dir
//   frame_001.rgb
// Each frame is a raw RGB8 buffer of width*height*3 bytes, row-major, top-left
// origin -- byte-identical to Observation::rgb (tools/png_to_rgb.mjs produces it).
std::unique_ptr<FrameSource> make_file_frame_source(const std::string& manifest_path);

}  // namespace ow
