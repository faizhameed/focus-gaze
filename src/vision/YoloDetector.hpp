#pragma once

/// @file YoloDetector.hpp
/// YOLO11n ONNX detector (cell phone) via ONNX Runtime.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace focusgaze {

/// One axis-aligned detection in original image coordinates.
struct YoloDetection {
  int class_id{0};
  float confidence{0.f};
  float x1{0.f};
  float y1{0.f};
  float x2{0.f};
  float y2{0.f};

  float cx() const { return 0.5f * (x1 + x2); }
  float cy() const { return 0.5f * (y1 + y2); }
  float width() const { return x2 - x1; }
  float height() const { return y2 - y1; }
};

/// Resolve model path (env, data dir, repo models/, executable-relative).
std::filesystem::path resolveYoloModelPath();

#if defined(FOCUSGAZE_HAS_YOLO)

/// Thin ONNX Runtime wrapper for Ultralytics YOLO11n export (1x84xN output).
class YoloDetector {
public:
  /// COCO "cell phone" class index.
  static constexpr int kCellPhoneClassId = 67;

  YoloDetector();
  ~YoloDetector();

  YoloDetector(const YoloDetector&) = delete;
  YoloDetector& operator=(const YoloDetector&) = delete;

  /// Load model from path. Returns false on failure (logs to stderr).
  bool load(const std::filesystem::path& model_path);

  bool isLoaded() const { return loaded_; }

  /// Run detection on BGR image. Returns all classes above conf (caller filters).
  /// @param conf_threshold minimum class score
  /// @param iou_threshold NMS IoU
  std::vector<YoloDetection> detect(const cv::Mat& bgr, float conf_threshold = 0.35f,
                                    float iou_threshold = 0.45f);

  /// Filter to cell phones only.
  static std::vector<YoloDetection> filterCellPhones(const std::vector<YoloDetection>& all,
                                                     float conf_threshold = 0.35f);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool loaded_{false};
  int input_w_{320};
  int input_h_{320};
};

#else

/// Stub when ONNX Runtime is not linked.
class YoloDetector {
public:
  static constexpr int kCellPhoneClassId = 67;
  YoloDetector() = default;
  bool load(const std::filesystem::path&) { return false; }
  bool isLoaded() const { return false; }
#if defined(FOCUSGAZE_HAS_OPENCV)
  std::vector<YoloDetection> detect(const cv::Mat&, float = 0.35f, float = 0.45f) { return {}; }
#endif
  static std::vector<YoloDetection> filterCellPhones(const std::vector<YoloDetection>& all,
                                                     float conf_threshold = 0.35f);
};

#endif

} // namespace focusgaze
