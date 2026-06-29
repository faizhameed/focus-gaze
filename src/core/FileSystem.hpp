#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace focusgaze::fsutil {

/// Create directories recursively; returns true if path exists as a directory afterwards.
bool ensureDirectory(const std::filesystem::path& path);

/// Read entire file into string; returns empty optional on failure.
std::optional<std::string> readTextFile(const std::filesystem::path& path);

/// Write text atomically (temp + rename when possible). Returns false on failure.
bool writeTextFile(const std::filesystem::path& path, const std::string& contents);

} // namespace focusgaze::fsutil
