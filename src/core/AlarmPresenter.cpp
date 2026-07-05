#include "core/AlarmPresenter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
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

void AlarmPresenter::setSoundEnabled(bool enabled) { sound_enabled_.store(enabled); }

void AlarmPresenter::setSoundPlayer(SoundPlayer player) {
  std::lock_guard lock(mu_);
  sound_player_ = std::move(player);
}

void AlarmPresenter::playTestSound() { playBeep(); }

std::string AlarmPresenter::resolveSystemSoundPath(const std::string& name) {
#if defined(__APPLE__)
  // macOS system sounds under /System/Library/Sounds
  static const std::map<std::string, std::string> kMap = {
      {"default", "/System/Library/Sounds/Sosumi.aiff"},
      {"sosumi", "/System/Library/Sounds/Sosumi.aiff"},
      {"basso", "/System/Library/Sounds/Basso.aiff"},
      {"blow", "/System/Library/Sounds/Blow.aiff"},
      {"bottle", "/System/Library/Sounds/Bottle.aiff"},
      {"frog", "/System/Library/Sounds/Frog.aiff"},
      {"funk", "/System/Library/Sounds/Funk.aiff"},
      {"glass", "/System/Library/Sounds/Glass.aiff"},
      {"hero", "/System/Library/Sounds/Hero.aiff"},
      {"morse", "/System/Library/Sounds/Morse.aiff"},
      {"ping", "/System/Library/Sounds/Ping.aiff"},
      {"pop", "/System/Library/Sounds/Pop.aiff"},
      {"purr", "/System/Library/Sounds/Purr.aiff"},
      {"submarine", "/System/Library/Sounds/Submarine.aiff"},
      {"tink", "/System/Library/Sounds/Tink.aiff"},
  };
  std::string key = name;
  for (char& c : key) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (const auto it = kMap.find(key); it != kMap.end()) return it->second;
  // Absolute path allowed for custom files.
  if (!name.empty() && name.front() == '/') return name;
  return kMap.at("default");
#else
  (void)name;
  return {};
#endif
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
  if (!sound_enabled_.load()) return;
  SoundPlayer player;
  {
    std::lock_guard lock(mu_);
    player = sound_player_;
  }
  if (player) {
    try {
      player();
    } catch (...) {
      // never kill the sound thread
    }
    return;
  }
#if defined(__APPLE__)
  // Fallback when no UI player is installed.
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
    if (active && sound_enabled_.load()) {
      playBeep();
      // ~1.2s between beeps while alarm is sticky.
      for (int i = 0; i < 12 && !stop_.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
}

} // namespace focusgaze
