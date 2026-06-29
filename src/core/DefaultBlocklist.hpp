#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace focusgaze {

/// Minimal social seed written when blocklist.txt does not exist yet.
std::vector<std::string> seedBlocklistDomains();

/// Parse a blocklist file (one domain per line, # comments).
std::vector<std::string> parseBlocklistText(const std::string& text);

/// Load domains from path. Returns empty vector if file missing/unreadable.
std::vector<std::string> loadBlocklistFile(const std::filesystem::path& path);

/// If path does not exist, create it with seedBlocklistDomains(). Never overwrites.
/// Returns true if the file exists afterward (created or already present).
bool ensureBlocklistFile(const std::filesystem::path& path);

/// Load blocklist from PlatformPaths::blocklistPath(), creating the seed file if needed.
std::vector<std::string> loadOrCreateBlocklist();

/// Merge two domain lists, preserving order, case-insensitive de-dupe.
std::vector<std::string> mergeDomainLists(const std::vector<std::string>& primary,
                                         const std::vector<std::string>& extra);

} // namespace focusgaze
