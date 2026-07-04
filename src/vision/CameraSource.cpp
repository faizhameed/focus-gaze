#include "vision/CameraSource.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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

void logCam(const std::string& msg) {
  if (const char* q = std::getenv("FOCUSGAZE_QUIET"); q && q[0] && q[0] != '0') return;
  std::cerr << "[focusGaze/camera] " << msg << std::endl;
}

#if defined(FOCUSGAZE_HAS_OPENCV)
/// Open a single device index (or video file). Does not scan other indices.
bool tryOpenCapture(cv::VideoCapture& cap, int device_index, const std::string& video_path) {
  if (!video_path.empty()) {
#if defined(__APPLE__)
    if (cap.open(video_path, cv::CAP_AVFOUNDATION) && cap.isOpened()) return true;
    cap.release();
#endif
    return cap.open(video_path) && cap.isOpened();
  }
  if (device_index < 0) device_index = 0;
#if defined(__APPLE__)
  cap.release();
  if (cap.open(device_index, cv::CAP_AVFOUNDATION) && cap.isOpened()) {
    cv::Mat probe;
    for (int t = 0; t < 10; ++t) {
      if (cap.read(probe) && !probe.empty()) {
        logCam("opened camera index=" + std::to_string(device_index) + " CAP_AVFOUNDATION");
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    // Opened but no frames (often Continuity / denied permission) — treat as failure.
    logCam("camera index=" + std::to_string(device_index) + " opened but produced no frames");
    cap.release();
  }
#endif
  cap.release();
  if (cap.open(device_index) && cap.isOpened()) {
    cv::Mat probe;
    if (cap.read(probe) && !probe.empty()) {
      logCam("opened camera index=" + std::to_string(device_index));
      return true;
    }
    logCam("camera index=" + std::to_string(device_index) + " open without frames");
    cap.release();
  }
  logCam("camera open failed index=" + std::to_string(device_index));
  return false;
}
#endif

} // namespace

#if defined(FOCUSGAZE_HAS_OPENCV)

struct CameraSource::Impl {
  cv::VideoCapture cap;
  int device_index_{0};
  int target_fps{15};
  int detect_every_n_{3};
  int frame_i_{0};
  std::chrono::steady_clock::time_point last_grab{};
  bool have_last{false};

  cv::Mat prev_gray;
  bool have_prev{false};

  YoloDetector yolo;
  bool yolo_ok{false};

  bool reported_visible_{false};
  std::chrono::steady_clock::time_point candidate_since_{};
  std::chrono::steady_clock::time_point clear_since_{};
  bool in_candidate_{false};
  bool in_clearing_{false};
  static constexpr double kOnHoldSec = 0.5;
  static constexpr double kOffHoldSec = 1.0; // sustained not-in-use → vision OFF → alarm clears

  mutable std::mutex debug_mu_;
  cv::Mat debug_bgr_;
  std::vector<CameraSource::DebugBox> debug_boxes_;
  bool debug_raw_{false};

  bool throttleCapture() {
    const auto now = std::chrono::steady_clock::now();
    if (have_last) {
      const auto gap = std::chrono::milliseconds(1000 / std::max(1, target_fps));
      if (now - last_grab < gap) return false;
    }
    last_grab = now;
    have_last = true;
    return true;
  }

  /// Motion fraction inside ROI (original image coords).
  float roiMotionFrac(const cv::Mat& gray_full, const cv::Rect& roi) {
    if (!have_prev || prev_gray.size() != gray_full.size() || roi.area() <= 0) return 0.f;
    cv::Rect r = roi & cv::Rect(0, 0, gray_full.cols, gray_full.rows);
    if (r.area() <= 0) return 0.f;
    cv::Mat diff;
    cv::absdiff(gray_full(r), prev_gray(r), diff);
    cv::threshold(diff, diff, 18, 255, cv::THRESH_BINARY);
    return static_cast<float>(cv::countNonZero(diff)) / static_cast<float>(r.area());
  }

  /// phone_present vs phone_in_use (hand zone + motion in phone box).
  bool evaluateInUse(const cv::Mat& bgr, std::vector<CameraSource::DebugBox>& boxes_out) {
    boxes_out.clear();
    if (!yolo_ok || bgr.empty()) return false;

    // Lower conf so tilted / landscape phones still detect (YOLO is weaker off-axis).
    const auto dets = YoloDetector::filterCellPhones(yolo.detect(bgr, 0.22f, 0.45f), 0.22f);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    bool any_in_use = false;
    // Only treat as "resting on desk" when deep in the bottom of the FOV + nearly still.
    // Held phones (portrait or landscape/tilted) sit higher and count as in-use even if still.
    const float desk_y = bgr.rows * 0.58f;

    for (const auto& d : dets) {
      CameraSource::DebugBox db;
      db.rect = cv::Rect(cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                         cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)));
      db.conf = d.confidence;
      const float cy = d.cy();
      const float motion = roiMotionFrac(gray, db.rect);
      const float bw = d.width();
      const float bh = d.height();
      // Flat-ish on desk (any orientation) in the bottom band with almost no motion.
      const bool resting_on_desk = (cy >= desk_y) && (motion < 0.035f);
      // Default: any detected cell phone is in-use unless clearly parked on the desk.
      // Covers horizontal / tilted holds mid-frame without requiring continuous motion.
      db.in_use_candidate = !resting_on_desk;
      (void)bw;
      (void)bh;
      boxes_out.push_back(db);
      if (db.in_use_candidate) any_in_use = true;
    }

    prev_gray = gray;
    have_prev = true;
    return any_in_use;
  }

  bool updateDebounced(bool raw_in_use) {
    const auto now = std::chrono::steady_clock::now();
    auto sec = [](auto a, auto b) { return std::chrono::duration<double>(b - a).count(); };
    if (raw_in_use) {
      in_clearing_ = false;
      if (!in_candidate_) {
        in_candidate_ = true;
        candidate_since_ = now;
      }
      if (!reported_visible_ && sec(candidate_since_, now) >= kOnHoldSec) {
        reported_visible_ = true;
        logCam("phone_in_use ON (YOLO cell phone + motion/zone)");
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
          logCam("phone_in_use OFF (sustained — alarm can clear)");
        }
      } else {
        in_clearing_ = false;
      }
    }
    return reported_visible_;
  }

  void publishDebug(const cv::Mat& bgr, const std::vector<CameraSource::DebugBox>& boxes,
                    bool raw) {
    std::lock_guard lock(debug_mu_);
    debug_bgr_ = bgr.clone();
    debug_boxes_ = boxes;
    debug_raw_ = raw;
  }
};

