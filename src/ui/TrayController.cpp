#include "ui/TrayController.hpp"

#include "core/DefaultBlocklist.hpp"
#include "core/PlatformPaths.hpp"
#include "core/UrlClassifier.hpp"
#include "ui/OnboardingWizard.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QStyle>
#include <QTextStream>
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
  configureAlarmSound();
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

  // First-run product wizard (permissions → extension → pair).
  maybeRunOnboarding();
  return true;
}

void TrayController::maybeRunOnboarding() {
  if (settings_.onboarding_completed) return;
  OnboardingWizard wizard;
  connect(&wizard, &OnboardingWizard::openSystemSettingsRequested, this,
          &TrayController::openSystemSettingsPrivacy);
  connect(&wizard, &OnboardingWizard::openExtensionStoreRequested, this,
          &TrayController::openExtensionInstallPage);
  connect(&wizard, &OnboardingWizard::connectBrowserRequested, this,
          &TrayController::connectBrowserExtension);
  wizard.exec();
  if (wizard.completed()) {
    settings_.onboarding_completed = true;
    saveSettings(settings_);
  }
}

void TrayController::openSystemSettingsPrivacy() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  // Opens Privacy & Security pane (user navigates to Camera).
  QProcess::startDetached("open", QStringList() << "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera");
#else
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://support.apple.com/guide/mac-help/mh32356/mac")));
#endif
}

void TrayController::rebuildMenu() {
  if (!menu_) menu_ = new QMenu();
  menu_->clear();

  if (focus_) focus_->ensureConsistentWithStorage();
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
  menu_->addAction(tr("Connect browser…"), this, &TrayController::connectBrowserExtension);
  menu_->addAction(tr("Copy bridge token (fallback)"), this, &TrayController::copyBridgeToken);
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

bool TrayController::ensureCamera() {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!settings_.camera_monitoring_enabled) return false;
  // Re-open if device index changed or previous open failed.
  if (camera_ && camera_->isOpen() && camera_->deviceIndex() == settings_.camera_device_index) {
    return true;
  }
  camera_.reset();
  camera_ = std::make_unique<CameraSource>(settings_.camera_device_index,
                                           CameraSource::resolveVideoPathFromEnv(), 15);
  if (!camera_->isOpen()) {
    if (tray_) {
      tray_->showMessage(
          "focusGaze",
          QString("Camera index %1 failed (denied or unavailable). "
                  "Pick another device in the dashboard, or allow access in "
                  "System Settings → Privacy & Security → Camera.")
              .arg(settings_.camera_device_index),
          QSystemTrayIcon::Warning, 8000);
    }
    return false;
  }
  return true;
#else
  return false;
#endif
}

void TrayController::releaseCamera() {
  if (phone_) phone_->forceClearAlarm();
  camera_.reset();
}

void TrayController::setCameraMonitoring(bool enabled) {
  if (settings_.camera_monitoring_enabled == enabled) {
    rebuildMenu();
    refreshDashboard();
    return;
  }

  if (!enabled) {
    settings_.camera_monitoring_enabled = false;
    saveSettings(settings_);
    releaseCamera();
    camera_preview_wanted_ = false;
    if (camera_window_) camera_window_->hide();
    if (tray_) {
      tray_->showMessage("focusGaze",
                         "Camera monitoring OFF. Focus still watches the browser only.",
                         QSystemTrayIcon::Information, 3000);
    }
    rebuildMenu();
    updateTrayTooltip();
    refreshDashboard();
    return;
  }

  // Turning ON: require a working camera first. If denied (e.g. Continuity/iPhone),
  // do not leave monitoring stuck enabled.
  settings_.camera_monitoring_enabled = true;
  camera_preview_wanted_ = false;
  const bool opened = ensureCamera();
  if (!opened) {
    settings_.camera_monitoring_enabled = false;
    saveSettings(settings_);
    releaseCamera();
    if (tray_) {
      tray_->showMessage(
          "focusGaze",
          "Camera monitoring was not enabled — no frames from the selected camera. "
          "Choose another camera on Overview, or grant permission.",
          QSystemTrayIcon::Warning, 8000);
    }
    rebuildMenu();
    updateTrayTooltip();
    refreshDashboard();
    return;
  }

  saveSettings(settings_);
  if (tray_) {
    tray_->showMessage("focusGaze",
                       QString("Camera monitoring ON (device %1). Use “Show camera preview” for live view.")
                           .arg(settings_.camera_device_index),
                       QSystemTrayIcon::Information, 4000);
  }
  rebuildMenu();
  updateTrayTooltip();
  refreshDashboard();
}

