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
  std::int64_t social_seconds{0};      // unproductive site time
  std::int64_t productive_seconds{0};  // allowlist (+ neutral browser time)
  std::int64_t phone_seconds{0};
  int session_count{0};
  double score{0.0};
};

/// Preset windows for the Statistics page (plus Custom date range from the UI).
enum class StatsWindow {
  LastSession,
  Today,
  Yesterday,
  ThisWeek,
  LastWeek,
  Last7Days,
  Month,
  /// Inclusive local calendar range chosen in the Statistics date picker.
  Custom,
};

/// Aggregated stats over a time window (or a single open/closed session).
struct WindowStats {
  StatsWindow window{StatsWindow::LastSession};
  std::string label; // human label e.g. "Today"
  EpochSeconds range_start{0};
  EpochSeconds range_end{0};
  std::int64_t focus_seconds{0};
  /// Browser time on allowlisted work sites + non-blocked “neutral” browsing.
  std::int64_t productive_seconds{0};
  /// Browser time on blocklisted / unproductive sites.
  std::int64_t unproductive_seconds{0};
  std::int64_t phone_seconds{0};
  int session_count{0};
  int blocked_event_count{0};
  double score{0.0};
  std::vector<SessionStats> sessions; // newest first within window
};

class ProductivityStats {
public:
  explicit ProductivityStats(Storage& storage);

  SessionStats computeSession(std::int64_t session_id) const;
  /// Prefer open Focus session for live UI; else most recently ended.
  std::optional<SessionStats> lastSessionSummary() const;
  DailyStats computeDay(const std::string& day_yyyy_mm_dd) const;

  /// Up to `limit` most recent sessions (newest first), with computed stats.
  std::vector<SessionStats> recentSessions(std::size_t limit = 10) const;

  /// Daily aggregates for the last `num_days` calendar days (oldest → newest).
  std::vector<DailyStats> lastNDays(int num_days = 7) const;

  /// Aggregate all sessions that overlap [start, end) (unix seconds, local calendar for presets).
  WindowStats computeRange(EpochSeconds start, EpochSeconds end, StatsWindow tag,
                           std::string label) const;

  /// Preset windows (Today, This week, …) using the local calendar.
  WindowStats computeWindow(StatsWindow window) const;

  /// Daily breakdown inside a window for the chart (oldest → newest).
  std::vector<DailyStats> daysInRange(EpochSeconds start, EpochSeconds end) const;

  /// Today's local date as YYYY-MM-DD.
  static std::string todayLocalYmd();
  /// Local date for a unix epoch second as YYYY-MM-DD.
  static std::string dayFromEpochSeconds(EpochSeconds ts);
  /// Local midnight (unix seconds) for a YYYY-MM-DD day.
  static EpochSeconds localMidnightEpoch(const std::string& ymd);

  /**
   * Score 0–100 from time shares of the focus period:
   *   +100 × productive/focus
   *   −40  × unproductive_sites/focus
   *   −30  × phone/focus
   * Productive = allowlist + neutral browser time (not on blocklist).
   * Clamped to [0, 100].
   */
  static double computeScore(const SessionStats& s);
  static double computeScoreParts(std::int64_t focus_s, std::int64_t productive_s,
                                  std::int64_t unproductive_s, std::int64_t phone_s);

  static std::string toCsv(const SessionStats& s);
  /// Multi-session CSV (header + rows).
  static std::string toCsvMany(const std::vector<SessionStats>& sessions);
  static std::string formatReport(const SessionStats& s);
  /// JSON array of session stats (for export).
  static std::string toJsonMany(const std::vector<SessionStats>& sessions);
  static const char* windowLabel(StatsWindow w);

private:
  Storage& storage_;
};

} // namespace focusgaze
