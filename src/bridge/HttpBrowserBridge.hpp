#pragma once

#include "core/BrowserMonitor.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace focusgaze {

/// Localhost HTTP API for the browser extension (Phase 2).
class HttpBrowserBridge {
public:
  HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port);
  ~HttpBrowserBridge();

  HttpBrowserBridge(const HttpBrowserBridge&) = delete;
  HttpBrowserBridge& operator=(const HttpBrowserBridge&) = delete;

  bool start();
  void stop();
  bool isRunning() const { return running_.load(); }
  int port() const { return port_; }
  const std::string& token() const { return token_; }

  /// Bind address (always 127.0.0.1 for security).
  static constexpr const char* kBindHost = "127.0.0.1";

private:
  void run();

  BrowserMonitor& monitor_;
  std::string token_;
  int port_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  // Opaque httplib::Server* stored as void to keep header light — use impl in cpp
  std::shared_ptr<void> server_holder_;
};

} // namespace focusgaze
