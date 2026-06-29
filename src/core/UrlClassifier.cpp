#include "core/UrlClassifier.hpp"

#include <algorithm>
#include <cctype>

namespace focusgaze {
namespace {

std::string toLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

std::string stripWww(std::string_view host) {
  std::string h = toLower(host);
  if (h.rfind("www.", 0) == 0 && h.size() > 4) {
    return h.substr(4);
  }
  return h;
}

} // namespace

UrlClassifier::UrlClassifier(Settings settings) : settings_(std::move(settings)) {}

void UrlClassifier::setSettings(Settings settings) { settings_ = std::move(settings); }

std::string UrlClassifier::extractDomain(std::string_view url) {
  if (url.empty()) {
    return {};
  }
  // Scheme-relative or absolute URL
  std::size_t start = 0;
  const auto scheme = url.find("://");
  if (scheme != std::string_view::npos) {
    start = scheme + 3;
  } else if (url.rfind("//", 0) == 0) {
    start = 2;
  } else {
    // No scheme: treat whole string up to / as host if it looks like a host
    start = 0;
  }

  if (start >= url.size()) {
    return {};
  }

  // userinfo@host
  auto host_begin = start;
  const auto slash = url.find('/', start);
  const auto end = slash == std::string_view::npos ? url.size() : slash;
  std::string_view authority = url.substr(start, end - start);
  const auto at = authority.find('@');
  if (at != std::string_view::npos) {
    authority = authority.substr(at + 1);
  }
  // strip port
  const auto colon = authority.find(':');
  if (colon != std::string_view::npos) {
    authority = authority.substr(0, colon);
  }
  // IPv6 in brackets — rare; take as-is without brackets for matching
  if (!authority.empty() && authority.front() == '[') {
    const auto rb = authority.find(']');
    if (rb != std::string_view::npos) {
      authority = authority.substr(1, rb - 1);
    }
  }
  return toLower(authority);
}

std::string UrlClassifier::redactUrl(std::string_view url, bool privacy_redact) {
  if (url.empty()) {
    return {};
  }
  std::string out(url);
  const auto hash = out.find('#');
  if (hash != std::string::npos) {
    out.resize(hash);
  }
  const auto q = out.find('?');
  if (q != std::string::npos) {
    out.resize(q);
  }
  if (privacy_redact) {
    const auto domain = extractDomain(out);
    if (!domain.empty()) {
      // Keep scheme if present
      std::string scheme = "https://";
      const auto sc = url.find("://");
      if (sc != std::string_view::npos) {
        scheme = std::string(url.substr(0, sc + 3));
      }
      return scheme + domain + "/";
    }
  }
  return out;
}

bool UrlClassifier::domainMatches(std::string_view domain, std::string_view pattern) {
  if (domain.empty() || pattern.empty()) {
    return false;
  }
  const std::string d = stripWww(domain);
  const std::string p = stripWww(pattern);
  if (d == p) {
    return true;
  }
  // subdomain: foo.instagram.com matches instagram.com
  if (d.size() > p.size() + 1 && d[d.size() - p.size() - 1] == '.' &&
      d.compare(d.size() - p.size(), p.size(), p) == 0) {
    return true;
  }
  return false;
}

UrlCategory UrlClassifier::classifyDomain(std::string_view domain) const {
  if (domain.empty()) {
    return UrlCategory::Neutral;
  }
  for (const auto& entry : settings_.allowlist) {
    if (domainMatches(domain, entry)) {
      return UrlCategory::Allow;
    }
  }
  for (const auto& entry : settings_.blocklist) {
    if (domainMatches(domain, entry)) {
      return UrlCategory::Blocked;
    }
  }
  return UrlCategory::Neutral;
}

UrlCategory UrlClassifier::classifyUrl(std::string_view url) const {
  return classifyDomain(extractDomain(url));
}

} // namespace focusgaze