void TrayController::setCameraDeviceIndex(int index) {
  if (index < 0) index = 0;
  if (settings_.camera_device_index == index) return;
  settings_.camera_device_index = index;
  saveSettings(settings_);
  // Force reopen on next ensure / immediately if monitoring is on.
  releaseCamera();
  if (settings_.camera_monitoring_enabled) {
    if (!ensureCamera()) {
      // Selected device unusable — turn monitoring off so UI matches reality.
      settings_.camera_monitoring_enabled = false;
      saveSettings(settings_);
      if (tray_) {
        tray_->showMessage("focusGaze",
                           "Selected camera failed; camera monitoring turned OFF.",
                           QSystemTrayIcon::Warning, 6000);
      }
    } else if (tray_) {
      tray_->showMessage("focusGaze",
                         QString("Using camera index %1").arg(index),
                         QSystemTrayIcon::Information, 2500);
    }
  }
  rebuildMenu();
  updateTrayTooltip();
  refreshDashboard();
}

void TrayController::toggleCameraMonitoring() {
  setCameraMonitoring(!settings_.camera_monitoring_enabled);
}

void TrayController::toggleFocus() {
  if (!focus_) return;
  // Keep memory and DB aligned before deciding on/off (fixes stuck "Turn Focus OFF").
  focus_->ensureConsistentWithStorage();
  if (focus_->isFocusOn()) turnFocusOff();
  else turnFocusOn();
}

void TrayController::turnFocusOn() {
  if (!focus_) return;
  focus_->ensureConsistentWithStorage();
  if (focus_->isFocusOn()) {
    rebuildMenu();
    updateTrayTooltip();
    refreshDashboard();
    return;
  }
  if (focus_->turnOn()) {
    QString msg = "Focus ON (browser)";
    if (settings_.camera_monitoring_enabled) {
      msg += " + camera in background";
      ensureCamera();
      // Do not force-open preview window.
    }
    if (tray_) {
      tray_->showMessage("focusGaze", msg, QSystemTrayIcon::Information, 3000);
    }
  }
  rebuildMenu();
  updateTrayTooltip();
  refreshDashboard();
}

