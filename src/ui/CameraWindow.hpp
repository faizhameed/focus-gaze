#pragma once

/// @file CameraWindow.hpp
/// Qt widget showing camera frames (avoids OpenCV highgui under QApplication on macOS).

#include <QLabel>
#include <QWidget>
#include <QString>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace focusgaze {

/// Simple always-on-top-capable preview window driven from the Qt main thread.
class CameraWindow : public QWidget {
  Q_OBJECT
public:
  explicit CameraWindow(QWidget* parent = nullptr);

  /// Update image + optional alarm banner text.
  void setFrameBgr(const QImage& image, const QString& status_line, const QString& alarm_banner);

  void setHint(const QString& text);

private:
  QLabel* image_label_{nullptr};
  QLabel* status_label_{nullptr};
  QLabel* alarm_label_{nullptr};
};

#if defined(FOCUSGAZE_HAS_OPENCV)
/// Convert BGR cv::Mat to QImage (deep copy).
QImage matBgrToQImage(const cv::Mat& bgr);
#endif

} // namespace focusgaze
