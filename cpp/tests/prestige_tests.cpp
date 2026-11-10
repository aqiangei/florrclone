// Prestige: the talent, the prestige twin maps, what grows in them, and the
// join that reaches them. See shared/game/prestige.h.

#include "test.h"

#include "server_harness.h"
#include "server/db.h"
#include "server/practice_board.h"
#include "server/systems/spawning.h"
#include "server/systems/combat.h"
#include "server/systems/dungeons.h"
#include "server/systems/loot.h"
#include "shared/game/prestige.h"
#include "shared/game/difficulty.h"
#include "shared/game/map_elements.h"
#include "shared/game/skills.h"
#include "shared/game/shop.h"
#include "shared/game/terrain.h"
#include "test_data.h"

#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace flix;
using namespace flix::testsupport;

namespace {

/// Gives a seeded account the Prestige node, and optionally XP enough for
/// `level`, which is what a purchase is paid from.
void seedProgress(const std::string& path, const std::string& username, bool prestige,
                  int level = 1) {
    Database db;
    std::string error;
    db.load(path, error);
    const Account* account = db.findUser(username);
    if (account == nullptr) return;
    PlayerRecord& record = db.progress(account->id);
    if (prestige) record.skills.set(SkillId::Prestige, 0);
    double xp = 0.0;
    for (int l = 1; l < level; ++l) xp += xpForNextLevel(l);
    record.totalXp = xp;
    db.markDirty();
    db.save();
}

bool logIn(Harness& h, NetClient& client, const char* name) {
    if (!connectClient(h, client)) return false;
    client.requestLogin(name, "password7");
    return h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::LoggedIn; });
}

bool join(Harness& h, NetClient& client, const char* name, bool prestige,
          const std::string& where = {}) {
    client.joinGame(1920, 1080, where, name, prestige);
    return h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                       200);
}

Realm realmOf(World& world, Entity e) {
    return e == NULL_ENTITY ? Realm::Overworld : world.get<Transform>(e).realm;
}

} // namespace

TEST(jurassic_links_both_jungles_and_stocks_all_six_species) {
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    const bool loaded = maps.load(dataDir(), &terrain, error);
    CHECK(loaded);
    if (!loaded) return;
    Rng rng{42};
    const auto checkPad = [&](const char* from, const char* to, const char* id) {
        bool found = false;
        const Realm realm = maps.realmOfId(from, found);
        CHECK(found);
        if (!found) return;
        const Realm expected = maps.realmOfId(to, found);
        CHECK(found);
        bool sawPad = false;
        for (const auto& pad : maps.forRealm(realm)->elements()) {
            if (pad.padId != id) continue;
            sawPad = true;
            CHECK(!terrain.resolveWall(pad.centre(), kPlayerBaseRadius, realm).collided);
            WorldMaps::Destination dest;
            CHECK(maps.resolveTeleporter(pad, rng, terrain, dest));
            CHECK(dest.realm == expected);
            CHECK(!terrain.resolveWall(dest.position, kPlayerBaseRadius, dest.realm).collided);
        }
        CHECK(sawPad);
    };
    checkPad("jungle", "jurassic", "to-jurassic");
    checkPad("jurassic", "jungle", "from-jungle");
    checkPad("prestige_jungle", "prestige_jurassic", "to-jurassic");
    checkPad("prestige_jurassic", "prestige_jungle", "from-jungle");

    bool found = false;
    const Realm realm = maps.realmOfId("jurassic", found);
    CHECK(found);
    for (const int row : {9, 14, 20}) {
        const auto a = Terrain::tileCenter(row == 14 ? 14 : 16, row);
        const auto b = Terrain::tileCenter(24, row);
        CHECK(!terrain.segmentBlocked(a, b, realm));
    }
    CHECK(terrain.segmentBlocked(Terrain::tileCenter(16, 12), Terrain::tileCenter(24, 12), realm));

    World world;
    CommandBuffer commands{world};
    SpawnSystem spawning;
    spawning.worldMaps = &maps;
    spawning.mobCap = 0;
    const auto& content = shippedContent();
    for (int step = 0; step < 15; ++step) {
        spawning.run(world, terrain, content, {{Terrain::tileCenter(22, 9), realm}},
                     rng, 50000.0 + step * kPopulationIntervalMillis, net::kTickSeconds, commands);
        commands.flush();
    }
    std::vector<SpawnSystem::LatentSite> sites;
    spawning.latentSites(realm, sites);
    std::set<std::string> species;
    for (const auto& site : sites) {
        CHECK(!terrain.blocked(site.position, realm));
        species.insert(content.mob(site.mobIndex).id);
    }
    CHECK_EQ(species.size(), std::size_t(6));
    CHECK(species.count("jurassic_tyrannosaurus"));
    CHECK(content.mob(content.mobIndex("jurassic_raptor")).jump.present);
    CHECK(content.mob(content.mobIndex("jurassic_tyrannosaurus")).crabStrafe);
    CHECK_EQ(content.mob(content.mobIndex("jurassic_pteranodon")).projectile.ammoPetalIndex,
             content.petalIndex("wing"));
    std::printf("[jurassic] %zu stocked monsters, %zu species\n", sites.size(), species.size());
}

TEST(jurassic_feedback_bone_stats_and_moon_rock_price) {
    const auto& registry = shippedContent();
    const auto bone = registry.petalIndex("bone");
    CHECK(bone != kInvalidIndex);
    const auto common = registry.petalStats(bone, Rarity::Common);
    CHECK_NEAR(common.health, 12.0, 1e-9);
    CHECK_NEAR(common.petalArmor, 12.0, 1e-9);
    CHECK_NEAR(common.damage, 14.0, 1e-9);
    CHECK_NEAR(common.reloadMillis, 2500.0, 1e-9);
    const auto super = registry.petalStats(bone, Rarity::Super);
    CHECK_NEAR(super.health, 26244.0, 1e-9);
    CHECK_NEAR(super.petalArmor, 26244.0, 1e-9);
    const auto orb = registry.petalIndex("mana_orb");
    CHECK(orb != kInvalidIndex);
    CHECK(!registry.petalStats(orb, Rarity::Common).breakable);
    CHECK(!registry.petalStats(orb, Rarity::Apex).breakable);
    std::string error;
    CHECK(loadContent(dataDir(), error));
    CHECK_NEAR(shopPrice("basic", Rarity::Super), 4188.0, 1e-9);
    const auto moon = registry.petalIndex("moon_rock");
    CHECK_NEAR(shopPrice(moon, Rarity::Ultra), 9000.0, 1e-9);
    CHECK_NEAR(shopPrice(moon, Rarity::Super), 45000.0, 1e-9);
    CHECK_NEAR(shopOfferPrice(moon, Rarity::Super, 30), 31500.0, 1e-9);
    CHECK(!shopSellsRarity(Rarity::Unique));
}

TEST(guild_deputy_can_invite_but_cannot_manage_or_transfer) {
    Harness h("guild-deputy", [](const std::string& path) {
        for (const char* name : {"leader", "deputy", "guest", "outsider"})
            seedUser(path, name, "password7");
    });
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient boss, deputy, guest, outsider;
    CHECK(logIn(h, boss, "leader"));
    CHECK(logIn(h, deputy, "deputy"));
    CHECK(logIn(h, guest, "guest"));
    CHECK(logIn(h, outsider, "outsider"));
    const std::vector<NetClient*> clients{&boss, &deputy, &guest, &outsider};
    boss.requestGuildCreate("ALPHA");
    CHECK(h.stepUntil(clients, [&] { return boss.guild().joined; }));
    boss.requestGuildInvite("deputy");
    CHECK(h.stepUntil(clients, [&] { return deputy.guildInvite().waiting; }));
    deputy.requestGuildAccept();
    CHECK(h.stepUntil(clients, [&] { return deputy.guild().joined; }));
    deputy.requestGuildInvite("guest");
    h.step(10, clients);
    CHECK(!guest.guildInvite().waiting);
    boss.sendChat("/guild-deputy deputy");
    CHECK(h.stepUntil(clients, [&] { return deputy.guild().deputy == "deputy"; }));
    deputy.requestGuildInvite("guest");
    CHECK(h.stepUntil(clients, [&] { return guest.guildInvite().waiting; }));
    guest.requestGuildAccept();
    CHECK(h.stepUntil(clients, [&] { return guest.guild().joined; }));

    deputy.sendChat("/guild-deputy guest");
    deputy.sendChat("/guild-transfer guest");
    deputy.requestGuildKick("guest");
    deputy.requestGuildEdit("Changed", "Changed");
    h.step(20, clients);
    CHECK(guest.guild().joined);
    CHECK_EQ(boss.guild().leader, std::string("leader"));
    CHECK_EQ(boss.guild().deputy, std::string("deputy"));
    CHECK(boss.guild().name != "Changed");

    boss.sendChat("/guild-deputy guest");
    CHECK(h.stepUntil(clients, [&] { return deputy.guild().deputy == "guest"; }));
    deputy.requestGuildInvite("outsider");
    h.step(10, clients);
    CHECK(!outsider.guildInvite().waiting);
    boss.sendChat("/guild-transfer guest");
    CHECK(h.stepUntil(clients, [&] { return boss.guild().leader == "guest"; }));
    CHECK(boss.guild().deputy.empty());
}

TEST(hyper_corruption_exposure_allows_retaliation_and_continue_returns_to_spawn) {
    Harness h("corruption-continue", [](const std::string& path) {
        seedUser(path, "attacker", "password7");
        seedUser(path, "survivor", "password7");
        seedProgress(path, "attacker", false, 25);
        seedProgress(path, "survivor", false, 25);
    });
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient attacker, survivor;
    CHECK(logIn(h, attacker, "attacker") && join(h, attacker, "attacker", false, "garden"));
    CHECK(logIn(h, survivor, "survivor") && join(h, survivor, "survivor", false, "garden"));
    auto& world = h.server.world();
    const Entity aggressor = bodyNamed(world, "attacker");
    const Entity victim = bodyNamed(world, "survivor");
    CHECK(aggressor != NULL_ENTITY && victim != NULL_ENTITY);
    if (aggressor == NULL_ENTITY || victim == NULL_ENTITY) return;
    const auto spawn = world.get<Transform>(victim).position;
    world.get<Loadout>(aggressor).slots[0] = {content().petalIndex("corruption"), Rarity::Apex};
    h.step(1, {&attacker, &survivor});
    CHECK(world.get<PlayerVisuals>(aggressor).hyperCorruption);
    world.get<Loadout>(aggressor).slots[0] = {};
    h.step(1, {&attacker, &survivor});
    CHECK(world.get<PlayerVisuals>(aggressor).hyperCorruptionExposed);
    CHECK(CombatSystem::canDamage(world, victim, aggressor));
    CHECK(!CombatSystem::canDamage(world, aggressor, victim));
    h.clock += kHyperCorruptionExposureMillis + 50.0;
    h.step(1, {&attacker, &survivor});
    CHECK(!world.get<PlayerVisuals>(aggressor).hyperCorruptionExposed);
    CHECK(!CombatSystem::canDamage(world, victim, aggressor));

    world.get<Loadout>(aggressor).slots[0] = {content().petalIndex("corruption"), Rarity::Apex};
    h.step(1, {&attacker, &survivor});
    const Vec2 deathAt = spawn + Vec2{5000.0, 5000.0};
    world.get<Transform>(victim).position = deathAt;
    world.add<Dead>(victim, Dead{aggressor});
    CHECK(h.stepUntil({&attacker, &survivor}, [&] { return survivor.dead(); }));
    survivor.requestRespawn();
    CHECK(h.stepUntil({&attacker, &survivor}, [&] {
        return !survivor.dead() && bodyNamed(world, "survivor") != victim;
    }));
    const Entity reborn = bodyNamed(world, "survivor");
    CHECK(reborn != NULL_ENTITY);
    if (reborn != NULL_ENTITY) {
        CHECK(distance(world.get<Transform>(reborn).position, spawn) < 1800.0);
        CHECK(distance(world.get<Transform>(reborn).position, deathAt) > 4000.0);
    }
}