void TrayController::turnFocusOff() {
  if (!focus_ || !storage_) return;
  focus_->ensureConsistentWithStorage();
  std::optional<std::int64_t> sid;
  if (auto s = focus_->activeSession()) sid = s->id;

  // Always attempt turnOff — clears DB open sessions even if memory was stale.
  const bool changed = focus_->turnOff();
  if (browser_) browser_->onFocusTurnedOff();
  if (phone_) phone_->onFocusTurnedOff();
  // Stop using camera for policy; keep device only if preview still open.
  if (!(camera_window_ && camera_window_->isVisible())) {
    releaseCamera();
  }

  if (changed && tray_) {
    QString msg = "Focus OFF";
    if (sid) {
      try {
        ProductivityStats stats(*storage_);
        msg += QString("\nScore: %1 / 100").arg(stats.computeSession(*sid).score, 0, 'f', 1);
      } catch (...) {
        // Never block turning focus off because of stats/reporting failures.
      }
    }
    tray_->showMessage("focusGaze", msg, QSystemTrayIcon::Information, 5000);
  }
  rebuildMenu();
  updateTrayTooltip();
  refreshDashboard();
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

QString TrayController::cameraDeviceLabel() const {
  return tr("Camera index %1").arg(settings_.camera_device_index);
}

void TrayController::populateCameraDevices() {
  if (!dashboard_) return;
  std::vector<std::pair<int, QString>> devices;
#if defined(FOCUSGAZE_HAS_OPENCV)
  for (const auto& d : CameraSource::listDevices(6)) {
    devices.emplace_back(d.index, QString::fromStdString(d.name));
  }
#endif
  // Always include the configured index even if probe failed (so user can re-try).
  bool has_sel = false;
  for (const auto& d : devices) {
    if (d.first == settings_.camera_device_index) has_sel = true;
  }
  if (!has_sel) {
    devices.emplace_back(settings_.camera_device_index,
                         tr("Camera %1 (saved — probe failed / permission denied)")
                             .arg(settings_.camera_device_index));
  }
  dashboard_->setCameraDevices(devices, settings_.camera_device_index);
}

void TrayController::refreshStatusPage() {
  if (!dashboard_) return;
  QString body;
  if (focus_) focus_->ensureConsistentWithStorage();
  body += QString("Updated: %1\n\n")
              .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
  body += QString("Focus: %1\n").arg(focus_ && focus_->isFocusOn() ? "ON" : "OFF");
  if (focus_ && focus_->activeSession()) {
    body += QString("Active session id: %1\n").arg(focus_->activeSession()->id);
  }
  body += QString("Camera monitoring: %1\n")
              .arg(settings_.camera_monitoring_enabled ? "ON" : "OFF");
  body += QString("Camera device index: %1\n").arg(settings_.camera_device_index);
  body += QString("Preview window: %1\n")
              .arg(camera_window_ && camera_window_->isVisible() ? "open" : "closed");
  body += QString("Bridge: %1 (port %2)\n")
              .arg(bridge_ok_ ? "running" : "stopped")
              .arg(settings_.bridge_port);
  body += QString("Token: %1\n\n").arg(QString::fromStdString(settings_.bridge_token));
#if defined(FOCUSGAZE_HAS_OPENCV)
  body += QString("Camera device open: %1\n")
              .arg(camera_ && camera_->isOpen() ? "yes" : "no");
  body += QString("YOLO: %1\n\n").arg(camera_ && camera_->yoloReady() ? "yes" : "no");
#else
  body += "OpenCV: not built into this binary\n\n";
#endif
  body += "— Phone (this Focus session) —\n";
  if (phone_) {
    const auto ph = phone_->status(wallNow());
    body += QString("Phone in-use right now: %1\n").arg(ph.phone_visible ? "yes" : "no");
    body += QString("Phone alarm: %1\n").arg(ph.phone_alarm ? "on" : "off");
    body += QString("Phone uses (pick-ups): %1\n").arg(ph.phone_use_count);
    body += QString("Phone intervals logged: %1\n").arg(ph.phone_intervals_logged);
    body += QString("Phone cumulative in window: %1s / %2s threshold\n")
                .arg(ph.cumulative_visible_seconds)
                .arg(ph.threshold_seconds);
    body += QString("Rolling window length: %1s\n\n").arg(ph.window_seconds);
  } else {
    body += "Phone monitor unavailable\n\n";
  }
  body += "— Browser —\n";
  if (browser_) {
    const auto st = browser_->status();
    body += QString("Alarm active: %1\n").arg(st.alarm_active ? "yes" : "no");
    if (!st.alarm_reasons.empty()) {
      body += "Alarm reasons: ";
      for (std::size_t i = 0; i < st.alarm_reasons.size(); ++i) {
        if (i) body += ", ";
        body += QString::fromStdString(st.alarm_reasons[i]);
      }
      body += "\n";
    }
    body += QString("Blocked tabs: %1\n").arg(static_cast<qulonglong>(st.blocked_tab_count));
    if (!st.last_domain.empty()) {
      body += QString("Last domain: %1 (%2)\n")
                  .arg(QString::fromStdString(st.last_domain),
                       QString::fromStdString(st.last_category));
    }
    if (!st.last_url.empty()) {
      body += QString("Last URL: %1\n").arg(QString::fromStdString(st.last_url));
    }
  }
  body += "\n(Live while this page is open · phone/browser data is recorded in the background.)";
  dashboard_->setStatusDetail(body);
}

void TrayController::refreshStatsPage() {
  if (!dashboard_ || !storage_) return;
  try {
    ProductivityStats stats(*storage_);
    // Aggregate for the chip the user selected (Last session / Today / Last 7 days / …).
    WindowStats window = stats.computeWindow(dashboard_->selectedStatsWindow());

    // While Focus is ON, fold the in-progress phone bout into live phone totals / score
    // (DB only stores completed bouts until the phone is put down).
    if (phone_ && focus_ && focus_->isFocusOn()) {
      const auto ph = phone_->status(wallNow());
      const auto open_bout = std::max<std::int64_t>(0, ph.open_bout_seconds);
      if (open_bout > 0) {
        window.phone_seconds += open_bout;
        for (auto& row : window.sessions) {
          const auto rec = storage_->getSession(row.session_id);
          if (rec && !rec->ended_at.has_value()) {
            row.phone_seconds += open_bout;
            row.score = ProductivityStats::computeScore(row);
            break;
          }
        }
        window.score = ProductivityStats::computeScoreParts(
            window.focus_seconds, window.productive_seconds, window.unproductive_seconds,
            window.phone_seconds);
      }
    }

    // Day chart uses real daily data for the selected window (Last 7 days = 7 bars, etc.).
    std::vector<DailyStats> day_chart;
    if (window.window == StatsWindow::LastSession) {
      // Single-session window: one bar for the session's calendar day (if any).
      if (window.range_start > 0) {
        day_chart = stats.daysInRange(window.range_start, window.range_end > window.range_start
                                                              ? window.range_end
                                                              : window.range_start + 1);
      }
    } else if (window.range_start > 0 && window.range_end > window.range_start) {
      day_chart = stats.daysInRange(window.range_start, window.range_end);
    } else {
      day_chart = stats.lastNDays(7);
    }

    dashboard_->setStatistics(window, day_chart);
  } catch (const std::exception& ex) {
    tray_->showMessage("focusGaze", QString("Stats error: %1").arg(ex.what()),
                       QSystemTrayIcon::Warning, 4000);
  } catch (...) {
    tray_->showMessage("focusGaze", "Failed to load session stats.", QSystemTrayIcon::Warning,
                       4000);
  }
}

void TrayController::configureAlarmSound() {
  alarms_ui_.setSoundEnabled(settings_.alarm_sound_enabled);
  const std::string path = AlarmPresenter::resolveSystemSoundPath(settings_.alarm_sound);
  // Play via QProcess on a detached process — reliable from the sound worker thread on macOS GUI apps.
  alarms_ui_.setSoundPlayer([path]() {
    if (path.empty()) return;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC) || defined(__APPLE__)
    QProcess::startDetached(QStringLiteral("afplay"),
                            QStringList() << QString::fromStdString(path));
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("powershell"),
                            QStringList()
                                << QStringLiteral("-c")
                                << QStringLiteral(
                                       "(New-Object Media.SoundPlayer "
                                       "'C:\\Windows\\Media\\Windows Exclamation.wav').PlaySync();"));
#else
    (void)path;
#endif
  });
}

