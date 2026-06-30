#include "ui/TrayController.hpp"

#include "core/PlatformPaths.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <chrono>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

EpochSeconds TrayController::wallNow() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

TrayController::TrayController(QObject* parent) : QObject(parent) {}

TrayController::~TrayController() {
  stopBridge();
  alarms_ui_.stop();
  if (camera_window_) {
    camera_window_->close();
    delete camera_window_;
    camera_window_ = nullptr;
  }
}

bool TrayController::initialize() {
  if (!PlatformPaths::ensureDataLayout()) return false;
  settings_ = loadOrCreateSettings();
  storage_ = std::make_unique<Storage>(PlatformPaths::databasePath());
  storage_->open();
  focus_ = std::make_unique<FocusSessionManager>(*storage_, settings_);
  focus_->syncFromStorage();
  browser_ = std::make_unique<BrowserMonitor>(*storage_, *focus_, settings_);
  phone_ = std::make_unique<PhoneMonitor>(*storage_, *focus_, browser_->alarms(), settings_);
  vision_ = std::make_unique<VisionLoop>(*phone_, []() { return false; }, wallNow, 500);

  alarms_ui_.start();
  ensureBridge();

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    QMessageBox::critical(nullptr, "focusGaze", "System tray is not available.");
    return false;
  }

  tray_ = new QSystemTrayIcon(this);
  tray_->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
  tray_->setToolTip("focusGaze");
  rebuildMenu();
  tray_->show();
  tray_->showMessage(
      "focusGaze",
      "Menu bar icon ready. Turn Focus ON, then use “Show camera preview” if the window is hidden.",
      QSystemTrayIcon::Information, 6000);

  tick_timer_ = new QTimer(this);
  connect(tick_timer_, &QTimer::timeout, this, &TrayController::onTick);
  tick_timer_->start(33);

  updateTrayTooltip();
  return true;
}

void TrayController::ensureCamera() {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (camera_ && camera_->isOpen()) return;
  const auto fake = CameraSource::resolveVideoPathFromEnv();
  camera_ = std::make_unique<CameraSource>(fake, 15);
  if (!camera_->isOpen()) {
    if (camera_window_) {
      camera_window_->setHint(
          tr("Camera failed to open.\nGrant Camera access to focusGaze in\n"
             "System Settings → Privacy & Security → Camera,\nthen quit and reopen the app."));
    }
    tray_->showMessage("focusGaze", "Camera failed to open. Check Privacy → Camera for focusGaze.",
                       QSystemTrayIcon::Warning, 6000);
  }
#else
  tray_->showMessage("focusGaze", "Built without OpenCV — camera unavailable.",
                     QSystemTrayIcon::Warning, 4000);
#endif
}

void TrayController::rebuildMenu() {
  if (!menu_) menu_ = new QMenu();
  menu_->clear();

  const bool on = focus_ && focus_->isFocusOn();
  focus_action_ =
      menu_->addAction(on ? tr("Turn Focus OFF") : tr("Turn Focus ON"), this, &TrayController::toggleFocus);

  camera_action_ = menu_->addAction(tr("Show camera preview"), this, &TrayController::toggleCameraPreview);
  camera_action_->setCheckable(true);
  camera_action_->setChecked(camera_preview_wanted_ && camera_window_ && camera_window_->isVisible());
  camera_action_->setToolTip(tr("Open live camera + YOLO boxes (required for phone monitoring UI)"));

  menu_->addSeparator();
  status_action_ = menu_->addAction(tr("Show status…"), this, &TrayController::showStatusMessage);
  stats_action_ = menu_->addAction(tr("Last session stats…"), this, &TrayController::showStats);
  token_action_ = menu_->addAction(tr("Copy bridge token"), this, &TrayController::copyBridgeToken);
  menu_->addSeparator();
  quit_action_ = menu_->addAction(tr("Quit focusGaze"), this, &TrayController::quitApp);
  tray_->setContextMenu(menu_);
}

