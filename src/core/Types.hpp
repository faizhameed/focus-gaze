#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace focusgaze {

/// Unix epoch seconds (UTC).
using EpochSeconds = std::int64_t;

struct SessionRecord {
  std::int64_t id{0};
  EpochSeconds started_at{0};
  std::optional<EpochSeconds> ended_at;
  bool focus_enabled{true};
};

/// Contiguous wall-clock span when focus time is actively counted (unlocked, awake, app alive).
/// Pauses for lock / sleep / process death leave gaps between segments.
struct FocusSegmentRecord {
  std::int64_t id{0};
  std::int64_t session_id{0};
  EpochSeconds started_at{0};
  /// Set when the segment stops counting (pause, Focus OFF, process exit).
  std::optional<EpochSeconds> ended_at;
  /// Heartbeat while the segment is open; used to close orphans after crash/sleep.
  std::optional<EpochSeconds> last_seen_at;
  /// Why the segment ended (lock, sleep_gap, focus_off, process_exit, orphan, …).
  std::string end_reason;
};

enum class UrlEventType {
  Activated,
  Updated,
  Closed,
};

inline const char* toString(UrlEventType t) {
  switch (t) {
    case UrlEventType::Activated: return "activated";
    case UrlEventType::Updated: return "updated";
    case UrlEventType::Closed: return "closed";
  }
  return "unknown";
}

inline std::optional<UrlEventType> urlEventTypeFromString(const std::string& s) {
  if (s == "activated") return UrlEventType::Activated;
  if (s == "updated") return UrlEventType::Updated;
  if (s == "closed") return UrlEventType::Closed;
  return std::nullopt;
}

struct UrlEventRecord {
  std::int64_t id{0};
  std::optional<std::int64_t> session_id;
  EpochSeconds ts{0};
  std::string url;
  std::string domain;
  std::string title;
  std::string tab_id;
  std::string browser;
  UrlEventType event{UrlEventType::Activated};
  std::string category; // blocked | allow | neutral
};

} // namespace focusgaze
