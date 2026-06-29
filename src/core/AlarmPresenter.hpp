#pragma once

#include "core/AlarmController.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace focusgaze {

/// Always-on-top style overlay + sound loop while any alarm reason is active.
class AlarmPresenter {
public:
  AlarmPresenter();
  ~AlarmPresenter();

  AlarmPresenter(const AlarmPresenter&) = delete;
  AlarmPresenter& operator=(const AlarmPresenter&) = delete;

  void start();
  void stop();

  /// Update active reasons (empty = clear overlay/sound).
  void setActiveReasons(std::vector<AlarmReason> reasons);

private:
  void uiLoop();
  void soundLoop();
  std::string messageFor(const std::vector<AlarmReason>& reasons) const;
  void showOverlay(const std::string& message);
  void hideOverlay();
  void playBeep();

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  std::vector<AlarmReason> reasons_;
  bool overlay_visible_{false};
  std::thread ui_thread_;
  std::thread sound_thread_;
};

} // namespace focusgaze
