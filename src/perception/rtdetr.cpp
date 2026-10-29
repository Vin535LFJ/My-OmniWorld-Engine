// RT-DETRv2-S perception adapter (stage C slice C3) — the first REAL AI model
// in the loop, on a single file, through the same PerceptionAdapter contract
// as the built-in blob detectors. Pre- and post-processing live here (the
// adapter owns them; contract slice-03 L2). CPU EP is the reference path
// (byte-deterministic, single thread); CUDA EP is wired but requires
// CUDA 12.8+ / cuDNN 9 (docs/17-research/2026-10-16-onnx-runtime-integration.md).
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

constexpr int kModelInput = 640;     // RT-DETRv2-S input width/height
constexpr float kScoreThreshold = 0.3f;

// The exported graph takes pixel_values, i.e. the output of the reference
// RTDetrImageProcessor: resize to 640x640 (bilinear, align_corners=false) then
// rescale by 1/255. Verified against the base model's preprocessor_config.json
// (PekingU/rtdetr_r18vd, 2026-10-17): do_resize=true, resample=2 (bilinear),
// do_rescale=true, rescale_factor=1/255, **do_normalize=false** — the ImageNet
// mean/std present in that file are inert kwargs for this checkpoint.
constexpr float kRescale = 1.0f / 255.0f;

// Bilinear resize with half-pixel centres (align_corners=false), matching the
// reference processor. Returns the source coordinate for a destination index.
float src_coord(int dst, int src_size, int dst_size) {
  const float scale = static_cast<float>(src_size) / static_cast<float>(dst_size);
  float s = (static_cast<float>(dst) + 0.5f) * scale - 0.5f;
  return std::max(0.0f, std::min(static_cast<float>(src_size - 1), s));
}

std::string resolve_model_path() {
  if (const char* env = std::getenv("OW_RTDETR_MODEL")) return env;
  return std::string(OW_MODELS_DIR) + "/rtdetrv2-s-onnx.onnx";
}

// Ort::Session takes ORTCHAR_T* for the model path; on Windows that is wchar_t.
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

float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

class RtdetrV2S final : public PerceptionAdapter {
 public:
  const char* name() const override { return "rtdetr_v2_s"; }

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
    if (W <= 0 || H <= 0 || obs.rgb.size() != static_cast<std::size_t>(W) * H * 3) {
      throw std::invalid_argument("rtdetr_v2_s: malformed observation");
    }

