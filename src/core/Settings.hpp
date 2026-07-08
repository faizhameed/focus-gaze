#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace focusgaze {

/// User-configurable application settings (JSON on disk).
struct Settings {
  /// Domains that trigger social / time-waste / adult alarms while Focus is on.
  /// Loaded from data-dir blocklist.txt (see resources/sample_blocklist.txt template).
  std::vector<std::string> blocklist{};

  /// Domains that are always treated as productive (wins over blocklist).
  std::vector<std::string> allowlist{};

  /// Cumulative phone-visible seconds that trigger alarm inside the rolling window.
  /// Product default: 60s in a 30-minute window (IMPLEMENTATION.md policy).
  std::int64_t phone_threshold_seconds{60};

  /// Rolling window length for phone accumulation (seconds).
  std::int64_t phone_window_seconds{30 * 60};

  /// Named system sound (default, sosumi, glass, funk, …) or absolute path to audio file.
  std::string alarm_sound{"default"};

  /// When false, sticky alarms still raise visually but no audio plays.
  bool alarm_sound_enabled{true};

  /// When true, strip query strings (and prefer domain) when logging URLs.
  bool privacy_redact{false};

  /// Resume Focus Mode automatically on application launch.
  bool resume_focus_on_launch{false};

  /// When false, Focus Mode still monitors browser URLs but does not use the camera / phone vision.
  bool camera_monitoring_enabled{false};

  /// OpenCV/AVFoundation capture device index (0 = first camera). Avoids Continuity/iPhone surprise.
  int camera_device_index{0};

  /// Local HTTP bridge port for the browser extension (Phase 2).
  int bridge_port{18765};

  /// Shared secret for extension authentication (Phase 2; empty = generate on first save later).
  std::string bridge_token{};

  /// When false, show first-run onboarding wizard on next GUI launch.
  bool onboarding_completed{false};

  /// Open focusGaze automatically when the user logs in (macOS login item).
  bool open_at_login{false};

  /// Chrome Native Messaging host manifests installed for this user (Phase 5).
  bool native_messaging_installed{false};

  static Settings defaults();

  /// Load from JSON file. Missing file => defaults (ok). Corrupt file => false and leaves *this unchanged.
  bool loadFromFile(const std::filesystem::path& path);

  /// Save JSON to path (creates parent directories). Returns false on I/O error.
  bool saveToFile(const std::filesystem::path& path) const;

  /// Serialize / parse helpers (also used by tests).
  std::string toJsonString(int indent = 2) const;
  bool fromJsonString(const std::string& json);
};

/// Load settings from PlatformPaths::settingsPath(), creating defaults on disk if missing.
Settings loadOrCreateSettings();

/// Persist settings to PlatformPaths::settingsPath().
bool saveSettings(const Settings& settings);

} // namespace focusgaze
