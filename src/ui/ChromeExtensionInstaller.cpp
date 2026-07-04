#include "ui/ChromeExtensionInstaller.hpp"

#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>

namespace focusgaze {
namespace {

namespace fs = std::filesystem;

bool looksLikeRepoRoot(const fs::path& p) {
  return fs::is_directory(p / "extension" / "chrome") &&
         fs::is_regular_file(p / "scripts" / "chrome_extension_installer.py");
}

} // namespace

std::optional<std::string> findFocusGazeRepoRoot() {
  // 1) Explicit override for packaged builds / custom layouts.
  if (const char* env = std::getenv("FOCUSGAZE_ROOT")) {
    fs::path p(env);
    if (looksLikeRepoRoot(p)) return p.string();
  }

  // 2) Walk up from the running executable (build/focusGaze.app/... or build/).
  QString app_dir = QCoreApplication::applicationDirPath();
  fs::path cur = fs::path(app_dir.toStdString());
  for (int i = 0; i < 8; ++i) {
    if (looksLikeRepoRoot(cur)) return cur.string();
    if (!cur.has_parent_path() || cur == cur.parent_path()) break;
    cur = cur.parent_path();
  }

  // 3) Walk up from current working directory.
  std::error_code ec;
  cur = fs::current_path(ec);
  if (!ec) {
    for (int i = 0; i < 8; ++i) {
      if (looksLikeRepoRoot(cur)) return cur.string();
      if (!cur.has_parent_path() || cur == cur.parent_path()) break;
      cur = cur.parent_path();
    }
  }
  return std::nullopt;
}

ChromeInstallResult installChromeExtensionAllProfiles(bool relaunch_chrome) {
  ChromeInstallResult out;
  auto root = findFocusGazeRepoRoot();
  if (!root) {
    out.ok = false;
    out.message =
        "Could not find focusGaze repo root (expected extension/chrome and "
        "scripts/chrome_extension_installer.py). Set FOCUSGAZE_ROOT.";
    return out;
  }

  const QString script =
      QString::fromStdString(*root + "/scripts/chrome_extension_installer.py");
  if (!QFileInfo::exists(script)) {
    out.ok = false;
    out.message = QString("Installer script missing: %1").arg(script);
    return out;
  }

  QStringList args;
  args << script << "--json";
  if (!relaunch_chrome) args << "--no-relaunch";

  QProcess proc;
  proc.setProgram("python3");
  proc.setArguments(args);
  proc.setWorkingDirectory(QString::fromStdString(*root));
  proc.setProcessChannelMode(QProcess::MergedChannels);
  proc.start();
  if (!proc.waitForStarted(5000)) {
    out.ok = false;
    out.message = "Failed to start python3 for Chrome extension installer.";
    return out;
  }
  // Packing + optional Chrome quit/relaunch can take a while.
  if (!proc.waitForFinished(180000)) {
    proc.kill();
    out.ok = false;
    out.message = "Chrome extension installer timed out.";
    return out;
  }

  const QByteArray raw = proc.readAllStandardOutput();
  out.raw_json = QString::fromUtf8(raw).trimmed();
  try {
    auto j = nlohmann::json::parse(out.raw_json.isEmpty() ? "{}" : out.raw_json.toStdString());
    out.ok = j.value("ok", false);
    out.message = QString::fromStdString(j.value("message", std::string("Install finished")));
    out.extension_id = QString::fromStdString(j.value("extension_id", std::string()));
    if (j.contains("profiles") && j["profiles"].is_array()) {
      for (const auto& p : j["profiles"]) {
        if (p.is_string()) out.profiles.push_back(QString::fromStdString(p.get<std::string>()));
      }
    }
    if (!out.ok && j.contains("error") && j["error"].is_string()) {
      out.message = QString::fromStdString(j["error"].get<std::string>());
      if (j.contains("message") && j["message"].is_string()) {
        out.message = QString::fromStdString(j["message"].get<std::string>());
      }
    }
  } catch (...) {
    out.ok = (proc.exitCode() == 0);
    out.message = out.raw_json.isEmpty()
                      ? QString("Installer failed (exit %1)").arg(proc.exitCode())
                      : out.raw_json;
  }
  return out;
}

std::string installChromeExtensionAllProfilesJson(bool relaunch_chrome) {
  const auto r = installChromeExtensionAllProfiles(relaunch_chrome);
  if (!r.raw_json.isEmpty()) {
    // Prefer the installer JSON when available.
    try {
      auto j = nlohmann::json::parse(r.raw_json.toStdString());
      return j.dump();
    } catch (...) {
      // fall through
    }
  }
  nlohmann::json j;
  j["ok"] = r.ok;
  j["message"] = r.message.toStdString();
  j["extension_id"] = r.extension_id.toStdString();
  j["profiles"] = nlohmann::json::array();
  for (const auto& p : r.profiles) j["profiles"].push_back(p.toStdString());
  return j.dump();
}

} // namespace focusgaze
