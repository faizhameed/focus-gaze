#include "core/Storage.hpp"

#include "core/FileSystem.hpp"

#include <sqlite3.h>

#include <mutex>
#include <utility>

namespace focusgaze {
namespace {

SessionRecord rowToSession(sqlite3_stmt* stmt) {
  SessionRecord rec;
  rec.id = sqlite3_column_int64(stmt, 0);
  rec.started_at = sqlite3_column_int64(stmt, 1);
  if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
    rec.ended_at = sqlite3_column_int64(stmt, 2);
  }
  rec.focus_enabled = sqlite3_column_int(stmt, 3) != 0;
  return rec;
}

FocusSegmentRecord rowToFocusSegment(sqlite3_stmt* stmt) {
  FocusSegmentRecord rec;
  rec.id = sqlite3_column_int64(stmt, 0);
  rec.session_id = sqlite3_column_int64(stmt, 1);
  rec.started_at = sqlite3_column_int64(stmt, 2);
  if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
    rec.ended_at = sqlite3_column_int64(stmt, 3);
  }
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
    rec.last_seen_at = sqlite3_column_int64(stmt, 4);
  }
  if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
    rec.end_reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
  }
  return rec;
}

UrlEventRecord rowToUrlEvent(sqlite3_stmt* stmt) {
  UrlEventRecord rec;
  rec.id = sqlite3_column_int64(stmt, 0);
  if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
    rec.session_id = sqlite3_column_int64(stmt, 1);
  }
  rec.ts = sqlite3_column_int64(stmt, 2);
  rec.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
    rec.domain = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
  }
  if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
    rec.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
  }
  if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
    rec.tab_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
  }
  if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
    rec.browser = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
  }
  const char* event = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
  if (event != nullptr) {
    if (auto t = urlEventTypeFromString(event)) {
      rec.event = *t;
    }
  }
  if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
    rec.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
  }
  return rec;
}

} // namespace

Storage::Storage(std::filesystem::path db_path) : db_path_(std::move(db_path)) {}

Storage::~Storage() { close(); }

Storage::Storage(Storage&& other) noexcept
    : db_path_(std::move(other.db_path_)), db_(other.db_) {
  other.db_ = nullptr;
}

Storage& Storage::operator=(Storage&& other) noexcept {
  if (this != &other) {
    close();
    db_path_ = std::move(other.db_path_);
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

bool Storage::isOpen() const {
  std::lock_guard lock(mu_);
  return db_ != nullptr;
}

void Storage::close() {
  std::lock_guard lock(mu_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void Storage::execOrThrow(const char* sql) const {
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : "sqlite3_exec failed";
    sqlite3_free(err);
    throw StorageError(msg);
  }
}

void Storage::migrate() {
  execOrThrow("PRAGMA foreign_keys = ON;");
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS sessions (
      id            INTEGER PRIMARY KEY,
      started_at    INTEGER NOT NULL,
      ended_at      INTEGER,
      focus_enabled INTEGER NOT NULL DEFAULT 1
    );
  )SQL");
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS url_events (
      id            INTEGER PRIMARY KEY,
      session_id    INTEGER REFERENCES sessions(id),
      ts            INTEGER NOT NULL,
      url           TEXT NOT NULL,
      domain        TEXT,
      title         TEXT,
      tab_id        TEXT,
      browser       TEXT,
      event         TEXT NOT NULL,
      category      TEXT
    );
  )SQL");
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS phone_events (
      id            INTEGER PRIMARY KEY,
      session_id    INTEGER REFERENCES sessions(id),
      started_at    INTEGER NOT NULL,
      ended_at      INTEGER,
      confidence    REAL
    );
  )SQL");
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS alarms (
      id            INTEGER PRIMARY KEY,
      session_id    INTEGER REFERENCES sessions(id),
      reason        TEXT NOT NULL,
      raised_at     INTEGER NOT NULL,
      cleared_at    INTEGER,
      meta_json     TEXT
    );
  )SQL");
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS daily_stats (
      day                   TEXT PRIMARY KEY,
      focus_seconds         INTEGER,
      productive_seconds    INTEGER,
      social_seconds        INTEGER,
      phone_seconds         INTEGER,
      alarm_count           INTEGER
    );
  )SQL");
  // Active focus-time spans (excludes lock / sleep / process-dead gaps).
  execOrThrow(R"SQL(
    CREATE TABLE IF NOT EXISTS focus_segments (
      id            INTEGER PRIMARY KEY,
      session_id    INTEGER NOT NULL REFERENCES sessions(id),
      started_at    INTEGER NOT NULL,
      ended_at      INTEGER,
      last_seen_at  INTEGER,
      end_reason    TEXT
    );
  )SQL");
  execOrThrow(
      "CREATE INDEX IF NOT EXISTS idx_url_events_session ON url_events(session_id);");
  execOrThrow(
      "CREATE INDEX IF NOT EXISTS idx_sessions_active ON sessions(ended_at);");
  execOrThrow(
      "CREATE INDEX IF NOT EXISTS idx_focus_segments_session ON focus_segments(session_id);");
  execOrThrow(
      "CREATE INDEX IF NOT EXISTS idx_focus_segments_open ON focus_segments(ended_at);");
  backfillFocusSegmentsUnlocked();
}

