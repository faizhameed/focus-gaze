#include "test_helpers.hpp"

#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "core/Types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using focusgaze::ProductivityStats;
using focusgaze::Storage;
using focusgaze::UrlEventRecord;
using focusgaze::UrlEventType;
using focusgaze::test::ScopedDataRoot;

TEST_CASE("ProductivityStats scores productive vs social time", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto session = db.createSession(1000, true);
  db.endSession(session.id, 1100); // 100s focus

  UrlEventRecord work;
  work.session_id = session.id;
  work.ts = 1000;
  work.url = "https://github.com";
  work.domain = "github.com";
  work.event = UrlEventType::Activated;
  work.category = "allow";
  db.insertUrlEvent(work);

  UrlEventRecord social;
  social.session_id = session.id;
  social.ts = 1050;
  social.url = "https://instagram.com";
  social.domain = "instagram.com";
  social.event = UrlEventType::Activated;
  social.category = "blocked";
  db.insertUrlEvent(social);

  ProductivityStats stats(db);
  const auto s = stats.computeSession(session.id);
  REQUIRE(s.focus_seconds == 100);
  REQUIRE(s.productive_seconds == 50);
  REQUIRE(s.social_seconds == 50);
  REQUIRE(s.score > 0);
  REQUIRE(s.score < 100);

  const auto csv = ProductivityStats::toCsv(s);
  REQUIRE(csv.find("session_id") != std::string::npos);
  REQUIRE(csv.find(std::to_string(session.id)) != std::string::npos);
}

TEST_CASE("ProductivityStats includes phone seconds", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto session = db.createSession(0, true);
  db.endSession(session.id, 100);
  db.insertPhoneEvent(session.id, 10, 40, 1.0);
  ProductivityStats stats(db);
  const auto s = stats.computeSession(session.id);
  REQUIRE(s.phone_seconds == 30);
}

TEST_CASE("lastSessionSummary prefers most recently ended session", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();

  const auto older = db.createSession(100, true);
  db.endSession(older.id, 200);
  const auto newer = db.createSession(300, true);
  db.endSession(newer.id, 400);

  ProductivityStats stats(db);
  auto s = stats.lastSessionSummary();
  REQUIRE(s.has_value());
  REQUIRE(s->session_id == newer.id);
  REQUIRE(s->focus_seconds == 100);

  const auto report = ProductivityStats::formatReport(*s);
  REQUIRE(report.find("score=") != std::string::npos);
  REQUIRE(report.find(std::to_string(newer.id)) != std::string::npos);
}

TEST_CASE("lastSessionSummary prefers open session over older ended ones", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto ended = db.createSession(100, true);
  db.endSession(ended.id, 200);
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto active = db.createSession(static_cast<focusgaze::EpochSeconds>(now - 90), true);

  ProductivityStats stats(db);
  auto s = stats.lastSessionSummary();
  REQUIRE(s.has_value());
  REQUIRE(s->session_id == active.id);
  REQUIRE(s->focus_seconds >= 60);
}

TEST_CASE("lastSessionSummary falls back to active session when none ended", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  // started far enough in the past that "now" yields positive focus_seconds
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto active = db.createSession(static_cast<focusgaze::EpochSeconds>(now - 120), true);

  ProductivityStats stats(db);
  auto s = stats.lastSessionSummary();
  REQUIRE(s.has_value());
  REQUIRE(s->session_id == active.id);
  // Open session must use wall-clock "now" as end so duration grows in the background.
  REQUIRE(s->focus_seconds >= 100);
}

TEST_CASE("lastSessionSummary empty database returns nullopt", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  ProductivityStats stats(db);
  REQUIRE_FALSE(stats.lastSessionSummary().has_value());
}

TEST_CASE("recentSessions and lastNDays and export helpers", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto a = db.createSession(1000, true);
  db.endSession(a.id, 1100);
  const auto b = db.createSession(2000, true);
  db.endSession(b.id, 2300);

  ProductivityStats stats(db);
  const auto recent = stats.recentSessions(10);
  REQUIRE(recent.size() >= 2);
  REQUIRE(recent.front().session_id == b.id);

  const auto week = stats.lastNDays(7);
  REQUIRE(week.size() == 7);
  for (const auto& d : week) {
    REQUIRE(d.day.size() == 10);
  }

  const auto csv = ProductivityStats::toCsvMany(recent);
  REQUIRE(csv.find("session_id") != std::string::npos);
  REQUIRE(csv.find(std::to_string(b.id)) != std::string::npos);

  const auto json = ProductivityStats::toJsonMany(recent);
  REQUIRE(json.find("session_id") != std::string::npos);
  REQUIRE(json.find(std::to_string(a.id)) != std::string::npos);

  REQUIRE_FALSE(ProductivityStats::todayLocalYmd().empty());
}

TEST_CASE("computeWindow Today and Last7Days aggregate real sessions", "[stats]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto started = static_cast<focusgaze::EpochSeconds>(now - 300);
  const auto session = db.createSession(started, true);
  db.endSession(session.id, static_cast<focusgaze::EpochSeconds>(now - 100));

  UrlEventRecord work;
  work.session_id = session.id;
  work.ts = started;
  work.url = "https://github.com";
  work.domain = "github.com";
  work.event = UrlEventType::Activated;
  work.category = "allow";
  db.insertUrlEvent(work);

  UrlEventRecord blocked;
  blocked.session_id = session.id;
  blocked.ts = started + 100;
  blocked.url = "https://instagram.com";
  blocked.domain = "instagram.com";
  blocked.event = UrlEventType::Activated;
  blocked.category = "blocked";
  db.insertUrlEvent(blocked);

  db.insertPhoneEvent(session.id, started + 150, started + 180, 1.0);

  ProductivityStats stats(db);

  const auto today = stats.computeWindow(focusgaze::StatsWindow::Today);
  REQUIRE(today.session_count >= 1);
  REQUIRE(today.focus_seconds >= 100);
  REQUIRE(today.unproductive_seconds > 0);
  REQUIRE(today.productive_seconds > 0);
  REQUIRE(today.phone_seconds == 30);
  REQUIRE(today.score >= 0.0);
  REQUIRE(today.score <= 100.0);

  const auto week = stats.computeWindow(focusgaze::StatsWindow::Last7Days);
  REQUIRE(week.session_count >= 1);
  REQUIRE(week.focus_seconds >= today.focus_seconds - 1); // same session in both windows

  const auto days = stats.daysInRange(week.range_start, week.range_end);
  REQUIRE(days.size() == 7);
  int days_with_focus = 0;
  for (const auto& d : days) {
    if (d.focus_seconds > 0) ++days_with_focus;
  }
  REQUIRE(days_with_focus >= 1);

  const auto last = stats.computeWindow(focusgaze::StatsWindow::LastSession);
  REQUIRE(last.session_count == 1);
  REQUIRE(last.phone_seconds == 30);

  // Score formula: 100*prod/f - 40*unprod/f - 30*phone/f
  const double expected = ProductivityStats::computeScoreParts(
      100, 60, 30, 10);
  REQUIRE(expected > 0.0);
  REQUIRE(expected < 100.0);
}
