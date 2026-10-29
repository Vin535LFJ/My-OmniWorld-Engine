// Annotation truth (stage D slice D3): line-manifest parsing plus the frozen
// loading protocol of L2 sec 4. This is the reference-mode path -- the bytes on
// disk are the evidence, so annotated truth is reproducible by construction,
// exactly like the frame sequence it corresponds to. Performance mode accepts
// the same manifest (its truth still cannot come from a decoder); the
// determinism claim below is what reference mode is graded on.
#include "truth_world/annotation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace ow {
namespace {

constexpr const char* kTag = "annotations";

// The seven fields of a box line, in order, as every message quotes them.
constexpr const char* kBoxUsage =
    "box u_min v_min u_max v_max class source_id crowd";

std::string trim(const std::string& s) {
  const std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  const std::size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Every message names the file and the line: a malformed truth manifest is a
// data defect the reader has to locate, not a mystery at the other end of a run.
[[noreturn]] void fail(const std::string& path, int line, const std::string& what) {
  throw std::runtime_error(std::string(kTag) + ": '" + path + "' line " +
                           std::to_string(line) + ": " + what);
}

// A defect in the header, before any tick block exists to carry a line number.
[[noreturn]] void fail_header(const std::string& path, const std::string& what) {
  throw std::runtime_error(std::string(kTag) + ": '" + path + "': " + what);
}

bool parse_number(const std::string& text, float& out) {
  const char* s = text.c_str();
  char* end = nullptr;
  const float v = std::strtof(s, &end);
  if (end == s || *end != '\0' || !std::isfinite(v)) return false;
  out = v;
  return true;
}

bool parse_uint(const std::string& text, std::uint64_t& out) {
  const char* s = text.c_str();
  char* end = nullptr;
  const unsigned long long v = std::strtoull(s, &end, 10);
  if (end == s || *end != '\0') return false;
  out = v;
  return true;
}

int parse_dimension(const std::string& path, int line, const std::string& key,
                    const std::string& text) {
  std::uint64_t v = 0;
  if (!parse_uint(text, v) || v == 0) {
    fail(path, line, "header key '" + key + "' needs a positive integer, got '" + text + "'");
  }
  if (v > (1u << 24)) {
    fail(path, line, "header key '" + key + "' = " + std::to_string(v) +
                         " exceeds the 16777216 sanity bound for a canvas");
  }
  return static_cast<int>(v);
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

std::vector<std::string> split_csv(const std::string& path, int line,
                                   const std::string& text) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t comma = text.find(',', pos);
    const std::string item =
        trim(text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
    if (item.empty()) {
      fail(path, line, "empty class name in the categories table");
    }
    out.push_back(item);
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return out;
}

// Protocol rules 1 (crowd), 2 (clamp + visible area) and 3 (per-frame cap), in
// the order L2 sec 4 states them. Rule 7 (source_id order) is what makes the
// cap deterministic; the counters are what makes the panel honest.
void apply_protocol(AnnotationSet& set) {
  for (AnnotationFrame& frame : set.frames) {
    std::vector<Annotation> kept;
    for (Annotation& a : frame.boxes) {
      if (a.crowded) {
        ++set.dropped_crowd;  // rule 1
        continue;
      }
      a.u_min = clamp01(a.u_min);
      a.v_min = clamp01(a.v_min);
      a.u_max = clamp01(a.u_max);
      a.v_max = clamp01(a.v_max);
      const float area = (a.u_max - a.u_min) * (a.v_max - a.v_min);
      // Rule 2; the negated comparison also drops a NaN area.
      if (!(area >= kAnnotationMinArea)) {
        ++set.dropped_degenerate;
        continue;
      }
      kept.push_back(a);
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const Annotation& x, const Annotation& y) {
                       return x.source_id < y.source_id;  // rule 7
                     });
    if (kept.size() > kAnnotationPerFrameCap) {  // rule 3
      set.dropped_over_cap += kept.size() - kAnnotationPerFrameCap;
      kept.resize(kAnnotationPerFrameCap);
    }
    frame.boxes.swap(kept);
  }
}

}  // namespace

std::size_t AnnotationSet::boxes_kept() const {
  std::size_t n = 0;
  for (const AnnotationFrame& f : frames) n += f.boxes.size();
  return n;
}

const AnnotationFrame* AnnotationSet::find(Tick t) const {
  if (t == 0 || t > frames.size()) return nullptr;
  return &frames[static_cast<std::size_t>(t) - 1];
}

AnnotationSet load_annotation_set(const std::string& manifest_path) {
  std::FILE* file = std::fopen(manifest_path.c_str(), "rb");
  if (file == nullptr) {
    throw std::runtime_error(std::string(kTag) + ": cannot open manifest '" +
                             manifest_path + "'");
  }
  std::string text;
  char buf[4096];
  std::size_t got = 0;
  while ((got = std::fread(buf, 1, sizeof(buf), file)) > 0) text.append(buf, got);
  std::fclose(file);

  AnnotationSet set;
  bool in_body = false;
  int lineno = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    const std::string line =
        trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
    ++lineno;
    if (line.empty() || line[0] == '#') continue;

    if (line.compare(0, 5, "tick=") == 0) {
      // Blocks must run 1,2,3...: a gap is missing truth, which is a different
      // fact from the end of the stream (the latter is a normal finish).
      const std::size_t want = set.frames.size() + 1;
      std::uint64_t t = 0;
      if (!parse_uint(trim(line.substr(5)), t)) {
        fail(manifest_path, lineno, "tick= needs a positive integer, got '" + line + "'");
      }
      if (t != want) {
        fail(manifest_path, lineno,
             "tick blocks must run 1,2,3...: expected " + std::to_string(want) + ", got " +
                 std::to_string(t) + " (a gap is missing truth, not the end of it)");
      }
      set.frames.push_back(AnnotationFrame{});
      in_body = true;
      continue;
    }

    if (!in_body) {
      const std::size_t eq = line.find('=');
      if (eq == std::string::npos) {
        fail(manifest_path, lineno, "expected a 'key=value' header or a 'tick=' block, got '" +
                                       line + "'");
      }
      const std::string key = trim(line.substr(0, eq));
      const std::string val = trim(line.substr(eq + 1));
      if (key == "dataset") {
        set.dataset = val;
      } else if (key == "license") {
        set.license = val;
      } else if (key == "width") {
        set.width = parse_dimension(manifest_path, lineno, key, val);
      } else if (key == "height") {
        set.height = parse_dimension(manifest_path, lineno, key, val);
      } else if (key == "categories") {
        set.categories = split_csv(manifest_path, lineno, val);
      } else {
        fail(manifest_path, lineno, "unknown header key '" + key + "'");
      }
      continue;
    }

    if (line.compare(0, 4, "box ") != 0) {
      fail(manifest_path, lineno, "expected '" + std::string(kBoxUsage) + "', got '" + line + "'");
    }
    std::vector<std::string> tok;
    for (std::size_t p = 4; p < line.size();) {
      const std::size_t s = line.find_first_not_of(" \t", p);
      if (s == std::string::npos) break;
      const std::size_t e = line.find_first_of(" \t", s);
      tok.push_back(line.substr(s, e == std::string::npos ? std::string::npos : e - s));
      p = (e == std::string::npos) ? line.size() : e;
    }
    if (tok.size() != 7) {
      fail(manifest_path, lineno, "box needs 7 fields (" + std::string(kBoxUsage) +
                                       "), got " + std::to_string(tok.size()) + ": '" +
                                       line + "'");
    }
    Annotation a;
    if (!parse_number(tok[0], a.u_min) || !parse_number(tok[1], a.v_min) ||
        !parse_number(tok[2], a.u_max) || !parse_number(tok[3], a.v_max)) {
      fail(manifest_path, lineno, "box coordinates must be finite numbers: '" + line + "'");
    }
    if (tok[4].find_first_of("\"\\") != std::string::npos) {
      fail(manifest_path, lineno, "box class name must be a plain token: '" + line + "'");
    }
    a.class_tag = ClassTag{tok[4]};
    std::uint64_t id = 0;
    if (!parse_uint(tok[5], id) || id == 0) {
      fail(manifest_path, lineno,
           "box source_id must be a positive integer, got '" + tok[5] + "': '" + line + "'");
    }
    a.source_id = id;
    if (tok[6] != "0" && tok[6] != "1") {
      fail(manifest_path, lineno,
           "box crowd flag must be 0 or 1, got '" + tok[6] + "': '" + line + "'");
    }
    a.crowded = (tok[6] == "1");
    set.frames.back().boxes.push_back(a);
  }

