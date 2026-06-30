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
  if (fps > 30) return 30;
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
    return false;
  }
  struct Attempt { int index; int api; const char* name; };
  std::vector<Attempt> attempts;
#if defined(__APPLE__)
  for (int idx = 0; idx <= 2; ++idx)
    attempts.push_back({idx, cv::CAP_AVFOUNDATION, "CAP_AVFOUNDATION"});
#endif
  for (int idx = 0; idx <= 2; ++idx)
    attempts.push_back({idx, cv::CAP_ANY, "CAP_ANY"});
  for (const auto& a : attempts) {
    cap.release();
    const bool ok = (a.api == 0) ? cap.open(a.index) : cap.open(a.index, a.api);
    if (!ok || !cap.isOpened()) continue;
    cv::Mat probe;
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    for (int t = 0; t < 10; ++t) {
      if (cap.read(probe) && !probe.empty()) {
        logCam(std::string("opened camera index=") + std::to_string(a.index) +
               " backend=" + a.name);
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    cap.release();
  }
  logCam("all camera open attempts failed");
  return false;
}
#endif

} // namespace

#if defined(FOCUSGAZE_HAS_OPENCV)

struct CameraSource::Impl {
  cv::VideoCapture cap;
  int target_fps{15}; // capture/preview rate (smooth)
  int detect_every_n_{3}; // run heavy detect every N captures (~5 Hz at 15fps)
  int frame_i_{0};
  std::chrono::steady_clock::time_point last_grab{};
  bool have_last{false};

  cv::Mat prev_gray;
  bool have_prev{false};

  // Time-based hysteresis (more stable than frame counts under load)
  bool reported_visible_{false};
  std::chrono::steady_clock::time_point candidate_since_{};
  std::chrono::steady_clock::time_point clear_since_{};
  bool in_candidate_{false};
  bool in_clearing_{false};
  static constexpr double kOnHoldSec = 0.7;   // sustained activity to turn ON
  static constexpr double kOffHoldSec = 1.2;  // sustained idle to turn OFF

  mutable std::mutex debug_mu_;
  cv::Mat debug_bgr_;
  std::vector<cv::Rect> debug_hits_;
  bool debug_raw_hit_{false};

  bool throttleCapture() {
    const auto now = std::chrono::steady_clock::now();
    if (have_last) {
      const auto min_gap = std::chrono::milliseconds(1000 / std::max(1, target_fps));
      if (now - last_grab < min_gap) return false;
    }
    last_grab = now;
    have_last = true;
    return true;
  }

  void publishDebug(const cv::Mat& full_bgr, const std::vector<cv::Rect>& hits_small,
                    double scale, bool raw_hit) {
    std::vector<cv::Rect> full_hits;
    const double inv = (scale > 1e-6) ? (1.0 / scale) : 1.0;
    for (const auto& r : hits_small) {
      cv::Rect fr(static_cast<int>(r.x * inv), static_cast<int>(r.y * inv),
                  static_cast<int>(r.width * inv), static_cast<int>(r.height * inv));
      if (!full_bgr.empty()) fr &= cv::Rect(0, 0, full_bgr.cols, full_bgr.rows);
      if (fr.area() > 0) full_hits.push_back(fr);
    }
    std::lock_guard lock(debug_mu_);
    debug_bgr_ = full_bgr.clone();
    debug_hits_ = std::move(full_hits);
    debug_raw_hit_ = raw_hit;
  }

