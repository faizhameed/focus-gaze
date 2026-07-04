#pragma once

#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PhoneMonitor.hpp"
#include "vision/VisionLoop.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace focusgaze {

/// Localhost HTTP API for the browser extension and phone inject.
/// Includes one-time pairing so the extension can receive the bridge token
/// automatically via a loopback HTML page + chrome.runtime.sendMessage.
class HttpBrowserBridge {
public:
  /// Optional install hook: invoked by POST /v1/install-extension.
  using InstallHandler = std::function<std::string(bool relaunch_chrome)>;

  /// Optional camera status provider for GET /v1/status.
  using CameraStatusProvider = std::function<bool()>;

  HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port,
                    PhoneMonitor* phone = nullptr, VisionLoop* vision = nullptr,
                    FocusSessionManager* focus = nullptr);
  ~HttpBrowserBridge();

  HttpBrowserBridge(const HttpBrowserBridge&) = delete;
  HttpBrowserBridge& operator=(const HttpBrowserBridge&) = delete;

  bool start();
  void stop();
  bool isRunning() const { return running_.load(); }
  int port() const { return port_; }
  const std::string& token() const { return token_; }

  /// Register the multi-profile Chrome installer (desktop app sets this).
  void setInstallHandler(InstallHandler handler) { install_handler_ = std::move(handler); }

  /// Optional: report whether camera monitoring is enabled.
  void setCameraStatusProvider(CameraStatusProvider provider) {
    camera_status_provider_ = std::move(provider);
  }

  /**
   * Create a short-lived one-time pairing session and return the full pair-ui URL.
   * Open this URL in Google Chrome so the page can push the token into the extension.
   * @return empty string if the bridge token/port is invalid
   */
  std::string createPairUrl();

  /// Chrome extension ID used by the pair page (stable key / Web Store id).
  /// Overridable via env FOCUSGAZE_CHROME_EXTENSION_ID.
  static std::string chromeExtensionId();

  static constexpr const char* kBindHost = "127.0.0.1";
  /// Default extension id from extension/keys (manifest key field).
  static constexpr const char* kDefaultChromeExtensionId =
      "ocbhbndfchcjlkailmcmohpohjdclelg";

private:
  struct PairSession {
    std::string code;
    std::chrono::steady_clock::time_point expires_at;
    bool consumed{false};
  };

  /// Mint a new one-time code bound to the current bridge token (caller holds no lock).
  std::string mintPairCodeLocked();
  /// Validate code; if ok, mark consumed and return true. Token/port read from members.
  bool consumePairCode(const std::string& code);

  BrowserMonitor& monitor_;
  PhoneMonitor* phone_{nullptr};
  VisionLoop* vision_{nullptr};
  FocusSessionManager* focus_{nullptr};
  InstallHandler install_handler_;
  CameraStatusProvider camera_status_provider_;
  std::string token_;
  int port_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  std::shared_ptr<void> server_holder_;

  mutable std::mutex pair_mu_;
  std::vector<PairSession> pair_sessions_;
};

} // namespace focusgaze
