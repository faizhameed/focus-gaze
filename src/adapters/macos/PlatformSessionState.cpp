/// @file PlatformSessionState.cpp
/// macOS: detect lock screen / off-console session via CGSession dictionary.

#include "core/PlatformSessionState.hpp"

#include <CoreFoundation/CoreFoundation.h>

// Undocumented but widely used; present in ApplicationServices / CoreGraphics.
extern "C" CFDictionaryRef CGSessionCopyCurrentDictionary(void);

namespace focusgaze {

bool isInteractiveSessionAvailable() {
  CFDictionaryRef dict = CGSessionCopyCurrentDictionary();
  if (dict == nullptr) {
    // No session info (e.g. headless) — allow counting.
    return true;
  }

  bool available = true;

  // Screen lock key used by the loginwindow / security agent.
  const void* locked_val =
      CFDictionaryGetValue(dict, CFSTR("CGSSessionScreenIsLocked"));
  if (locked_val != nullptr && CFGetTypeID(locked_val) == CFBooleanGetTypeID()) {
    if (CFBooleanGetValue(static_cast<CFBooleanRef>(locked_val))) {
      available = false;
    }
  }

  // Prefer only the console (local) session, not a remote/switched-away session.
  const void* on_console =
      CFDictionaryGetValue(dict, CFSTR("kCGSSessionOnConsoleKey"));
  if (on_console != nullptr && CFGetTypeID(on_console) == CFBooleanGetTypeID()) {
    if (!CFBooleanGetValue(static_cast<CFBooleanRef>(on_console))) {
      available = false;
    }
  }

  CFRelease(dict);
  return available;
}

} // namespace focusgaze
