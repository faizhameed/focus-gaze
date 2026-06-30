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

bool tryOpenCapture(cv::VideoCapture& cap, const std::string& video_path) {
  if (!video_path.empty()) {
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
  for (int idx = 0; idx <= 1; ++idx) {
    attempts.push_back({idx, 0, "default"});
  }
#endif

  for (const auto& a : attempts) {
    cap.release();
    const bool ok = (a.api == 0) ? cap.open(a.index) : cap.open(a.index, a.api);
    if (!ok || !cap.isOpened()) {
      continue;
    }
    cv::Mat probe;
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
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
           " opened but no frames");
    cap.release();
  }
  logCam("all camera open attempts failed (TCC / busy camera / backend).");
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

  cv::Mat prev_gray; // motion reference
  bool have_prev{false};

  // Debounce: require sustained evidence (avoids desk phone / flicker).
  // ON needs more consecutive hits than OFF so we clear promptly when phone leaves.
  int consecutive_hit_{0};
  int consecutive_miss_{0};
  bool reported_visible_{false};
  static constexpr int kHitsToActivate = 4;   // ~1.3s at 3fps
  static constexpr int kMissesToClear = 3;    // ~1s at 3fps to drop visible

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

  /// True only if a phone-like rectangle overlaps meaningful *motion*.
  /// Static phone on desk → contours maybe, but little motion → false.
  bool detectActivePhoneUse(const cv::Mat& bgr) {
    if (bgr.empty()) {
      return false;
    }
    cv::Mat small;
    const double scale = 320.0 / std::max(bgr.cols, 1);
    cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);

    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    cv::Mat motion;
    double motion_frac = 0.0;
    if (have_prev && prev_gray.size() == gray.size()) {
      cv::absdiff(gray, prev_gray, motion);
      cv::threshold(motion, motion, 18, 255, cv::THRESH_BINARY);
      motion_frac = static_cast<double>(cv::countNonZero(motion)) /
                    static_cast<double>(motion.rows * motion.cols);
    }
    prev_gray = gray.clone();
    have_prev = true;

    // Almost no global motion → user not handling phone (desk / idle scene).
    if (motion_frac < 0.004) {
      return false;
    }

    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(small.rows * small.cols);
    for (const auto& c : contours) {
      if (c.size() < 5) {
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
      // Tighter phone-ish portrait band
      if (aspect < 1.5 || aspect > 2.6) {
        continue;
      }
      // Must be a noticeable object (not tiny noise), not huge (whole torso)
      if (area < frame_area * 0.03 || area > frame_area * 0.35) {
        continue;
      }
      // Ignore very top of frame (monitors / shelves)
      if (rr.center.y < small.rows * 0.2) {
        continue;
      }

      // Require motion *inside* the candidate box (hand moving phone), not just elsewhere.
      if (!motion.empty()) {
        cv::Rect box = rr.boundingRect();
        box &= cv::Rect(0, 0, motion.cols, motion.rows);
        if (box.area() <= 0) {
          continue;
        }
        const cv::Mat roi = motion(box);
        const double roi_motion =
            static_cast<double>(cv::countNonZero(roi)) / static_cast<double>(box.area());
        // Desk phone: near-zero motion in ROI. Hand use: higher.
        if (roi_motion < 0.04) {
          continue;
        }
        return true;
      }
    }
    return false;
  }

  bool updateDebounced(bool raw_hit) {
    if (raw_hit) {
      ++consecutive_hit_;
      consecutive_miss_ = 0;
    } else {
      ++consecutive_miss_;
      consecutive_hit_ = 0;
    }

    if (!reported_visible_ && consecutive_hit_ >= kHitsToActivate) {
      reported_visible_ = true;
      logCam("active phone use detected (motion+shape, debounced)");
    } else if (reported_visible_ && consecutive_miss_ >= kMissesToClear) {
      reported_visible_ = false;
      logCam("phone activity cleared (no motion/shape, debounced)");
    }
    return reported_visible_;
  }
};

CameraSource::CameraSource(std::string video_path, int target_fps)
    : impl_(std::make_unique<Impl>()) {
  impl_->target_fps = clampFps(target_fps);
#if defined(__APPLE__)
  setenv("OPENCV_VIDEOIO_PRIORITY_LIST", "AVFOUNDATION,FFMPEG", 0);
#endif
  tryOpenCapture(impl_->cap, video_path);
  if (impl_->cap.isOpened()) {
    impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
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
  out_visible = impl_ ? impl_->reported_visible_ : false;
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
        // Treat failed read as miss so alarm can clear.
        out_visible = impl_->updateDebounced(false);
        return true;
      }
    } else {
      out_visible = impl_->updateDebounced(false);
      return true;
    }
  }
  const bool raw = impl_->detectActivePhoneUse(frame);
  out_visible = impl_->updateDebounced(raw);
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
