#pragma once

/// @file StatsViewModel.hpp
/// Presentation model for the minimal Statistics page (snapshot-testable, no Qt).

#include "core/ProductivityStats.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace focusgaze {

/// One day column on the minimal stats chart.
struct StatsDayBar {
  std::string day_label; // e.g. "07-06" or "Mon"
  int score_0_100{0};
  std::int64_t focus_seconds{0};
};

/**
 * Minimal, human-readable stats layout for the Statistics page.
 * Intentionally sparse — big numbers + three shares + optional day bars.
 */
struct StatsViewModel {
  std::string window_title;     // "Today", "Last session", custom range, …
  int session_count{0};
  std::string score_text;       // "87" or "—"
  std::string focus_text;       // "1h 24m" or "—"
  int productive_pct{0};
  int unproductive_pct{0};
  int phone_pct{0};
  std::string productive_text;  // duration under bar
  std::string unproductive_text;
  std::string phone_text;
  std::vector<StatsDayBar> days;
  /// Compact session rows: "id · duration · score"
  std::vector<std::string> session_lines;

  /// Stable multi-line snapshot for golden tests / design review.
  std::string toSnapshot() const;
};

/// Format seconds as "Xh Ym" / "Ym" (shared with dashboard).
std::string formatFocusDuration(std::int64_t seconds);

/// Build the minimal stats view from core aggregates (and optional day chart).
StatsViewModel buildStatsViewModel(const WindowStats& window,
                                   const std::vector<DailyStats>& day_chart);

} // namespace focusgaze
