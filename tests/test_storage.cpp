#include "test_helpers.hpp"

#include "core/Storage.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::Storage;
using focusgaze::StorageError;
using focusgaze::UrlEventRecord;
using focusgaze::UrlEventType;
using focusgaze::test::ScopedDataRoot;

TEST_CASE("Storage opens and creates schema", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "focusgaze.db");
  REQUIRE_NOTHROW(db.open());
  REQUIRE(db.isOpen());
  REQUIRE(std::filesystem::is_regular_file(db.path()));
  db.close();
  REQUIRE_FALSE(db.isOpen());
}

TEST_CASE("Storage createSession and getSession", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();

  const auto session = db.createSession(1'700'000'000, true);
  REQUIRE(session.id > 0);
  REQUIRE(session.started_at == 1'700'000'000);
  REQUIRE_FALSE(session.ended_at.has_value());
  REQUIRE(session.focus_enabled);

  const auto loaded = db.getSession(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->id == session.id);
  REQUIRE(loaded->started_at == session.started_at);
  REQUIRE_FALSE(loaded->ended_at.has_value());
}

TEST_CASE("Storage endSession sets ended_at and clears active", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();

  const auto session = db.createSession(100, true);
  REQUIRE(db.getActiveSession().has_value());
  REQUIRE(db.endSession(session.id, 200));
  REQUIRE_FALSE(db.endSession(session.id, 300)); // already ended

  const auto loaded = db.getSession(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->ended_at.has_value());
  REQUIRE(*loaded->ended_at == 200);
  REQUIRE_FALSE(db.getActiveSession().has_value());
}

TEST_CASE("Storage listSessions returns newest first", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();

  const auto a = db.createSession(10, true);
  db.endSession(a.id, 11);
  const auto b = db.createSession(20, true);
  db.endSession(b.id, 21);
  const auto c = db.createSession(30, true);

  const auto list = db.listSessions(10);
  REQUIRE(list.size() == 3);
  REQUIRE(list[0].id == c.id);
  REQUIRE(list[1].id == b.id);
  REQUIRE(list[2].id == a.id);
}

TEST_CASE("Storage persists across reopen", "[storage]") {
  ScopedDataRoot scope;
  const auto path = scope.path() / "persist.db";
  std::int64_t id = 0;
  {
    Storage db(path);
    db.open();
    id = db.createSession(42, true).id;
  }
  {
    Storage db(path);
    db.open();
    const auto s = db.getSession(id);
    REQUIRE(s.has_value());
    REQUIRE(s->started_at == 42);
    REQUIRE(db.getActiveSession().has_value());
  }
}

TEST_CASE("Storage insertUrlEvent and list by session", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  const auto session = db.createSession(1, true);

  UrlEventRecord ev;
  ev.session_id = session.id;
  ev.ts = 2;
  ev.url = "https://instagram.com/";
  ev.domain = "instagram.com";
  ev.title = "Instagram";
  ev.tab_id = "t1";
  ev.browser = "chrome";
  ev.event = UrlEventType::Activated;
  ev.category = "blocked";

  const auto eid = db.insertUrlEvent(ev);
  REQUIRE(eid > 0);

  const auto events = db.listUrlEventsForSession(session.id);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].url == ev.url);
  REQUIRE(events[0].category == "blocked");
  REQUIRE(events[0].event == UrlEventType::Activated);
}

TEST_CASE("Storage throws when used before open", "[storage]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  REQUIRE_THROWS_AS(db.createSession(1, true), StorageError);
}
