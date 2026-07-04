#include "test_helpers.hpp"

#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::BrowserMonitor;
using focusgaze::BrowserUrlEvent;
using focusgaze::FocusSessionManager;
using focusgaze::Settings;
using focusgaze::Storage;
using focusgaze::UrlEventType;
using focusgaze::test::ScopedDataRoot;

namespace {
struct FakeClock {
  focusgaze::EpochSeconds now{1000};
  focusgaze::EpochSeconds operator()() const { return now; }
};
} // namespace

TEST_CASE("BrowserMonitor raises sticky alarm on blocked site when focus on", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);

  REQUIRE(focus.turnOn());

  BrowserUrlEvent ev;
  ev.url = "https://www.instagram.com/";
  ev.tab_id = "t1";
  ev.event = UrlEventType::Activated;
  ev.ts = 1001;
  REQUIRE(mon.handleEvent(ev));

  auto st = mon.status();
  REQUIRE(st.focus_on);
  REQUIRE(st.alarm_active);
  REQUIRE(st.blocked_tab_count == 1);
  REQUIRE(st.last_category == "blocked");

  const auto events = db.listUrlEventsForSession(*st.session_id);
  REQUIRE_FALSE(events.empty());
  REQUIRE(events.back().category == "blocked");
}

TEST_CASE("BrowserMonitor clears alarm when blocked tab closed", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  focus.turnOn();

  BrowserUrlEvent open;
  open.url = "https://instagram.com/";
  open.tab_id = "t1";
  open.event = UrlEventType::Activated;
  mon.handleEvent(open);
  REQUIRE(mon.status().alarm_active);

  BrowserUrlEvent close;
  close.tab_id = "t1";
  close.event = UrlEventType::Closed;
  mon.handleEvent(close);
  REQUIRE_FALSE(mon.status().alarm_active);
}

TEST_CASE("BrowserMonitor multi-tab sticky until all cleared", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager focus(db, Settings::defaults(), [&] { return clock(); });
  BrowserMonitor mon(db, focus, Settings::defaults());
  focus.turnOn();

  BrowserUrlEvent a{.url = "https://instagram.com/", .tab_id = "1", .event = UrlEventType::Activated};
  BrowserUrlEvent b{.url = "https://x.com/", .tab_id = "2", .event = UrlEventType::Activated};
  mon.handleEvent(a);
  mon.handleEvent(b);
  REQUIRE(mon.status().blocked_tab_count == 2);

  BrowserUrlEvent close1{.tab_id = "1", .event = UrlEventType::Closed};
  mon.handleEvent(close1);
  REQUIRE(mon.status().alarm_active);

  BrowserUrlEvent nav{.url = "https://example.com/", .tab_id = "2", .event = UrlEventType::Updated};
  mon.handleEvent(nav);
  REQUIRE_FALSE(mon.status().alarm_active);
}

TEST_CASE("BrowserMonitor does not alarm when focus off", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager focus(db, Settings::defaults(), [&] { return clock(); });
  BrowserMonitor mon(db, focus, Settings::defaults());

  BrowserUrlEvent ev{.url = "https://instagram.com/", .tab_id = "t1", .event = UrlEventType::Activated};
  mon.handleEvent(ev);
  REQUIRE_FALSE(mon.status().alarm_active);
  REQUIRE(db.listUrlEventsForSession(1).empty());
}

TEST_CASE("BrowserMonitor allowlist wins over blocklist", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.allowlist = {"reddit.com"};
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  focus.turnOn();

  BrowserUrlEvent ev{.url = "https://www.reddit.com/", .tab_id = "t1", .event = UrlEventType::Activated};
  mon.handleEvent(ev);
  REQUIRE_FALSE(mon.status().alarm_active);
  REQUIRE(mon.status().last_category == "allow");
}

TEST_CASE("onFocusTurnedOff clears social alarm", "[monitor]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  FocusSessionManager focus(db, Settings::defaults(), [&] { return clock(); });
  BrowserMonitor mon(db, focus, Settings::defaults());
  focus.turnOn();
  BrowserUrlEvent ev{.url = "https://instagram.com/", .tab_id = "t1", .event = UrlEventType::Activated};
  mon.handleEvent(ev);
  REQUIRE(mon.status().alarm_active);
  focus.turnOff();
  mon.onFocusTurnedOff();
  REQUIRE_FALSE(mon.status().alarm_active);
}
