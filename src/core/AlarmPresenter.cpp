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
  // Sound only — never create NSWindow off the main thread.
  sound_thread_ = std::thread([this] { soundLoop(); });
}

void AlarmPresenter::stop() {
  stop_ = true;
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

void AlarmPresenter::tick() {
  std::vector<AlarmReason> reasons;
  {
    std::lock_guard lock(mu_);
    reasons = reasons_;
  }
  if (reasons.empty()) {
    if (overlay_visible_) {
      hideOverlay();
    }
    return;
  }
  showOverlay(messageFor(reasons));
#if defined(FOCUSGAZE_HAS_OPENCV)
  // Pump Cocoa/OpenCV event loop on the main thread.
  cv::waitKey(1);
#endif
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
  oss << "\n(Overlay is main-thread UI; works under Do Not Disturb.)";
  return oss.str();
}

void AlarmPresenter::showOverlay(const std::string& message) {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (overlay_visible_ && message == last_message_) {
    // Still refresh so window stays responsive.
    cv::waitKey(1);
    return;
  }
  last_message_ = message;
  const int W = 900;
  const int H = 500;
  cv::Mat canvas(H, W, CV_8UC3, cv::Scalar(40, 40, 200));
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
  overlay_visible_ = true;
#else
  if (!overlay_visible_ || message != last_message_) {
    last_message_ = message;
    std::cout << "\n==========\n" << message << "\n==========\n" << std::flush;
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
  last_message_.clear();
}

void AlarmPresenter::playBeep() {
#if defined(__APPLE__)
  std::system("afplay /System/Library/Sounds/Sosumi.aiff >/dev/null 2>&1 &");
#elif defined(_WIN32)
  std::system("powershell -c (New-Object Media.SoundPlayer "
              "'C:\\Windows\\Media\\Windows Exclamation.wav').PlaySync(); >/dev/null 2>&1");
#else
  std::cout << '\a' << std::flush;
#endif
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
      for (int i = 0; i < 12 && !stop_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
}

} // namespace focusgaze
