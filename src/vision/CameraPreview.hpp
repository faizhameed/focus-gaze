#pragma once

#include "vision/CameraSource.hpp"

namespace focusgaze {

/// Live camera debug window (main thread only on macOS).
class CameraPreview {
public:
  /// @return OpenCV waitKey code (0 if none / no OpenCV)
  int tick(const CameraSource* camera);
  void close();

private:
  bool window_open_{false};
};

} // namespace focusgaze
