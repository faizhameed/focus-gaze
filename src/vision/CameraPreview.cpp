#include "vision/CameraPreview.hpp"

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

int CameraPreview::tick(const CameraSource* camera) {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!camera || !camera->isOpen()) {
    close();
    return 0;
  }
  auto snap = camera->copyDebugSnapshot();
  if (snap.bgr.empty()) return cv::waitKey(1) & 0xFF;

  cv::Mat canvas = snap.bgr.clone();
  for (const auto& b : snap.boxes) {
    const cv::Scalar color = b.in_use_candidate ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 165, 255);
    cv::rectangle(canvas, b.rect, color, 2);
    const std::string label =
        (b.in_use_candidate ? "in-use " : "present ") + std::to_string(b.conf).substr(0, 4);
    cv::putText(canvas, label, cv::Point(b.rect.x, std::max(12, b.rect.y - 4)),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1, cv::LINE_AA);
  }

  std::string status = snap.debounced_visible ? "IN-USE (counts toward alarm)" : "idle";
  if (snap.raw_in_use && !snap.debounced_visible) status = "candidate (debouncing on...)";
  if (!snap.raw_in_use && snap.debounced_visible) status = "clearing (phone down / still)...";
  if (!snap.yolo_loaded) status = "YOLO not loaded";

  cv::rectangle(canvas, cv::Rect(0, 0, canvas.cols, 40), cv::Scalar(30, 30, 30), cv::FILLED);
  cv::putText(canvas, "red=in-use  orange=desk/present (no alarm)  |  D=dismiss phone alarm",
              cv::Point(8, 16), cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(220, 220, 220), 1,
              cv::LINE_AA);
  cv::putText(canvas, status, cv::Point(8, 34), cv::FONT_HERSHEY_SIMPLEX, 0.5,
              snap.debounced_visible ? cv::Scalar(0, 0, 220) : cv::Scalar(80, 200, 80), 1,
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