  /// Prefer hands / lower frame; exclude face band (top ~42%).
  bool detectActivePhoneUse(const cv::Mat& bgr, std::vector<cv::Rect>& hit_rects_small,
                            double& used_scale) {
    hit_rects_small.clear();
    used_scale = 1.0;
    if (bgr.empty()) return false;

    cv::Mat small;
    used_scale = 480.0 / std::max(bgr.cols, 1); // slightly sharper than 320
    if (used_scale > 1.0) used_scale = 1.0;
    cv::resize(bgr, small, cv::Size(), used_scale, used_scale, cv::INTER_AREA);

    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    cv::Mat motion;
    double motion_frac = 0.0;
    if (have_prev && prev_gray.size() == gray.size()) {
      cv::absdiff(gray, prev_gray, motion);
      cv::threshold(motion, motion, 20, 255, cv::THRESH_BINARY);
      // Ignore motion in face band for global gate
      const int face_band = static_cast<int>(motion.rows * 0.42);
      if (face_band > 0 && face_band < motion.rows) {
        motion(cv::Rect(0, 0, motion.cols, face_band)).setTo(0);
      }
      motion_frac = static_cast<double>(cv::countNonZero(motion)) /
                    static_cast<double>(std::max(1, motion.rows * motion.cols));
    }
    prev_gray = gray.clone();
    have_prev = true;

    // Idle / talking head only in upper frame → low motion in hand zone
    if (motion_frac < 0.0035) {
      return false;
    }

    cv::Mat edges;
    cv::Canny(gray, edges, 60, 160);
    // Zero edges in face band so we never box faces
    const int face_band = static_cast<int>(edges.rows * 0.42);
    if (face_band > 0) {
      edges(cv::Rect(0, 0, edges.cols, face_band)).setTo(0);
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    const double frame_area = static_cast<double>(small.rows * small.cols);
    const double y_min = small.rows * 0.45; // hands / desk — below mid
    bool any = false;

    for (const auto& c : contours) {
      if (c.size() < 6) continue;
      const cv::RotatedRect rr = cv::minAreaRect(c);
      double w = rr.size.width, h = rr.size.height;
      if (w <= 1 || h <= 1) continue;
      if (w > h) std::swap(w, h);
      const double aspect = h / w;
      const double area = w * h;

      // Phones are more elongated than faces (~1.1–1.5). Use stricter band.
      if (aspect < 1.75 || aspect > 2.55) continue;
      // Not tiny noise, not head-sized blobs
      if (area < frame_area * 0.015 || area > frame_area * 0.12) continue;
      // Must sit in lower half (exclude face/center-upper)
      if (rr.center.y < y_min) continue;
      // Reject very centered large blobs (face/torso)
      const double nx = rr.center.x / small.cols;
      if (nx > 0.25 && nx < 0.75 && area > frame_area * 0.08 && aspect < 1.9) continue;

      if (motion.empty()) continue;
      cv::Rect box = rr.boundingRect();
      box &= cv::Rect(0, 0, motion.cols, motion.rows);
      if (box.area() <= 0) continue;
      // Box must not spill mostly into face band
      if (box.y + box.height / 2 < face_band) continue;

      const cv::Mat roi = motion(box);
      const double roi_motion =
          static_cast<double>(cv::countNonZero(roi)) / static_cast<double>(box.area());
      if (roi_motion < 0.06) continue; // stronger than before

      hit_rects_small.push_back(box);
      any = true;
    }
    return any;
  }

  bool updateDebounced(bool raw_hit) {
    const auto now = std::chrono::steady_clock::now();
    auto sec = [](std::chrono::steady_clock::time_point a,
                  std::chrono::steady_clock::time_point b) {
      return std::chrono::duration<double>(b - a).count();
    };

    if (raw_hit) {
      in_clearing_ = false;
      if (!in_candidate_) {
        in_candidate_ = true;
        candidate_since_ = now;
      }
      if (!reported_visible_ && sec(candidate_since_, now) >= kOnHoldSec) {
        reported_visible_ = true;
        logCam("active phone use ON (sustained motion+shape in hand zone)");
      }
    } else {
      in_candidate_ = false;
      if (reported_visible_) {
        if (!in_clearing_) {
          in_clearing_ = true;
          clear_since_ = now;
        } else if (sec(clear_since_, now) >= kOffHoldSec) {
          reported_visible_ = false;
          in_clearing_ = false;
          logCam("active phone use OFF (sustained idle — alarm can clear)");
        }
      } else {
        in_clearing_ = false;
      }
    }
    return reported_visible_;
  }
};

CameraSource::CameraSource(std::string video_path, int target_fps)
    : impl_(std::make_unique<Impl>()) {
  // Smooth preview: capture ~15fps; detect ~5fps
  impl_->target_fps = clampFps(target_fps > 0 ? target_fps : 15);
  if (impl_->target_fps < 10) impl_->target_fps = 15;
  impl_->detect_every_n_ = std::max(1, impl_->target_fps / 5);
#if defined(__APPLE__)
  setenv("OPENCV_VIDEOIO_PRIORITY_LIST", "AVFOUNDATION,FFMPEG", 0);
#endif
  tryOpenCapture(impl_->cap, video_path);
  if (impl_->cap.isOpened()) {
    impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    impl_->cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    impl_->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    impl_->cap.set(cv::CAP_PROP_FPS, 30);
  }
}

CameraSource::~CameraSource() {
  if (impl_ && impl_->cap.isOpened()) impl_->cap.release();
}

bool CameraSource::isOpen() const { return impl_ && impl_->cap.isOpened(); }
bool CameraSource::reportedVisible() const { return impl_ && impl_->reported_visible_; }

CameraSource::DebugSnapshot CameraSource::copyDebugSnapshot() const {
  DebugSnapshot s;
  if (!impl_) return s;
  std::lock_guard lock(impl_->debug_mu_);
  if (!impl_->debug_bgr_.empty()) s.bgr = impl_->debug_bgr_.clone();
  s.hit_rects = impl_->debug_hits_;
  s.raw_hit = impl_->debug_raw_hit_;
  s.debounced_visible = impl_->reported_visible_;
  return s;
}

bool CameraSource::pollPhoneVisible(bool& out_visible) {
  out_visible = impl_ ? impl_->reported_visible_ : false;
  if (!isOpen()) return false;
  if (!impl_->throttleCapture()) return false;

  cv::Mat frame;
  if (!impl_->cap.read(frame) || frame.empty()) {
    if (!resolveVideoPathFromEnv().empty()) {
      impl_->cap.set(cv::CAP_PROP_POS_FRAMES, 0);
      if (!impl_->cap.read(frame) || frame.empty()) {
        out_visible = impl_->updateDebounced(false);
        return true;
      }
    } else {
      out_visible = impl_->updateDebounced(false);
      return true;
    }
  }

  // Always publish frame for smooth preview; run detector on subset of frames.
  ++impl_->frame_i_;
  const bool run_detect = (impl_->frame_i_ % impl_->detect_every_n_) == 0;
  bool raw = false;
  std::vector<cv::Rect> hits;
  double scale = 1.0;
  if (run_detect) {
    raw = impl_->detectActivePhoneUse(frame, hits, scale);
    out_visible = impl_->updateDebounced(raw);
  } else {
    // Keep last debounced state; still refresh preview without red boxes flicker
    out_visible = impl_->reported_visible_;
    std::lock_guard lock(impl_->debug_mu_);
    // preserve last hit rects on preview for continuity unless cleared
    impl_->debug_bgr_ = frame.clone();
    // if not visible, clear rects so face boxes don't linger
    if (!impl_->reported_visible_ && !impl_->debug_raw_hit_) {
      impl_->debug_hits_.clear();
    }
    return true;
  }
  impl_->publishDebug(frame, hits, scale, raw);
  return true;
}

#else

struct CameraSource::Impl {};
CameraSource::CameraSource(std::string, int) : impl_(std::make_unique<Impl>()) {}
CameraSource::~CameraSource() = default;
bool CameraSource::isOpen() const { return false; }
bool CameraSource::reportedVisible() const { return false; }
bool CameraSource::pollPhoneVisible(bool& out_visible) {
  out_visible = false;
  return false;
}

#endif

std::string CameraSource::resolveVideoPathFromEnv() {
  if (const char* p = std::getenv("FOCUSGAZE_FAKE_CAMERA"); p && p[0]) return std::string(p);
  return {};
}

} // namespace focusgaze
