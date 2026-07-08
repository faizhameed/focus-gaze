#include "core/FocusSession.hpp"

namespace focusgaze {

EpochSeconds FocusSessionManager::defaultTimeProvider() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             Clock::now().time_since_epoch())
      .count();
}

FocusSessionManager::FocusSessionManager(Storage& storage, Settings settings,
                                         TimeProvider time_provider)
    : storage_(storage),
      settings_(std::move(settings)),
      time_provider_(std::move(time_provider)) {
  syncFromStorage();
}

void FocusSessionManager::syncFromStorage() {
  if (!storage_.isOpen()) {
    active_.reset();
    return;
  }
  active_ = storage_.getActiveSession();
}

bool FocusSessionManager::isCounting() const {
  if (!active_) return false;
  try {
    return storage_.getOpenFocusSegment(active_->id).has_value();
  } catch (const StorageError&) {
    return false;
  }
}

void FocusSessionManager::pauseCounting(const std::string& reason) {
  if (!active_) return;
  const auto now = time_provider_();
  try {
    // End at "now": the process is running and just observed lock (or explicit pause).
    // Sleep/orphan paths end at last_seen instead (see onPresenceTick / closeOrphan).
    if (auto open = storage_.getOpenFocusSegment(active_->id)) {
      EpochSeconds end_at = now;
      if (end_at < open->started_at) end_at = open->started_at;
      storage_.endOpenFocusSegments(active_->id, end_at, reason);
    }
  } catch (const StorageError&) {
  }
}

void FocusSessionManager::resumeCounting() {
  if (!active_) return;
  const auto now = time_provider_();
  try {
    if (!storage_.getOpenFocusSegment(active_->id)) {
      storage_.startFocusSegment(active_->id, now);
    }
  } catch (const StorageError&) {
  }
}

void FocusSessionManager::prepareForProcessExit() {
  const auto now = time_provider_();
  try {
    if (active_) {
      storage_.endOpenFocusSegments(active_->id, now, "process_exit");
    } else {
      storage_.endOpenFocusSegments(std::nullopt, now, "process_exit");
    }
  } catch (const StorageError&) {
  }
}

void FocusSessionManager::onPresenceTick(bool interactive_session_available) {
  if (!active_) return;
  const auto now = time_provider_();

  if (!interactive_session_available) {
    pauseCounting("lock");
    return;
  }

  try {
    auto open = storage_.getOpenFocusSegment(active_->id);
    if (!open) {
      // Was paused (lock/sleep) — resume now that session is interactive.
      storage_.startFocusSegment(active_->id, now);
      return;
    }

    // Sleep/suspend: process froze; last_seen is pre-sleep. Split the segment.
    if (open->last_seen_at) {
      const EpochSeconds gap = now - *open->last_seen_at;
      if (gap >= kSleepGapSeconds) {
        storage_.endOpenFocusSegments(active_->id, *open->last_seen_at, "sleep_gap");
        storage_.startFocusSegment(active_->id, now);
        return;
      }
    }

    (void)storage_.touchFocusSegment(active_->id, now);
  } catch (const StorageError&) {
  }
}

void FocusSessionManager::reconcileOnLaunch(bool interactive_session_available) {
  // Never attribute process-dead time: close open segments at last heartbeat.
  try {
    storage_.closeOrphanFocusSegments("orphan_launch");
  } catch (const StorageError&) {
  }

  auto open = storage_.getActiveSession();
  if (!open) {
    active_.reset();
    return;
  }

  if (settings_.resume_focus_on_launch) {
    active_ = open;
    // Start a fresh counting segment only if the machine is unlocked now.
    if (interactive_session_available) {
      try {
        storage_.startFocusSegment(open->id, time_provider_());
      } catch (const StorageError&) {
      }
    }
    return;
  }

  // Do not silently resume: close orphan session. Segments already closed at last_seen.
  storage_.endSession(open->id, time_provider_(), "orphan_launch");
  active_.reset();
}

bool FocusSessionManager::turnOn() {
  if (active_) {
    return false;
  }
  // Close any stray open session first (defensive).
  if (auto stray = storage_.getActiveSession()) {
    storage_.endSession(stray->id, time_provider_(), "stray_close");
  }
  // createSession also opens a counting segment.
  active_ = storage_.createSession(time_provider_(), true);
  return true;
}

bool FocusSessionManager::turnOff() {
  // Always force Focus OFF in both memory and DB. Previously we returned false when
  // `active_` was empty even after closing a DB session, which left monitors thinking
  // focus was still on (they also check getActiveSession()).
  bool was_on = active_.has_value();
  active_.reset();

  // End every open session (should be at most one; loop is defensive).
  for (;;) {
    auto open = storage_.getActiveSession();
    if (!open) break;
    was_on = true;
    storage_.endSession(open->id, time_provider_(), "focus_off");
  }
  return was_on;
}

bool FocusSessionManager::toggle() {
  if (isFocusOn()) {
    turnOff();
  } else {
    turnOn();
  }
  return isFocusOn();
}

void FocusSessionManager::ensureConsistentWithStorage() {
  // Re-adopt or clear so tray/UI never disagree with the DB after crashes or dual clients.
  if (auto open = storage_.getActiveSession()) {
    if (!active_ || active_->id != open->id) {
      active_ = open;
    }
  } else if (active_) {
    active_.reset();
  }
}

} // namespace focusgaze