    // ---- preprocessing (adapter-owned): bilinear resize + 1/255 rescale ----
    const std::size_t px = static_cast<std::size_t>(kModelInput) * kModelInput;
    std::vector<float> input(3 * px);
    for (int y = 0; y < kModelInput; ++y) {
      const float fy = src_coord(y, H, kModelInput);
      const int y0 = static_cast<int>(fy);
      const int y1 = std::min(H - 1, y0 + 1);
      const float wy = fy - static_cast<float>(y0);
      for (int x = 0; x < kModelInput; ++x) {
        const float fx = src_coord(x, W, kModelInput);
        const int x0 = static_cast<int>(fx);
        const int x1 = std::min(W - 1, x0 + 1);
        const float wx = fx - static_cast<float>(x0);
        const std::size_t i00 = (static_cast<std::size_t>(y0) * W + x0) * 3;
        const std::size_t i01 = (static_cast<std::size_t>(y0) * W + x1) * 3;
        const std::size_t i10 = (static_cast<std::size_t>(y1) * W + x0) * 3;
        const std::size_t i11 = (static_cast<std::size_t>(y1) * W + x1) * 3;
        const std::size_t dst = static_cast<std::size_t>(y) * kModelInput + x;
        for (int c = 0; c < 3; ++c) {
          const float v =
              (1.0f - wy) * ((1.0f - wx) * obs.rgb[i00 + c] + wx * obs.rgb[i01 + c]) +
              wy * ((1.0f - wx) * obs.rgb[i10 + c] + wx * obs.rgb[i11 + c]);
          input[static_cast<std::size_t>(c) * px + dst] = v * kRescale;
        }
      }
    }
    const int64_t shape[4] = {1, 3, kModelInput, kModelInput};
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        mem_, input.data(), input.size(), shape, 4);

    const char* in_names[1] = {in_name_.c_str()};
    std::vector<const char*> out_names;
    out_names.reserve(out_names_.size());
    for (const std::string& n : out_names_) out_names.push_back(n.c_str());
    std::vector<Ort::Value> outputs =
        session_.Run(Ort::RunOptions{nullptr}, in_names, &tensor, 1,
                     out_names.data(), out_names.size());

    // ---- outputs classified at construction: boxes (last dim 4) / logits ----
    const float* boxes = outputs[box_index_].GetTensorData<float>();
    const auto boxes_shape = outputs[box_index_].GetTensorTypeAndShapeInfo().GetShape();
    const float* logits = outputs[logits_index_].GetTensorData<float>();
    const auto logits_shape =
        outputs[logits_index_].GetTensorTypeAndShapeInfo().GetShape();
    const std::size_t n_boxes = static_cast<std::size_t>(boxes_shape[1]);
    const std::size_t n_classes = static_cast<std::size_t>(logits_shape[2]);

    // ---- postprocessing: per-box max sigmoid(class) over threshold -> world --
    static const bool debug = std::getenv("OW_RTDETR_DEBUG") != nullptr;
    if (debug) {
      float lo = logits[0], hi = logits[0];
      for (std::size_t i = 0; i < n_boxes * n_classes; ++i) {
        lo = std::min(lo, logits[i]);
        hi = std::max(hi, logits[i]);
      }
      std::size_t raw_gt0 = 0;
      for (std::size_t i = 0; i < n_boxes * n_classes; ++i) {
        if (logits[i] > 0.0f) ++raw_gt0;
      }
      std::printf(
          "[rtdetr debug] boxes=%zu classes=%zu logits[%.4f, %.4f] "
          "raw>0 count=%zu\n",
          n_boxes, n_classes, lo, hi, raw_gt0);
    }
    std::vector<Proposal> out;
    for (std::size_t i = 0; i < n_boxes; ++i) {
      float best = 0.0f;
      std::size_t best_class = 0;
      for (std::size_t c = 0; c < n_classes; ++c) {
        const float s = sigmoid(logits[i * n_classes + c]);
        if (s > best) {
          best = s;
          best_class = c;
        }
      }
      if (best < kScoreThreshold) continue;
      // Normalized cxcywh over the image -> world x/z. ViewCamera covers the
      // unit square with an identity pose (v1 top-down: render() stores
      // view.pose but never offsets pixels), and its inverse map is
      // world_x(px) = (px + 0.5) / width. Substituting the box centre
      // px = cx * width - 0.5 returns cx exactly, so the direct assignment
      // below IS the camera mapping, not a bypass of it.
      const float cx = boxes[i * 4 + 0];
      const float cy = boxes[i * 4 + 1];
      if (debug) {
        // Class index (not a name) is printed: the argmax axis is the graph's
        // own class order, and mapping it to a label is the caller's problem --
        // adapters must not mint class tags (core/proposal.hpp:18).
        std::printf("[rtdetr debug] box %zu: cx=%.3f cy=%.3f w=%.3f h=%.3f "
                    "conf=%.3f cls=%zu\n",
                    i, static_cast<double>(cx), static_cast<double>(cy),
                    static_cast<double>(boxes[i * 4 + 2]),
                    static_cast<double>(boxes[i * 4 + 3]),
                    static_cast<double>(best), best_class);
      }
      Proposal p;
      p.producer = name();
      p.tick_observed = obs.tick;
      p.target = NewEntity{};
      Transform t;
      t.position = Vec3{std::min(1.0f, std::max(0.0f, cx)), 0.0f,
                        std::min(1.0f, std::max(0.0f, cy))};
      p.fields.transform = t;
      p.confidence = best;
      out.push_back(std::move(p));
    }
    return out;
  }

 private:
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
            std::string("rtdetr_v2_s: CUDA EP unavailable (") + e.what() +
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
      throw std::runtime_error("rtdetr_v2_s: model load failed at '" + path +
                               "' (" + e.what() +
                               "); run: node tools/fetch_models.mjs");
    }
    Ort::AllocatorWithDefaultOptions alloc;
    in_name_ = session_.GetInputNameAllocated(0, alloc).get();
    const std::size_t n_out = session_.GetOutputCount();
    out_names_.clear();
    box_index_ = logits_index_ = SIZE_MAX;
    for (std::size_t i = 0; i < n_out; ++i) {
      out_names_.push_back(session_.GetOutputNameAllocated(i, alloc).get());
      const auto shape = session_.GetOutputTypeInfo(i)
                             .GetTensorTypeAndShapeInfo()
                             .GetShape();
      if (shape.size() == 3 && shape[2] == 4) box_index_ = i;
      if (shape.size() == 3 && shape[2] != 4) logits_index_ = i;
    }
    if (box_index_ == SIZE_MAX || logits_index_ == SIZE_MAX) {
      throw std::runtime_error(
          "rtdetr_v2_s: cannot classify model outputs (expect boxes[N,4] and "
          "logits[N,C])");
    }
    session_ready_ = true;
  }

  Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "omniworld"};
  Ort::MemoryInfo mem_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
                                                   OrtMemTypeDefault);
  Ort::Session session_{nullptr};
  ExecProvider provider_ = ExecProvider::CpuEp;
  bool session_ready_ = false;
  std::string in_name_;
  std::vector<std::string> out_names_;
  std::size_t box_index_ = SIZE_MAX;
  std::size_t logits_index_ = SIZE_MAX;
};

}  // namespace

OW_REGISTER_PERCEPTION("rtdetr_v2_s", RtdetrV2S)

}  // namespace ow

#endif  // OW_WITH_ONNXRUNTIME
