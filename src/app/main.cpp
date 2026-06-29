#include "bridge/HttpBrowserBridge.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop = true; }

void printUsage(const char* argv0) {
  std::cerr
      << "focusGaze CLI (Phase 2)\n"
      << "Usage:\n"
      << "  " << argv0 << " status\n"
      << "  " << argv0 << " on | off | toggle\n"
      << "  " << argv0 << " settings-show\n"
      << "  " << argv0 << " reconcile\n"
      << "  " << argv0 << " serve          # run HTTP bridge (blocks)\n"
      << "\nNotes:\n"
      << "  Alarm state lives in the `serve` process. `status` queries the live bridge\n"
      << "  when it is running (same FOCUSGAZE_DATA_DIR / settings token).\n"
      << "\nEnvironment:\n"
      << "  FOCUSGAZE_DATA_DIR  Override data root (settings + SQLite)\n";
}

struct AppContext {
  focusgaze::Settings settings;
  focusgaze::Storage storage;
  focusgaze::FocusSessionManager focus;
  focusgaze::BrowserMonitor monitor;

  static focusgaze::Storage openStorage() {
    focusgaze::Storage db(focusgaze::PlatformPaths::databasePath());
    db.open();
    return db;
  }

  AppContext()
      : settings(focusgaze::loadOrCreateSettings()),
        storage(openStorage()),
        focus(storage, settings),
        monitor(storage, focus, settings) {}
};

struct LiveStatus {
  bool focus_on{false};
  bool alarm_active{false};
  std::vector<std::string> alarm_reasons;
  std::size_t blocked_tab_count{0};
  std::optional<std::int64_t> session_id;
  std::string last_url;
  std::string last_domain;
  std::string last_category;
  bool from_bridge{false};
};