  if (set.dataset.empty() || set.license.empty()) {
    fail_header(manifest_path,
                "needs non-empty dataset= and license= (provenance is not optional for "
                "real data)");
  }
  if (set.width <= 0 || set.height <= 0) {
    fail_header(manifest_path,
                "needs positive width= and height= (the canvas the normalized boxes refer to)");
  }
  if (set.categories.empty()) {
    fail_header(manifest_path,
                "needs a categories= table (protocol rule 4 checks classes against it)");
  }
  if (set.frames.empty()) {
    fail_header(manifest_path,
                "declares no tick= block (an empty truth stream is not a panel)");
  }

  // Rule 4 before the protocol: an unknown class is a format disagreement
  // between the fetcher and the engine, and must never be filtered away.
  for (const AnnotationFrame& frame : set.frames) {
    for (const Annotation& a : frame.boxes) {
      if (std::find(set.categories.begin(), set.categories.end(), a.class_tag.value) ==
          set.categories.end()) {
        fail_header(manifest_path, "class '" + a.class_tag.value +
                                      "' is not in the declared categories table");
      }
    }
    // Duplicate ids inside one frame mean the manifest was spliced by hand.
    for (std::size_t i = 0; i < frame.boxes.size(); ++i) {
      for (std::size_t j = i + 1; j < frame.boxes.size(); ++j) {
        if (frame.boxes[i].source_id == frame.boxes[j].source_id) {
          fail_header(manifest_path, "duplicate source_id " +
                                        std::to_string(frame.boxes[i].source_id) +
                                        " in one frame (dataset annotation ids are unique "
                                        "by definition)");
        }
      }
    }
  }

  apply_protocol(set);
  return set;
}

}  // namespace ow
