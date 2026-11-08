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

TEST(prestige_is_one_root_node_costing_225) {
    CHECK_EQ(skillTierCount(SkillId::Prestige), 1);
    CHECK_EQ(skillTierCost(SkillId::Prestige, 0), 225);
    CHECK(skillGate(SkillId::Prestige) == nullptr);
    CHECK(skillFromKey("prestige") == SkillId::Prestige);

    SkillSet skills;
    CHECK(!skills.prestigeUnlocked());
    CHECK(skills.prerequisiteMet(SkillId::Prestige));
    skills.set(SkillId::Prestige, 0);
    CHECK(skills.prestigeUnlocked());
    CHECK_EQ(skills.spent(), 225);
    CHECK_EQ(availableTalentPoints(113, skills), 1);
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

TEST(prestige_is_bought_with_225_points_and_not_on_the_maze_tree) {
    Harness h("prestige-buy",
              [](const std::string& path) {
                  seedUser(path, "rich", "password7");
                  seedUser(path, "poor", "password7");
                  seedProgress(path, "rich", false, 113);
                  seedProgress(path, "poor", false, 112);
              },
              dataDir(), 0);
    if (!h.ready) { CHECK(false); return; }

    NetClient poor;
    CHECK(logIn(h, poor, "poor"));
    CHECK(awaitProfile(h, poor, [](const Profile& p) { return p.level == 112; }));
    poor.requestUpgradeSkill(SkillId::Prestige, 0);
    CHECK(h.stepUntil({&poor}, [&] { return sawText(poor, "need 225"); }, 60));
    CHECK(!poor.profile().skills.prestigeUnlocked());

    NetClient rich;
    CHECK(logIn(h, rich, "rich"));
    CHECK(awaitProfile(h, rich, [](const Profile& p) { return p.level == 113; }));
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

    world.get<Transform>(walker).position = {2000.0, 2000.0};
    world.get<Transform>(climber).position = {2200.0, 2200.0};
    GameServerPeer::advancePyramidCycle(h.server);
    const auto newOrdinary = realmOf(world, walker);
    const auto newPrestige = realmOf(world, climber);
    CHECK(newOrdinary != oldOrdinary);
    CHECK(newPrestige != oldPrestige);
    CHECK(maps.isPrestige(newPrestige));
    for (const auto player : {walker, climber}) {
        const auto& at = world.get<Transform>(player);
        const auto doors = maps.forRealm(at.realm)->playerSpawns();
        CHECK(!doors.empty());
        if (!doors.empty()) CHECK(doors.front()->bounds.contains(at.position));
        CHECK(!h.server.terrain().blocked(at.position, at.realm));
    }
    for (int i = 0; i < 20; ++i) {
        h.server.serviceNetwork(1);
        ordinary.poll(1);
        prestige.poll(1);
    }
    CHECK(ordinary.view().realm() == newOrdinary);
    CHECK(prestige.view().realm() == newPrestige);
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
}

TEST(wave_room_is_capped_revives_at_the_next_wave_and_pays_no_bounties) {
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

    for (int round = 1; round <= 5; ++round) {
        const double now = 16000.0 + (round - 1) * 3600.0;
        tick(now);
        const auto mobs = Query<MobTag>{world}.collect();
        CHECK_EQ(mobs.size(), static_cast<std::size_t>(round == 5 ? 3 : 10 + round));
        if (round == 2) CHECK(isStanding(world, members[1]));
        for (const Entity mob : mobs) {
            CHECK(!world.has<Bounty>(mob));
            CHECK(CombatSystem::canDamage(world, members[0], mob));
            if (round == 5) CHECK(world.get<MobType>(mob).rarity == Rarity::Super);
            world.destroy(mob);
        }
        if (round == 1) {
            world.get<Health>(members[1]).current = 0.0;
            world.add<Dead>(members[1]);
        }
        tick(now + 50.0);
    }
    for (const Entity e : members) {
        CHECK(world.get<Transform>(e).realm == Realm::Overworld);
        CHECK(!world.has<WavePlayer>(e));
    }
    CHECK(CombatSystem::canDamage(world, members[0], members[1]));
}
