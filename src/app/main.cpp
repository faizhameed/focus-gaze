#include "bridge/HttpBrowserBridge.hpp"
#include "core/AlarmPresenter.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PhoneMonitor.hpp"
#include "core/PlatformPaths.hpp"
#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "vision/CameraSource.hpp"
#include "vision/VisionLoop.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

void printUsage(const char* argv0) {
  std::cerr
      << "focusGaze CLI (Phase 2–4)\n"
      << "Usage:\n"
      << "  " << argv0 << " status | on | off | toggle | settings-show | reconcile\n"
      << "  " << argv0 << " serve\n"
      << "  " << argv0 << " stats              # last session productivity report\n"
      << "  " << argv0 << " stats-csv [file]   # export last session CSV\n"
      << "\nVision:\n"
      << "  FOCUSGAZE_FAKE_CAMERA=/path/to/video.mp4\n"
      << "  FOCUSGAZE_PHONE_VISIBLE=1   # force phone-visible without camera\n"
      << "  FOCUSGAZE_DATA_DIR=...\n"
      << "  FOCUSGAZE_QUIET=1\n";
}

focusgaze::EpochSeconds wallNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct AppContext {
  focusgaze::Settings settings;
  focusgaze::Storage storage;
  focusgaze::FocusSessionManager focus;
  focusgaze::BrowserMonitor browser;
  focusgaze::PhoneMonitor phone;

  static focusgaze::Storage openStorage() {
    focusgaze::Storage db(focusgaze::PlatformPaths::databasePath());
    db.open();
    return db;
  }

  AppContext()
      : settings(focusgaze::loadOrCreateSettings()),
        storage(openStorage()),
        focus(storage, settings),
        browser(storage, focus, settings),
        phone(storage, focus, browser.alarms(), settings) {}
};

struct LiveStatus {
  bool focus_on{false};
  bool alarm_active{false};
  std::vector<std::string> alarm_reasons;
  std::size_t blocked_tab_count{0};
  std::optional<std::int64_t> session_id;
  std::string last_url, last_domain, last_category;
  bool from_bridge{false};
  bool phone_visible{false};
  std::int64_t phone_cumulative{0};
  bool phone_alarm{false};
};

std::optional<LiveStatus> fetchLiveBridgeStatus(const focusgaze::Settings& settings) {
  if (settings.bridge_token.empty() || settings.bridge_port <= 0) return std::nullopt;
  httplib::Client client("127.0.0.1", settings.bridge_port);
  client.set_connection_timeout(0, 200000);
  client.set_read_timeout(1, 0);
  httplib::Headers headers{{"Authorization", "Bearer " + settings.bridge_token}};
  auto res = client.Get("/v1/status", headers);
  if (!res || res->status != 200) return std::nullopt;
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
    st.phone_visible = j.value("phone_visible", false);
    st.phone_cumulative = j.value("phone_cumulative_seconds", 0);
    st.phone_alarm = j.value("phone_alarm", false);
    if (j.contains("session_id") && !j["session_id"].is_null())
      st.session_id = j["session_id"].get<std::int64_t>();
    if (j.contains("alarm_reasons") && j["alarm_reasons"].is_array())
      for (const auto& r : j["alarm_reasons"])
        if (r.is_string()) st.alarm_reasons.push_back(r.get<std::string>());
    return st;
  } catch (...) { return std::nullopt; }
}