void TrayController::testAlarmSound() {
  configureAlarmSound();
  if (!settings_.alarm_sound_enabled) {
    tray_->showMessage("focusGaze", "Alarm sound is disabled in Settings — enable it to hear beeps.",
                       QSystemTrayIcon::Warning, 4000);
  }
  alarms_ui_.playTestSound();
  tray_->showMessage("focusGaze", "Playing test alarm sound…", QSystemTrayIcon::Information, 2000);
}

void TrayController::applySettings(Settings next, bool restart_bridge_if_needed) {
  const bool port_changed = next.bridge_port != settings_.bridge_port;
  const bool token_changed = next.bridge_token != settings_.bridge_token;
  const bool camera_idx_changed = next.camera_device_index != settings_.camera_device_index;
  // Normalize domain lists when applying from any source.
  next.blocklist = UrlClassifier::normalizeDomainList(next.blocklist);
  next.allowlist = UrlClassifier::normalizeDomainList(next.allowlist);
  settings_ = std::move(next);
  saveSettings(settings_);
  if (focus_) focus_->setSettings(settings_);
  if (browser_) browser_->setSettings(settings_);
  if (phone_) phone_->setSettings(settings_);
  configureAlarmSound();
  if (camera_idx_changed && settings_.camera_monitoring_enabled) {
    releaseCamera();
    ensureCamera();
  }
  if (restart_bridge_if_needed && (port_changed || token_changed)) {
    stopBridge();
    ensureBridge();
  }
  if (dashboard_) dashboard_->loadSettingsForm(settings_);
  rebuildMenu();
  updateTrayTooltip();
  refreshDashboard();
}

