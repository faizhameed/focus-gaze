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
  SessionRecord createSession(EpochSeconds started_at, bool focus_enabled = true);
  bool endSession(std::int64_t session_id, EpochSeconds ended_at);
  std::optional<SessionRecord> getSession(std::int64_t session_id) const;
  std::optional<SessionRecord> getActiveSession() const;
  std::vector<SessionRecord> listSessions(std::size_t limit = 100) const;

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

  std::filesystem::path db_path_;
  sqlite3* db_{nullptr};
  mutable std::mutex mu_;
};

} // namespace focusgaze
