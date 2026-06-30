#include "ui/CameraWindow.hpp"

#include <QVBoxLayout>
#include <QShowEvent>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

CameraWindow::CameraWindow(QWidget* parent) : QWidget(parent) {
  setWindowTitle(tr("focusGaze camera"));
  setMinimumSize(480, 360);
  // Stay visible while using other apps (not true system-wide always-on-top on all platforms).
  setWindowFlag(Qt::Window);
  setAttribute(Qt::WA_DeleteOnClose, false);

  auto* layout = new QVBoxLayout(this);
  alarm_label_ = new QLabel(this);
  alarm_label_->setWordWrap(true);
  alarm_label_->setStyleSheet("QLabel { background: #c0392b; color: white; padding: 8px; font-weight: bold; }");
  alarm_label_->hide();

  image_label_ = new QLabel(this);
  image_label_->setAlignment(Qt::AlignCenter);
  image_label_->setMinimumSize(320, 240);
  image_label_->setStyleSheet("QLabel { background: #111; color: #aaa; }");
  image_label_->setText(tr("Starting camera…\nGrant Camera permission to focusGaze if prompted."));

  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setStyleSheet("QLabel { padding: 4px; }");

  layout->addWidget(alarm_label_);
  layout->addWidget(image_label_, 1);
  layout->addWidget(status_label_);
}

void CameraWindow::setHint(const QString& text) {
  if (image_label_) image_label_->setText(text);
}

void CameraWindow::setFrameBgr(const QImage& image, const QString& status_line,
                              const QString& alarm_banner) {
  if (!image.isNull() && image_label_) {
    image_label_->setPixmap(QPixmap::fromImage(image).scaled(image_label_->size(), Qt::KeepAspectRatio,
                                                             Qt::SmoothTransformation));
  }
  if (status_label_) status_label_->setText(status_line);
  if (alarm_label_) {
    if (alarm_banner.isEmpty()) {
      alarm_label_->hide();
    } else {
      alarm_label_->setText(alarm_banner);
      alarm_label_->show();
    }
  }
}

#if defined(FOCUSGAZE_HAS_OPENCV)
QImage matBgrToQImage(const cv::Mat& bgr) {
  if (bgr.empty()) return {};
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  // QImage needs contiguous data; clone then copy into QImage buffer.
  QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
  return img.copy();
}
#endif

} // namespace focusgaze
