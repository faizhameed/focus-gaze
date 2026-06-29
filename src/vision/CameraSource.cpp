#include "vision/CameraSource.hpp"

#include <chrono>
#include <cstdlib>

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

} // namespace

#if defined(FOCUSGAZE_HAS_OPENCV)

struct CameraSource::Impl {
  cv::VideoCapture cap;
  int target_fps{3};
  std::chrono::steady_clock::time_point last_grab{};
  bool have_last{false};

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

  /// Heuristic: phone-like rectangular contours (portrait aspect) with strong edges.
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
      const double aspect = h / w; // portrait phone ~ 1.5–2.5
      const double area = w * h;
      if (aspect < 1.4 || aspect > 2.8) {
        continue;
      }
      if (area < frame_area * 0.02 || area > frame_area * 0.45) {
        continue;
      }
      // Prefer lower/middle of frame (hands)
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
  if (!video_path.empty()) {
    impl_->cap.open(video_path);
  } else {
    impl_->cap.open(0);
  }
  if (impl_->cap.isOpened()) {
    impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
  }
}

CameraSource::~CameraSource() = default;

bool CameraSource::isOpen() const { return impl_ && impl_->cap.isOpened(); }

bool CameraSource::pollPhoneVisible(bool& out_visible) {
  out_visible = false;
  if (!isOpen()) {
    return false;
  }
  if (!impl_->throttle()) {
    // Keep last decision sticky within throttle window: re-grab not needed
    return false;
  }
  cv::Mat frame;
  if (!impl_->cap.read(frame) || frame.empty()) {
    // Loop video files
    if (!CameraSource::resolveVideoPathFromEnv().empty()) {
      impl_->cap.set(cv::CAP_PROP_POS_FRAMES, 0);
      if (!impl_->cap.read(frame) || frame.empty()) {
        return false;
      }
    } else {
      return false;
    }
  }
  out_visible = Impl::detectPhoneHeuristic(frame);
  return true;
}

#else

struct CameraSource::Impl {};

CameraSource::CameraSource(std::string, int) : impl_(std::make_unique<Impl>()) {}
CameraSource::~CameraSource() = default;
bool CameraSource::isOpen() const { return false; }
bool CameraSource::pollPhoneVisible(bool&) { return false; }

#endif

std::string CameraSource::resolveVideoPathFromEnv() {
  if (const char* p = std::getenv("FOCUSGAZE_FAKE_CAMERA"); p != nullptr && p[0] != '\0') {
    return std::string(p);
  }
  return {};
}

} // namespace focusgaze
