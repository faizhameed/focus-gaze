#pragma once

/// @file LoginItem.hpp
/// Optional “open at login” registration for the Mac app (Phase 5).

#include <string>

namespace focusgaze {

/**
 * Register or remove a macOS login item for focusGaze.
 * Implementation uses a small AppleScript via /usr/bin/osascript (user-level, no admin).
 *
 * @param app_path absolute path to focusGaze.app (empty = best-effort /Applications or running bundle)
 * @param enabled true to add, false to remove matching login items named focusGaze
 */
bool setOpenAtLogin(bool enabled, std::string* error_message = nullptr);

/// Best-effort probe: true if a login item whose path/name mentions focusGaze exists.
bool isOpenAtLoginEnabled();

} // namespace focusgaze
