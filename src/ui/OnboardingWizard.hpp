#pragma once

/// @file OnboardingWizard.hpp
/// First-run wizard: welcome → permissions → install extension → pair (Phase 5).

#include <QDialog>

class QLabel;
class QStackedWidget;
class QPushButton;
class QCheckBox;

namespace focusgaze {

/// Modal first-run experience (Stitch-inspired dark wizard).
class OnboardingWizard : public QDialog {
  Q_OBJECT
public:
  explicit OnboardingWizard(QWidget* parent = nullptr);

  /// True if user finished or skipped (caller should not re-show until settings cleared).
  bool completed() const { return completed_; }

  /// User wants focusGaze to open at login (macOS login item).
  bool openAtLoginRequested() const;

private slots:
  void next();
  void back();
  void skip();
  void finish();

signals:
  void openSystemSettingsRequested();
  void openExtensionStoreRequested();
  void connectBrowserRequested();

private:
  void setStep(int step);
  QWidget* buildWelcome();
  QWidget* buildPermissions();
  QWidget* buildBrowser();
  QWidget* buildConnect();

  QStackedWidget* stack_{nullptr};
  QLabel* step_label_{nullptr};
  QPushButton* back_btn_{nullptr};
  QPushButton* next_btn_{nullptr};
  QCheckBox* open_at_login_check_{nullptr};
  int step_{0};
  bool completed_{false};
};

} // namespace focusgaze
