#include "vision/CameraPreview.hpp"

#include <string>

#if defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace focusgaze {

void CameraPreview::tick(const CameraSource* camera) {
#if defined(FOCUSGAZE_HAS_OPENCV)
  if (!camera || !camera->isOpen()) {
    close();
    return;
  }
  auto snap = camera->copyDebugSnapshot();
  if (snap.bgr.empty()) {
    return;
  }

  cv::Mat canvas = snap.bgr.clone();
  // Red rectangles (BGR: 0,0,255) on regions that triggered this frame's raw hit.
  for (const auto& r : snap.hit_rects) {
    cv::rectangle(canvas, r, cv::Scalar(0, 0, 255), 2);
  }

  // Status banner
  std::string status = snap.debounced_visible ? "ACTIVE USE (counts toward alarm)" : "idle / ignored";
  if (snap.raw_hit && !snap.debounced_visible) {
    status = "candidate (debouncing...)";
  }
  if (!snap.raw_hit && snap.debounced_visible) {
    status = "clearing...";
  }
  cv::rectangle(canvas, cv::Rect(0, 0, canvas.cols, 36), cv::Scalar(30, 30, 30), cv::FILLED);
  cv::putText(canvas, "focusGaze camera — red box = motion+shape trigger", cv::Point(8, 24),
              cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);

  const cv::Scalar badge =
      snap.debounced_visible ? cv::Scalar(0, 0, 220) : cv::Scalar(60, 160, 60);
  cv::putText(canvas, status, cv::Point(8, canvas.rows - 12), cv::FONT_HERSHEY_SIMPLEX, 0.6,
              badge, 2, cv::LINE_AA);

  cv::namedWindow("focusGaze camera", cv::WINDOW_NORMAL);
  // Keep preview manageable
  const int max_w = 960;
  if (canvas.cols > max_w) {
    cv::Mat scaled;
    const double s = static_cast<double>(max_w) / canvas.cols;
    cv::resize(canvas, scaled, cv::Size(), s, s);
    cv::imshow("focusGaze camera", scaled);
  } else {
    cv::imshow("focusGaze camera", canvas);
  }
  try {
    cv::setWindowProperty("focusGaze camera", cv::WND_PROP_TOPMOST, 0);
  } catch (...) {
  }
  cv::waitKey(1);
  window_open_ = true;
#else
  (void)camera;
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
