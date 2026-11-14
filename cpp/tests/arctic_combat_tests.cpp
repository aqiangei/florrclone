#include "test.h"
#include "test_data.h"
#include "client/ui/chat_i18n.h"
#include "client/ui/content_i18n.h"
#include "client/ui/i18n.h"
#include "server/systems/combat.h"
#include "server/systems/petals.h"
#include "server/systems/loot.h"

using namespace flix;
namespace {
struct ArcticBlast {
    const ContentRegistry& registry = testsupport::shippedContent();
    World world;
    CommandBuffer commands{world};
    SpatialGrid grid;
    EventQueue events;
    PetalSystem petals;
    CombatSystem combat;
    Entity player = NULL_ENTITY;

    explicit ArcticBlast(const char* id, Rarity rarity = Rarity::Common) {
        player = world.create();
        world.add<PlayerTag>(player);
        world.add<Transform>(player, Transform{{5000,5000},0});
        world.add<Health>(player, Health{1000,1000});
        world.add<Body>(player, Body{25,1});
        world.add<Faction>(player, Faction{Team::Players,false});
        world.add<PlayerModifiers>(player);
        world.add<PetalRing>(player);
        world.add<Loadout>(player);
        equip(0, id, rarity);
        world.add<PlayerInput>(player);
        world.add<Motion>(player);
        tickPetals(0);
        tickPetals(4000);
    }

    void equip(int slot, const char* id, Rarity rarity = Rarity::Common) {
        world.get<Loadout>(player).slots[slot] = {registry.petalIndex(id), rarity};
    }

    std::vector<Entity> liveIn(int slot) {
        std::vector<Entity> result;
        for (const Entity e : world.get<Loadout>(player).spawned) {
            if (world.get<PetalInstance>(e).slot == slot) result.push_back(e);
        }
        return result;
    }

    void tickPetals(double now) {
        petals.run(world,registry,now,net::kTickSeconds,commands);
        commands.flush();
    }

    Entity mob(Vec2 at) {
        const auto e=world.create();
        world.add<MobTag>(e);
        world.add<Bounty>(e);
        world.add<Transform>(e,Transform{at,0});
        world.add<Body>(e,Body{5,1});
        world.add<Health>(e,Health{1000,1000});
        world.add<Faction>(e,Faction{Team::Hostiles,false});
        return e;
    }

    void combatTick(double now) {
        grid.clear();
        Query<Transform,Body>{world}.each([&](Entity e,Transform& t,Body& b){
            grid.insert(e,t.realm,t.position,b.radius);
        });
        combat.run(world,grid,registry,now,net::kTickSeconds,commands,events);
        commands.flush();
    }

    Vec2 armBomb() {
        const auto& ring = world.get<Loadout>(player).spawned;
        CHECK(!ring.empty());
        if (ring.empty()) return {};
        const auto petal = ring.front();
        const auto position = world.get<Transform>(petal).position;
        world.get<Health>(petal).current = 0;
        world.add<Dead>(petal);
        tickPetals(4050);
        return position;
    }
};
}

TEST(arctic_cryo_bomb_freezes_nearby_mobs_and_leaves_distant_mobs_alone) {
    ArcticBlast rig("arctic_cryo_bomb");
    const auto centre=rig.armBomb();
    const auto near=rig.mob(centre+Vec2{30,0});
    const auto far=rig.mob(centre+Vec2{200,0});
    rig.combatTick(4050);
    const auto* chill=rig.world.tryGet<Afflictions>(near);
    CHECK(chill && chill->slowFactor < 1.0);
    CHECK(!rig.world.has<Afflictions>(far));
    rig.combatTick(4150);
    chill=rig.world.tryGet<Afflictions>(near);
    CHECK(!chill || chill->slowFactor == 1.0);
}

TEST(arctic_nuke_fires_main_burst_and_three_delayed_sub_bursts) {
    ArcticBlast rig("arctic_nuke");
    const auto centre=rig.armBomb();
    const auto near=rig.mob(centre+Vec2{30,0});
    const auto far=rig.mob(centre+Vec2{200,0});
    const auto overlap=rig.mob(centre);
    rig.world.get<Body>(overlap).radius = 30.0;
    const auto firstSub=rig.mob(centre+Vec2{85,0});
    const auto secondSub=rig.mob(centre+Vec2::fromAngle(kTau/3,85));
    const auto thirdSub=rig.mob(centre+Vec2::fromAngle(2*kTau/3,85));
    rig.combatTick(4050);
    const double health=rig.world.get<Health>(near).current;
    CHECK_NEAR(health,910,1e-6);
    CHECK_NEAR(rig.world.get<Health>(far).current,1000,1e-6);
    CHECK_NEAR(rig.world.get<Health>(firstSub).current,1000,1e-6);
    rig.combatTick(4050+net::kTickMillis);
    CHECK_NEAR(rig.world.get<Health>(firstSub).current,990,1e-6);
    CHECK_NEAR(rig.world.get<Health>(secondSub).current,1000,1e-6);
    rig.combatTick(4050+2*net::kTickMillis);
    CHECK_NEAR(rig.world.get<Health>(secondSub).current,990,1e-6);
    CHECK_NEAR(rig.world.get<Health>(thirdSub).current,1000,1e-6);
    rig.combatTick(4050+3*net::kTickMillis);
    CHECK_NEAR(rig.world.get<Health>(thirdSub).current,990,1e-6);
    CHECK_NEAR(rig.world.get<Health>(overlap).current,880,1e-6);
    CHECK_NEAR(rig.world.get<Health>(far).current,1000,1e-6);
}

