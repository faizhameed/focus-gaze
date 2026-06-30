#include "test_helpers.hpp"

#include "core/PlatformPaths.hpp"
#include "core/Settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using focusgaze::Settings;
using focusgaze::loadOrCreateSettings;
using focusgaze::saveSettings;
using focusgaze::test::ScopedDataRoot;

TEST_CASE("Settings defaults include social blocklist and phone thresholds", "[settings]") {
  const auto s = Settings::defaults();
  REQUIRE_FALSE(s.blocklist.empty());
  REQUIRE(s.phone_threshold_seconds == 5);
  REQUIRE(s.phone_window_seconds == 30 * 60);
  REQUIRE(s.privacy_redact == false);
  REQUIRE(s.alarm_sound == "default");
  REQUIRE(s.bridge_port == 18765);
}

TEST_CASE("Settings JSON round-trip preserves fields", "[settings]") {
  Settings s = Settings::defaults();
  s.allowlist = {"github.com", "gitlab.com"};
  s.blocklist = {"instagram.com"};
  s.phone_threshold_seconds = 90;
  s.phone_window_seconds = 1200;
  s.alarm_sound = "chime.wav";
  s.privacy_redact = true;
  s.resume_focus_on_launch = true;
  s.bridge_port = 19000;
  s.bridge_token = "secret-token";

  const std::string json = s.toJsonString(2);
  Settings loaded;
  REQUIRE(loaded.fromJsonString(json));
  REQUIRE(loaded.allowlist == s.allowlist);
  REQUIRE(loaded.blocklist == s.blocklist);
  REQUIRE(loaded.phone_threshold_seconds == 90);
  REQUIRE(loaded.phone_window_seconds == 1200);
  REQUIRE(loaded.alarm_sound == "chime.wav");
  REQUIRE(loaded.privacy_redact == true);
  REQUIRE(loaded.resume_focus_on_launch == true);
  REQUIRE(loaded.bridge_port == 19000);
  REQUIRE(loaded.bridge_token == "secret-token");
}

TEST_CASE("Settings rejects invalid JSON and invalid values", "[settings]") {
  Settings s;
  REQUIRE_FALSE(s.fromJsonString("not-json"));
  REQUIRE_FALSE(s.fromJsonString("[]"));
  REQUIRE_FALSE(s.fromJsonString(R"({"phone_threshold_seconds": -1})"));
  REQUIRE_FALSE(s.fromJsonString(R"({"phone_window_seconds": 0})"));
  REQUIRE_FALSE(s.fromJsonString(R"({"bridge_port": 70000})"));
}

TEST_CASE("Settings save and load from file", "[settings]") {
  ScopedDataRoot scope;
  const auto path = scope.path() / "settings.json";

  Settings s = Settings::defaults();
  s.privacy_redact = true;
  s.allowlist = {"example.com"};
  REQUIRE(s.saveToFile(path));
  REQUIRE(std::filesystem::is_regular_file(path));

  Settings loaded;
  REQUIRE(loaded.loadFromFile(path));
  REQUIRE(loaded.privacy_redact == true);
  REQUIRE(loaded.allowlist.size() == 1);
  REQUIRE(loaded.allowlist[0] == "example.com");
}

TEST_CASE("loadOrCreateSettings writes defaults when missing", "[settings]") {
  ScopedDataRoot scope;
  REQUIRE(focusgaze::PlatformPaths::ensureDataLayout());

  const auto path = focusgaze::PlatformPaths::settingsPath();
  REQUIRE_FALSE(std::filesystem::exists(path));

  Settings s = loadOrCreateSettings();
  REQUIRE(std::filesystem::is_regular_file(path));
  REQUIRE(s.phone_threshold_seconds == 5);

  s.phone_threshold_seconds = 45;
  REQUIRE(saveSettings(s));

  Settings again = loadOrCreateSettings();
  // Temporary product policy forces 5s phone threshold while tuning vision.
  REQUIRE(again.phone_threshold_seconds == 5);
}

TEST_CASE("loadFromFile fails on corrupt file without clobbering instance", "[settings]") {
  ScopedDataRoot scope;
  const auto path = scope.path() / "bad.json";
  {
    std::ofstream out(path);
    out << "{not valid";
  }
  Settings s = Settings::defaults();
  s.alarm_sound = "keep-me";
  REQUIRE_FALSE(s.loadFromFile(path));
  REQUIRE(s.alarm_sound == "keep-me");
}
