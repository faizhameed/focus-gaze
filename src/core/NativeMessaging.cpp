/// @file NativeMessaging.cpp
/// Install Chrome Native Messaging manifests and run the stdin/stdout host protocol.

#include "core/NativeMessaging.hpp"

#include "core/PlatformPaths.hpp"
#include "core/Settings.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace focusgaze {
namespace {

std::filesystem::path homeDir() {
  if (const char* h = std::getenv("HOME")) {
    return std::filesystem::path(h);
  }
  return {};
}

/// User-level NativeMessagingHosts directories for major Chromium browsers on macOS.
std::vector<std::filesystem::path> userNativeMessagingDirs() {
  std::vector<std::filesystem::path> dirs;
  const auto home = homeDir();
  if (home.empty()) return dirs;
  const auto support = home / "Library" / "Application Support";
  dirs.push_back(support / "Google" / "Chrome" / "NativeMessagingHosts");
  dirs.push_back(support / "Chromium" / "NativeMessagingHosts");
  dirs.push_back(support / "Microsoft Edge" / "NativeMessagingHosts");
  dirs.push_back(support / "BraveSoftware" / "Brave-Browser" / "NativeMessagingHosts");
  dirs.push_back(support / "Vivaldi" / "NativeMessagingHosts");
  return dirs;
}

std::filesystem::path runningExecutablePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size > 0 ? size : 1, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) == 0) {
    buf.resize(size > 0 && buf[size - 1] == '\0' ? size - 1 : size);
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(buf, ec);
    if (!ec) return p;
    return std::filesystem::path(buf);
  }
#endif
  return {};
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << text;
  return static_cast<bool>(out);
}

bool readExact(std::istream& in, char* buf, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    in.read(buf + got, static_cast<std::streamsize>(n - got));
    const auto g = static_cast<std::size_t>(in.gcount());
    if (g == 0) return false;
    got += g;
  }
  return true;
}

bool writeExact(std::ostream& out, const char* buf, std::size_t n) {
  out.write(buf, static_cast<std::streamsize>(n));
  return static_cast<bool>(out);
}

/// Chrome NM message: 4-byte little-endian length + UTF-8 JSON body.
bool readNativeMessage(std::istream& in, std::string& json_out) {
  unsigned char len_buf[4];
  if (!readExact(in, reinterpret_cast<char*>(len_buf), 4)) return false;
  const std::uint32_t len = static_cast<std::uint32_t>(len_buf[0]) |
                            (static_cast<std::uint32_t>(len_buf[1]) << 8) |
                            (static_cast<std::uint32_t>(len_buf[2]) << 16) |
                            (static_cast<std::uint32_t>(len_buf[3]) << 24);
  // Chrome rejects huge messages; keep a sane ceiling.
  if (len == 0 || len > 1024 * 1024) return false;
  json_out.assign(len, '\0');
  return readExact(in, json_out.data(), len);
}

bool writeNativeMessage(std::ostream& out, const std::string& json) {
  const auto len = static_cast<std::uint32_t>(json.size());
  unsigned char len_buf[4] = {
      static_cast<unsigned char>(len & 0xff),
      static_cast<unsigned char>((len >> 8) & 0xff),
      static_cast<unsigned char>((len >> 16) & 0xff),
      static_cast<unsigned char>((len >> 24) & 0xff),
  };
  if (!writeExact(out, reinterpret_cast<const char*>(len_buf), 4)) return false;
  return writeExact(out, json.data(), json.size());
}

nlohmann::json handleRequest(const nlohmann::json& req) {
  nlohmann::json res;
  res["ok"] = true;
  res["app"] = "focusGaze";
  res["version"] = FOCUSGAZE_VERSION;

  std::string type = "ping";
  if (req.contains("type") && req["type"].is_string()) {
    type = req["type"].get<std::string>();
  }

  if (type == "ping") {
    res["type"] = "pong";
    return res;
  }

  if (type == "getBridge" || type == "bridge") {
    Settings s = loadOrCreateSettings();
    res["type"] = "bridge";
    res["host"] = "127.0.0.1";
    res["port"] = s.bridge_port;
    res["token"] = s.bridge_token;
    res["pair_hint"] =
        "If the desktop app is running, open Connect browser from the tray for one-click pair.";
    return res;
  }

  res["ok"] = false;
  res["error"] = "unknown_type";
  res["type"] = type;
  return res;
}

} // namespace

