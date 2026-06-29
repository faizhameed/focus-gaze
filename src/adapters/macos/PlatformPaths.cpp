#include "core/PlatformPaths.hpp"

#include "core/FileSystem.hpp"

#include <cstdlib>
#include <mutex>
#include <optional>

#if defined(_WIN32)
#error "macOS PlatformPaths.cpp must not be compiled on Windows"
#endif

namespace focusgaze {
namespace {

std::mutex g_mutex;
std::filesystem::path g_override;
bool g_has_override = false;
std::optional<std::filesystem::path> g_cached;

std::filesystem::path defaultDataRootMac() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return std::filesystem::path{"/tmp"} / PlatformPaths::kAppName;
  }
  return std::filesystem::path{home} / "Library" / "Application Support" / PlatformPaths::kAppName;
}

std::filesystem::path resolveDataRootUnlocked() {
  if (g_has_override) {
    return g_override;
  }
  if (const char* env = std::getenv(PlatformPaths::kEnvDataDir); env != nullptr && env[0] != '\0') {
    return std::filesystem::absolute(std::filesystem::path{env});
  }
  return defaultDataRootMac();
}

} // namespace

std::filesystem::path PlatformPaths::dataRoot() {
  std::lock_guard lock(g_mutex);
  if (!g_cached.has_value()) {
    g_cached = resolveDataRootUnlocked();
  }
  return *g_cached;
}

std::filesystem::path PlatformPaths::settingsPath() {
  return dataRoot() / "settings.json";
}

std::filesystem::path PlatformPaths::databasePath() {
  return dataRoot() / "focusgaze.db";
}

std::filesystem::path PlatformPaths::logsDir() {
  return dataRoot() / "logs";
}

std::filesystem::path PlatformPaths::blocklistPath() {
  return dataRoot() / "blocklist.txt";
}

bool PlatformPaths::ensureDataLayout() {
  const auto root = dataRoot();
  if (!fsutil::ensureDirectory(root)) {
    return false;
  }
  return fsutil::ensureDirectory(logsDir());
}

void PlatformPaths::clearCacheForTests() {
  std::lock_guard lock(g_mutex);
  g_cached.reset();
  g_has_override = false;
  g_override.clear();
}

void PlatformPaths::setDataRootOverrideForTests(std::filesystem::path root) {
  std::lock_guard lock(g_mutex);
  if (root.empty()) {
    g_has_override = false;
    g_override.clear();
  } else {
    g_has_override = true;
    g_override = std::filesystem::absolute(root);
  }
  g_cached.reset();
}

} // namespace focusgaze
