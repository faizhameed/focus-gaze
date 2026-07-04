#include "vision/YoloDetector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("resolveYoloModelPath finds repo models/yolo11n.onnx when present", "[yolo]") {
  // Run from build/ — path resolution also checks CWD; set env for reliability.
  const char* forced = std::getenv("FOCUSGAZE_YOLO_MODEL");
#if defined(FOCUSGAZE_TEST_MODEL_PATH)
  if (!forced) {
    // Prefer compile-time path from CMake
    REQUIRE(std::filesystem::exists(FOCUSGAZE_TEST_MODEL_PATH));
  }
#endif
  auto p = focusgaze::resolveYoloModelPath();
  // May fail if model not in tree; still should return a path ending in yolo11n.onnx
  REQUIRE(p.filename() == "yolo11n.onnx");
}

#if defined(FOCUSGAZE_HAS_YOLO) && defined(FOCUSGAZE_HAS_OPENCV)
#include <opencv2/imgproc.hpp>

TEST_CASE("YoloDetector loads model and runs on blank image", "[yolo]") {
  focusgaze::YoloDetector det;
#if defined(FOCUSGAZE_TEST_MODEL_PATH)
  const std::filesystem::path model = FOCUSGAZE_TEST_MODEL_PATH;
#else
  const std::filesystem::path model = focusgaze::resolveYoloModelPath();
#endif
  if (!std::filesystem::exists(model)) {
    SKIP("yolo11n.onnx not available");
  }
  REQUIRE(det.load(model));
  REQUIRE(det.isLoaded());
  // Blank image → typically zero cell phones
  cv::Mat blank(480, 640, CV_8UC3, cv::Scalar(40, 40, 40));
  auto all = det.detect(blank, 0.5f, 0.45f);
  auto phones = focusgaze::YoloDetector::filterCellPhones(all, 0.5f);
  REQUIRE(phones.empty());
}

TEST_CASE("filterCellPhones keeps only class 67", "[yolo]") {
  std::vector<focusgaze::YoloDetection> all;
  focusgaze::YoloDetection a;
  a.class_id = 67;
  a.confidence = 0.9f;
  a.x1 = 0;
  a.y1 = 0;
  a.x2 = 10;
  a.y2 = 10;
  focusgaze::YoloDetection b = a;
  b.class_id = 0;
  all.push_back(a);
  all.push_back(b);
  auto phones = focusgaze::YoloDetector::filterCellPhones(all, 0.3f);
  REQUIRE(phones.size() == 1);
  REQUIRE(phones[0].class_id == 67);
}
#endif
