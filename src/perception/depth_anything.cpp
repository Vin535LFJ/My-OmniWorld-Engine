// Depth-Anything-V2-Small perception adapter (stage C slice C4) — the SECOND
// model family through the same single-file PerceptionAdapter contract. Its
// output is a dense per-pixel relative-depth map, i.e. a shape with nothing in
// common with detection boxes, which is the point: the seam must be
// family-agnostic. The dense->sparse reduction lives here (the adapter owns
// pre/post-processing; contract slice-03 L2).
//
// Honest scope note: the truth world is a y=0 plane with no depth ground truth
// (src/truth_world/truth_world.cpp), so these Proposals cannot be scored as
// correct here. This slice delivers the seam proof + a real-image ordering
// check, not a depth utility claim. See
// docs/design/stage-c-ai-in-loop/slices/04-depth-anything-v2-small-adapter.md
//
// Compiled only when OW_WITH_ONNXRUNTIME=1 (CMake adds this file conditionally).
#include "perception/adapter.hpp"

#if OW_WITH_ONNXRUNTIME

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // <windows.h> min/max macros would break std::min/std::max
#include <windows.h>  // MultiByteToWideChar: ORTCHAR_T is wchar_t on Windows
#endif

#ifndef OW_MODELS_DIR
#define OW_MODELS_DIR "models"
#endif

namespace ow {
namespace {

// Reference processor = DPTImageProcessor, verified against the base repo's
// preprocessor_config.json on 2026-10-19: size 518x518, keep_aspect_ratio,
// ensure_multiple_of 14, resample 3 (bicubic), do_rescale 1/255, and -- unlike
// the RT-DETR checkpoint -- do_normalize TRUE with ImageNet mean/std. Two real
// models in one stage disagree on normalization, which is why these constants
// are read per model and never migrated across adapters.
constexpr int kMaxSide = 518;          // longest edge target
constexpr int kSizeMultiple = 14;      // DPT patch constraint
constexpr float kRescale = 1.0f / 255.0f;
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};

// Dense->sparse reduction (L2 §3.2): a coarse grid, ranked within the frame.
constexpr int kGrid = 8;               // 8x8 cells -> at most 32 proposals
constexpr float kDegenerateEps = 1e-6f;

std::string resolve_model_path() {
  if (const char* env = std::getenv("OW_DA_MODEL")) return env;
  return std::string(OW_MODELS_DIR) + "/depth-anything-v2-small-onnx.onnx";
}

#ifdef _WIN32
std::wstring to_ort_path(const std::string& path) {
  if (path.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                                    static_cast<int>(path.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()),
                        w.data(), n);
  }
  return w;
}
#endif

// Keys cubic kernel, a = -0.5 (Catmull-Rom). Same kernel FAMILY as the
// reference processor's bicubic; bit-exactness with PIL is deliberately not
// claimed -- the reference path only promises our own reproducibility.
float cubic_w(float t, float a) {
  const float x = std::fabs(t);
  if (x <= 1.0f) return ((a + 3.0f) * x - (a + 4.0f)) * x * x + 1.0f;
  if (x < 2.0f) return ((a * x - 5.0f * a) * x + 8.0f * a) * x - 4.0f * a;
  return 0.0f;
}

// Fit the frame into kMaxSide on the long edge, keep the aspect ratio, then
// round each side to the nearest multiple of kSizeMultiple (>= one multiple).
void choose_input_size(int w, int h, bool fixed_square, int& out_w, int& out_h) {
  if (fixed_square) {  // graph declares static axes: obey them
    out_w = kMaxSide;
    out_h = kMaxSide;
    return;
  }
  const float scale = static_cast<float>(kMaxSide) / static_cast<float>(std::max(w, h));
  const auto snap = [](float v) {
    const int q = static_cast<int>(std::lround(v / static_cast<float>(kSizeMultiple)));
    return std::max(1, q) * kSizeMultiple;
  };
  out_w = snap(static_cast<float>(w) * scale);
  out_h = snap(static_cast<float>(h) * scale);
}

class DepthAnythingV2S final : public PerceptionAdapter {
 public:
  const char* name() const override { return "depth_anything_v2_s"; }

  void set_execution_provider(ExecProvider ep) override {
    if (ep != provider_) {
      provider_ = ep;
      session_ready_ = false;  // rebuild lazily under the new provider
    }
  }

