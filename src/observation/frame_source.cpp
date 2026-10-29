// File-backed frame supply (stage D slice D2). Reference-mode only: bytes come
// from disk, so the observation is reproducible by construction.
#include "observation/frame_source.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace ow {
namespace {

std::string trim(const std::string& s) {
  const std::size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  const std::size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string dir_of(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

bool absolute(const std::string& p) {
  return (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\'));
}

class FileFrameSource final : public FrameSource {
 public:
  explicit FileFrameSource(const std::string& manifest_path) {
    std::FILE* f = std::fopen(manifest_path.c_str(), "rb");
    if (f == nullptr) {
      throw std::runtime_error("frame source: cannot open manifest '" + manifest_path + "'");
    }
    std::string text;
    char buf[4096];
    std::size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
    std::fclose(f);

    const std::string base = dir_of(manifest_path);
    std::size_t pos = 0;
    while (pos <= text.size()) {
      const std::size_t nl = text.find('\n', pos);
      const std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
      pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
      if (line.empty() || line[0] == '#') continue;

      const std::size_t eq = line.find('=');
      if (eq != std::string::npos && !frame_seen_) {
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "width") {
          width_ = std::atoi(val.c_str());
        } else if (key == "height") {
          height_ = std::atoi(val.c_str());
        } else if (key == "rgb") {
          if (val != "rgb8") {
            throw std::runtime_error("frame source: unsupported rgb='" + val +
                                     "' (only rgb8) in '" + manifest_path + "'");
          }
        } else {
          throw std::runtime_error("frame source: unknown key '" + key + "' in '" +
                                   manifest_path + "'");
        }
        continue;
      }
      frame_seen_ = true;
      paths_.push_back(absolute(line) ? line : base + line);
    }

    if (width_ <= 0 || height_ <= 0) {
      throw std::runtime_error("frame source: manifest '" + manifest_path +
                               "' needs positive width= and height= (got " +
                               std::to_string(width_) + "x" + std::to_string(height_) + ")");
    }
    if (paths_.empty()) {
      throw std::runtime_error("frame source: manifest '" + manifest_path + "' lists no frames");
    }
  }

  const char* name() const override { return "file_frames"; }
  std::size_t frame_count() const override { return paths_.size(); }

  bool next(Observation& out) override {
    if (idx_ >= paths_.size()) return false;
    const std::string& path = paths_[idx_];
    const std::size_t want = static_cast<std::size_t>(width_) * height_ * 3;

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
      throw std::runtime_error("frame source: cannot open frame '" + path + "'");
    }
    out.rgb.assign(want, 0);
    const std::size_t got = std::fread(out.rgb.data(), 1, want, f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    if (got != want || (size >= 0 && static_cast<std::size_t>(size) != want)) {
      throw std::runtime_error("frame source: frame '" + path + "' is " +
                               std::to_string(size) + " bytes, expected " +
                               std::to_string(want) + " (" + std::to_string(width_) + "x" +
                               std::to_string(height_) + "x3 RGB8)");
    }

    out.view = ViewCamera{Transform{}, width_, height_};
    out.sensor_kind = "rgb_video";
    out.meta = FrameMeta{"cpu", "host", "RGB8", width_, height_};
    // A video frame carries no per-entity visibility; leaving the list empty is
    // the honest statement ("this source does not know"), not an implicit "all
    // unseen" (contract 8: unseen != empty != zero).
    out.visibility.clear();
    ++idx_;
    return true;
  }

 private:
  int width_ = 0;
  int height_ = 0;
  bool frame_seen_ = false;
  std::size_t idx_ = 0;
  std::vector<std::string> paths_;
};

}  // namespace

std::unique_ptr<FrameSource> make_file_frame_source(const std::string& manifest_path) {
  return std::unique_ptr<FrameSource>(new FileFrameSource(manifest_path));
}

}  // namespace ow
