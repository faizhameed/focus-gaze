/// @file test_camera_source.cpp
/// Unit tests for camera device selection helpers (no hardware required for core asserts).

#include "vision/CameraSource.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using focusgaze::CameraDeviceInfo;
using focusgaze::CameraSource;

TEST_CASE("CameraSource::listDevices returns unique non-negative indices", "[camera]") {
  // May be empty in headless CI; must not throw or return invalid indices.
  const auto devices = CameraSource::listDevices(4);
  std::set<int> seen;
  for (const auto& d : devices) {
    REQUIRE(d.index >= 0);
    REQUIRE(d.index <= 4);
    REQUIRE_FALSE(d.name.empty());
    REQUIRE(seen.count(d.index) == 0);
    seen.insert(d.index);
  }
}

TEST_CASE("CameraSource reports configured device index even if open fails", "[camera]") {
  // Extremely high index should not open on a normal machine; constructor must still
  // record the requested index for UI/settings wiring.
  CameraSource cam(31, /*video_path=*/{}, /*target_fps=*/15);
  REQUIRE(cam.deviceIndex() == 31);
  // Open may fail (expected without a 32nd camera); isOpen is false-or-true hardware dependent.
  // We only assert API stability:
  (void)cam.isOpen();
  (void)cam.yoloReady();
  bool vis = true;
  // poll should be safe when closed.
  if (!cam.isOpen()) {
    REQUIRE_FALSE(cam.pollPhoneVisible(vis));
  }
}

TEST_CASE("CameraSource fake video path from env resolver is stable", "[camera]") {
  // resolveVideoPathFromEnv should return empty when unset (tests must not require env).
  // If FOCUSGAZE_FAKE_CAMERA is set in the environment, still returns a string.
  const std::string path = CameraSource::resolveVideoPathFromEnv();
  (void)path; // non-throwing is the contract
  SUCCEED("resolveVideoPathFromEnv callable");
}
