#pragma once

#include "core/PhoneMonitor.hpp"
#include "core/Types.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace focusgaze {

/// Background loop: poll phone visibility and feed PhoneMonitor.
class VisionLoop {
public:
  using VisibilitySource = std::function<bool()>;
  using ClockFn = std::function<EpochSeconds()>;

  VisionLoop(PhoneMonitor& phone, VisibilitySource source, ClockFn clock,
             int interval_ms = 500);
  ~VisionLoop();

  VisionLoop(const VisionLoop&) = delete;
  VisionLoop& operator=(const VisionLoop&) = delete;

  void start();
  void stop();
  bool isRunning() const { return running_.load(); }

  void setInjectedVisibility(std::optional<bool> visible);

private:
  void run();

  PhoneMonitor& phone_;
  VisibilitySource source_;
  ClockFn clock_;
  int interval_ms_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::mutex inject_mutex_;
  std::optional<bool> injected_;
  std::thread thread_;
};

} // namespace focusgaze
