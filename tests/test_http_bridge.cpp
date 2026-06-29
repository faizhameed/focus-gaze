#include "test_helpers.hpp"

#include "bridge/HttpBrowserBridge.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <httplib.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using focusgaze::BrowserMonitor;
using focusgaze::FocusSessionManager;
using focusgaze::HttpBrowserBridge;
using focusgaze::Settings;
using focusgaze::Storage;
using focusgaze::test::ScopedDataRoot;

namespace {
struct FakeClock {
  focusgaze::EpochSeconds now{2000};
  focusgaze::EpochSeconds operator()() const { return now; }
};

int pickPort() {
  // Ephemeral-ish port for tests; avoid default 18765 conflicts
  static int n = 19000;
  return n++;
}
} // namespace

TEST_CASE("HttpBrowserBridge health is public; status requires auth", "[bridge]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "test-token-abc";
  const int port = pickPort();
  settings.bridge_port = port;

  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);

  auto health = client.Get("/v1/health");
  REQUIRE(health);
  REQUIRE(health->status == 200);

  auto unauth = client.Get("/v1/status");
  REQUIRE(unauth);
  REQUIRE(unauth->status == 401);

  httplib::Headers headers{{"Authorization", "Bearer test-token-abc"}};
  auto st = client.Get("/v1/status", headers);
  REQUIRE(st);
  REQUIRE(st->status == 200);
  REQUIRE(st->body.find("\"focus_on\"") != std::string::npos);

  bridge.stop();
}

TEST_CASE("HttpBrowserBridge POST url drives sticky alarm", "[bridge]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "tok";
  const int port = pickPort();
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  REQUIRE(focus.turnOn());
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  httplib::Headers headers{
      {"Authorization", "Bearer tok"},
      {"Content-Type", "application/json"},
  };

  const std::string body =
      R"({"url":"https://www.instagram.com/","tabId":"42","event":"activated","ts":2001})";
  auto res = client.Post("/v1/url", headers, body, "application/json");
  REQUIRE(res);
  REQUIRE(res->status == 200);
  REQUIRE(res->body.find("\"alarm_active\":true") != std::string::npos);

  const std::string close_body = R"({"tabId":"42","event":"closed","ts":2002})";
  auto res2 = client.Post("/v1/url", headers, close_body, "application/json");
  REQUIRE(res2);
  REQUIRE(res2->status == 200);
  REQUIRE(res2->body.find("\"alarm_active\":false") != std::string::npos);

  bridge.stop();
}

TEST_CASE("loadOrCreateSettings generates bridge_token when empty", "[settings][bridge]") {
  ScopedDataRoot scope;
  REQUIRE(focusgaze::PlatformPaths::ensureDataLayout());
  // Write settings without token
  focusgaze::Settings s = focusgaze::Settings::defaults();
  s.bridge_token.clear();
  REQUIRE(s.saveToFile(focusgaze::PlatformPaths::settingsPath()));

  auto loaded = focusgaze::loadOrCreateSettings();
  REQUIRE_FALSE(loaded.bridge_token.empty());
  REQUIRE(loaded.bridge_token.size() == 32);

  auto again = focusgaze::loadOrCreateSettings();
  REQUIRE(again.bridge_token == loaded.bridge_token);
}
