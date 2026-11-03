#include "test.h"
#include "server_harness.h"
#include "server/systems/petals.h"
#include "server/systems/combat.h"
#include "shared/game/absorb.h"

using namespace flix;
using namespace flix::testsupport;

TEST(reference_talents_preserve_old_indices_and_price_every_tier) {
    CHECK_EQ(static_cast<int>(SkillId::Summoner), 16);
    CHECK_EQ(static_cast<int>(SkillId::Damage), 17);
    CHECK_EQ(static_cast<int>(SkillId::Absorbing), 18);
    SkillSet skills;
    CHECK_NEAR(skills.effectScale(SkillId::Damage), 1.0, 1e-12);
    CHECK_NEAR(absorbXpScale(skills), 1.0, 1e-12);
    for (const auto id : {SkillId::Damage, SkillId::Absorbing}) {
        CHECK_EQ(skillTierCount(id), 10);
        CHECK_EQ(skillPurchaseCost(skills, id, 9), 130);
        CHECK_EQ(skillFromKey(kSkillKeys[static_cast<int>(id)]), id);
        skills.set(id, 9);
    }
    CHECK_EQ(skills.spent(), 260);
    CHECK_EQ(availableTalentPoints(200, skills), 140);
    CHECK_NEAR(skills.statScale(SkillId::Damage), 1.9, 1e-12);
    CHECK_NEAR(skills.effectScale(SkillId::Damage), 4.8, 1e-12);
    CHECK_EQ(absorbXpTotal(50 * absorbUnits(Rarity::Rare), skills), 400.0);
    skillRefundTo(skills, SkillId::Damage, -1);
    CHECK_EQ(skills.spent(), 130);
    CHECK_NEAR(skills.effectScale(SkillId::Damage), 1.0, 1e-12);
    CHECK_NEAR(absorbXpScale(skills), 8.0, 1e-12);
}