TEST(arctic_nuke_higher_tiers_keep_one_damage_budget_per_detonation) {
    for (const auto rarity : {Rarity::Rare, Rarity::Super, Rarity::Apex, Rarity::Ascendant}) {
        ArcticBlast rig("arctic_nuke", rarity);
        const Vec2 centre = rig.armBomb();
        const auto target = rig.mob(centre);
        rig.world.get<Body>(target).radius = 200.0;
        const double health = 1e8;
        rig.world.get<Health>(target) = Health{health, health};

        const double expected = 120.0 * petalStrengthScale(rarity);
        const int count = 3 + ladderIndex(rarity) / 2;
        for (int step = 0; step <= count; ++step) rig.combatTick(4050 + step * net::kTickMillis);
        CHECK_NEAR(health - rig.world.get<Health>(target).current, expected, 1e-6);
    }
}

TEST(arctic_splitter_stacks_rightward_and_collapses_after_removal) {
    ArcticBlast rig("basic");
    rig.equip(1, "arctic_splitter");
    rig.equip(2, "basic");
    rig.equip(3, "arctic_splitter", Rarity::Rare);
    rig.equip(4, "basic");
    for (double now=4050; now<=9000; now+=net::kTickMillis) rig.tickPetals(now);
    const auto p0=rig.liveIn(0), p2=rig.liveIn(2), p4=rig.liveIn(4);
    CHECK(p0.size()==1 && p2.size()==1 && p4.size()==1);
    if (p0.empty() || p2.empty() || p4.empty()) return;
    auto reach=[&](Entity e) {
        return (rig.world.get<Transform>(e).position-rig.world.get<Transform>(rig.player).position).length();
    };
    // The ring spring has the same steady-state radial gain at each radius.
    const double gain=reach(p0[0])/rig.world.get<PetalRing>(rig.player).radius;
    CHECK_NEAR((reach(p2[0])-reach(p0[0]))/gain,30.00847,0.1);
    CHECK_NEAR((reach(p4[0])-reach(p2[0]))/gain,32.05761,0.1);
    rig.world.get<Loadout>(rig.player).slots[1] = {};
    for (double now=9050; now<=11000; now+=net::kTickMillis) rig.tickPetals(now);
    CHECK_NEAR(reach(p2[0]),reach(p0[0]),0.1);
    CHECK_NEAR((reach(p4[0])-reach(p2[0]))/gain,32.05761,0.1);
}

TEST(arctic_fission_splits_only_the_eligible_right_slot) {
    ArcticBlast rig("arctic_fission");
    rig.equip(1, "basic");
    rig.equip(2, "basic");
    rig.tickPetals(4050);
    rig.tickPetals(8000);
    const auto split=rig.liveIn(1), ordinary=rig.liveIn(2);
    CHECK(split.size()==3 && ordinary.size()==1);
    if (split.size()!=3 || ordinary.empty()) return;
    const auto& health=rig.world.get<Health>(ordinary[0]);
    const auto& damage=rig.world.get<ContactDamage>(ordinary[0]);
    for (const Entity e:split) {
        CHECK_NEAR(rig.world.get<Health>(e).max,health.max/3,1e-6);
        CHECK_NEAR(rig.world.get<ContactDamage>(e).amount,damage.amount/3,1e-6);
    }
    rig.equip(1,"basic",Rarity::Rare);
    rig.tickPetals(8100);
    rig.tickPetals(13000);
    CHECK(rig.liveIn(1).size()==1);
}

TEST(arctic_fusion_combines_three_and_restores_separate_slots_after_swap) {
    ArcticBlast rig("arctic_fusion");
    for (int i=1; i<=4; ++i) rig.equip(i,"basic");
    rig.tickPetals(4050);
    rig.tickPetals(8000);
    const auto fused=rig.liveIn(1), ordinary=rig.liveIn(4);
    CHECK(fused.size()==1 && ordinary.size()==1);
    CHECK(rig.liveIn(2).empty() && rig.liveIn(3).empty());
    if (fused.empty() || ordinary.empty()) return;
    CHECK_NEAR(rig.world.get<Health>(fused[0]).max,3*rig.world.get<Health>(ordinary[0]).max,1e-6);
    CHECK_NEAR(rig.world.get<ContactDamage>(fused[0]).amount,3*rig.world.get<ContactDamage>(ordinary[0]).amount,1e-6);
    rig.equip(0,"basic");
    rig.tickPetals(8100);
    rig.tickPetals(13000);
    CHECK(rig.liveIn(2).size()==1 && rig.liveIn(3).size()==1);
}

TEST(arctic_plutonium_burn_lingers_and_accelerates_after_leaving_cloud) {
    ArcticBlast rig("arctic_plutonium");
    rig.tickPetals(6000);
    const auto active=rig.liveIn(0);
    CHECK(active.size()==1);
    if (active.empty()) return;
    const auto centre=rig.world.get<Transform>(active[0]).position;
    const auto near=rig.mob(centre+Vec2{80,0});
    rig.combatTick(6000);
    CHECK(rig.world.has<Burn>(near));
    CHECK(!rig.world.has<Afflictions>(near));
    rig.world.get<Transform>(near).position += Vec2{700,0};
    rig.combatTick(6500);
    const double health=rig.world.get<Health>(near).current;
    rig.combatTick(7000);
    CHECK(1000-health > 0);
    CHECK(health-rig.world.get<Health>(near).current > 1000-health);
    const double beforeExpiry=rig.world.get<Health>(near).current;
    rig.combatTick(9100);
    CHECK_NEAR(rig.world.get<Health>(near).current,beforeExpiry,1e-6);
}

