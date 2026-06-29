#include "core/ProductivityStats.hpp"

#include <algorithm>
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
  out.ended_at = session->ended_at.value_or(session->started_at);
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
  for (const auto& s : sessions) {
    if (s.ended_at.has_value()) {
      return computeSession(s.id);
    }
  }
  if (!sessions.empty()) {
    return computeSession(sessions.front().id);
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

} // namespace focusgaze
