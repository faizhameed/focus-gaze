#pragma once

#include "vision/CameraSource.hpp"

namespace focusgaze {

/// Live camera debug window (main thread only on macOS — uses OpenCV highgui / NSWindow).
class CameraPreview {
public:
  /// Show/update preview from camera snapshot. Call from main thread in serve loop.
  void tick(const CameraSource* camera);

  void close();

private:
  bool window_open_{false};
};

} // namespace focusgaze
