#include "core/PhoneMonitor.hpp"

namespace focusgaze {

PhoneMonitor::PhoneMonitor(Storage& storage, FocusSessionManager& focus, AlarmController& alarms,
                           Settings settings)
    : storage_(storage),
      focus_(focus),
      alarms_(alarms),
      settings_(std::move(settings)),
      tracker_(settings_.phone_threshold_seconds, settings_.phone_window_seconds, 3) {}

void PhoneMonitor::setSettings(const Settings& settings) {
  std::lock_guard lock(mutex_);
  settings_ = settings;
  tracker_.setThresholdSeconds(settings_.phone_threshold_seconds);
  tracker_.setWindowSeconds(settings_.phone_window_seconds);
}

bool PhoneMonitor::focusOn() const {
  return storage_.getActiveSession().has_value() || focus_.isFocusOn();
}

void PhoneMonitor::onFocusTurnedOff() {
  std::lock_guard lock(mutex_);
  tracker_.reset();
  alarms_.setPhoneAlarm(false);
  last_visible_ = false;
  interval_start_.reset();
}

void PhoneMonitor::reset() {
  std::lock_guard lock(mutex_);
  tracker_.reset();
  last_visible_ = false;
  interval_start_.reset();
}

void PhoneMonitor::sample(EpochSeconds now, bool phone_visible) {
  std::lock_guard lock(mutex_);
  if (!focusOn()) {
    tracker_.reset();
    alarms_.setPhoneAlarm(false);
    last_visible_ = false;
    interval_start_.reset();
    return;
  }

  // Persist closed phone intervals for stats
  if (last_visible_ && !phone_visible && interval_start_) {
    try {
      std::optional<std::int64_t> sid;
      if (auto s = focus_.activeSession()) {
        sid = s->id;
      } else if (auto open = storage_.getActiveSession()) {
        sid = open->id;
      }
      storage_.insertPhoneEvent(sid, *interval_start_, now, 1.0);
    } catch (...) {
    }
    interval_start_.reset();
  } else if (!last_visible_ && phone_visible) {
    interval_start_ = now;
  }

  tracker_.sample(now, phone_visible);
  last_visible_ = phone_visible;
  alarms_.setPhoneAlarm(tracker_.shouldAlarmBeActive(now));
}

PhoneStatus PhoneMonitor::status(EpochSeconds now) const {
  std::lock_guard lock(mutex_);
  PhoneStatus st;
  st.focus_on = focusOn();
  st.phone_visible = tracker_.currentlyVisible();
  st.cumulative_visible_seconds = tracker_.cumulativeVisibleSeconds(now);
  st.threshold_seconds = tracker_.thresholdSeconds();
  st.window_seconds = tracker_.windowSeconds();
  st.phone_alarm = tracker_.shouldAlarmBeActive(now);
  return st;
}

} // namespace focusgaze
