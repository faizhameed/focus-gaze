#pragma once

/// @file DashboardWindow.hpp
/// Minimal app shell: Overview, Status, Statistics, Settings.

#include "core/ProductivityStats.hpp"
#include "core/Settings.hpp"

#include <QDate>
#include <QString>
#include <QWidget>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QStackedWidget;
class QPlainTextEdit;
class QSpinBox;
class QLineEdit;
class QProgressBar;
class QHBoxLayout;
class QButtonGroup;
class QDateEdit;
class QFrame;
class QToolButton;

namespace focusgaze {

struct DailyStats;

class DashboardWindow : public QWidget {
  Q_OBJECT
public:
  enum class Page { Overview = 0, Status = 1, Stats = 2, Settings = 3 };

  explicit DashboardWindow(QWidget* parent = nullptr);

public slots:
  void setStatus(bool focus_on, bool bridge_ok, int bridge_port, bool camera_on,
                 bool phone_visible, bool alarm_active, const QString& alarm_text,
                 const QString& last_session_line, int camera_device_index,
                 const QString& camera_device_label, const QString& phone_detail = {});

  void setStatusDetail(const QString& text);
  void setStatistics(const WindowStats& window, const std::vector<DailyStats>& day_chart);

  StatsWindow selectedStatsWindow() const { return stats_window_; }
  bool selectedCustomRangeEpoch(EpochSeconds& range_start,
                                EpochSeconds& range_end_exclusive) const;
  QString customRangeLabel() const;

  void setCameraDevices(const std::vector<std::pair<int, QString>>& devices, int selected_index);
  void loadSettingsForm(const Settings& s);
  Settings readSettingsForm(const Settings& base) const;
  void showPage(Page page);
  Page currentPage() const;

signals:
  void focusToggled(bool on);
  void cameraToggled(bool on);
  void cameraDeviceChanged(int device_index);
  void showCameraPreviewRequested();
  void copyTokenRequested();
  void connectBrowserRequested();
  void openExtensionStoreRequested();
  void refreshStatsRequested();
  void statsWindowChanged(focusgaze::StatsWindow window);
  void customDateRangeApplied();
  void saveSettingsRequested();
  void resetSettingsRequested();
  void testAlarmSoundRequested();

private:
  void setNavChecked(Page page);
  QWidget* buildOverview();
  QWidget* buildStatus();
  QWidget* buildStats();
  QWidget* buildSettings();
  void setDatePickerVisible(bool visible);
  void activateCustomDateRange();
  void applyCustomDateRangeFromEditors();
  void syncDateEditorsFromCustomRange();
  void onFromDateEdited(QDate d);
  void onToDateEdited(QDate d);
  void openCalendarPopupFor(bool is_from);
  void selectStatsWindow(StatsWindow w);

  QStackedWidget* stack_{nullptr};
  QPushButton* nav_overview_{nullptr};
  QPushButton* nav_status_{nullptr};
  QPushButton* nav_stats_{nullptr};
  QPushButton* nav_settings_{nullptr};

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

  // Minimal stats
  QLabel* window_label_{nullptr};
  QLabel* score_value_{nullptr};
  QLabel* duration_value_{nullptr};
  QProgressBar* bar_productive_{nullptr};
  QProgressBar* bar_unproductive_{nullptr};
  QProgressBar* bar_phone_{nullptr};
  QLabel* lbl_productive_{nullptr};
  QLabel* lbl_unproductive_{nullptr};
  QLabel* lbl_phone_{nullptr};
  QWidget* week_bars_host_{nullptr};
  QHBoxLayout* week_bars_layout_{nullptr};
  QPlainTextEdit* sessions_list_{nullptr};
  QButtonGroup* window_group_{nullptr};
  StatsWindow stats_window_{StatsWindow::LastSession};
  QPushButton* stats_custom_chip_{nullptr};
  QFrame* date_picker_panel_{nullptr};
  QDateEdit* date_from_{nullptr};
  QDateEdit* date_to_{nullptr};
  QLabel* date_range_hint_{nullptr};
  QDate custom_from_{QDate::currentDate()};
  QDate custom_to_{QDate::currentDate()};
  bool suppress_date_sync_{false};

  QCheckBox* set_resume_{nullptr};
  QCheckBox* set_open_at_login_{nullptr};
  QCheckBox* set_privacy_{nullptr};
  QCheckBox* set_alarm_sound_{nullptr};
  QComboBox* set_alarm_sound_name_{nullptr};
  QSpinBox* set_phone_threshold_{nullptr};
  QSpinBox* set_phone_window_min_{nullptr};
  QSpinBox* set_bridge_port_{nullptr};
  QSpinBox* set_camera_index_{nullptr};
  QLineEdit* set_token_{nullptr};
  QPlainTextEdit* set_blocklist_{nullptr};
  QPlainTextEdit* set_allowlist_{nullptr};

  bool focus_on_{false};
  bool suppress_camera_device_signal_{false};
};

} // namespace focusgaze
