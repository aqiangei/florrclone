#include "test.h"
#include "test_data.h"
#include "client/ui/menu_widgets.h"
#include "client/ui/menus.h"
#include "client/ui/i18n.h"
#include "shared/game/config.h"
using namespace flix;
TEST(mimic_tooltip_shows_eternal_cpp_stats_and_copied_reload){
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));ui::setLanguage(ui::Language::Zh);
    const auto mimic=content().petalIndex("mimic"),cpp=content().petalIndex("c++");
    CHECK(mimic!=kInvalidIndex);CHECK(cpp!=kInvalidIndex);
    for(const auto sourceRarity:{Rarity::Common,Rarity::Unique}){
        auto slotAt=[&](int i){return i==0?EquippedPetal{cpp,sourceRarity,sourceRarity}:EquippedPetal{mimic,Rarity::Unique,Rarity::Unique};};
        const auto effective=resolveEquippedPetal(content(),1,slotAt);
        CHECK_EQ(effective.configIndex,cpp);CHECK(effective.rarity==Rarity::Unique);
        const auto lines=ui::equippedPetalTooltipLines(mimic,effective,{},1.0);
        const auto stats=equippedPetalStats(content(),effective);
        CHECK(stats.damage>0);CHECK(stats.health>0);
        const auto base=content().petalStats(cpp,Rarity::Unique);
        const auto expected=ui::petalTooltipLines(cpp,Rarity::Unique,{},stats.reloadMillis/base.reloadMillis);
        CHECK_EQ(lines.size(),expected.size());CHECK(lines.front().text.find("C++")!=std::string::npos);
        for(std::size_t i=1;i<lines.size()&&i<expected.size();++i){CHECK_EQ(lines[i].text,expected[i].text);CHECK_EQ(lines[i].altText,expected[i].altText);}
        CHECK_EQ(lines.front().badges.size(),expected.front().badges.size());
        for(std::size_t i=0;i<lines.front().badges.size();++i)CHECK_EQ(lines.front().badges[i].text,expected.front().badges[i].text);
    }
}
TEST(ant_and_beetle_tooltips_match_the_buffed_summon_stats){
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));ui::setLanguage(ui::Language::En);
    for(const auto* id:{"egg","beetle_egg"})for(int tier=0;tier<kRarityCount;++tier){
        const auto rarity=clampRarity(tier);const auto index=content().petalIndex(id);const auto& config=content().petal(index);
        const auto lines=ui::petalTooltipLines(index,rarity,{},1.0);bool health=false,damage=false;
        for(const auto& line:lines){health|=line.text.find("Summon health: ")==0;damage|=line.text.find("Summon damage: ")==0;}
        CHECK(health);CHECK(damage);CHECK_NEAR(eggSummonCombatScale(config),1.5,1e-9);
    }
}

TEST(eternal_and_hyper_hammer_lightning_base_is_twelve){
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));
    for(const auto* id:{"mjolnir","timed_mjolnir"})for(const auto rarity:{Rarity::Unique,Rarity::Apex}){
        const auto stats=content().petalStats(content().petalIndex(id),rarity);
        CHECK_NEAR(stats.lightningDamagePerHit,12*petalStrengthScale(rarity),1e-6);
    }
}

TEST(cpp_uses_45_damage_32_health_and_2500ms_reload_on_every_ladder_tier){
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));ui::setLanguage(ui::Language::En);
    const auto cpp=content().petalIndex("c++"),saw=content().petalIndex("sawblade");
    for(int tier=0;tier<kLadderRarityCount;++tier){const auto rarity=static_cast<Rarity>(tier);
        const auto stats=content().petalStats(cpp,rarity),blade=content().petalStats(saw,rarity);
        CHECK_NEAR(stats.damage,45*petalStrengthScale(rarity),1e-6);
        CHECK_NEAR(stats.health,32*petalStrengthScale(rarity),1e-6);CHECK_NEAR(stats.reloadMillis,2500,1e-8);
        CHECK_NEAR(stats.damage/blade.damage,45.0/40,1e-9);CHECK_NEAR(stats.health/blade.health,32.0/25,1e-9);
        const auto lines=ui::petalTooltipLines(cpp,rarity,{},1);bool reload=false;
        for(const auto& badge:lines.front().badges)reload|=badge.text.find("2.5s")!=std::string::npos;CHECK(reload);
        const auto mimic=content().petalIndex("mimic");
        const auto copied=ui::equippedPetalTooltipLines(mimic,EquippedPetal{cpp,rarity,rarity},{},1);
        CHECK_EQ(copied.size(),lines.size());for(std::size_t i=1;i<copied.size();++i)CHECK_EQ(copied[i].altText,lines[i].altText);
    }
}

