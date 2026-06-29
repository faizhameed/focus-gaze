#include "core/AlarmPresenter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

AlarmPresenter::AlarmPresenter() = default;

AlarmPresenter::~AlarmPresenter() { stop(); }

void AlarmPresenter::start() {
  if (running_.load()) {
    return;
  }
  stop_ = false;
  running_ = true;
  ui_thread_ = std::thread([this] { uiLoop(); });
  sound_thread_ = std::thread([this] { soundLoop(); });
}

void AlarmPresenter::stop() {
  stop_ = true;
  if (ui_thread_.joinable()) {
    ui_thread_.join();
  }
  if (sound_thread_.joinable()) {
    sound_thread_.join();
  }
  hideOverlay();
  running_ = false;
}

void AlarmPresenter::setActiveReasons(std::vector<AlarmReason> reasons) {
  std::lock_guard lock(mu_);
  reasons_ = std::move(reasons);
}

std::string AlarmPresenter::messageFor(const std::vector<AlarmReason>& reasons) const {
  if (reasons.empty()) {
    return {};
  }
  bool social = false;
  bool phone = false;
  for (const auto r : reasons) {
    if (r == AlarmReason::SocialTab) {
      social = true;
    }
    if (r == AlarmReason::PhoneWindow) {
      phone = true;
    }
  }
  std::ostringstream oss;
  oss << "focusGaze ALARM\n\n";
  if (social) {
    oss << "Close social / blocked tab(s) to continue.\n";
  }
  if (phone) {
    oss << "Put the phone down to continue.\n";
  }
  oss << "\n(Overlay stays up under Do Not Disturb; sound may be muted.)";
  return oss.str();
}

void AlarmPresenter::showOverlay(const std::string& message) {
#if defined(FOCUSGAZE_HAS_OPENCV)
  const int W = 900;
  const int H = 500;
  cv::Mat canvas(H, W, CV_8UC3, cv::Scalar(40, 40, 200)); // BGR red-ish
  std::stringstream ss(message);
  std::string line;
  int y = 80;
  while (std::getline(ss, line)) {
    cv::putText(canvas, line, cv::Point(40, y), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
    y += 40;
  }
  cv::namedWindow("focusGaze ALARM", cv::WINDOW_NORMAL);
  cv::resizeWindow("focusGaze ALARM", W, H);
  try {
    cv::setWindowProperty("focusGaze ALARM", cv::WND_PROP_TOPMOST, 1);
  } catch (...) {
  }
  cv::imshow("focusGaze ALARM", canvas);
  cv::waitKey(1);
  overlay_visible_ = true;
#else
  if (!overlay_visible_) {
    std::cout << "\n========== " << message << " ==========\n" << std::flush;
    overlay_visible_ = true;
  }
#endif
}

void AlarmPresenter::hideOverlay() {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (overlay_visible_) {
    try {
      cv::destroyWindow("focusGaze ALARM");
      cv::waitKey(1);
    } catch (...) {
    }
  }
#endif
  overlay_visible_ = false;
}

void AlarmPresenter::playBeep() {
#if defined(__APPLE__)
  // System sound — still attempt even if Mac volume is low; overlay is primary under mute.
  std::system("afplay /System/Library/Sounds/Sosumi.aiff >/dev/null 2>&1 &");
#elif defined(_WIN32)
  // Best-effort; overlay remains primary.
  std::system("powershell -c (New-Object Media.SoundPlayer "
              "'C:\\Windows\\Media\\Windows Exclamation.wav').PlaySync(); >/dev/null 2>&1");
#else
  std::cout << '\a' << std::flush;
#endif
}

void AlarmPresenter::uiLoop() {
  while (!stop_.load()) {
    std::vector<AlarmReason> reasons;
    {
      std::lock_guard lock(mu_);
      reasons = reasons_;
    }
    if (reasons.empty()) {
      hideOverlay();
    } else {
      showOverlay(messageFor(reasons));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  hideOverlay();
}

void AlarmPresenter::soundLoop() {
  while (!stop_.load()) {
    bool active = false;
    {
      std::lock_guard lock(mu_);
      active = !reasons_.empty();
    }
    if (active) {
      playBeep();
      // Loop cadence ~1.2s
      for (int i = 0; i < 12 && !stop_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
}

} // namespace focusgaze
