#include "bridge/HttpBrowserBridge.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <utility>

namespace focusgaze {
namespace {

using nlohmann::json;

bool authorized(const httplib::Request& req, const std::string& token) {
  if (token.empty()) {
    return false;
  }
  const auto auth = req.get_header_value("Authorization");
  const std::string prefix = "Bearer ";
  if (auth.size() > prefix.size() && auth.compare(0, prefix.size(), prefix) == 0) {
    return auth.substr(prefix.size()) == token;
  }
  // Also accept X-FocusGaze-Token for simpler extension code
  const auto alt = req.get_header_value("X-FocusGaze-Token");
  return alt == token;
}

BrowserUrlEvent parseEvent(const json& body) {
  BrowserUrlEvent ev;
  if (body.contains("url") && body["url"].is_string()) {
    ev.url = body["url"].get<std::string>();
  }
  if (body.contains("title") && body["title"].is_string()) {
    ev.title = body["title"].get<std::string>();
  }
  if (body.contains("tabId")) {
    if (body["tabId"].is_string()) {
      ev.tab_id = body["tabId"].get<std::string>();
    } else if (body["tabId"].is_number_integer()) {
      ev.tab_id = std::to_string(body["tabId"].get<std::int64_t>());
    }
  }
  if (body.contains("browser") && body["browser"].is_string()) {
    ev.browser = body["browser"].get<std::string>();
  }
  std::string event_str = "activated";
  if (body.contains("event") && body["event"].is_string()) {
    event_str = body["event"].get<std::string>();
  }
  if (auto t = urlEventTypeFromString(event_str)) {
    ev.event = *t;
  }
  if (body.contains("ts") && body["ts"].is_number_integer()) {
    ev.ts = body["ts"].get<std::int64_t>();
  }
  return ev;
}

json statusToJson(const MonitorStatus& st) {
  json j;
  j["focus_on"] = st.focus_on;
  j["alarm_active"] = st.alarm_active;
  j["alarm_reasons"] = st.alarm_reasons;
  j["blocked_tab_count"] = st.blocked_tab_count;
  if (st.session_id) {
    j["session_id"] = *st.session_id;
  } else {
    j["session_id"] = nullptr;
  }
  j["last_url"] = st.last_url;
  j["last_domain"] = st.last_domain;
  j["last_category"] = st.last_category;
  return j;
}

} // namespace

HttpBrowserBridge::HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port)
    : monitor_(monitor), token_(std::move(token)), port_(port) {}

HttpBrowserBridge::~HttpBrowserBridge() { stop(); }

bool HttpBrowserBridge::start() {
  if (running_.load()) {
    return true;
  }
  if (token_.empty() || port_ <= 0 || port_ > 65535) {
    return false;
  }
  stop_requested_ = false;
  auto server = std::make_shared<httplib::Server>();
  server_holder_ = server;

  server->Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"ok":true,"service":"focusGaze"})", "application/json");
  });

  server->Get("/v1/status", [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    res.set_content(statusToJson(monitor_.status()).dump(), "application/json");
  });

  auto handle_url = [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    json body;
    try {
      body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (const json::parse_error&) {
      res.status = 400;
      res.set_content(R"({"error":"invalid_json"})", "application/json");
      return;
    }
    const auto ev = parseEvent(body);
    const bool ok = monitor_.handleEvent(ev);
    json out = statusToJson(monitor_.status());
    out["accepted"] = ok;
    if (monitor_.status().alarm_active) {
      // Log sticky alarm for CLI / operators (no GUI overlay yet)
      std::cerr << "[focusGaze] ALARM active reasons=";
      for (const auto& r : monitor_.status().alarm_reasons) {
        std::cerr << r << " ";
      }
      std::cerr << "blocked_tabs=" << monitor_.status().blocked_tab_count << "\n";
    }
    res.set_content(out.dump(), "application/json");
  };

  server->Post("/v1/url", handle_url);
  server->Post("/v1/events/url", handle_url);

  thread_ = std::thread([this, server]() {
    running_ = true;
    // listen blocks until stop
    server->listen(kBindHost, port_);
    running_ = false;
  });

  // Wait briefly for listen to bind
  for (int i = 0; i < 50; ++i) {
    if (server->is_running()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  // Might still be starting; check port with a health client
  httplib::Client client(kBindHost, port_);
  client.set_connection_timeout(0, 200000);
  for (int i = 0; i < 25; ++i) {
    if (auto r = client.Get("/v1/health")) {
      if (r->status == 200) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return running_.load() || server->is_running();
}

void HttpBrowserBridge::stop() {
  stop_requested_ = true;
  if (server_holder_) {
    auto* server = static_cast<httplib::Server*>(server_holder_.get());
    server->stop();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  server_holder_.reset();
  running_ = false;
}

} // namespace focusgaze
