#include "ui/TrayController.hpp"

#include "core/PlatformPaths.hpp"
#include "ui/ChromeExtensionInstaller.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStyle>
#include <QUrl>
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
  releaseCamera();
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
  // Preview off by default — detection is headless when camera monitoring is on.
  camera_preview_wanted_ = false;

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
  rebuildMenu();
  tray_->show();
  tray_->showMessage(
      "focusGaze",
      "Focus = browser. Optional: enable Camera monitoring (runs in background). "
      "Open “Show camera preview” only if you want the video window.",
      QSystemTrayIcon::Information, 8000);

  tick_timer_ = new QTimer(this);
  connect(tick_timer_, &QTimer::timeout, this, &TrayController::onTick);
  tick_timer_->start(33);
  updateTrayTooltip();
  return true;
}

void TrayController::rebuildMenu() {
  if (!menu_) menu_ = new QMenu();
  menu_->clear();

  const bool on = focus_ && focus_->isFocusOn();
  menu_->addAction(on ? tr("Turn Focus OFF") : tr("Turn Focus ON"), this,
                   &TrayController::toggleFocus);

  auto* cam_mon = menu_->addAction(tr("Camera monitoring (phone detection)"));
  cam_mon->setCheckable(true);
  cam_mon->setChecked(settings_.camera_monitoring_enabled);
  cam_mon->setToolTip(tr(
      "Runs the webcam + YOLO in the background while Focus is ON. "
      "Does not require the preview window."));
  // Use triggered(bool) carefully — block signals when rebuilding to avoid loops
  connect(cam_mon, &QAction::toggled, this, [this](bool on) {
    if (on != settings_.camera_monitoring_enabled) setCameraMonitoring(on);
  });

  auto* cam_prev = menu_->addAction(tr("Show camera preview"));
  cam_prev->setCheckable(true);
  cam_prev->setEnabled(settings_.camera_monitoring_enabled);
  cam_prev->setChecked(settings_.camera_monitoring_enabled && camera_preview_wanted_ &&
                       camera_window_ && camera_window_->isVisible());
  cam_prev->setToolTip(tr("Optional live view with detection boxes. Closing it does not stop monitoring."));
  connect(cam_prev, &QAction::triggered, this, &TrayController::toggleCameraPreview);

  menu_->addSeparator();
  menu_->addAction(tr("Open dashboard…"), this, &TrayController::showDashboard);
  menu_->addAction(tr("Show status…"), this, &TrayController::showStatusMessage);
  menu_->addAction(tr("Last session stats…"), this, &TrayController::showStats);
  menu_->addSeparator();
  menu_->addAction(tr("Get Chrome extension…"), this, &TrayController::openExtensionInstallPage);
  menu_->addAction(tr("Connect browser (auto pair)…"), this,
                   &TrayController::connectBrowserExtension);
  menu_->addAction(tr("Copy bridge token (fallback)"), this, &TrayController::copyBridgeToken);
  menu_->addAction(tr("Dev: install to all Chrome profiles…"), this,
                   &TrayController::installChromeExtension);
  menu_->addSeparator();
  menu_->addAction(tr("Quit focusGaze"), this, &TrayController::quitApp);
  tray_->setContextMenu(menu_);
}

void TrayController::updateTrayTooltip() {
  if (!tray_ || !focus_) return;
  QString tip = focus_->isFocusOn() ? "focusGaze — Focus ON" : "focusGaze — Focus OFF";
  tip += settings_.camera_monitoring_enabled ? " | camera ON (background)" : " | camera OFF";
  if (bridge_ok_) tip += QString(" | :%1").arg(settings_.bridge_port);
  tray_->setToolTip(tip);
}

void TrayController::ensureBridge() {
  if (bridge_ && bridge_->isRunning()) {
    bridge_ok_ = true;
    return;
  }
  bridge_ = std::make_unique<HttpBrowserBridge>(*browser_, settings_.bridge_token, settings_.bridge_port,
                                                phone_.get(), vision_.get(), focus_.get());
  // One-click install from the extension popup (POST /v1/install-extension).
  bridge_->setInstallHandler([](bool relaunch) {
    return installChromeExtensionAllProfilesJson(relaunch);
  });
  bridge_->setCameraStatusProvider([this]() { return settings_.camera_monitoring_enabled; });
  bridge_ok_ = bridge_->start();
  if (vision_) vision_->start();
}

