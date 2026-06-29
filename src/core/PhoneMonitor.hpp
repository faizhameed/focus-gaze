#pragma once

#include "core/AlarmController.hpp"
#include "core/FocusSession.hpp"
#include "core/PhonePresenceTracker.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <mutex>
#include <optional>

namespace focusgaze {

struct PhoneStatus {
  bool focus_on{false};
  bool phone_visible{false};
  std::int64_t cumulative_visible_seconds{0};
  std::int64_t threshold_seconds{60};
  std::int64_t window_seconds{1800};
  bool phone_alarm{false};
};

/// Applies phone visibility samples when Focus is on; updates phone_window alarm (policy A).
class PhoneMonitor {
public:
  PhoneMonitor(Storage& storage, FocusSessionManager& focus, AlarmController& alarms,
               Settings settings);

  void setSettings(const Settings& settings);
  /// now: epoch seconds (inject time scale in tests / CLI).
  void sample(EpochSeconds now, bool phone_visible);

  PhoneStatus status(EpochSeconds now) const;
  void onFocusTurnedOff();
  void reset();

  PhonePresenceTracker& tracker() { return tracker_; }

private:
  bool focusOn() const;

  Storage& storage_;
  FocusSessionManager& focus_;
  AlarmController& alarms_;
  mutable std::mutex mutex_;
  Settings settings_;
  PhonePresenceTracker tracker_;
  bool last_visible_{false};
  std::optional<EpochSeconds> interval_start_;
};

} // namespace focusgaze