CameraSource::CameraSource(int device_index, std::string video_path, int target_fps)
    : impl_(std::make_unique<Impl>()) {
  impl_->device_index_ = device_index < 0 ? 0 : device_index;
  impl_->target_fps = clampFps(target_fps < 10 ? 15 : target_fps);
  impl_->detect_every_n_ = std::max(1, impl_->target_fps / 5);
#if defined(__APPLE__)
  setenv("OPENCV_VIDEOIO_PRIORITY_LIST", "AVFOUNDATION,FFMPEG", 0);
#endif
  tryOpenCapture(impl_->cap, impl_->device_index_, video_path);
  if (impl_->cap.isOpened()) {
    impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    impl_->cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    impl_->cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
  }
  const auto model = resolveYoloModelPath();
#if defined(FOCUSGAZE_HAS_YOLO)
  impl_->yolo_ok = impl_->yolo.load(model);
  if (!impl_->yolo_ok) {
    logCam("YOLO unavailable (" + model.string() + ") — phone vision disabled (use inject)");
  } else {
    logCam("YOLO11n ready: " + model.string());
  }
#else
  impl_->yolo_ok = false;
  logCam("built without ONNX Runtime — phone vision disabled");
  (void)model;
#endif
}

CameraSource::~CameraSource() {
  if (impl_ && impl_->cap.isOpened()) impl_->cap.release();
}