void TrayController::saveSettingsFromDashboard() {
  if (!dashboard_) return;
  auto next = dashboard_->readSettingsForm(settings_);
  if (next.phone_threshold_seconds < 1) next.phone_threshold_seconds = 60;
  if (next.phone_window_seconds < 60) next.phone_window_seconds = 30 * 60;
  applySettings(std::move(next), true);
  tray_->showMessage("focusGaze", "Settings saved.", QSystemTrayIcon::Information, 2500);
}

void TrayController::resetSettingsFromDashboard() {
  Settings next = Settings::defaults();
  next.bridge_token = settings_.bridge_token; // keep pairing secret
  next.bridge_port = settings_.bridge_port;
  next.onboarding_completed = settings_.onboarding_completed;
  next.blocklist = seedBlocklistDomains();
  applySettings(std::move(next), false);
  if (dashboard_) dashboard_->loadSettingsForm(settings_);
  tray_->showMessage("focusGaze", "Settings reset to product defaults (token kept).",
                     QSystemTrayIcon::Information, 3000);
}

void TrayController::exportStatsCsv() {
  if (!storage_) return;
  ProductivityStats stats(*storage_);
  const auto recent = stats.recentSessions(50);
  const QString path = QFileDialog::getSaveFileName(
      dashboard_, tr("Export sessions CSV"), QStringLiteral("focusgaze-sessions.csv"),
      tr("CSV (*.csv)"));
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(dashboard_, tr("Export failed"), tr("Could not write file."));
    return;
  }
  QTextStream out(&f);
  out << QString::fromStdString(ProductivityStats::toCsvMany(recent));
  tray_->showMessage("focusGaze", "CSV exported.", QSystemTrayIcon::Information, 2000);
}

void TrayController::exportStatsJson() {
  if (!storage_) return;
  ProductivityStats stats(*storage_);
  const auto recent = stats.recentSessions(50);
  const QString path = QFileDialog::getSaveFileName(
      dashboard_, tr("Export sessions JSON"), QStringLiteral("focusgaze-sessions.json"),
      tr("JSON (*.json)"));
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(dashboard_, tr("Export failed"), tr("Could not write file."));
    return;
  }
  f.write(QByteArray::fromStdString(ProductivityStats::toJsonMany(recent)));
  tray_->showMessage("focusGaze", "JSON exported.", QSystemTrayIcon::Information, 2000);
}

void TrayController::showStats() {
  ensureDashboard();
  refreshStatsPage();
  dashboard_->showPage(DashboardWindow::Page::Stats);
  dashboard_->show();
  dashboard_->raise();
  dashboard_->activateWindow();
}