TEST(arctic_nuke_kill_and_pickup_use_the_arctic_splitter_drop) {
    ArcticBlast rig("basic");
    LootSystem loot;
    std::string error;
    const bool loaded=loot.loadTables(rig.registry,testsupport::dataFile("mob_drops.json"),error);
    CHECK(loaded);
    if (!loaded) return;
    Rng rng(1447);
    rig.world.add<PlayerAccount>(rig.player, PlayerAccount{"arctic-test", "arctic-test", 77, false});
    rig.world.get<PlayerModifiers>(rig.player).magnetism = 100.0;
    const auto nuke=rig.registry.mobIndex("arctic_nuke");
    const auto splitter=rig.registry.petalIndex("arctic_splitter");
    const auto legacy=rig.registry.petalIndex("splitter");
    CHECK(nuke!=kInvalidIndex && splitter!=kInvalidIndex && legacy!=splitter);
    int pickedUp=0;
    int anyPickup=0;
    int oldItem=0;
    loot.onPickup=[&](const LootSystem::Pickup& item) {
        ++anyPickup;
        if (item.player==rig.player && item.petalIndex==splitter) pickedUp+=item.quantity;
        if (item.petalIndex==legacy) oldItem+=item.quantity;
    };
    const auto position=rig.world.get<Transform>(rig.player).position;
    for (int i=0; i<300 && pickedUp==0; ++i) {
        const auto corpse=rig.mob(position);
        rig.world.add<MobType>(corpse,MobType{nuke,Rarity::Super});
        const double now=5000+net::kTickMillis*i;
        const auto hit=rig.combat.applyDamage(rig.world,corpse,rig.player,10000,now);
        CHECK(hit.killed);
        CHECK(!rig.world.get<Bounty>(corpse).contributors.empty());
        loot.run(rig.world,rig.grid,rig.registry,rng,now,net::kTickSeconds,rig.commands,rig.events);
        rig.commands.flush();
        rig.world.destroy(corpse);
    }
    CHECK(anyPickup>0);
    CHECK(pickedUp>0);
    CHECK(oldItem==0);
}

TEST(arctic_names_descriptions_and_notice_follow_the_language_setting) {
    const auto& content = testsupport::shippedContent();
    const auto oldLanguage = ui::language();
    const std::string notice = "Frozen Mark: sent to Arctic. Use /mark-return to go back.";
    ui::setLanguage(ui::Language::Zh);
    int petals = 0;
    for (std::uint16_t i = 0; i < content.petalCount(); ++i) {
        const auto& petal = content.petal(i);
        if (petal.id.rfind("arctic_", 0) != 0) continue;
        CHECK(std::string(ui::petalNameTr(petal.id, petal.name.c_str())) != petal.name);
        CHECK(std::string(ui::petalDescTr(petal.id, petal.description.c_str())) != petal.description);
        ++petals;
    }
    CHECK_EQ(petals, 43);
    int mobs = 0;
    for (std::uint16_t i = 0; i < content.mobCount(); ++i) {
        const auto& mob = content.mob(i);
        if (mob.id.rfind("arctic_", 0) != 0) continue;
        CHECK(std::string(ui::mobNameTr(mob.id, mob.name.c_str())) != mob.name);
        CHECK(std::string(ui::mobDescTr(mob.id, mob.description.c_str())) != mob.description);
        ++mobs;
    }
    CHECK_EQ(mobs, 17);
    CHECK(ui::localizeChatText(notice) != notice);
    CHECK(std::string(ui::biomeTr("zorr_arctic", "Arctic")) != "Arctic");
    ui::setLanguage(ui::Language::En);
    CHECK_EQ(ui::localizeChatText(notice), notice);
    CHECK_EQ(std::string(ui::biomeTr("zorr_arctic", "Arctic")), "Arctic");
    const auto& nuke = content.petal(content.petalIndex("arctic_nuke"));
    CHECK_EQ(std::string(ui::petalNameTr(nuke.id, nuke.name.c_str())), "Nuke");
    CHECK(std::string(ui::petalDescTr(nuke.id, nuke.description.c_str())).find("triggers several blasts") != std::string::npos);
    ui::setLanguage(oldLanguage);
}

