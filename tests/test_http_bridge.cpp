#include "test_helpers.hpp"

#include "bridge/HttpBrowserBridge.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

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

TEST_CASE("HttpBrowserBridge focus toggle requires auth and forces off", "[bridge]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "tok-focus";
  const int port = pickPort();
  settings.bridge_port = port;

  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port, nullptr, nullptr, &focus);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);

  auto unauth = client.Post("/v1/focus", "{\"on\":true}", "application/json");
  REQUIRE(unauth);
  REQUIRE(unauth->status == 401);

  httplib::Headers headers{{"Authorization", "Bearer tok-focus"}};
  auto on = client.Post("/v1/focus", headers, "{\"on\":true}", "application/json");
  REQUIRE(on);
  REQUIRE(on->status == 200);
  REQUIRE(on->body.find("\"focus_on\":true") != std::string::npos);
  REQUIRE(focus.isFocusOn());

  auto off = client.Post("/v1/focus", headers, "{\"on\":false}", "application/json");
  REQUIRE(off);
  REQUIRE(off->status == 200);
  REQUIRE_FALSE(focus.isFocusOn());
  // DB must not retain an open session after focus off.
  REQUIRE_FALSE(db.getActiveSession().has_value());

  // Idempotent off still succeeds as accepted.
  auto off2 = client.Post("/v1/focus", headers, "{\"on\":false}", "application/json");
  REQUIRE(off2);
  REQUIRE(off2->status == 200);
  REQUIRE_FALSE(focus.isFocusOn());

  bridge.stop();
}

TEST_CASE("FocusSessionManager turnOff clears DB-only open sessions", "[focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  // Simulate stale memory: open session only in DB.
  db.createSession(1000, true);
  focus.syncFromStorage();
  REQUIRE(focus.isFocusOn());
  REQUIRE(focus.turnOff());
  REQUIRE_FALSE(focus.isFocusOn());
  REQUIRE_FALSE(db.getActiveSession().has_value());
}

TEST_CASE("HttpBrowserBridge one-time pair issues token once then expires", "[bridge]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "pair-secret-xyz";
  const int port = pickPort();
  settings.bridge_port = port;

  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port, nullptr, nullptr, &focus);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);

  auto start = client.Post("/v1/pair/start", "", "application/json");
  REQUIRE(start);
  REQUIRE(start->status == 200);
  auto start_json = nlohmann::json::parse(start->body);
  REQUIRE(start_json["ok"] == true);
  REQUIRE(start_json.contains("code"));
  const std::string code = start_json["code"].get<std::string>();
  REQUIRE(code.size() == 32);

  // Pair UI should be served for a live code.
  auto ui = client.Get(("/v1/pair-ui?code=" + code).c_str());
  REQUIRE(ui);
  REQUIRE(ui->status == 200);
  REQUIRE(ui->body.find("focusgaze.pair") != std::string::npos);
  REQUIRE(ui->body.find(HttpBrowserBridge::chromeExtensionId()) != std::string::npos);

  // First consume succeeds and returns the bridge token.
  auto sess = client.Get(("/v1/pair/session?code=" + code).c_str());
  REQUIRE(sess);
  REQUIRE(sess->status == 200);
  auto sess_json = nlohmann::json::parse(sess->body);
  REQUIRE(sess_json["ok"] == true);
  REQUIRE(sess_json["token"] == "pair-secret-xyz");
  REQUIRE(sess_json["port"] == port);

  // Second consume fails (one-time).
  auto again = client.Get(("/v1/pair/session?code=" + code).c_str());
  REQUIRE(again);
  REQUIRE(again->status == 400);

  // createPairUrl helper also works.
  const std::string direct = bridge.createPairUrl();
  REQUIRE(direct.find("/v1/pair-ui?code=") != std::string::npos);

  auto help = client.Get("/v1/install-help");
  REQUIRE(help);
  REQUIRE(help->status == 200);
  REQUIRE(help->body.find("Connect now") != std::string::npos);

  bridge.stop();
}