TEST(wave_loot_pays_from_normal_tables_and_throttles_boss_tiers_independently) {
    World world;
    CommandBuffer commands{world};
    SpatialGrid grid;
    LootSystem loot;
    EventQueue events;
    Rng rng{5150};
    const auto& registry = shippedContent();
    std::string error;
    CHECK(loot.loadTables(registry, dataDir() + "/mob_drops.json", error));
    const Entity player = world.create();
    world.add<PlayerTag>(player);
    world.add<Health>(player, Health{100.0, 100.0});
    world.add<Transform>(player, Transform{{2000.0, 2000.0}, 0.0});
    const auto roll = [&](Rarity rarity, double now, std::optional<bool> waveCanDrop) {
        const Entity corpse = world.create();
        world.add<MobTag>(corpse);
        world.add<Transform>(corpse, Transform{{4000.0, 4000.0}, 0.0});
        world.add<MobType>(corpse, MobType{registry.mobIndex("beetle"), rarity, 1.0});
        world.add<Health>(corpse, Health{0.0, 1000.0});
        world.add<Bounty>(corpse).credit(player, 1000.0);
        world.add<Dead>(corpse, Dead{player});
        if (waveCanDrop) world.add<WaveEnemy>(corpse, WaveEnemy{*waveCanDrop});
        loot.run(world, grid, registry, rng, now, net::kTickSeconds, commands, events);
        commands.flush();
        const auto drops = Query<DropTag>{world}.collect();
        for (const Entity drop : drops) world.destroy(drop);
        world.destroy(corpse);
        return drops.size();
    };
    CHECK_EQ(roll(Rarity::Rare, 1000.0, false), std::size_t(0));
    CHECK(roll(Rarity::Rare, 1100.0, true) > 0);
    CHECK_EQ(roll(Rarity::Apex, 1200.0, false), std::size_t(0));
    CHECK(roll(Rarity::Apex, 1300.0, true) > 0);
    CHECK(roll(Rarity::Unique, 1400.0, true) > 0);
    CHECK_EQ(roll(Rarity::Apex, 1500.0, true), std::size_t(0));
    CHECK_EQ(roll(Rarity::Unique, 1600.0, true), std::size_t(0));
    CHECK(roll(Rarity::Apex, 1700.0, std::nullopt) > 0);
    CHECK(roll(Rarity::Unique, 361400.0, true) > 0);
    CHECK_EQ(roll(Rarity::Apex, 361500.0, true), std::size_t(0));
    CHECK(roll(Rarity::Apex, 721300.0, true) > 0);
}

// ---------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------

TEST(an_prestige_band_rolls_one_tier_up_and_stops_at_super) {
    CHECK(prestigeRarity(Rarity::Common) == Rarity::Uncommon);
    CHECK(prestigeRarity(Rarity::Mythic) == Rarity::Ultra);
    CHECK(prestigeRarity(Rarity::Ultra) == Rarity::Super);
    // Super has nowhere to go: unique and apex never stand in a twin.
    CHECK(prestigeRarity(Rarity::Super) == Rarity::Super);
    CHECK(!barredFromPrestige(Rarity::Super));
    CHECK(barredFromPrestige(Rarity::Unique));
    CHECK(barredFromPrestige(Rarity::Apex));
}

TEST(deep_prestige_sewers_grows_mostly_super_with_some_ultra) {
    constexpr int samples = 50000;
    Rng band{711}, promotion{117}, previousPromotion{117};
    int ultra = 0;
    int fullPromotionSupers = 0;
    int previousSupers = 0;
    int newSupers = 0;
    for (int i = 0; i < samples; ++i) {
        const auto ordinary = rollReferenceSpawnRarity(75, 1.0, band);
        const auto adjusted = rollPrestigeAmbientRarity(ordinary, promotion);
        ultra += adjusted == Rarity::Ultra;
        fullPromotionSupers += prestigeRarity(ordinary) == Rarity::Super;
        previousSupers += ordinary == Rarity::Super ||
                          (ordinary == Rarity::Ultra && previousPromotion.chance(0.25));
        newSupers += adjusted == Rarity::Super;
        if (ordinary != Rarity::Ultra) CHECK(adjusted == prestigeRarity(ordinary));
    }
    CHECK(fullPromotionSupers > samples * .75);
    CHECK(previousSupers > samples * .18 && previousSupers < samples * .22);
    CHECK(newSupers > samples * .58 && newSupers < samples * .62);
    CHECK(ultra > samples * .34 && ultra < samples * .38);
    std::printf("[prestige sewers difficulty 75] full promotion Super %.2f%%, previous %.2f%%, "
                "retuned Super %.2f%%, Ultra %.2f%%\n",
                100.0 * fullPromotionSupers / samples, 100.0 * previousSupers / samples,
                100.0 * newSupers / samples,
                100.0 * ultra / samples);
}

TEST(prestige_is_one_root_node_costing_175) {
    CHECK_EQ(skillTierCount(SkillId::Prestige), 1);
    CHECK_EQ(skillTierCost(SkillId::Prestige, 0), 175);
    CHECK(skillGate(SkillId::Prestige) == nullptr);
    CHECK(skillFromKey("prestige") == SkillId::Prestige);

    SkillSet skills;
    CHECK(!skills.prestigeUnlocked());
    CHECK(skills.prerequisiteMet(SkillId::Prestige));
    skills.set(SkillId::Prestige, 0);
    CHECK(skills.prestigeUnlocked());
    CHECK_EQ(skills.spent(), 175);
    CHECK_EQ(availableTalentPoints(88, skills), 1);
    CHECK_EQ(availableTalentPoints(113, skills), 51);
    // It multiplies nothing.
    CHECK_NEAR(skills.statScale(SkillId::Damage), 1.0, 1e-12);
    CHECK_NEAR(skills.reloadScale(), 1.0, 1e-12);
}

// ---------------------------------------------------------------------------
// The twin maps
// ---------------------------------------------------------------------------

TEST(every_authored_prestige_twin_is_loaded_after_the_plain_realms) {
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), nullptr, error));

    Json manifest;
    CHECK(Json::parseFile(dataDir() + "/maps.json", manifest, error));
    std::set<std::string> authoredTwins;
    for (const Json& entry : manifest["maps"].items()) {
        if (entry["prestige"].asBool()) {
            authoredTwins.insert(std::filesystem::path(entry["file"].asString()).stem().string());
        }
    }
    CHECK(authoredTwins.size() >= 8u);
    int twins = 0;
    int lastPlainSlot = -1;
    int firstTwinSlot = maps.count();
    for (int i = 0; i < maps.count(); ++i) {
        const MapData& map = maps.maps()[static_cast<std::size_t>(i)];
        if (map.prestige()) {
            ++twins;
            firstTwinSlot = std::min(firstTwinSlot, i);
            CHECK(maps.isPrestige(map.realm()));
            CHECK_EQ(map.id(), std::string(kPrestigeMapPrefix) + map.templateId());
            CHECK(map.displayName().rfind("Prestige ", 0) == 0);
            continue;
        }
        lastPlainSlot = i;
        CHECK(!maps.isPrestige(map.realm()));
        bool found = false;
        const Realm twin = maps.prestigeRealm(map.realm(), found);
        CHECK(found == (authoredTwins.count(map.id()) > 0));
        if (!found) continue;
        const MapData* other = maps.forRealm(twin);
        CHECK(other != nullptr);
        if (other == nullptr) continue;
        CHECK(other->prestige());
        CHECK_EQ(other->id(), std::string(kPrestigeMapPrefix) + map.id());
        // Same place, same mobs.
        CHECK_EQ(other->biome(), map.biome());
        CHECK_EQ(other->defaultMobGroup(), map.defaultMobGroup());
        CHECK_EQ(other->elements().size(), map.elements().size());
        // A twin of a twin is not a thing.
        bool again = false;
        maps.prestigeRealm(twin, again);
        CHECK(!again);
    }
    CHECK_EQ(static_cast<std::size_t>(twins), authoredTwins.size());
    // Appended: turning prestige on renumbered no existing realm.
    CHECK(lastPlainSlot < firstTwinSlot);
    // There is no prestige arena or maze.
    bool found = true;
    maps.prestigeRealm(Realm::Arena, found);
    CHECK(!found);
    maps.prestigeRealm(Realm::Maze, found);
    CHECK(!found);
    // Nor are a twin's doors offered on their own.
    for (const SpawnChoice& door : maps.doors()) CHECK(!maps.isPrestige(door.realm));
}

TEST(a_pad_on_an_prestige_twin_leads_to_another_twin) {
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), nullptr, error));
    int pads = 0;
    for (const MapData& map : maps.maps()) {
        if (!map.prestige()) continue;
        for (const MapElement& pad : map.elements()) {
            if (pad.kind != MapElementKind::Teleporter || pad.targetMap.empty()) continue;
            bool found = false;
            const Realm target = maps.realmOfId(pad.targetMap, found);
            CHECK(found);
            if (!found) continue;
            const MapData* destination = maps.forRealm(target);
            if (destination == nullptr) continue;
            // Into a twin, unless the place it leads has none (a dungeon).
            bool hasTwin = false;
            maps.prestigeRealm(maps.realmOfId(stripPrestigePrefix(pad.targetMap), found), hasTwin);
            CHECK(destination->prestige() || !hasTwin);
            ++pads;
        }
    }
    CHECK(pads > 0);
    // And the plain maps' pads were left alone.
    for (const MapData& map : maps.maps()) {
        if (map.prestige()) continue;
        for (const MapElement& pad : map.elements()) {
            if (pad.kind != MapElementKind::Teleporter) continue;
            CHECK(pad.targetMap.rfind(kPrestigeMapPrefix, 0) != 0);
        }
    }
}