TEST(arctic_eggs_match_original_species_count_tier_and_hatch_delay) {
    // Zorr's public client egg definitions. Omega and Unique occupy AshFlor's
    // Eternal and Hyper rungs in the same order.
    struct Egg {
        const char* id;
        const char* mob;
        int count;
        std::array<int, kLadderRarityCount> tiers;
        std::array<int, kLadderRarityCount> hatchMillis;
    };
    constexpr Egg eggs[] = {
        {"arctic_centipede_egg", "arctic_centipede", 1, {0, 1, 2, 2, 3, 4, 5, 6, 7, 8}, {5000, 10000, 15000, 2000, 3000, 4000, 5000, 6000, 15000, 25000}},
        {"arctic_spider_cave_egg", "arctic_spider", 10, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {17500, 20000, 23000, 26000, 30000, 5000, 7000, 11000, 16000, 75000}},
        {"arctic_spider_egg", "arctic_spider", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {8000, 10000, 12000, 14000, 17500, 3000, 5000, 7500, 12500, 40000}},
        {"arctic_frost_digger_egg", "arctic_frost_digger", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {6000, 7000, 8000, 9000, 10000, 3000, 6000, 9000, 12000, 46250}},
        {"arctic_ice_cube_egg", "arctic_ice_cube", 1, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}, {3000, 4500, 6000, 8000, 10000, 14000, 4000, 7500, 12500, 35000}},
        {"arctic_ice_dragon_egg", "arctic_ice_dragon", 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {7000, 9000, 11000, 13000, 3000, 4500, 6000, 8500, 15000, 45000}},
        {"arctic_ice_hornet_egg", "arctic_ice_hornet", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {6000, 7000, 9000, 11000, 14000, 3000, 5500, 10000, 12500, 40000}},
        {"arctic_igloo_egg", "arctic_snowman", 10, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {10000, 15000, 20000, 25000, 30000, 10000, 12000, 14000, 30000, 95000}},
        {"arctic_nuke_egg", "arctic_nuke", 2, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}, {3800, 4500, 5300, 6000, 6800, 7500, 3400, 6000, 9000, 19450}},
        {"arctic_snowbush_egg", "arctic_snowbush", 1, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}, {5000, 6000, 7000, 8000, 9000, 10000, 2000, 5000, 11250, 25000}},
        {"arctic_snowman_egg", "arctic_snowman", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {2000, 3000, 5000, 8000, 13000, 2000, 4000, 8000, 16000, 40000}},
        {"arctic_spider_digger_egg", "arctic_spider_digger", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {4000, 4500, 5000, 5500, 6500, 3000, 5000, 7000, 13000, 37500}},
        {"arctic_spider_yoba_egg", "arctic_spider_yoba", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {3000, 4000, 5500, 7000, 8500, 3500, 5500, 7500, 15000, 40000}},
        {"arctic_stalagmite_egg", "arctic_stalagmite", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {1750, 2250, 3000, 4000, 6000, 3000, 3500, 4000, 7000, 27500}},
        {"arctic_tick_egg", "arctic_tick", 1, {0, 1, 2, 3, 4, 4, 5, 6, 7, 8}, {5000, 7000, 10000, 13000, 15000, 1500, 2500, 5000, 12500, 40000}},
    };
    for (const auto& egg : eggs) {
        for (int tier = 0; tier < kLadderRarityCount; ++tier) {
            const auto rarity = static_cast<Rarity>(tier);
            ArcticBlast rig(egg.id, rarity);
            const auto species = rig.registry.mobIndex(egg.mob);
            const double hatchAt = 4000 + egg.hatchMillis[tier];
            const auto countHatchlings = [&] {
                int count = 0;
                Query<Pet, MobType>{rig.world}.without<Dead>().each(
                    [&](Entity, Pet& pet, MobType& mob) {
                        if (pet.owner != rig.player || mob.configIndex != species) return;
                        CHECK_EQ(ladderIndex(mob.rarity), egg.tiers[tier]);
                        ++count;
                    });
                return count;
            };
            rig.tickPetals(hatchAt - 1);
            CHECK_EQ(countHatchlings(), 0);
            rig.tickPetals(hatchAt);
            CHECK_EQ(countHatchlings(), egg.count);
            rig.tickPetals(hatchAt + egg.hatchMillis[tier]);
            CHECK_EQ(countHatchlings(), egg.count);
            rig.world.get<Loadout>(rig.player).slots[0] = {};
            rig.tickPetals(hatchAt + 2 * egg.hatchMillis[tier]);
            CHECK_EQ(countHatchlings(), 0);
        }
    }
}

TEST(zorr_soap_expands_liquid_fields_without_scaling_damage) {
    ArcticBlast rig("basic");
    const auto sample = [&](double now, double expectedRadius) {
        rig.tickPetals(now);
        SpillSpec acid;
        acid.kind = SpillKind::Acid;
        acid.radius = 50.0;
        acid.strength = 14.0;
        acid.durationMillis = 2100.0;
        CombatSystem::createSpill(rig.world, rig.player, {5000, 5000}, Rarity::Common, acid, 1.0, 0);
        int acidFields = 0;
        Query<GroundEffect>{rig.world}.each([&](Entity, GroundEffect& field) {
            if (field.kind != GroundEffectKind::AcidSpill) return;
            ++acidFields;
            CHECK_NEAR(field.radius, expectedRadius, 1e-6);
            CHECK_NEAR(field.damagePerSecond, 14.0, 1e-6);
        });
        CHECK(acidFields == 1);
        std::vector<Entity> fields;
        Query<GroundEffect>{rig.world}.collect(fields);
        for (const Entity field : fields) rig.world.destroy(field);
    };
    sample(4050, 50.0);
    rig.equip(1, "void_soap");
    sample(4100, 60.0);
    rig.equip(2, "void_soap", Rarity::Rare);
    sample(4150, 76.0);
    rig.equip(3, "void_soap", Rarity::Super);
    sample(4200, 100.0);
    auto& slots = rig.world.get<Loadout>(rig.player).slots;
    slots[1] = slots[2] = slots[3] = {};
    sample(4250, 50.0);
    rig.equip(1, "void_soap", Rarity::Super);
    sample(4300, 81.0);
    rig.equip(1, "void_soap", Rarity::Unique);
    sample(4350, 84.0);
    rig.equip(1, "void_soap", Rarity::Apex);
    sample(4400, 87.0);
    rig.equip(1, "void_soap", Rarity::PermanentHyper);
    sample(4450, 87.0);
    rig.equip(1, "void_soap", Rarity::Ascendant);
    sample(4500, 90.0);
}

