#pragma once

#include "core/AlarmController.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace focusgaze {

/// Overlay + sound while any alarm reason is active.
/// IMPORTANT (macOS): call tick() only from the main thread — OpenCV highgui uses NSWindow.
class AlarmPresenter {
public:
  AlarmPresenter();
  ~AlarmPresenter();

  AlarmPresenter(const AlarmPresenter&) = delete;
  AlarmPresenter& operator=(const AlarmPresenter&) = delete;

  void start();
  void stop();

  /// Update active reasons (thread-safe).
  void setActiveReasons(std::vector<AlarmReason> reasons);

  /// Apply overlay changes. Must run on the main thread on macOS.
  void tick();

private:
  std::string messageFor(const std::vector<AlarmReason>& reasons) const;
  void showOverlay(const std::string& message);
  void hideOverlay();
  void soundLoop();
  void playBeep();

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  std::vector<AlarmReason> reasons_;
  bool overlay_visible_{false};
  std::string last_message_;
  std::thread sound_thread_;
};

} // namespace focusgaze
