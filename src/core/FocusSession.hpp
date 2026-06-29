#pragma once

#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "core/Types.hpp"

#include <chrono>
#include <functional>
#include <optional>

namespace focusgaze {

/// Manages Focus Mode lifecycle and persists sessions via Storage.
class FocusSessionManager {
public:
  using Clock = std::chrono::system_clock;
  using TimeProvider = std::function<EpochSeconds()>;

  FocusSessionManager(Storage& storage, Settings settings,
                      TimeProvider time_provider = defaultTimeProvider);

  const Settings& settings() const { return settings_; }
  void setSettings(Settings settings) { settings_ = std::move(settings); }

  bool isFocusOn() const { return active_.has_value(); }
  std::optional<SessionRecord> activeSession() const { return active_; }

  /// Start Focus Mode. Creates a DB session. Returns false if already on.
  bool turnOn();

  /// Stop Focus Mode and end the active DB session. Returns false if already off.
  bool turnOff();

  /// Toggle Focus Mode. Returns new isFocusOn() state.
  bool toggle();

  /// Re-load active session from DB (e.g. after crash / restart). Does not create one.
  void syncFromStorage();

  /// If settings.resume_focus_on_launch and an open session exists, adopt it; else end orphans.
  /// When resume is false, any open session is ended with "now".
  void reconcileOnLaunch();

  static EpochSeconds defaultTimeProvider();

private:
  Storage& storage_;
  Settings settings_;
  TimeProvider time_provider_;
  std::optional<SessionRecord> active_;
};

} // namespace focusgaze
