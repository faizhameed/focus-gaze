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

/// One discoverable capture device (OpenCV index on this machine).
struct CameraDeviceInfo {
  int index{0};
  std::string name; ///< Human label, e.g. "Camera 0 (AVFoundation)"
};

/// Captures frames and estimates active phone use via YOLO cell-phone + motion/hand-zone.
class CameraSource {
public:
  /**
   * @param device_index OpenCV camera index (0 = first). Prefer an explicit index so
   *        Continuity Camera / iPhone does not steal the default open path.
   * @param video_path empty = live camera at device_index; else file path (FOCUSGAZE_FAKE_CAMERA)
   * @param target_fps capture/preview rate (default 15)
   */
  explicit CameraSource(int device_index = 0, std::string video_path = {}, int target_fps = 15);
  ~CameraSource();

  CameraSource(const CameraSource&) = delete;
  CameraSource& operator=(const CameraSource&) = delete;

  bool isOpen() const;
  bool yoloReady() const;
  int deviceIndex() const;

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

  /// Probe indices [0, max_index] for openable devices (best-effort names).
  static std::vector<CameraDeviceInfo> listDevices(int max_index = 6);

  static std::string resolveVideoPathFromEnv();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace focusgaze
