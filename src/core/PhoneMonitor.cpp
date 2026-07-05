#include "core/PhoneMonitor.hpp"

#include <chrono>

namespace focusgaze {
namespace {

EpochSeconds wallNowSeconds() {
  return static_cast<EpochSeconds>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

} // namespace

PhoneMonitor::PhoneMonitor(Storage& storage, FocusSessionManager& focus, AlarmController& alarms,
                           Settings settings)
    : storage_(storage),
      focus_(focus),
      alarms_(alarms),
      settings_(std::move(settings)),
      tracker_(settings_.phone_threshold_seconds, settings_.phone_window_seconds, 2) {}

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
  // Flush open interval so the last use is recorded for session stats.
  if (last_visible_ && interval_start_) {
    try {
      std::optional<std::int64_t> sid;
      if (auto s = focus_.activeSession()) sid = s->id;
      else if (auto open = storage_.getActiveSession()) sid = open->id;
      storage_.insertPhoneEvent(sid, *interval_start_, wallNowSeconds(), 1.0);
      ++phone_intervals_logged_;
    } catch (...) {
    }
  }
  tracker_.reset();
  alarms_.setPhoneAlarm(false);
  last_visible_ = false;
  interval_start_.reset();
  phone_use_count_ = 0;
  phone_intervals_logged_ = 0;
}

void PhoneMonitor::forceClearAlarm() {
  std::lock_guard lock(mutex_);
  tracker_.reset();
  alarms_.setPhoneAlarm(false);
  last_visible_ = false;
  interval_start_.reset();
  // Keep use counters — user dismissed alarm, not end of session.
}

void PhoneMonitor::reset() {
  std::lock_guard lock(mutex_);
  tracker_.reset();
  last_visible_ = false;
  interval_start_.reset();
  phone_use_count_ = 0;
  phone_intervals_logged_ = 0;
}

void PhoneMonitor::sample(EpochSeconds now, bool phone_visible) {
  std::lock_guard lock(mutex_);
  if (!focusOn()) {
    tracker_.reset();
    alarms_.setPhoneAlarm(false);
    last_visible_ = false;
    interval_start_.reset();
    phone_use_count_ = 0;
    phone_intervals_logged_ = 0;
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
      ++phone_intervals_logged_;
    } catch (...) {
    }
    interval_start_.reset();
  } else if (!last_visible_ && phone_visible) {
    interval_start_ = now;
    ++phone_use_count_; // each distinct pick-up / in-use bout
  }

  tracker_.sample(now, phone_visible);
  last_visible_ = phone_visible;

  // Vision already applied sustained OFF hysteresis (~1.5s). When it reports not in-use,
  // clear the phone alarm immediately so overlay matches logs (phone_visible=no).
  if (!phone_visible) {
    tracker_.clearAlarmLatch();
    alarms_.setPhoneAlarm(false);
  } else {
    alarms_.setPhoneAlarm(tracker_.shouldAlarmBeActive(now));
  }
}

PhoneStatus PhoneMonitor::status(EpochSeconds now) const {
  std::lock_guard lock(mutex_);
  PhoneStatus st;
  st.focus_on = focusOn();
  st.phone_visible = tracker_.currentlyVisible();
  st.cumulative_visible_seconds = tracker_.cumulativeVisibleSeconds(now);
  st.threshold_seconds = tracker_.thresholdSeconds();
  st.window_seconds = tracker_.windowSeconds();
  // Alarm only while in-use; if tracker not currently visible, alarm is off.
  st.phone_alarm = tracker_.currentlyVisible() && tracker_.shouldAlarmBeActive(now);
  st.phone_use_count = phone_use_count_;
  st.phone_intervals_logged = phone_intervals_logged_;
  if (interval_start_ && last_visible_ && now >= *interval_start_) {
    st.open_bout_seconds = now - *interval_start_;
  }
  return st;
}

} // namespace focusgaze
