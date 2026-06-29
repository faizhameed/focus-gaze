#pragma once

#include <filesystem>
#include <string>

namespace focusgaze {

/// Resolves application data locations. OS-specific implementation lives in adapters/.
/// Honors FOCUSGAZE_DATA_DIR when set (absolute or relative path becomes the data root).
class PlatformPaths {
public:
  static constexpr const char* kAppName = "focusGaze";
  static constexpr const char* kEnvDataDir = "FOCUSGAZE_DATA_DIR";

  /// Root directory for config, database, and logs.
  static std::filesystem::path dataRoot();

  static std::filesystem::path settingsPath();
  static std::filesystem::path databasePath();
  static std::filesystem::path logsDir();
  /// User-editable blocked domains (auto-created with a small seed if missing).
  static std::filesystem::path blocklistPath();

  /// Ensure dataRoot (and logs) exist. Returns false if directories cannot be created.
  static bool ensureDataLayout();

  /// Clear cached override (test helper). Re-reads env on next dataRoot().
  static void clearCacheForTests();

  /// Force data root (test helper). Empty path clears override and uses env/OS default.
  static void setDataRootOverrideForTests(std::filesystem::path root);
};

} // namespace focusgaze