TEST(a_copies_entry_cannot_also_be_prestige) {
    const std::string dir = tempDir("flix-prestige-manifest-" + std::to_string(::getpid()));
    CHECK(writeText(dir + "/maps.json",
                    R"({"maps": [{"file": "a.tmj", "copies": 2, "prestige": true}]})"));
    WorldMaps maps;
    std::string error;
    CHECK(!maps.load(dir, nullptr, error));
    CHECK(error.find("cannot also have a prestige twin") != std::string::npos);
}

// ---------------------------------------------------------------------------
// What grows there
// ---------------------------------------------------------------------------

TEST(an_prestige_twin_grows_every_band_a_tier_up_and_no_boss_above_super) {
    World world;
    CommandBuffer commands{world};
    Terrain terrain;
    SpawnSystem spawner;
    Rng rng{0xA5CE11u};
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    spawner.worldMaps = &maps;
    // One pass with nobody about stocks every band on every map as records.
    spawner.run(world, terrain, shippedContent(), {}, rng, 50000.0, net::kTickSeconds, commands);
    commands.flush();

    bool found = false;
    const Realm garden = maps.realmOfId("garden", found);
    CHECK(found);
    const Realm prestigeGarden = maps.prestigeRealm(garden, found);
    CHECK(found);
    CHECK(spawner.prestigeRealm(prestigeGarden));
    CHECK(!spawner.prestigeRealm(garden));

    const auto tiers = [&](Realm realm) {
        std::vector<SpawnSystem::LatentSite> sites;
        spawner.latentSites(realm, sites);
        return sites;
    };
    const std::vector<SpawnSystem::LatentSite> plainSites = tiers(garden);
    const std::vector<SpawnSystem::LatentSite> twinSites = tiers(prestigeGarden);
    CHECK(!plainSites.empty());
    CHECK(!twinSites.empty());

    // Difficulty-0 ground is all common in the garden and all uncommon in its
    // twin, mob floors aside -- and nothing in the twin is common.
    int plainCommons = 0;
    double plainSum = 0.0;
    for (const auto& site : plainSites) {
        plainCommons += site.rarity == Rarity::Common ? 1 : 0;
        plainSum += rarityIndex(site.rarity);
    }
    double twinSum = 0.0;
    for (const auto& site : twinSites) {
        CHECK(site.rarity != Rarity::Common);
        twinSum += rarityIndex(site.rarity);
    }
    CHECK(plainCommons > 0);
    CHECK(twinSum / twinSites.size() > plainSum / plainSites.size() + 0.5);

    // On every twin: nothing above super, as a record or as a body.
    for (const MapData& map : maps.maps()) {
        if (!map.prestige()) continue;
        for (const auto& site : tiers(map.realm())) CHECK(!barredFromPrestige(site.rarity));
    }
    Query<MobTag, Transform, MobType> mobs{world};
    mobs.each([&](Entity, MobTag&, Transform& transform, MobType& type) {
        if (maps.isPrestige(transform.realm)) CHECK(!barredFromPrestige(type.rarity));
    });
    // And no clock was dealt for one: a twin is not a boss biome.
    for (const SpawnSystem::BiomeBossClock& clock : spawner.bossClocks()) {
        CHECK(clock.biome.rfind(kPrestigeMapPrefix, 0) != 0);
    }
    // Spawn a real announcing tier in the ordinary biome. Supers in the
    // cold stock are below the global announcement threshold.
    const auto& content = shippedContent();
    spawner.restoreBossClock("garden", 0.0, 0.0);
    const auto boss = spawner.spawnMob(world, terrain, content, content.mobIndex("bee"),
                                       Rarity::Unique, terrain.spawnPoint(garden), garden, 50000.0, rng);
    CHECK(boss != NULL_ENTITY);
    bool announced = false;
    for (const auto& entry : spawner.bossSpawns) {
        CHECK(!maps.isPrestige(entry.realm));
        announced |= entry.entity == boss;
    }
    CHECK(announced);
}

TEST(prestige_hel_stock_is_denser_and_upper_bands_have_more_supers) {
    World world;
    CommandBuffer commands{world};
    Terrain terrain;
    SpawnSystem spawner;
    Rng rng{0xE114u};
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    if (!error.empty()) return;
    spawner.worldMaps = &maps;
    spawner.run(world, terrain, shippedContent(), {}, rng, 50000.0, net::kTickSeconds, commands);
    commands.flush();
    bool found = false;
    const Realm hel = maps.realmOfId("hel", found);
    CHECK(found);
    const Realm prestigeHel = maps.prestigeRealm(hel, found);
    CHECK(found);
    if (!found) return;

    std::size_t ordinaryStock = 0;
    for (const auto realm : {hel, prestigeHel}) {
        std::vector<SpawnSystem::LatentSite> sites;
        spawner.latentSites(realm, sites);
        int deep = 0;
        int supers = 0;
        int middle = 0;
        int middleSupers = 0;
        for (const auto& site : sites) {
            if (realm == prestigeHel) CHECK(!barredFromPrestige(site.rarity));
            if (site.difficulty == 50.0) {
                ++middle;
                middleSupers += site.rarity == Rarity::Super;
            }
            if (site.difficulty < 60.0) continue;
            CHECK_EQ(site.difficulty, realm == prestigeHel ? 65.0 : 60.0);
            ++deep;
            supers += site.rarity == Rarity::Super;
        }
        CHECK(deep >= 12);
        if (realm == prestigeHel) {
            CHECK(sites.size() > ordinaryStock);
            CHECK(middleSupers > middle * 0.23 && middleSupers < middle * 0.55);
            CHECK(supers > deep * 0.4 && supers < deep * 0.9);
            CHECK(supers * middle > middleSupers * deep);
        } else {
            ordinaryStock = sites.size();
            CHECK(supers < deep * 0.1);
        }
        std::printf("Hel stock (%s): %zu total, %d/%d middle Super, %d/%d deepest Super\n",
                    realm == prestigeHel ? "prestige" : "ordinary", sites.size(),
                    middleSupers, middle, supers, deep);
    }
}

