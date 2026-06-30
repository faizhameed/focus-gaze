#include "vision/CameraSource.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace focusgaze {
namespace {

int clampFps(int fps) {
  if (fps < 1) return 1;
  if (fps > 10) return 10;
  return fps;
}

#if defined(FOCUSGAZE_HAS_OPENCV)
void logCam(const std::string& msg) {
  if (const char* q = std::getenv("FOCUSGAZE_QUIET"); q && q[0] && q[0] != '0') {
    return;
  }
  std::cerr << "[focusGaze/camera] " << msg << std::endl;
}

/// Prefer AVFoundation on macOS — default open(0) often hits FFmpeg and fails with
/// "Failed list devices for backend avfoundation".
bool tryOpenCapture(cv::VideoCapture& cap, const std::string& video_path) {
  if (!video_path.empty()) {
    // File/URL: try AVFoundation first on Apple (for .mov), then default.
#if defined(__APPLE__)
    if (cap.open(video_path, cv::CAP_AVFOUNDATION) && cap.isOpened()) {
      logCam("opened video via CAP_AVFOUNDATION: " + video_path);
      return true;
    }
    cap.release();
#endif
    if (cap.open(video_path) && cap.isOpened()) {
      logCam("opened video via default backend: " + video_path);
      return true;
    }
    logCam("failed to open video: " + video_path);
    return false;
  }

  // Live camera: explicit backends + indices (0 = default FaceTime, sometimes 1 on Continuity).
  struct Attempt {
    int index;
    int api;
    const char* name;
  };

  std::vector<Attempt> attempts;
#if defined(__APPLE__)
  for (int idx = 0; idx <= 2; ++idx) {
    attempts.push_back({idx, cv::CAP_AVFOUNDATION, "CAP_AVFOUNDATION"});
  }
#endif
  for (int idx = 0; idx <= 2; ++idx) {
    attempts.push_back({idx, cv::CAP_ANY, "CAP_ANY"});
  }
#if defined(__APPLE__)
  // Last resort: integer API preference not used by some builds
  for (int idx = 0; idx <= 1; ++idx) {
    attempts.push_back({idx, 0, "default"});
  }
#endif

  for (const auto& a : attempts) {
    cap.release();
    bool ok = false;
    if (a.api == 0) {
      ok = cap.open(a.index);
    } else {
      ok = cap.open(a.index, a.api);
    }
    if (!ok || !cap.isOpened()) {
      continue;
    }
    // Verify we can actually grab a frame (isOpened can lie briefly).
    cv::Mat probe;
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    // Give AVFoundation a moment after TCC grant
    for (int t = 0; t < 10; ++t) {
      if (cap.read(probe) && !probe.empty()) {
        logCam(std::string("opened camera index=") + std::to_string(a.index) + " backend=" +
               a.name + " frame=" + std::to_string(probe.cols) + "x" +
               std::to_string(probe.rows));
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    logCam(std::string("camera index=") + std::to_string(a.index) + " backend=" + a.name +
           " opened but no frames (permission or busy?)");
    cap.release();
  }

  logCam("all camera open attempts failed. On macOS grant Camera to Terminal/iTerm "
         "(System Settings → Privacy & Security → Camera), quit and relaunch the terminal, "
         "and ensure no other app exclusively holds the camera.");
  return false;
}
#endif

} // namespace

#if defined(FOCUSGAZE_HAS_OPENCV)

struct CameraSource::Impl {
  cv::VideoCapture cap;
  int target_fps{3};
  std::chrono::steady_clock::time_point last_grab{};
  bool have_last{false};
  bool last_visible{false};

  bool throttle() {
    const auto now = std::chrono::steady_clock::now();
    if (have_last) {
      const auto min_gap = std::chrono::milliseconds(1000 / target_fps);
      if (now - last_grab < min_gap) {
        return false;
      }
    }
    last_grab = now;
    have_last = true;
    return true;
  }

  static bool detectPhoneHeuristic(const cv::Mat& bgr) {
    if (bgr.empty()) {
      return false;
    }
    cv::Mat small;
    const double scale = 320.0 / std::max(bgr.cols, 1);
    cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::Mat gray, blur, edges;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blur, cv::Size(5, 5), 0);
    cv::Canny(blur, edges, 50, 150);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(small.rows * small.cols);
    int phone_like = 0;
    for (const auto& c : contours) {
      if (c.size() < 4) {
        continue;
      }
      const cv::RotatedRect rr = cv::minAreaRect(c);
      double w = rr.size.width;
      double h = rr.size.height;
      if (w <= 1 || h <= 1) {
        continue;
      }
      if (w > h) {
        std::swap(w, h);
      }
      const double aspect = h / w;
      const double area = w * h;
      if (aspect < 1.4 || aspect > 2.8) {
        continue;
      }
      if (area < frame_area * 0.02 || area > frame_area * 0.45) {
        continue;
      }
      if (rr.center.y < small.rows * 0.15) {
        continue;
      }
      ++phone_like;
    }
    return phone_like >= 1;
  }
};

CameraSource::CameraSource(std::string video_path, int target_fps) : impl_(std::make_unique<Impl>()) {
  impl_->target_fps = clampFps(target_fps);
  // Discourage FFmpeg device enumeration on macOS when AVFoundation is available.
#if defined(__APPLE__)
  setenv("OPENCV_VIDEOIO_PRIORITY_LIST", "AVFOUNDATION,FFMPEG", 0);
#endif
  tryOpenCapture(impl_->cap, video_path);
  if (impl_->cap.isOpened()) {
    impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    // Prefer modest resolution for stability on Continuity / laptop cams.
    impl_->cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    impl_->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
  }
}

CameraSource::~CameraSource() {
  if (impl_ && impl_->cap.isOpened()) {
    impl_->cap.release();
  }
}

bool CameraSource::isOpen() const { return impl_ && impl_->cap.isOpened(); }

bool CameraSource::pollPhoneVisible(bool& out_visible) {
  out_visible = impl_ ? impl_->last_visible : false;
  if (!isOpen()) {
    return false;
  }
  if (!impl_->throttle()) {
    return false;
  }
  cv::Mat frame;
  if (!impl_->cap.read(frame) || frame.empty()) {
    if (!CameraSource::resolveVideoPathFromEnv().empty()) {
      impl_->cap.set(cv::CAP_PROP_POS_FRAMES, 0);
      if (!impl_->cap.read(frame) || frame.empty()) {
        return false;
      }
    } else {
      return false;
    }
  }
  const bool vis = Impl::detectPhoneHeuristic(frame);
  if (vis != impl_->last_visible) {
    logCam(std::string("phone_heuristic visible=") + (vis ? "true" : "false"));
  }
  impl_->last_visible = vis;
  out_visible = vis;
  return true;
}

#else

struct CameraSource::Impl {};

CameraSource::CameraSource(std::string, int) : impl_(std::make_unique<Impl>()) {}
CameraSource::~CameraSource() = default;
bool CameraSource::isOpen() const { return false; }
bool CameraSource::pollPhoneVisible(bool& out_visible) {
  out_visible = false;
  return false;
}

#endif

std::string CameraSource::resolveVideoPathFromEnv() {
  if (const char* p = std::getenv("FOCUSGAZE_FAKE_CAMERA"); p != nullptr && p[0] != '\0') {
    return std::string(p);
  }
  return {};
}

} // namespace focusgaze
