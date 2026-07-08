#include "core/ProductivityStats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace focusgaze {
namespace {

std::string dayFromEpoch(EpochSeconds ts) {
  std::time_t t = static_cast<std::time_t>(ts);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d");
  return oss.str();
}

/// Shift a local calendar day by delta days (can be negative). Input/output YYYY-MM-DD.
std::string shiftDay(const std::string& ymd, int delta_days) {
  std::tm tm{};
  if (std::sscanf(ymd.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
    return ymd;
  }
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  tm.tm_hour = 12; // midday avoids DST edge issues for day arithmetic
  tm.tm_isdst = -1;
  std::time_t t = std::mktime(&tm);
  if (t == static_cast<std::time_t>(-1)) return ymd;
  t += static_cast<std::time_t>(delta_days) * 24 * 60 * 60;
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d");
  return oss.str();
}

} // namespace

ProductivityStats::ProductivityStats(Storage& storage) : storage_(storage) {}

double ProductivityStats::computeScoreParts(std::int64_t focus_s, std::int64_t productive_s,
                                            std::int64_t unproductive_s, std::int64_t phone_s) {
  if (focus_s <= 0) return 0.0;
  const double f = static_cast<double>(focus_s);
  // Productive share is rewarded; unproductive sites and phone time are penalized.
  double score = 100.0 * (static_cast<double>(productive_s) / f) -
                 40.0 * (static_cast<double>(unproductive_s) / f) -
                 30.0 * (static_cast<double>(phone_s) / f);
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

double ProductivityStats::computeScore(const SessionStats& s) {
  // “Productive time” for scoring = allowlist + neutral (anything not on the blocklist).
  const std::int64_t productive = s.productive_seconds + s.neutral_seconds;
  return computeScoreParts(s.focus_seconds, productive, s.social_seconds, s.phone_seconds);
}

const char* ProductivityStats::windowLabel(StatsWindow w) {
  switch (w) {
    case StatsWindow::LastSession: return "Last session";
    case StatsWindow::Today: return "Today";
    case StatsWindow::Yesterday: return "Yesterday";
    case StatsWindow::ThisWeek: return "This week";
    case StatsWindow::LastWeek: return "Last week";
    case StatsWindow::Last7Days: return "Last 7 days";
    case StatsWindow::Month: return "This month";
    case StatsWindow::Custom: return "Custom range";
  }
  return "Last session";
}

EpochSeconds ProductivityStats::localMidnightEpoch(const std::string& ymd) {
  std::tm tm{};
  if (std::sscanf(ymd.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
    return 0;
  }
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  const std::time_t t = std::mktime(&tm);
  if (t == static_cast<std::time_t>(-1)) return 0;
  return static_cast<EpochSeconds>(t);
}

/// Seconds of [t0, t1) that fall inside any counting segment (open → now_for_open).
static std::int64_t overlapWithSegments(EpochSeconds t0, EpochSeconds t1,
                                        const std::vector<FocusSegmentRecord>& segments,
                                        EpochSeconds now_for_open) {
  if (t1 <= t0) return 0;
  std::int64_t total = 0;
  for (const auto& seg : segments) {
    const EpochSeconds s0 = seg.started_at;
    const EpochSeconds s1 = seg.ended_at.value_or(now_for_open);
    if (s1 <= s0) continue;
    const EpochSeconds a = std::max(t0, s0);
    const EpochSeconds b = std::min(t1, s1);
    if (b > a) total += (b - a);
  }
  return total;
}

SessionStats ProductivityStats::computeSession(std::int64_t session_id) const {
  SessionStats out;
  out.session_id = session_id;
  const auto session = storage_.getSession(session_id);
  if (!session) {
    return out;
  }
  out.started_at = session->started_at;
  // Open sessions: "now" is only used for live UI and open segment ends — not for
  // inventing wall-clock focus across lock/sleep (those are non-segment gaps).
  const auto now = static_cast<EpochSeconds>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  if (session->ended_at.has_value()) {
    out.ended_at = *session->ended_at;
  } else {
    out.ended_at = now;
  }
  if (out.ended_at < out.started_at) {
    out.ended_at = out.started_at;
  }

  // Authoritative focus duration = sum of counting segments (not session wall span).
  out.focus_seconds = storage_.sumFocusSecondsForSession(session_id, now);

  const auto segments = storage_.listFocusSegmentsForSession(session_id);
  auto events = storage_.listUrlEventsForSession(session_id, 100000);
  out.url_event_count = static_cast<int>(events.size());

  // Attribute URL time only while a counting segment was active (exclude lock/sleep gaps).
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& ev = events[i];
    if (ev.category == "blocked") {
      ++out.blocked_event_count;
    }
    if (ev.event == UrlEventType::Closed) {
      continue;
    }
    const EpochSeconds t0 = std::max(ev.ts, out.started_at);
    EpochSeconds t1 = out.ended_at;
    if (i + 1 < events.size()) {
      t1 = std::min(out.ended_at, events[i + 1].ts);
    }
    if (t1 <= t0) {
      continue;
    }
    const std::int64_t dur = overlapWithSegments(t0, t1, segments, now);
    if (dur <= 0) continue;
    if (ev.category == "blocked") {
      out.social_seconds += dur;
    } else if (ev.category == "allow") {
      out.productive_seconds += dur;
    } else {
      out.neutral_seconds += dur;
    }
  }

  // Unaccounted *counted* focus time → neutral (never inflate with lock/sleep gaps).
  const std::int64_t accounted =
      out.social_seconds + out.productive_seconds + out.neutral_seconds;
  if (out.focus_seconds > accounted) {
    out.neutral_seconds += (out.focus_seconds - accounted);
  }

  out.phone_seconds = storage_.sumPhoneSecondsForSession(session_id);
  // Cap phone to focus window so paused gaps cannot dominate score alone.
  if (out.phone_seconds > out.focus_seconds) {
    out.phone_seconds = out.focus_seconds;
  }
  out.score = computeScore(out);
  return out;
}

std::optional<SessionStats> ProductivityStats::lastSessionSummary() const {
  const auto sessions = storage_.listSessions(20);
  // Prefer the open (active) Focus session so live UI bars update while Focus is ON.
  // Newest-first list: first open session is the current one.
  for (const auto& s : sessions) {
    if (!s.ended_at.has_value()) {
      return computeSession(s.id);
    }
  }
  // Otherwise most recently ended session.
  for (const auto& s : sessions) {
    if (s.ended_at.has_value()) {
      return computeSession(s.id);
    }
  }
  return std::nullopt;
}

DailyStats ProductivityStats::computeDay(const std::string& day) const {
  DailyStats d;
  d.day = day;
  const auto sessions = storage_.listSessions(500);
  for (const auto& s : sessions) {
    if (dayFromEpoch(s.started_at) != day) {
      continue;
    }
    const auto st = computeSession(s.id);
    d.focus_seconds += st.focus_seconds;
    d.social_seconds += st.social_seconds;
    // UI “productive” = allowlist work sites + neutral (non-blocklist) browsing.
    d.productive_seconds += st.productive_seconds + st.neutral_seconds;
    d.phone_seconds += st.phone_seconds;
    ++d.session_count;
  }
  // Score uses the same three-part formula as WindowStats (productive already includes neutral).
  d.score = computeScoreParts(d.focus_seconds, d.productive_seconds, d.social_seconds,
                              d.phone_seconds);
  return d;
}

std::vector<SessionStats> ProductivityStats::recentSessions(std::size_t limit) const {
  std::vector<SessionStats> out;
  const auto sessions = storage_.listSessions(limit == 0 ? 1 : limit);
  out.reserve(sessions.size());
  for (const auto& s : sessions) {
    out.push_back(computeSession(s.id));
  }
  return out;
}

std::vector<DailyStats> ProductivityStats::lastNDays(int num_days) const {
  if (num_days < 1) num_days = 1;
  if (num_days > 90) num_days = 90;
  const std::string today = todayLocalYmd();
  std::vector<DailyStats> out;
  out.reserve(static_cast<std::size_t>(num_days));
  for (int i = num_days - 1; i >= 0; --i) {
    out.push_back(computeDay(shiftDay(today, -i)));
  }
  return out;
}

std::string ProductivityStats::todayLocalYmd() {
  const auto now = std::chrono::system_clock::now();
  const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return dayFromEpoch(static_cast<EpochSeconds>(ts));
}

std::string ProductivityStats::dayFromEpochSeconds(EpochSeconds ts) {
  return dayFromEpoch(ts);
}

std::string ProductivityStats::toCsv(const SessionStats& s) {
  std::ostringstream oss;
  oss << "session_id,started_at,ended_at,focus_seconds,social_seconds,productive_seconds,"
         "neutral_seconds,phone_seconds,url_events,blocked_events,score\n";
  oss << s.session_id << "," << s.started_at << "," << s.ended_at << "," << s.focus_seconds
      << "," << s.social_seconds << "," << s.productive_seconds << "," << s.neutral_seconds
      << "," << s.phone_seconds << "," << s.url_event_count << "," << s.blocked_event_count
      << "," << std::fixed << std::setprecision(1) << s.score << "\n";
  return oss.str();
}

std::string ProductivityStats::formatReport(const SessionStats& s) {
  std::ostringstream oss;
  oss << "=== focusGaze session summary ===\n"
      << "session_id=" << s.session_id << "\n"
      << "focus_seconds=" << s.focus_seconds << "\n"
      << "productive_seconds=" << s.productive_seconds << "\n"
      << "social_seconds=" << s.social_seconds << "\n"
      << "neutral_seconds=" << s.neutral_seconds << "\n"
      << "phone_seconds=" << s.phone_seconds << "\n"
      << "url_events=" << s.url_event_count << " blocked_events=" << s.blocked_event_count
      << "\n"
      << "score=" << std::fixed << std::setprecision(1) << s.score << " / 100\n"
      << "(Higher is more productive: more allowlist time, less social/phone.)\n";
  return oss.str();
}

std::string ProductivityStats::toCsvMany(const std::vector<SessionStats>& sessions) {
  if (sessions.empty()) {
    return "session_id,started_at,ended_at,focus_seconds,social_seconds,productive_seconds,"
           "neutral_seconds,phone_seconds,url_events,blocked_events,score\n";
  }
  std::ostringstream oss;
  // Header once, then data rows without repeating header from toCsv.
  oss << "session_id,started_at,ended_at,focus_seconds,social_seconds,productive_seconds,"
         "neutral_seconds,phone_seconds,url_events,blocked_events,score\n";
  for (const auto& s : sessions) {
    oss << s.session_id << "," << s.started_at << "," << s.ended_at << "," << s.focus_seconds
        << "," << s.social_seconds << "," << s.productive_seconds << "," << s.neutral_seconds
        << "," << s.phone_seconds << "," << s.url_event_count << "," << s.blocked_event_count
        << "," << std::fixed << std::setprecision(1) << s.score << "\n";
  }
  return oss.str();
}

std::string ProductivityStats::toJsonMany(const std::vector<SessionStats>& sessions) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : sessions) {
    nlohmann::json j;
    j["session_id"] = s.session_id;
    j["started_at"] = s.started_at;
    j["ended_at"] = s.ended_at;
    j["focus_seconds"] = s.focus_seconds;
    j["social_seconds"] = s.social_seconds;
    j["productive_seconds"] = s.productive_seconds;
    j["neutral_seconds"] = s.neutral_seconds;
    j["phone_seconds"] = s.phone_seconds;
    j["url_event_count"] = s.url_event_count;
    j["blocked_event_count"] = s.blocked_event_count;
    j["score"] = s.score;
    arr.push_back(std::move(j));
  }
  return arr.dump(2);
}

