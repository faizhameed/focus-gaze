/// @file StatsViewModel.cpp
/// Pure presentation helpers for the simplified Statistics page.

#include "ui/StatsViewModel.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace focusgaze {
namespace {

int pct(std::int64_t part, std::int64_t whole) {
  if (whole <= 0) return 0;
  return static_cast<int>((100.0 * static_cast<double>(part)) / static_cast<double>(whole) + 0.5);
}

std::string dayShort(const std::string& ymd) {
  // "2026-07-06" -> "07-06"
  if (ymd.size() >= 10) return ymd.substr(5, 5);
  return ymd;
}

} // namespace

std::string formatFocusDuration(std::int64_t seconds) {
  if (seconds < 0) seconds = 0;
  const auto h = seconds / 3600;
  const auto m = (seconds % 3600) / 60;
  std::ostringstream oss;
  if (h > 0) {
    oss << h << "h " << m << "m";
  } else {
    oss << m << "m";
  }
  return oss.str();
}

StatsViewModel buildStatsViewModel(const WindowStats& window,
                                   const std::vector<DailyStats>& day_chart) {
  StatsViewModel vm;
  vm.window_title = window.label.empty() ? ProductivityStats::windowLabel(window.window)
                                         : window.label;
  vm.session_count = window.session_count;

  if (window.session_count > 0 || window.focus_seconds > 0) {
    // Whole-number score for a calmer UI (no "87.3" noise).
    vm.score_text = std::to_string(static_cast<int>(std::lround(window.score)));
    vm.focus_text = formatFocusDuration(window.focus_seconds);
    const auto fs = std::max<std::int64_t>(1, window.focus_seconds);
    vm.productive_pct = pct(window.productive_seconds, fs);
    vm.unproductive_pct = pct(window.unproductive_seconds, fs);
    vm.phone_pct = pct(window.phone_seconds, fs);
    vm.productive_text = formatFocusDuration(window.productive_seconds);
    vm.unproductive_text = formatFocusDuration(window.unproductive_seconds);
    vm.phone_text = formatFocusDuration(window.phone_seconds);
  } else {
    vm.score_text = "—";
    vm.focus_text = "—";
    vm.productive_text = "0m";
    vm.unproductive_text = "0m";
    vm.phone_text = "0m";
  }

  for (const auto& d : day_chart) {
    StatsDayBar bar;
    bar.day_label = dayShort(d.day);
    bar.focus_seconds = d.focus_seconds;
    const std::int64_t prod =
        d.productive_seconds > 0
            ? d.productive_seconds
            : std::max<std::int64_t>(0, d.focus_seconds - d.social_seconds - d.phone_seconds);
    bar.score_0_100 = std::clamp(
        static_cast<int>(std::lround(ProductivityStats::computeScoreParts(
            d.focus_seconds, prod, d.social_seconds, d.phone_seconds))),
        0, 100);
    vm.days.push_back(std::move(bar));
  }

  // At most 5 recent sessions in the minimal list.
  const std::size_t n = std::min<std::size_t>(5, window.sessions.size());
  for (std::size_t i = 0; i < n; ++i) {
    const auto& s = window.sessions[i];
    std::ostringstream line;
    line << "#" << s.session_id << "  " << formatFocusDuration(s.focus_seconds) << "  "
         << static_cast<int>(std::lround(s.score));
    vm.session_lines.push_back(line.str());
  }
  return vm;
}

std::string StatsViewModel::toSnapshot() const {
  std::ostringstream out;
  out << "STATS_SNAPSHOT_V1\n";
  out << "window=" << window_title << "\n";
  out << "sessions=" << session_count << "\n";
  out << "score=" << score_text << "\n";
  out << "focus=" << focus_text << "\n";
  out << "productive=" << productive_pct << "% " << productive_text << "\n";
  out << "distracted=" << unproductive_pct << "% " << unproductive_text << "\n";
  out << "phone=" << phone_pct << "% " << phone_text << "\n";
  out << "days=" << days.size() << "\n";
  for (const auto& d : days) {
    out << "  day " << d.day_label << " score=" << d.score_0_100
        << " focus=" << formatFocusDuration(d.focus_seconds) << "\n";
  }
  out << "recent=" << session_lines.size() << "\n";
  for (const auto& line : session_lines) {
    out << "  " << line << "\n";
  }
  return out.str();
}

} // namespace focusgaze
