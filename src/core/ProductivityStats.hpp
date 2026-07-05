#pragma once

#include "core/Storage.hpp"
#include "core/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace focusgaze {

struct SessionStats {
  std::int64_t session_id{0};
  EpochSeconds started_at{0};
  EpochSeconds ended_at{0};
  std::int64_t focus_seconds{0};
  std::int64_t social_seconds{0};
  std::int64_t productive_seconds{0};
  std::int64_t neutral_seconds{0};
  std::int64_t phone_seconds{0};
  int url_event_count{0};
  int blocked_event_count{0};
  double score{0.0}; // 0–100
};

struct DailyStats {
  std::string day; // YYYY-MM-DD local-ish from epoch formatting
  std::int64_t focus_seconds{0};
  std::int64_t social_seconds{0};
  std::int64_t productive_seconds{0};
  std::int64_t phone_seconds{0};
  int session_count{0};
  double score{0.0};
};

class ProductivityStats {
public:
  explicit ProductivityStats(Storage& storage);

  SessionStats computeSession(std::int64_t session_id) const;
  /// Most recently ended session, or active session if none ended.
  std::optional<SessionStats> lastSessionSummary() const;
  DailyStats computeDay(const std::string& day_yyyy_mm_dd) const;

  /// Up to `limit` most recent sessions (newest first), with computed stats.
  std::vector<SessionStats> recentSessions(std::size_t limit = 10) const;

  /// Daily aggregates for the last `num_days` calendar days (oldest → newest).
  std::vector<DailyStats> lastNDays(int num_days = 7) const;

  /// Today's local date as YYYY-MM-DD.
  static std::string todayLocalYmd();
  /// Local date for a unix epoch second as YYYY-MM-DD.
  static std::string dayFromEpochSeconds(EpochSeconds ts);

  static double computeScore(const SessionStats& s);
  static std::string toCsv(const SessionStats& s);
  /// Multi-session CSV (header + rows).
  static std::string toCsvMany(const std::vector<SessionStats>& sessions);
  static std::string formatReport(const SessionStats& s);
  /// JSON array of session stats (for export).
  static std::string toJsonMany(const std::vector<SessionStats>& sessions);

private:
  Storage& storage_;
};

} // namespace focusgaze