void Storage::open() {
  std::lock_guard lock(mu_);
  if (db_ != nullptr) {
    return;
  }
  if (!db_path_.parent_path().empty()) {
    if (!fsutil::ensureDirectory(db_path_.parent_path())) {
      throw StorageError("failed to create database directory: " +
                         db_path_.parent_path().string());
    }
  }
  // FULLMUTEX: serialize SQLite internal use; we also lock all public APIs.
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(db_path_.string().c_str(), &db_, flags, nullptr);
  if (rc != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    throw StorageError(msg);
  }
  migrate();
}

void Storage::backfillFocusSegmentsUnlocked() {
  // Legacy DBs: closed sessions without segments get one wall-clock segment (old behavior).
  // Open sessions without segments get no segment and are left for reconcileOnLaunch /
  // FocusSessionManager so multi-day orphans do not become huge focus times.
  const char* sql = R"SQL(
    INSERT INTO focus_segments(session_id, started_at, ended_at, last_seen_at, end_reason)
    SELECT s.id, s.started_at, s.ended_at, s.ended_at, 'legacy_backfill'
    FROM sessions s
    WHERE s.ended_at IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM focus_segments f WHERE f.session_id = s.id);
  )SQL";
  execOrThrow(sql);
}

FocusSegmentRecord Storage::startFocusSegmentUnlocked(std::int64_t session_id,
                                                      EpochSeconds started_at) {
  // If already counting, return the open segment (idempotent).
  {
    sqlite3_stmt* find = nullptr;
    const char* find_sql =
        "SELECT id, session_id, started_at, ended_at, last_seen_at, end_reason "
        "FROM focus_segments WHERE session_id = ? AND ended_at IS NULL "
        "ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(db_, find_sql, -1, &find, nullptr) != SQLITE_OK) {
      throw StorageError(sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(find, 1, session_id);
    if (sqlite3_step(find) == SQLITE_ROW) {
      auto existing = rowToFocusSegment(find);
      sqlite3_finalize(find);
      return existing;
    }
    sqlite3_finalize(find);
  }

  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO focus_segments(session_id, started_at, ended_at, last_seen_at, end_reason) "
      "VALUES(?, ?, NULL, ?, NULL);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  sqlite3_bind_int64(stmt, 2, started_at);
  sqlite3_bind_int64(stmt, 3, started_at);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string msg = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw StorageError(msg);
  }
  sqlite3_finalize(stmt);

  FocusSegmentRecord rec;
  rec.id = sqlite3_last_insert_rowid(db_);
  rec.session_id = session_id;
  rec.started_at = started_at;
  rec.last_seen_at = started_at;
  return rec;
}

