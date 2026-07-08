#pragma once

/// @file PlatformSessionState.hpp
/// OS interactive-session probes used to pause focus time (lock screen, etc.).
/// Sleep/suspend is handled via heartbeat gaps, not only this API.

namespace focusgaze {

/**
 * True when the user session can accrue focus time for an unlocked workstation.
 *
 * Policy (product):
 * - Locked screen / non-console session → false (do not count).
 * - Unlocked with no keyboard/mouse activity → true (still count).
 * - Sleep is detected separately via large gaps between heartbeats.
 *
 * Unknown / unsupported platforms default to true so focus still works.
 */
bool isInteractiveSessionAvailable();

} // namespace focusgaze
