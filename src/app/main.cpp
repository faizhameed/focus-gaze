#include "core/FocusSession.hpp"
#include "core/PlatformPaths.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void printUsage(const char* argv0) {
  std::cerr
      << "focusGaze Phase 1 CLI\n"
      << "Usage:\n"
      << "  " << argv0 << " status\n"
      << "  " << argv0 << " on\n"
      << "  " << argv0 << " off\n"
      << "  " << argv0 << " toggle\n"
      << "  " << argv0 << " settings-show\n"
      << "  " << argv0 << " reconcile   # apply resume_focus_on_launch policy\n"
      << "\nEnvironment:\n"
      << "  FOCUSGAZE_DATA_DIR  Override data root (settings + SQLite)\n";
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

  focusgaze::Settings settings = focusgaze::loadOrCreateSettings();
  focusgaze::Storage storage(focusgaze::PlatformPaths::databasePath());
  try {
    storage.open();
  } catch (const focusgaze::StorageError& e) {
    std::cerr << "Database error: " << e.what() << "\n";
    return 1;
  }

  focusgaze::FocusSessionManager focus(storage, settings);
  // Constructor already syncs any open session. reconcileOnLaunch() is for GUI startup
  // (ends orphans unless resume_focus_on_launch). CLI commands must not auto-end sessions.
  if (cmd == "reconcile") {
    focus.reconcileOnLaunch();
    std::cout << "reconciled focus=" << (focus.isFocusOn() ? "on" : "off") << "\n";
    return 0;
  }

  if (cmd == "status") {
    std::cout << "data_root=" << focusgaze::PlatformPaths::dataRoot() << "\n"
              << "db=" << focusgaze::PlatformPaths::databasePath() << "\n"
              << "settings=" << focusgaze::PlatformPaths::settingsPath() << "\n"
              << "focus=" << (focus.isFocusOn() ? "on" : "off") << "\n";
    if (auto s = focus.activeSession()) {
      std::cout << "session_id=" << s->id << " started_at=" << s->started_at << "\n";
    }
    const auto sessions = storage.listSessions(5);
    std::cout << "recent_sessions=" << sessions.size() << "\n";
    for (const auto& s : sessions) {
      std::cout << "  id=" << s.id << " start=" << s.started_at << " end="
                << (s.ended_at ? std::to_string(*s.ended_at) : std::string("null"))
                << "\n";
    }
    return 0;
  }

  if (cmd == "on") {
    if (!focus.turnOn()) {
      std::cout << "already on\n";
      return 0;
    }
    std::cout << "focus on session_id=" << focus.activeSession()->id << "\n";
    return 0;
  }

  if (cmd == "off") {
    if (!focus.turnOff()) {
      std::cout << "already off\n";
      return 0;
    }
    std::cout << "focus off\n";
    return 0;
  }

  if (cmd == "toggle") {
    const bool on = focus.toggle();
    std::cout << "focus " << (on ? "on" : "off") << "\n";
    return 0;
  }

  if (cmd == "settings-show") {
    std::cout << settings.toJsonString(2) << "\n";
    return 0;
  }

  printUsage(argv[0]);
  return 2;
}
