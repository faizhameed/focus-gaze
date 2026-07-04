#include "vision/YoloDetector.hpp"

#include "core/PlatformPaths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>

#if defined(FOCUSGAZE_HAS_YOLO)
#include <onnxruntime_cxx_api.h>
#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>
#endif
#endif

namespace focusgaze {
namespace {

void logYolo(const std::string& msg) {
  if (const char* q = std::getenv("FOCUSGAZE_QUIET"); q && q[0] && q[0] != '0') {
    return;
  }
  std::cerr << "[focusGaze/yolo] " << msg << std::endl;
}

#if defined(FOCUSGAZE_HAS_YOLO) && defined(FOCUSGAZE_HAS_OPENCV)
/// Letterbox resize preserving aspect ratio into square dst_size.
struct LetterboxInfo {
  float scale{1.f};
  float pad_x{0.f};
  float pad_y{0.f};
};

LetterboxInfo letterbox(const cv::Mat& src, cv::Mat& dst, int dst_size) {
  LetterboxInfo info;
  const int w = src.cols;
  const int h = src.rows;
  info.scale = std::min(static_cast<float>(dst_size) / w, static_cast<float>(dst_size) / h);
  const int nw = static_cast<int>(std::round(w * info.scale));
  const int nh = static_cast<int>(std::round(h * info.scale));
  info.pad_x = (dst_size - nw) * 0.5f;
  info.pad_y = (dst_size - nh) * 0.5f;
  cv::Mat resized;
  cv::resize(src, resized, cv::Size(nw, nh));
  dst = cv::Mat(dst_size, dst_size, CV_8UC3, cv::Scalar(114, 114, 114));
  resized.copyTo(dst(cv::Rect(static_cast<int>(info.pad_x), static_cast<int>(info.pad_y), nw, nh)));
  return info;
}

float iouBox(const YoloDetection& a, const YoloDetection& b) {
  const float xx1 = std::max(a.x1, b.x1);
  const float yy1 = std::max(a.y1, b.y1);
  const float xx2 = std::min(a.x2, b.x2);
  const float yy2 = std::min(a.y2, b.y2);
  const float w = std::max(0.f, xx2 - xx1);
  const float h = std::max(0.f, yy2 - yy1);
  const float inter = w * h;
  const float area_a = std::max(0.f, a.x2 - a.x1) * std::max(0.f, a.y2 - a.y1);
  const float area_b = std::max(0.f, b.x2 - b.x1) * std::max(0.f, b.y2 - b.y1);
  const float uni = area_a + area_b - inter + 1e-6f;
  return inter / uni;
}

std::vector<YoloDetection> nms(std::vector<YoloDetection> boxes, float iou_thr) {
  std::sort(boxes.begin(), boxes.end(),
            [](const YoloDetection& a, const YoloDetection& b) { return a.confidence > b.confidence; });
  std::vector<YoloDetection> keep;
  std::vector<char> removed(boxes.size(), 0);
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    if (removed[i]) continue;
    keep.push_back(boxes[i]);
    for (std::size_t j = i + 1; j < boxes.size(); ++j) {
      if (removed[j]) continue;
      if (boxes[i].class_id != boxes[j].class_id) continue;
      if (iouBox(boxes[i], boxes[j]) > iou_thr) removed[j] = 1;
    }
  }
  return keep;
}
#endif

} // namespace

std::filesystem::path resolveYoloModelPath() {
  if (const char* env = std::getenv("FOCUSGAZE_YOLO_MODEL"); env && env[0]) {
    return std::filesystem::path{env};
  }
  const auto data_model = PlatformPaths::dataRoot() / "models" / "yolo11n.onnx";
  std::error_code ec;
  if (std::filesystem::is_regular_file(data_model, ec)) {
    return data_model;
  }
  // Dev: repo models/ next to CWD or common relatives
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "models" / "yolo11n.onnx",
      std::filesystem::current_path() / ".." / "models" / "yolo11n.onnx",
      std::filesystem::current_path() / ".." / "Resources" / "models" / "yolo11n.onnx",
      std::filesystem::path{"models/yolo11n.onnx"},
  };
  for (const auto& c : candidates) {
    if (std::filesystem::is_regular_file(c, ec)) {
      return std::filesystem::weakly_canonical(c, ec);
    }
  }
  return data_model; // default location even if missing (caller reports error)
}

