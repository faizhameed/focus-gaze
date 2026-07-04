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

void FocusSessionManager::reconcileOnLaunch() {
  auto open = storage_.getActiveSession();
  if (!open) {
    active_.reset();
    return;
  }
  if (settings_.resume_focus_on_launch) {
    active_ = open;
    return;
  }
  // Do not silently resume: close orphan session.
  storage_.endSession(open->id, time_provider_());
  active_.reset();
}

bool FocusSessionManager::turnOn() {
  if (active_) {
    return false;
  }
  // Close any stray open session first (defensive).
  if (auto stray = storage_.getActiveSession()) {
    storage_.endSession(stray->id, time_provider_());
  }
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
    storage_.endSession(open->id, time_provider_());
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