/// Prefer live `serve` process status over this process's empty in-memory monitor.
std::optional<LiveStatus> fetchLiveBridgeStatus(const focusgaze::Settings& settings) {
  if (settings.bridge_token.empty() || settings.bridge_port <= 0) {
    return std::nullopt;
  }
  httplib::Client client("127.0.0.1", settings.bridge_port);
  client.set_connection_timeout(0, 200000); // 200ms
  client.set_read_timeout(1, 0);
  httplib::Headers headers{{"Authorization", "Bearer " + settings.bridge_token}};
  auto res = client.Get("/v1/status", headers);
  if (!res || res->status != 200) {
    return std::nullopt;
  }
  try {
    const auto j = nlohmann::json::parse(res->body);
    LiveStatus st;
    st.from_bridge = true;
    st.focus_on = j.value("focus_on", false);
    st.alarm_active = j.value("alarm_active", false);
    st.blocked_tab_count = j.value("blocked_tab_count", 0);
    st.last_url = j.value("last_url", "");
    st.last_domain = j.value("last_domain", "");
    st.last_category = j.value("last_category", "");
    if (j.contains("session_id") && !j["session_id"].is_null()) {
      st.session_id = j["session_id"].get<std::int64_t>();
    }
    if (j.contains("alarm_reasons") && j["alarm_reasons"].is_array()) {
      for (const auto& r : j["alarm_reasons"]) {
        if (r.is_string()) {
          st.alarm_reasons.push_back(r.get<std::string>());
        }
      }
    }
    return st;
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 2;
  }
  const std::string cmd = argv[1];

  if (!focusgaze::PlatformPaths::ensureDataLayout()) {
    std::cerr << "Failed to create data directory: "
              << focusgaze::PlatformPaths::dataRoot() << "\n";
    return 1;
  }

  try {
    AppContext app;

    if (cmd == "reconcile") {
      app.focus.reconcileOnLaunch();
      std::cout << "reconciled focus=" << (app.focus.isFocusOn() ? "on" : "off") << "\n";
      return 0;
    }

    if (cmd == "status") {
      LiveStatus st;
      if (auto live = fetchLiveBridgeStatus(app.settings)) {
        st = *live;
      } else {
        const auto local = app.monitor.status();
        st.focus_on = local.focus_on;
        st.alarm_active = local.alarm_active;
        st.alarm_reasons = local.alarm_reasons;
        st.blocked_tab_count = local.blocked_tab_count;
        st.session_id = local.session_id;
        st.last_url = local.last_url;
        st.last_domain = local.last_domain;
        st.last_category = local.last_category;
        st.from_bridge = false;
      }

      std::cout << "data_root=" << focusgaze::PlatformPaths::dataRoot() << "\n"
                << "db=" << focusgaze::PlatformPaths::databasePath() << "\n"
                << "settings=" << focusgaze::PlatformPaths::settingsPath() << "\n"
                << "blocklist_file=" << focusgaze::PlatformPaths::blocklistPath() << "\n"
                << "bridge_port=" << app.settings.bridge_port << "\n"
                << "bridge_token=" << app.settings.bridge_token << "\n"
                << "status_source=" << (st.from_bridge ? "live_bridge" : "local_process") << "\n"
                << "focus=" << (st.focus_on ? "on" : "off") << "\n"
                << "alarm=" << (st.alarm_active ? "on" : "off") << "\n"
                << "blocked_tabs=" << st.blocked_tab_count << "\n";
      if (!st.from_bridge) {
        std::cout << "note=start `focusgaze serve` for live alarm status from extension events\n";
      }
      if (!st.alarm_reasons.empty()) {
        std::cout << "alarm_reasons=";
        for (std::size_t i = 0; i < st.alarm_reasons.size(); ++i) {
          if (i) std::cout << ",";
          std::cout << st.alarm_reasons[i];
        }
        std::cout << "\n";
      }
      if (st.session_id) {
        std::cout << "session_id=" << *st.session_id << "\n";
      }
      if (!st.last_url.empty()) {
        std::cout << "last_url=" << st.last_url << "\n"
                  << "last_domain=" << st.last_domain << "\n"
                  << "last_category=" << st.last_category << "\n";
      }
      const auto sessions = app.storage.listSessions(5);
      std::cout << "recent_sessions=" << sessions.size() << "\n";
      for (const auto& s : sessions) {
        std::cout << "  id=" << s.id << " start=" << s.started_at << " end="
                  << (s.ended_at ? std::to_string(*s.ended_at) : std::string("null"))
                  << "\n";
      }
      std::cout << "blocklist_size=" << app.settings.blocklist.size() << "\n";
      return 0;
    }

    if (cmd == "on") {
      if (!app.focus.turnOn()) {
        std::cout << "already on\n";
        return 0;
      }
      std::cout << "focus on session_id=" << app.focus.activeSession()->id << "\n";
      return 0;
    }

    if (cmd == "off") {
      if (!app.focus.turnOff()) {
        std::cout << "already off\n";
        return 0;
      }
      app.monitor.onFocusTurnedOff();
      std::cout << "focus off\n";
      return 0;
    }

    if (cmd == "toggle") {
      const bool on = app.focus.toggle();
      if (!on) {
        app.monitor.onFocusTurnedOff();
      }
      std::cout << "focus " << (on ? "on" : "off") << "\n";
      return 0;
    }

    if (cmd == "settings-show") {
      std::cout << app.settings.toJsonString(2) << "\n";
      return 0;
    }

    if (cmd == "serve") {
      std::signal(SIGINT, onSignal);
      std::signal(SIGTERM, onSignal);

      // Refresh focus from DB in case `on` was run in another process already.
      app.focus.syncFromStorage();

      focusgaze::HttpBrowserBridge bridge(app.monitor, app.settings.bridge_token,
                                          app.settings.bridge_port);
      if (!bridge.start()) {
        std::cerr << "Failed to start bridge on 127.0.0.1:" << app.settings.bridge_port
                  << " (check token/port)\n";
        return 1;
      }
      const bool focus_on =
          app.focus.isFocusOn() || app.storage.getActiveSession().has_value();
      std::cout << "focusGaze bridge listening on http://127.0.0.1:" << app.settings.bridge_port
                << "\n"
                << "token=" << app.settings.bridge_token << "\n"
                << "focus=" << (focus_on ? "on" : "off") << "\n"
                << "blocklist_file=" << focusgaze::PlatformPaths::blocklistPath() << "\n"
                << "blocklist_size=" << app.settings.blocklist.size() << "\n"
                << "logging=on (set FOCUSGAZE_QUIET=1 to silence event logs)\n"
                << "POST /v1/url  GET /v1/status  GET /v1/health\n"
                << "Waiting for extension events...\n"
                << "Ctrl+C to stop\n"
                << std::flush;
      if (!focus_on) {
        std::cout << "[focusGaze] warning: focus is OFF — run: ./build/focusgaze on\n"
                  << std::flush;
      }
      while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      bridge.stop();
      std::cout << "stopped\n";
      return 0;
    }

    printUsage(argv[0]);
    return 2;
  } catch (const focusgaze::StorageError& e) {
    std::cerr << "Database error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
