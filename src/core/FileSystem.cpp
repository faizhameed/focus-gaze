#include "core/FileSystem.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

namespace focusgaze::fsutil {

bool ensureDirectory(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return true;
  }
  std::filesystem::create_directories(path, ec);
  return std::filesystem::is_directory(path, ec);
}

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& contents) {
  if (!ensureDirectory(path.parent_path())) {
    return false;
  }
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << contents;
    if (!out.good()) {
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    // Fallback: overwrite directly (Windows may need remove first in some cases)
    std::error_code remove_ec;
    std::filesystem::remove(path, remove_ec);
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
      if (!out) {
        return false;
      }
      out << contents;
      std::filesystem::remove(tmp, remove_ec);
      return static_cast<bool>(out);
    }
  }
  return true;
}

} // namespace focusgaze::fsutil
