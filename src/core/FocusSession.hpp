#pragma once

#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "core/Types.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace focusgaze {

/// Manages Focus Mode lifecycle and persists sessions via Storage.
///
/// Focus *time* is counted only in focus_segments while the workstation is
/// interactive (unlocked). Lock, sleep (heartbeat gap), and process death pause
/// counting; idle without input still counts when unlocked.
class FocusSessionManager {
public:
  using Clock = std::chrono::system_clock;
  using TimeProvider = std::function<EpochSeconds()>;

  /// Wall-clock gap between heartbeats treated as sleep/suspend (seconds).
  static constexpr EpochSeconds kSleepGapSeconds = 90;

  FocusSessionManager(Storage& storage, Settings settings,
                      TimeProvider time_provider = defaultTimeProvider);

  const Settings& settings() const { return settings_; }
  void setSettings(Settings settings) { settings_ = std::move(settings); }

  bool isFocusOn() const { return active_.has_value(); }
  /// True when Focus is on and a counting segment is open (metrics + enforcement).
  bool isCounting() const;
  std::optional<SessionRecord> activeSession() const { return active_; }

  /// Start Focus Mode. Creates a DB session (+ counting segment). Returns false if already on.
  bool turnOn();

  /// Stop Focus Mode and end the active DB session. Returns false if already off.
  bool turnOff();

  /// Toggle Focus Mode. Returns new isFocusOn() state.
  bool toggle();

  /**
   * Periodic tick (≈1 Hz from UI or serve loop).
   * - Pauses counting when the interactive session is unavailable (lock).
   * - Splits segments across large heartbeat gaps (sleep).
   * - Heartbeats last_seen_at while counting.
   * Does not use keyboard/mouse idle.
   */
  void onPresenceTick(bool interactive_session_available);

  /// End open counting segments without ending the Focus session (e.g. lock).
  void pauseCounting(const std::string& reason);

  /// Resume counting if Focus is on and no open segment (e.g. unlock).
  void resumeCounting();

  /// Close open segments at now (graceful process exit). Session may stay open for resume.
  void prepareForProcessExit();

  /// Re-load active session from DB (e.g. after crash / restart). Does not create one.
  void syncFromStorage();

  /// Align in-memory active session with the open DB session (or clear if none).
  void ensureConsistentWithStorage();

  /**
   * On app launch: close orphan open segments at last heartbeat (not "now"),
   * then either adopt the open session (resume_focus_on_launch) and start a new
   * segment if interactive, or end the session without backfilling dead time.
   */
  void reconcileOnLaunch(bool interactive_session_available = true);

  static EpochSeconds defaultTimeProvider();

private:
  Storage& storage_;
  Settings settings_;
  TimeProvider time_provider_;
  std::optional<SessionRecord> active_;
};

} // namespace focusgaze
