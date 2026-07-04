#pragma once

#include "core/PlatformPaths.hpp"

#include <cstdint>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

namespace focusgaze::test {

inline std::filesystem::path makeTempDir(const std::string& prefix = "fg-test-") {
  const auto base = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<std::uint64_t> dist;
  for (int i = 0; i < 20; ++i) {
    auto path = base / (prefix + std::to_string(dist(gen)));
    if (std::filesystem::create_directory(path)) {
      return path;
    }
  }
  throw std::runtime_error("failed to create temp directory");
}

/// RAII: overrides PlatformPaths data root for the duration of a test.
class ScopedDataRoot {
public:
  explicit ScopedDataRoot(std::filesystem::path root = makeTempDir())
      : root_(std::move(root)) {
    PlatformPaths::setDataRootOverrideForTests(root_);
  }

  ~ScopedDataRoot() {
    PlatformPaths::clearCacheForTests();
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  ScopedDataRoot(const ScopedDataRoot&) = delete;
  ScopedDataRoot& operator=(const ScopedDataRoot&) = delete;

  const std::filesystem::path& path() const { return root_; }

private:
  std::filesystem::path root_;
};

} // namespace focusgaze::test