TEST(reference_talents_purchase_refund_and_reconnect_through_real_server) {
    Harness h("reference-talents", [](const std::string& path) {
        seedUser(path, "talentqa", "password7");
        Database db;
        std::string error;
        CHECK(db.load(path, error));
        auto& record = db.progress(db.findUser("talentqa")->id);
        record.totalXp = 1e15;
        record.skills.set(SkillId::Health, 2);
        db.markDirty();
        CHECK(db.save());
    }, dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    NetClient first;
    CHECK(loginAs(h, first, "talentqa", "password7"));
    const int before = first.profile().talentPoints();
    // A jump is refused even when the account has plenty of TP.
    first.requestUpgradeSkill(SkillId::Damage, 9);
    h.step(10, {&first});
    CHECK_EQ(first.profile().skills.level(SkillId::Damage), -1);
    for (const auto id : {SkillId::Damage, SkillId::Absorbing}) {
        for (int tier = 0; tier < 10; ++tier) {
            first.requestUpgradeSkill(id, tier);
            CHECK(awaitProfile(h, first, [=](const Profile& p) {
                return p.skills.level(id) == tier;
            }));
        }
    }
    CHECK_EQ(first.profile().talentPoints(), before - 260);
    first.disconnect();
    h.step(10, {});
    NetClient second;
    CHECK(loginAs(h, second, "talentqa", "password7"));
    CHECK_EQ(second.profile().skills.level(SkillId::Damage), 9);
    CHECK_EQ(second.profile().skills.level(SkillId::Absorbing), 9);
    CHECK_EQ(second.profile().skills.level(SkillId::Health), 2);
    second.requestDowngradeSkill(SkillId::Damage, 0);
    CHECK(awaitProfile(h, second, [](const Profile& p) {
        return p.skills.level(SkillId::Damage) == -1;
    }));
    CHECK_EQ(second.profile().talentPoints(), before - 130);
    second.requestDowngradeSkill(SkillId::Absorbing, 0);
    CHECK(awaitProfile(h, second, [](const Profile& p) {
        return p.skills.level(SkillId::Absorbing) == -1;
    }));
    CHECK_EQ(second.profile().talentPoints(), before);
    CHECK_EQ(second.profile().skills.level(SkillId::Health), 2);
}

TEST(reference_talents_survive_disk_reload_without_changing_old_branches) {
    HarnessFiles files("reference-talent-disk");
    seedUser(files.dbPath, "saveqa", "password7");
    std::string id, error;
    {
        Database db;
        CHECK(db.load(files.dbPath, error));
        id = db.findUser("saveqa")->id;
        auto& record = db.progress(id);
        CHECK_EQ(record.skills.level(SkillId::Damage), -1);
        CHECK_EQ(record.skills.level(SkillId::Absorbing), -1);
        record.skills.set(SkillId::Health, 3);
        record.skills.set(SkillId::SharpEdges, 5);
        record.skills.set(SkillId::Damage, 9);
        record.skills.set(SkillId::Absorbing, 8);
        record.mazeSkills.set(SkillId::Absorbing, 4);
        db.markDirty();
        CHECK(db.save());
    }
    Database reloaded;
    CHECK(reloaded.load(files.dbPath, error));
    const auto& record = reloaded.progress(id);
    CHECK_EQ(record.skills.level(SkillId::Health), 3);
    CHECK_EQ(record.skills.level(SkillId::SharpEdges), 5);
    CHECK_EQ(record.skills.level(SkillId::Damage), 9);
    CHECK_EQ(record.skills.level(SkillId::Absorbing), 8);
    CHECK_EQ(record.mazeSkills.level(SkillId::Absorbing), 4);
}

TEST(reference_damage_talent_updates_live_petals_and_body_then_refunds_cleanly) {
    std::string error;
    CHECK(loadContent(dataDir(), error));
    World world;
    CommandBuffer commands{world};
    PetalSystem petals;
    const auto player = world.create();
    world.add<PlayerTag>(player);
    world.add<Transform>(player, Transform{{10000, 10000}, 0});
    world.add<Motion>(player);
    world.add<Body>(player, Body{25, 1});
    world.add<Health>(player, Health{1000, 1000});
    world.add<Faction>(player, Faction{Team::Players, false});
    world.add<PlayerProgress>(player);
    world.add<PlayerInput>(player);
    world.add<PlayerModifiers>(player);
    world.add<PetalRing>(player);
    world.add<Afflictions>(player);
    world.add<HitCooldowns>(player);
    world.add<PlayerSkillTree>(player);
    auto& slot = world.add<Loadout>(player).slots[0];
    slot.configIndex = content().petalIndex("basic");
    slot.rarity = Rarity::Common;
    double now = 1000;
    auto tick = [&](int count) {
        for (int n = 0; n < count; ++n) {
            now += net::kTickMillis;
            petals.run(world, content(), now, net::kTickSeconds, commands);
            commands.flush();
        }
    };
    auto petalDamage = [&]() {
        Vec2 at;
        bool found = false;
        Query<PetalInstance, Transform> query{world};
        query.each([&](Entity, PetalInstance& p, Transform& transform) {
            if (p.owner == player && p.slot == 0) { at = transform.position; found = true; }
        });
        CHECK(found);
        const auto victim = world.create();
        world.add<MobTag>(victim);
        world.add<Transform>(victim, Transform{at, 0});
        world.add<Body>(victim, Body{2, 1});
        world.add<Health>(victim, Health{100000, 100000});
        world.add<Faction>(victim, Faction{Team::Hostiles, false});
        SpatialGrid grid;
        Query<Transform, Body> bodies{world};
        bodies.each([&](Entity e, Transform& t, Body& b) {
            grid.insert(e, t.realm, t.position, b.radius);
        });
        CombatSystem combat;
        EventQueue events;
        combat.run(world, grid, content(), now, net::kTickSeconds, commands, events);
        const double damage = 100000 - world.get<Health>(victim).current;
        world.destroy(victim);
        return damage;
    };
    tick(400);
    const double base = petalDamage();
    CHECK(base > 0);
    world.get<PlayerSkillTree>(player).skills.set(SkillId::SharpEdges, 0);
    world.get<PlayerSkillTree>(player).skills.set(SkillId::Damage, 9);
    tick(60); // clear the previous petal swing's source cooldown
    CHECK_NEAR(petalDamage(), base * 4.8, 1e-8);
    CHECK_NEAR(world.get<PlayerModifiers>(player).damageScale, 1.38 * 1.9, 1e-12);
    skillRefundTo(world.get<PlayerSkillTree>(player).skills, SkillId::Damage, -1);
    tick(60);
    CHECK_NEAR(petalDamage(), base, 1e-8);
    CHECK_NEAR(world.get<PlayerModifiers>(player).damageScale, 1.38, 1e-12);
}

TEST(reference_absorption_talent_preview_matches_server_and_protects_hyper) {
    Harness h("reference-absorption", [](const std::string& path) {
        seedUser(path, "absorbqa", "password7");
        seedStack(path, "absorbqa", "petal_rose", Rarity::Rare, 50);
        seedStack(path, "absorbqa", "petal_rose", Rarity::Apex, 1);
        Database db;
        std::string error;
        CHECK(db.load(path, error));
        auto& record = db.progress(db.findUser("absorbqa")->id);
        record.skills.set(SkillId::Absorbing, 9);
        db.markDirty();
        CHECK(db.save());
    }, dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    NetClient client;
    CHECK(loginAs(h, client, "absorbqa", "password7"));
    const auto rose = content().petalIndex("rose");
    const double before = client.profile().totalXp;
    const double preview = absorbXpTotal(50 * absorbUnits(Rarity::Rare), client.profile().skills);
    CHECK_EQ(preview, 400.0);
    client.requestAbsorb({{rose, Rarity::Rare, 50}});
    CHECK(h.stepUntil({&client}, [&] { return client.absorbOutcome().pending; }, 200));
    CHECK(client.absorbOutcome().success);
    CHECK_EQ(client.absorbOutcome().xpGained, preview);
    CHECK(awaitProfile(h, client, [&](const Profile& p) { return p.totalXp == before + preview; }));
    client.absorbOutcome().pending = false;
    client.requestAbsorb({{rose, Rarity::Apex, 1}});
    CHECK(h.stepUntil({&client}, [&] { return client.absorbOutcome().pending; }, 200));
    CHECK(!client.absorbOutcome().success);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Apex), 1u);
    CHECK_EQ(client.profile().totalXp, before + preview);
}
