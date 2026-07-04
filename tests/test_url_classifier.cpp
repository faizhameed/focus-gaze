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
  REQUIRE_FALSE(UrlClassifier::domainMatches("notinstagram.com", "instagram.com"));
  REQUIRE_FALSE(UrlClassifier::domainMatches("instagram.com.evil.com", "instagram.com"));
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
