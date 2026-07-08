/// @file test_native_messaging.cpp
/// Phase 5: Native Messaging manifest generation + install paths.

#include "test_helpers.hpp"

#include "core/NativeMessaging.hpp"
#include "core/Settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <filesystem>

using focusgaze::test::ScopedDataRoot;

TEST_CASE("nativeMessagingManifestJson includes host path and extension origin",
          "[phase5][native-messaging]") {
  const auto json = focusgaze::nativeMessagingManifestJson(
      "/Applications/focusGaze.app/Contents/MacOS/focusgaze-nm-host",
      focusgaze::kChromeExtensionId);
  REQUIRE(json.find(focusgaze::kNativeHostName) != std::string::npos);
  REQUIRE(json.find("focusgaze-nm-host") != std::string::npos);
  REQUIRE(json.find(std::string("chrome-extension://") + focusgaze::kChromeExtensionId + "/") !=
          std::string::npos);
  REQUIRE(json.find("stdio") != std::string::npos);
}

TEST_CASE("installNativeMessagingHost writes at least one manifest under a fake home",
          "[phase5][native-messaging]") {
  ScopedDataRoot scope;
  // Create a fake host binary file the installer only needs to exist.
  const auto host = scope.path() / "focusgaze-nm-host";
  {
    std::ofstream out(host);
    out << "#!/bin/sh\n";
  }
  std::filesystem::permissions(host, std::filesystem::perms::owner_all);

  // Point HOME at the temp tree so manifests land under our control.
  const auto old_home = getenv("HOME");
  setenv("HOME", scope.path().c_str(), 1);

  const auto result = focusgaze::installNativeMessagingHost(host);
  REQUIRE(result.ok);
  REQUIRE_FALSE(result.written_manifests.empty());

  bool any_exists = false;
  for (const auto& p : result.written_manifests) {
    if (std::filesystem::exists(p)) {
      any_exists = true;
      std::ifstream in(p);
      std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      REQUIRE(body.find(host.string()) != std::string::npos);
    }
  }
  REQUIRE(any_exists);

  const auto removed = focusgaze::uninstallNativeMessagingHost();
  REQUIRE(removed.ok);

  if (old_home) setenv("HOME", old_home, 1);
  else unsetenv("HOME");
}

TEST_CASE("settings round-trip open_at_login and native_messaging_installed",
          "[phase5][settings]") {
  auto s = focusgaze::Settings::defaults();
  s.open_at_login = true;
  s.native_messaging_installed = true;
  const auto json = s.toJsonString();
  focusgaze::Settings loaded;
  REQUIRE(loaded.fromJsonString(json));
  REQUIRE(loaded.open_at_login);
  REQUIRE(loaded.native_messaging_installed);
}
