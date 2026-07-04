#include "test_helpers.hpp"

#include "core/DefaultBlocklist.hpp"
#include "core/FileSystem.hpp"
#include "core/PlatformPaths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using focusgaze::test::ScopedDataRoot;

TEST_CASE("ensureBlocklistFile creates seed when missing", "[blocklist]") {
  ScopedDataRoot scope;
  REQUIRE(focusgaze::PlatformPaths::ensureDataLayout());
  const auto path = focusgaze::PlatformPaths::blocklistPath();
  REQUIRE_FALSE(std::filesystem::exists(path));

  REQUIRE(focusgaze::ensureBlocklistFile(path));
  REQUIRE(std::filesystem::is_regular_file(path));

  const auto domains = focusgaze::loadBlocklistFile(path);
  REQUIRE(domains.size() >= 4);
  REQUIRE(std::find(domains.begin(), domains.end(), "instagram.com") != domains.end());
}

TEST_CASE("ensureBlocklistFile does not overwrite existing file", "[blocklist]") {
  ScopedDataRoot scope;
  REQUIRE(focusgaze::PlatformPaths::ensureDataLayout());
  const auto path = focusgaze::PlatformPaths::blocklistPath();
  REQUIRE(focusgaze::fsutil::writeTextFile(path, "# custom\nexample-blocked.test\n"));

  REQUIRE(focusgaze::ensureBlocklistFile(path));
  const auto domains = focusgaze::loadBlocklistFile(path);
  REQUIRE(domains.size() == 1);
  REQUIRE(domains[0] == "example-blocked.test");
}

TEST_CASE("loadOrCreateBlocklist uses data-dir file", "[blocklist]") {
  ScopedDataRoot scope;
  auto domains = focusgaze::loadOrCreateBlocklist();
  REQUIRE_FALSE(domains.empty());
  REQUIRE(std::filesystem::is_regular_file(focusgaze::PlatformPaths::blocklistPath()));
}

TEST_CASE("parseBlocklistText ignores comments and blanks", "[blocklist]") {
  const auto d = focusgaze::parseBlocklistText("# hi\n\ninstagram.com\n  tiktok.com  \n");
  REQUIRE(d.size() == 2);
  REQUIRE(d[0] == "instagram.com");
  REQUIRE(d[1] == "tiktok.com");
}
