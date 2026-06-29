#include "core/Settings.hpp"

#include "core/FileSystem.hpp"
#include "core/PlatformPaths.hpp"

#include <nlohmann/json.hpp>

namespace focusgaze {
namespace {

using nlohmann::json;

json stringVectorToJson(const std::vector<std::string>& v) {
  return json(v);
}

std::vector<std::string> stringVectorFromJson(const json& j) {
  if (!j.is_array()) {
    return {};
  }
  std::vector<std::string> out;
  out.reserve(j.size());
  for (const auto& item : j) {
    if (item.is_string()) {
      out.push_back(item.get<std::string>());
    }
  }
  return out;
}

} // namespace

Settings Settings::defaults() {
  return Settings{};
}

std::string Settings::toJsonString(int indent) const {
  json j;
  j["blocklist"] = stringVectorToJson(blocklist);
  j["allowlist"] = stringVectorToJson(allowlist);
  j["phone_threshold_seconds"] = phone_threshold_seconds;
  j["phone_window_seconds"] = phone_window_seconds;
  j["alarm_sound"] = alarm_sound;
  j["privacy_redact"] = privacy_redact;
  j["resume_focus_on_launch"] = resume_focus_on_launch;
  j["bridge_port"] = bridge_port;
  j["bridge_token"] = bridge_token;
  return j.dump(indent);
}

bool Settings::fromJsonString(const std::string& text) {
  json j;
  try {
    j = json::parse(text);
  } catch (const json::parse_error&) {
    return false;
  }
  if (!j.is_object()) {
    return false;
  }

  Settings next = defaults();
  if (j.contains("blocklist")) {
    next.blocklist = stringVectorFromJson(j["blocklist"]);
  }
  if (j.contains("allowlist")) {
    next.allowlist = stringVectorFromJson(j["allowlist"]);
  }
  if (j.contains("phone_threshold_seconds") && j["phone_threshold_seconds"].is_number_integer()) {
    next.phone_threshold_seconds = j["phone_threshold_seconds"].get<std::int64_t>();
  }
  if (j.contains("phone_window_seconds") && j["phone_window_seconds"].is_number_integer()) {
    next.phone_window_seconds = j["phone_window_seconds"].get<std::int64_t>();
  }
  if (j.contains("alarm_sound") && j["alarm_sound"].is_string()) {
    next.alarm_sound = j["alarm_sound"].get<std::string>();
  }
  if (j.contains("privacy_redact") && j["privacy_redact"].is_boolean()) {
    next.privacy_redact = j["privacy_redact"].get<bool>();
  }
  if (j.contains("resume_focus_on_launch") && j["resume_focus_on_launch"].is_boolean()) {
    next.resume_focus_on_launch = j["resume_focus_on_launch"].get<bool>();
  }
  if (j.contains("bridge_port") && j["bridge_port"].is_number_integer()) {
    next.bridge_port = j["bridge_port"].get<int>();
  }
  if (j.contains("bridge_token") && j["bridge_token"].is_string()) {
    next.bridge_token = j["bridge_token"].get<std::string>();
  }

  // Basic validation
  if (next.phone_threshold_seconds < 0 || next.phone_window_seconds <= 0) {
    return false;
  }
  if (next.bridge_port <= 0 || next.bridge_port > 65535) {
    return false;
  }

  *this = std::move(next);
  return true;
}

bool Settings::loadFromFile(const std::filesystem::path& path) {
  const auto text = fsutil::readTextFile(path);
  if (!text.has_value()) {
    return false;
  }
  return fromJsonString(*text);
}

bool Settings::saveToFile(const std::filesystem::path& path) const {
  return fsutil::writeTextFile(path, toJsonString(2) + "\n");
}

Settings loadOrCreateSettings() {
  PlatformPaths::ensureDataLayout();
  Settings settings = Settings::defaults();
  const auto path = PlatformPaths::settingsPath();
  if (!settings.loadFromFile(path)) {
    // Missing or corrupt: write defaults for a clean first run.
    (void)settings.saveToFile(path);
  }
  return settings;
}

bool saveSettings(const Settings& settings) {
  PlatformPaths::ensureDataLayout();
  return settings.saveToFile(PlatformPaths::settingsPath());
}

} // namespace focusgaze
