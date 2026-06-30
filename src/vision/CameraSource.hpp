#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace focusgaze {

/// Captures frames and estimates active phone use (motion + shape).
/// Exposes a debug snapshot (frame + hit rectangles) for UI preview on the main thread.
class CameraSource {
public:
  explicit CameraSource(std::string video_path = {}, int target_fps = 3);
  ~CameraSource();

  CameraSource(const CameraSource&) = delete;
  CameraSource& operator=(const CameraSource&) = delete;

  bool isOpen() const;
  /// Grab/process a frame. Updates debounced visibility and debug snapshot.
  bool pollPhoneVisible(bool& out_visible);

  /// Debounced "active phone use" state.
  bool reportedVisible() const;

#if defined(FOCUSGAZE_HAS_OPENCV)
  struct DebugSnapshot {
    cv::Mat bgr;                       // BGR preview frame (may be empty)
    std::vector<cv::Rect> hit_rects;   // regions that contributed to a raw hit this frame
    bool raw_hit{false};               // this frame's undebounced decision
    bool debounced_visible{false};
  };
  /// Thread-safe copy of latest snapshot for main-thread UI.
  DebugSnapshot copyDebugSnapshot() const;
#endif

  static std::string resolveVideoPathFromEnv();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace focusgaze
