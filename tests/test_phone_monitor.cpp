#include "test_helpers.hpp"

#include "core/AlarmController.hpp"
#include "core/FocusSession.hpp"
#include "core/PhoneMonitor.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::AlarmController;
using focusgaze::AlarmReason;
using focusgaze::FocusSessionManager;
using focusgaze::PhoneMonitor;
using focusgaze::Settings;
using focusgaze::Storage;
using focusgaze::test::ScopedDataRoot;

TEST_CASE("PhoneMonitor raises phone alarm when focus on and over threshold", "[phone]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  Settings s = Settings::defaults();
  s.phone_threshold_seconds = 5;
  s.phone_window_seconds = 100;
  FocusSessionManager focus(db, s, [] { return focusgaze::EpochSeconds{0}; });
  AlarmController alarms;
  PhoneMonitor phone(db, focus, alarms, s);
  REQUIRE(focus.turnOn());

  for (int i = 0; i <= 6; ++i) {
    phone.sample(i, true);
  }
  REQUIRE(alarms.isReasonActive(AlarmReason::PhoneWindow));
  auto st = phone.status(6);
  REQUIRE(st.phone_alarm);
  REQUIRE(st.cumulative_visible_seconds > 5);
}

TEST_CASE("PhoneMonitor ignores phone when focus off", "[phone]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  Settings s = Settings::defaults();
  s.phone_threshold_seconds = 5;
  FocusSessionManager focus(db, s, [] { return focusgaze::EpochSeconds{0}; });
  AlarmController alarms;
  PhoneMonitor phone(db, focus, alarms, s);

  for (int i = 0; i <= 20; ++i) {
    phone.sample(i, true);
  }
  REQUIRE_FALSE(alarms.isReasonActive(AlarmReason::PhoneWindow));
}
