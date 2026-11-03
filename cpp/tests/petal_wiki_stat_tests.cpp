#include "test.h"
#include "test_data.h"
#include "client/ui/menu_widgets.h"
#include "client/ui/i18n.h"
#include "shared/game/config.h"
#include "server/systems/petals.h"
using namespace flix;
using namespace flix::testsupport;
namespace {
bool loadWikiStats() { std::string error; return loadContent(dataDir(),error); }
bool hasLine(std::uint16_t id,Rarity r,const std::string& text) {
    for(const auto& line:ui::petalTooltipLines(id,r,{},1)) if(line.text.find(text)!=std::string::npos || line.altText.find(text)!=std::string::npos)return true;
    return false;
}
}
TEST(wiki_stat_leaf_keeps_server_three_times_and_shows_base) {
    CHECK(loadWikiStats()); ui::setLanguage(ui::Language::Zh);
    const auto leaf=content().petalIndex("leaf");
    CHECK_NEAR(content().petalStats(leaf,Rarity::Super).passiveHealPerSecond,729,1e-8);
    CHECK_NEAR(kPlayerHealMultiplier,3,1e-12);
    CHECK(hasLine(leaf,Rarity::Super,"2187")); CHECK(hasLine(leaf,Rarity::Super,"729"));
    CHECK(hasLine(leaf,Rarity::Super,"本服治疗倍率：x3"));
}
TEST(wiki_stat_battery_and_lightning_share_damage_chain_and_description) {
    CHECK(loadWikiStats()); ui::setLanguage(ui::Language::Zh);
    for(const auto* id:{"battery","lightning"}) {
        const auto i=content().petalIndex(id); const auto s=content().petalStats(i,Rarity::Mythic);
        CHECK_NEAR(s.lightningDamagePerHit,2916,1e-8); CHECK_EQ(s.lightningBounces,7);
        CHECK_NEAR(s.lightningRange,240,1e-8); CHECK(hasLine(i,Rarity::Mythic,"基础闪电伤害：2916"));
    }
    CHECK(hasLine(content().petalIndex("battery"),Rarity::Mythic,"每轮放电次数：3"));
    CHECK(hasLine(content().petalIndex("battery"),Rarity::Mythic,"实际受伤时放电"));
    CHECK_NEAR(content().petalStats(content().petalIndex("lightning"),Rarity::Super).health,43740,1e-8);
}
TEST(wiki_stat_root_fang_clover_use_their_special_scaling) {
    CHECK(loadWikiStats());
    CHECK_NEAR(content().petalStats(content().petalIndex("root"),Rarity::Super).armorPerStack,1822.5,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("fang"),Rarity::Super).lifesteal,0.35/3,1e-10);
    CHECK_NEAR(content().petalStats(content().petalIndex("clover"),Rarity::Common).modifiers.luck,0.1,1e-10);
    CHECK_NEAR(content().petalStats(content().petalIndex("clover"),Rarity::Super).modifiers.luck,0.8,1e-10);
}
TEST(wiki_stat_all_petals_have_finite_values_and_no_phantom_luck) {
    CHECK(loadWikiStats()); ui::setLanguage(ui::Language::Zh); int checked=0;
    for(std::uint16_t i=0;i<content().petalCount();++i)for(int t=0;t<kLadderRarityCount;++t) {
        const auto r=static_cast<Rarity>(t);const auto s=content().petalStats(i,r);
        const double values[]={s.damage,s.health,s.heal,s.passiveHealPerSecond,s.armorPerStack,
            s.lifesteal,s.lightningDamagePerHit,s.flowerHealthBonus,s.maxMana,s.mana,s.requiredMana,
            s.poisonPerSecond,s.knockback,s.modifiers.luck,s.modifiers.speed,s.modifiers.maxHealth};
        for(double v:values)CHECK(std::isfinite(v));
        CHECK(!ui::petalTooltipLines(i,r,{},1).empty());
        if(s.modifiers.luck==0)CHECK(!hasLine(i,r,"幸运变化：")&&!hasLine(i,r,"幸运加成："));
        ++checked;
    }
    std::printf("  checked %d petal/tier stat and tooltip combinations\n",checked);
}

