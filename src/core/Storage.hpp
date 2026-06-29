#pragma once

#include "core/Types.hpp"

#include <filesystem>
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

/// SQLite-backed persistence for sessions and related Phase-1 tables.
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
  SessionRecord createSession(EpochSeconds started_at, bool focus_enabled = true);
  bool endSession(std::int64_t session_id, EpochSeconds ended_at);
  std::optional<SessionRecord> getSession(std::int64_t session_id) const;
  std::optional<SessionRecord> getActiveSession() const;
  std::vector<SessionRecord> listSessions(std::size_t limit = 100) const;

  // --- URL events (schema ready; used heavily in Phase 2) ---
  std::int64_t insertUrlEvent(const UrlEventRecord& event);
  std::vector<UrlEventRecord> listUrlEventsForSession(std::int64_t session_id,
                                                     std::size_t limit = 1000) const;

private:
  void migrate();
  void execOrThrow(const char* sql) const;

  std::filesystem::path db_path_;
  sqlite3* db_{nullptr};
};

} // namespace focusgaze
