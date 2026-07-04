#include "core/DefaultBlocklist.hpp"

#include "core/FileSystem.hpp"
#include "core/PlatformPaths.hpp"

#include <cctype>
#include <filesystem>
#include <sstream>
#include <unordered_set>

namespace focusgaze {
namespace {

std::string toLowerCopy(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string seedBlocklistFileContents() {
  std::ostringstream oss;
  oss << "# focusGaze blocklist — one domain per line (# comments ok)\n"
      << "# This file was auto-created because it was missing.\n"
      << "# It will NOT be overwritten on later runs.\n"
      << "# For a larger template, copy resources/sample_blocklist.txt over this file.\n"
      << "#\n";
  for (const auto& d : seedBlocklistDomains()) {
    oss << d << "\n";
  }
  return oss.str();
}

} // namespace

std::vector<std::string> seedBlocklistDomains() {
  // Still relevant with file-based blocklists:
  // 1) Written once into blocklist.txt when that file is missing (ensureBlocklistFile).
  // 2) In-memory fallback if blocklist.txt exists but parses to empty/corrupt.
  // Ongoing edits happen only in blocklist.txt (or by copying sample_blocklist.txt).
  // This is NOT the long-term source of truth for a customized install.
  return {
      "instagram.com", "www.instagram.com", "tiktok.com", "www.tiktok.com",
      "x.com",         "twitter.com",       "www.twitter.com",
      "reddit.com",    "www.reddit.com",    "facebook.com", "www.facebook.com",
      "youtube.com",   "www.youtube.com",   "netflix.com",  "www.netflix.com",
  };
}

std::vector<std::string> parseBlocklistText(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    // trim
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    if (start > 0) {
      line = line.substr(start);
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    out.push_back(line);
  }
  return out;
}

std::vector<std::string> loadBlocklistFile(const std::filesystem::path& path) {
  const auto text = fsutil::readTextFile(path);
  if (!text) {
    return {};
  }
  return parseBlocklistText(*text);
}

bool ensureBlocklistFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(path, ec)) {
    return true;
  }
  return fsutil::writeTextFile(path, seedBlocklistFileContents());
}

std::vector<std::string> loadOrCreateBlocklist() {
  PlatformPaths::ensureDataLayout();
  const auto path = PlatformPaths::blocklistPath();
  (void)ensureBlocklistFile(path);
  auto domains = loadBlocklistFile(path);
  if (domains.empty()) {
    // Corrupt/empty file: do not overwrite; fall back to seed in memory only.
    domains = seedBlocklistDomains();
  }
  return domains;
}

std::vector<std::string> mergeDomainLists(const std::vector<std::string>& primary,
                                         const std::vector<std::string>& extra) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::vector<std::string>& list) {
    for (const auto& d : list) {
      const auto key = toLowerCopy(d);
      if (key.empty() || !seen.insert(key).second) {
        continue;
      }
      out.push_back(d);
    }
  };
  add(primary);
  add(extra);
  return out;
}

} // namespace focusgaze
