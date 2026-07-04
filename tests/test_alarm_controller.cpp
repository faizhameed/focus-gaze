#include "core/AlarmController.hpp"

#include <catch2/catch_test_macros.hpp>

using focusgaze::AlarmController;
using focusgaze::AlarmReason;

TEST_CASE("AlarmController social sticky until all blocked tabs cleared", "[alarm]") {
  AlarmController a;
  REQUIRE_FALSE(a.isActive());

  a.markBlockedTab("1", "instagram.com");
  REQUIRE(a.isActive());
  REQUIRE(a.isReasonActive(AlarmReason::SocialTab));
  REQUIRE(a.blockedTabs().size() == 1);

  a.markBlockedTab("2", "x.com");
  REQUIRE(a.blockedTabs().size() == 2);

  a.clearBlockedTab("1");
  REQUIRE(a.isActive());
  REQUIRE(a.blockedTabs().size() == 1);

  a.clearBlockedTab("2");
  REQUIRE_FALSE(a.isActive());
  REQUIRE_FALSE(a.isReasonActive(AlarmReason::SocialTab));
}

TEST_CASE("AlarmController clearAllSocialTabs", "[alarm]") {
  AlarmController a;
  a.markBlockedTab("1", "instagram.com");
  a.markBlockedTab("2", "tiktok.com");
  a.clearAllSocialTabs();
  REQUIRE_FALSE(a.isActive());
  REQUIRE(a.blockedTabs().empty());
}

TEST_CASE("AlarmController phone reason independent of social", "[alarm]") {
  AlarmController a;
  a.setPhoneAlarm(true);
  REQUIRE(a.isActive());
  REQUIRE(a.isReasonActive(AlarmReason::PhoneWindow));
  REQUIRE_FALSE(a.isReasonActive(AlarmReason::SocialTab));

  a.markBlockedTab("1", "instagram.com");
  REQUIRE(a.activeReasons().size() == 2);

  a.clearBlockedTab("1");
  REQUIRE(a.isReasonActive(AlarmReason::PhoneWindow));
  a.setPhoneAlarm(false);
  REQUIRE_FALSE(a.isActive());
}

TEST_CASE("AlarmController ignores empty tab id", "[alarm]") {
  AlarmController a;
  a.markBlockedTab("", "instagram.com");
  REQUIRE_FALSE(a.isActive());
}
