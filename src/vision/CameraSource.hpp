#pragma once

#include <memory>
#include <string>

namespace focusgaze {

/// Captures frames and estimates whether a phone is visible (heuristic).
/// Built when FOCUSGAZE_HAS_OPENCV is defined; otherwise a no-op stub.
class CameraSource {
public:
  /// Open default camera (device 0) or video path if non-empty.
  /// target_fps throttles grab rate (e.g. 2–5).
  explicit CameraSource(std::string video_path = {}, int target_fps = 3);
  ~CameraSource();

  CameraSource(const CameraSource&) = delete;
  CameraSource& operator=(const CameraSource&) = delete;

  bool isOpen() const;
  /// Grab frame and run phone heuristic. Returns false if no frame.
  bool pollPhoneVisible(bool& out_visible);

  static std::string resolveVideoPathFromEnv();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace focusgaze
