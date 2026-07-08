#pragma once

#include "core/AlarmController.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace focusgaze {

/// Overlay + sound while any alarm reason is active.
/// Sound is played via an optional callback (set from the UI layer) so macOS GUI apps
/// can use QProcess/afplay on a reliable path instead of a bare std::system thread.
class AlarmPresenter {
public:
  /// Invoked on the sound worker thread when a beep should play (keep it non-blocking).
  using SoundPlayer = std::function<void()>;

  AlarmPresenter();
  ~AlarmPresenter();

  AlarmPresenter(const AlarmPresenter&) = delete;
  AlarmPresenter& operator=(const AlarmPresenter&) = delete;

  void start();
  void stop();

  /// When false, active alarms still set reasons but no audio plays.
  void setSoundEnabled(bool enabled);
  bool soundEnabled() const { return sound_enabled_.load(); }

  /// Optional custom player (e.g. QProcess afplay). Empty = built-in system beep.
  void setSoundPlayer(SoundPlayer player);

  /// Fire one beep immediately (settings “Test alarm sound”).
  void playTestSound();

  /// Update active reasons (thread-safe).
  void setActiveReasons(std::vector<AlarmReason> reasons);

  /// Apply overlay changes. Must run on the main thread on macOS.
  void tick();

  /// Built-in paths for named system sounds (macOS). Returns empty if unknown.
  static std::string resolveSystemSoundPath(const std::string& name);

private:
  std::string messageFor(const std::vector<AlarmReason>& reasons) const;
  void showOverlay(const std::string& message);
  void hideOverlay();
  void soundLoop();
  void playBeep();

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<bool> sound_enabled_{true};
  std::mutex mu_;
  std::vector<AlarmReason> reasons_;
  SoundPlayer sound_player_;
  bool overlay_visible_{false};
  std::string last_message_;
  std::thread sound_thread_;
};

} // namespace focusgaze
