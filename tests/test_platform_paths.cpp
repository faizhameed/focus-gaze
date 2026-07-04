#include "test_helpers.hpp"

#include "core/PlatformPaths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using focusgaze::PlatformPaths;
using focusgaze::test::ScopedDataRoot;

TEST_CASE("PlatformPaths override sets data root and derived paths", "[paths]") {
  ScopedDataRoot scope;
  const auto root = scope.path();

  REQUIRE(PlatformPaths::dataRoot() == std::filesystem::absolute(root));
  REQUIRE(PlatformPaths::settingsPath() == PlatformPaths::dataRoot() / "settings.json");
  REQUIRE(PlatformPaths::databasePath() == PlatformPaths::dataRoot() / "focusgaze.db");
  REQUIRE(PlatformPaths::logsDir() == PlatformPaths::dataRoot() / "logs");
}

TEST_CASE("PlatformPaths::ensureDataLayout creates root and logs", "[paths]") {
  ScopedDataRoot scope;
  REQUIRE(PlatformPaths::ensureDataLayout());
  REQUIRE(std::filesystem::is_directory(PlatformPaths::dataRoot()));
  REQUIRE(std::filesystem::is_directory(PlatformPaths::logsDir()));
}

TEST_CASE("PlatformPaths honors FOCUSGAZE_DATA_DIR when no override", "[paths]") {
  PlatformPaths::clearCacheForTests();
  const auto tmp = focusgaze::test::makeTempDir("fg-env-");
  // setenv is POSIX; fine on macOS for Phase 1 tests.
  REQUIRE(setenv(PlatformPaths::kEnvDataDir, tmp.string().c_str(), 1) == 0);
  PlatformPaths::clearCacheForTests();

  REQUIRE(PlatformPaths::dataRoot() == std::filesystem::absolute(tmp));

  unsetenv(PlatformPaths::kEnvDataDir);
  PlatformPaths::clearCacheForTests();
  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("PlatformPaths default root contains app name on macOS/Unix", "[paths]") {
  PlatformPaths::clearCacheForTests();
  unsetenv(PlatformPaths::kEnvDataDir);
  PlatformPaths::clearCacheForTests();

  const auto root = PlatformPaths::dataRoot().string();
  REQUIRE(root.find(PlatformPaths::kAppName) != std::string::npos);
#if defined(__APPLE__)
  // Expect Application Support layout when HOME is set.
  if (std::getenv("HOME") != nullptr) {
    REQUIRE(root.find("Application Support") != std::string::npos);
  }
#endif
  PlatformPaths::clearCacheForTests();
}
