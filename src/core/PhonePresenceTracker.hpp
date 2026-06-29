#pragma once

#include "core/Types.hpp"

#include <cstdint>
#include <vector>

namespace focusgaze {

/// Rolling-window phone visibility; drives phone_window alarm (policy A: clear after cooldown off-frame).
class PhonePresenceTracker {
public:
  PhonePresenceTracker(std::int64_t threshold_seconds = 60,
                       std::int64_t window_seconds = 30 * 60,
                       std::int64_t clear_cooldown_seconds = 3);

  void setThresholdSeconds(std::int64_t s) { threshold_seconds_ = s; }
  void setWindowSeconds(std::int64_t s) { window_seconds_ = s; }
  void setClearCooldownSeconds(std::int64_t s) { clear_cooldown_seconds_ = s; }

  std::int64_t thresholdSeconds() const { return threshold_seconds_; }
  std::int64_t windowSeconds() const { return window_seconds_; }

  /// Feed a sample at absolute epoch seconds (or scaled test time).
  void sample(EpochSeconds now, bool phone_visible);

  /// Cumulative phone-visible seconds inside (now - window, now].
  std::int64_t cumulativeVisibleSeconds(EpochSeconds now) const;

  bool shouldRaiseAlarm(EpochSeconds now) const;
  /// Policy A: alarm clears after continuous non-visible for clear_cooldown_seconds while previously over threshold.
  bool shouldAlarmBeActive(EpochSeconds now) const;

  void reset();

  bool currentlyVisible() const { return currently_visible_; }

private:
  struct Interval {
    EpochSeconds start{0};
    EpochSeconds end{0}; // exclusive; if open, end == 0 and currently_visible_
  };

  void prune(EpochSeconds now);
  std::int64_t cumulativeUnlocked(EpochSeconds now) const;

  std::int64_t threshold_seconds_;
  std::int64_t window_seconds_;
  std::int64_t clear_cooldown_seconds_;
  std::vector<Interval> intervals_;
  bool currently_visible_{false};
  EpochSeconds open_start_{0};
  EpochSeconds last_sample_{0};
  /// When phone became not visible while alarm was warranted; 0 if visible or never.
  EpochSeconds non_visible_since_{0};
  bool alarm_latched_{false};
};

} // namespace focusgaze