TEST(prestige_jungle_sewers_and_hel_wake_local_mobs_at_the_shared_ambient_limit) {
    World world;
    CommandBuffer commands{world};
    Terrain terrain;
    SpawnSystem spawner;
    Rng rng{42u};
    WorldMaps maps;
    std::string error;
    if (!maps.load(dataDir(), &terrain, error)) {
        CHECK(false);
        return;
    }
    spawner.worldMaps = &maps;
    bool found = false;
    const Realm jungle = maps.realmOfId("jungle", found);
    CHECK(found);
    const Realm twin = maps.prestigeRealm(jungle, found);
    CHECK(found);
    if (!found) return;

    const Realm pyramid = maps.realmOfId("pyramid", found);
    CHECK(found);
    if (!found) return;
    const Realm prestigePyramid = maps.prestigeRealm(pyramid, found);
    CHECK(found);
    if (!found) return;
    spawner.setActivePyramidRealms(pyramid, prestigePyramid);

    // Many ordinary bands in view exhaust their share before the first
    // player enters the prestige twin. Use the shipped map geometry.
    std::vector<RealmPoint> viewers;
    for (const MapData& map : maps.maps()) {
        if (map.prestige() || map.id() != map.templateId()) continue;
        if (map.biome() == "pyramid" && map.realm() != pyramid) continue;
        for (const MapElement& band : map.elements()) {
            if (!band.isSpawnBand() || band.singular || band.forceRarity >= 0) continue;
            const Vec2 at{band.bounds.x + band.bounds.w * 0.5, band.bounds.y + band.bounds.h * 0.5};
            if (!terrain.blocked(at, map.realm())) viewers.push_back({at, map.realm()});
            if (viewers.size() == 60) break;
        }
        if (viewers.size() == 60) break;
    }
    const auto tick = [&](double time) {
        spawner.run(world, terrain, shippedContent(), viewers, rng, time, net::kTickSeconds, commands);
        commands.flush();
    };
    for (int pass = 0; pass < 12; ++pass) tick(50'000.0 + pass * kPopulationIntervalMillis);

    const int before = spawner.census().mobs;
    CHECK(before >= kMaxLiveMobs - 170);
    const Vec2 entrance{25217.0, 3577.0};
    viewers.push_back({entrance, twin});
    tick(55'550.0); // Arrive between regular population passes.

    int nearby = 0;
    Query<MobTag, Transform, MobType, AmbientMob> mobs{world};
    mobs.without<Dead>();
    mobs.each([&](Entity, MobTag&, Transform& at, MobType& type, AmbientMob&) {
        if (at.realm != twin) return;
        CHECK(!barredFromPrestige(type.rarity));
        if (std::abs(at.position.x - entrance.x) <= 1200.0 &&
            std::abs(at.position.y - entrance.y) <= 820.0) ++nearby;
    });
    CHECK(nearby > 0);
    CHECK(spawner.census().mobs <= kMaxLiveMobs);

    const Realm boxRealm = maps.realmOfId("prestige_sewers", found);
    CHECK(found);
    if (!found) return;
    const Vec2 box = terrain.resolveCircle({21400.0, 4050.0}, kPlayerBaseRadius, boxRealm);
    viewers.push_back({box, boxRealm});
    tick(55'600.0);
    int boxNearby = 0;
    mobs.each([&](Entity, MobTag&, Transform& at, MobType&, AmbientMob&) {
        if (at.realm == boxRealm && std::abs(at.position.x - box.x) <= 1400.0 &&
            std::abs(at.position.y - box.y) <= 1000.0) ++boxNearby;
    });
    CHECK(boxNearby > 0);
    CHECK(spawner.census().mobs <= kMaxLiveMobs);
    std::printf("[sewers] %d mobs near the box at the ambient budget\n", boxNearby);

    const Realm helRealm = maps.realmOfId("prestige_hel", found);
    CHECK(found);
    if (!found) return;
    const Vec2 hel = terrain.resolveCircle({4252.0, 12721.0}, kPlayerBaseRadius, helRealm);
    viewers.push_back({hel, helRealm});
    tick(55'650.0);
    int helNearby = 0;
    mobs.each([&](Entity, MobTag&, Transform& at, MobType& type, AmbientMob&) {
        if (at.realm != helRealm) return;
        CHECK(!barredFromPrestige(type.rarity));
        if (std::abs(at.position.x - hel.x) <= 1400.0 &&
            std::abs(at.position.y - hel.y) <= 1000.0) ++helNearby;
    });
    CHECK(helNearby > 0);
    CHECK(spawner.census().mobs <= kMaxLiveMobs);
    std::printf("[prestige hel] %d mobs near the upper band at the ambient budget\n", helNearby);
}

TEST(jungle_refills_near_a_stationary_viewer_after_kills) {
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    for (const char* mapId : {"jungle", "prestige_jungle"}) {
        bool found = false;
        const Realm realm = maps.realmOfId(mapId, found);
        CHECK(found);
        if (!found) return;
        const Vec2 at = terrain.resolveCircle({25217, 3577}, kPlayerBaseRadius, realm);
        World world;
        SpawnSystem spawner;
        spawner.worldMaps = &maps;
        CommandBuffer commands{world};
        Rng rng{42};
        const auto tick = [&](double now) {
            spawner.run(world, terrain, shippedContent(), {{at, realm}}, rng, now,
                        net::kTickSeconds, commands);
            commands.flush();
        };
        const auto nearby = [&] {
            std::vector<Entity> entities;
            Query<AmbientMob, Transform, MobType> mobs{world};
            mobs.without<Dead>();
            mobs.each([&](Entity e, AmbientMob& ambient, Transform& where, MobType& type) {
                if (ambient.zone != kInvalidIndex && where.realm == realm &&
                    rarityIndex(type.rarity) < rarityIndex(Rarity::Unique) &&
                    std::abs(where.position.x - at.x) <= 1200 &&
                    std::abs(where.position.y - at.y) <= 820) entities.push_back(e);
            });
            return entities;
        };
        for (int i = 0; i < 12; ++i) tick(50'000 + i * 500);
        const auto cleared = nearby();
        CHECK(!cleared.empty());
        for (const Entity e : cleared) world.add<Dead>(e);
        tick(56'000);
        for (const Entity e : cleared) world.destroy(e);
        for (int i = 1; i <= 13; ++i) tick(56'000 + i * 500);
        const auto refilled = nearby();
        CHECK(!refilled.empty());
        CHECK(spawner.census().mobs <= kMaxLiveMobs);
        std::printf("[stationary %s] cleared %zu, refilled %zu after 6.5 seconds\n",
                    mapId, cleared.size(), refilled.size());
    }
}

TEST(deep_jungle_refills_a_reachable_empty_room_for_a_stationary_flower) {
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    if (!error.empty()) return;
    bool found = false;
    const auto realm = maps.realmOfId("jungle", found);
    CHECK(found);
    if (!found) return;
    const auto& registry = shippedContent();
    const auto* map = maps.forRealm(realm);
    World world;
    SpawnSystem spawn;
    spawn.worldMaps = &maps;
    spawn.sizePlacementGrids(terrain);
    CommandBuffer commands{world};
    Rng rng{42};
    double now = 50'000.0;
    const auto tick = [&](const std::vector<RealmPoint>& viewers) {
        spawn.run(world, terrain, registry, viewers, rng, now, net::kTickSeconds, commands);
        commands.flush();
        now += kPopulationIntervalMillis;
    };
    for (int pass = 0; pass < 4; ++pass) tick({});
    std::vector<SpawnSystem::LatentSite> sites;
    spawn.latentSites(realm, sites);
    constexpr double half = kViewportBuffer + 120.0;
    const auto nearby = [&](Vec2 a, Vec2 b) {
        return std::abs(a.x - b.x) <= half && std::abs(a.y - b.y) <= half &&
            !terrain.segmentBlocked(a, b, realm);
    };
    std::optional<Vec2> stand;
    for (const auto& band : map->elements()) {
        if (!band.isSpawnBand() || band.difficulty < 70.0 || band.singular) continue;
        for (double y = band.bounds.y; !stand && y < band.bounds.bottom(); y += 160.0) {
            for (double x = band.bounds.x; !stand && x < band.bounds.right(); x += 160.0) {
                const Vec2 point{x, y};
                if (!band.contains(point) || terrain.blocked(point, realm) ||
                    std::any_of(sites.begin(), sites.end(), [&](const auto& site) {
                        return nearby(point, site.position);
                    })) continue;
                int usable = 0;
                for (int i = 0; i < 8; ++i) {
                    const auto at = point + Vec2::fromAngle(i * kPi / 4.0, 540.0);
                    usable += band.contains(at) && nearby(point, at);
                }
                if (usable >= 4) stand = point;
            }
        }
    }
    CHECK(stand.has_value());
    if (!stand) return;
    const auto player = world.create();
    world.add<PlayerTag>(player);
    world.add<Transform>(player, Transform{*stand, 0.0, realm});
    world.add<PlayerLocation>(player).viewport = {240.0, 240.0};
    for (int pass = 0; pass < 18; ++pass) tick({{*stand, realm}});
    int inRoom = 0;
    Query<AmbientMob, Transform, MobType>{world}.each(
        [&](Entity, AmbientMob&, Transform& at, MobType&) {
            if (at.realm == realm && nearby(*stand, at.position)) ++inRoom;
        });
    std::printf("Deep Jungle room: %d nearby mobs after stationary refill\n", inRoom);
    CHECK(inRoom > 0);
}

TEST(authored_maps_wake_blocked_ambient_records_without_viewer_movement) {
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    World world;
    SpawnSystem spawner;
    spawner.worldMaps = &maps;
    spawner.sizePlacementGrids(terrain);
    CommandBuffer commands{world};
    Rng rng{42};
    double now = 50'000.0;
    const auto tick = [&](const std::vector<RealmPoint>& viewers) {
        spawner.run(world, terrain, shippedContent(), viewers, rng, now,
                    net::kTickSeconds, commands);
        commands.flush();
        now += kPopulationIntervalMillis;
    };
    for (int pass = 0; pass < 5; ++pass) tick({});
    for (const char* id : {"garden", "desert", "computer", "ocean", "jungle", "prestige_jungle",
                           "hel", "ant_hell", "sewers", "prestige_sewers", "pyramid", "jurassic"}) {
        bool found = false;
        const Realm realm = maps.realmOfId(id, found);
        const auto* map = maps.forRealm(realm);
        CHECK(found && map != nullptr);
        if (!found || map == nullptr) continue;
        std::vector<SpawnSystem::LatentSite> sites;
        spawner.latentSites(realm, sites);
        // Probe a point with open ground in the same band around it.
        const bool narrow = map->biome() == "pyramid";
        const auto probe = std::find_if(sites.begin(), sites.end(), [&](const auto& site) {
            if (shippedContent().mob(site.mobIndex).segmentCount > 0 || terrain.blocked(site.position, realm))
                return false;
            return std::any_of(map->elements().begin(), map->elements().end(), [&](const auto& band) {
                if (!band.isSpawnBand() || band.singular || band.forceRarity >= 0 ||
                    band.difficulty != site.difficulty || !band.contains(site.position)) return false;
                int clear = 0;
                for (int i = 0; i < 8; ++i) {
                    const Vec2 at = site.position + Vec2::fromAngle(i * kPi / 4.0, narrow ? 400.0 : 600.0);
                    if (band.contains(at) && !terrain.segmentBlocked(site.position, at, realm)) ++clear;
                }
                return clear >= (narrow ? 1 : 6);
            });
        });
        CHECK(probe != sites.end());
        if (probe == sites.end()) continue;
        const Vec2 standing = probe->position;
        tick({{standing, realm}});
        std::vector<Entity> cleared;
        Query<AmbientMob, Transform, MobType> nearby{world};
        nearby.without<Dead>();
        nearby.each([&](Entity mob, AmbientMob& ambient, Transform& at, MobType& type) {
            if (ambient.zone != kInvalidIndex && at.realm == realm &&
                rarityIndex(type.rarity) < rarityIndex(Rarity::Super) &&
                distanceSq(at.position, standing) <= kRespawnScatter * kRespawnScatter)
                cleared.push_back(mob);
        });
        for (const Entity mob : cleared) world.add<Dead>(mob);
        tick({{standing, realm}});
        for (const Entity mob : cleared) world.destroy(mob);
        // Relocation checks a rotating, bounded set of blocked records. Allow
        // more than one pass through a congested band while the viewer stays put.
        double clearedAfter = 0.0;
        for (int pass = 1; pass <= 12 || (pass <= 24 && clearedAfter == 0.0); ++pass) {
            tick({{standing, realm}});
            sites.clear();
            spawner.latentSites(realm, sites);
            const bool held = std::any_of(sites.begin(), sites.end(), [&](const auto& site) {
                return distanceSq(site.position, standing) == 0.0;
            });
            if (!held && clearedAfter == 0.0) clearedAfter = pass * kPopulationIntervalMillis / 1000.0;
        }
        std::printf("[ambient wake] %s: cleared after %.1fs stationary (0 means held; %zu nearby mobs cleared)\n",
                    id, clearedAfter, cleared.size());
        CHECK(clearedAfter > 0.0);
    }
}

TEST(nothing_puts_a_unique_or_apex_into_an_prestige_twin) {
    World world;
    Terrain terrain;
    SpawnSystem spawner;
    Rng rng{7u};
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    spawner.worldMaps = &maps;
    bool found = false;
    const Realm garden = maps.realmOfId("garden", found);
    const Realm twin = maps.prestigeRealm(garden, found);
    CHECK(found);
    const std::uint16_t bee = shippedContent().mobIndex("bee");
    CHECK(bee != kInvalidIndex);
    const Vec2 at = terrain.spawnPoint(twin);

    CHECK(spawner.spawnMob(world, terrain, shippedContent(), bee, Rarity::Unique, at, twin, 0.0,
                           rng) == NULL_ENTITY);
    CHECK(spawner.spawnMob(world, terrain, shippedContent(), bee, Rarity::Apex, at, twin, 0.0,
                           rng) == NULL_ENTITY);
    // Super is the ceiling, not over it.
    CHECK(spawner.spawnMob(world, terrain, shippedContent(), bee, Rarity::Super, at, twin, 0.0,
                           rng) != NULL_ENTITY);
}

TEST(prestige_xp_bonus_caps_the_jump_from_the_tier_below) {
    World world;
    Terrain terrain;
    SpawnSystem spawner;
    Rng rng{11u};
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    spawner.worldMaps = &maps;
    bool found = false;
    const Realm garden = maps.realmOfId("garden", found);
    const Realm twin = maps.prestigeRealm(garden, found);
    CHECK(found);
    const ContentRegistry& content = shippedContent();
    const std::uint16_t bee = content.mobIndex("bee");
    CHECK(bee != kInvalidIndex);
    const auto xpOf = [&](Rarity rarity, Realm realm) {
        const Entity e = spawner.spawnMob(world, terrain, content, bee, rarity,
                                          terrain.spawnPoint(realm), realm, 0.0, rng);
        CHECK(e != NULL_ENTITY);
        return e == NULL_ENTITY ? -1.0 : world.get<Bounty>(e).xp;
    };

    // The plain garden pays a tier its own XP.
    CHECK(xpOf(Rarity::Super, garden) == content.mobStats(bee, Rarity::Super).xp);
    // A prestige tier earns at most 50% more than the former payout and
    // never more than the same monster in the ordinary biome.
    CHECK(xpOf(Rarity::Super, twin) == std::min(content.mobStats(bee, Rarity::Ultra).xp * 1.5,
        content.mobStats(bee, Rarity::Super).xp));
    CHECK(xpOf(Rarity::Ultra, twin) == std::min(content.mobStats(bee, Rarity::Mythic).xp * 1.5,
        content.mobStats(bee, Rarity::Ultra).xp));
    CHECK(xpOf(Rarity::Uncommon, twin) == std::min(content.mobStats(bee, Rarity::Common).xp * 1.5,
        content.mobStats(bee, Rarity::Uncommon).xp));
    CHECK(xpOf(Rarity::Common, twin) == content.mobStats(bee, Rarity::Common).xp);
    CHECK(content.mobStats(bee, Rarity::Ultra).xp < content.mobStats(bee, Rarity::Super).xp);
}

// ---------------------------------------------------------------------------
// The join
// ---------------------------------------------------------------------------

TEST(only_a_tree_with_prestige_joins_the_prestige_biome) {
    Harness h("prestige-join",
              [](const std::string& path) {
                  seedUser(path, "climber", "password7");
                  seedUser(path, "walker", "password7");
                  seedProgress(path, "climber", true);
                  seedProgress(path, "walker", false);
              },
              dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    World& world = h.server.world();
    const WorldMaps& maps = h.server.worldMaps();
    bool found = false;
    const Realm twin = maps.prestigeRealm(Realm::Overworld, found);
    CHECK(found);

    // Without the talent the flag is refused, and said so.
    NetClient walker;
    CHECK(logIn(h, walker, "walker"));
    CHECK(join(h, walker, "walker", true));
    CHECK(realmOf(world, bodyNamed(world, "walker")) == Realm::Overworld);
    CHECK(sawText(walker, "Prestige is a talent"));

    // With it, the default door's twin.
    NetClient climber;
    CHECK(logIn(h, climber, "climber"));
    CHECK(join(h, climber, "climber", true));
    CHECK(realmOf(world, bodyNamed(world, "climber")) == twin);
    CHECK(climber.view().realm() == twin);

    // A named door goes to that door's twin.
    climber.leaveGame();
    h.step(5, {&climber, &walker});
    const SpawnChoice* desert = nullptr;
    for (const auto& door : maps.doors()) {
        const auto* map = maps.forRealm(door.realm);
        if (map && map->id() == "desert") { desert = &door; break; }
    }
    CHECK(desert != nullptr);
    if (desert != nullptr) {
        CHECK(join(h, climber, "climber", true, desert->id));
        const Realm desertTwin = maps.prestigeRealm(desert->realm, found);
        CHECK(found);
        CHECK(realmOf(world, bodyNamed(world, "climber")) == desertTwin);
    }

    // The arena has no twin: the flag means nothing there.
    climber.leaveGame();
    h.step(5, {&climber, &walker});
    CHECK(join(h, climber, "climber", true, kArenaSpawnChoice));
    CHECK(realmOf(world, bodyNamed(world, "climber")) == Realm::Arena);
}

TEST(prestige_is_bought_with_175_points_and_not_on_the_maze_tree) {
    Harness h("prestige-buy",
              [](const std::string& path) {
                  seedUser(path, "rich", "password7");
                  seedUser(path, "poor", "password7");
                  seedProgress(path, "rich", false, 88);
                  seedProgress(path, "poor", false, 87);
              },
              dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }

    NetClient poor;
    CHECK(logIn(h, poor, "poor"));
    CHECK(awaitProfile(h, poor, [](const Profile& p) { return p.level == 87; }));
    poor.requestUpgradeSkill(SkillId::Prestige, 0);
    CHECK(h.stepUntil({&poor}, [&] { return sawText(poor, "need 175"); }, 60));
    CHECK(!poor.profile().skills.prestigeUnlocked());

    NetClient rich;
    CHECK(logIn(h, rich, "rich"));
    CHECK(awaitProfile(h, rich, [](const Profile& p) { return p.level == 88; }));
    rich.requestUpgradeSkill(SkillId::Prestige, 0);
    CHECK(awaitProfile(h, rich, [](const Profile& p) {
        return p.skills.prestigeUnlocked() && p.talentPoints() == 1;
    }));

    // A reset hands the points back and the next join is an ordinary one.
    rich.requestResetSkills();
    CHECK(awaitProfile(h, rich, [](const Profile& p) { return !p.skills.prestigeUnlocked(); }));
    CHECK(join(h, rich, "rich", true));
    World& world = h.server.world();
    CHECK(realmOf(world, bodyNamed(world, "rich")) == Realm::Overworld);
}

TEST(a_reset_in_a_prestige_biome_sends_the_flower_home) {
    Harness h("prestige-reset-kick",
              [](const std::string& path) {
                  seedUser(path, "climber", "password7");
                  seedUser(path, "walker", "password7");
                  seedProgress(path, "climber", true);
                  seedProgress(path, "walker", true);
              },
              dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    World& world = h.server.world();

    NetClient climber;
    CHECK(logIn(h, climber, "climber"));
    CHECK(awaitProfile(h, climber, [](const Profile& p) { return p.skills.prestigeUnlocked(); }));
    CHECK(join(h, climber, "climber", true));
    CHECK(h.server.worldMaps().isPrestige(realmOf(world, bodyNamed(world, "climber"))));

    // Without the talent the flower may not stay: the body goes, and the
    // client is told so rather than left drawing a world it is not in.
    climber.requestResetSkills();
    CHECK(h.stepUntil({&climber}, [&] { return climber.leftWorld; }, 60));
    CHECK(climber.status() == NetClient::Status::LoggedIn);
    CHECK(bodyNamed(world, "climber") == NULL_ENTITY);
    CHECK(awaitProfile(h, climber, [](const Profile& p) { return !p.skills.prestigeUnlocked(); }));

    // And the next join, asking for prestige all the same, is an ordinary one.
    CHECK(join(h, climber, "climber", true));
    CHECK(realmOf(world, bodyNamed(world, "climber")) == Realm::Overworld);

    // A reset in an ordinary biome leaves the flower where it is.
    NetClient walker;
    CHECK(logIn(h, walker, "walker"));
    CHECK(awaitProfile(h, walker, [](const Profile& p) { return p.skills.prestigeUnlocked(); }));
    CHECK(join(h, walker, "walker", false));
    walker.requestResetSkills();
    CHECK(awaitProfile(h, walker, [](const Profile& p) { return !p.skills.prestigeUnlocked(); }));
    h.step(10, {&walker});
    CHECK(!walker.leftWorld);
    CHECK(walker.status() == NetClient::Status::Playing);
    CHECK(bodyNamed(world, "walker") != NULL_ENTITY);
}

TEST(a_prestige_super_is_neither_announced_nor_mourned) {
    // A super is a prestige biome's everyday stock, not a boss: no spawn line,
    // no "has been defeated" line, whether it dies or an operator clears it.
    Harness h("prestige-quiet-super",
              [](const std::string& path) {
                  seedUser(path, "warden", "password7", true);
                  seedProgress(path, "warden", true);
              },
              dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    World& world = h.server.world();

    NetClient client;
    CHECK(logIn(h, client, "warden"));
    CHECK(join(h, client, "warden", true));
    const Realm realm = realmOf(world, bodyNamed(world, "warden"));
    CHECK(h.server.worldMaps().isPrestige(realm));

    CHECK(say(h, client, "/admin spawn bee super"));
    CHECK(sawText(client, "Spawned super bee"));
    const std::uint16_t bee = shippedContent().mobIndex("bee");
    std::vector<Entity> supers;
    Query<MobTag, MobType, Transform> mobs{world};
    mobs.each([&](Entity e, MobTag&, MobType& type, Transform& at) {
        if (type.configIndex == bee && type.rarity == Rarity::Super && at.realm == realm) {
            supers.push_back(e);
        }
    });
    CHECK_EQ(supers.size(), std::size_t(1));
    for (const Entity e : supers) {
        Afflictions* afflictions = world.tryGet<Afflictions>(e);
        if (afflictions == nullptr) {
            world.add<Afflictions>(e, Afflictions{});
            afflictions = world.tryGet<Afflictions>(e);
        }
        afflictions->poisonStacks.push_back({NULL_ENTITY, 1e12, 1e18});
    }
    CHECK(h.stepUntil({&client}, [&] {
        for (const Entity e : supers) {
            if (world.isAlive(e)) return false;
        }
        return true;
    }, 120));
    h.step(10, {&client});

    // And one cleared by killall, which announces the bosses it removes.
    CHECK(say(h, client, "/admin spawn bee super"));
    CHECK(say(h, client, "/admin killall"));
    h.step(10, {&client});

    // The plain biomes' supers are still announced server-wide, "somewhere";
    // what must not appear is a line about THIS bee, which would say it
    // spawned here.
    CHECK(!sawText(client, "Super Bee has spawned!"));
    CHECK(!sawText(client, "Super Bee has been defeated"));
}

TEST(pyramid_layouts_have_connected_routes_and_share_the_active_spawn_rules) {
    Terrain terrain;
    WorldMaps maps;
    std::string error;
    if (!maps.load(dataDir(), &terrain, error)) { CHECK(false); return; }

    const auto center = [](const MapElement& object) {
        return Vec2{object.bounds.x + object.bounds.w * 0.5, object.bounds.y + object.bounds.h * 0.5};
    };
    const auto exitPad = [](const MapData& map) {
        return std::find_if(map.elements().begin(), map.elements().end(), [](const MapElement& object) {
            return object.kind == MapElementKind::Teleporter;
        });
    };
    int layouts = 0;
    for (const MapData& map : maps.maps()) {
        if (map.biome() != "pyramid" || map.prestige()) continue;
        ++layouts;
        const Realm realm = map.realm();
        bool found = false;
        const Realm twin = maps.prestigeRealm(realm, found);
        if (!found) { CHECK(false); continue; }
        CHECK(realmFromByte(static_cast<std::uint8_t>(twin)) == twin);
        const auto entrances = map.playerSpawns();
        const auto pad = exitPad(map);
        if (entrances.size() != 1 || pad == map.elements().end()) { CHECK(false); continue; }
        const Vec2 entrance = center(*entrances.front());
        const Vec2 exit = center(*pad);
        CHECK(!terrain.resolveWall(entrance, kPlayerBaseRadius, realm).collided);
        CHECK(!terrain.resolveWall(exit, kPlayerBaseRadius, realm).collided);

        // Test the collision polygons used by the server, including the
        // transformed tiles. Every walkable sample must be reachable from
        // the entrance by a flower with room to clear each step.
        constexpr int step = 64;
        const Vec2 extent = terrain.realmExtent(realm);
        const int width = static_cast<int>(extent.x) / step;
        const int height = static_cast<int>(extent.y) / step;
        const auto grid = [width](Vec2 at) {
            return static_cast<int>(at.y) / step * width + static_cast<int>(at.x) / step;
        };
        const auto sample = [width](int cell) {
            return Vec2{static_cast<double>(cell % width * step + step / 2),
                        static_cast<double>(cell / width * step + step / 2)};
        };
        std::vector<unsigned char> open(width * height);
        int walkable = 0;
        for (int cell = 0; cell < width * height; ++cell) {
            if (terrain.resolveWall(sample(cell), kPlayerBaseRadius, realm).collided) continue;
            open[cell] = 1;
            ++walkable;
        }
        const int start = grid(entrance);
        if (!open[start]) { CHECK(false); continue; }
        std::vector<int> reached{start};
        open[start] = 2;
        for (std::size_t i = 0; i < reached.size(); ++i) {
            const int cell = reached[i];
            const int x = cell % width;
            for (const int next : {x > 0 ? cell - 1 : -1, x + 1 < width ? cell + 1 : -1,
                                   cell - width, cell + width}) {
                if (next < 0 || next >= width * height || open[next] != 1) continue;
                if (terrain.resolveWall((sample(cell) + sample(next)) * 0.5, kPlayerBaseRadius, realm).collided)
                    continue;
                open[next] = 2;
                reached.push_back(next);
            }
        }
        CHECK(open[grid(exit)] == 2);
        CHECK_EQ(reached.size(), static_cast<std::size_t>(walkable));

        Rng rng{20261108};
        for (const Realm current : {realm, twin}) {
            const MapData& currentMap = *maps.forRealm(current);
            const auto currentPad = exitPad(currentMap);
            WorldMaps::Destination destination;
            if (currentPad == currentMap.elements().end() ||
                !maps.resolveTeleporter(*currentPad, rng, terrain, destination)) {
                CHECK(false);
                continue;
            }
            CHECK(maps.forRealm(destination.realm)->biome() == "desert");
            CHECK(maps.isPrestige(destination.realm) == (current == twin));
            CHECK(!terrain.blocked(destination.position, destination.realm));
        }

        World world;
        CommandBuffer commands{world};
        SpawnSystem spawner;
        spawner.worldMaps = &maps;
        spawner.sizePlacementGrids(terrain);
        spawner.setActivePyramidRealms(realm, twin);
        spawner.restoreBossClock("pyramid", 0.0, 0.0);
        for (int pass = 0; pass < 12; ++pass) {
            spawner.run(world, terrain, shippedContent(), {{entrance, realm}, {entrance, twin}},
                        rng, 50'000.0 + pass * kPopulationIntervalMillis, net::kTickSeconds, commands);
            commands.flush();
        }

        int ordinary = 0, prestige = 0, farm = 0, eternal = 0, hyper = 0;
        Query<MobTag, Transform, MobType, AmbientMob> mobs{world};
        mobs.without<Dead>();
        mobs.each([&](Entity entity, MobTag&, Transform& at, MobType& mob, AmbientMob& ambient) {
            if (maps.forRealm(at.realm) == nullptr || maps.forRealm(at.realm)->biome() != "pyramid")
                return;
            CHECK(at.realm == realm || at.realm == twin);
            if (at.realm == realm) {
                ordinary += ambient.zone != kInvalidIndex;
                farm += world.has<SuperFarmMob>(entity);
                eternal += mob.rarity == Rarity::Unique;
                hyper += mob.rarity == Rarity::Apex;
            } else if (at.realm == twin) {
                prestige += ambient.zone != kInvalidIndex;
                CHECK(!barredFromPrestige(mob.rarity));
            }
        });
        CHECK(ordinary > 0);
        CHECK(prestige > 0);
        CHECK_EQ(farm, kSuperFarmPopulation);
        CHECK(eternal > 0);
        CHECK(hyper > 0);
        for (const MapData& inactive : maps.maps()) {
            if (inactive.biome() != "pyramid" || inactive.realm() == realm || inactive.realm() == twin)
                continue;
            std::vector<SpawnSystem::LatentSite> sites;
            spawner.latentSites(inactive.realm(), sites);
            CHECK(sites.empty());
        }
        std::printf("[pyramid %s] reachable %zu/%d, ordinary %d, prestige %d, farm %d, Eternal %d, Hyper %d\n",
                    map.id().c_str(), reached.size(), walkable, ordinary, prestige, farm, eternal, hyper);

        std::vector<SpawnSystem::LatentSite> previousStock;
        spawner.latentSites(twin, previousStock);
        CHECK(!previousStock.empty());
        const Realm next = maps.realmOfId(map.id() == "pyramid" ? "pyramid_alt" : "pyramid", found);
        CHECK(found);
        const Realm nextTwin = maps.prestigeRealm(next, found);
        CHECK(found);
        spawner.setActivePyramidRealms(next, nextTwin);
        std::vector<SpawnSystem::LatentSite> retiredStock;
        spawner.latentSites(twin, retiredStock);
        CHECK(retiredStock.empty());
    }
    CHECK_EQ(layouts, 4);
}

TEST(pyramid_rotation_returns_ordinary_and_prestige_players_to_the_new_entrance) {
    Harness h("pyramid-rotation",
              [](const std::string& path) {
                  seedUser(path, "pyramidwalker", "password7");
                  seedUser(path, "pyramidclimber", "password7");
                  seedProgress(path, "pyramidclimber", true);
              }, dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    const auto& maps = h.server.worldMaps();
    bool found = false;
    const Realm pyramid = maps.realmOfId("pyramid", found);
    CHECK(found);
    const auto& choices = maps.spawnChoices();
    const auto entry = std::find_if(choices.begin(), choices.end(), [&](const SpawnChoice& choice) {
        return choice.realm == pyramid;
    });
    CHECK(entry != choices.end());
    if (!found || entry == choices.end()) return;
    NetClient ordinary, prestige;
    CHECK(logIn(h, ordinary, "pyramidwalker"));
    CHECK(join(h, ordinary, "pyramidwalker", false, entry->id));
    CHECK(logIn(h, prestige, "pyramidclimber"));
    CHECK(join(h, prestige, "pyramidclimber", true, entry->id));
    World& world = h.server.world();
    const Entity walker = bodyNamed(world, "pyramidwalker");
    const Entity climber = bodyNamed(world, "pyramidclimber");
    if (walker == NULL_ENTITY || climber == NULL_ENTITY) { CHECK(false); return; }

    const auto oldOrdinary = realmOf(world, walker);
    const auto oldPrestige = realmOf(world, climber);
    CHECK_EQ(maps.forRealm(oldOrdinary)->biome(), std::string("pyramid"));
    CHECK_EQ(maps.forRealm(oldPrestige)->biome(), std::string("pyramid"));
    CHECK(maps.isPrestige(oldPrestige));
    CHECK(ordinary.view().realm() == oldOrdinary);
    CHECK(prestige.view().realm() == oldPrestige);

    const auto padFor = [](const MapData& map, const std::string& target) {
        return std::find_if(map.elements().begin(), map.elements().end(), [&](const MapElement& object) {
            return object.kind == MapElementKind::Teleporter && object.targetMap == target;
        });
    };
    Rng padRng{123u};
    std::set<Realm> visited;
    for (int i = 0; i < 4; ++i) {
        const Realm before = realmOf(world, walker);
        const Entity retired = world.create();
        world.add<MobTag>(retired);
        world.add<Transform>(retired, Transform{{2000.0, 2000.0}, 0.0, before});
        world.get<Transform>(walker).position = {2000.0, 2000.0};
        world.get<Transform>(climber).position = {2200.0, 2200.0};
        GameServerPeer::advancePyramidCycle(h.server);
        CHECK(!world.isAlive(retired));
        const Realm newOrdinary = realmOf(world, walker);
        const Realm newPrestige = realmOf(world, climber);
        CHECK(newOrdinary != before);
        bool twinFound = false;
        CHECK(maps.prestigeRealm(newOrdinary, twinFound) == newPrestige);
        CHECK(twinFound);
        visited.insert(newOrdinary);

        for (const auto player : {walker, climber}) {
            const auto at = world.get<Transform>(player);
            const MapData& map = *maps.forRealm(at.realm);
            const auto doors = map.playerSpawns();
            CHECK(!doors.empty());
            if (!doors.empty()) CHECK(doors.front()->bounds.contains(at.position));
            CHECK(!h.server.terrain().blocked(at.position, at.realm));

            const auto outgoing = padFor(map, maps.isPrestige(at.realm) ? "prestige_desert" : "desert");
            WorldMaps::Destination exit;
            if (outgoing == map.elements().end() ||
                !maps.resolveTeleporter(*outgoing, padRng, h.server.terrain(), exit)) {
                CHECK(false);
                continue;
            }
            GameServerPeer::markTeleport(h.server, player, exit.realm, exit.position);
            CHECK(realmOf(world, player) == exit.realm);
            const MapData& desert = *maps.forRealm(exit.realm);
            const auto incoming = padFor(desert, maps.isPrestige(exit.realm) ? "prestige_pyramid" : "pyramid");
            WorldMaps::Destination arrival;
            if (incoming == desert.elements().end() ||
                !maps.resolveTeleporter(*incoming, padRng, h.server.terrain(), arrival)) {
                CHECK(false);
                continue;
            }
            GameServerPeer::markTeleport(h.server, player, arrival.realm, arrival.position);
            CHECK(realmOf(world, player) == at.realm);
            CHECK(!h.server.terrain().blocked(world.get<Transform>(player).position, at.realm));
        }
        for (int frame = 0; frame < 20; ++frame) {
            h.server.serviceNetwork(1);
            ordinary.poll(1);
            prestige.poll(1);
        }
        CHECK(ordinary.view().realm() == newOrdinary);
        CHECK(prestige.view().realm() == newPrestige);
    }
    CHECK_EQ(visited.size(), std::size_t(4));
    CHECK(realmOf(world, walker) == oldOrdinary);
    CHECK(realmOf(world, climber) == oldPrestige);
}

TEST(wave_chat_creates_a_private_code_and_round_trips_to_the_origin) {
    Harness h("wave-chat",
              [](const std::string& path) {
                  seedUser(path, "wavehost", "password7");
                  seedUser(path, "waveguest", "password7");
              },
              dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient host, guest;
    CHECK(logIn(h, host, "wavehost"));
    CHECK(join(h, host, "wavehost", false));
    CHECK(logIn(h, guest, "waveguest"));
    CHECK(join(h, guest, "waveguest", false));

    World& world = h.server.world();
    const auto hostBody = bodyNamed(world, "wavehost");
    const auto guestBody = bodyNamed(world, "waveguest");
    if (hostBody == NULL_ENTITY || guestBody == NULL_ENTITY) {
        CHECK(false);
        return;
    }
    const auto origin = world.get<Transform>(guestBody).realm;
    guest.requestWaveRoomStatus();
    CHECK(h.stepUntil({&host, &guest}, [&] { return guest.waveRoomStatus.known; }));
    CHECK_EQ(guest.waveRoomStatus.occupied, 0);
    CHECK(!guest.waveRoomStatus.dailyCapReached);
    std::string code;
    host.sendChat("/wave start");
    CHECK(h.stepUntil({&host, &guest}, [&] {
        for (const auto& line : host.chat()) {
            const auto marker = line.text.find("Wave \u623f ");
            if (marker == std::string::npos) continue;
            const auto firstDigit = line.text.find_first_of("0123456789", marker);
            if (firstDigit != std::string::npos) code = line.text.substr(firstDigit, 6);
        }
        return code.size() == 6 && world.has<WavePlayer>(hostBody);
    }));
    if (code.size() != 6) return;
    CHECK(!sawText(guest, code));
    const auto arena = world.get<Transform>(hostBody).realm;
    CHECK(arena != origin);

    guest.sendChat("/wave join " + code);
    CHECK(h.stepUntil({&host, &guest}, [&] {
        return world.has<WavePlayer>(guestBody) && guest.view().realm() == arena;
    }));
    guest.requestWaveRoomStatus();
    CHECK(h.stepUntil({&host, &guest}, [&] { return guest.waveRoomStatus.occupied == 2; }));
    CHECK(host.view().self().wave.durationMillis == 30000);
    const Vec2 rescuePoint = world.get<Transform>(hostBody).position;
    world.get<Transform>(guestBody).position = rescuePoint;
    world.get<Health>(guestBody).current = 0.0;
    world.add<Dead>(guestBody);
    CHECK(h.stepUntil({&host, &guest}, [&] { return isStanding(world, guestBody); }, 140));
    guest.sendChat("/wave leave");
    CHECK(h.stepUntil({&host, &guest}, [&] {
        return !world.has<WavePlayer>(guestBody) && guest.view().realm() == origin;
    }));
    CHECK(world.has<WavePlayer>(hostBody));
    guest.requestWaveRoomStatus();
    CHECK(h.stepUntil({&host, &guest}, [&] { return guest.waveRoomStatus.occupied == 1; }));
    CHECK(h.stepUntil({&guest}, [&] { return guest.waveResultNotice().sequence > 0; }));
    CHECK(guest.waveResultNotice().lines[1].find("本局 +0 星星") != std::string::npos);

    const double initialStars = host.profile().stars;
    double total = initialStars;
    constexpr int wavesPerRun = 5;
    for (int run = 0; run < 4; ++run) {
        if (run > 0) {
            host.sendChat("/wave start");
            CHECK(h.stepUntil({&host, &guest}, [&] { return world.has<WavePlayer>(hostBody); }));
        }
        world.get<Health>(hostBody).invulnerableUntilMillis = std::numeric_limits<double>::infinity();
        h.clock += 30000.0;
        bool sawEternal = false;
        for (int wave = 1; wave <= wavesPerRun; ++wave) {
            total += 7500.0;
            CHECK(h.stepUntil({&host, &guest}, [&] {
                for (const Entity enemy : Query<WaveEnemy>{world}.collect()) {
                    if (!isStanding(world, enemy)) continue;
                    if (world.get<MobType>(enemy).rarity == Rarity::Unique) sawEternal = true;
                    world.get<Bounty>(enemy).credit(hostBody, world.get<Health>(enemy).max);
                    world.add<Dead>(enemy);
                }
                return host.profile().stars >= total;
            }, 850));
            CHECK_NEAR(host.profile().stars, total, 1e-9);
        }
        CHECK(sawEternal);
        const auto noticeBefore = host.waveResultNotice().sequence;
        host.sendChat("/wave leave");
        CHECK(h.stepUntil({&host, &guest}, [&] {
            return host.waveResultNotice().sequence > noticeBefore && host.view().realm() == origin;
        }));
        CHECK(host.waveResultNotice().lines[1] == "通关 5 波 · 本局 +37500 星星");
    }
    CHECK_NEAR(host.profile().stars, initialStars + 150000.0, 1e-9);
}

TEST(wave_is_endless_bounded_spaced_and_rewards_participation) {
    World world;
    Terrain terrain;
    WorldMaps maps;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    if (!error.empty()) return;
    const auto& content = shippedContent();
    SpawnSystem spawner;
    spawner.worldMaps = &maps;
    DungeonSystem dungeon;
    dungeon.worldMaps = &maps;
    dungeon.moveToRealm = [&](Entity e, Realm realm, Vec2 position) {
        world.get<Transform>(e) = Transform{position, 0.0, realm};
    };
    dungeon.waveRevived = [&](Entity e) {
        auto& hp = world.get<Health>(e);
        hp.current = hp.max;
        world.remove<Dead>(e);
    };
    CombatSystem combat;
    Rng rng{42u};
    CommandBuffer commands{world};
    const auto tick = [&](double now) {
        dungeon.run(world, terrain, content, spawner, combat, rng, now, commands);
        commands.flush();
    };
    const Vec2 entry = terrain.spawnPoint(Realm::Overworld);
    std::vector<Entity> members;
    for (int i = 0; i < 5; ++i) {
        const auto e = world.create();
        world.add<PlayerTag>(e);
        world.add<Body>(e, Body{25.0, 1.0});
        world.add<Health>(e, Health{100.0, 100.0});
        world.add<Faction>(e, Faction{Team::Players, true});
        world.add<Transform>(e, Transform{entry, 0.0});
        members.push_back(e);
    }
    tick(0.0);
    const auto reply = dungeon.waveCommand(world, terrain, members[0], "start", rng, 1000.0);
    const auto firstDigit = reply.find_first_of("0123456789");
    CHECK(firstDigit != std::string::npos);
    if (firstDigit == std::string::npos) return;
    const auto join = "join " + reply.substr(firstDigit, 6);
    for (int i = 1; i < 5; ++i)
        dungeon.waveCommand(world, terrain, members[i], join, rng, 2000.0);
    for (int i = 0; i < 4; ++i) CHECK(dungeon.inWave(world, members[i]));
    CHECK(!dungeon.inWave(world, members[4]));
    CHECK(!CombatSystem::canDamage(world, members[0], members[1]));

    int hostAwards = 0;
    int guestAwards = 0;
    std::map<Entity, std::string> summaries;
    dungeon.waveAward = [&](Entity player, std::uint64_t round, bool bonus) -> DungeonSystem::WaveAward {
        CHECK_EQ(bonus, round == 7 || round == 13 || round == 17);
        if (player == members[3]) {
            CHECK_EQ(round, std::uint64_t(1));
            ++guestAwards;
            return {7500, false};
        }
        CHECK(player == members[0]);
        ++hostAwards;
        CHECK_EQ(round, static_cast<std::uint64_t>(hostAwards));
        return {round <= 20 ? 7500 : 0, round >= 20, round == 7};
    };
    dungeon.waveSummary = [&](Entity player, const std::string& text) { summaries[player] = text; };
    const Realm realm = world.get<Transform>(members[0]).realm;
    spawner.mobCap = 0;
    spawner.restoreBossClock("wave", 1000000.0, 1000000.0);
    spawner.run(world, terrain, content, {{world.get<Transform>(members[0]).position, realm}},
                rng, 0.0, 0.0, commands);
    commands.flush();
    CHECK(spawner.bossCooldownLeft(Rarity::Apex, realm, 16000.0) > 0.0);
    CHECK_EQ(spawner.census().mobs, 0);
    tick(30999.0);
    CHECK(Query<WaveEnemy>{world}.collect().empty());
    CHECK_EQ(dungeon.waveHudFor(world, members[0], 30999.0).remainingMillis, 1u);
    tick(31000.0);
    CHECK(Query<WaveEnemy>{world}.collect().empty());
    CHECK(dungeon.inWave(world, members[0]));
    spawner.mobCap = kMaxLiveMobs;

    double now = 31100.0;
    int total = 0;
    for (int wave = 1; wave <= 21; ++wave) {
        const bool boss = wave % 5 == 0;
        const int expected = boss ? 1 : 24 + 2 * std::min(wave - 1, 8);
        const auto theme = wave % 10 == 0 ? net::WaveTheme::HyperBoss :
                           boss ? net::WaveTheme::EternalBoss :
                           wave == 7 ? net::WaveTheme::Bubble :
                           wave == 13 ? net::WaveTheme::Sandstorm :
                           wave == 17 ? net::WaveTheme::Golden : net::WaveTheme::Normal;
        int seen = 0;
        const double start = now;
        for (; now - start <= 42000.0; now += 2800.0) {
            tick(now);
            const auto hud = dungeon.waveHudFor(world, members[0], now);
            CHECK_EQ(hud.round, static_cast<unsigned>(wave));
            CHECK_EQ(static_cast<int>(hud.theme), static_cast<int>(theme));
            const auto mobs = Query<WaveEnemy>{world}.collect();
            if (seen == 0) CHECK_EQ(mobs.size(), static_cast<std::size_t>(boss ? 1 : 3));
            CHECK(mobs.size() <= 3);
            if (wave == 2) CHECK(isStanding(world, members[1]));
            for (const Entity mob : mobs) {
                const auto& at = world.get<Transform>(mob);
                const auto& hp = world.get<Health>(mob);
                const auto& body = world.get<Body>(mob);
                const auto& type = world.get<MobType>(mob);
                CHECK_EQ(world.get<WaveEnemy>(mob).round, static_cast<std::uint64_t>(wave));
                CHECK(at.realm == realm);
                CHECK(!terrain.resolveWall(at.position, body.radius, realm).collided);
                CHECK_EQ(world.get<Bounty>(mob).xp, 0.0);
                CHECK(CombatSystem::canDamage(world, members[0], mob));
                for (const auto other : mobs) {
                    if (other == mob) continue;
                    CHECK(distance(at.position, world.get<Transform>(other).position) >=
                          body.radius + world.get<Body>(other).radius);
                }
                if (boss) {
                    CHECK_EQ(type.configIndex, content.mobIndex("beetle"));
                    CHECK(type.rarity == (wave % 10 == 0 ? Rarity::Apex : Rarity::Unique));
                    CHECK(body.radius > 92.0);
                } else if (wave == 7 || wave == 13 || wave == 17) {
                    CHECK_EQ(type.configIndex, content.mobIndex(
                        wave == 7 ? "bubble" : wave == 13 ? "sandstorm" : "golden_leafbug"));
                }
                // Hold the final batch to near its deadline; 21 clears exceed ten minutes.
                if (seen + static_cast<int>(mobs.size()) == expected && now - start < 42000.0) continue;
                world.get<Bounty>(mob).credit(members[0], hp.max);
                world.get<Bounty>(mob).credit(members[2], hp.max * 0.005);
                if (wave == 1) world.get<Bounty>(mob).credit(members[3], hp.max * 0.01);
                world.add<Dead>(mob);
            }
            if (!mobs.empty() && world.has<Dead>(mobs.front())) {
                seen += static_cast<int>(mobs.size());
                if (wave == 1 && seen == expected) {
                    world.get<Health>(members[1]).current = 0.0;
                    world.add<Dead>(members[1]);
                }
                if (seen == expected) {
                    now += 50.0;
                    tick(now);
                    for (const auto mob : mobs) world.destroy(mob);
                    break;
                }
                tick(now + 50.0);
                for (const auto mob : mobs) world.destroy(mob);
            }
        }
        CHECK_EQ(seen, expected);
        total += seen;
        CHECK(dungeon.inWave(world, members[0]));
    }
    CHECK_EQ(hostAwards, 21);
    CHECK_EQ(guestAwards, 1);
    CHECK(spawner.bossSpawns.empty());
    CHECK(summaries.empty());
    std::printf("[wave] %d enemies in 21 waves, paid each of the first 20 clears\n", total);

    dungeon.waveCommand(world, terrain, members[0], "leave", rng, now);
    CHECK(summaries[members[0]].find("通关 21 波 · 本局 +150000 星星") != std::string::npos);
    CHECK(summaries[members[0]].find("已领满每日 150,000 星星") != std::string::npos);
    CHECK(summaries[members[0]].find("+1 限时锤") != std::string::npos);
    const auto survivors = Query<WaveEnemy>{world}.collect();
    CHECK(!survivors.empty());
    tick(now + 45000.0);
    CHECK_EQ(dungeon.waveHudFor(world, members[1], now + 45000.0).round, 23u);
    bool overlapped = false;
    for (const auto enemy : Query<WaveEnemy>{world}.collect()) {
        if (world.get<WaveEnemy>(enemy).round == 23) overlapped = true;
    }
    CHECK(overlapped);
    for (const auto enemy : survivors) CHECK(isStanding(world, enemy));
    tick(now + 181000.0);
    CHECK(summaries[members[1]].find("连续 3 分钟未推进") != std::string::npos);
    CHECK(summaries[members[1]].find("至少一只怪造成 1% 伤害") != std::string::npos);
    for (const Entity e : members) {
        CHECK(world.get<Transform>(e).realm == Realm::Overworld);
        CHECK(!world.has<WavePlayer>(e));
    }
    CHECK(Query<WaveEnemy>{world}.collect().empty());
    CHECK(CombatSystem::canDamage(world, members[0], members[1]));
}

namespace {
std::int64_t waveStarClock = 0;
std::int64_t waveStarNow() { return waveStarClock; }
}

TEST(wave_stars_accumulate_per_wave_to_the_daily_cap_across_reloads) {
    const std::string dir = tempDir("flix-wave-stars-" + std::to_string(::getpid()));
    const std::string path = dir + "/inventory.json";
    std::string error;
    Database db;
    CHECK(db.load(path, error));
    waveStarClock = 100 * 86400000ll;
    db.setClock(waveStarNow);
    auto& legacy = db.progress("wave-account");
    legacy.stars = 1500.0;
    legacy.extra["waveStars"]["day"] = 100;
    legacy.extra["waveStars"]["earned"] = 1500;
    for (int wave = 0; wave < 19; ++wave)
        CHECK_EQ(db.awardWaveStars("wave-account"), 7500);
    CHECK_NEAR(db.progress("wave-account").stars, 144000.0, 1e-9);
    CHECK(db.save());

    Database restarted;
    CHECK(restarted.load(path, error));
    restarted.setClock(waveStarNow);
    bool reachedDailyCap = false;
    CHECK(!restarted.waveDailyStarCapReached("wave-account"));
    CHECK_EQ(restarted.awardWaveStars("wave-account", &reachedDailyCap), 6000);
    CHECK(restarted.waveDailyStarCapReached("wave-account"));
    CHECK(!restarted.waveDailyStarCapReached("other-account"));
    CHECK(reachedDailyCap);
    CHECK_EQ(restarted.awardWaveStars("wave-account", &reachedDailyCap), 0);
    CHECK(reachedDailyCap);
    CHECK_NEAR(restarted.progress("wave-account").stars, 150000.0, 1e-9);
    CHECK_EQ(restarted.awardWaveStars("other-account"), 7500);
    waveStarClock += 86400000ll;
    CHECK(!restarted.waveDailyStarCapReached("wave-account"));
    CHECK_EQ(restarted.awardWaveStars("wave-account"), 7500);
    CHECK(restarted.awardWaveBonusHammer("wave-account"));
    CHECK(!restarted.awardWaveBonusHammer("wave-account"));
    CHECK_EQ(restarted.progress("wave-account").itemCount(Rarity::Unique, "petal_timed_mjolnir"), 1);
    CHECK(restarted.save());
    Database hammerReload;
    hammerReload.setClock(waveStarNow);
    CHECK(hammerReload.load(path, error));
    CHECK(!hammerReload.awardWaveBonusHammer("wave-account"));
    waveStarClock += 86400000ll;
    CHECK(hammerReload.awardWaveBonusHammer("wave-account"));
    CHECK_NEAR(restarted.progress("wave-account").stars, 157500.0, 1e-9);
}


TEST(practice_history_is_private_persistent_and_shared_per_score) {
    const std::string dir = tempDir("flix-practice-history-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/inventory.json";
    seedUser(path, "tester_one", "password7");
    seedUser(path, "tester_two", "password7");
    std::string error;
    Database db;
    if (!db.load(path, error)) { CHECK(false); return; }
    const auto* first = db.findUser("tester_one");
    const auto* second = db.findUser("tester_two");
    if (!first || !second) { CHECK(false); return; }
    const std::string owner = first->id, viewer = second->id;
    const std::string basic = R"([{"slot":1,"petal":"basic","tier":0}])";
    const std::string leaf = R"([{"slot":2,"petal":"leaf","tier":0}])";
    savePracticeScore(db, owner, basic, 1000.0);
    savePracticeScore(db, viewer, leaf, 500.0);
    const auto before = practiceBoardView(db, viewer, false);
    CHECK_EQ(before["rank"].asInt(), 2);
    CHECK(before["public"].items().empty());
    CHECK_EQ(before["own"].size(), 1u);
    const auto ownerView = practiceBoardView(db, owner, false);
    CHECK_EQ(ownerView["own"].size(), 1u);
    if (ownerView["own"].items().empty()) return;
    const auto id = static_cast<std::uint32_t>(ownerView["own"][std::size_t(0)]["id"].asDouble());
    sharePracticeScore(db, owner, id, true);
    savePracticeScore(db, owner, basic, 1300.0);
    savePracticeScore(db, owner, leaf, 1500.0);
    CHECK(db.save());
    Database reloaded;
    CHECK(reloaded.load(path, error));
    const auto fromOther = practiceBoardView(reloaded, viewer, false);
    CHECK_EQ(fromOther["rank"].asInt(), 2);
    CHECK_EQ(fromOther["public"].size(), 1u);
    if (fromOther["public"].items().empty()) return;
    CHECK_EQ(fromOther["public"][std::size_t(0)]["damage"].asDouble(), 1000.0);
    CHECK_EQ(fromOther["own"][std::size_t(0)]["damage"].asDouble(), 500.0);
    sharePracticeScore(reloaded, owner, id, false);
    CHECK(practiceBoardView(reloaded, viewer, false)["public"].items().empty());
    CHECK_EQ(practiceBoardView(reloaded, owner, false)["own"].size(), 3u);
}


TEST(practice_history_round_trips_real_timed_trial_and_publication) {
    Harness h("practice-trial", [](const std::string& path) {
        seedUser(path, "trial_owner", "password7");
        seedUser(path, "trial_viewer", "password7");
    }, dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }
    NetClient owner, viewer;
    if (!logIn(h, owner, "trial_owner") || !join(h, owner, "trial_owner", false) ||
        !logIn(h, viewer, "trial_viewer") || !join(h, viewer, "trial_viewer", false)) {
        CHECK(false);
        return;
    }
    auto& world = h.server.world();
    const auto player = bodyNamed(world, "trial_owner");
    bool found = false;
    const auto jungle = h.server.worldMaps().realmOfId("jungle", found);
    if (!found || player == NULL_ENTITY) { CHECK(false); return; }
    const auto dummyType = shippedContent().mobIndex("target_dummy");
    Entity dummy = NULL_ENTITY;
    const auto testPoint = Vec2{12148.17, 8225.17};
    GameServerPeer::markTeleport(h.server, player, jungle, testPoint + Vec2{0, 230});
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        Query<Npc, Transform>{world}.each([&](Entity e, Npc& npc, Transform& at) {
            if (npc.configIndex == dummyType && npc.rarity == Rarity::Super && at.realm == jungle) {
                dummy = e;
            }
        });
        return dummy != NULL_ENTITY;
    }));
    if (dummy == NULL_ENTITY) return;
    const auto* ownerSession = GameServerPeer::sessionOf(h.server, "trial_owner");
    if (!ownerSession) { CHECK(false); return; }
    owner.requestPracticeBoard(false, net::PracticeBoardAction::Start);
    CHECK(h.stepUntil({&owner, &viewer}, [&] { return ownerSession->practiceTrial.player == player; }));
    if (ownerSession->practiceTrial.player != player) return;
    auto& combat = GameServerPeer::practiceCombat(h.server);
    combat.applyDamage(world, dummy, player, 1000.0,
                       GameServerPeer::practiceClock(h.server), DamageKind::Reflect);
    h.clock += 10000;
    h.step(4, {&owner, &viewer});
    owner.requestPracticeBoard(false);
    viewer.requestPracticeBoard(false);
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        return !owner.practiceBoard().pending && !viewer.practiceBoard().pending;
    }));
    CHECK_EQ(owner.practiceBoard().own.size(), 1u);
    CHECK(viewer.practiceBoard().own.empty());
    CHECK(viewer.practiceBoard().published.empty());
    if (owner.practiceBoard().own.empty()) return;
    const auto& row = owner.practiceBoard().own.front();
    const auto id = row.id;
    CHECK(!row.published);
    CHECK(row.damage > 0.0);
    owner.requestPracticeBoard(false, net::PracticeBoardAction::Publish, id);
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        return !owner.practiceBoard().pending && !owner.practiceBoard().own.empty() &&
            owner.practiceBoard().own.front().published;
    }));
    viewer.requestPracticeBoard(false);
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        return !viewer.practiceBoard().pending && viewer.practiceBoard().published.size() == 1;
    }));
    CHECK(viewer.practiceBoard().own.empty());
    owner.requestPracticeBoard(false, net::PracticeBoardAction::Hide, id);
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        return !owner.practiceBoard().pending && !owner.practiceBoard().own.empty() &&
            !owner.practiceBoard().own.front().published;
    }));
    viewer.requestPracticeBoard(false);
    CHECK(h.stepUntil({&owner, &viewer}, [&] {
        return !viewer.practiceBoard().pending && viewer.practiceBoard().published.empty();
    }));
}
