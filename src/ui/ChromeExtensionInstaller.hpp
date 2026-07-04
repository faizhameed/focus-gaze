#pragma once

/// @file ChromeExtensionInstaller.hpp
/// Invokes scripts/chrome_extension_installer.py for one-click multi-profile install.

#include <QString>
#include <optional>
#include <string>
#include <vector>

namespace focusgaze {

/// Parsed result of the Python multi-profile Chrome installer.
struct ChromeInstallResult {
  bool ok{false};
  QString message;
  QString extension_id;
  std::vector<QString> profiles;
  QString raw_json;
};

/// Locate the focusGaze repository root (contains extension/ and scripts/).
std::optional<std::string> findFocusGazeRepoRoot();

/**
 * Run the one-click Chrome extension installer.
 * @param relaunch_chrome When true, quit/relaunch Chrome so External Extensions apply.
 * @return Structured result; ok=false on hard failure.
 */
ChromeInstallResult installChromeExtensionAllProfiles(bool relaunch_chrome = true);

/// JSON string form suitable for the HTTP bridge response body.
std::string installChromeExtensionAllProfilesJson(bool relaunch_chrome = true);

} // namespace focusgaze