TEST(goggles_reduce_visible_width_by_twenty_percent_without_stacking){
    std::string error;CHECK(loadContent(testsupport::dataDir(),error));Profile p;p.loadout.resize(kLoadoutSlots);
    const double normal=loadoutCameraZoom(p,content());const auto goggles=content().petalIndex("paranormal_goggles");
    p.loadout[0]={goggles,Rarity::Common};CHECK_NEAR(loadoutCameraZoom(p,content()),normal*1.25,1e-9);
    p.loadout[1]={goggles,Rarity::Apex};CHECK_NEAR(loadoutCameraZoom(p,content()),normal*1.25,1e-9);
    p.loadout[0]={};p.loadout[1]={};CHECK_NEAR(loadoutCameraZoom(p,content()),normal,1e-9);
}

TEST(mimic_wraps_within_its_row_and_all_mimic_rows_terminate) {
    std::string error; CHECK(loadContent(testsupport::dataDir(),error));
    Loadout row; const auto mimic=content().petalIndex("mimic");
    const auto egg=content().petalIndex("egg");
    row.slots[0]={mimic,Rarity::Unique};
    row.slots[kLoadoutActiveSlots-1]={egg,Rarity::Super};
    auto copy=equippedPetal(content(),row,0);
    CHECK_EQ(copy.configIndex,egg); CHECK(copy.rarity==Rarity::Unique);
    CHECK(copy.reloadRarity==Rarity::Super);
    for(int i=0;i<kLoadoutActiveSlots;++i)row.slots[i]={mimic,Rarity::Unique};
    CHECK_EQ(equippedPetal(content(),row,0).configIndex,mimic);
    row.slots[kLoadoutActiveSlots-1]={};
    CHECK_EQ(equippedPetal(content(),row,0).configIndex,mimic);
}

#include "server/shop_lock.h"
TEST(shop_prices_double_even_discounted_locked_quotes_without_unlocking) {
    std::string error; CHECK(loadContent(testsupport::dataDir(),error));
    PlayerRecord record; const auto rotation=shopRotation(1234567890);
    record.extra["lockedShopRotation"]=static_cast<double>(rotation);
    captureShopLock(record,rotation);
    const auto rows=record.extra.dump();
    for(const auto& offer:lockedShopOffers(record)) {
        const double tierPrices[]={3,5,10,20,40,80,180,600};
        const double base=std::max(1.0,content().petal(offer.petalIndex).price);
        const double variety=std::max(clamp(1.0+std::log(base/20.0)*.1,.75,1.5),std::min(10.0,std::sqrt(base/20.0)));
        const double old=std::ceil(std::max(1.0,std::floor(tierPrices[rarityIndex(offer.rarity)]*variety))*1.75);
        CHECK_NEAR(offer.price,2*std::floor(old*(100-offer.discountPercent)/100),1e-9);
    }
    CHECK_EQ(record.extra.dump(),rows);
}

TEST(peas_full_volley_is_below_sawblade_with_the_same_reload) {
 std::string error;CHECK(loadContent(testsupport::dataDir(),error));
 for(int tier=0;tier<kLadderRarityCount;++tier){auto r=static_cast<Rarity>(tier);
  auto peas=content().petalStats(content().petalIndex("peas"),r),saw=content().petalStats(content().petalIndex("sawblade"),r);
  CHECK_NEAR(peas.damage*peas.count,saw.damage*.8,1e-5);
  CHECK_NEAR(peas.health*peas.count,saw.health*.8,1e-5);
  CHECK_NEAR(peas.reloadMillis,saw.reloadMillis,1e-9);
 }
}

#include "client/ui/content_i18n.h"
TEST(chinese_inventory_names_are_available_in_both_ui_languages) {
 auto previous=ui::language();
 for(auto language:{ui::Language::Zh,ui::Language::En}) {
  ui::setLanguage(language);
  CHECK_EQ(std::string(ui::petalNameZh("magic_peas")),std::string("蓝莓"));
  CHECK(std::string(ui::petalNameZh("peas")).find("豌豆")!=std::string::npos);
 }
 ui::setLanguage(previous);
}
