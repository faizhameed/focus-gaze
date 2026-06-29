#include "core/BrowserMonitor.hpp"

#include <chrono>

namespace focusgaze {

BrowserMonitor::BrowserMonitor(Storage& storage, FocusSessionManager& focus, Settings settings)
    : storage_(storage),
      focus_(focus),
      settings_(std::move(settings)),
      classifier_(settings_) {}

void BrowserMonitor::setSettings(Settings settings) {
  std::lock_guard lock(mutex_);
  settings_ = std::move(settings);
  classifier_.setSettings(settings_);
}

Settings BrowserMonitor::settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

EpochSeconds BrowserMonitor::now() const {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void BrowserMonitor::onFocusTurnedOff() {
  std::lock_guard lock(mutex_);
  alarms_.clearAllSocialTabs();
}

bool BrowserMonitor::handleEvent(const BrowserUrlEvent& event) {
  std::lock_guard lock(mutex_);

  const EpochSeconds ts = event.ts.value_or(now());
  const bool focus_on = focus_.isFocusOn();

  if (event.event == UrlEventType::Closed) {
    if (event.tab_id.empty()) {
      return false;
    }
    alarms_.clearBlockedTab(event.tab_id);

    UrlEventRecord rec;
    if (auto s = focus_.activeSession()) {
      rec.session_id = s->id;
    }
    rec.ts = ts;
    rec.url = event.url.empty() ? std::string("(closed)") : event.url;
    rec.domain = UrlClassifier::extractDomain(event.url);
    rec.title = event.title;
    rec.tab_id = event.tab_id;
    rec.browser = event.browser.empty() ? "chrome" : event.browser;
    rec.event = UrlEventType::Closed;
    rec.category = last_category_.empty() ? "neutral" : last_category_;
    if (focus_on || rec.session_id) {
      try {
        storage_.insertUrlEvent(rec);
      } catch (const StorageError&) {
        // best effort logging
      }
    }
    if (!focus_on) {
      alarms_.clearAllSocialTabs();
    }
    return true;
  }

  // activated / updated need a URL ideally
  std::string url = event.url;
  const std::string domain = UrlClassifier::extractDomain(url);
  const UrlCategory category = classifier_.classifyDomain(domain);
  const std::string category_str = toString(category);

  if (settings_.privacy_redact && !url.empty()) {
    url = UrlClassifier::redactUrl(url, true);
  } else if (!url.empty()) {
    url = UrlClassifier::redactUrl(url, false);
  }

  last_url_ = url;
  last_domain_ = domain;
  last_category_ = category_str;

  UrlEventRecord rec;
  if (auto s = focus_.activeSession()) {
    rec.session_id = s->id;
  }
  rec.ts = ts;
  rec.url = url.empty() ? std::string("(empty)") : url;
  rec.domain = domain;
  rec.title = event.title;
  rec.tab_id = event.tab_id;
  rec.browser = event.browser.empty() ? "chrome" : event.browser;
  rec.event = event.event;
  rec.category = category_str;

  // Log whenever we have focus session or always log for debugging when focus on only?
  // Spec: log URLs; enforce only when focus on. Log when focus on (session active).
  if (focus_on && rec.session_id) {
    try {
      storage_.insertUrlEvent(rec);
    } catch (const StorageError&) {
    }
  }

  if (!focus_on) {
    alarms_.clearAllSocialTabs();
    return true;
  }

  if (event.tab_id.empty()) {
    return true;
  }

  if (category == UrlCategory::Blocked) {
    alarms_.markBlockedTab(event.tab_id, domain);
  } else {
    alarms_.clearBlockedTab(event.tab_id);
  }
  return true;
}

MonitorStatus BrowserMonitor::status() const {
  std::lock_guard lock(mutex_);
  MonitorStatus st;
  st.focus_on = focus_.isFocusOn();
  st.alarm_active = alarms_.isActive();
  for (const auto reason : alarms_.activeReasons()) {
    st.alarm_reasons.emplace_back(toString(reason));
  }
  st.blocked_tab_count = alarms_.blockedTabs().size();
  if (auto s = focus_.activeSession()) {
    st.session_id = s->id;
  }
  st.last_url = last_url_;
  st.last_domain = last_domain_;
  st.last_category = last_category_;
  return st;
}

} // namespace focusgaze
