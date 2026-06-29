#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace focusgaze {

enum class AlarmReason {
  SocialTab,
  PhoneWindow,
};

inline const char* toString(AlarmReason r) {
  switch (r) {
    case AlarmReason::SocialTab: return "social_tab";
    case AlarmReason::PhoneWindow: return "phone_window";
  }
  return "unknown";
}

/// Sticky alarms. Social alarm stays on while any tracked tab is blocked.
class AlarmController {
public:
  bool isActive() const { return !active_reasons_.empty(); }
  bool isReasonActive(AlarmReason reason) const;
  std::vector<AlarmReason> activeReasons() const;

  /// Tab URL became blocked while focus on. No-op if tab_id empty.
  void markBlockedTab(const std::string& tab_id, const std::string& domain);

  /// Tab navigated to non-blocked or closed.
  void clearBlockedTab(const std::string& tab_id);

  /// Drop all social tab state (e.g. Focus OFF).
  void clearAllSocialTabs();

  /// Explicit phone alarm raise/clear (Phase 3 will drive this).
  void setPhoneAlarm(bool active);

  const std::unordered_map<std::string, std::string>& blockedTabs() const {
    return blocked_tabs_;
  }

private:
  void syncSocialReason();

  std::unordered_map<std::string, std::string> blocked_tabs_; // tab_id -> domain
  std::unordered_set<AlarmReason> active_reasons_;
};

} // namespace focusgaze