void TrayController::updateTrayTooltip() {
  if (!tray_ || !focus_) return;
  QString tip = focus_->isFocusOn() ? "focusGaze — Focus ON" : "focusGaze — Focus OFF";
  if (bridge_ok_) tip += QString(" | :%1").arg(settings_.bridge_port);
  if (camera_preview_wanted_) tip += " | camera UI";
  tray_->setToolTip(tip);
}

void TrayController::ensureBridge() {
  if (bridge_ && bridge_->isRunning()) {
    bridge_ok_ = true;
    return;
  }
  bridge_ = std::make_unique<HttpBrowserBridge>(*browser_, settings_.bridge_token, settings_.bridge_port,
                                                phone_.get(), vision_.get());
  bridge_ok_ = bridge_->start();
  if (vision_) vision_->start();
  if (!bridge_ok_ && tray_) {
    tray_->showMessage("focusGaze", "Bridge failed to start (port busy?).", QSystemTrayIcon::Warning,
                       5000);
  }
}

void TrayController::stopBridge() {
  if (vision_) vision_->stop();
  if (bridge_) {
    bridge_->stop();
    bridge_.reset();
  }
  bridge_ok_ = false;
}

void TrayController::toggleFocus() {
  if (focus_ && focus_->isFocusOn()) turnFocusOff();
  else turnFocusOn();
}

void TrayController::turnFocusOn() {
  if (!focus_) return;
  if (focus_->turnOn()) {
    tray_->showMessage("focusGaze", "Focus Mode ON", QSystemTrayIcon::Information, 2000);
    // Auto-open camera preview when Focus starts so phone monitoring is visible.
    if (camera_preview_wanted_) showCameraPreview(true);
  }
  rebuildMenu();
  updateTrayTooltip();
}

void TrayController::turnFocusOff() {
  if (!focus_ || !storage_) return;
  std::optional<std::int64_t> sid;
  if (auto s = focus_->activeSession()) sid = s->id;
  if (focus_->turnOff()) {
    browser_->onFocusTurnedOff();
    phone_->onFocusTurnedOff();
    QString msg = "Focus Mode OFF";
    if (sid) {
      ProductivityStats stats(*storage_);
      msg += QString("\nScore: %1 / 100").arg(stats.computeSession(*sid).score, 0, 'f', 1);
    }
    tray_->showMessage("focusGaze", msg, QSystemTrayIcon::Information, 5000);
  }
  rebuildMenu();
  updateTrayTooltip();
}

void TrayController::showCameraPreview(bool show) {
  camera_preview_wanted_ = show;
  if (!show) {
    if (camera_window_) camera_window_->hide();
    rebuildMenu();
    updateTrayTooltip();
    return;
  }
  ensureCamera();
  if (!camera_window_) {
    camera_window_ = new CameraWindow();
  }
  camera_window_->show();
  camera_window_->raise();
  camera_window_->activateWindow();
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!camera_ || !camera_->isOpen()) {
    camera_window_->setHint(
        tr("Camera not open.\n\nSystem Settings → Privacy & Security → Camera\n"
           "Enable focusGaze, then Quit and reopen the app.\n\n"
           "Or run CLI: ./build/focusgaze serve"));
  }
#else
  camera_window_->setHint(tr("This build has no OpenCV/camera support."));
#endif
  rebuildMenu();
  updateTrayTooltip();
}

void TrayController::toggleCameraPreview() {
  const bool show = !(camera_window_ && camera_window_->isVisible());
  showCameraPreview(show);
}

void TrayController::showStats() {
  if (!storage_) return;
  ProductivityStats stats(*storage_);
  auto s = stats.lastSessionSummary();
  if (!s) {
    QMessageBox::information(nullptr, "focusGaze stats", "No sessions yet.");
    return;
  }
  QMessageBox::information(nullptr, "focusGaze stats",
                           QString::fromStdString(ProductivityStats::formatReport(*s)));
}

