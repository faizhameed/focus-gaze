#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;

namespace focusgaze {

class StorageError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// SQLite-backed persistence. All public methods are thread-safe (mutex).
/// Required because the HTTP bridge thread and Qt main thread share one Storage.
class Storage {
public:
  explicit Storage(std::filesystem::path db_path);
  ~Storage();

  Storage(const Storage&) = delete;
  Storage& operator=(const Storage&) = delete;
  Storage(Storage&&) noexcept;
  Storage& operator=(Storage&&) noexcept;

  const std::filesystem::path& path() const { return db_path_; }

  /// Open DB (create parent dirs), apply schema migrations. Throws StorageError on failure.
  void open();
  void close();
  bool isOpen() const;

  // --- Sessions ---
  /// Creates a session and starts an open focus-counting segment at @p started_at.
  SessionRecord createSession(EpochSeconds started_at, bool focus_enabled = true);
  /// Ends open focus segments for the session, then marks the session ended.
  bool endSession(std::int64_t session_id, EpochSeconds ended_at,
                  const std::string& segment_end_reason = "focus_off");
  std::optional<SessionRecord> getSession(std::int64_t session_id) const;
  std::optional<SessionRecord> getActiveSession() const;
  std::vector<SessionRecord> listSessions(std::size_t limit = 100) const;

  // --- Focus counting segments (pause for lock / sleep / process death) ---
  /// Start counting focus time for an open session. No-op if a segment is already open.
  FocusSegmentRecord startFocusSegment(std::int64_t session_id, EpochSeconds started_at);
  /// Close all open segments for one session (or every session if session_id is nullopt).
  int endOpenFocusSegments(std::optional<std::int64_t> session_id, EpochSeconds ended_at,
                           const std::string& reason);
  /**
   * Close every open segment at COALESCE(last_seen_at, started_at) — never "now".
   * Use on process launch so time while the app was dead is not counted.
   */
  int closeOrphanFocusSegments(const std::string& reason = "orphan");
  /// Update last_seen_at on the open segment for @p session_id (heartbeat).
  bool touchFocusSegment(std::int64_t session_id, EpochSeconds seen_at);
  std::optional<FocusSegmentRecord> getOpenFocusSegment(std::int64_t session_id) const;
  std::vector<FocusSegmentRecord> listFocusSegmentsForSession(std::int64_t session_id) const;
  /**
   * Sum counted focus seconds for a session.
   * Open segments use min(now_for_open, last_seen+grace) policy via @p now_for_open as the open end.
   */
  std::int64_t sumFocusSecondsForSession(std::int64_t session_id,
                                         EpochSeconds now_for_open) const;

  // --- URL events (schema ready; used heavily in Phase 2) ---
  std::int64_t insertUrlEvent(const UrlEventRecord& event);
  std::vector<UrlEventRecord> listUrlEventsForSession(std::int64_t session_id,
                                                     std::size_t limit = 1000) const;

  // --- Phone intervals (Phase 3 / stats) ---
  std::int64_t insertPhoneEvent(std::optional<std::int64_t> session_id, EpochSeconds started_at,
                                std::optional<EpochSeconds> ended_at, double confidence = 1.0);
  std::int64_t sumPhoneSecondsForSession(std::int64_t session_id) const;

private:
  void migrate(); // caller must hold mu_
  void execOrThrow(const char* sql) const; // caller must hold mu_
  /// Backfill segments for legacy sessions that predate focus_segments (holds mu_).
  void backfillFocusSegmentsUnlocked();
  FocusSegmentRecord startFocusSegmentUnlocked(std::int64_t session_id, EpochSeconds started_at);
  int endOpenFocusSegmentsUnlocked(std::optional<std::int64_t> session_id, EpochSeconds ended_at,
                                   const std::string& reason);

  std::filesystem::path db_path_;
  sqlite3* db_{nullptr};
  mutable std::mutex mu_;
};

} // namespace focusgaze
