#pragma once

/// @file NativeMessaging.hpp
/// Chrome/Chromium Native Messaging host manifest install + host protocol (Phase 5).

#include <filesystem>
#include <string>
#include <vector>

namespace focusgaze {

/// Chrome extension id for the packaged focusGaze Bridge (stable via manifest key).
inline constexpr const char* kChromeExtensionId = "ocbhbndfchcjlkailmcmohpohjdclelg";

/// Native host name used in the NM manifest and chrome.runtime.connectNative.
inline constexpr const char* kNativeHostName = "com.focusgaze.host";

struct NativeMessagingInstallResult {
  bool ok{false};
  std::string message;
  std::vector<std::filesystem::path> written_manifests;
};

/**
 * Resolve the absolute path to the native-messaging host binary that Chrome should launch.
 * Prefer Contents/MacOS/focusgaze-nm-host next to the running app, then PATH lookup fallbacks.
 */
std::filesystem::path resolveNativeMessagingHostBinary();

/**
 * Write user-level Native Messaging host manifests for Chrome / Chromium / Edge / Brave.
 * Does not require admin rights. Safe to call repeatedly (overwrites manifests).
 *
 * @param host_binary absolute path to focusgaze-nm-host (or equivalent)
 * @param extension_id Chrome extension id allowed to connect (default: packaged id)
 */
NativeMessagingInstallResult installNativeMessagingHost(
    const std::filesystem::path& host_binary,
    const std::string& extension_id = kChromeExtensionId);

/// Remove previously installed user-level manifests for this host name.
NativeMessagingInstallResult uninstallNativeMessagingHost();

/**
 * Build the JSON body of a Chrome Native Messaging host manifest.
 * @param host_binary absolute path to the host executable (no arguments).
 */
std::string nativeMessagingManifestJson(const std::filesystem::path& host_binary,
                                        const std::string& extension_id = kChromeExtensionId);

/**
 * Run the Native Messaging host IO loop on stdin/stdout (Chrome protocol).
 * Reads settings from the standard data dir and answers:
 *   {"type":"ping"} | {"type":"getBridge"}
 * Returns process exit code (0 = clean EOF).
 */
int runNativeMessagingHostLoop();

} // namespace focusgaze
