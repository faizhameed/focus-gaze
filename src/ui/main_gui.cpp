/// @file main_gui.cpp
/// Phase 5 desktop entry: Qt system tray application (MACOSX_BUNDLE).

#include "ui/TrayController.hpp"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("focusGaze");
  QApplication::setOrganizationName("focusGaze");
  QApplication::setQuitOnLastWindowClosed(false);

  focusgaze::TrayController tray;
  if (!tray.initialize()) {
    QMessageBox::critical(nullptr, "focusGaze", "Failed to start focusGaze.");
    return 1;
  }
  return app.exec();
}