  std::vector<Proposal> observe(const Observation& obs) override {
    ensure_session();
    const int W = obs.view.width;
    const int H = obs.view.height;
    if (W <= 0 || H <= 0 ||
        obs.rgb.size() != static_cast<std::size_t>(W) * H * 3) {
      throw std::invalid_argument("depth_anything_v2_s: malformed observation");
    }

    // ---- preprocessing (adapter-owned): bicubic + /255 + ImageNet normalize --
    int tw = 0, th = 0;
    const bool rank4 = in_shape_.size() == 4;
    choose_input_size(W, H, in_h_fixed_ && in_w_fixed_, tw, th);
    if (rank4 && in_shape_[2] > 0) th = static_cast<int>(in_shape_[2]);
    if (rank4 && in_shape_[3] > 0) tw = static_cast<int>(in_shape_[3]);
    const std::size_t px = static_cast<std::size_t>(tw) * th;
    std::vector<float> input(3 * px);
    const float scale_x = static_cast<float>(W) / static_cast<float>(tw);
    const float scale_y = static_cast<float>(H) / static_cast<float>(th);
    for (int y = 0; y < th; ++y) {
      const float sy = (static_cast<float>(y) + 0.5f) * scale_y - 0.5f;
      const int iy = static_cast<int>(std::floor(sy));
      float wy[4];
      for (int k = 0; k < 4; ++k) wy[k] = cubic_w(sy - (iy - 1 + k), -0.5f);
      for (int x = 0; x < tw; ++x) {
        const float sx = (static_cast<float>(x) + 0.5f) * scale_x - 0.5f;
        const int ix = static_cast<int>(std::floor(sx));
        float wx[4];
        for (int k = 0; k < 4; ++k) wx[k] = cubic_w(sx - (ix - 1 + k), -0.5f);
        const std::size_t dst = static_cast<std::size_t>(y) * tw + x;
        for (int c = 0; c < 3; ++c) {
          float acc = 0.0f;
          for (int ky = 0; ky < 4; ++ky) {
            const int src_y = clamp_row(iy - 1 + ky, H);
            for (int kx = 0; kx < 4; ++kx) {
              const int src_x = clamp_col(ix - 1 + kx, W);
              acc += wy[ky] * wx[kx] *
                     static_cast<float>(
                         obs.rgb[(static_cast<std::size_t>(src_y) * W + src_x) *
                                  3 + c]);
            }
          }
          input[static_cast<std::size_t>(c) * px + dst] =
              (acc * kRescale - kMean[c]) / kStd[c];
        }
      }
    }
    const int64_t shape[4] = {1, 3, th, tw};
    Ort::Value tensor =
        Ort::Value::CreateTensor<float>(mem_, input.data(), input.size(), shape, 4);
    const char* in_names[1] = {in_name_.c_str()};
    std::vector<const char*> out_names;
    for (const std::string& n : out_names_) out_names.push_back(n.c_str());
    std::vector<Ort::Value> outputs =
        session_.Run(Ort::RunOptions{nullptr}, in_names, &tensor, 1,
                     out_names.data(), out_names.size());

    // ---- the dense map ----
    // The graph declares dynamic axes (input probed as [-1,3,-1,-1]), so the
    // REAL extent comes from the output value at run time, never from the
    // session-time metadata (which is all -1s here).
    const auto out_info = outputs[map_index_].GetTensorTypeAndShapeInfo();
    const std::size_t n_elems = out_info.GetElementCount();
    const auto map_shape = out_info.GetShape();
    std::string dims;
    for (const int64_t d : map_shape) dims += std::to_string(d) + ",";
    const std::string where =
        std::string("output '") + out_names_[map_index_] + "' dims [" + dims + "]";
    if (map_shape.size() < 2) {
      throw std::runtime_error("depth_anything_v2_s: not a 2D map, " + where);
    }
    if (map_shape.size() == 4 && map_shape[1] != 1) {
      throw std::runtime_error("depth_anything_v2_s: multi-channel map, " + where +
                               "; expected a single-channel {1,H,W} / {1,1,H,W}");
    }
    const int mh = static_cast<int>(map_shape[map_shape.size() - 2]);
    const int mw = static_cast<int>(map_shape[map_shape.size() - 1]);
    // Exact element count: a single-channel map is mh*mw and nothing else, so a
    // wider last axis (e.g. a second per-pixel field) cannot slip through.
    if (mh < kGrid || mw < kGrid ||
        static_cast<std::size_t>(mh) * static_cast<std::size_t>(mw) != n_elems) {
      throw std::runtime_error(
          std::string("depth_anything_v2_s: unexpected extent, ") + where + " with " +
          std::to_string(n_elems) + " elements (need a single-channel map of at "
          "least " +
          std::to_string(kGrid) + "x" + std::to_string(kGrid) + ")");
    }
    const float* map = outputs[map_index_].GetTensorData<float>();

    // ---- dense -> sparse: grid means, ranked within this frame ----
    // Relative depth is affine-indeterminate, so only the ORDER inside a frame
    // is identifiable; no absolute distance is ever claimed. mh/mw >= kGrid is
    // checked above, which also guarantees every cell collects >= 1 pixel.
    std::vector<float> cell(kGrid * kGrid, 0.0f);
    std::vector<int> cell_n(kGrid * kGrid, 0);
    for (int y = 0; y < mh; ++y) {
      const int r = std::min(kGrid - 1, y * kGrid / mh);
      for (int x = 0; x < mw; ++x) {
        const int c = std::min(kGrid - 1, x * kGrid / mw);
        cell[static_cast<std::size_t>(r) * kGrid + c] +=
            map[static_cast<std::size_t>(y) * mw + x];
        ++cell_n[static_cast<std::size_t>(r) * kGrid + c];
      }
    }
    for (std::size_t i = 0; i < cell.size(); ++i) {
      cell[i] /= static_cast<float>(cell_n[i]);
    }
    const auto bounds = std::minmax_element(cell.begin(), cell.end());
    const float lo = *bounds.first, hi = *bounds.second;
    if (debug_) {
      const int max_i = static_cast<int>(std::distance(
          cell.begin(), std::find(cell.begin(), cell.end(), hi)));
      std::printf("[da debug] tensor 1x3x%dx%d -> %s\n", th, tw, where.c_str());
      std::printf("[da debug] cell means [%.4f, %.4f] spread %.4f; max at row %d col %d\n",
                  lo, hi, hi - lo, max_i / kGrid, max_i % kGrid);
    }
    std::vector<Proposal> out;
    if (hi - lo < kDegenerateEps) {
      if (debug_) {
        std::printf("[da debug] degenerate map (spread %.6f < %.6f) -> empty list\n",
                    hi - lo, kDegenerateEps);
      }
      return out;  // flat frame: nothing to rank
    }
    std::vector<float> sorted = cell;
    std::sort(sorted.begin(), sorted.end());
    const float median =
        0.5f * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    for (int r = 0; r < kGrid; ++r) {
      for (int c = 0; c < kGrid; ++c) {
        const float v = cell[static_cast<std::size_t>(r) * kGrid + c];
        if (v <= median) continue;
        const float norm = (v - lo) / (hi - lo);
        Proposal p;
        p.producer = name();
        p.tick_observed = obs.tick;
        p.target = NewEntity{};
        Transform t;
        // Cell centre in normalized image coords; the camera covers the unit
        // square with an identity pose, so u/v ARE world x/z (same identity as
        // ViewCamera::world_x, see rtdetr.cpp for the derivation).
        t.position = Vec3{(static_cast<float>(c) + 0.5f) / static_cast<float>(kGrid),
                          0.0f,
                          (static_cast<float>(r) + 0.5f) / static_cast<float>(kGrid)};
        p.fields.transform = t;
        // Prominence, NOT a calibrated probability: there is no depth ground
        // truth in this world to calibrate against (contract 7 gives it zero
        // adjudication power either way). L2 §9 R-C4d. The value is the cell's
        // min-max normalized mean mapped into [0.5, 1] -- order-preserving, so
        // it carries the same information as a rank would.
        p.confidence = 0.5f + 0.5f * norm;
        if (debug_) {
          std::printf("[da debug] cell r=%d c=%d mean %.4f norm %.3f conf %.3f\n", r,
                      c, static_cast<double>(v), static_cast<double>(norm),
                      static_cast<double>(p.confidence));
        }
        out.push_back(std::move(p));
      }
    }
    return out;
  }

