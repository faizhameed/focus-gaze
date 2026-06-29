#include "test_helpers.hpp"

#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "core/Types.hpp"

#include <catch2/catch_test_macros.hpp>

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
