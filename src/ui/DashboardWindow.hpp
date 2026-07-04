#pragma once

/// @file DashboardWindow.hpp
/// In-app shell: Overview, Status, and Last session (no modal dialogs for those).

#include <QWidget>
#include <QString>
#include <vector>

class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QStackedWidget;
class QPlainTextEdit;

namespace focusgaze {

/// Live status dashboard + Status/Stats routes in one window.
class DashboardWindow : public QWidget {
  Q_OBJECT
public:
  enum class Page { Overview = 0, Status = 1, Stats = 2 };

  explicit DashboardWindow(QWidget* parent = nullptr);

public slots:
  /// Refresh overview labels from current app state.
  void setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                 bool phone_visible, bool alarm_active, const QString& alarm_text,
                 const QString& last_session_line, int camera_device_index,
                 const QString& camera_device_label);

  /// Full multi-line status for the Status page (not a dialog).
  void setStatusDetail(const QString& text);

  /// Full multi-line last-session report for the Stats page.
  void setStatsDetail(const QString& text);

  /// Populate camera device chooser (index + display name).
  void setCameraDevices(const std::vector<std::pair<int, QString>>& devices,
                        int selected_index);

  void showPage(Page page);

signals:
  void focusToggled(bool on);
  void cameraToggled(bool on);
  void cameraDeviceChanged(int device_index);
  void showCameraPreviewRequested();
  void copyTokenRequested();
  void connectBrowserRequested();
  void openExtensionStoreRequested();
  void refreshStatsRequested();

private:
  void setNavChecked(Page page);

  QStackedWidget* stack_{nullptr};
  QPushButton* nav_overview_{nullptr};
  QPushButton* nav_status_{nullptr};
  QPushButton* nav_stats_{nullptr};

  QLabel* focus_badge_{nullptr};
  QLabel* bridge_value_{nullptr};
  QLabel* camera_value_{nullptr};
  QLabel* phone_value_{nullptr};
  QLabel* alarm_value_{nullptr};
  QLabel* session_line_{nullptr};
  QPushButton* focus_btn_{nullptr};
  QCheckBox* camera_check_{nullptr};
  QComboBox* camera_combo_{nullptr};
  QPlainTextEdit* status_detail_{nullptr};
  QPlainTextEdit* stats_detail_{nullptr};
  bool focus_on_{false};
  bool suppress_camera_device_signal_{false};
};

} // namespace focusgaze
