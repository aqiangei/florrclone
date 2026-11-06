#include "test.h"
#include "server/daily_drop_bonus.h"
using namespace flix;
TEST(daily_bonus_maintenance_pauses_until_next_login_without_manual_switch) {
    PlayerRecord r;const std::int64_t start=1700000000000;
    CHECK(claimDropBonus(r,start));
    auto& b=r.extra["dailyDropBonus"];
    const auto day=b["day"].asDouble();const auto streak=b["streak"].asInt();
    CHECK(pauseDropBonusForMaintenance(b,start+600000,"update-one"));
    CHECK_EQ(activeDropBonus(r,start+600000),1);
    CHECK(!pauseDropBonusForMaintenance(b,start+1200000,"update-one"));
    CHECK(resumeDropBonusAfterMaintenance(r,start+7200000));
    CHECK_EQ(b["until"].asDouble(),start+7200000+3000000);
    CHECK_EQ(b["day"].asDouble(),day);CHECK_EQ(b["streak"].asInt(),streak);
    CHECK_EQ(activeDropBonus(r,start+7200001),2);
    CHECK(!resumeDropBonusAfterMaintenance(r,start+7300000));
    CHECK_EQ(b["until"].asDouble(),start+7200000+3000000);
    CHECK_EQ(activeDropBonus(r,start+7200000+3000000),1);
    CHECK(!pauseDropBonusForMaintenance(b,start+7200000+3000000,"update-two"));
}
