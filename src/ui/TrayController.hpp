#pragma once

/// @file TrayController.hpp
/// Qt system-tray UI: Focus, optional camera monitoring, bridge, stats, dashboard.

#include "bridge/HttpBrowserBridge.hpp"
#include "core/AlarmPresenter.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PhoneMonitor.hpp"
#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "ui/CameraWindow.hpp"
#include "ui/DashboardWindow.hpp"
#include "vision/CameraSource.hpp"
#include "vision/VisionLoop.hpp"

#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

class QMenu;
class QAction;

namespace focusgaze {

class TrayController : public QObject {
  Q_OBJECT
public:
  explicit TrayController(QObject* parent = nullptr);
  ~TrayController() override;

  bool initialize();

public slots:
  void toggleFocus();
  void turnFocusOn();
  void turnFocusOff();
  /// Enable/disable camera + phone vision (independent of Focus URL monitoring).
  void setCameraMonitoring(bool enabled);
  void toggleCameraMonitoring();
  void showCameraPreview(bool show);
  void toggleCameraPreview();
  void showStats();
  void showStatusMessage();
  void showDashboard();
  void copyBridgeToken();
  /// Open Chrome Web Store / install help for the extension.
  void openExtensionInstallPage();
  /// Open a one-time pair URL in Google Chrome so the extension receives the token.
  void connectBrowserExtension();
  /// User picked a different capture device in the dashboard.
  void setCameraDeviceIndex(int index);
  void quitApp();

private slots:
  void onTick();

private:
  void rebuildMenu();
  void updateTrayTooltip();
  void ensureBridge();
  void stopBridge();
  /// Open the configured camera device. Returns true if capture is live.
  bool ensureCamera();
  void releaseCamera();
  void persistSettings();
  void ensureDashboard();
  void refreshDashboard();
  void refreshStatusPage();
  void refreshStatsPage();
  void populateCameraDevices();
  QString cameraDeviceLabel() const;
  static EpochSeconds wallNow();

  Settings settings_;
  std::unique_ptr<Storage> storage_;
  std::unique_ptr<FocusSessionManager> focus_;
  std::unique_ptr<BrowserMonitor> browser_;
  std::unique_ptr<PhoneMonitor> phone_;
  std::unique_ptr<VisionLoop> vision_;
  std::unique_ptr<HttpBrowserBridge> bridge_;
  std::unique_ptr<CameraSource> camera_;
  CameraWindow* camera_window_{nullptr};
  DashboardWindow* dashboard_{nullptr};
  AlarmPresenter alarms_ui_;

  QSystemTrayIcon* tray_{nullptr};
  QMenu* menu_{nullptr};
  QTimer* tick_timer_{nullptr};
  bool bridge_ok_{false};
  /// User wants the preview window visible (only meaningful when camera monitoring is on).
  bool camera_preview_wanted_{true};
};

} // namespace focusgaze
