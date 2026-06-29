#include "core/PhonePresenceTracker.hpp"

#include <algorithm>

namespace focusgaze {

PhonePresenceTracker::PhonePresenceTracker(std::int64_t threshold_seconds,
                                           std::int64_t window_seconds,
                                           std::int64_t clear_cooldown_seconds)
    : threshold_seconds_(threshold_seconds),
      window_seconds_(window_seconds),
      clear_cooldown_seconds_(clear_cooldown_seconds) {}

void PhonePresenceTracker::reset() {
  intervals_.clear();
  currently_visible_ = false;
  open_start_ = 0;
  last_sample_ = 0;
  non_visible_since_ = 0;
  alarm_latched_ = false;
}

void PhonePresenceTracker::prune(EpochSeconds now) {
  if (window_seconds_ <= 0) {
    return;
  }
  const EpochSeconds cutoff = now - window_seconds_;
  std::vector<Interval> kept;
  for (auto iv : intervals_) {
    if (iv.end <= cutoff) {
      continue;
    }
    if (iv.start < cutoff) {
      iv.start = cutoff;
    }
    kept.push_back(iv);
  }
  intervals_ = std::move(kept);
  if (currently_visible_ && open_start_ < cutoff) {
    open_start_ = cutoff;
  }
}

std::int64_t PhonePresenceTracker::cumulativeUnlocked(EpochSeconds now) const {
  std::int64_t total = 0;
  const EpochSeconds cutoff = now - window_seconds_;
  for (const auto& iv : intervals_) {
    const EpochSeconds a = std::max(iv.start, cutoff);
    const EpochSeconds b = iv.end;
    if (b > a) {
      total += (b - a);
    }
  }
  if (currently_visible_) {
    const EpochSeconds a = std::max(open_start_, cutoff);
    if (now > a) {
      total += (now - a);
    }
  }
  return total;
}

std::int64_t PhonePresenceTracker::cumulativeVisibleSeconds(EpochSeconds now) const {
  return cumulativeUnlocked(now);
}

bool PhonePresenceTracker::shouldRaiseAlarm(EpochSeconds now) const {
  return cumulativeUnlocked(now) > threshold_seconds_;
}

bool PhonePresenceTracker::shouldAlarmBeActive(EpochSeconds now) const {
  // Policy A: once raised, stays until continuous non-visible for clear_cooldown_seconds.
  // Do not re-assert solely from historical cumulative after clear (window may still be over threshold).
  if (!alarm_latched_) {
    return false;
  }
  if (currently_visible_) {
    return true;
  }
  if (non_visible_since_ == 0) {
    return true;
  }
  return (now - non_visible_since_) < clear_cooldown_seconds_;
}

void PhonePresenceTracker::sample(EpochSeconds now, bool phone_visible) {
  if (last_sample_ != 0 && now < last_sample_) {
    // Clock went backwards (tests); reset softly.
    last_sample_ = now;
  }

  if (currently_visible_ && !phone_visible) {
    // Close open interval
    Interval iv;
    iv.start = open_start_;
    iv.end = now;
    if (iv.end > iv.start) {
      intervals_.push_back(iv);
    }
    currently_visible_ = false;
    open_start_ = 0;
    if (alarm_latched_ || shouldRaiseAlarm(now)) {
      non_visible_since_ = now;
    }
  } else if (!currently_visible_ && phone_visible) {
    currently_visible_ = true;
    open_start_ = now;
    non_visible_since_ = 0;
  }

  last_sample_ = now;
  prune(now);

  if (shouldRaiseAlarm(now)) {
    alarm_latched_ = true;
    if (phone_visible) {
      non_visible_since_ = 0;
    } else if (non_visible_since_ == 0) {
      non_visible_since_ = now;
    }
  }

  // Clear latch after cooldown off-frame
  if (alarm_latched_ && !currently_visible_ && non_visible_since_ != 0 &&
      (now - non_visible_since_) >= clear_cooldown_seconds_) {
    alarm_latched_ = false;
    non_visible_since_ = 0;
  }
}

} // namespace focusgaze