TEST(arctic_nuke_egg_apex_hatches_hyper_and_detonates_once_when_defeated) {
    ArcticBlast rig("arctic_nuke_egg", Rarity::Ascendant);
    rig.tickPetals(24000);
    std::vector<Entity> nukes;
    Query<Pet,MobType>{rig.world}.each([&](Entity e, Pet&, MobType& type) {
        CHECK(type.rarity == Rarity::Apex);
        nukes.push_back(e);
    });
    CHECK(nukes.size() == 2);
    if (nukes.size() != 2) return;

    const Vec2 centre{5400,5000};
    rig.world.get<Transform>(nukes[0]).position = centre;
    rig.world.get<Transform>(nukes[1]).position = centre + Vec2{700,0};
    const double damage = rig.world.get<ContactDamage>(nukes[0]).amount;
    const Entity killer = rig.mob(centre);
    rig.world.get<Health>(killer) = Health{1e9,1e9};
    const Entity near = rig.mob(centre + Vec2{150,0});
    const Entity far = rig.mob(centre + Vec2{200,0});
    rig.world.get<Health>(near) = Health{1e9,1e9};
    rig.world.get<Health>(far) = Health{1e9,1e9};

    const auto blasts = [&]() {
        int count = 0;
        for (const auto& event : rig.events.events())
            if (event.kind == net::EventKind::Explosion) ++count;
        return count;
    };
    rig.grid.insert(near, rig.world.get<Transform>(near).realm, centre + Vec2{150,0}, 5);
    rig.grid.insert(far, rig.world.get<Transform>(far).realm, centre + Vec2{200,0}, 5);
    rig.combat.beginTick(rig.world, rig.registry, 24000, net::kTickSeconds, rig.events);
    const auto hit = rig.combat.applyDamage(rig.world, nukes[0], killer, 1e9, 24000);
    CHECK(hit.killed);
    rig.combat.runWorldPhase(rig.world, rig.grid, rig.registry, 24000, net::kTickSeconds);
    CHECK(rig.world.has<Dead>(nukes[0]));
    CHECK_NEAR(1e9 - rig.world.get<Health>(near).current, damage, 1e-5);
    CHECK_NEAR(rig.world.get<Health>(far).current, 1e9, 1e-5);
    CHECK(blasts() == 1);
    rig.world.get<Loadout>(rig.player).slots[0] = {};
    rig.tickPetals(24050);
    rig.combatTick(24050);
    CHECK(blasts() == 1);
}

TEST(arctic_wild_nuke_blast_targets_enemy_faction) {
    ArcticBlast rig("basic");
    const Vec2 centre{5100,5000};
    const Entity nuke = rig.mob(centre);
    rig.world.add<MobType>(nuke, MobType{rig.registry.mobIndex("arctic_nuke"), Rarity::Common});
    rig.world.add<ContactDamage>(nuke, ContactDamage{26.16});
    const Entity ally = rig.mob(centre + Vec2{100,0});

    rig.world.get<Health>(rig.player) = Health{110,110};
    const auto playerAt = rig.world.get<Transform>(rig.player);
    rig.grid.insert(rig.player, playerAt.realm, playerAt.position, 25);
    const auto allyAt = rig.world.get<Transform>(ally);
    rig.grid.insert(ally, allyAt.realm, allyAt.position, 5);
    rig.combat.beginTick(rig.world, rig.registry, 24000, net::kTickSeconds, rig.events);
    CHECK(rig.combat.applyDamage(rig.world, nuke, rig.player, 1e9, 24000).killed);
    rig.combat.runWorldPhase(rig.world, rig.grid, rig.registry, 24000, net::kTickSeconds);
    CHECK_NEAR(rig.world.get<Health>(rig.player).current, 83.84, 1e-6);
    CHECK_NEAR(rig.world.get<Health>(ally).current, 1000, 1e-6);
}

TEST(zorr_mechanic_cyanide_extends_live_poison_and_keeps_credit) {
    ArcticBlast rig("void_cyanide");
    const auto live = rig.liveIn(0);
    CHECK(!live.empty());
    if (live.empty()) return;
    const Vec2 at = rig.world.get<Transform>(live[0]).position;
    const auto poisoned = rig.mob(at);
    const auto clean = rig.mob(at + Vec2{500,0});
    rig.world.add<MobType>(poisoned, MobType{rig.registry.mobIndex("ladybug"), Rarity::Common});
    rig.world.add<MobType>(clean, MobType{rig.registry.mobIndex("ladybug"), Rarity::Common});
    rig.world.add<Afflictions>(poisoned).poisonStacks.push_back({rig.player, 4, 5000});
    rig.combatTick(4050);
    const auto& stacks = rig.world.get<Afflictions>(poisoned).poisonStacks;
    CHECK(stacks.size() == 1);
    if (stacks.size() != 1) return;
    CHECK(stacks[0].source == rig.player);
    CHECK_NEAR(stacks[0].perSecond, 4, 1e-6);
    CHECK_NEAR(stacks[0].untilMillis, 5150, 1e-6);
    CHECK(!rig.world.has<Afflictions>(clean));
}

TEST(zorr_mechanic_dizzy_has_a_single_direct_contact_bonus) {
    ArcticBlast rig("void_dizzy", Rarity::Super);
    rig.equip(1, "void_dizzy", Rarity::Common);
    rig.equip(2, "wound", Rarity::Super);
    rig.petals.foldModifiers(rig.world, rig.registry);
    CHECK_NEAR(rig.world.get<PlayerModifiers>(rig.player).jurassicDirectDamageScale, 1.18, 1e-6);
    const auto& content = rig.registry.petal(rig.registry.petalIndex("void_dizzy"));
    CHECK(content.noPhysics);
}

TEST(zorr_mechanic_void_fire_lights_without_contact_damage) {
    ArcticBlast rig("void_fire");
    const auto live = rig.liveIn(0);
    CHECK(!live.empty());
    if (live.empty()) return;
    const auto mob = rig.mob(rig.world.get<Transform>(live[0]).position);
    rig.combatTick(4050);
    const auto* status = rig.world.tryGet<Afflictions>(mob);
    CHECK(status && status->burns.size() == 1);
    if (!status || status->burns.size() != 1) return;
    CHECK_NEAR(status->burns.front().untilMillis, 7050, 1e-6);
    CHECK_NEAR(status->burns.front().perSecond, 10, 1e-6);
    CHECK_NEAR(rig.world.get<Health>(mob).current, 1000, 1e-6);
    rig.combatTick(4100);
    CHECK(rig.world.get<Health>(mob).current < 1000);
}

