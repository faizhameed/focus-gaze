/// @file test_focus_segments.cpp
/// Focus time must exclude lock / sleep / process-dead gaps (not keyboard idle).
///
/// Presence ticks must be less than FocusSessionManager::kSleepGapSeconds apart while
/// interactive, matching the real ~1 Hz UI/serve heartbeat.

#include "test_helpers.hpp"

#include "core/FocusSession.hpp"
#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::FocusSessionManager;
using focusgaze::ProductivityStats;
using focusgaze::Settings;
using focusgaze::Storage;
using focusgaze::test::ScopedDataRoot;

namespace {

struct FakeClock {
  focusgaze::EpochSeconds now{1'000};
  focusgaze::EpochSeconds operator()() const { return now; }
};

/// Advance wall time in steps under the sleep-gap threshold and tick presence.
void advanceInteractive(FocusSessionManager& mgr, FakeClock& clock, focusgaze::EpochSeconds target) {
  constexpr focusgaze::EpochSeconds kStep = 30; // < kSleepGapSeconds (90)
  while (clock.now + kStep < target) {
    clock.now += kStep;
    mgr.onPresenceTick(true);
  }
  if (clock.now < target) {
    clock.now = target;
    mgr.onPresenceTick(true);
  }
}

} // namespace

TEST_CASE("createSession opens a counting segment", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto s = db.createSession(100, true);
  REQUIRE(db.getOpenFocusSegment(s.id).has_value());
  REQUIRE(db.sumFocusSecondsForSession(s.id, 160) == 60);
  db.endSession(s.id, 160);
  REQUIRE_FALSE(db.getOpenFocusSegment(s.id).has_value());
  REQUIRE(db.sumFocusSecondsForSession(s.id, 9999) == 60);
}

TEST_CASE("pause and resume exclude locked gap from focus seconds", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE(mgr.turnOn()); // t=1000
  advanceInteractive(mgr, clock, 1100);
  REQUIRE(mgr.isCounting());

  // Lock screen: stop counting at observation time (1100).
  mgr.onPresenceTick(/*interactive=*/false);
  REQUIRE_FALSE(mgr.isCounting());

  // Still locked for a long wall-clock span (must not accrue). No interactive ticks.
  clock.now = 5000;
  mgr.onPresenceTick(/*interactive=*/false);
  REQUIRE_FALSE(mgr.isCounting());

  // Unlock: new segment from 5000; count only unlocked time after that.
  clock.now = 5000;
  mgr.onPresenceTick(/*interactive=*/true);
  REQUIRE(mgr.isCounting());
  advanceInteractive(mgr, clock, 5100);
  REQUIRE(mgr.turnOff());

  ProductivityStats stats(db);
  const auto sid = db.listSessions(1).front().id;
  const auto summary = stats.computeSession(sid);
  // 100s before lock (1000→1100) + 100s after unlock (5000→5100) = 200. Not ~4100 wall.
  REQUIRE(summary.focus_seconds == 200);
  REQUIRE(summary.focus_seconds < 500);
}

TEST_CASE("sleep gap splits segment at last heartbeat", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE(mgr.turnOn()); // t=1000
  advanceInteractive(mgr, clock, 1050);

  // Simulate suspend: next interactive tick jumps by more than kSleepGapSeconds.
  clock.now = 1050 + FocusSessionManager::kSleepGapSeconds + 10; // 1150
  mgr.onPresenceTick(true);
  REQUIRE(mgr.isCounting());

  advanceInteractive(mgr, clock, 1200);
  REQUIRE(mgr.turnOff());

  const auto sid = db.listSessions(1).front().id;
  const auto segments = db.listFocusSegmentsForSession(sid);
  REQUIRE(segments.size() >= 2);

  ProductivityStats stats(db);
  const auto summary = stats.computeSession(sid);
  // First segment ends at last_seen 1050 → 50s; second 1150→1200 → 50s; total 100.
  REQUIRE(summary.focus_seconds == 100);
}

TEST_CASE("orphan launch does not backfill process-dead time", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  clock.now = 1000;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });
  REQUIRE(mgr.turnOn());
  advanceInteractive(mgr, clock, 1100);
  // Crash: open segment left with last_seen≈1100, no process_exit.

  Settings settings = Settings::defaults();
  settings.resume_focus_on_launch = false;
  clock.now = 1'000'000; // days later
  FocusSessionManager relaunch(db, settings, [&] { return clock(); });
  relaunch.reconcileOnLaunch(/*interactive=*/true);

  REQUIRE_FALSE(relaunch.isFocusOn());
  ProductivityStats stats(db);
  const auto sid = db.listSessions(1).front().id;
  const auto summary = stats.computeSession(sid);
  // Only up to last heartbeat (~100s), not ~1e6 seconds of dead time.
  REQUIRE(summary.focus_seconds == 100);
}

TEST_CASE("resume on launch starts fresh segment without dead gap", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  clock.now = 1000;
  {
    FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });
    REQUIRE(mgr.turnOn());
    advanceInteractive(mgr, clock, 1080);
  }

  Settings settings = Settings::defaults();
  settings.resume_focus_on_launch = true;
  clock.now = 5000;
  FocusSessionManager relaunch(db, settings, [&] { return clock(); });
  relaunch.reconcileOnLaunch(true);
  REQUIRE(relaunch.isFocusOn());
  REQUIRE(relaunch.isCounting());

  advanceInteractive(relaunch, clock, 5100);
  relaunch.turnOff();

  ProductivityStats stats(db);
  const auto summary = stats.computeSession(db.listSessions(1).front().id);
  // 80s pre-crash + 100s post-resume = 180; not 4100 wall.
  REQUIRE(summary.focus_seconds == 180);
}

TEST_CASE("unlocked idle still counts (no input required)", "[focus][segments]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });
  REQUIRE(mgr.turnOn());
  // Many presence ticks with interactive=true and no keyboard model at all.
  advanceInteractive(mgr, clock, 1600);
  REQUIRE(mgr.turnOff());
  REQUIRE(db.sumFocusSecondsForSession(db.listSessions(1).front().id, clock.now) == 600);
}
