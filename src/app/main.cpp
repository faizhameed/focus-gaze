#include "bridge/HttpBrowserBridge.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
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
      << "\nEnvironment:\n"
      << "  FOCUSGAZE_DATA_DIR  Override data root (settings + SQLite)\n";
}

struct AppContext {
  focusgaze::Settings settings;
  focusgaze::Storage storage;
  focusgaze::FocusSessionManager focus;
  focusgaze::BrowserMonitor monitor;

  AppContext()
      : settings(focusgaze::loadOrCreateSettings()),
        storage(focusgaze::PlatformPaths::databasePath()),
        focus(storage, settings),
        monitor(storage, focus, settings) {
    storage.open();
  }
};

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
      const auto st = app.monitor.status();
      std::cout << "data_root=" << focusgaze::PlatformPaths::dataRoot() << "\n"
                << "db=" << focusgaze::PlatformPaths::databasePath() << "\n"
                << "settings=" << focusgaze::PlatformPaths::settingsPath() << "\n"
                << "bridge_port=" << app.settings.bridge_port << "\n"
                << "bridge_token=" << app.settings.bridge_token << "\n"
                << "focus=" << (st.focus_on ? "on" : "off") << "\n"
                << "alarm=" << (st.alarm_active ? "on" : "off") << "\n"
                << "blocked_tabs=" << st.blocked_tab_count << "\n";
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

      focusgaze::HttpBrowserBridge bridge(app.monitor, app.settings.bridge_token,
                                          app.settings.bridge_port);
      if (!bridge.start()) {
        std::cerr << "Failed to start bridge on 127.0.0.1:" << app.settings.bridge_port
                  << " (check token/port)\n";
        return 1;
      }
      std::cout << "focusGaze bridge listening on http://127.0.0.1:" << app.settings.bridge_port
                << "\n"
                << "token=" << app.settings.bridge_token << "\n"
                << "focus=" << (app.focus.isFocusOn() ? "on" : "off") << "\n"
                << "POST /v1/url  GET /v1/status  GET /v1/health\n"
                << "Ctrl+C to stop\n";
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
