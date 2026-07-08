/// @file test_stats_view_model.cpp
/// Snapshot test for the minimal Statistics presentation model.

#include "ui/StatsViewModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path);
  REQUIRE(in.good());
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

focusgaze::WindowStats sampleWindow() {
  focusgaze::WindowStats w;
  w.window = focusgaze::StatsWindow::Today;
  w.label = "Today";
  w.session_count = 1;
  w.focus_seconds = 6000; // 1h 40m
  w.productive_seconds = 3600;
  w.unproductive_seconds = 1500;
  w.phone_seconds = 600;
  w.score = 72.4;
  focusgaze::SessionStats s;
  s.session_id = 42;
  s.focus_seconds = 6000;
  s.score = 72.4;
  w.sessions.push_back(s);
  return w;
}

std::vector<focusgaze::DailyStats> sampleDays() {
  focusgaze::DailyStats a;
  a.day = "2026-07-05";
  a.focus_seconds = 1800;
  a.productive_seconds = 1400;
  a.social_seconds = 200;
  a.phone_seconds = 100;
  a.score = 80;
  focusgaze::DailyStats b;
  b.day = "2026-07-06";
  b.focus_seconds = 4200;
  b.productive_seconds = 2800;
  b.social_seconds = 900;
  b.phone_seconds = 400;
  b.score = 70;
  return {a, b};
}

} // namespace

TEST_CASE("StatsViewModel snapshot matches golden minimal layout", "[ui][snapshot]") {
  using focusgaze::buildStatsViewModel;
  const auto vm = buildStatsViewModel(sampleWindow(), sampleDays());
  const std::string snap = vm.toSnapshot();

  const std::string golden_path =
      std::string(FOCUSGAZE_TEST_SOURCE_DIR) + "/testdata/ui/stats_snapshot_golden.txt";
  const std::string golden = readFile(golden_path);
  // Normalize trailing newlines for cross-platform stability.
  auto trim_end = [](std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s + "\n";
  };
  REQUIRE(trim_end(snap) == trim_end(golden));
}

TEST_CASE("StatsViewModel empty window is dash placeholders", "[ui][snapshot]") {
  focusgaze::WindowStats empty;
  empty.label = "Last session";
  const auto vm = focusgaze::buildStatsViewModel(empty, {});
  const auto snap = vm.toSnapshot();
  REQUIRE(snap.find("score=—") != std::string::npos);
  REQUIRE(snap.find("focus=—") != std::string::npos);
  REQUIRE(snap.find("sessions=0") != std::string::npos);
}
