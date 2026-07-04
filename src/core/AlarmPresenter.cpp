#include "core/AlarmPresenter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace focusgaze {

AlarmPresenter::AlarmPresenter() = default;
AlarmPresenter::~AlarmPresenter() { stop(); }

void AlarmPresenter::start() {
  if (running_.load()) return;
  stop_ = false;
  running_ = true;
  sound_thread_ = std::thread([this] { soundLoop(); });
}

void AlarmPresenter::stop() {
  stop_ = true;
  if (sound_thread_.joinable()) sound_thread_.join();
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
    if (overlay_visible_) hideOverlay();
    return;
  }
  showOverlay(messageFor(reasons));
}

std::string AlarmPresenter::messageFor(const std::vector<AlarmReason>& reasons) const {
  if (reasons.empty()) return {};
  bool social = false, phone = false;
  for (const auto r : reasons) {
    if (r == AlarmReason::SocialTab) social = true;
    if (r == AlarmReason::PhoneWindow) phone = true;
  }
  std::ostringstream oss;
  oss << "focusGaze ALARM — ";
  if (social) oss << "Close social / blocked tab(s). ";
  if (phone) oss << "Put the phone down. ";
  oss << "(Also shown on camera window; press D to dismiss phone alarm.)";
  return oss.str();
}

void AlarmPresenter::showOverlay(const std::string& message) {
  // No OpenCV windows here — avoids NSWindow / multi-thread OpenCV crashes on macOS.
  // CameraPreview draws the on-screen banner; we only log once per message change.
  if (!overlay_visible_ || message != last_message_) {
    last_message_ = message;
    std::cout << "\n*** " << message << " ***\n" << std::flush;
    overlay_visible_ = true;
  }
}

void AlarmPresenter::hideOverlay() {
  if (overlay_visible_) {
    std::cout << "[focusGaze] alarm cleared\n" << std::flush;
  }
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
      for (int i = 0; i < 12 && !stop_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
}

} // namespace focusgaze
