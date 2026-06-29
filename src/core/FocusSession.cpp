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
  if (!active_) {
    // Ensure DB has no open session.
    if (auto open = storage_.getActiveSession()) {
      storage_.endSession(open->id, time_provider_());
    }
    return false;
  }
  const auto id = active_->id;
  storage_.endSession(id, time_provider_());
  active_ = storage_.getSession(id);
  // After end, treat as off.
  active_.reset();
  return true;
}

bool FocusSessionManager::toggle() {
  if (isFocusOn()) {
    turnOff();
  } else {
    turnOn();
  }
  return isFocusOn();
}

} // namespace focusgaze
