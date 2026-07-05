#pragma once

#include "core/Settings.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace focusgaze {

enum class UrlCategory {
  Blocked,
  Allow,
  Neutral,
};

inline const char* toString(UrlCategory c) {
  switch (c) {
    case UrlCategory::Blocked: return "blocked";
    case UrlCategory::Allow: return "allow";
    case UrlCategory::Neutral: return "neutral";
  }
  return "neutral";
}

/// Classifies browser URLs using allowlist (wins) then blocklist domain matching.
class UrlClassifier {
public:
  explicit UrlClassifier(Settings settings = Settings::defaults());

  void setSettings(Settings settings);
  const Settings& settings() const { return settings_; }

  /// Host only, lowercased, no port. Empty if URL has no host (e.g. chrome://).
  static std::string extractDomain(std::string_view url);

  /// Strip query and fragment; if privacy_redact also drop path to origin+path minimal.
  static std::string redactUrl(std::string_view url, bool privacy_redact);

  UrlCategory classifyUrl(std::string_view url) const;
  UrlCategory classifyDomain(std::string_view domain) const;

  /// True if domain equals entry or is a subdomain of entry (entry without leading www preference).
  static bool domainMatches(std::string_view domain, std::string_view pattern);

  /**
   * Normalize a user-entered blocklist/allowlist line into a bare domain.
   * Accepts "sitename.com", "www.sitename.com", "https://sitename.com/path", "m.sitename.com"
   * → host without scheme/path/port, lowercased (www. kept only if it is the whole host logic
   * is handled in domainMatches via stripWww).
   */
  static std::string normalizeDomainEntry(std::string_view entry);

  /// Normalize every entry in a list (drops empty / comment-only results).
  static std::vector<std::string> normalizeDomainList(const std::vector<std::string>& entries);

private:
  Settings settings_;
};

} // namespace focusgaze