int Storage::endOpenFocusSegmentsUnlocked(std::optional<std::int64_t> session_id,
                                          EpochSeconds ended_at, const std::string& reason) {
  sqlite3_stmt* stmt = nullptr;
  const char* sql = session_id
                        ? "UPDATE focus_segments SET ended_at = ?, end_reason = ? "
                          "WHERE session_id = ? AND ended_at IS NULL;"
                        : "UPDATE focus_segments SET ended_at = ?, end_reason = ? "
                          "WHERE ended_at IS NULL;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, ended_at);
  sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_TRANSIENT);
  if (session_id) {
    sqlite3_bind_int64(stmt, 3, *session_id);
  }
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  return sqlite3_changes(db_);
}

SessionRecord Storage::createSession(EpochSeconds started_at, bool focus_enabled) {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO sessions(started_at, ended_at, focus_enabled) VALUES(?, NULL, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, started_at);
  sqlite3_bind_int(stmt, 2, focus_enabled ? 1 : 0);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string msg = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw StorageError(msg);
  }
  sqlite3_finalize(stmt);

  SessionRecord rec;
  rec.id = sqlite3_last_insert_rowid(db_);
  rec.started_at = started_at;
  rec.ended_at = std::nullopt;
  rec.focus_enabled = focus_enabled;
  // Begin counting immediately (unlocked path); pause APIs may end this segment later.
  (void)startFocusSegmentUnlocked(rec.id, started_at);
  return rec;
}

bool Storage::endSession(std::int64_t session_id, EpochSeconds ended_at,
                         const std::string& segment_end_reason) {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  (void)endOpenFocusSegmentsUnlocked(session_id, ended_at, segment_end_reason);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE sessions SET ended_at = ? WHERE id = ? AND ended_at IS NULL;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, ended_at);
  sqlite3_bind_int64(stmt, 2, session_id);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  return sqlite3_changes(db_) > 0;
}

FocusSegmentRecord Storage::startFocusSegment(std::int64_t session_id, EpochSeconds started_at) {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  return startFocusSegmentUnlocked(session_id, started_at);
}

int Storage::endOpenFocusSegments(std::optional<std::int64_t> session_id, EpochSeconds ended_at,
                                  const std::string& reason) {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  return endOpenFocusSegmentsUnlocked(session_id, ended_at, reason);
}

int Storage::closeOrphanFocusSegments(const std::string& reason) {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  // Cap end at last heartbeat so multi-day dead time is excluded.
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE focus_segments SET "
      "ended_at = CASE "
      "  WHEN last_seen_at IS NOT NULL AND last_seen_at >= started_at THEN last_seen_at "
      "  ELSE started_at END, "
      "end_reason = ? "
      "WHERE ended_at IS NULL;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, reason.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  return sqlite3_changes(db_);
}

bool Storage::touchFocusSegment(std::int64_t session_id, EpochSeconds seen_at) {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE focus_segments SET last_seen_at = ? "
      "WHERE session_id = ? AND ended_at IS NULL;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, seen_at);
  sqlite3_bind_int64(stmt, 2, session_id);
  const int step = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (step != SQLITE_DONE) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  return sqlite3_changes(db_) > 0;
}

