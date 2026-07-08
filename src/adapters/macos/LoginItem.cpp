/// @file LoginItem.cpp
/// macOS login-item helpers via osascript (Phase 5 optional open-at-login).

#include "core/LoginItem.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace focusgaze {
namespace {

std::string shellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

std::string runCommand(const std::string& cmd, int* exit_code) {
  std::array<char, 512> buf{};
  std::string result;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    if (exit_code) *exit_code = -1;
    return {};
  }
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    result += buf.data();
  }
  const int rc = pclose(pipe);
  if (exit_code) *exit_code = rc;
  return result;
}

std::string resolveAppBundlePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size > 0 ? size : 1, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) == 0) {
    buf.resize(size > 0 && buf[size - 1] == '\0' ? size - 1 : size);
    // .../focusGaze.app/Contents/MacOS/focusGaze → .../focusGaze.app
    const std::string marker = ".app/Contents/MacOS/";
    const auto pos = buf.rfind(marker);
    if (pos != std::string::npos) {
      // include ".app"
      return buf.substr(0, pos + 4);
    }
  }
#endif
  return "/Applications/focusGaze.app";
}

} // namespace

bool setOpenAtLogin(bool enabled, std::string* error_message) {
  const std::string app = resolveAppBundlePath();
  int rc = 0;
  if (enabled) {
    // Remove duplicates then add once. Path is quoted for AppleScript.
    std::string applescript =
        "tell application \"System Events\"\n"
        "  try\n"
        "    delete (every login item whose name is \"focusGaze\")\n"
        "  end try\n"
        "  try\n"
        "    delete (every login item whose path contains \"focusGaze.app\")\n"
        "  end try\n"
        "  make login item at end with properties {path:\"" +
        app + "\", hidden:false, name:\"focusGaze\"}\n"
              "end tell";
    (void)runCommand("osascript -e " + shellQuote(applescript), &rc);
  } else {
    const std::string applescript =
        "tell application \"System Events\"\n"
        "  try\n"
        "    delete (every login item whose name is \"focusGaze\")\n"
        "  end try\n"
        "  try\n"
        "    delete (every login item whose path contains \"focusGaze.app\")\n"
        "  end try\n"
        "end tell";
    (void)runCommand("osascript -e " + shellQuote(applescript), &rc);
  }
  if (rc != 0) {
    if (error_message) *error_message = "osascript failed (login items may require permission)";
    return false;
  }
  return true;
}

bool isOpenAtLoginEnabled() {
  int rc = 0;
  const std::string out = runCommand(
      "osascript -e " +
          shellQuote("tell application \"System Events\" to get the name of every login item"),
      &rc);
  if (rc != 0) return false;
  return out.find("focusGaze") != std::string::npos;
}

} // namespace focusgaze
