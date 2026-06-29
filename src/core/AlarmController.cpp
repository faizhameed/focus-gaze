#include "core/AlarmController.hpp"

#include <algorithm>

namespace focusgaze {

bool AlarmController::isReasonActive(AlarmReason reason) const {
  return active_reasons_.count(reason) > 0;
}

std::vector<AlarmReason> AlarmController::activeReasons() const {
  std::vector<AlarmReason> out(active_reasons_.begin(), active_reasons_.end());
  std::sort(out.begin(), out.end(), [](AlarmReason a, AlarmReason b) {
    return static_cast<int>(a) < static_cast<int>(b);
  });
  return out;
}

void AlarmController::syncSocialReason() {
  if (blocked_tabs_.empty()) {
    active_reasons_.erase(AlarmReason::SocialTab);
  } else {
    active_reasons_.insert(AlarmReason::SocialTab);
  }
}

void AlarmController::markBlockedTab(const std::string& tab_id, const std::string& domain) {
  if (tab_id.empty()) {
    return;
  }
  blocked_tabs_[tab_id] = domain;
  syncSocialReason();
}

void AlarmController::clearBlockedTab(const std::string& tab_id) {
  if (tab_id.empty()) {
    return;
  }
  blocked_tabs_.erase(tab_id);
  syncSocialReason();
}

void AlarmController::clearAllSocialTabs() {
  blocked_tabs_.clear();
  syncSocialReason();
}

void AlarmController::setPhoneAlarm(bool active) {
  if (active) {
    active_reasons_.insert(AlarmReason::PhoneWindow);
  } else {
    active_reasons_.erase(AlarmReason::PhoneWindow);
  }
}

} // namespace focusgaze
