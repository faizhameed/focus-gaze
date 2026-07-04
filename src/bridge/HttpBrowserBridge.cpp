#include "bridge/HttpBrowserBridge.hpp"

#include "core/PhoneMonitor.hpp"
#include "vision/VisionLoop.hpp"

#include "core/UrlClassifier.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
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

json statusToJson(const MonitorStatus& st, const PhoneStatus* phone = nullptr,
                  const bool* camera_monitoring = nullptr) {
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
  if (camera_monitoring) {
    j["camera_monitoring"] = *camera_monitoring;
  }
  return j;
}

/// HTML pair page: fetches one-time session, then sendMessage to the extension.
std::string buildPairUiHtml(const std::string& code, int port, const std::string& extension_id) {
  // Escape for embedding in a JS string literal (code/id are hex / a-p only).
  std::ostringstream oss;
  oss << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Connect focusGaze</title>
  <style>
    :root { color-scheme: dark; font-family: Inter, system-ui, sans-serif; }
    body { margin: 0; min-height: 100vh; display: grid; place-items: center;
           background: #0B0F14; color: #F1F5F9; }
    .card { width: min(420px, 92vw); background: #141A22; border: 1px solid #243041;
            border-radius: 14px; padding: 28px 24px; box-shadow: 0 16px 40px rgba(0,0,0,.35); }
    h1 { margin: 0 0 8px; font-size: 1.35rem; color: #2DD4BF; }
    p { margin: 0 0 12px; color: #94A3B8; line-height: 1.45; font-size: .95rem; }
    #status { margin-top: 16px; padding: 12px 14px; border-radius: 10px; font-weight: 600;
              background: #1a222d; border: 1px solid #243041; }
    .ok { color: #34D399; border-color: rgba(52,211,153,.35) !important; }
    .err { color: #F87171; border-color: rgba(248,113,113,.35) !important; }
    .hint { font-size: .8rem; color: #64748b; margin-top: 14px; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Connect focusGaze</h1>
    <p>Linking this Chrome profile to the desktop app. Keep this tab open for a moment…</p>
    <div id="status">Connecting…</div>
    <p class="hint">Open this page in <strong>Google Chrome</strong> with the focusGaze Bridge extension installed. Other browsers cannot complete pairing.</p>
  </div>
  <script>
(function () {
  const CODE = )HTML"
     << json(code).dump()
     << R"HTML(;
  const PORT = )HTML"
     << port
     << R"HTML(;
  const EXT_ID = )HTML"
     << json(extension_id).dump()
     << R"HTML(;
  const statusEl = document.getElementById('status');
  function setStatus(text, cls) {
    statusEl.textContent = text;
    statusEl.className = cls || '';
  }
  async function run() {
    if (typeof chrome === 'undefined' || !chrome.runtime || !chrome.runtime.sendMessage) {
      setStatus('Open this link in Google Chrome (extension APIs unavailable here).', 'err');
      return;
    }
    let session;
    try {
      const res = await fetch('http://127.0.0.1:' + PORT + '/v1/pair/session?code=' + encodeURIComponent(CODE), {
        method: 'GET',
        cache: 'no-store',
      });
      session = await res.json();
      if (!res.ok || !session.ok) {
        setStatus(session.message || session.error || 'Pairing code invalid or expired. Use Connect browser in the app again.', 'err');
        return;
      }
    } catch (e) {
      setStatus('Could not reach the focusGaze app. Is it running?', 'err');
      return;
    }
    try {
      chrome.runtime.sendMessage(
        EXT_ID,
        { type: 'focusgaze.pair', token: session.token, port: session.port || PORT },
        function (response) {
          const err = chrome.runtime.lastError;
          if (err) {
            setStatus(
              'Extension not found. Install focusGaze Bridge, then click Connect browser again. (' + err.message + ')',
              'err'
            );
            return;
          }
          if (response && response.ok) {
            setStatus('Connected! You can close this tab and return to focusGaze.', 'ok');
          } else {
            setStatus((response && response.error) || 'Extension rejected pairing.', 'err');
          }
        }
      );
    } catch (e) {
      setStatus('Failed to message the extension: ' + e, 'err');
    }
  }
  run();
})();
  </script>
</body>
</html>
)HTML";
  return oss.str();
}

std::string randomPairCode() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 15);
  std::string code;
  code.reserve(32);
  for (int i = 0; i < 32; ++i) code.push_back(kHex[dist(gen)]);
  return code;
}

} // namespace

std::string HttpBrowserBridge::chromeExtensionId() {
  if (const char* env = std::getenv("FOCUSGAZE_CHROME_EXTENSION_ID")) {
    if (env[0] != '\0') return std::string(env);
  }
  return kDefaultChromeExtensionId;
}

