#pragma once

#include "vision/CameraSource.hpp"

#include <string>

namespace focusgaze {

/// Live camera debug window — ONLY OpenCV UI entry point (main thread).
class CameraPreview {
public:
  /// @param alarm_banner non-empty draws red alert strip on the video
  /// @return OpenCV waitKey code
  int tick(const CameraSource* camera, const std::string& alarm_banner = {});
  void close();

private:
  bool window_open_{false};
};

} // namespace focusgaze
