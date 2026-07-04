#include "ui/CameraWindow.hpp"

#include <QVBoxLayout>
#include <QPixmap>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

CameraWindow::CameraWindow(QWidget* parent) : QWidget(parent) {
  // Stitch-aligned dark camera chrome (focusGaze design system).
  setWindowTitle(tr("focusGaze camera (optional preview)"));
  setMinimumSize(480, 360);
  setWindowFlag(Qt::Window);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setStyleSheet("QWidget { background: #0B0F14; color: #F1F5F9; }");

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);

  alarm_label_ = new QLabel(this);
  alarm_label_->setWordWrap(true);
  alarm_label_->setStyleSheet(
      "QLabel { background: #7F1D1D; color: #FEE2E2; padding: 10px 12px; font-weight: 700; "
      "border: 1px solid #F87171; border-radius: 10px; }");
  alarm_label_->hide();

  image_label_ = new QLabel(this);
  image_label_->setAlignment(Qt::AlignCenter);
  image_label_->setMinimumSize(320, 240);
  image_label_->setStyleSheet(
      "QLabel { background: #141A22; color: #94A3B8; border: 1px solid #243041; border-radius: 12px; }");
  image_label_->setText(tr("Camera preview\n(Detection runs in the background without this window.)"));

  status_label_ = new QLabel(this);
  status_label_->setWordWrap(true);
  status_label_->setStyleSheet("QLabel { padding: 4px 2px; color: #94A3B8; font-size: 12px; }");

  layout->addWidget(alarm_label_);
  layout->addWidget(image_label_, 1);
  layout->addWidget(status_label_);
}

void CameraWindow::closeEvent(QCloseEvent* event) {
  // Hide only — do not destroy; parent keeps monitoring alive.
  hide();
  emit previewClosedByUser();
  event->ignore(); // keep QWidget instance
}

void CameraWindow::setHint(const QString& text) {
  if (image_label_) image_label_->setText(text);
}

void CameraWindow::setFrameBgr(const QImage& image, const QString& status_line,
                              const QString& alarm_banner) {
  if (!image.isNull() && image_label_) {
    image_label_->setPixmap(QPixmap::fromImage(image).scaled(
        image_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
  QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
  return img.copy();
}
#endif

} // namespace focusgaze