void TrayController::showStatusMessage() {
  ensureDashboard();
  refreshStatusPage();
  dashboard_->showPage(DashboardWindow::Page::Status);
  dashboard_->show();
  dashboard_->raise();
  dashboard_->activateWindow();
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

void TrayController::ensureDashboard() {
  if (dashboard_) return;
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
  connect(dashboard_, &DashboardWindow::cameraDeviceChanged, this,
          &TrayController::setCameraDeviceIndex);
  connect(dashboard_, &DashboardWindow::showCameraPreviewRequested, this, [this]() {
    showCameraPreview(true);
  });
  connect(dashboard_, &DashboardWindow::copyTokenRequested, this, &TrayController::copyBridgeToken);
  connect(dashboard_, &DashboardWindow::connectBrowserRequested, this,
          &TrayController::connectBrowserExtension);
  connect(dashboard_, &DashboardWindow::openExtensionStoreRequested, this,
          &TrayController::openExtensionInstallPage);
  connect(dashboard_, &DashboardWindow::refreshStatsRequested, this, [this]() {
    refreshStatsPage();
  });
  connect(dashboard_, &DashboardWindow::saveSettingsRequested, this,
          &TrayController::saveSettingsFromDashboard);
  connect(dashboard_, &DashboardWindow::resetSettingsRequested, this,
          &TrayController::resetSettingsFromDashboard);
  // CSV/JSON export remains available from the tray menu (exportStatsCsv / exportStatsJson).
  connect(dashboard_, &DashboardWindow::testAlarmSoundRequested, this, &TrayController::testAlarmSound);
  populateCameraDevices();
  dashboard_->loadSettingsForm(settings_);
}

void TrayController::showDashboard() {
  ensureDashboard();
  refreshDashboard();
  refreshStatusPage();
  dashboard_->showPage(DashboardWindow::Page::Overview);
  dashboard_->show();
  dashboard_->raise();
  dashboard_->activateWindow();
}

void TrayController::refreshDashboard() {
  if (!dashboard_) return;
  bool phone_vis = false;
  bool alarm = false;
  QString alarm_text;
  QString phone_card_extra;
  if (phone_) {
    const auto ph = phone_->status(wallNow());
    phone_vis = ph.phone_visible;
    phone_card_extra =
        tr("%1 uses · %2s / %3s window")
            .arg(ph.phone_use_count)
            .arg(ph.cumulative_visible_seconds)
            .arg(ph.threshold_seconds);
    if (ph.phone_alarm) phone_card_extra += tr(" · ALARM");
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
    try {
      ProductivityStats stats(*storage_);
      if (auto s = stats.lastSessionSummary()) {
        session_line =
            tr("Score %1 / 100 · focus %2 min")
                .arg(s->score, 0, 'f', 1)
                .arg(s->focus_seconds / 60.0, 0, 'f', 1);
      }
    } catch (...) {
      session_line = tr("Could not load last session.");
    }
  }
  dashboard_->setStatus(focus_ && focus_->isFocusOn(), bridge_ok_, settings_.bridge_port,
                        settings_.camera_monitoring_enabled, phone_vis, alarm, alarm_text,
                        session_line, settings_.camera_device_index, cameraDeviceLabel(),
                        phone_card_extra);
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

  // Live-refresh Status / Statistics while those pages are visible (~1 Hz; timer is ~30 Hz).
  // Recording always runs below regardless of which page is open.
  if (dashboard_ && dashboard_->isVisible()) {
    if (++stats_refresh_divider_ >= 30) {
      stats_refresh_divider_ = 0;
      const auto page = dashboard_->currentPage();
      if (page == DashboardWindow::Page::Stats) {
        refreshStatsPage();
      } else if (page == DashboardWindow::Page::Status) {
        refreshStatusPage();
      } else if (page == DashboardWindow::Page::Overview) {
        // Keep overview cards (phone / alarms) current without full stats recompute.
        refreshDashboard();
      }
    }
  } else {
    stats_refresh_divider_ = 0;
  }

  // Background phone detection: camera monitoring ON + Focus ON — no preview required.
  if (settings_.camera_monitoring_enabled && focus_ && focus_->isFocusOn()) {
#if defined(FOCUSGAZE_HAS_OPENCV)
    if (!ensureCamera()) {
      // Device lost / permission revoked mid-session — disable monitoring to match reality.
      settings_.camera_monitoring_enabled = false;
      saveSettings(settings_);
      releaseCamera();
      if (tray_) {
        tray_->showMessage("focusGaze",
                           "Camera lost or denied — camera monitoring turned OFF.",
                           QSystemTrayIcon::Warning, 5000);
      }
      rebuildMenu();
    } else if (camera_ && camera_->isOpen()) {
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