TEST(zorr_mechanic_cryo_freezes_near_and_not_far) {
    ArcticBlast rig("void_cryo_bomb");
    const Vec2 at = rig.armBomb();
    const auto near = rig.mob(at + Vec2{30,0});
    const auto far = rig.mob(at + Vec2{200,0});
    rig.combatTick(4050);
    const auto* status = rig.world.tryGet<Afflictions>(near);
    CHECK(status);
    if (!status) return;
    CHECK(status->slowFactor < 1.0);
    CHECK(!rig.world.has<Afflictions>(far));
}

TEST(zorr_mechanic_fire_bomb_keeps_blast_and_burn_budget) {
    ArcticBlast rig("void_fire_bomb");
    const Vec2 at = rig.armBomb();
    const auto near = rig.mob(at + Vec2{30,0});
    const auto far = rig.mob(at + Vec2{200,0});
    rig.combatTick(4050);
    CHECK_NEAR(rig.world.get<Health>(near).current, 988, 1e-6);
    const auto* status = rig.world.tryGet<Afflictions>(near);
    CHECK(status);
    if (!status) return;
    CHECK_NEAR(status->burnPerSecond, 6, 1e-6);
    CHECK_NEAR(rig.world.get<Health>(far).current, 1000, 1e-6);
    CHECK(!rig.world.has<Afflictions>(far));
}

TEST(zorr_mechanic_soap_widens_pyrite_burn_while_idle) {
    for (bool soap : {false, true}) {
        ArcticBlast rig("pyrite");
        if (soap) rig.equip(1, "void_soap", Rarity::Super);
        for (int now = 4050; now <= 11000; now += 50) rig.tickPetals(now);
        std::vector<Entity> spills;
        Query<GroundEffect>{rig.world}.each([&](Entity e, GroundEffect& field) {
            if (field.kind == GroundEffectKind::LavaSpill) spills.push_back(e);
        });
        CHECK(!spills.empty());
        if (spills.empty()) continue;
        const Vec2 center = rig.world.get<Transform>(spills.back()).position;
        const Entity edge = rig.mob(center + Vec2{0, 190});
        const Entity outside = rig.mob(center + Vec2{0, 270});
        rig.combatTick(11000);
        const auto burned = [&](Entity e) {
            const auto* effects = rig.world.tryGet<Afflictions>(e);
            return effects && !effects->burns.empty();
        };
        CHECK(burned(edge) == soap);
        CHECK(!burned(outside));
    }
}

TEST(zorr_mechanic_soap_expands_blood_without_changing_shred) {
    for (bool soap : {false, true}) {
        ArcticBlast rig("blood");
        if (soap) rig.equip(1, "void_soap");
        rig.world.get<PlayerInput>(rig.player).current.flags = net::InputAttack;
        rig.tickPetals(5000);
        rig.tickPetals(6000);
        int patches = 0;
        Query<GroundEffect>{rig.world}.each([&](Entity, GroundEffect& field) {
            if (field.kind != GroundEffectKind::Blood) return;
            ++patches;
            CHECK_NEAR(field.radius, soap ? 38.4 : 32.0, 1e-6);
            CHECK_NEAR(field.armorReduction,
                       rig.registry.petalStats(rig.registry.petalIndex("blood"), Rarity::Common).armorReduction,
                       1e-6);
        });
        CHECK(patches == 1);
    }
}

TEST(zorr_mechanic_soap_applies_to_all_configured_liquid_fields) {
    ArcticBlast rig("basic");
    for (const char* id : {"acid", "lava", "oil", "hot_water", "elixir", "acid_bomb", "lava_bomb", "pyrite"}) {
        const auto& spill = rig.registry.petal(rig.registry.petalIndex(id)).spill;
        CHECK(spill.kind != SpillKind::None);
        CHECK(spill.radius > 0.0);
        for (bool soap : {false, true}) {
            if (soap) rig.equip(1, "void_soap");
            else rig.world.get<Loadout>(rig.player).slots[1] = {};
            rig.petals.foldModifiers(rig.world, rig.registry);
            CombatSystem::createSpill(rig.world, rig.player, {5000,5000}, Rarity::Common, spill, 1.0, 0);
            std::vector<Entity> fields;
            Query<GroundEffect>{rig.world}.collect(fields);
            CHECK(fields.size() == 1);
            for (const auto field : fields) {
                const auto& effect = rig.world.get<GroundEffect>(field);
                CHECK_NEAR(effect.radius, spill.radius * (soap ? 1.2 : 1.0), 1e-6);
                CHECK_NEAR(effect.damagePerSecond, spill.strength, 1e-6);
                rig.world.destroy(field);
            }
        }
    }
}

TEST(zorr_mechanic_void_opal_enables_each_eligible_tier) {
    ArcticBlast rig("void_opal", Rarity::Rare);
    rig.petals.foldModifiers(rig.world, rig.registry);
    auto tiers = rig.world.get<PlayerModifiers>(rig.player).opalTiers;
    CHECK(tiers[0] && tiers[1] && tiers[2] && !tiers[3]);
    rig.equip(0, "basic");
    rig.petals.foldModifiers(rig.world, rig.registry);
    tiers = rig.world.get<PlayerModifiers>(rig.player).opalTiers;
    CHECK(!tiers[0] && !tiers[1] && !tiers[2]);
}

