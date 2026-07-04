#pragma once

/// @file DashboardWindow.hpp
/// Dark themed main dashboard inspired by the Stitch focusGaze design.

#include <QWidget>

class QLabel;
class QPushButton;
class QCheckBox;

namespace focusgaze {

/// Live status dashboard: focus toggle, bridge/camera/phone cards, install CTA.
class DashboardWindow : public QWidget {
  Q_OBJECT
public:
  explicit DashboardWindow(QWidget* parent = nullptr);

public slots:
  /// Refresh labels from current app state.
  void setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                 bool phone_visible, bool alarm_active, const QString& alarm_text,
                 const QString& last_session_line);

signals:
  void focusToggled(bool on);
  void cameraToggled(bool on);
  void showCameraPreviewRequested();
  void copyTokenRequested();
  void connectBrowserRequested();
  void openExtensionStoreRequested();
  void installExtensionRequested();
  void showStatsRequested();

private:
  QLabel* focus_badge_{nullptr};
  QLabel* bridge_value_{nullptr};
  QLabel* camera_value_{nullptr};
  QLabel* phone_value_{nullptr};
  QLabel* alarm_value_{nullptr};
  QLabel* session_line_{nullptr};
  QPushButton* focus_btn_{nullptr};
  QCheckBox* camera_check_{nullptr};
  bool focus_on_{false};
};

} // namespace focusgaze