std::string nativeMessagingManifestJson(const std::filesystem::path& host_binary,
                                        const std::string& extension_id) {
  nlohmann::json j;
  j["name"] = kNativeHostName;
  j["description"] = "focusGaze local bridge helper for Chrome Native Messaging";
  j["path"] = host_binary.string();
  j["type"] = "stdio";
  j["allowed_origins"] = nlohmann::json::array(
      {std::string("chrome-extension://") + extension_id + "/"});
  return j.dump(2);
}

std::filesystem::path resolveNativeMessagingHostBinary() {
  std::error_code ec;
  const auto self = runningExecutablePath();
  if (!self.empty()) {
    // Prefer dedicated host next to the GUI binary inside the .app bundle.
    const auto sibling = self.parent_path() / "focusgaze-nm-host";
    if (std::filesystem::exists(sibling, ec)) {
      auto canon = std::filesystem::weakly_canonical(sibling, ec);
      return ec ? sibling : canon;
    }
    // If this process *is* the host binary, use self.
    if (self.filename() == "focusgaze-nm-host") {
      return self;
    }
  }

  // Fallback: default Applications install location.
  const std::filesystem::path apps =
      "/Applications/focusGaze.app/Contents/MacOS/focusgaze-nm-host";
  if (std::filesystem::exists(apps, ec)) {
    return apps;
  }
  if (!self.empty()) {
    return self.parent_path() / "focusgaze-nm-host";
  }
  return {};
}

NativeMessagingInstallResult installNativeMessagingHost(const std::filesystem::path& host_binary,
                                                        const std::string& extension_id) {
  NativeMessagingInstallResult result;
  if (host_binary.empty()) {
    result.message = "host binary path is empty";
    return result;
  }
  std::error_code ec;
  if (!std::filesystem::exists(host_binary, ec)) {
    result.message = "host binary not found: " + host_binary.string();
    return result;
  }

  const auto abs = std::filesystem::weakly_canonical(host_binary, ec);
  const auto path = ec ? host_binary : abs;
  const std::string body = nativeMessagingManifestJson(path, extension_id);
  const std::string filename = std::string(kNativeHostName) + ".json";

  int ok_count = 0;
  std::ostringstream errors;
  for (const auto& dir : userNativeMessagingDirs()) {
    const auto manifest = dir / filename;
    if (writeTextFile(manifest, body)) {
      result.written_manifests.push_back(manifest);
      ++ok_count;
    } else {
      errors << "failed: " << manifest << "; ";
    }
  }

  result.ok = ok_count > 0;
  if (result.ok) {
    result.message = "Installed " + std::to_string(ok_count) + " Native Messaging manifest(s)";
  } else {
    result.message = errors.str().empty() ? "no browser directories written" : errors.str();
  }
  return result;
}

NativeMessagingInstallResult uninstallNativeMessagingHost() {
  NativeMessagingInstallResult result;
  const std::string filename = std::string(kNativeHostName) + ".json";
  int removed = 0;
  for (const auto& dir : userNativeMessagingDirs()) {
    const auto manifest = dir / filename;
    std::error_code ec;
    if (std::filesystem::exists(manifest, ec)) {
      if (std::filesystem::remove(manifest, ec)) {
        result.written_manifests.push_back(manifest);
        ++removed;
      }
    }
  }
  result.ok = true;
  result.message = "Removed " + std::to_string(removed) + " manifest(s)";
  return result;
}

int runNativeMessagingHostLoop() {
  // Avoid mixing C++ streams with Chrome's binary framing unexpectedly.
  std::cin.tie(nullptr);
  std::ios::sync_with_stdio(false);

  std::string json;
  while (readNativeMessage(std::cin, json)) {
    nlohmann::json req;
    try {
      req = nlohmann::json::parse(json);
    } catch (...) {
      nlohmann::json err;
      err["ok"] = false;
      err["error"] = "invalid_json";
      if (!writeNativeMessage(std::cout, err.dump())) break;
      continue;
    }
    const auto res = handleRequest(req);
    if (!writeNativeMessage(std::cout, res.dump())) break;
    std::cout.flush();
  }
  return 0;
}

} // namespace focusgaze
