// Prestige: the talent, the prestige twin maps, what grows in them, and the
// join that reaches them. See shared/game/prestige.h.

#include "test.h"

#include "server_harness.h"
#include "server/db.h"
#include "server/systems/spawning.h"
#include "server/systems/combat.h"
#include "server/systems/dungeons.h"
#include "shared/game/prestige.h"
#include "shared/game/difficulty.h"
#include "shared/game/map_elements.h"
#include "shared/game/skills.h"
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

TEST(prestige_jungle_wakes_local_mobs_at_the_shared_ambient_limit) {
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
    guest.sendChat("/wave leave");
    CHECK(h.stepUntil({&host, &guest}, [&] {
        return !world.has<WavePlayer>(guestBody) && guest.view().realm() == origin;
    }));
    CHECK(world.has<WavePlayer>(hostBody));
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
        h.clock += 15000.0;
        bool sawHyper = false;
        for (int wave = 1; wave <= wavesPerRun; ++wave) {
            total += 7500.0;
            CHECK(h.stepUntil({&host, &guest}, [&] {
                for (const Entity enemy : Query<WaveEnemy>{world}.collect()) {
                    if (!isStanding(world, enemy)) continue;
                    if (world.get<MobType>(enemy).rarity == Rarity::Apex) sawHyper = true;
                    world.get<Bounty>(enemy).credit(hostBody, world.get<Health>(enemy).max);
                    world.add<Dead>(enemy);
                }
                return host.profile().stars >= total;
            }, 850));
            CHECK_NEAR(host.profile().stars, total, 1e-9);
        }
        CHECK(sawHyper);
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
    dungeon.waveAward = [&](Entity player, std::uint64_t round) {
        if (player == members[3]) {
            CHECK_EQ(round, std::uint64_t(1));
            ++guestAwards;
            return 7500;
        }
        CHECK(player == members[0]);
        ++hostAwards;
        CHECK_EQ(round, static_cast<std::uint64_t>(hostAwards));
        return round <= 20 ? 7500 : 0;
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
    tick(16000.0);
    CHECK(Query<WaveEnemy>{world}.collect().empty());
    CHECK(dungeon.inWave(world, members[0]));
    spawner.mobCap = kMaxLiveMobs;

    int peakLive = 0;
    int total = 0;
    for (int round = 1; round <= 21; ++round) {
        // A successful session can pass the old ten-minute limit.
        double now = 16100.0 + (round - 1) * 32000.0;
        const int expected = 54 + 7 * std::min(round - 1, 12);
        int seen = 0, elites = 0, hypers = 0;
        bool firstBatch = true;
        for (int batch = 0; batch < 20 && seen < expected; ++batch, now += 650.0) {
            tick(now);
            const auto mobs = Query<WaveEnemy>{world}.collect();
            if (firstBatch) {
                CHECK_EQ(mobs.size(), std::size_t(12));
                if (round == 2) CHECK(isStanding(world, members[1]));
                firstBatch = false;
            }
            CHECK(mobs.size() <= 60);
            peakLive = std::max(peakLive, static_cast<int>(mobs.size()));
            for (std::size_t i = 0; i < mobs.size(); ++i) {
                const Entity mob = mobs[i];
                const auto& at = world.get<Transform>(mob);
                const auto& hp = world.get<Health>(mob);
                const auto& body = world.get<Body>(mob);
                const auto& type = world.get<MobType>(mob);
                CHECK(at.realm == realm);
                CHECK(!terrain.resolveWall(at.position, body.radius, realm).collided);
                CHECK_EQ(world.get<Bounty>(mob).xp, 0.0);
                CHECK(CombatSystem::canDamage(world, members[0], mob));
                CHECK(body.radius <= 92.0);
                if (round == 1)
                    CHECK_NEAR(hp.max, content.mobStats(type.configIndex, type.rarity).health * 1.12, 1e-6);
                if (type.rarity == Rarity::Apex) {
                    CHECK_EQ(type.configIndex, content.mobIndex("beetle"));
                    CHECK(hp.max > content.mobStats(type.configIndex, Rarity::Super).health * 2.0);
                    CHECK(hp.max < content.mobStats(type.configIndex, Rarity::Apex).health);
                }
                if (round == 1 || round == 21) {
                    for (std::size_t j = 0; j < i; ++j) {
                        CHECK(distance(at.position, world.get<Transform>(mobs[j]).position) >=
                              body.radius + world.get<Body>(mobs[j]).radius);
                    }
                    for (int j = 0; j < 4; ++j) {
                        CHECK(distance(at.position, world.get<Transform>(members[j]).position) >=
                              body.radius + world.get<Body>(members[j]).radius + 150.0);
                    }
                }
                if (round == 21)
                    CHECK(hp.max > content.mobStats(type.configIndex, type.rarity).health * 2.0);
            }

            // Let monsters fill the room before clearing them, including the
            // reinforcement cap once the wave has more than 60 in reserve.
            if (mobs.size() < static_cast<std::size_t>(std::min(60, expected - seen))) continue;
            for (const Entity mob : mobs) {
                const double hp = world.get<Health>(mob).max;
                world.get<Bounty>(mob).credit(members[0], hp);
                world.get<Bounty>(mob).credit(members[2], hp * 0.005); // Below participation floor.
                if (round == 1) world.get<Bounty>(mob).credit(members[3], hp * 0.01);
                elites += world.get<MobType>(mob).rarity == Rarity::Super;
                hypers += world.get<MobType>(mob).rarity == Rarity::Apex;
                world.add<Dead>(mob);
            }
            seen += static_cast<int>(mobs.size());
            if (round == 1) {
                world.get<Health>(members[1]).current = 0.0;
                world.add<Dead>(members[1]);
            }
            tick(now + 50.0);
            for (const Entity mob : mobs) world.destroy(mob);
        }
        CHECK_EQ(seen, expected);
        CHECK_EQ(hypers, round % 5 == 0 ? 1 : 0);
        if (round == 5) CHECK_EQ(elites, 6);
        if (round == 21) CHECK(elites > 10);
        total += seen;
        CHECK(dungeon.inWave(world, members[0]));
    }
    CHECK_EQ(peakLive, 60);
    CHECK_EQ(hostAwards, 21);
    CHECK_EQ(guestAwards, 1);
    CHECK(spawner.bossSpawns.empty());
    CHECK(summaries.empty());
    std::printf("[wave] %d enemies in 21 waves, peak %d, paid each of the first 20 clears\n", total, peakLive);

    dungeon.waveCommand(world, terrain, members[0], "leave", rng, 670000.0);
    CHECK(summaries[members[0]].find("通关 21 波 · 本局 +150000 星星") != std::string::npos);
    tick(673000.0);
    tick(854000.0);
    CHECK(summaries[members[1]].find("连续 3 分钟未推进") != std::string::npos);
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
    CHECK_EQ(restarted.awardWaveStars("wave-account"), 6000);
    CHECK_EQ(restarted.awardWaveStars("wave-account"), 0);
    CHECK_NEAR(restarted.progress("wave-account").stars, 150000.0, 1e-9);
    CHECK_EQ(restarted.awardWaveStars("other-account"), 7500);
    waveStarClock += 86400000ll;
    CHECK_EQ(restarted.awardWaveStars("wave-account"), 7500);
    CHECK_NEAR(restarted.progress("wave-account").stars, 157500.0, 1e-9);
}