TEST_CASE("HttpBrowserBridge pair rejects invalid and missing codes", "[bridge][pair]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "pair-tok";
  const int port = pickPort();
  settings.bridge_port = port;
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port, nullptr, nullptr, &focus);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);

  auto bad = client.Get("/v1/pair/session?code=deadbeef");
  REQUIRE(bad);
  REQUIRE(bad->status == 400);

  auto missing = client.Get("/v1/pair/session");
  REQUIRE(missing);
  REQUIRE(missing->status == 400);

  auto ui_bad = client.Get("/v1/pair-ui?code=notavalidlivecode0123456789ab");
  REQUIRE(ui_bad);
  REQUIRE(ui_bad->status == 400);

  auto ui_empty = client.Get("/v1/pair-ui");
  REQUIRE(ui_empty);
  REQUIRE(ui_empty->status == 400);

  // Two distinct pair codes from start / createPairUrl.
  auto a = client.Post("/v1/pair/start", "", "application/json");
  auto b_url = bridge.createPairUrl();
  REQUIRE(a);
  REQUIRE(a->status == 200);
  const auto ja = nlohmann::json::parse(a->body);
  const std::string code_a = ja["code"].get<std::string>();
  const auto pos = b_url.find("code=");
  REQUIRE(pos != std::string::npos);
  const std::string code_b = b_url.substr(pos + 5);
  REQUIRE(code_a != code_b);
  REQUIRE(code_a.size() == 32);
  REQUIRE(code_b.size() == 32);

  bridge.stop();
}

TEST_CASE("HttpBrowserBridge status includes camera_monitoring from provider", "[bridge]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "cam-tok";
  const int port = pickPort();
  settings.bridge_port = port;
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port, nullptr, nullptr, &focus);
  bool cam_flag = true;
  bridge.setCameraStatusProvider([&]() { return cam_flag; });
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);
  httplib::Headers headers{{"Authorization", "Bearer cam-tok"}};

  auto st = client.Get("/v1/status", headers);
  REQUIRE(st);
  REQUIRE(st->status == 200);
  auto j = nlohmann::json::parse(st->body);
  REQUIRE(j.contains("camera_monitoring"));
  REQUIRE(j["camera_monitoring"] == true);

  cam_flag = false;
  auto st2 = client.Get("/v1/status", headers);
  REQUIRE(st2);
  REQUIRE(nlohmann::json::parse(st2->body)["camera_monitoring"] == false);

  bridge.stop();
}

TEST_CASE("HttpBrowserBridge focus off clears browser sticky alarm path", "[bridge][focus]") {
  ScopedDataRoot scope;
  Storage db(scope.path() / "t.db");
  db.open();
  FakeClock clock;
  Settings settings = Settings::defaults();
  settings.bridge_token = "alarm-tok";
  settings.blocklist = {"instagram.com"};
  const int port = pickPort();
  settings.bridge_port = port;
  FocusSessionManager focus(db, settings, [&] { return clock(); });
  BrowserMonitor mon(db, focus, settings);
  HttpBrowserBridge bridge(mon, settings.bridge_token, port, nullptr, nullptr, &focus);
  REQUIRE(bridge.start());

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(2, 0);
  httplib::Headers headers{{"Authorization", "Bearer alarm-tok"},
                           {"Content-Type", "application/json"}};

  REQUIRE(client.Post("/v1/focus", headers, "{\"on\":true}", "application/json"));
  auto url = client.Post(
      "/v1/url", headers,
      R"({"url":"https://www.instagram.com/x","tabId":"9","event":"activated","browser":"chrome"})",
      "application/json");
  REQUIRE(url);
  REQUIRE(url->body.find("\"alarm_active\":true") != std::string::npos);

  auto off = client.Post("/v1/focus", headers, "{\"on\":false}", "application/json");
  REQUIRE(off);
  REQUIRE(off->body.find("\"focus_on\":false") != std::string::npos);
  REQUIRE_FALSE(focus.isFocusOn());
  // Social sticky should be cleared when focus turns off via bridge.
  REQUIRE_FALSE(mon.status().alarm_active);

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
