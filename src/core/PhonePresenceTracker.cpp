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
  visible_streak_start_ = 0;
}

void PhonePresenceTracker::prune(EpochSeconds now) {
  if (window_seconds_ <= 0) return;
  const EpochSeconds cutoff = now - window_seconds_;
  std::vector<Interval> kept;
  for (auto iv : intervals_) {
    if (iv.end <= cutoff) continue;
    if (iv.start < cutoff) iv.start = cutoff;
    kept.push_back(iv);
  }
  intervals_ = std::move(kept);
  if (currently_visible_ && open_start_ < cutoff) open_start_ = cutoff;
}

std::int64_t PhonePresenceTracker::cumulativeUnlocked(EpochSeconds now) const {
  std::int64_t total = 0;
  const EpochSeconds cutoff = now - window_seconds_;
  for (const auto& iv : intervals_) {
    const EpochSeconds a = std::max(iv.start, cutoff);
    if (iv.end > a) total += (iv.end - a);
  }
  if (currently_visible_) {
    const EpochSeconds a = std::max(open_start_, cutoff);
    if (now > a) total += (now - a);
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
  if (!alarm_latched_) return false;
  // Do not use currently_visible_ here — brief flickers would keep the alarm stuck on.
  // Clear timer starts when samples go false; only sustained visible resets it.
  if (non_visible_since_ == 0) return true;
  return (now - non_visible_since_) < clear_cooldown_seconds_;
}

void PhonePresenceTracker::sample(EpochSeconds now, bool phone_visible) {
  if (last_sample_ != 0 && now < last_sample_) last_sample_ = now;

  // Ignore sub-second visible glitches for resetting the clear timer.
  // Only treat as "really visible" for latch/clear if sustained.
  constexpr EpochSeconds kGlitchSec = 1; // need >=1s continuous visible to interrupt clear

  if (phone_visible) {
    if (visible_streak_start_ == 0) visible_streak_start_ = now;
  } else {
    visible_streak_start_ = 0;
  }
  const bool sustained_visible =
      phone_visible && visible_streak_start_ != 0 && (now - visible_streak_start_) >= kGlitchSec;

  if (currently_visible_ && !phone_visible) {
    Interval iv{open_start_, now};
    if (iv.end > iv.start) intervals_.push_back(iv);
    currently_visible_ = false;
    open_start_ = 0;
    if (alarm_latched_) {
      if (non_visible_since_ == 0) non_visible_since_ = now;
    }
  } else if (!currently_visible_ && phone_visible) {
    currently_visible_ = true;
    open_start_ = now;
    // Do not clear non_visible_since_ on brief flicker — only on sustained visible
    if (sustained_visible) {
      non_visible_since_ = 0;
    }
  } else if (currently_visible_ && phone_visible && sustained_visible) {
    non_visible_since_ = 0;
  } else if (!phone_visible && alarm_latched_ && non_visible_since_ == 0) {
    non_visible_since_ = now;
  }

  // If we flickered visible without sustaining, keep counting clear time
  if (!phone_visible && alarm_latched_ && non_visible_since_ == 0) {
    non_visible_since_ = now;
  }

  last_sample_ = now;
  prune(now);

  if (shouldRaiseAlarm(now)) {
    alarm_latched_ = true;
    if (sustained_visible) non_visible_since_ = 0;
    else if (!phone_visible && non_visible_since_ == 0) non_visible_since_ = now;
  }

  if (alarm_latched_ && !phone_visible && non_visible_since_ != 0 &&
      (now - non_visible_since_) >= clear_cooldown_seconds_) {
    alarm_latched_ = false;
    non_visible_since_ = 0;
  }
}

} // namespace focusgaze
