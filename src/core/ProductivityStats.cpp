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

double ProductivityStats::computeScore(const SessionStats& s) {
  if (s.focus_seconds <= 0) {
    return 0.0;
  }
  const double productive_ratio =
      static_cast<double>(s.productive_seconds) / static_cast<double>(s.focus_seconds);
  const double social_ratio =
      static_cast<double>(s.social_seconds) / static_cast<double>(s.focus_seconds);
  const double phone_ratio =
      static_cast<double>(s.phone_seconds) / static_cast<double>(s.focus_seconds);
  double score = 100.0 * productive_ratio - 40.0 * social_ratio - 30.0 * phone_ratio;
  // Small bonus for neutral focus time (not social)
  const double neutral_ratio =
      static_cast<double>(s.neutral_seconds) / static_cast<double>(s.focus_seconds);
  score += 20.0 * neutral_ratio;
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

SessionStats ProductivityStats::computeSession(std::int64_t session_id) const {
  SessionStats out;
  out.session_id = session_id;
  const auto session = storage_.getSession(session_id);
  if (!session) {
    return out;
  }
  out.started_at = session->started_at;
  // Open sessions keep recording in the background; treat "now" as the end so live
  // duration/score stay meaningful while Focus is still ON (UI refresh is optional).
  if (session->ended_at.has_value()) {
    out.ended_at = *session->ended_at;
  } else {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    out.ended_at = static_cast<EpochSeconds>(now);
  }
  if (out.ended_at < out.started_at) {
    out.ended_at = out.started_at;
  }
  out.focus_seconds = out.ended_at - out.started_at;

  auto events = storage_.listUrlEventsForSession(session_id, 100000);
  out.url_event_count = static_cast<int>(events.size());

  // Attribute time from event_i.ts to event_{i+1}.ts (or session end) to event_i.category
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
    const std::int64_t dur = t1 - t0;
    if (ev.category == "blocked") {
      out.social_seconds += dur;
    } else if (ev.category == "allow") {
      out.productive_seconds += dur;
    } else {
      out.neutral_seconds += dur;
    }
  }

  // Unaccounted focus time → neutral
  const std::int64_t accounted =
      out.social_seconds + out.productive_seconds + out.neutral_seconds;
  if (out.focus_seconds > accounted) {
    out.neutral_seconds += (out.focus_seconds - accounted);
  }

  out.phone_seconds = storage_.sumPhoneSecondsForSession(session_id);
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
    d.productive_seconds += st.productive_seconds;
    d.phone_seconds += st.phone_seconds;
    ++d.session_count;
  }
  SessionStats agg;
  agg.focus_seconds = d.focus_seconds;
  agg.social_seconds = d.social_seconds;
  agg.productive_seconds = d.productive_seconds;
  agg.phone_seconds = d.phone_seconds;
  d.score = computeScore(agg);
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

} // namespace focusgaze
