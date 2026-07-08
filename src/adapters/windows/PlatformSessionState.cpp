/// @file PlatformSessionState.cpp
/// Windows stub: lock detection can be added later (WTS / session notifications).
/// Default to available so focus counting still works on Windows builds.

#include "core/PlatformSessionState.hpp"

namespace focusgaze {

bool isInteractiveSessionAvailable() {
  // TODO(phase6): use WTSQuerySessionInformation / session lock notifications.
  return true;
}

} // namespace focusgaze
