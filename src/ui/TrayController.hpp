#pragma once

/// @file TrayController.hpp
/// Qt system-tray UI for Focus toggle, camera preview, bridge, stats (Phase 5).

#include "bridge/HttpBrowserBridge.hpp"
#include "core/AlarmPresenter.hpp"
#include "core/BrowserMonitor.hpp"
#include "core/FocusSession.hpp"
#include "core/PhoneMonitor.hpp"
#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"
#include "core/Storage.hpp"
#include "ui/CameraWindow.hpp"
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
  void showCameraPreview(bool show);
  void toggleCameraPreview();
  void showStats();
  void showStatusMessage();
  void copyBridgeToken();
  void quitApp();

private slots:
  void onTick();

private:
  void rebuildMenu();
  void updateTrayTooltip();
  void ensureBridge();
  void stopBridge();
  void ensureCamera();
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
  AlarmPresenter alarms_ui_;

  QSystemTrayIcon* tray_{nullptr};
  QMenu* menu_{nullptr};
  QAction* focus_action_{nullptr};
  QAction* camera_action_{nullptr};
  QAction* stats_action_{nullptr};
  QAction* status_action_{nullptr};
  QAction* token_action_{nullptr};
  QAction* quit_action_{nullptr};
  QTimer* tick_timer_{nullptr};
  bool bridge_ok_{false};
  bool camera_preview_wanted_{true}; // show camera by default when Focus ON
  bool last_phone_log_{false};
};

} // namespace focusgaze