void TrayController::stopBridge() {
  if (vision_) vision_->stop();
  if (bridge_) {
    bridge_->stop();
    bridge_.reset();
  }
  bridge_ok_ = false;
}

void TrayController::ensureCamera() {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!settings_.camera_monitoring_enabled) return;
  if (camera_ && camera_->isOpen()) return;
  camera_ = std::make_unique<CameraSource>(CameraSource::resolveVideoPathFromEnv(), 15);
  if (!camera_->isOpen() && tray_) {
    tray_->showMessage("focusGaze",
                       "Camera failed to open. Check Privacy → Camera → focusGaze.",
                       QSystemTrayIcon::Warning, 6000);
  }
#endif
}

void TrayController::releaseCamera() {
  if (phone_) phone_->forceClearAlarm();
  camera_.reset();
}

void TrayController::setCameraMonitoring(bool enabled) {
  if (settings_.camera_monitoring_enabled == enabled) {
    rebuildMenu();
    return;
  }
  settings_.camera_monitoring_enabled = enabled;
  saveSettings(settings_);

  if (!enabled) {
    releaseCamera();
    camera_preview_wanted_ = false;
    if (camera_window_) camera_window_->hide();
    if (tray_) {
      tray_->showMessage("focusGaze",
                         "Camera monitoring OFF. Focus still watches the browser only.",
                         QSystemTrayIcon::Information, 3000);
    }
  } else {
    // Do NOT open preview automatically — run headless while Focus is on.
    camera_preview_wanted_ = false;
    if (focus_ && focus_->isFocusOn()) {
      ensureCamera();
    }
    if (tray_) {
      tray_->showMessage("focusGaze",
                         "Camera monitoring ON (background). Use “Show camera preview” for the video window.",
                         QSystemTrayIcon::Information, 4000);
    }
  }
  rebuildMenu();
  updateTrayTooltip();
}

void TrayController::toggleCameraMonitoring() {
  setCameraMonitoring(!settings_.camera_monitoring_enabled);
}

void TrayController::toggleFocus() {
  if (focus_ && focus_->isFocusOn()) turnFocusOff();
  else turnFocusOn();
}

void TrayController::turnFocusOn() {
  if (!focus_) return;
  if (focus_->turnOn()) {
    QString msg = "Focus ON (browser)";
    if (settings_.camera_monitoring_enabled) {
      msg += " + camera in background";
      ensureCamera();
      // Do not force-open preview window.
    }
    tray_->showMessage("focusGaze", msg, QSystemTrayIcon::Information, 3000);
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
    // Stop using camera for policy; keep device only if preview still open.
    if (!(camera_window_ && camera_window_->isVisible())) {
      releaseCamera();
    }
    QString msg = "Focus OFF";
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
  if (!settings_.camera_monitoring_enabled) {
    tray_->showMessage("focusGaze", "Enable “Camera monitoring” first.", QSystemTrayIcon::Warning,
                       3000);
    rebuildMenu();
    return;
  }
  camera_preview_wanted_ = show;
  if (!show) {
    if (camera_window_) camera_window_->hide();
    // Closing preview must NOT stop background detection while Focus + monitoring are on.
    rebuildMenu();
    updateTrayTooltip();
    return;
  }
  ensureCamera();
  if (!camera_window_) {
    camera_window_ = new CameraWindow();
    connect(camera_window_, &CameraWindow::previewClosedByUser, this, [this]() {
      camera_preview_wanted_ = false;
      rebuildMenu();
      updateTrayTooltip();
      // Detection continues if Focus ON + camera monitoring ON.
    });
  }
  camera_window_->show();
  camera_window_->raise();
  camera_window_->activateWindow();
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!camera_ || !camera_->isOpen()) {
    camera_window_->setHint(
        tr("Camera not open.\nSystem Settings → Privacy & Security → Camera → focusGaze"));
  }
#endif
  rebuildMenu();
  updateTrayTooltip();
}

