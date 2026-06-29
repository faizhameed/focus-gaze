#include "vision/VisionLoop.hpp"

#include <chrono>

namespace focusgaze {

VisionLoop::VisionLoop(PhoneMonitor& phone, VisibilitySource source, ClockFn clock,
                       int interval_ms)
    : phone_(phone),
      source_(std::move(source)),
      clock_(std::move(clock)),
      interval_ms_(interval_ms) {}

VisionLoop::~VisionLoop() { stop(); }

void VisionLoop::setInjectedVisibility(std::optional<bool> visible) {
  std::lock_guard lock(inject_mutex_);
  injected_ = visible;
}

void VisionLoop::start() {
  if (running_.load()) {
    return;
  }
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void VisionLoop::stop() {
  stop_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  running_ = false;
}

void VisionLoop::run() {
  running_ = true;
  while (!stop_.load()) {
    bool visible = false;
    {
      std::lock_guard lock(inject_mutex_);
      if (injected_.has_value()) {
        visible = *injected_;
      } else if (source_) {
        visible = source_();
      }
    }
    phone_.sample(clock_(), visible);
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms_));
  }
  running_ = false;
}

} // namespace focusgaze
