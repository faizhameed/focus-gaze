#include "bridge/HttpBrowserBridge.hpp"

#include "core/PhoneMonitor.hpp"
#include "vision/VisionLoop.hpp"

#include "core/UrlClassifier.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <utility>

namespace focusgaze {
namespace {

using nlohmann::json;

bool quietLogs() {
  const char* q = std::getenv("FOCUSGAZE_QUIET");
  return q != nullptr && q[0] != '\0' && q[0] != '0';
}

void logLine(const std::string& msg) {
  if (quietLogs()) {
    return;
  }
  // Use cout + flush so lines show up immediately in the serve terminal.
  std::cout << "[focusGaze] " << msg << std::endl;
}

bool authorized(const httplib::Request& req, const std::string& token) {
  if (token.empty()) {
    return false;
  }
  const auto auth = req.get_header_value("Authorization");
  const std::string prefix = "Bearer ";
  if (auth.size() > prefix.size() && auth.compare(0, prefix.size(), prefix) == 0) {
    return auth.substr(prefix.size()) == token;
  }
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

json statusToJson(const MonitorStatus& st, const PhoneStatus* phone = nullptr) {
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
  if (phone) {
    j["phone_visible"] = phone->phone_visible;
    j["phone_cumulative_seconds"] = phone->cumulative_visible_seconds;
    j["phone_threshold_seconds"] = phone->threshold_seconds;
    j["phone_window_seconds"] = phone->window_seconds;
    j["phone_alarm"] = phone->phone_alarm;
  }
  return j;
}

} // namespace

HttpBrowserBridge::HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port,
                                    PhoneMonitor* phone, VisionLoop* vision)
    : monitor_(monitor),
      phone_(phone),
      vision_(vision),
      token_(std::move(token)),
      port_(port) {}

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
      logLine("GET /v1/status unauthorized (check extension token)");
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    PhoneStatus pst{};
    PhoneStatus* pptr = nullptr;
    if (phone_) {
      const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      pst = phone_->status(now);
      pptr = &pst;
    }
    res.set_content(statusToJson(monitor_.status(), pptr).dump(), "application/json");
  });

  auto handle_url = [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      logLine("POST /v1/url unauthorized (check extension token matches settings)");
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    json body;
    try {
      body = json::parse(req.body.empty() ? "{}" : req.body);
    } catch (const json::parse_error&) {
      logLine("POST /v1/url invalid JSON");
      res.status = 400;
      res.set_content(R"({"error":"invalid_json"})", "application/json");
      return;
    }
    const bool was_active = monitor_.status().alarm_active;
    const auto ev = parseEvent(body);
    const bool ok = monitor_.handleEvent(ev);
    const auto st = monitor_.status();
    PhoneStatus pst{};
    PhoneStatus* pptr = nullptr;
    if (phone_) {
      pst = phone_->status(st.focus_on ? (
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count()) : 0);
      // always use real now
      const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      pst = phone_->status(now);
      pptr = &pst;
    }
    json out = statusToJson(st, pptr);
    out["accepted"] = ok;

    const char* ev_name = toString(ev.event);
    std::string domain = st.last_domain;
    if (domain.empty() && !ev.url.empty()) {
      domain = UrlClassifier::extractDomain(ev.url);
    }

    // Always log each event so the serve terminal stays lively while debugging.
    {
      std::ostringstream line;
      line << "event=" << ev_name << " tab=" << (ev.tab_id.empty() ? "-" : ev.tab_id)
           << " domain=" << (domain.empty() ? "-" : domain)
           << " category=" << (st.last_category.empty() ? "-" : st.last_category)
           << " focus=" << (st.focus_on ? "on" : "off")
           << " alarm=" << (st.alarm_active ? "on" : "off")
           << " blocked_tabs=" << st.blocked_tab_count;
      if (!st.focus_on) {
        line << " (focus off — not enforcing)";
      }
      logLine(line.str());
    }

    if (st.alarm_active && !was_active) {
      logLine("ALARM active blocked_tabs=" + std::to_string(st.blocked_tab_count) +
              (domain.empty() ? "" : " domain=" + domain));
    } else if (!st.alarm_active && was_active) {
      logLine("ALARM cleared (no blocked tabs remain)");
    }

    res.set_content(out.dump(), "application/json");
  };

  server->Post("/v1/url", handle_url);
  server->Post("/v1/events/url", handle_url);

  server->Post("/v1/phone", [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      logLine("POST /v1/phone unauthorized");
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    if (!phone_) {
      res.status = 503;
      res.set_content(R"({"error":"phone_monitor_unavailable"})", "application/json");
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
    bool visible = false;
    if (body.contains("visible") && body["visible"].is_boolean()) {
      visible = body["visible"].get<bool>();
    }
    EpochSeconds ts = 0;
    if (body.contains("ts") && body["ts"].is_number_integer()) {
      ts = body["ts"].get<std::int64_t>();
    } else {
      ts = std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count();
    }
    if (vision_) {
      vision_->setInjectedVisibility(visible);
    }
    phone_->sample(ts, visible);
    const auto pst = phone_->status(ts);
    const auto bst = monitor_.status();
    logLine(std::string("phone visible=") + (visible ? "true" : "false") +
            " cumulative_s=" + std::to_string(pst.cumulative_visible_seconds) +
            " phone_alarm=" + (pst.phone_alarm ? "on" : "off") +
            " alarm=" + (bst.alarm_active ? "on" : "off"));
    if (pst.phone_alarm) {
      logLine("ALARM active reason=phone_window cumulative_s=" +
              std::to_string(pst.cumulative_visible_seconds));
    }
    json out = statusToJson(bst, &pst);
    out["accepted"] = true;
    res.set_content(out.dump(), "application/json");
  });

  thread_ = std::thread([this, server]() {
    running_ = true;
    server->listen(kBindHost, port_);
    running_ = false;
  });

  for (int i = 0; i < 50; ++i) {
    if (server->is_running()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
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