void TrayController::toggleCameraPreview() {
  if (!settings_.camera_monitoring_enabled) {
    tray_->showMessage("focusGaze", "Enable “Camera monitoring” first.", QSystemTrayIcon::Warning,
                       3000);
    rebuildMenu();
    return;
  }
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
  body += QString("Camera monitoring: %1 (background)\n")
              .arg(settings_.camera_monitoring_enabled ? "ON" : "OFF");
  body += QString("Preview window: %1\n")
              .arg(camera_window_ && camera_window_->isVisible() ? "open" : "closed");
  body += QString("Bridge: %1 (port %2)\n")
              .arg(bridge_ok_ ? "running" : "stopped")
              .arg(settings_.bridge_port);
  body += QString("Token: %1\n").arg(QString::fromStdString(settings_.bridge_token));
#if defined(FOCUSGAZE_HAS_OPENCV)
  body += QString("Camera device open: %1  YOLO: %2\n")
              .arg(camera_ && camera_->isOpen() ? "yes" : "no")
              .arg(camera_ && camera_->yoloReady() ? "yes" : "no");
#endif
  if (phone_ && focus_ && focus_->isFocusOn()) {
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

void TrayController::openExtensionInstallPage() {
  // Production: set FOCUSGAZE_EXTENSION_STORE_URL to the Chrome Web Store listing.
  // Default: local bridge help page (works offline while the app is running).
  ensureBridge();
  QString url = qEnvironmentVariable("FOCUSGAZE_EXTENSION_STORE_URL");
  if (url.isEmpty() && bridge_ && bridge_->isRunning()) {
    url = QStringLiteral("http://127.0.0.1:%1/v1/install-help").arg(settings_.bridge_port);
  }
  if (url.isEmpty()) {
    url = QStringLiteral("https://chromewebstore.google.com/");
  }
  // Prefer Chrome so users land on the right browser for the extension.
  bool opened = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  opened = QProcess::startDetached("open", QStringList() << "-a"
                                                         << "Google Chrome" << url);
#endif
  if (!opened) opened = QDesktopServices::openUrl(QUrl(url));
  tray_->showMessage(
      "focusGaze",
      "Install focusGaze Bridge, then use “Connect browser (auto pair)” — no token paste needed.",
      QSystemTrayIcon::Information, 7000);
}

void TrayController::connectBrowserExtension() {
  ensureBridge();
  if (!bridge_ || !bridge_->isRunning()) {
    QMessageBox::warning(dashboard_, tr("Connect browser"),
                         tr("The local bridge is not running. Restart focusGaze and try again."));
    return;
  }
  const std::string pair_url = bridge_->createPairUrl();
  if (pair_url.empty()) {
    QMessageBox::warning(dashboard_, tr("Connect browser"),
                         tr("Could not create a pairing link (missing bridge token)."));
    return;
  }
  const QString qurl = QString::fromStdString(pair_url);
  bool opened = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  // Must open in Chrome — Safari cannot talk to the extension.
  opened = QProcess::startDetached("open", QStringList() << "-a"
                                                         << "Google Chrome" << qurl);
#elif defined(Q_OS_WIN)
  opened = QProcess::startDetached("cmd", QStringList() << "/c"
                                                        << "start"
                                                        << "chrome" << qurl);
#else
  // Linux: try google-chrome then chromium.
  opened = QProcess::startDetached("google-chrome", QStringList() << qurl);
  if (!opened) opened = QProcess::startDetached("chromium-browser", QStringList() << qurl);
  if (!opened) opened = QProcess::startDetached("chromium", QStringList() << qurl);
#endif
  if (!opened) {
    // Last resort: default browser (may fail pairing if not Chrome).
    opened = QDesktopServices::openUrl(QUrl(qurl));
  }
  if (opened) {
    tray_->showMessage(
        "focusGaze",
        "Opened pairing page in Chrome. Keep that tab open until it says Connected.",
        QSystemTrayIcon::Information, 6000);
  } else {
    QMessageBox::information(
        dashboard_, tr("Connect browser"),
        tr("Could not launch Chrome automatically.\n\nOpen this URL in Google Chrome:\n\n%1")
            .arg(qurl));
  }
}

void TrayController::showDashboard() {
  if (!dashboard_) {
    dashboard_ = new DashboardWindow();
    connect(dashboard_, &DashboardWindow::focusToggled, this, [this](bool on) {
      if (on) turnFocusOn();
      else turnFocusOff();
      refreshDashboard();
    });
    connect(dashboard_, &DashboardWindow::cameraToggled, this, [this](bool on) {
      setCameraMonitoring(on);
      refreshDashboard();
    });
    connect(dashboard_, &DashboardWindow::showCameraPreviewRequested, this, [this]() {
      showCameraPreview(true);
    });
    connect(dashboard_, &DashboardWindow::copyTokenRequested, this, &TrayController::copyBridgeToken);
    connect(dashboard_, &DashboardWindow::connectBrowserRequested, this,
            &TrayController::connectBrowserExtension);
    connect(dashboard_, &DashboardWindow::openExtensionStoreRequested, this,
            &TrayController::openExtensionInstallPage);
    connect(dashboard_, &DashboardWindow::installExtensionRequested, this,
            &TrayController::installChromeExtension);
    connect(dashboard_, &DashboardWindow::showStatsRequested, this, &TrayController::showStats);
  }
  refreshDashboard();
  dashboard_->show();
  dashboard_->raise();
  dashboard_->activateWindow();
}

void TrayController::refreshDashboard() {
  if (!dashboard_) return;
  bool phone_vis = false;
  bool alarm = false;
  QString alarm_text;
  if (phone_ && focus_ && focus_->isFocusOn()) {
    const auto ph = phone_->status(wallNow());
    phone_vis = ph.phone_visible;
  }
  if (browser_) {
    const auto st = browser_->status();
    alarm = st.alarm_active;
    if (alarm) {
      QStringList parts;
      for (const auto& r : st.alarm_reasons) parts << QString::fromStdString(r);
      alarm_text = parts.join(" + ");
    }
  }
  QString session_line = tr("No sessions yet.");
  if (storage_) {
    ProductivityStats stats(*storage_);
    if (auto s = stats.lastSessionSummary()) {
      session_line = QString::fromStdString(ProductivityStats::formatReport(*s));
    }
  }
  dashboard_->setStatus(focus_ && focus_->isFocusOn(), bridge_ok_, settings_.bridge_port,
                        settings_.camera_monitoring_enabled, phone_vis, alarm, alarm_text,
                        session_line);
}

void TrayController::installChromeExtension() {
  const auto reply = QMessageBox::question(
      dashboard_ ? static_cast<QWidget*>(dashboard_) : nullptr, tr("Install Chrome extension"),
      tr("Install focusGaze Bridge into every Chrome profile on this Mac?\n\n"
         "Chrome will quit and relaunch so the extension loads everywhere "
         "(Default, Work, Personal, …)."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (reply != QMessageBox::Yes) return;

  tray_->showMessage("focusGaze", "Installing Chrome extension for all profiles…",
                     QSystemTrayIcon::Information, 4000);
  const auto result = installChromeExtensionAllProfiles(true);
  if (result.ok) {
    QString detail = result.message;
    if (!result.profiles.empty()) {
      detail += "\n\nProfiles: ";
      for (int i = 0; i < static_cast<int>(result.profiles.size()); ++i) {
        if (i) detail += ", ";
        detail += result.profiles[static_cast<size_t>(i)];
      }
    }
    QMessageBox::information(dashboard_, tr("Chrome extension installed"), detail);
    tray_->showMessage("focusGaze", "Chrome extension installed for all profiles.",
                       QSystemTrayIcon::Information, 5000);
  } else {
    QMessageBox::warning(dashboard_, tr("Install failed"), result.message);
    tray_->showMessage("focusGaze", result.message, QSystemTrayIcon::Warning, 8000);
  }
}

void TrayController::quitApp() {
  stopBridge();
  releaseCamera();
  alarms_ui_.stop();
  if (camera_window_) camera_window_->close();
  if (dashboard_) dashboard_->close();
  QApplication::quit();
}

void TrayController::onTick() {
  if (!phone_ || !browser_) return;

  // Background phone detection: camera monitoring ON + Focus ON — no preview required.
  if (settings_.camera_monitoring_enabled && focus_ && focus_->isFocusOn()) {
#if defined(FOCUSGAZE_HAS_OPENCV)
    ensureCamera();
    if (camera_ && camera_->isOpen()) {
      bool vis = false;
      if (camera_->pollPhoneVisible(vis)) {
        phone_->sample(wallNow(), vis);
      }
    }
#endif
  }

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

  // Preview is optional UI only.
  if (settings_.camera_monitoring_enabled && camera_preview_wanted_ && camera_window_ &&
      camera_window_->isVisible()) {
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
      QString status = snap.debounced_visible ? "IN-USE" : "idle / desk";
      if (!snap.yolo_loaded) status = "YOLO not loaded";
      status += "  |  preview optional — detection runs in background";
      camera_window_->setFrameBgr(matBgrToQImage(canvas.empty() ? snap.bgr : canvas), status, banner);
    }
#endif
  }

  updateTrayTooltip();
  // Keep dashboard cards live while open (cheap label updates).
  if (dashboard_ && dashboard_->isVisible()) {
    refreshDashboard();
  }
}

} // namespace focusgaze