#if defined(FOCUSGAZE_HAS_YOLO)

struct YoloDetector::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "focusGaze"};
  Ort::SessionOptions opts;
  std::unique_ptr<Ort::Session> session;
  std::vector<std::string> input_names_str;
  std::vector<std::string> output_names_str;
  std::vector<const char*> input_names;
  std::vector<const char*> output_names;
};

YoloDetector::YoloDetector() : impl_(std::make_unique<Impl>()) {
  impl_->opts.SetIntraOpNumThreads(2);
  impl_->opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

YoloDetector::~YoloDetector() = default;

bool YoloDetector::load(const std::filesystem::path& model_path) {
  loaded_ = false;
  if (!impl_) impl_ = std::make_unique<Impl>();
  std::error_code ec;
  if (!std::filesystem::is_regular_file(model_path, ec)) {
    logYolo("model not found: " + model_path.string());
    return false;
  }
  try {
#if defined(_WIN32)
    const std::wstring wpath = model_path.wstring();
    impl_->session = std::make_unique<Ort::Session>(impl_->env, wpath.c_str(), impl_->opts);
#else
    impl_->session =
        std::make_unique<Ort::Session>(impl_->env, model_path.string().c_str(), impl_->opts);
#endif
    Ort::AllocatorWithDefaultOptions alloc;
    const std::size_t n_in = impl_->session->GetInputCount();
    const std::size_t n_out = impl_->session->GetOutputCount();
    impl_->input_names_str.clear();
    impl_->output_names_str.clear();
    impl_->input_names.clear();
    impl_->output_names.clear();
    for (std::size_t i = 0; i < n_in; ++i) {
      auto name = impl_->session->GetInputNameAllocated(i, alloc);
      impl_->input_names_str.emplace_back(name.get());
    }
    for (std::size_t i = 0; i < n_out; ++i) {
      auto name = impl_->session->GetOutputNameAllocated(i, alloc);
      impl_->output_names_str.emplace_back(name.get());
    }
    for (auto& s : impl_->input_names_str) impl_->input_names.push_back(s.c_str());
    for (auto& s : impl_->output_names_str) impl_->output_names.push_back(s.c_str());

    auto in_info = impl_->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    // Expect [1,3,H,W] or dynamic
    if (in_info.size() == 4) {
      if (in_info[2] > 0) input_h_ = static_cast<int>(in_info[2]);
      if (in_info[3] > 0) input_w_ = static_cast<int>(in_info[3]);
    }
    loaded_ = true;
    logYolo("loaded " + model_path.string() + " input=" + std::to_string(input_w_) + "x" +
            std::to_string(input_h_));
    return true;
  } catch (const Ort::Exception& e) {
    logYolo(std::string("ORT load failed: ") + e.what());
    return false;
  } catch (const std::exception& e) {
    logYolo(std::string("load failed: ") + e.what());
    return false;
  }
}

std::vector<YoloDetection> YoloDetector::detect(const cv::Mat& bgr, float conf_threshold,
                                                float iou_threshold) {
  std::vector<YoloDetection> empty;
  if (!loaded_ || !impl_ || !impl_->session || bgr.empty()) {
    return empty;
  }
#if !defined(FOCUSGAZE_HAS_OPENCV)
  (void)conf_threshold;
  (void)iou_threshold;
  return empty;
#else
  try {
    cv::Mat lb;
    const LetterboxInfo lb_info = letterbox(bgr, lb, input_w_);
    cv::Mat rgb;
    cv::cvtColor(lb, rgb, cv::COLOR_BGR2RGB);
    // NCHW float32 0..1
    std::vector<float> input(1 * 3 * input_h_ * input_w_);
    for (int y = 0; y < input_h_; ++y) {
      for (int x = 0; x < input_w_; ++x) {
        const cv::Vec3b pix = rgb.at<cv::Vec3b>(y, x);
        input[0 * input_h_ * input_w_ + y * input_w_ + x] = pix[0] / 255.f;
        input[1 * input_h_ * input_w_ + y * input_w_ + x] = pix[1] / 255.f;
        input[2 * input_h_ * input_w_ + y * input_w_ + x] = pix[2] / 255.f;
      }
    }
    const std::array<int64_t, 4> shape{1, 3, input_h_, input_w_};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        mem, input.data(), input.size(), shape.data(), shape.size());

    auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, impl_->input_names.data(),
                                       &input_tensor, 1, impl_->output_names.data(),
                                       impl_->output_names.size());
    if (outputs.empty()) return empty;

    float* data = outputs[0].GetTensorMutableData<float>();
    auto out_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    // Ultralytics: [1, 84, N] or [1, N, 84]
    int64_t dim1 = out_shape.size() >= 2 ? out_shape[1] : 0;
    int64_t dim2 = out_shape.size() >= 3 ? out_shape[2] : 0;
    const bool channels_first = (dim1 == 84 || dim1 == 85);
    const int64_t num_attrs = channels_first ? dim1 : dim2;
    const int64_t num_preds = channels_first ? dim2 : dim1;
    const int num_classes = static_cast<int>(num_attrs - 4);

    std::vector<YoloDetection> proposals;
    proposals.reserve(static_cast<std::size_t>(num_preds / 4));

    auto at = [&](int64_t a, int64_t p) -> float {
      if (channels_first) {
        return data[a * num_preds + p];
      }
      return data[p * num_attrs + a];
    };

    for (int64_t p = 0; p < num_preds; ++p) {
      float best_score = 0.f;
      int best_cls = -1;
      for (int c = 0; c < num_classes; ++c) {
        const float s = at(4 + c, p);
        if (s > best_score) {
          best_score = s;
          best_cls = c;
        }
      }
      if (best_score < conf_threshold || best_cls < 0) continue;

      // xywh center format in letterboxed space
      float cx = at(0, p);
      float cy = at(1, p);
      float w = at(2, p);
      float h = at(3, p);
      float x1 = cx - w * 0.5f;
      float y1 = cy - h * 0.5f;
      float x2 = cx + w * 0.5f;
      float y2 = cy + h * 0.5f;

      // Undo letterbox to original image coords
      x1 = (x1 - lb_info.pad_x) / lb_info.scale;
      y1 = (y1 - lb_info.pad_y) / lb_info.scale;
      x2 = (x2 - lb_info.pad_x) / lb_info.scale;
      y2 = (y2 - lb_info.pad_y) / lb_info.scale;

      x1 = std::clamp(x1, 0.f, static_cast<float>(bgr.cols - 1));
      y1 = std::clamp(y1, 0.f, static_cast<float>(bgr.rows - 1));
      x2 = std::clamp(x2, 0.f, static_cast<float>(bgr.cols - 1));
      y2 = std::clamp(y2, 0.f, static_cast<float>(bgr.rows - 1));
      if (x2 <= x1 || y2 <= y1) continue;

      YoloDetection d;
      d.class_id = best_cls;
      d.confidence = best_score;
      d.x1 = x1;
      d.y1 = y1;
      d.x2 = x2;
      d.y2 = y2;
      proposals.push_back(d);
    }
    return nms(std::move(proposals), iou_threshold);
  } catch (const Ort::Exception& e) {
    logYolo(std::string("infer failed: ") + e.what());
    return empty;
  }
#endif
}

#endif // FOCUSGAZE_HAS_YOLO

std::vector<YoloDetection> YoloDetector::filterCellPhones(const std::vector<YoloDetection>& all,
                                                          float conf_threshold) {
  std::vector<YoloDetection> out;
  for (const auto& d : all) {
    if (d.class_id == YoloDetector::kCellPhoneClassId && d.confidence >= conf_threshold) {
      out.push_back(d);
    }
  }
  return out;
}

} // namespace focusgaze
