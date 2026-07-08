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
  /// Distinct phone “pick-up” intervals in the current Focus session (starts of in-use).
  int phone_use_count{0};
  /// Closed intervals already written to SQLite for this session (subset of use_count when mid-use).
  int phone_intervals_logged{0};
  /// Seconds in the current open in-use bout (0 if phone not currently visible).
  std::int64_t open_bout_seconds{0};
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

  /// User dismiss / escape hatch — clears phone latch and alarm reason.
  void forceClearAlarm();

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
  /// How many times phone went from not-visible → visible while Focus is on (this session).
  int phone_use_count_{0};
  /// How many closed intervals were written to the DB this session.
  int phone_intervals_logged_{0};
};

} // namespace focusgaze