std::optional<FocusSegmentRecord> Storage::getOpenFocusSegment(std::int64_t session_id) const {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, session_id, started_at, ended_at, last_seen_at, end_reason "
      "FROM focus_segments WHERE session_id = ? AND ended_at IS NULL "
      "ORDER BY id DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  std::optional<FocusSegmentRecord> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = rowToFocusSegment(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<FocusSegmentRecord> Storage::listFocusSegmentsForSession(
    std::int64_t session_id) const {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, session_id, started_at, ended_at, last_seen_at, end_reason "
      "FROM focus_segments WHERE session_id = ? ORDER BY id ASC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  std::vector<FocusSegmentRecord> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back(rowToFocusSegment(stmt));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::int64_t Storage::sumFocusSecondsForSession(std::int64_t session_id,
                                                EpochSeconds now_for_open) const {
  std::lock_guard lock(mu_);
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  // Closed: ended_at - started_at. Open: now_for_open - started_at (live UI while counting).
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT COALESCE(SUM(CASE "
      "  WHEN ended_at IS NOT NULL AND ended_at > started_at THEN ended_at - started_at "
      "  WHEN ended_at IS NULL AND ? > started_at THEN ? - started_at "
      "  ELSE 0 END), 0) "
      "FROM focus_segments WHERE session_id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, now_for_open);
  sqlite3_bind_int64(stmt, 2, now_for_open);
  sqlite3_bind_int64(stmt, 3, session_id);
  std::int64_t total = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return total;
}

std::optional<SessionRecord> Storage::getSession(std::int64_t session_id) const {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, started_at, ended_at, focus_enabled FROM sessions WHERE id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  std::optional<SessionRecord> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = rowToSession(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::optional<SessionRecord> Storage::getActiveSession() const {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, started_at, ended_at, focus_enabled FROM sessions "
      "WHERE ended_at IS NULL ORDER BY id DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  std::optional<SessionRecord> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = rowToSession(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<SessionRecord> Storage::listSessions(std::size_t limit) const {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, started_at, ended_at, focus_enabled FROM sessions "
      "ORDER BY id DESC LIMIT ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
  std::vector<SessionRecord> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back(rowToSession(stmt));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::int64_t Storage::insertUrlEvent(const UrlEventRecord& event) {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO url_events(session_id, ts, url, domain, title, tab_id, browser, event, "
      "category) VALUES(?,?,?,?,?,?,?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  if (event.session_id) {
    sqlite3_bind_int64(stmt, 1, *event.session_id);
  } else {
    sqlite3_bind_null(stmt, 1);
  }
  sqlite3_bind_int64(stmt, 2, event.ts);
  sqlite3_bind_text(stmt, 3, event.url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, event.domain.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, event.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, event.tab_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, event.browser.c_str(), -1, SQLITE_TRANSIENT);
  const char* ev = toString(event.event);
  sqlite3_bind_text(stmt, 8, ev, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 9, event.category.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string msg = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw StorageError(msg);
  }
  sqlite3_finalize(stmt);
  return sqlite3_last_insert_rowid(db_);
}

std::vector<UrlEventRecord> Storage::listUrlEventsForSession(std::int64_t session_id,
                                                            std::size_t limit) const {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT id, session_id, ts, url, domain, title, tab_id, browser, event, category "
      "FROM url_events WHERE session_id = ? ORDER BY id ASC LIMIT ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit));
  std::vector<UrlEventRecord> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back(rowToUrlEvent(stmt));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::int64_t Storage::insertPhoneEvent(std::optional<std::int64_t> session_id,
                                       EpochSeconds started_at,
                                       std::optional<EpochSeconds> ended_at, double confidence) {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO phone_events(session_id, started_at, ended_at, confidence) VALUES(?,?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  if (session_id) {
    sqlite3_bind_int64(stmt, 1, *session_id);
  } else {
    sqlite3_bind_null(stmt, 1);
  }
  sqlite3_bind_int64(stmt, 2, started_at);
  if (ended_at) {
    sqlite3_bind_int64(stmt, 3, *ended_at);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  sqlite3_bind_double(stmt, 4, confidence);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    const std::string msg = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    throw StorageError(msg);
  }
  sqlite3_finalize(stmt);
  return sqlite3_last_insert_rowid(db_);
}

std::int64_t Storage::sumPhoneSecondsForSession(std::int64_t session_id) const {
  std::lock_guard lock(mu_);
  // Do not call isOpen() here — it also locks mu_ (non-recursive) and deadlocks.
  if (db_ == nullptr) {
    throw StorageError("database is not open");
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT COALESCE(SUM(CASE WHEN ended_at IS NOT NULL AND ended_at > started_at "
      "THEN ended_at - started_at ELSE 0 END), 0) FROM phone_events WHERE session_id = ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw StorageError(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int64(stmt, 1, session_id);
  std::int64_t total = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return total;
}

} // namespace focusgaze
