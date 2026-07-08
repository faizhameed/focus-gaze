/// @file native_messaging_host_main.cpp
/// Standalone Chrome Native Messaging host (no args — Chrome launches this binary).

#include "core/NativeMessaging.hpp"
#include "core/PlatformPaths.hpp"

int main() {
  // Ensure data layout exists so settings/token can be read for getBridge.
  (void)focusgaze::PlatformPaths::ensureDataLayout();
  return focusgaze::runNativeMessagingHostLoop();
}
