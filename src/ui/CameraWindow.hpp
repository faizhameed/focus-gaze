#pragma once

/// @file CameraWindow.hpp
/// Optional Qt preview of camera frames (phone detection runs without this window).

#include <QLabel>
#include <QWidget>
#include <QString>
#include <QCloseEvent>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace focusgaze {

class CameraWindow : public QWidget {
  Q_OBJECT
public:
  explicit CameraWindow(QWidget* parent = nullptr);

  void setFrameBgr(const QImage& image, const QString& status_line, const QString& alarm_banner);
  void setHint(const QString& text);

signals:
  /// User closed the window with the red traffic-light / X — monitoring must keep running.
  void previewClosedByUser();

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  QLabel* image_label_{nullptr};
  QLabel* status_label_{nullptr};
  QLabel* alarm_label_{nullptr};
};

#if defined(FOCUSGAZE_HAS_OPENCV)
QImage matBgrToQImage(const cv::Mat& bgr);
#endif

} // namespace focusgaze
