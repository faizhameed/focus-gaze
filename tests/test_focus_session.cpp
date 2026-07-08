#include "test_helpers.hpp"

#include "core/FocusSession.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::FocusSessionManager;
using focusgaze::Settings;
using focusgaze::Storage;
using focusgaze::test::ScopedDataRoot;

namespace {

struct FakeClock {
  focusgaze::EpochSeconds now{1'000};
  focusgaze::EpochSeconds operator()() const { return now; }
};

} // namespace

TEST_CASE("FocusSessionManager turnOn creates persisted session", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE_FALSE(mgr.isFocusOn());
  REQUIRE(mgr.turnOn());
  REQUIRE(mgr.isFocusOn());
  REQUIRE(mgr.activeSession().has_value());
  REQUIRE(mgr.activeSession()->started_at == 1000);
  REQUIRE(db.getActiveSession().has_value());
  REQUIRE(db.getActiveSession()->id == mgr.activeSession()->id);

  // Second turnOn is a no-op failure.
  REQUIRE_FALSE(mgr.turnOn());
}

TEST_CASE("FocusSessionManager turnOff ends session in database", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE(mgr.turnOn());
  const auto id = mgr.activeSession()->id;
  clock.now = 1500;
  REQUIRE(mgr.turnOff());
  REQUIRE_FALSE(mgr.isFocusOn());
  REQUIRE_FALSE(db.getActiveSession().has_value());

  const auto ended = db.getSession(id);
  REQUIRE(ended.has_value());
  REQUIRE(ended->ended_at.has_value());
  REQUIRE(*ended->ended_at == 1500);
  REQUIRE_FALSE(mgr.turnOff());
}

TEST_CASE("FocusSessionManager toggle flips state", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE(mgr.toggle() == true);
  REQUIRE(mgr.isFocusOn());
  clock.now = 2;
  REQUIRE(mgr.toggle() == false);
  REQUIRE_FALSE(mgr.isFocusOn());
  REQUIRE(db.listSessions(10).size() == 1);
}

TEST_CASE("FocusSessionManager syncFromStorage adopts open session", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto created = db.createSession(50, true);

  FocusSessionManager mgr(db, Settings::defaults(), [] { return focusgaze::EpochSeconds{99}; });
  REQUIRE(mgr.isFocusOn());
  REQUIRE(mgr.activeSession()->id == created.id);
}

TEST_CASE("reconcileOnLaunch ends orphan when resume disabled", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto created = db.createSession(10, true);

  Settings settings = Settings::defaults();
  settings.resume_focus_on_launch = false;
  FakeClock clock;
  clock.now = 99;
  FocusSessionManager mgr(db, settings, [&] { return clock(); });
  mgr.reconcileOnLaunch(/*interactive=*/true);

  REQUIRE_FALSE(mgr.isFocusOn());
  const auto ended = db.getSession(created.id);
  REQUIRE(ended->ended_at.has_value());
  REQUIRE(*ended->ended_at == 99);
}

TEST_CASE("reconcileOnLaunch keeps session when resume enabled", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto created = db.createSession(10, true);

  Settings settings = Settings::defaults();
  settings.resume_focus_on_launch = true;
  FocusSessionManager mgr(db, settings, [] { return focusgaze::EpochSeconds{1}; });
  mgr.reconcileOnLaunch(/*interactive=*/true);

  REQUIRE(mgr.isFocusOn());
  REQUIRE(mgr.activeSession()->id == created.id);
  REQUIRE_FALSE(db.getSession(created.id)->ended_at.has_value());
  // Orphan open segment closed at last_seen; a new counting segment starts when interactive.
  REQUIRE(mgr.isCounting());
}

TEST_CASE("multiple focus cycles create multiple session rows", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  for (int i = 0; i < 3; ++i) {
    clock.now = 100 + i * 10;
    REQUIRE(mgr.turnOn());
    clock.now = 105 + i * 10;
    REQUIRE(mgr.turnOff());
  }
  REQUIRE(db.listSessions(10).size() == 3);
}

TEST_CASE("ensureConsistentWithStorage re-adopts open DB session", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  // Empty manager, open session created outside memory.
  REQUIRE_FALSE(mgr.isFocusOn());
  const auto created = db.createSession(500, true);
  mgr.ensureConsistentWithStorage();
  REQUIRE(mgr.isFocusOn());
  REQUIRE(mgr.activeSession()->id == created.id);
}

TEST_CASE("ensureConsistentWithStorage clears memory when DB session ended", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  REQUIRE(mgr.turnOn());
  const auto id = mgr.activeSession()->id;
  // End session only in DB (simulates desync / external end).
  REQUIRE(db.endSession(id, 2000));
  REQUIRE(db.getActiveSession().has_value() == false);
  // Memory still thinks focus is on until we reconcile.
  REQUIRE(mgr.isFocusOn());
  mgr.ensureConsistentWithStorage();
  REQUIRE_FALSE(mgr.isFocusOn());
}

TEST_CASE("turnOff closes multiple open DB sessions defensively", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager mgr(db, Settings::defaults(), [&] { return clock(); });

  // Two open rows (shouldn't happen in normal use, but turnOff must clear them).
  db.createSession(10, true);
  db.createSession(20, true);
  REQUIRE(db.getActiveSession().has_value());
  REQUIRE(mgr.turnOff());
  REQUIRE_FALSE(db.getActiveSession().has_value());
  REQUIRE_FALSE(mgr.isFocusOn());
}
