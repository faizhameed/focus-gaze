#include "core/Settings.hpp"
#include "core/UrlClassifier.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::Settings;
using focusgaze::UrlCategory;
using focusgaze::UrlClassifier;

TEST_CASE("extractDomain parses common URLs", "[classifier]") {
  REQUIRE(UrlClassifier::extractDomain("https://www.instagram.com/reel/abc") == "www.instagram.com");
  REQUIRE(UrlClassifier::extractDomain("http://GITHUB.com/foo") == "github.com");
  REQUIRE(UrlClassifier::extractDomain("https://user:pass@x.com:443/path") == "x.com");
  REQUIRE(UrlClassifier::extractDomain("chrome://extensions") == "extensions");
  REQUIRE(UrlClassifier::extractDomain("") == "");
}

TEST_CASE("domainMatches supports subdomains and www", "[classifier]") {
  REQUIRE(UrlClassifier::domainMatches("www.instagram.com", "instagram.com"));
  REQUIRE(UrlClassifier::domainMatches("m.instagram.com", "instagram.com"));
  REQUIRE(UrlClassifier::domainMatches("instagram.com", "www.instagram.com"));
  REQUIRE(UrlClassifier::domainMatches("https://www.instagram.com/reel/1", "instagram.com"));
  REQUIRE_FALSE(UrlClassifier::domainMatches("notinstagram.com", "instagram.com"));
  REQUIRE_FALSE(UrlClassifier::domainMatches("instagram.com.evil.com", "instagram.com"));
}

TEST_CASE("normalizeDomainEntry accepts bare domains and full URLs", "[classifier]") {
  REQUIRE(UrlClassifier::normalizeDomainEntry("sitename.com") == "sitename.com");
  REQUIRE(UrlClassifier::normalizeDomainEntry("www.sitename.com") == "sitename.com");
  REQUIRE(UrlClassifier::normalizeDomainEntry("https://www.sitename.com/path?q=1") == "sitename.com");
  REQUIRE(UrlClassifier::normalizeDomainEntry("*.sitename.com") == "sitename.com");
  REQUIRE(UrlClassifier::normalizeDomainEntry("  Reddit.COM  ") == "reddit.com");
  REQUIRE(UrlClassifier::normalizeDomainEntry("# comment") == "");
  REQUIRE(UrlClassifier::normalizeDomainEntry("") == "");

  const auto list = UrlClassifier::normalizeDomainList(
      {"www.foo.com", "https://foo.com/x", "foo.com", "bar.com", "#x", ""});
  REQUIRE(list.size() == 2);
  REQUIRE(list[0] == "foo.com");
  REQUIRE(list[1] == "bar.com");
}

TEST_CASE("blocklist sitename.com blocks www and mobile variants", "[classifier]") {
  Settings s = Settings::defaults();
  s.blocklist = UrlClassifier::normalizeDomainList({"sitename.com"});
  UrlClassifier c(s);
  REQUIRE(c.classifyUrl("https://sitename.com/") == UrlCategory::Blocked);
  REQUIRE(c.classifyUrl("https://www.sitename.com/home") == UrlCategory::Blocked);
  REQUIRE(c.classifyUrl("https://m.sitename.com/") == UrlCategory::Blocked);
  REQUIRE(c.classifyUrl("https://app.sitename.com/v1") == UrlCategory::Blocked);
  REQUIRE(c.classifyUrl("https://notsitename.com/") == UrlCategory::Neutral);
}

TEST_CASE("classifyUrl blocklist and allowlist precedence", "[classifier]") {
  Settings s = Settings::defaults();
  s.blocklist = {"instagram.com", "reddit.com"};
  s.allowlist = {"reddit.com"};
  UrlClassifier c(s);

  REQUIRE(c.classifyUrl("https://instagram.com/") == UrlCategory::Blocked);
  REQUIRE(c.classifyUrl("https://www.reddit.com/r/cpp") == UrlCategory::Allow);
  REQUIRE(c.classifyUrl("https://github.com") == UrlCategory::Neutral);
}

TEST_CASE("redactUrl strips query and optional path under privacy mode", "[classifier]") {
  const auto normal = UrlClassifier::redactUrl("https://x.com/a?q=1#frag", false);
  REQUIRE(normal.find('?') == std::string::npos);
  REQUIRE(normal.find('#') == std::string::npos);
  REQUIRE(normal.find("/a") != std::string::npos);

  const auto priv = UrlClassifier::redactUrl("https://x.com/a?q=1", true);
  REQUIRE(priv == "https://x.com/");
}