TEST(zorr_mechanic_comb_applies_poison_and_burn_on_contact) {
    ArcticBlast rig("void_comb");
    rig.tickPetals(4600);
    const auto live = rig.liveIn(0);
    CHECK(!live.empty());
    if (live.empty()) return;
    const auto victim = rig.mob(rig.world.get<Transform>(live.front()).position);
    rig.combatTick(4650);
    const auto* status = rig.world.tryGet<Afflictions>(victim);
    CHECK(status && status->poisonStacks.size() == 1 && status->burns.size() == 1);
    if (!status || status->poisonStacks.empty() || status->burns.empty()) return;
    CHECK_NEAR(status->poisonStacks.front().perSecond, 12, 1e-6);
    CHECK_NEAR(status->burns.front().perSecond, 5, 1e-6);
    CHECK(status->poisonStacks.front().source == rig.player);
    CHECK(status->burns.front().owner == rig.player);
}

TEST(zorr_mechanic_mirror_and_dvd_spawn_armored) {
    for (const char* id : {"void_mirror", "void_dvd"}) {
        ArcticBlast rig(id);
        const auto live = rig.liveIn(0);
        CHECK(!live.empty());
        if (live.empty()) return;
        const auto* armor = rig.world.tryGet<Armor>(live.front());
        CHECK(armor);
        if (armor) CHECK_NEAR(armor->amount, id == std::string("void_mirror") ? 2 : 3, 1e-6);
    }
    ArcticBlast rig("void_dvd");
    const auto live = rig.liveIn(0);
    CHECK(!live.empty());
    if (live.empty()) return;
    const auto victim = rig.mob(rig.world.get<Transform>(live.front()).position);
    rig.combatTick(4050);
    const auto* status = rig.world.tryGet<Afflictions>(victim);
    CHECK(status && status->burns.size() == 1);
    if (status && !status->burns.empty()) CHECK_NEAR(status->burns.front().perSecond, 3, 1e-6);
}

TEST(zorr_mechanic_horn_and_fan_use_the_strongest_worn_bonus) {
    ArcticBlast rig("void_horn", Rarity::Super);
    rig.equip(1, "void_horn");
    rig.equip(2, "void_fan", Rarity::Rare);
    rig.equip(3, "void_fan");
    rig.petals.foldModifiers(rig.world, rig.registry);
    auto mods = rig.world.get<PlayerModifiers>(rig.player);
    CHECK_NEAR(mods.summonDamageScale, 1.124, 1e-6);
    CHECK_NEAR(mods.fireResistance, .034, 1e-6);
    const double before = rig.world.get<Health>(rig.player).current;
    rig.world.add<Afflictions>(rig.player).burns.push_back({NULL_ENTITY, NULL_ENTITY, 100, 6000});
    rig.combatTick(4050);
    CHECK_NEAR(rig.world.get<Health>(rig.player).current, before - 96.6 * net::kTickSeconds, 1e-6);
    for (int i = 0; i < 4; ++i) rig.world.get<Loadout>(rig.player).slots[i] = {};
    rig.petals.foldModifiers(rig.world, rig.registry);
    mods = rig.world.get<PlayerModifiers>(rig.player);
    CHECK_NEAR(mods.summonDamageScale, 1, 1e-6);
    CHECK_NEAR(mods.fireResistance, 0, 1e-6);
}
TEST(zorr_mechanic_horn_updates_an_existing_summon_and_restores_damage) {
    ArcticBlast rig("egg");
    for (double now : {10000.0, 20000.0, 30000.0}) rig.tickPetals(now);
    std::vector<Entity> pets;
    Query<Pet,ContactDamage>{rig.world}.collect(pets);
    CHECK(!pets.empty());
    if (pets.empty()) return;
    const auto pet = pets.front();
    const double before = rig.world.get<ContactDamage>(pet).amount;
    rig.equip(1, "void_horn", Rarity::Super);
    rig.petals.foldModifiers(rig.world, rig.registry);
    rig.tickPetals(30050);
    CHECK_NEAR(rig.world.get<ContactDamage>(pet).amount, before * 1.124, 1e-6);
    CHECK_NEAR(rig.world.get<Pet>(pet).damageTalentScale, 1.124, 1e-6);
    rig.world.get<Loadout>(rig.player).slots[1] = {};
    rig.petals.foldModifiers(rig.world, rig.registry);
    rig.tickPetals(30100);
    CHECK_NEAR(rig.world.get<ContactDamage>(pet).amount, before, 1e-6);
}

TEST(magic_bubble_bomb_contact_pushes_nearby_wild_mobs_once) {
    ArcticBlast rig("magic_bubble_bomb");
    const auto live = rig.liveIn(0);
    CHECK(!live.empty());
    if (live.empty()) return;
    auto at = rig.world.get<Transform>(live.front()).position;
    const auto allied = rig.mob(at + Vec2{1,0});
    rig.world.add<Pet>(allied, Pet{rig.player});
    rig.world.get<Faction>(allied).team = Team::Players;
    rig.tickPetals(4000);
    CHECK(!rig.world.has<Dead>(live.front()));
    at = rig.world.get<Transform>(live.front()).position;
    const auto trigger = rig.mob(at + Vec2{1,0});
    const auto near = rig.mob(at + Vec2{35,0});
    const auto far = rig.mob(at + Vec2{200,0});
    for (const auto mob : {trigger,near,far}) rig.world.add<Motion>(mob);
    rig.tickPetals(4000);
    CHECK(!rig.world.has<Knockback>(allied));
    CHECK(rig.world.has<Dead>(live.front()));
    const auto* shove = rig.world.tryGet<Knockback>(near);
    CHECK(shove && shove->impulse.x > 0);
    CHECK(!rig.world.has<Knockback>(far));
    rig.combatTick(4050);
    CHECK_NEAR(rig.world.get<Health>(near).current, 999.65, 1e-6);
    CHECK_NEAR(rig.world.get<Health>(far).current, 1000, 1e-6);
    rig.tickPetals(4100);
    rig.combatTick(4100);
    CHECK_NEAR(rig.world.get<Health>(near).current, 999.65, 1e-6);
}
TEST(magic_bubble_bomb_high_blueberries_keep_volley_rate) {
    const auto& c=testsupport::shippedContent();
    const auto berry=c.petalIndex("magic_peas");
    for(const auto r:{Rarity::Super,Rarity::Unique,Rarity::Apex,Rarity::PermanentHyper}) {
        const auto stats=c.petalStats(berry,r);
        CHECK_NEAR(stats.lightningDamagePerHit,r==Rarity::Super?262440:r==Rarity::Unique?372009:744018,1e-6);
        CHECK_NEAR(stats.reloadMillis,250,1e-6);
        CHECK(stats.count==4);
    }
    LootSystem loot;
    std::string error;
    CHECK(loot.loadTables(c,testsupport::dataFile("mob_drops.json"),error));
}