WindowStats ProductivityStats::computeRange(EpochSeconds start, EpochSeconds end, StatsWindow tag,
                                            std::string label) const {
  WindowStats w;
  w.window = tag;
  w.label = std::move(label);
  w.range_start = start;
  w.range_end = end;
  if (end < start) std::swap(start, end);

  const auto sessions = storage_.listSessions(500);
  for (const auto& rec : sessions) {
    const EpochSeconds s_end = rec.ended_at.value_or(static_cast<EpochSeconds>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count()));
    // Include session if it overlaps [start, end).
    if (s_end < start || rec.started_at >= end) continue;
    auto st = computeSession(rec.id);
    w.sessions.push_back(st);
    w.focus_seconds += st.focus_seconds;
    w.productive_seconds += st.productive_seconds + st.neutral_seconds;
    w.unproductive_seconds += st.social_seconds;
    w.phone_seconds += st.phone_seconds;
    w.blocked_event_count += st.blocked_event_count;
    ++w.session_count;
  }
  w.score = computeScoreParts(w.focus_seconds, w.productive_seconds, w.unproductive_seconds,
                              w.phone_seconds);
  return w;
}

WindowStats ProductivityStats::computeWindow(StatsWindow window) const {
  const std::string today = todayLocalYmd();
  const EpochSeconds now = static_cast<EpochSeconds>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  if (window == StatsWindow::LastSession) {
    WindowStats w;
    w.window = window;
    w.label = windowLabel(window);
    if (auto s = lastSessionSummary()) {
      w.sessions.push_back(*s);
      w.focus_seconds = s->focus_seconds;
      w.productive_seconds = s->productive_seconds + s->neutral_seconds;
      w.unproductive_seconds = s->social_seconds;
      w.phone_seconds = s->phone_seconds;
      w.blocked_event_count = s->blocked_event_count;
      w.session_count = 1;
      w.range_start = s->started_at;
      w.range_end = s->ended_at;
      w.score = s->score;
    }
    return w;
  }

  EpochSeconds start = 0;
  EpochSeconds end = now + 1;

  switch (window) {
    case StatsWindow::Today:
      start = localMidnightEpoch(today);
      end = start + 24 * 3600;
      break;
    case StatsWindow::Yesterday: {
      const std::string y = shiftDay(today, -1);
      start = localMidnightEpoch(y);
      end = start + 24 * 3600;
      break;
    }
    case StatsWindow::Last7Days:
      start = localMidnightEpoch(shiftDay(today, -6));
      end = localMidnightEpoch(today) + 24 * 3600;
      break;
    case StatsWindow::ThisWeek: {
      // Monday-start week containing today.
      std::tm tm{};
      const EpochSeconds mid = localMidnightEpoch(today);
      std::time_t t = static_cast<std::time_t>(mid);
#if defined(_WIN32)
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      // tm_wday: 0=Sun … 6=Sat → days since Monday
      const int since_mon = (tm.tm_wday == 0) ? 6 : (tm.tm_wday - 1);
      start = mid - static_cast<EpochSeconds>(since_mon) * 24 * 3600;
      end = start + 7 * 24 * 3600;
      break;
    }
    case StatsWindow::LastWeek: {
      std::tm tm{};
      const EpochSeconds mid = localMidnightEpoch(today);
      std::time_t t = static_cast<std::time_t>(mid);
#if defined(_WIN32)
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      const int since_mon = (tm.tm_wday == 0) ? 6 : (tm.tm_wday - 1);
      const EpochSeconds this_mon = mid - static_cast<EpochSeconds>(since_mon) * 24 * 3600;
      end = this_mon;
      start = this_mon - 7 * 24 * 3600;
      break;
    }
    case StatsWindow::Month: {
      // First day of current month 00:00 → now
      std::string first = today.substr(0, 8) + "01";
      start = localMidnightEpoch(first);
      end = now + 1;
      break;
    }
    case StatsWindow::Custom:
      // Custom ranges must be built with computeRange(start, end, Custom, label)
      // from the Statistics date picker (from/to calendar days).
      return WindowStats{};
    case StatsWindow::LastSession:
      break;
  }

  return computeRange(start, end, window, windowLabel(window));
}

std::vector<DailyStats> ProductivityStats::daysInRange(EpochSeconds start, EpochSeconds end) const {
  std::vector<DailyStats> out;
  if (end <= start) return out;
  const std::string d0 = dayFromEpoch(start);
  const std::string d1 = dayFromEpoch(end - 1);
  std::string cur = d0;
  // Safety: max 62 days in a chart
  for (int n = 0; n < 62; ++n) {
    out.push_back(computeDay(cur));
    if (cur == d1) break;
    cur = shiftDay(cur, 1);
  }
  return out;
}

} // namespace focusgaze