bool CameraSource::isOpen() const { return impl_ && impl_->cap.isOpened(); }
bool CameraSource::yoloReady() const { return impl_ && impl_->yolo_ok; }
int CameraSource::deviceIndex() const { return impl_ ? impl_->device_index_ : 0; }

std::vector<CameraDeviceInfo> CameraSource::listDevices(int max_index) {
  std::vector<CameraDeviceInfo> out;
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (max_index < 0) max_index = 0;
  if (max_index > 16) max_index = 16;
#if defined(__APPLE__)
  setenv("OPENCV_VIDEOIO_PRIORITY_LIST", "AVFOUNDATION,FFMPEG", 0);
#endif
  for (int idx = 0; idx <= max_index; ++idx) {
    cv::VideoCapture cap;
    bool ok = false;
#if defined(__APPLE__)
    if (cap.open(idx, cv::CAP_AVFOUNDATION) && cap.isOpened()) {
      cv::Mat probe;
      for (int t = 0; t < 6; ++t) {
        if (cap.read(probe) && !probe.empty()) { ok = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
    }
#endif
    if (!ok) {
      cap.release();
      if (cap.open(idx) && cap.isOpened()) {
        cv::Mat probe;
        if (cap.read(probe) && !probe.empty()) ok = true;
      }
    }
    cap.release();
    if (!ok) continue;
    CameraDeviceInfo info;
    info.index = idx;
    // OpenCV rarely exposes friendly names; give stable labels for the UI.
    info.name = "Camera " + std::to_string(idx);
    if (idx == 0) info.name += " (often built-in / Continuity if iPhone is preferred by macOS)";
    else info.name += " (external or secondary)";
    out.push_back(info);
  }
#else
  (void)max_index;
#endif
  return out;
}

bool CameraSource::reportedVisible() const { return impl_ && impl_->reported_visible_; }

CameraSource::DebugSnapshot CameraSource::copyDebugSnapshot() const {
  DebugSnapshot s;
  if (!impl_) return s;
  std::lock_guard lock(impl_->debug_mu_);
  if (!impl_->debug_bgr_.empty()) s.bgr = impl_->debug_bgr_.clone();
  s.boxes = impl_->debug_boxes_;
  s.raw_in_use = impl_->debug_raw_;
  s.debounced_visible = impl_->reported_visible_;
  s.yolo_loaded = impl_->yolo_ok;
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

  ++impl_->frame_i_;
  const bool run_detect = (impl_->frame_i_ % impl_->detect_every_n_) == 0;
  if (!impl_->yolo_ok) {
    // No model: never claim in-use from vision
    impl_->publishDebug(frame, {}, false);
    out_visible = impl_->updateDebounced(false);
    return true;
  }

  if (run_detect) {
    std::vector<DebugBox> boxes;
    const bool raw = impl_->evaluateInUse(frame, boxes);
    out_visible = impl_->updateDebounced(raw);
    impl_->publishDebug(frame, boxes, raw);
  } else {
    out_visible = impl_->reported_visible_;
    std::lock_guard lock(impl_->debug_mu_);
    impl_->debug_bgr_ = frame.clone();
  }
  return true;
}

#else

struct CameraSource::Impl { int device_index_{0}; };
CameraSource::CameraSource(int device_index, std::string, int) : impl_(std::make_unique<Impl>()) {
  impl_->device_index_ = device_index < 0 ? 0 : device_index;
}
CameraSource::~CameraSource() = default;
bool CameraSource::isOpen() const { return false; }
bool CameraSource::yoloReady() const { return false; }
int CameraSource::deviceIndex() const { return impl_ ? impl_->device_index_ : 0; }
std::vector<CameraDeviceInfo> CameraSource::listDevices(int) { return {}; }
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