void TrayController::showStatusMessage() {
  QString body;
  body += QString("Focus: %1\n").arg(focus_ && focus_->isFocusOn() ? "ON" : "OFF");
  body += QString("Bridge: %1 (port %2)\n")
              .arg(bridge_ok_ ? "running" : "stopped")
              .arg(settings_.bridge_port);
  body += QString("Data: %1\n").arg(QString::fromStdString(PlatformPaths::dataRoot().string()));
  body += QString("Token: %1\n").arg(QString::fromStdString(settings_.bridge_token));
#if defined(FOCUSGAZE_HAS_OPENCV)
  body += QString("Camera open: %1  YOLO: %2  Preview: %3\n")
              .arg(camera_ && camera_->isOpen() ? "yes" : "no")
              .arg(camera_ && camera_->yoloReady() ? "yes" : "no")
              .arg(camera_window_ && camera_window_->isVisible() ? "shown" : "hidden");
#endif
  if (phone_) {
    const auto ph = phone_->status(wallNow());
    body += QString("Phone in-use: %1  alarm: %2  cumulative: %3s\n")
                .arg(ph.phone_visible ? "yes" : "no")
                .arg(ph.phone_alarm ? "on" : "off")
                .arg(ph.cumulative_visible_seconds);
  }
  QMessageBox::information(nullptr, "focusGaze status", body);
}

void TrayController::copyBridgeToken() {
  QGuiApplication::clipboard()->setText(QString::fromStdString(settings_.bridge_token));
  tray_->showMessage("focusGaze", "Bridge token copied.", QSystemTrayIcon::Information, 2000);
}

void TrayController::quitApp() {
  stopBridge();
  alarms_ui_.stop();
  if (camera_window_) {
    camera_window_->close();
  }
  QApplication::quit();
}

void TrayController::onTick() {
  if (!phone_ || !browser_) return;

  // Only run camera when preview is wanted (saves CPU when hidden).
  const bool want_cam = camera_preview_wanted_ || (focus_ && focus_->isFocusOn());
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (want_cam) {
    if (!camera_ || !camera_->isOpen()) {
      // Lazy-open when Focus on even if user hasn't opened preview yet
      if (focus_ && focus_->isFocusOn()) ensureCamera();
    }
    if (camera_ && camera_->isOpen()) {
      bool vis = false;
      if (camera_->pollPhoneVisible(vis)) {
        phone_->sample(wallNow(), vis);
      }
    }
  }
#endif

  const auto reasons = browser_->alarms().activeReasons();
  alarms_ui_.setActiveReasons(reasons);
  alarms_ui_.tick();

  QString banner;
  if (!reasons.empty()) {
    banner = "ALARM: ";
    for (std::size_t i = 0; i < reasons.size(); ++i) {
      if (i) banner += " + ";
      banner += QString::fromUtf8(toString(reasons[i]));
    }
  }

  // Qt camera window (not OpenCV highgui — works with QApplication on macOS).
  if (camera_preview_wanted_ && camera_window_ && camera_window_->isVisible()) {
#if defined(FOCUSGAZE_HAS_OPENCV)
    if (camera_ && camera_->isOpen()) {
      auto snap = camera_->copyDebugSnapshot();
      cv::Mat canvas;
      if (!snap.bgr.empty()) {
        canvas = snap.bgr.clone();
        for (const auto& b : snap.boxes) {
          const cv::Scalar color =
              b.in_use_candidate ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 165, 255);
          cv::rectangle(canvas, b.rect, color, 2);
        }
      }
      QString status = snap.debounced_visible ? "IN-USE (counts toward alarm)" : "idle / desk ignored";
      if (!snap.yolo_loaded) status = "YOLO not loaded";
      if (snap.raw_in_use && !snap.debounced_visible) status = "candidate (debouncing…)";
      if (!snap.raw_in_use && snap.debounced_visible) status = "clearing…";
      status += "  |  red=in-use  orange=present/desk";
      camera_window_->setFrameBgr(matBgrToQImage(canvas.empty() ? snap.bgr : canvas), status, banner);
    }
#else
    camera_window_->setHint(tr("No OpenCV in this build."));
#endif
  }

  updateTrayTooltip();
}

} // namespace focusgaze