std::string HttpBrowserBridge::mintPairCodeLocked() {
  // Drop expired / consumed sessions.
  const auto now = std::chrono::steady_clock::now();
  pair_sessions_.erase(
      std::remove_if(pair_sessions_.begin(), pair_sessions_.end(),
                     [&](const PairSession& s) {
                       return s.consumed || s.expires_at <= now;
                     }),
      pair_sessions_.end());
  // Cap outstanding sessions.
  while (pair_sessions_.size() >= 8) {
    pair_sessions_.erase(pair_sessions_.begin());
  }
  PairSession session;
  session.code = randomPairCode();
  session.expires_at = now + std::chrono::seconds(120);
  session.consumed = false;
  pair_sessions_.push_back(session);
  return session.code;
}

bool HttpBrowserBridge::consumePairCode(const std::string& code) {
  if (code.empty()) return false;
  std::lock_guard lock(pair_mu_);
  const auto now = std::chrono::steady_clock::now();
  for (auto& s : pair_sessions_) {
    if (s.code != code) continue;
    if (s.consumed || s.expires_at <= now) return false;
    s.consumed = true;
    return true;
  }
  return false;
}

std::string HttpBrowserBridge::createPairUrl() {
  if (token_.empty() || port_ <= 0) return {};
  std::string code;
  {
    std::lock_guard lock(pair_mu_);
    code = mintPairCodeLocked();
  }
  std::ostringstream url;
  url << "http://" << kBindHost << ":" << port_ << "/v1/pair-ui?code=" << code;
  return url.str();
}

