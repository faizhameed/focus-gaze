#pragma once

/// @file CameraSource.hpp
/// Webcam / fake video capture with YOLO11n phone in-use detection (when ORT+OpenCV available).

#include "vision/YoloDetector.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace focusgaze {

/// Captures frames and estimates active phone use via YOLO cell-phone + motion/hand-zone.
class CameraSource {
public:
  /// @param video_path empty = webcam; else file path (FOCUSGAZE_FAKE_CAMERA)
  /// @param target_fps capture/preview rate (default 15)
  explicit CameraSource(std::string video_path = {}, int target_fps = 15);
  ~CameraSource();

  CameraSource(const CameraSource&) = delete;
  CameraSource& operator=(const CameraSource&) = delete;

  bool isOpen() const;
  bool yoloReady() const;

  /// Grab frame and update debounced in-use visibility.
  bool pollPhoneVisible(bool& out_visible);
  bool reportedVisible() const;

#if defined(FOCUSGAZE_HAS_OPENCV)
  struct DebugBox {
    cv::Rect rect;
    float conf{0.f};
    bool in_use_candidate{false}; // red if true, orange if present-only
  };
  struct DebugSnapshot {
    cv::Mat bgr;
    std::vector<DebugBox> boxes;
    bool raw_in_use{false};
    bool debounced_visible{false};
    bool yolo_loaded{false};
  };
  DebugSnapshot copyDebugSnapshot() const;
#endif

  static std::string resolveVideoPathFromEnv();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace focusgaze
