#pragma once

#include "core/AlarmController.hpp"
#include "core/FocusSession.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "core/Types.hpp"
#include "core/UrlClassifier.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace focusgaze {

struct BrowserUrlEvent {
  std::string url;
  std::string title;
  std::string tab_id;
  std::string browser{"chrome"};
  UrlEventType event{UrlEventType::Activated};
  std::optional<EpochSeconds> ts;
};

struct MonitorStatus {
  bool focus_on{false};
  bool alarm_active{false};
  std::vector<std::string> alarm_reasons;
  std::size_t blocked_tab_count{0};
  std::optional<std::int64_t> session_id;
  std::string last_url;
  std::string last_domain;
  std::string last_category;
};

/// Applies URL events: classify, persist, update sticky social alarm when Focus ON.
class BrowserMonitor {
public:
  BrowserMonitor(Storage& storage, FocusSessionManager& focus, Settings settings);

  void setSettings(Settings settings);
  Settings settings() const;

  /// Process one extension event.
  bool handleEvent(const BrowserUrlEvent& event);

  MonitorStatus status() const;
  AlarmController& alarms() { return alarms_; }
  const AlarmController& alarms() const { return alarms_; }

  /// Called when focus turns off externally — clears social alarm state.
  void onFocusTurnedOff();

private:
  EpochSeconds now() const;

  Storage& storage_;
  FocusSessionManager& focus_;
  mutable std::mutex mutex_;
  Settings settings_;
  UrlClassifier classifier_;
  AlarmController alarms_;
  std::string last_url_;
  std::string last_domain_;
  std::string last_category_;
};

} // namespace focusgaze