HttpBrowserBridge::HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port,
                                    PhoneMonitor* phone, VisionLoop* vision,
                                    FocusSessionManager* focus)
    : monitor_(monitor),
      phone_(phone),
      vision_(vision),
      focus_(focus),
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
    bool cam = false;
    const bool* cam_ptr = nullptr;
    if (camera_status_provider_) {
      cam = camera_status_provider_();
      cam_ptr = &cam;
    }
    res.set_content(statusToJson(monitor_.status(), pptr, cam_ptr).dump(), "application/json");
  });

  // Toggle Focus Mode from the extension popup (authenticated).
  server->Post("/v1/focus", [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    if (!focus_) {
      res.status = 503;
      res.set_content(R"({"error":"focus_unavailable"})", "application/json");
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
    if (!body.contains("on") || !body["on"].is_boolean()) {
      res.status = 400;
      res.set_content(R"({"error":"missing_on_bool"})", "application/json");
      return;
    }
    const bool want_on = body["on"].get<bool>();
    const bool was_on = focus_->isFocusOn();
    if (want_on && !was_on) {
      focus_->turnOn();
      logLine("focus turned ON via /v1/focus");
    } else if (!want_on && was_on) {
      focus_->turnOff();
      monitor_.onFocusTurnedOff();
      if (phone_) phone_->onFocusTurnedOff();
      logLine("focus turned OFF via /v1/focus");
    }
    json out;
    out["focus_on"] = focus_->isFocusOn();
    out["accepted"] = true;
    res.set_content(out.dump(), "application/json");
  });

  // One-click install of the Chrome extension into every profile (authenticated).
  server->Post("/v1/install-extension", [this](const httplib::Request& req, httplib::Response& res) {
    if (!authorized(req, token_)) {
      res.status = 401;
      res.set_content(R"({"error":"unauthorized"})", "application/json");
      return;
    }
    if (!install_handler_) {
      res.status = 503;
      res.set_content(
          R"({"ok":false,"error":"install_unavailable","message":"Installer not configured in this process"})",
          "application/json");
      return;
    }
    bool relaunch = true;
    if (!req.body.empty()) {
      try {
        const auto body = json::parse(req.body);
        if (body.contains("relaunch_chrome") && body["relaunch_chrome"].is_boolean()) {
          relaunch = body["relaunch_chrome"].get<bool>();
        }
      } catch (const json::parse_error&) {
        // keep default relaunch=true
      }
    }
    logLine("POST /v1/install-extension relaunch=" + std::string(relaunch ? "true" : "false"));
    try {
      const std::string payload = install_handler_(relaunch);
      res.set_content(payload.empty() ? R"({"ok":false,"error":"empty_installer_result"})" : payload,
                      "application/json");
    } catch (const std::exception& ex) {
      json err;
      err["ok"] = false;
      err["error"] = "install_failed";
      err["message"] = ex.what();
      res.status = 500;
      res.set_content(err.dump(), "application/json");
    }
  });

  // --- Automatic extension pairing (loopback only; server binds 127.0.0.1) ---

  /// Mint a one-time pair code + URL (used by the desktop app / diagnostics).
  server->Post("/v1/pair/start", [this](const httplib::Request&, httplib::Response& res) {
    if (token_.empty()) {
      res.status = 503;
      res.set_content(R"({"ok":false,"error":"no_token"})", "application/json");
      return;
    }
    const std::string url = createPairUrl();
    if (url.empty()) {
      res.status = 500;
      res.set_content(R"({"ok":false,"error":"pair_failed"})", "application/json");
      return;
    }
    // Extract code from query for JSON convenience.
    const auto pos = url.find("code=");
    const std::string code = pos == std::string::npos ? "" : url.substr(pos + 5);
    json out;
    out["ok"] = true;
    out["code"] = code;
    out["pair_url"] = url;
    out["expires_in"] = 120;
    out["extension_id"] = chromeExtensionId();
    out["port"] = port_;
    logLine("pair session started (one-time code, 120s)");
    res.set_content(out.dump(), "application/json");
  });

  /// Consume one-time code and return bridge credentials exactly once.
  server->Get("/v1/pair/session", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string code = req.get_param_value("code");
    if (!consumePairCode(code)) {
      res.status = 400;
      res.set_content(
          R"({"ok":false,"error":"invalid_or_expired_code","message":"Pairing code invalid or expired. Click Connect browser in focusGaze again."})",
          "application/json");
      return;
    }
    json out;
    out["ok"] = true;
    out["token"] = token_;
    out["port"] = port_;
    out["extension_id"] = chromeExtensionId();
    logLine("pair session consumed — credentials issued once");
    res.set_content(out.dump(), "application/json");
  });

  /// Onboarding help: how to install the extension, then pair.
  server->Get("/v1/install-help", [this](const httplib::Request&, httplib::Response& res) {
    const std::string pair = createPairUrl();
    std::ostringstream html;
    html << R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"/><title>Install focusGaze Bridge</title>
<style>
 body{font-family:system-ui,sans-serif;background:#0B0F14;color:#F1F5F9;max-width:560px;margin:40px auto;padding:0 16px;line-height:1.5}
 h1{color:#2DD4BF} a.btn{display:inline-block;margin:8px 8px 8px 0;padding:12px 16px;border-radius:8px;
  background:#2DD4BF;color:#04352F;font-weight:700;text-decoration:none}
 a.secondary{background:transparent;color:#CBD5E1;border:1px solid #243041}
 .card{background:#141A22;border:1px solid #243041;border-radius:12px;padding:16px;margin:16px 0}
 ol{padding-left:1.2rem} li{margin:8px 0;color:#94A3B8}
</style></head><body>
<h1>Install focusGaze Bridge</h1>
<div class="card">
<ol>
<li><strong>Install the extension</strong> in Google Chrome (Web Store in production, or Load unpacked for development).</li>
<li>Click <strong>Connect now</strong> below (or use the app tray → Connect browser).</li>
<li>Wait until the page says <strong>Connected</strong> — the token is applied automatically.</li>
</ol>
</div>
)HTML";
    if (!pair.empty()) {
      html << "<p><a class=\"btn\" href=\"" << pair << "\">Connect now (auto pair)</a></p>";
    }
    html << R"HTML(
<p style="color:#64748b;font-size:13px">Dev unpacked path:
<code>~/Library/Application Support/focusGaze/chrome-extension-unpacked</code>
or the repo <code>extension/chrome</code> folder. Production: set env
<code>FOCUSGAZE_EXTENSION_STORE_URL</code> to your Web Store listing.</p>
</body></html>
)HTML";
    res.set_content(html.str(), "text/html; charset=utf-8");
  });

  /// Browser-facing pair UI (open in Google Chrome).
  server->Get("/v1/pair-ui", [this](const httplib::Request& req, httplib::Response& res) {
    const std::string code = req.get_param_value("code");
    if (code.empty() || code.size() > 64) {
      res.status = 400;
      res.set_content("Missing or invalid pairing code.", "text/plain; charset=utf-8");
      return;
    }
    // Do not consume here — the page JS calls /v1/pair/session once.
    // Soft-check: reject obviously expired codes so the page shows a clear error early.
    bool known = false;
    {
      std::lock_guard lock(pair_mu_);
      const auto now = std::chrono::steady_clock::now();
      for (const auto& s : pair_sessions_) {
        if (s.code == code && !s.consumed && s.expires_at > now) {
          known = true;
          break;
        }
      }
    }
    if (!known) {
      res.status = 400;
      res.set_content(
          "<!DOCTYPE html><html><body style='background:#0B0F14;color:#F87171;font-family:system-ui;padding:2rem'>"
          "<h1>Pairing link expired</h1><p>Return to focusGaze and click <strong>Connect browser</strong> again."
          "</p></body></html>",
          "text/html; charset=utf-8");
      return;
    }
    res.set_content(buildPairUiHtml(code, port_, chromeExtensionId()), "text/html; charset=utf-8");
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
