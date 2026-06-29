#include "core/PhonePresenceTracker.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::PhonePresenceTracker;

TEST_CASE("PhonePresenceTracker accumulates visibility in window", "[phone]") {
  PhonePresenceTracker t(60, 1800, 3);
  // visible 0..50
  for (int i = 0; i <= 50; ++i) {
    t.sample(i, true);
  }
  REQUIRE(t.cumulativeVisibleSeconds(50) == 50);
  REQUIRE_FALSE(t.shouldRaiseAlarm(50));

  // continue to 61
  for (int i = 51; i <= 61; ++i) {
    t.sample(i, true);
  }
  REQUIRE(t.cumulativeVisibleSeconds(61) == 61);
  REQUIRE(t.shouldRaiseAlarm(61));
  REQUIRE(t.shouldAlarmBeActive(61));
}

TEST_CASE("PhonePresenceTracker alarm clears after cooldown off-frame", "[phone]") {
  PhonePresenceTracker t(10, 1800, 3);
  for (int i = 0; i <= 12; ++i) {
    t.sample(i, true);
  }
  REQUIRE(t.shouldAlarmBeActive(12));

  t.sample(13, false);
  REQUIRE(t.shouldAlarmBeActive(13)); // within cooldown
  t.sample(14, false);
  t.sample(15, false);
  t.sample(16, false); // 13..16 = 3s cooldown from non_visible_since=13
  REQUIRE_FALSE(t.shouldAlarmBeActive(16));
}

TEST_CASE("PhonePresenceTracker drops samples outside rolling window", "[phone]") {
  PhonePresenceTracker t(60, 100, 3);
  for (int i = 0; i < 70; ++i) {
    t.sample(i, true);
  }
  t.sample(70, false);
  // at t=170, old visibility should be outside 100s window
  t.sample(170, false);
  REQUIRE(t.cumulativeVisibleSeconds(170) == 0);
}

TEST_CASE("PhonePresenceTracker fragmented visibility sums", "[phone]") {
  PhonePresenceTracker t(60, 1800, 3);
  // 40s on, 10s off, 30s on = 70s
  for (int i = 0; i < 40; ++i) t.sample(i, true);
  for (int i = 40; i < 50; ++i) t.sample(i, false);
  for (int i = 50; i < 80; ++i) t.sample(i, true);
  REQUIRE(t.cumulativeVisibleSeconds(80) >= 69);
  REQUIRE(t.shouldRaiseAlarm(80));
}