bool envPhoneAlwaysVisible() {
  const char* v = std::getenv("FOCUSGAZE_PHONE_VISIBLE");
  return v && v[0] && v[0] != '0';
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) { printUsage(argv[0]); return 2; }
  const std::string cmd = argv[1];

  if (!focusgaze::PlatformPaths::ensureDataLayout()) {
    std::cerr << "Failed to create data directory\n";
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
      if (auto live = fetchLiveBridgeStatus(app.settings)) st = *live;
      else {
        const auto local = app.browser.status();
        const auto ph = app.phone.status(wallNow());
        st.focus_on = local.focus_on;
        st.alarm_active = local.alarm_active;
        st.alarm_reasons = local.alarm_reasons;
        st.blocked_tab_count = local.blocked_tab_count;
        st.session_id = local.session_id;
        st.last_url = local.last_url;
        st.last_domain = local.last_domain;
        st.last_category = local.last_category;
        st.phone_visible = ph.phone_visible;
        st.phone_cumulative = ph.cumulative_visible_seconds;
        st.phone_alarm = ph.phone_alarm;
      }
      std::cout << "data_root=" << focusgaze::PlatformPaths::dataRoot() << "\n"
                << "focus=" << (st.focus_on ? "on" : "off") << "\n"
                << "alarm=" << (st.alarm_active ? "on" : "off") << "\n"
                << "blocked_tabs=" << st.blocked_tab_count << "\n"
                << "phone_visible=" << (st.phone_visible ? "yes" : "no") << "\n"
                << "phone_cumulative_s=" << st.phone_cumulative << "\n"
                << "phone_alarm=" << (st.phone_alarm ? "on" : "off") << "\n"
                << "status_source=" << (st.from_bridge ? "live_bridge" : "local_process") << "\n"
                << "bridge_token=" << app.settings.bridge_token << "\n"
                << "blocklist_file=" << focusgaze::PlatformPaths::blocklistPath() << "\n";
#if defined(FOCUSGAZE_HAS_OPENCV)
      std::cout << "opencv=on\n";
#else
      std::cout << "opencv=off\n";
#endif
      if (!st.alarm_reasons.empty()) {
        std::cout << "alarm_reasons=";
        for (size_t i = 0; i < st.alarm_reasons.size(); ++i) {
          if (i) std::cout << ",";
          std::cout << st.alarm_reasons[i];
        }
        std::cout << "\n";
      }
      return 0;
    }

    if (cmd == "on") {
      if (!app.focus.turnOn()) { std::cout << "already on\n"; return 0; }
      std::cout << "focus on session_id=" << app.focus.activeSession()->id << "\n";
      return 0;
    }
    if (cmd == "off") {
      std::optional<std::int64_t> ended_id;
      if (auto s = app.focus.activeSession()) ended_id = s->id;
      if (!app.focus.turnOff()) { std::cout << "already off\n"; return 0; }
      app.browser.onFocusTurnedOff();
      app.phone.onFocusTurnedOff();
      std::cout << "focus off\n";
      if (ended_id) {
        focusgaze::ProductivityStats stats(app.storage);
        const auto summary = stats.computeSession(*ended_id);
        std::cout << focusgaze::ProductivityStats::formatReport(summary);
      }
      return 0;
    }
    if (cmd == "toggle") {
      const bool on = app.focus.toggle();
      if (!on) {
        app.browser.onFocusTurnedOff();
        app.phone.onFocusTurnedOff();
      }
      std::cout << "focus " << (on ? "on" : "off") << "\n";
      return 0;
    }
    if (cmd == "settings-show") {
      std::cout << app.settings.toJsonString(2) << "\n";
      return 0;
    }

    if (cmd == "stats") {
      focusgaze::ProductivityStats stats(app.storage);
      auto s = stats.lastSessionSummary();
      if (!s) { std::cout << "no sessions\n"; return 0; }
      std::cout << focusgaze::ProductivityStats::formatReport(*s);
      return 0;
    }

    if (cmd == "stats-csv") {
      focusgaze::ProductivityStats stats(app.storage);
      auto s = stats.lastSessionSummary();
      if (!s) { std::cout << "no sessions\n"; return 0; }
      const std::string csv = focusgaze::ProductivityStats::toCsv(*s);
      if (argc >= 3) {
        std::ofstream out(argv[2]);
        out << csv;
        std::cout << "wrote " << argv[2] << "\n";
      } else {
        std::cout << csv;
      }
      return 0;
    }

    if (cmd == "serve") {
      std::signal(SIGINT, onSignal);
      std::signal(SIGTERM, onSignal);
      app.focus.syncFromStorage();

      // Camera / fake video / env override
      std::unique_ptr<focusgaze::CameraSource> camera;
      const std::string fake = focusgaze::CameraSource::resolveVideoPathFromEnv();
      bool camera_ok = false;
#if defined(FOCUSGAZE_HAS_OPENCV)
      camera = std::make_unique<focusgaze::CameraSource>(fake, 3);
      camera_ok = camera->isOpen();
#endif
      // Sticky last camera decision between throttled frames
      auto last_cam = std::make_shared<std::atomic<bool>>(false);
      auto visibility = [camera = camera.get(), last_cam]() -> bool {
        if (envPhoneAlwaysVisible()) return true;
        if (!camera || !camera->isOpen()) {
          last_cam->store(false);
          return false;
        }
        bool v = false;
        if (camera->pollPhoneVisible(v)) {
          last_cam->store(v);
          return v;
        }
        // Throttled frame: keep last debounced decision (clears once camera reports miss streak).
        return last_cam->load();
      };

      focusgaze::VisionLoop vision(app.phone, visibility, wallNow, 400);
      focusgaze::AlarmPresenter alarms_ui;
      alarms_ui.start();

      focusgaze::HttpBrowserBridge bridge(app.browser, app.settings.bridge_token,
                                          app.settings.bridge_port, &app.phone, &vision);
      if (!bridge.start()) {
        std::cerr << "Failed to start bridge\n";
        return 1;
      }
      vision.start();

      const bool focus_on =
          app.focus.isFocusOn() || app.storage.getActiveSession().has_value();
      std::cout << "focusGaze bridge on http://127.0.0.1:" << app.settings.bridge_port << "\n"
                << "token=" << app.settings.bridge_token << "\n"
                << "focus=" << (focus_on ? "on" : "off") << "\n"
                << "camera=" << (camera_ok ? (fake.empty() ? "webcam" : "fake_video") : "off")
                << "\n"
                << "alarm_overlay="
#if defined(FOCUSGAZE_HAS_OPENCV)
                << "opencv_window"
#else
                << "console"
#endif
                << "\n"
                << "POST /v1/url  POST /v1/phone  GET /v1/status\n"
                << "Ctrl+C to stop\n"
                << std::flush;

      // Poll alarms on the MAIN thread only (OpenCV/AppKit NSWindow requirement).
      bool last_phone_log = false;
      while (!g_stop.load()) {
        const auto reasons = app.browser.alarms().activeReasons();
        alarms_ui.setActiveReasons(reasons);
        alarms_ui.tick();  // must be main thread
        const auto ph = app.phone.status(wallNow());
        if (ph.phone_visible != last_phone_log) {
          std::cout << "[focusGaze] phone_visible=" << (ph.phone_visible ? "yes" : "no")
                    << " cumulative_s=" << ph.cumulative_visible_seconds
                    << " phone_alarm=" << (ph.phone_alarm ? "on" : "off") << std::endl;
          last_phone_log = ph.phone_visible;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      vision.stop();
      alarms_ui.stop();
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