 private:
  static int clamp_row(int y, int H) { return std::max(0, std::min(H - 1, y)); }
  static int clamp_col(int x, int W) { return std::max(0, std::min(W - 1, x)); }

  void ensure_session() {
    if (session_ready_) return;
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);  // determinism: reference path is single-thread
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (provider_ == ExecProvider::CudaEp) {
      try {
        OrtCUDAProviderOptions cuda_opts{};
        cuda_opts.device_id = 0;
        opts.AppendExecutionProvider_CUDA(cuda_opts);
      } catch (const Ort::Exception& e) {
        throw std::runtime_error(
            std::string("depth_anything_v2_s: CUDA EP unavailable (") + e.what() +
            "); needs CUDA 12.8+/cuDNN 9 (see docs/17-research/"
            "2026-10-16-onnx-runtime-integration.md)");
      }
    }
    const std::string path = resolve_model_path();
    try {
#ifdef _WIN32
      const std::wstring wpath = to_ort_path(path);
      session_ = Ort::Session(env_, wpath.c_str(), opts);
#else
      session_ = Ort::Session(env_, path.c_str(), opts);
#endif
    } catch (const Ort::Exception& e) {
      throw std::runtime_error("depth_anything_v2_s: model load failed at '" + path +
                               "' (" + e.what() +
                               "); run: node tools/fetch_models.mjs");
    }
    Ort::AllocatorWithDefaultOptions alloc;
    in_name_ = session_.GetInputNameAllocated(0, alloc).get();
    in_shape_ = session_.GetInputTypeInfo(0)
                    .GetTensorTypeAndShapeInfo()
                    .GetShape();
    in_h_fixed_ = in_shape_.size() == 4 && in_shape_[2] > 0;
    in_w_fixed_ = in_shape_.size() == 4 && in_shape_[3] > 0;
    const std::size_t n_out = session_.GetOutputCount();
    out_names_.clear();
    map_index_ = SIZE_MAX;
    for (std::size_t i = 0; i < n_out; ++i) {
      out_names_.push_back(session_.GetOutputNameAllocated(i, alloc).get());
      const auto shape = session_.GetOutputTypeInfo(i)
                             .GetTensorTypeAndShapeInfo()
                             .GetShape();
      // Static metadata classifies only (dynamic axes make it useless for
      // extents -- problem-log 011). A dense map is rank 3 {1,H,W} or rank 4
      // {1,1,H,W}; -1 in those positions is expected, not a wrong layout.
      const bool rank3 = shape.size() == 3;
      const bool rank4_map = shape.size() == 4 && (shape[1] == 1 || shape[1] == -1);
      if ((rank3 || rank4_map) && shape[shape.size() - 1] != 4) {
        map_index_ = i;
      }
    }
    if (map_index_ == SIZE_MAX) {
      throw std::runtime_error(
          "depth_anything_v2_s: no dense map output found (expected {1,H,W} or "
          "{1,1,H,W}); outputs are: [" +
          [&] {
            std::string s;
            for (std::size_t i = 0; i < out_names_.size(); ++i) {
              if (i) s += ", ";
              s += out_names_[i];
            }
            return s;
          }() +
          "]");
    }
    debug_ = std::getenv("OW_DA_DEBUG") != nullptr;
    // Probed I/O metadata, printed once per session: this adapter deliberately
    // hard-codes no shape, so the values it actually found belong in the test
    // output, not only behind a debug flag (L2 §6 test 1).
    std::string in_dims;
    for (const int64_t d : in_shape_) {
      in_dims += std::to_string(d) + ",";
    }
    std::printf("[da probe] input '%s' dims [%s] outputs=%zu, map='%s'\n",
                in_name_.c_str(), in_dims.c_str(), n_out,
                out_names_[map_index_].c_str());
    session_ready_ = true;
  }

  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "omniworld"};
  Ort::MemoryInfo mem_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                                   OrtMemTypeDefault);
  Ort::Session session_{nullptr};
  ExecProvider provider_ = ExecProvider::CpuEp;
  bool session_ready_ = false;
  bool debug_ = false;
  bool in_h_fixed_ = false;
  bool in_w_fixed_ = false;
  std::string in_name_;
  std::vector<int64_t> in_shape_;
  std::vector<std::string> out_names_;
  std::size_t map_index_ = SIZE_MAX;
};

}  // namespace

OW_REGISTER_PERCEPTION("depth_anything_v2_s", DepthAnythingV2S)

}  // namespace ow

#endif  // OW_WITH_ONNXRUNTIME
