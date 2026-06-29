#pragma once

#include "core/BrowserMonitor.hpp"
#include "core/PhoneMonitor.hpp"
#include "vision/VisionLoop.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace focusgaze {

/// Localhost HTTP API for the browser extension and phone inject (Phase 2–3).
class HttpBrowserBridge {
public:
  HttpBrowserBridge(BrowserMonitor& monitor, std::string token, int port,
                    PhoneMonitor* phone = nullptr, VisionLoop* vision = nullptr);
  ~HttpBrowserBridge();

  HttpBrowserBridge(const HttpBrowserBridge&) = delete;
  HttpBrowserBridge& operator=(const HttpBrowserBridge&) = delete;

  bool start();
  void stop();
  bool isRunning() const { return running_.load(); }
  int port() const { return port_; }
  const std::string& token() const { return token_; }

  static constexpr const char* kBindHost = "127.0.0.1";

private:
  BrowserMonitor& monitor_;
  PhoneMonitor* phone_{nullptr};
  VisionLoop* vision_{nullptr};
  std::string token_;
  int port_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;
  std::shared_ptr<void> server_holder_;
};

} // namespace focusgaze