TEST(wiki_stat_moon_rock_uses_existing_loose_body_and_ring_anchor) {
    CHECK(loadWikiStats());const auto moon=content().petalIndex("moon_rock");CHECK(moon!=kInvalidIndex);
    CHECK(petalAnchorsRing(content().petal(moon)));CHECK(petalIsLooseBody(content().petal(moon)));
    Loadout loadout;loadout.slots[0].configIndex=moon;loadout.slots[0].rarity=Rarity::Common;
    loadout.slots[1].configIndex=moon;loadout.slots[1].rarity=Rarity::Super;
    CHECK_EQ(liveMoonSlotOf(content(),loadout),1);
}
TEST(wiki_stat_touch_hold_inspects_without_click_and_drag_still_cancels) {
    ui::TouchInspectDelay h;
    CHECK(!h.update(2,{10,10},1,true)); CHECK(h.update(2,{11,10},1.5,true));
    CHECK(!h.update(2,{11,10},1.6,false)); CHECK(h.consumedRelease);
    CHECK(!h.update(2,{10,10},2,true));CHECK(!h.update(2,{30,10},2.5,true));
    CHECK(!h.update(2,{30,10},2.6,false));CHECK(!h.consumedRelease);
    CHECK(!h.update(2,{10,10},3,true));CHECK(!h.update(2,{10,10},3.2,false));CHECK(!h.consumedRelease);
}

TEST(wiki_stat_rarity_exceptions_are_applied_to_real_runtime_stats) {
    CHECK(loadWikiStats());
    CHECK_EQ(content().petalStats(content().petalIndex("stinger"),Rarity::Super).count,5);
    CHECK_NEAR(content().petalStats(content().petalIndex("stinger"),Rarity::Super).health,1,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("bone"),Rarity::Super).petalArmor,21870,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("cotton"),Rarity::Super).health,1458,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("shell"),Rarity::Super).shield,5103,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("iris"),Rarity::Super).poisonPerSecond,51030,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("starfish"),Rarity::Super).conditionalHealPerSecond,1640,1e-8);
    CHECK_NEAR(content().petalStats(content().petalIndex("moon_rock"),Rarity::Super).reloadMillis,100000,1e-8);
}

TEST(wiki_stat_moon_rock_actually_spawns_and_becomes_the_ring_anchor) {
    CHECK(loadWikiStats());World world;CommandBuffer commands{world};PetalSystem petals;
    const auto player=world.create();world.add<PlayerTag>(player);
    world.add<Transform>(player,Transform{{1000,1000},0});world.add<Motion>(player);
    world.add<Body>(player,Body{25,1});world.add<Health>(player,Health{1000,1000});
    world.add<Faction>(player,Faction{Team::Players,false});world.add<PlayerProgress>(player);
    world.add<PlayerInput>(player);world.add<PlayerModifiers>(player);world.add<Loadout>(player);
    world.add<PetalRing>(player);world.add<Afflictions>(player);world.add<HitCooldowns>(player);
    auto& loadout=world.get<Loadout>(player);loadout.slots[0].configIndex=content().petalIndex("moon_rock");
    loadout.slots[1].configIndex=content().petalIndex("basic");
    for(int i=0;i<3100;++i){petals.run(world,content(),1000+i*net::kTickMillis,net::kTickSeconds,commands);commands.flush();}
    int anchors=0;Query<RingAnchor,PetalInstance> q{world};
    q.each([&](Entity e,RingAnchor&,PetalInstance& p){if(p.owner==player&&!world.has<Dead>(e))++anchors;});
    CHECK_EQ(anchors,1);
}

TEST(wiki_stat_lentil_keeps_functional_petals_excluded) {
    CHECK(loadWikiStats());
    CHECK(petalCanBeAttracted(content().petal(content().petalIndex("sawblade"))));
    for(const auto* id : {"sponge","poo","root","cotton","rose","shell","battery"})
        CHECK(!petalCanBeAttracted(content().petal(content().petalIndex(id))));
}
