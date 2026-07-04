#include "vision/CameraPreview.hpp"

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

int CameraPreview::tick(const CameraSource* camera, const std::string& alarm_banner) {
#if defined(FOCUSGAZE_HAS_OPENCV)
  // Always pump events even without a frame so Cocoa stays happy.
  if (!camera || !camera->isOpen()) {
    if (!alarm_banner.empty()) {
      // Minimal alarm-only mat if no camera
      cv::Mat canvas(200, 800, CV_8UC3, cv::Scalar(40, 40, 200));
      cv::putText(canvas, alarm_banner.substr(0, 80), cv::Point(20, 100),
                  cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
      cv::namedWindow("focusGaze camera", cv::WINDOW_NORMAL);
      cv::imshow("focusGaze camera", canvas);
      window_open_ = true;
      return cv::waitKey(1) & 0xFF;
    }
    close();
    return 0;
  }

  auto snap = camera->copyDebugSnapshot();
  cv::Mat canvas;
  if (!snap.bgr.empty()) {
    canvas = snap.bgr.clone();
  } else {
    canvas = cv::Mat(480, 640, CV_8UC3, cv::Scalar(20, 20, 20));
  }

  for (const auto& b : snap.boxes) {
    const cv::Scalar color = b.in_use_candidate ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 165, 255);
    cv::rectangle(canvas, b.rect, color, 2);
    const std::string label =
        (b.in_use_candidate ? "in-use " : "present ") + std::to_string(b.conf).substr(0, 4);
    cv::putText(canvas, label, cv::Point(b.rect.x, std::max(12, b.rect.y - 4)),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
  }

  std::string status = snap.debounced_visible ? "IN-USE" : "idle";
  if (snap.raw_in_use && !snap.debounced_visible) status = "candidate...";
  if (!snap.raw_in_use && snap.debounced_visible) status = "clearing...";
  if (!snap.yolo_loaded) status = "YOLO off";

  cv::rectangle(canvas, cv::Rect(0, 0, canvas.cols, 36), cv::Scalar(30, 30, 30), cv::FILLED);
  cv::putText(canvas, "red=in-use  orange=desk  |  D=dismiss phone alarm", cv::Point(8, 22),
              cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);

  if (!alarm_banner.empty()) {
    cv::rectangle(canvas, cv::Rect(0, 36, canvas.cols, 50), cv::Scalar(0, 0, 180), cv::FILLED);
    cv::putText(canvas, alarm_banner.substr(0, 90), cv::Point(8, 68), cv::FONT_HERSHEY_SIMPLEX,
                0.55, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
  }

  cv::putText(canvas, status, cv::Point(8, canvas.rows - 12), cv::FONT_HERSHEY_SIMPLEX, 0.55,
              snap.debounced_visible ? cv::Scalar(0, 0, 220) : cv::Scalar(80, 200, 80), 2,
              cv::LINE_AA);

  cv::namedWindow("focusGaze camera", cv::WINDOW_NORMAL);
  if (canvas.cols > 960) {
    cv::Mat scaled;
    cv::resize(canvas, scaled, cv::Size(), 960.0 / canvas.cols, 960.0 / canvas.cols);
    cv::imshow("focusGaze camera", scaled);
  } else {
    cv::imshow("focusGaze camera", canvas);
  }
  window_open_ = true;
  return cv::waitKey(1) & 0xFF;
#else
  (void)camera;
  (void)alarm_banner;
  return 0;
#endif
}

void CameraPreview::close() {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (window_open_) {
    try {
      cv::destroyWindow("focusGaze camera");
      cv::waitKey(1);
    } catch (...) {
    }
    window_open_ = false;
  }
#endif
}

} // namespace focusgaze