TEST(feedback_728_802_eggs_hatch_real_duplicates_and_replicate_ice_dragon_rarity) {
    for (const auto* egg : {"jurassic_ankylosaurus_egg", "arctic_ice_dragon_egg"}) {
        const bool dragon = std::string_view(egg) == "arctic_ice_dragon_egg";
        for (const auto rarity : {Rarity::Super, Rarity::Unique, Rarity::Apex,
                                  Rarity::PermanentHyper, Rarity::Ascendant}) {
            for (int slot : {0, 1}) {
                ArcticBlast rig("basic");
                auto& skills = rig.world.add<PlayerSkillTree>(rig.player).skills;
                skills.set(SkillId::Loadout, 4);
                skills.set(SkillId::Duplicator, 3);
                rig.world.get<Loadout>(rig.player).slots[0] = {};
                rig.equip(slot, egg, rarity);
                for (const double now : {5000.0, 80000.0, 140000.0, 220000.0}) rig.tickPetals(now);
                int actual = 0;
                Query<Pet, MobType, Replicated>{rig.world}.without<Dead>().each(
                    [&](Entity, Pet& pet, MobType& mob, Replicated& net) {
                        if (pet.owner != rig.player || pet.slot != slot) return;
                        ++actual;
                        if (dragon) {
                            CHECK(mob.rarity == petalGameplayRarity(rarity));
                            CHECK(net.rarity == mob.rarity);
                        }
                    });
                const int copies = slot == 0 ? 2 : 1;
                CHECK_EQ(actual, copies * (!dragon && ladderRarity(rarity) == Rarity::Apex ? 3 : 1));
            }
        }
    }
}

TEST(zorr_reference_eyeball_homes_same_or_lower_tiers_and_gunpowder_speeds_missiles) {
    for (auto rarity : {Rarity::Common, Rarity::Rare, Rarity::Epic}) {
        for (bool supports : {false, true}) {
            ArcticBlast rig("missile", rarity);
            if (supports) {
                rig.equip(1,"void_eyeball",Rarity::Rare);
                rig.equip(2,"void_gunpowder",Rarity::Rare);
            }
            rig.world.get<PlayerInput>(rig.player).current.flags = net::InputAttack;
            for (int now=4050;now<=12000;now+=50) rig.tickPetals(now);
            const auto& spec=rig.registry.petal(rig.registry.petalIndex("missile")).projectile;
            int shots=0;
            Query<Projectile,Motion>{rig.world}.each([&](Entity,Projectile& p,Motion& motion){
                if(p.petalConfigIndex!=rig.registry.petalIndex("missile")) return;
                ++shots;
                const bool guided=supports && rarity!=Rarity::Epic;
                CHECK_NEAR(p.seekRange,guided ? std::max(spec.seekRange,std::min(1200.0,spec.distance)) : spec.seekRange,1e-6);
                CHECK_NEAR(p.seekCone,guided ? kPi : spec.seekCone,1e-6);
                CHECK_NEAR(motion.velocity.length(),spec.speed*(supports ? 1.15 : 1.0),1e-6);
            });
            CHECK(shots>0);
        }
    }
}

TEST(zorr_reference_gunpowder_fivefold_contact_damage_requires_live_burn) {
    double plain=0;
    for (int burn : {0,1,2}) {
        ArcticBlast rig("void_gunpowder");
        const auto petals = rig.liveIn(0);
        CHECK(!petals.empty());
        if (petals.empty()) continue;
        const auto victim=rig.mob(rig.world.get<Transform>(petals.front()).position);
        if (burn) rig.world.ensure<Afflictions>(victim).burns.push_back(
            {rig.player,rig.player,0.001,burn==1 ? 9000.0 : 3999.0});
        rig.combatTick(4050);
        const double hit=1000.0-rig.world.get<Health>(victim).current;
        if (!burn) plain=hit;
        CHECK(plain>0);
        CHECK_NEAR(hit,plain*(burn==1 ? 5 : 1),0.002);
    }
}

TEST(zorr_reference_mask_powder_and_slime_use_the_standard_combat_stats) {
    ArcticBlast rig("void_slime", Rarity::Rare);
    rig.equip(1,"void_mask");
    rig.equip(2,"void_powder");
    rig.tickPetals(4500);
    const auto& mods=rig.world.get<PlayerModifiers>(rig.player);
    CHECK_NEAR(mods.poisonArmor,1.0,1e-6);
    CHECK_NEAR(mods.speedScale,1.04,1e-6);
    const auto slime=rig.registry.petalIndex("void_slime");
    CHECK_EQ(rig.liveIn(0).size(),std::size_t(2));
    CHECK_NEAR(rig.registry.petalStats(slime,Rarity::Rare).knockback,20.0,1e-6);
    CHECK_NEAR(rig.registry.petalStats(slime,Rarity::Apex).knockback,148.0,1e-6);
}
