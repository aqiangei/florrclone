#include "server/shop_lock.h"
#include "shared/game/shop.h"
#include "server/daily_drop_bonus.h"
#include "server/timed_hammers.h"
#include "server/hyper_forge.h"
// NPCs, and the first two with a service: the oracle and the trader.
//
// An NPC is a mob's config standing in the world without being a mob
// (shared/game/npc.h). These hold the whole chain to account: the map places
// one, its pool never moves -- a friendly one refuses every hit, a hostile one
// like the target dummy takes them all and loses nothing -- it watches whoever
// walks up, it is streamed as its own kind and drawn with the mob's plate and
// an invulnerable bar, an admin can still spawn the same creature as an enemy,
// and the oracle's service, a guaranteed craft at a fixed price, is charged
// exactly that price and is refused to anyone not standing at it. The trader's,
// one petal for one coin of its tier once a day, is held to the same.

#include "test.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "client/camera.h"
#include "client/render/sprites.h"
#include "client/render/world_renderer.h"
#include "client/ui/theme.h"
#include "client/ui/i18n.h"
#include "client/world_view.h"
#include "fixture_content.h"
#include "server/db.h"
#include "server/systems/combat.h"
#include "server/systems/movement.h"
#include "server/systems/npcs.h"
#include "server_harness.h"
#include "shared/game/map_elements.h"

using namespace flix;
using namespace flix::testsupport;

namespace {

/// An account joinAs() below can log in: every one here shares its password.
/// (seedStack, awaitProfile, bodyNamed, onlyPlayer and sawText are the harness's
/// own, in server_harness.h.)
void seedAccount(const std::string& path, const std::string& username, bool admin = false) {
    seedUser(path, username, "password7", admin);
}

/// Where the fixture's oracle stands: a cell and a half east of the one-cell
/// door, so a flower put down anywhere in the door is inside the server's
/// reach of it without starting on top of it.
constexpr double kOracleX = kTileSize * 3.5;
constexpr double kOracleY = kTileSize * 2.5;

/// A 24x24 walled field: one small door, and whatever `npcs` the test names.
std::string npcWorld(const std::string& name, const std::string& npcs) {
    const double cell = kTileSize;
    const std::string map =
        fixtureMap(24, 24, fixtureDoor("meadow", "Meadow", cell * 2, cell * 2, cell, cell, true, 0.0),
                   std::string(), std::string(), {}, {}, {}, npcs);
    return stageDataDir(name, {{"meadow", map}});
}

std::string oracleWorld(const std::string& name) {
    return npcWorld(name, fixtureNpc(kOracleX, kOracleY, "oracle", "epic"));
}

/// The NPC standing in `world` that wears mob `id`, or NULL_ENTITY.
Entity npcWearing(World& world, const char* id) {
    Entity found = NULL_ENTITY;
    Query<NpcTag, Npc> npcs{world};
    npcs.each([&](Entity e, NpcTag&, Npc& npc) {
        if (npc.configIndex == content().mobIndex(id)) found = e;
    });
    return found;
}

/// Steps until every petal `flower` has equipped is out. A new flower's ring
/// arrives a couple of seconds after it does, and a test that starts counting
/// hits before then is counting the wait.
bool awaitRing(Harness& h, const std::vector<NetClient*>& clients, World& world, Entity flower) {
    return h.stepUntil(clients, [&] {
        const Loadout& loadout = world.get<Loadout>(flower);
        std::size_t equipped = 0;
        for (const LoadoutSlot& slot : loadout.slots) equipped += slot.empty() ? 0 : 1;
        return equipped > 0 && loadout.spawned.size() >= equipped;
    }, 200);
}

/// One full turn of the ring and a little over: every petal passes a given
/// point once, which is as many hits as a ring of petals that each break on
/// their first touch can land.
constexpr int kOneRingTurnTicks = static_cast<int>(kTau / kPetalSpinRate * net::kTicksPerSecond) + 15;

/// Where `flower` stands to sweep its ring through `npc` without touching it:
/// just off the NPC's skin, on its west side. The ring orbits wider than the
/// flower's own body, so from here the petals pass through the NPC while the
/// body stays clear of a hostile one's bite -- a flower parked on the ring's
/// own radius is flush against the dummy, and is bitten to death in seconds.
Vec2 ringOnly(World& world, Entity npc, Entity flower) {
    const double standoff = world.get<Body>(npc).radius + world.get<Body>(flower).radius + 8.0;
    return world.get<Transform>(npc).position - Vec2{standoff, 0.0};
}

/// Stands the one flower just clear of the NPC wearing `id`, wherever its
/// cruise has taken it, and lets a tick pass so the server has it there. The
/// oracle does not hold still any more, so "at the oracle" is a place that has
/// to be looked up at the moment it is needed.
void standBeside(Harness& h, NetClient& client, const char* id) {
    World& world = h.server.world();
    const Entity npc = npcWearing(world, id);
    const Entity player = onlyPlayer(world);
    if (npc == NULL_ENTITY || player == NULL_ENTITY) return;
    const Vec2 at = world.get<Transform>(npc).position;
    world.get<Transform>(player).position = at + Vec2{world.get<Body>(npc).radius + 60.0, 0.0};
    h.step(1, {&client});
}

/// Logs `name` in and puts it in the world, waiting for both.
bool joinAs(Harness& h, NetClient& client, const char* name) {
    if (!connectClient(h, client)) return false;
    client.requestLogin(name, "password7");
    if (!h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::LoggedIn; })) {
        return false;
    }
    client.joinGame(1920, 1080, {}, name);
    return h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                       200);
}

std::vector<Entity> npcsIn(World& world) {
    std::vector<Entity> out;
    Query<NpcTag> npcs{world};
    npcs.each([&](Entity e, NpcTag&) { out.push_back(e); });
    return out;
}

/// Waits for the oracle's answer and hands it back with `pending` cleared, as
/// the panel reads it.
bool awaitOracle(Harness& h, NetClient& client, OracleOutcome& out) {
    if (!h.stepUntil({&client}, [&] { return client.oracleOutcome().pending; }, 200)) return false;
    out = client.oracleOutcome();
    client.oracleOutcome().pending = false;
    return true;
}

bool ensureShippedContent() {
    std::string error;
    return loadContent(dataDir(), error);
}

} // namespace

// ---------------------------------------------------------------------------
// The price list and the content
// ---------------------------------------------------------------------------

TEST(the_oracle_charges_the_stated_price_for_every_tier) {
    // The design's own numbers, tier by tier -- "unusual" is this game's
    // uncommon. Apex and universal cost nothing because nothing crafts out of
    // either.
    const int expected[] = {7, 11, 19, 34, 65, 128, 253, 506, 0, 0, 0, 0};
    static_assert(sizeof(expected) / sizeof(expected[0]) == kRarityCount);
    for (int i = 0; i < kRarityCount; ++i) {
        CHECK_EQ(oracleCraftCost(static_cast<Rarity>(i)), expected[i]);
    }
}

TEST(the_shipped_oracle_is_a_mob_that_offers_a_service) {
    CHECK(ensureShippedContent());
    const std::uint16_t oracle = content().mobIndex("oracle");
    CHECK(oracle != kInvalidIndex);
    if (oracle == kInvalidIndex) return;
    const MobConfig& config = content().mob(oracle);
    CHECK(config.npc.present);
    CHECK(config.npc.service == NpcService::Oracle);
    CHECK(config.npc.team == Team::Players);
    // Drawn by code, not a document: the reference art is a 12-frame capture
    // whose eye has to move.
    CHECK_EQ(config.image, std::string("$oracle"));
    // Never grown by the ground and never dropped as an egg: an oracle in the
    // world is one a map or an admin put there.
    CHECK(config.noEggDrop);
    CHECK(!content().mobStats(oracle, Rarity::Common).ambient);
    // The target dummy is another NPC: on the hostiles' side, so a flower
    // can hit it, and offering nothing but that. The trader is the third, the
    // titan the fourth and the assembler the fifth (their own tests below).
    // Nothing else in the shipped content is an NPC by accident.
    const std::uint16_t dummy = content().mobIndex("target_dummy");
    CHECK(dummy != kInvalidIndex);
    if (dummy != kInvalidIndex) {
        CHECK(content().mob(dummy).npc.present);
        CHECK(content().mob(dummy).npc.team == Team::Hostiles);
        CHECK(content().mob(dummy).npc.service == NpcService::None);
    }
    const std::uint16_t trader = content().mobIndex("trader");
    const std::uint16_t titan = content().mobIndex("titan");
    const std::uint16_t assembler = content().mobIndex("assembler");
    for (std::uint16_t i = 0; i < content().mobCount(); ++i) {
        if (i == oracle || i == dummy || i == trader || i == titan || i == assembler || content().mob(i).id=="cyber_assembler") continue;
        CHECK(!content().mob(i).npc.present);
    }
}

TEST(an_npc_block_is_read_and_its_unknowns_are_reported) {
    ContentRegistry registry;
    std::string error;
    // A service or a side this build does not know is SAID, and read as the
    // default -- the mob is still an NPC, so a map that places one still gets
    // something standing there. A block that is not an object is no NPC.
    const std::string mobs = R"({
        "seer": { "name": "Seer", "health": 10, "npc": { "service": "oracle" } },
        "fake": { "name": "Fake", "health": 10, "npc": { "service": "banker" } },
        "post": { "name": "Post", "health": 10, "npc": { "team": "hostile" } },
        "odd": { "name": "Odd", "health": 10, "npc": { "team": "pirates" } },
        "flat": { "name": "Flat", "health": 10, "npc": "oracle" },
        "wild": { "name": "Wild", "health": 10 }
    })";
    const std::string petals = R"({ "basic": { "name": "Basic" } })";
    const std::string dir = tempDir("florr-npc-config-" + std::to_string(::getpid()));
    CHECK(test::loadFixtureContent(registry, dir, mobs, petals, error));
    const auto spec = [&](const char* id) { return registry.mob(registry.mobIndex(id)).npc; };
    CHECK(spec("seer").present);
    CHECK(spec("seer").service == NpcService::Oracle);
    CHECK(spec("seer").team == Team::Players);
    CHECK(spec("fake").present);
    CHECK(spec("fake").service == NpcService::None);
    CHECK(spec("post").present);
    CHECK(spec("post").team == Team::Hostiles);
    CHECK(spec("odd").present);
    CHECK(spec("odd").team == Team::Players);
    CHECK(!spec("flat").present);
    CHECK(!spec("wild").present);
    const auto warned = [&](const char* needle) {
        for (const std::string& line : registry.warnings()) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    };
    CHECK(warned("banker"));
    CHECK(warned("pirates"));
    CHECK(warned("npc is"));
    removeDataDir(dir);
}

// ---------------------------------------------------------------------------
// The map
// ---------------------------------------------------------------------------

TEST(an_npc_object_is_a_point_naming_its_mob_and_tier) {
    CHECK(ensureShippedContent());
    // Tiled is where an author types "Epic", so the tier is read case-blind;
    // an object naming no mob is dropped rather than placed as nothing.
    const std::string dir = npcWorld("npc-map", fixtureNpc(700.0, 900.0, "oracle", "Epic") + "," +
                                                    fixtureNpc(300.0, 300.0, "", "rare"));
    if (dir.empty()) { CHECK(false); return; }
    MapData map;
    std::string error;
    CHECK(map.loadTiled(dir + "/meadow.tmj", error));
    int npcs = 0;
    for (const MapElement& element : map.elements()) {
        if (element.kind != MapElementKind::Npc) continue;
        ++npcs;
        CHECK_EQ(element.npcId, std::string("oracle"));
        CHECK(element.npcRarity == Rarity::Epic);
        CHECK_NEAR(element.bounds.x, 700.0, 1e-6);
        CHECK_NEAR(element.bounds.y, 900.0, 1e-6);
    }
    CHECK_EQ(npcs, 1);
    removeDataDir(dir);
}

TEST(a_site_that_names_no_npc_is_reported_and_left_empty) {
    CHECK(ensureShippedContent());
    // A mob that offers nothing, and a mob that does not exist: both are
    // typos for an NPC someone meant, and both say so.
    const std::string dir = npcWorld(
        "npc-sites", fixtureNpc(700.0, 700.0, "oracle", "common") + "," +
                         fixtureNpc(900.0, 700.0, "bee", "common") + "," +
                         fixtureNpc(1100.0, 700.0, "not_a_mob", "common"));
    if (dir.empty()) { CHECK(false); return; }
    MapData map;
    std::string error;
    CHECK(map.loadTiled(dir + "/meadow.tmj", error));
    WorldMaps maps;
    maps.adoptSingle(std::move(map));

    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK_EQ(npcs.sites().size(), std::size_t(1));
    CHECK_EQ(warnings.size(), std::size_t(2));
    if (!npcs.sites().empty()) {
        CHECK_EQ(npcs.sites()[0].mobIndex, content().mobIndex("oracle"));
    }
    removeDataDir(dir);
}

TEST(an_npc_object_classed_with_its_layers_name_is_read) {
    CHECK(ensureShippedContent());
    // "npcs" on the "npcs" layer is what the jungle's author typed, and every
    // dummy on it was dropped for not saying "npc". The layer's name is as
    // plain a statement of kind as the singular; another layer's kind is not.
    std::string layerNamed = fixtureNpc(700.0, 900.0, "target_dummy", "rare");
    std::string otherKind = fixtureNpc(300.0, 300.0, "oracle", "common");
    const std::string npcClass = "\"type\": \"npc\"";
    layerNamed.replace(layerNamed.find(npcClass), npcClass.size(), "\"type\": \"npcs\"");
    otherKind.replace(otherKind.find(npcClass), npcClass.size(), "\"type\": \"spawn\"");
    const std::string dir = npcWorld("npc-layer-class", layerNamed + "," + otherKind);
    if (dir.empty()) { CHECK(false); return; }
    MapData map;
    std::string error;
    CHECK(map.loadTiled(dir + "/meadow.tmj", error));
    int npcs = 0;
    for (const MapElement& element : map.elements()) {
        if (element.kind != MapElementKind::Npc) continue;
        ++npcs;
        CHECK_EQ(element.npcId, std::string("target_dummy"));
        CHECK(element.npcRarity == Rarity::Rare);
    }
    CHECK_EQ(npcs, 1);
    removeDataDir(dir);
}

TEST(the_shipped_jungle_stands_its_target_dummies) {
    CHECK(ensureShippedContent());
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    Realm jungle = Realm::Overworld;
    bool found = false;
    for (std::size_t slot = 0; slot < maps.maps().size(); ++slot) {
        if (maps.maps()[slot].id() != "jungle") continue;
        jungle = maps.maps()[slot].realm();
        found = true;
    }
    CHECK(found);
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    int dummies = 0;
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (site.realm != jungle || site.mobIndex != content().mobIndex("target_dummy")) continue;
        ++dummies;
    }
    // One plot per tier, common to apex. Whatever the count becomes, zero is
    // the bug: a whole layer dropped on the class of its objects.
    CHECK(dummies > 0);
    CHECK_EQ(dummies, kLadderRarityCount);
}

TEST(the_shipped_oracle_stands_on_open_ground_where_it_was_drawn) {
    // The one place a petal is crafted up, so the shipped world has to hold an
    // oracle a flower can walk up to. WHICH map it stands on is the author's
    // call -- it was drawn in the garden first and has since moved to the
    // ocean -- so the realm is not pinned: whichever map draws it, it is a map
    // of the catalogue, and the oracle stands on open ground in it.
    CHECK(ensureShippedContent());
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK(warnings.empty());
    int oracles = 0;
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (content().mob(site.mobIndex).npc.service != NpcService::Oracle) continue;
        ++oracles;
        CHECK(maps.forRealm(site.realm) != nullptr);
        // Standing where it was drawn: a centre pushed out of a wall would be
        // somewhere its author did not put it. An NPC meets walls as a point.
        const Vec2 placed = terrain.resolveCircle(site.position, kMobWallRadius, site.realm);
        CHECK_NEAR(placed.x, site.position.x, 1e-6);
        CHECK_NEAR(placed.y, site.position.y, 1e-6);
        CHECK(!terrain.blocked(site.position, site.realm));
    }
    CHECK(oracles >= 1);
}

TEST(an_npc_meets_walls_with_its_centre_not_its_body) {
    CHECK(ensureShippedContent());
    Terrain terrain;
    for (int ty = 0; ty < terrain.tileRows(); ++ty) terrain.setTile(10, ty, Tile::Solid);
    const double face = 10.0 * kTileSize;
    World world;
    NpcSystem npcs;
    const std::uint16_t oracle = content().mobIndex("oracle");

    // A big one, so a body-sized standoff would be unmistakable: drawn with
    // its centre just short of the wall, it stands exactly there.
    const Entity beside = npcs.spawnNpc(world, terrain, content(), oracle, Rarity::Mythic,
                                        {face - 5.0, 5000.0}, Realm::Overworld, 0.0);
    CHECK(world.get<Body>(beside).radius > 100.0);
    CHECK_NEAR(world.get<Transform>(beside).position.x, face - 5.0, 1e-9);

    // Drawn inside the wall: out to the face, not a body's width back from it.
    const Entity inside = npcs.spawnNpc(world, terrain, content(), oracle, Rarity::Mythic,
                                        {face + 20.0, 6000.0}, Realm::Overworld, 0.0);
    const Vec2 at = world.get<Transform>(inside).position;
    CHECK(!terrain.blocked(at, Realm::Overworld));
    CHECK(at.x <= face - kMobWallRadius + 1e-6);
    CHECK(at.x > face - 1.0);
}

TEST(a_target_dummy_npc_shrinks_with_its_tier_like_a_band_fixture) {
    // The dummy a map places as an NPC is the same practice target a band
    // plants: a common one is a common mob's size, a unique one three
    // quarters of a wild unique. An NPC that skipped the ramp stood a third
    // bigger than its band twin at unique.
    CHECK(ensureShippedContent());
    Terrain terrain;
    World world;
    NpcSystem npcs;
    const std::uint16_t dummy = content().mobIndex("target_dummy");
    const std::uint16_t oracle = content().mobIndex("oracle");
    const auto radiusOf = [&](std::uint16_t mob, Rarity rarity, double x) {
        const Entity e = npcs.spawnNpc(world, terrain, content(), mob, rarity, {x, 5000.0},
                                       Realm::Overworld, 0.0);
        return world.get<Body>(e).radius;
    };
    CHECK_NEAR(radiusOf(dummy, Rarity::Common, 2000.0),
               content().mobStats(dummy, Rarity::Common).radius, 1e-9);
    CHECK_NEAR(radiusOf(dummy, Rarity::Unique, 4000.0),
               content().mobStats(dummy, Rarity::Unique).radius * 0.75, 1e-9);
    // Only the fixture shrinks: an oracle stands at its mob's full size.
    CHECK_NEAR(radiusOf(oracle, Rarity::Unique, 6000.0),
               content().mobStats(oracle, Rarity::Unique).radius, 1e-9);
}

TEST(a_cruising_npc_flies_up_to_a_wall_with_its_centre) {
    // The oracle cruises on a leash about its home. Homed beside a wall, its
    // flight takes it against the wall, and it is the CENTRE that stops there:
    // measured from the body it could never come nearer than its radius.
    CHECK(ensureShippedContent());
    Terrain terrain;
    for (int ty = 0; ty < terrain.tileRows(); ++ty) terrain.setTile(10, ty, Tile::Solid);
    const double face = 10.0 * kTileSize;
    World world;
    NpcSystem npcs;
    const Entity oracle = npcs.spawnNpc(world, terrain, content(), content().mobIndex("oracle"),
                                        Rarity::Common, {face - 60.0, 5000.0},
                                        Realm::Overworld, 0.0);
    CHECK(world.get<Npc>(oracle).cruises);
    const double radius = world.get<Body>(oracle).radius;

    double now = 0.0;
    double closest = 1e30;
    int inside = 0;
    for (int i = 0; i < 3000; ++i) {
        npcs.run(world, terrain, content(), {}, now);
        now += net::kTickSeconds * 1000.0;
        const Vec2 at = world.get<Transform>(oracle).position;
        if (terrain.blocked(at, Realm::Overworld) || at.x >= face) ++inside;
        closest = std::min(closest, face - at.x);
    }
    CHECK_EQ(inside, 0);
    CHECK(closest < radius * 0.5);
}

// ---------------------------------------------------------------------------
// In the world
// ---------------------------------------------------------------------------

TEST(the_map_places_a_friendly_oracle_whose_pool_never_moves) {
    const std::string dir = oracleWorld("npc-place");
    Harness h("npc-place", [](const std::string& path) { seedAccount(path, "seer"); }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(5, {&client});

    World& world = h.server.world();
    const std::vector<Entity> npcs = npcsIn(world);
    CHECK_EQ(npcs.size(), std::size_t(1));
    if (npcs.size() != 1) { removeDataDir(dir); return; }
    const Entity oracle = npcs[0];

    const Npc& npc = world.get<Npc>(oracle);
    CHECK(npc.service == NpcService::Oracle);
    CHECK(npc.rarity == Rarity::Epic);
    // Its home is the map's mark, and its cruise keeps it about there.
    CHECK_NEAR(npc.home.x, kOracleX, 1e-6);
    CHECK_NEAR(npc.home.y, kOracleY, 1e-6);
    CHECK(npc.cruises);
    CHECK(distance(world.get<Transform>(oracle).position, npc.home) < kNpcLeashRadius);
    // On the players' side, wearing its mob's pool at its tier -- the one its
    // plate is drawn over -- full.
    CHECK(world.get<Faction>(oracle).team == Team::Players);
    CHECK(world.has<Health>(oracle));
    const MobStats stats = content().mobStats(content().mobIndex("oracle"), Rarity::Epic);
    CHECK_NEAR(world.get<Health>(oracle).max, stats.health, 1e-9);
    CHECK_NEAR(world.get<Health>(oracle).current, stats.health, 1e-9);
    // Not a mob: nothing that hunts, farms or recycles mobs can see it.
    CHECK(!world.has<MobTag>(oracle));
    CHECK(!world.has<MobType>(oracle));
    CHECK(!world.has<MobAi>(oracle));
    CHECK(!world.has<Motion>(oracle));

    // Streamed as its own kind, wearing the oracle's config.
    CHECK(h.stepUntil({&client}, [&] {
        for (const auto& entry : client.view().entities()) {
            if (entry.second.kind == net::EntityKind::Npc &&
                entry.second.typeIndex == content().mobIndex("oracle")) {
                return true;
            }
        }
        return false;
    }, 60));
    removeDataDir(dir);
}

TEST(an_npc_that_stands_turns_to_look_at_the_flower_beside_it) {
    // The dummy stands still, so it is the one that watches: walked round to
    // the far side of it, the facing follows the flower.
    const double dummyX = kTileSize * 8.0;
    const double dummyY = kTileSize * 8.0;
    const std::string dir =
        npcWorld("npc-look", fixtureNpc(dummyX, dummyY, "target_dummy", "common"));
    Harness h("npc-look", [](const std::string& path) { seedAccount(path, "seer"); }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity dummy = npcWearing(world, "target_dummy");
    const Entity player = onlyPlayer(world);
    if (dummy == NULL_ENTITY || player == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }

    for (const Vec2 at : {Vec2{dummyX, dummyY + 300.0}, Vec2{dummyX - 300.0, dummyY},
                          Vec2{dummyX + 300.0, dummyY - 10.0}}) {
        world.get<Transform>(player).position = at;
        h.step(2, {&client});
        const Transform& self = world.get<Transform>(dummy);
        const Vec2 flower = world.get<Transform>(player).position;
        const double toward = (flower - self.position).angle();
        CHECK(std::fabs(angleDelta(self.angle, toward)) < 0.05);
    }
    removeDataDir(dir);
}

TEST(the_oracle_looks_where_it_is_flying_even_with_a_flower_beside_it) {
    // The oracle cruises, and its eye is on where it is going: a flower
    // standing right next to it does not turn its head.
    const std::string dir = oracleWorld("npc-look-fly");
    Harness h("npc-look-fly", [](const std::string& path) { seedAccount(path, "seer"); }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity oracle = npcWearing(world, "oracle");
    const Entity player = onlyPlayer(world);
    if (oracle == NULL_ENTITY || player == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }

    int checked = 0;
    for (int i = 0; i < 120; ++i) {
        // Kept right beside it, on the side it is NOT flying toward, so a head
        // that turned to the flower would be pointing the wrong way.
        const Transform& at = world.get<Transform>(oracle);
        const double side = at.angle + kPi * 0.5;
        world.get<Transform>(player).position =
            at.position + Vec2::fromAngle(side, world.get<Body>(oracle).radius + 60.0);
        const Vec2 before = at.position;
        h.step(1, {&client});
        const Transform& after = world.get<Transform>(oracle);
        const Vec2 step = after.position - before;
        if (step.lengthSq() < 1e-6) continue;
        CHECK_NEAR(angleDelta(after.angle, step.angle()), 0.0, 1e-9);
        ++checked;
    }
    CHECK(checked > 60);
    removeDataDir(dir);
}

TEST(an_npc_pool_never_moves_whatever_lands_on_it) {
    // The one rule the damage path adds, asked of the damage path itself: a
    // hit on an NPC LANDS -- it is not refused, it reports what it was worth
    // -- and nothing comes off, not even a blow worth ten times the pool.
    World world;
    CombatSystem combat;
    const auto body = [&](Team team, double health) {
        const Entity e = world.create();
        world.add<Transform>(e, Transform{{0.0, 0.0}, 0.0});
        world.add<Body>(e, Body{20.0, 1.0});
        world.add<Health>(e, Health{health, health, 0.0, 0.0});
        world.add<Faction>(e, Faction{team, false});
        return e;
    };
    const Entity flower = body(Team::Players, 100.0);
    world.add<PlayerTag>(flower);
    const Entity dummy = body(Team::Hostiles, 100.0);
    world.add<NpcTag>(dummy);
    // The control: the same body as a mob loses what it is hit for.
    const Entity mob = body(Team::Hostiles, 100.0);
    world.add<MobTag>(mob);

    const DamageResult hit = combat.applyDamage(world, dummy, flower, 40.0, 1000.0);
    CHECK(!hit.refused);
    CHECK_NEAR(hit.applied, 40.0, 1e-9);
    CHECK_NEAR(world.get<Health>(dummy).current, 100.0, 1e-9);
    CHECK(world.get<Health>(dummy).flashUntilMillis > 1000.0);

    const DamageResult overkill = combat.applyDamage(world, dummy, flower, 1000.0, 2000.0);
    CHECK(!overkill.refused);
    CHECK(!overkill.killed);
    CHECK_NEAR(world.get<Health>(dummy).current, 100.0, 1e-9);
    CHECK(!world.has<Dead>(dummy));

    combat.applyDamage(world, mob, flower, 40.0, 1000.0);
    CHECK_NEAR(world.get<Health>(mob).current, 60.0, 1e-9);
}

TEST(a_friendly_npc_refuses_the_hit_its_team_would_allow) {
    // Team rules already keep a flower's petals off the oracle. What they do
    // NOT keep off it is the other side: a mob wandering through it is a
    // hostile body touching a player-side one. That hit is refused, where the
    // same body without the tag -- a summon, say -- takes it.
    World world;
    CombatSystem combat;
    const auto body = [&](Team team) {
        const Entity e = world.create();
        world.add<Transform>(e, Transform{{0.0, 0.0}, 0.0});
        world.add<Body>(e, Body{20.0, 1.0});
        world.add<Health>(e, Health{100.0, 100.0, 0.0, 0.0});
        world.add<Faction>(e, Faction{team, false});
        return e;
    };
    const Entity mob = body(Team::Hostiles);
    world.add<MobTag>(mob);
    const Entity oracle = body(Team::Players);
    world.add<NpcTag>(oracle);
    const Entity summon = body(Team::Players);

    CHECK(!CombatSystem::canHit(world, oracle, mob, 1000.0));
    const DamageResult refused = combat.applyDamage(world, oracle, mob, 30.0, 1000.0);
    CHECK(refused.refused);
    CHECK_EQ(world.get<Health>(oracle).flashUntilMillis, 0.0);

    CHECK(CombatSystem::canHit(world, summon, mob, 1000.0));
    CHECK(!combat.applyDamage(world, summon, mob, 30.0, 1000.0).refused);
}

TEST(a_hostile_npc_takes_every_hit_and_a_friendly_one_refuses_them) {
    // The dummy stands on the hostiles' side, so a flower's ring hits it:
    // every hit flashes it and is numbered for the DPS readout, and its pool
    // stays full. The oracle, beside it, is on the players' side, and the same
    // ring passing through it lands nothing at all.
    const double dummyX = kTileSize * 8.0;
    const double oracleX = kTileSize * 14.0;
    const double y = kTileSize * 8.0;
    const std::string dir = npcWorld("npc-hits", fixtureNpc(dummyX, y, "target_dummy", "common") +
                                                    "," + fixtureNpc(oracleX, y, "oracle", "common"));
    Harness h("npc-hits", {}, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    // A registered account, so it wears the starter ring of five basics.
    NetClient client;
    CHECK(loginNew(h, client, "hitter", "password7"));
    client.joinGame(1920, 1080, {}, "hitter");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                      200));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity dummy = npcWearing(world, "target_dummy");
    const Entity oracle = npcWearing(world, "oracle");
    const Entity player = onlyPlayer(world);
    if (dummy == NULL_ENTITY || oracle == NULL_ENTITY || player == NULL_ENTITY) {
        CHECK(false);
        removeDataDir(dir);
        return;
    }
    CHECK(world.get<Faction>(dummy).team == Team::Hostiles);
    const std::uint32_t dummyId = world.get<NetId>(dummy).value;
    const std::uint32_t oracleId = world.get<NetId>(oracle).value;
    const double dummyMax = world.get<Health>(dummy).max;
    const double oracleMax = world.get<Health>(oracle).max;

    // Each NPC in turn sits on the ring's orbit, so the five petals sweep
    // straight through it.
    CHECK(awaitRing(h, {&client}, world, player));
    const auto standBeside = [&](Entity npc) {
        client.view().events().clear();
        for (int i = 0; i < kOneRingTurnTicks; ++i) {
            world.get<Transform>(player).position = ringOnly(world, npc, player);
            h.step(1, {&client});
        }
    };
    const auto numbered = [&](std::uint32_t netId) {
        int count = 0;
        for (const ViewEvent& event : client.view().events()) {
            if (event.kind == net::EventKind::Damage && event.netId == netId && event.amount > 0) {
                ++count;
            }
        }
        return count;
    };

    standBeside(dummy);
    CHECK(numbered(dummyId) > 3);
    CHECK(world.get<Health>(dummy).flashUntilMillis > 0.0);
    CHECK_NEAR(world.get<Health>(dummy).current, dummyMax, 1e-9);
    CHECK(!world.has<Dead>(dummy));

    standBeside(oracle);
    CHECK_EQ(numbered(oracleId), 0);
    CHECK_EQ(world.get<Health>(oracle).flashUntilMillis, 0.0);
    CHECK_NEAR(world.get<Health>(oracle).current, oracleMax, 1e-9);
    removeDataDir(dir);
}

TEST(a_hostile_npc_bites_the_flower_touching_it_and_breaks_the_petals_striking_it) {
    // The dummy hits back with its mob's body, as the TypeScript build's dummy
    // -- an ordinary mob -- always did: a flower pressed against it is bumped and
    // bitten, and a ring sweeping through it pays for every hit and breaks.
    // The oracle beside it, on the players' side, does neither.
    const double dummyX = kTileSize * 8.0;
    const double oracleX = kTileSize * 14.0;
    const double y = kTileSize * 8.0;
    const std::string dir = npcWorld("npc-bites", fixtureNpc(dummyX, y, "target_dummy", "common") +
                                                     "," + fixtureNpc(oracleX, y, "oracle", "common"));
    Harness h("npc-bites", {}, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(loginNew(h, client, "bitten", "password7"));
    client.joinGame(1920, 1080, {}, "bitten");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                      200));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity dummy = npcWearing(world, "target_dummy");
    const Entity oracle = npcWearing(world, "oracle");
    const Entity player = onlyPlayer(world);
    if (dummy == NULL_ENTITY || oracle == NULL_ENTITY || player == NULL_ENTITY) {
        CHECK(false);
        removeDataDir(dir);
        return;
    }
    CHECK(world.has<ContactDamage>(dummy));
    CHECK(!world.has<ContactDamage>(oracle));

    // The ring on the dummy: some petal breaks.
    CHECK(awaitRing(h, {&client}, world, player));
    bool broke = false;
    for (int i = 0; i < kOneRingTurnTicks && !broke; ++i) {
        world.get<Transform>(player).position = ringOnly(world, dummy, player);
        h.step(1, {&client});
        for (const LoadoutSlot& slot : world.get<Loadout>(player).slots) broke |= slot.broken;
    }
    CHECK(broke);

    // The flower against it -- flush, which is as close as it can ever get --
    // with its spawn shield dropped: bitten, and shoved off.
    const auto pressAgainst = [&](Entity npc) {
        // Copied out: the step below may relocate the flower's row.
        const double max = world.get<Health>(player).max;
        world.get<Health>(player).current = max;
        world.get<Health>(player).invulnerableUntilMillis = 0.0;
        world.get<Health>(player).hitCooldownUntilMillis = 0.0;
        const Vec2 at = world.get<Transform>(npc).position;
        const double reach = world.get<Body>(npc).radius + world.get<Body>(player).radius;
        world.get<Transform>(player).position = at - Vec2{reach - 3.0, 0.0};
        h.step(1, {&client});
        return max - world.get<Health>(player).current;
    };
    const double dummyBite = pressAgainst(dummy);
    CHECK(dummyBite > 0.0);
    // The bounce is velocity (gardn's), so the flower slides clear over the
    // next few ticks rather than being put there in the one it was bitten in.
    h.step(15, {&client});
    CHECK(distance(world.get<Transform>(player).position, world.get<Transform>(dummy).position) >
          world.get<Body>(dummy).radius + world.get<Body>(player).radius + 10.0);

    CHECK_NEAR(pressAgainst(oracle), 0.0, 1e-9);
    removeDataDir(dir);
}

TEST(a_dummy_counts_a_hit_as_yours_only_on_your_own_wire) {
    // Two flowers, one dummy, one of them hitting it. The hitter's snapshots
    // mark every number on the dummy as theirs; the watcher is sent the same
    // numbers, none of them marked -- so the watcher's DPS readout stays at
    // zero while the hitter's climbs.
    const double dummyX = kTileSize * 8.0;
    const double y = kTileSize * 8.0;
    const std::string dir =
        npcWorld("npc-dps-owner", fixtureNpc(dummyX, y, "target_dummy", "common"));
    Harness h("npc-dps-owner", {}, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient hitter, watcher;
    CHECK(loginNew(h, hitter, "hitter", "password7"));
    CHECK(loginNew(h, watcher, "watcher", "password7"));
    hitter.joinGame(1920, 1080, {}, "hitter");
    watcher.joinGame(1920, 1080, {}, "watcher");
    CHECK(h.stepUntil({&hitter, &watcher}, [&] {
        return hitter.status() == NetClient::Status::Playing &&
               watcher.status() == NetClient::Status::Playing;
    }, 200));
    h.step(3, {&hitter, &watcher});
    World& world = h.server.world();
    const Entity dummy = npcWearing(world, "target_dummy");
    Entity hitterBody = NULL_ENTITY;
    Entity watcherBody = NULL_ENTITY;
    Query<PlayerTag, PlayerAccount> bodies{world};
    bodies.each([&](Entity e, PlayerTag&, PlayerAccount& account) {
        if (account.username == "hitter") hitterBody = e;
        if (account.username == "watcher") watcherBody = e;
    });
    if (dummy == NULL_ENTITY || hitterBody == NULL_ENTITY || watcherBody == NULL_ENTITY) {
        CHECK(false);
        removeDataDir(dir);
        return;
    }
    const std::uint32_t dummyId = world.get<NetId>(dummy).value;

    CHECK(awaitRing(h, {&hitter, &watcher}, world, hitterBody));
    hitter.view().events().clear();
    watcher.view().events().clear();
    for (int i = 0; i < kOneRingTurnTicks; ++i) {
        world.get<Transform>(hitterBody).position = ringOnly(world, dummy, hitterBody);
        // Well clear of the dummy and of the hitter's ring, well inside view.
        world.get<Transform>(watcherBody).position = Vec2{dummyX + 700.0, y};
        h.step(1, {&hitter, &watcher});
    }
    const auto count = [&](NetClient& client, bool mine) {
        int n = 0;
        for (const ViewEvent& event : client.view().events()) {
            if (event.kind != net::EventKind::Damage || event.netId != dummyId) continue;
            if (((event.flag & net::DamageByViewer) != 0) == mine) ++n;
        }
        return n;
    };
    CHECK(count(hitter, true) > 3);
    CHECK_EQ(count(hitter, false), 0);
    CHECK(count(watcher, false) > 3);
    CHECK_EQ(count(watcher, true), 0);
    removeDataDir(dir);
}

TEST(a_flower_cannot_walk_through_an_npc) {
    // The dummy stands still, which makes it the clean case: a flower put down
    // on its centre, and one pressing into its side, both end the tick just
    // clear of its body -- and the dummy is where it was.
    const double dummyX = kTileSize * 8.0;
    const double y = kTileSize * 8.0;
    const std::string dir =
        npcWorld("npc-solid", fixtureNpc(dummyX, y, "target_dummy", "common"));
    Harness h("npc-solid", [](const std::string& path) { seedAccount(path, "seer"); }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity dummy = npcWearing(world, "target_dummy");
    const Entity player = onlyPlayer(world);
    if (dummy == NULL_ENTITY || player == NULL_ENTITY) {
        CHECK(false);
        removeDataDir(dir);
        return;
    }
    CHECK(!world.get<Npc>(dummy).cruises);
    const Vec2 home = world.get<Transform>(dummy).position;
    const double reach = world.get<Body>(dummy).radius + world.get<Body>(player).radius;

    for (const Vec2 start : {home, home + Vec2{-reach * 0.5, 3.0}, home + Vec2{0.0, reach - 2.0}}) {
        world.get<Transform>(player).position = start;
        h.step(1, {&client});
        const Vec2 after = world.get<Transform>(player).position;
        CHECK(distance(after, home) >= reach - 1e-6);
        // Out the near side, not through to the far one.
        if (start.x != home.x || start.y != home.y) {
            CHECK((after - home).x * (start - home).x + (after - home).y * (start - home).y > 0.0);
        }
    }
    CHECK_NEAR(world.get<Transform>(dummy).position.x, home.x, 1e-9);
    CHECK_NEAR(world.get<Transform>(dummy).position.y, home.y, 1e-9);
    removeDataDir(dir);
}

TEST(the_oracle_cruises_about_its_home_like_a_bee) {
    const std::string dir = oracleWorld("npc-cruise");
    Harness h("npc-cruise", [](const std::string& path) { seedAccount(path, "seer"); }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(2, {&client});
    World& world = h.server.world();
    const Entity oracle = npcWearing(world, "oracle");
    const Entity player = onlyPlayer(world);
    if (oracle == NULL_ENTITY || player == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }
    CHECK(world.get<Npc>(oracle).cruises);
    const Vec2 home = world.get<Npc>(oracle).home;

    // Nobody near: the flower is parked across the field, far out of its watch.
    const Vec2 away{kTileSize * 20.0, kTileSize * 20.0};
    double travelled = 0.0;
    double farthest = 0.0;
    Vec2 last = world.get<Transform>(oracle).position;
    for (int i = 0; i < 900; ++i) {
        world.get<Transform>(player).position = away;
        const Vec2 previous = world.get<Transform>(oracle).position;
        h.step(1, {&client});
        const Transform& at = world.get<Transform>(oracle);
        travelled += distance(at.position, last);
        last = at.position;
        farthest = std::max(farthest, distance(at.position, home));
        // A cruiser looks along the step it just took.
        if (distance(at.position, previous) > 1e-6) {
            CHECK_NEAR(angleDelta(at.angle, (at.position - previous).angle()), 0.0, 1e-9);
        }
    }
    // It really does fly -- thirty seconds of cruising covers ground -- and it
    // never strays much past its leash: the turn back is a curve, not a wall.
    CHECK(travelled > 300.0);
    CHECK(farthest < kNpcLeashRadius + 200.0);
    removeDataDir(dir);
}

TEST(an_npc_the_map_placed_comes_back_and_killall_leaves_it_alone) {
    const std::string dir = oracleWorld("npc-return");
    Harness h("npc-return", [](const std::string& path) { seedAccount(path, "boss", true); },
              dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "boss"));
    h.step(3, {&client});
    World& world = h.server.world();
    CHECK_EQ(npcsIn(world).size(), std::size_t(1));

    // Wild mobs are cleared; the NPC is not a wild mob.
    client.sendChat("/admin killall");
    h.step(4, {&client});
    CHECK_EQ(npcsIn(world).size(), std::size_t(1));

    // Clearing the NPCs is honoured -- and the map's own is back on the tick
    // after, because it is part of the map.
    const Entity before = npcsIn(world)[0];
    client.sendChat("/admin clear_npcs");
    CHECK(h.stepUntil({&client}, [&] {
        const std::vector<Entity> now = npcsIn(world);
        return now.size() == 1 && now[0] != before;
    }, 30));
    removeDataDir(dir);
}

TEST(spawn_makes_the_oracle_an_enemy_and_spawn_npc_puts_down_any_mob) {
    const std::string dir = oracleWorld("npc-admin");
    Harness h("npc-admin", [](const std::string& path) { seedAccount(path, "boss", true); },
              dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "boss"));
    h.step(3, {&client});
    World& world = h.server.world();
    const std::uint16_t oracle = content().mobIndex("oracle");

    // `spawn` is the ENEMY: an ordinary mob wearing the oracle's config, on
    // the hostile team, thinking like any other. Put down across the field,
    // well outside its aggro range: a rare one bites hard enough to put the
    // admin on the death card before the next command.
    client.sendChat("/admin spawn oracle rare 4000 4000");
    CHECK(h.stepUntil({&client}, [&] {
        bool found = false;
        Query<MobTag, MobType> mobs{world};
        mobs.each([&](Entity e, MobTag&, MobType& type) {
            if (type.configIndex != oracle) return;
            found = type.rarity == Rarity::Rare && world.get<Faction>(e).team == Team::Hostiles &&
                    world.has<MobAi>(e) && world.has<Health>(e) && !world.has<Npc>(e);
        });
        return found;
    }, 30));

    // `spawn_npc` is the FRIEND, where the admin stands.
    const std::size_t npcsBefore = npcsIn(world).size();
    client.sendChat("/admin spawn_npc oracle legendary");
    CHECK(h.stepUntil({&client}, [&] { return npcsIn(world).size() == npcsBefore + 1; }, 30));
    bool placed = false;
    for (const Entity e : npcsIn(world)) {
        if (world.get<Npc>(e).rarity == Rarity::Legendary) placed = true;
    }
    CHECK(placed);

    // The target dummy is an NPC too, on its own side.
    client.sendChat("/admin spawn_npc target_dummy");
    CHECK(h.stepUntil({&client}, [&] { return npcsIn(world).size() == npcsBefore + 2; }, 30));
    const Entity dummy = npcWearing(world, "target_dummy");
    CHECK(dummy != NULL_ENTITY && world.get<Faction>(dummy).team == Team::Hostiles);

    // ANY mob can be one. A bee has no `npc` block, so it stands as an empty
    // block would have it: the players' side, offering nothing, touching
    // nobody.
    const std::uint16_t bee = content().mobIndex("bee");
    const auto beeNpc = [&](Team team) {
        Entity found = NULL_ENTITY;
        for (const Entity e : npcsIn(world)) {
            if (world.get<Npc>(e).configIndex == bee && world.get<Faction>(e).team == team) found = e;
        }
        return found;
    };
    client.sendChat("/admin spawn_npc bee");
    CHECK(h.stepUntil({&client}, [&] { return npcsIn(world).size() == npcsBefore + 3; }, 30));
    const Entity friendlyBee = beeNpc(Team::Players);
    CHECK(friendlyBee != NULL_ENTITY);
    if (friendlyBee != NULL_ENTITY) {
        CHECK(world.get<Npc>(friendlyBee).service == NpcService::None);
        CHECK(!world.has<ContactDamage>(friendlyBee));
    }

    // A side typed after it -- before or after the tier -- overrides that,
    // and a hostile one bites with its mob's body.
    client.sendChat("/admin spawn_npc bee hostile rare");
    CHECK(h.stepUntil({&client}, [&] { return npcsIn(world).size() == npcsBefore + 4; }, 30));
    const Entity hostileBee = beeNpc(Team::Hostiles);
    CHECK(hostileBee != NULL_ENTITY);
    if (hostileBee != NULL_ENTITY) {
        CHECK(world.get<Npc>(hostileBee).rarity == Rarity::Rare);
        CHECK(world.has<ContactDamage>(hostileBee));
        CHECK(!world.has<MobTag>(hostileBee));
    }

    // A word that is neither a tier nor a side places nothing.
    client.sendChat("/admin spawn_npc bee sideways");
    h.step(10, {&client});
    CHECK_EQ(npcsIn(world).size(), npcsBefore + 4);
    removeDataDir(dir);
}

// ---------------------------------------------------------------------------
// The service
// ---------------------------------------------------------------------------

TEST(an_oracle_craft_is_certain_and_costs_exactly_its_price) {
    const std::string dir = oracleWorld("oracle-craft");
    Harness h("oracle-craft", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Common, 20);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    if (rose == kInvalidIndex) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Common) == 20u;
    }));
    CHECK_EQ(client.oracleCooldownRemainingMillis(), 0.0);

    // One upgrade for seven: seven in, one out, no roll anywhere.
    OracleOutcome outcome;
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.crafted, 1);
    CHECK_EQ(outcome.spent, 7u);
    CHECK(outcome.rarity == Rarity::Uncommon);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Common) == 13u &&
               p.stackCount(rose, Rarity::Uncommon) == 1u;
    }));
    removeDataDir(dir);
}

TEST(the_oracle_refuses_a_short_stack_and_apex_and_neither_starts_the_wait) {
    const std::string dir = oracleWorld("oracle-refuse");
    Harness h("oracle-refuse", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Common, 6);
        seedStack(path, "seer", "petal_rose", Rarity::Uncommon, 11);
        seedStack(path, "seer", "petal_rose", Rarity::Apex, 3000);
        seedStack(path, "seer", "petal_rose", Rarity::Universal, 3000);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});

    OracleOutcome outcome;
    // Six is not seven: all or nothing, never a part. Standing at the oracle
    // each time, so every refusal is for its own reason and not for reach.
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.spent, 0u);
    // Nothing crafts out of apex, however many are offered.
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Apex);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);
    // Nor out of universal, which is only ever given.
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Universal);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);

    h.step(5, {&client});
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Common), 6u);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Apex), 3000u);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Universal), 3000u);
    CHECK_EQ(client.oracleCooldownRemainingMillis(), 0.0);

    // A refusal is not a craft: the wait has not started, and a craft the
    // stack CAN pay for still goes through.
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Uncommon);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);
    removeDataDir(dir);
}

TEST(the_oracle_serves_only_a_flower_standing_at_it) {
    const std::string dir = oracleWorld("oracle-far");
    Harness h("oracle-far", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Common, 20);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    NetClient client;
    CHECK(connectClient(h, client));
    client.requestLogin("seer", "password7");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::LoggedIn; }));

    // On the title screen there is no body to be standing anywhere.
    OracleOutcome outcome;
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);

    client.joinGame(1920, 1080, {}, "seer");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                      200));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity player = onlyPlayer(world);
    if (player == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }
    const double radius =
        content().mobStats(content().mobIndex("oracle"), Rarity::Epic).radius;

    // Just outside what the server allows -- the reach and its slack, from the
    // oracle's skin, wherever it has cruised to -- is refused...
    const Entity oracle = npcWearing(world, "oracle");
    if (oracle == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }
    Vec2 at = world.get<Transform>(oracle).position;
    world.get<Transform>(player).position =
        Vec2{at.x + radius + kNpcServiceReach + kNpcServiceSlack + 40.0, at.y};
    h.step(1, {&client});
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);
    CHECK(outcome.reason.find("far") != std::string::npos);

    // ...and just inside it is served, slack included: the client measured a
    // drawn position a snapshot old, and that must not cost the player.
    at = world.get<Transform>(oracle).position;
    world.get<Transform>(player).position =
        Vec2{at.x + radius + kNpcServiceReach + kNpcServiceSlack - 40.0, at.y};
    h.step(1, {&client});
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Common) == 13u &&
               p.stackCount(rose, Rarity::Uncommon) == 1u;
    }));
    removeDataDir(dir);
}

TEST(a_super_bought_from_the_oracle_is_announced) {
    const std::string dir = oracleWorld("oracle-announce");
    Harness h("oracle-announce", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Ultra, 253);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    const std::size_t chatBefore = client.chat().size();

    OracleOutcome outcome;
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Ultra);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);
    CHECK(outcome.rarity == Rarity::Super);
    CHECK(h.stepUntil({&client}, [&] {
        for (std::size_t i = chatBefore; i < client.chat().size(); ++i) {
            if (client.chat()[i].text.find("has been crafted by") != std::string::npos) return true;
        }
        return false;
    }, 60));
    removeDataDir(dir);
}

// ---------------------------------------------------------------------------
// The wait between crafts
// ---------------------------------------------------------------------------

TEST(the_wait_is_said_in_whole_minutes_rounded_up) {
    // The reference's own sentence. Rounded up, so a wait of seconds reads as
    // a minute and never as zero.
    CHECK_EQ(oracleCooldownText(27.0 * 60000.0),
             std::string("You'll be able to craft again in 27 minutes"));
    CHECK_EQ(oracleCooldownText(26.2 * 60000.0),
             std::string("You'll be able to craft again in 27 minutes"));
    CHECK_EQ(oracleCooldownText(kOracleCooldownMillis),
             std::string("You'll be able to craft again in 30 minutes"));
    CHECK_EQ(oracleCooldownText(60000.0), std::string("You'll be able to craft again in 1 minute"));
    CHECK_EQ(oracleCooldownText(1.0), std::string("You'll be able to craft again in 1 minute"));
}

TEST(the_oracle_grants_one_craft_every_thirty_minutes) {
    const std::string dir = oracleWorld("oracle-wait");
    Harness h("oracle-wait", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Common, 20);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});

    OracleOutcome outcome;
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);

    // The craft started the half hour, and the profile that followed it says
    // so -- which is what turns the panel's line red.
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Uncommon) == 1u;
    }));
    const double wait = client.oracleCooldownRemainingMillis();
    CHECK(wait > kOracleCooldownMillis - 60000.0);
    CHECK(wait <= kOracleCooldownMillis);

    // The next one is refused in the panel's own words, and costs nothing.
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.reason, std::string("You'll be able to craft again in 30 minutes"));
    h.step(5, {&client});
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Common), 13u);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Uncommon), 1u);

    removeDataDir(dir);
}

TEST(the_oracle_wait_is_the_accounts_across_a_relog_and_ends_on_the_server_clock) {
    const std::string dir = oracleWorld("oracle-wait-kept");
    Harness h("oracle-wait-kept", [](const std::string& path) {
        seedAccount(path, "seer");
        seedStack(path, "seer", "petal_rose", Rarity::Common, 30);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    {
        NetClient client;
        CHECK(joinAs(h, client, "seer"));
        h.step(3, {&client});
        OracleOutcome outcome;
        standBeside(h, client, "oracle");
        client.requestOracleCraft(rose, Rarity::Common);
        CHECK(awaitOracle(h, client, outcome));
        CHECK(outcome.success);
        client.disconnect();
        h.step(10, {});
    }

    // A new connection is the same account, so the wait came with it: the
    // join's profile already carries it, and the craft is refused.
    NetClient client;
    CHECK(joinAs(h, client, "seer"));
    h.step(3, {&client});
    CHECK(client.oracleCooldownRemainingMillis() > kOracleCooldownMillis - 60000.0);
    OracleOutcome outcome;
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);

    // The wait is measured on the server's own tick clock, so moving that
    // clock is the whole of "half an hour later". A minute and a half short,
    // what is left rounds up to two...
    h.clock += kOracleCooldownMillis - 90000.0;
    h.step(3, {&client});
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.reason, std::string("You'll be able to craft again in 2 minutes"));

    // ...and past it, the oracle serves again.
    h.clock += 120000.0;
    h.step(3, {&client});
    standBeside(h, client, "oracle");
    client.requestOracleCraft(rose, Rarity::Common);
    CHECK(awaitOracle(h, client, outcome));
    CHECK(outcome.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Uncommon) == 2u;
    }));
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Common), 16u);
    removeDataDir(dir);
}

// ---------------------------------------------------------------------------
// The trader
// ---------------------------------------------------------------------------

namespace {

std::string traderWorld(const std::string& name) {
    return npcWorld(name, fixtureNpc(kOracleX, kOracleY, "trader", "common"));
}

/// Waits for the trader's answer and hands it back with `pending` cleared, as
/// the panel reads it.
bool awaitTrade(Harness& h, NetClient& client, TradeOutcome& out) {
    if (!h.stepUntil({&client}, [&] { return client.tradeOutcome().pending; }, 200)) return false;
    out = client.tradeOutcome();
    client.tradeOutcome().pending = false;
    return true;
}

constexpr double kHourMillis = 60.0 * 60.0 * 1000.0;

} // namespace

TEST(the_trader_wait_is_said_in_whole_hours_rounded_down_then_in_minutes) {
    // The reference says 23 the moment a trade is made: whole hours, down.
    CHECK_EQ(traderCooldownText(kTraderCooldownMillis - 1000.0),
             std::string("You'll be able to trade again in 23 hours"));
    CHECK_EQ(traderCooldownText(kTraderCooldownMillis),
             std::string("You'll be able to trade again in 24 hours"));
    CHECK_EQ(traderCooldownText(1.5 * kHourMillis),
             std::string("You'll be able to trade again in 1 hour"));
    // Under an hour there is no whole hour to say: minutes, rounded up, and
    // never zero.
    CHECK_EQ(traderCooldownText(30.0 * 60000.0),
             std::string("You'll be able to trade again in 30 minutes"));
    CHECK_EQ(traderCooldownText(29.2 * 60000.0),
             std::string("You'll be able to trade again in 30 minutes"));
    CHECK_EQ(traderCooldownText(1.0), std::string("You'll be able to trade again in 1 minute"));
}

TEST(the_shipped_trader_stands_in_the_desert_and_takes_all_but_the_marked_petals) {
    CHECK(ensureShippedContent());
    const std::uint16_t trader = content().mobIndex("trader");
    CHECK(trader != kInvalidIndex);
    if (trader == kInvalidIndex) return;
    const MobConfig& config = content().mob(trader);
    CHECK(config.npc.present);
    CHECK(config.npc.service == NpcService::Trader);
    CHECK(config.npc.team == Team::Players);
    // Drawn by code: its eyes are a flower's, and they move.
    CHECK_EQ(config.image, std::string("$trader"));
    CHECK(config.noEggDrop);
    CHECK(!content().mobStats(trader, Rarity::Common).ambient);
    // It wanders about its point on the oracle's leashed cruise rather than
    // standing on it.
    CHECK(config.beeFlight);
    CHECK(content().mobStats(trader, Rarity::Common).speed > 0.0);

    // What it hands back exists and is marked untradable -- or it would take
    // its own coins back one for one -- and so is the basic petal. A petal
    // petals.json says nothing about trades.
    const std::uint16_t coin = content().petalIndex(kTraderCoinPetal);
    CHECK(coin != kInvalidIndex);
    if (coin != kInvalidIndex) CHECK(!content().petal(coin).tradable);
    CHECK(!content().petal(content().petalIndex("basic")).tradable);
    CHECK(content().petal(content().petalIndex("rose")).tradable);

    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    Realm desert = Realm::Overworld;
    bool haveDesert = false;
    for (const MapData& map : maps.maps()) {
        if (map.id() != "desert") continue;
        desert = map.realm();
        haveDesert = true;
    }
    CHECK(haveDesert);
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK(warnings.empty());
    int traders = 0;
    int prestigeTraders = 0;
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (content().mob(site.mobIndex).npc.service != NpcService::Trader) continue;
        // The prestige desert is the same map, trader and all.
        if (maps.isPrestige(site.realm)) {
            ++prestigeTraders;
            continue;
        }
        ++traders;
        CHECK(site.realm == desert);
        CHECK(site.rarity == Rarity::Common);
        // On open ground, body and all: nothing pushes it off its point, and
        // a flower can walk right round it.
        const double radius = content().mobStats(site.mobIndex, site.rarity).radius;
        const Vec2 placed = terrain.resolveCircle(site.position, radius, site.realm);
        CHECK_NEAR(placed.x, site.position.x, 1e-6);
        CHECK_NEAR(placed.y, site.position.y, 1e-6);
    }
    CHECK_EQ(traders, 1);
    CHECK_EQ(prestigeTraders, 1);
}

TEST(a_trade_is_one_petal_for_one_coin_of_the_same_tier) {
    const std::string dir = traderWorld("trader-trade");
    Harness h("trader-trade", [](const std::string& path) {
        seedAccount(path, "merchant");
        seedStack(path, "merchant", "petal_rose", Rarity::Epic, 3);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    const std::uint16_t coin = content().petalIndex(kTraderCoinPetal);
    if (rose == kInvalidIndex || coin == kInvalidIndex) { CHECK(false); removeDataDir(dir); return; }

    NetClient client;
    CHECK(joinAs(h, client, "merchant"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Epic) == 3u;
    }));
    CHECK_EQ(client.traderCooldownRemainingMillis(), 0.0);

    TradeOutcome outcome;
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Epic);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.petalIndex, rose);
    CHECK(outcome.rarity == Rarity::Epic);
    CHECK_EQ(outcome.receivedIndex, coin);
    // One out of the stack, one coin of its tier in -- and the day's wait on
    // the profile that followed, which the oracle's is not part of.
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Epic) == 2u && p.stackCount(coin, Rarity::Epic) == 1u;
    }));
    CHECK_EQ(client.profile().stackCount(coin, Rarity::Common), 0u);
    const double wait = client.traderCooldownRemainingMillis();
    CHECK(wait > kTraderCooldownMillis - 60000.0);
    CHECK(wait <= kTraderCooldownMillis);
    CHECK_EQ(client.oracleCooldownRemainingMillis(), 0.0);
    removeDataDir(dir);
}

TEST(the_trader_refuses_what_it_does_not_take_and_a_refusal_starts_no_wait) {
    const std::string dir = traderWorld("trader-refuse");
    Harness h("trader-refuse", [](const std::string& path) {
        seedAccount(path, "merchant");
        seedStack(path, "merchant", "petal_basic", Rarity::Common, 3);
        seedStack(path, "merchant", "petal_coin", Rarity::Rare, 2);
        seedStack(path, "merchant", "petal_rose", Rarity::Apex, 1);
        seedStack(path, "merchant", "petal_rose", Rarity::Universal, 1);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    const std::uint16_t coin = content().petalIndex(kTraderCoinPetal);
    const std::uint16_t basic = content().petalIndex("basic");

    NetClient client;
    CHECK(joinAs(h, client, "merchant"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(coin, Rarity::Rare) == 2u && p.stackCount(basic, Rarity::Common) > 0u;
    }));
    const std::uint32_t basics = client.profile().stackCount(basic, Rarity::Common);

    // Marked untradable, both of them: the basic petal, and a coin -- which
    // would otherwise trade for itself.
    TradeOutcome outcome;
    standBeside(h, client, "trader");
    client.requestTrade(basic, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.receivedIndex, kNoPetal);
    standBeside(h, client, "trader");
    client.requestTrade(coin, Rarity::Rare);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    // A petal the account does not have, at a tier it does not have.
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Epic);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    // A tradable petal at universal: a universal coin would be a universal
    // nobody gave.
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Universal);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.receivedIndex, kNoPetal);

    h.step(5, {&client});
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Universal), 1u);
    CHECK_EQ(client.profile().stackCount(coin, Rarity::Universal), 0u);
    CHECK_EQ(client.profile().stackCount(basic, Rarity::Common), basics);
    CHECK_EQ(client.profile().stackCount(coin, Rarity::Rare), 2u);
    CHECK_EQ(client.traderCooldownRemainingMillis(), 0.0);

    // None of that was a trade, so the day has not started -- and the top
    // tier trades like any other.
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Apex);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(outcome.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Apex) == 0u && p.stackCount(coin, Rarity::Apex) == 1u;
    }));
    removeDataDir(dir);
}

TEST(the_trader_serves_only_a_flower_standing_at_it) {
    const std::string dir = traderWorld("trader-far");
    Harness h("trader-far", [](const std::string& path) {
        seedAccount(path, "merchant");
        seedStack(path, "merchant", "petal_rose", Rarity::Common, 5);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");

    NetClient client;
    CHECK(connectClient(h, client));
    client.requestLogin("merchant", "password7");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::LoggedIn; }));

    // On the title screen there is no body to be standing anywhere.
    TradeOutcome outcome;
    client.requestTrade(rose, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);

    client.joinGame(1920, 1080, {}, "merchant");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; },
                      200));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity player = onlyPlayer(world);
    const Entity trader = npcWearing(world, "trader");
    if (player == NULL_ENTITY || trader == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }
    const double radius = world.get<Body>(trader).radius;

    // Just past the reach and its slack, from the trader's skin, wherever it
    // has cruised to: refused...
    Vec2 at = world.get<Transform>(trader).position;
    world.get<Transform>(player).position =
        Vec2{at.x + radius + kNpcServiceReach + kNpcServiceSlack + 40.0, at.y};
    h.step(1, {&client});
    client.requestTrade(rose, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    CHECK(outcome.reason.find("far") != std::string::npos);

    // ...and just inside, served. It has flown on while the refusal came
    // back, so "at the trader" is looked up again.
    at = world.get<Transform>(trader).position;
    world.get<Transform>(player).position =
        Vec2{at.x + radius + kNpcServiceReach + kNpcServiceSlack - 40.0, at.y};
    h.step(1, {&client});
    client.requestTrade(rose, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(outcome.success);
    removeDataDir(dir);
}

TEST(the_trader_grants_one_trade_a_day_kept_across_a_relog) {
    const std::string dir = traderWorld("trader-wait");
    Harness h("trader-wait", [](const std::string& path) {
        seedAccount(path, "merchant");
        seedStack(path, "merchant", "petal_rose", Rarity::Common, 5);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t rose = content().petalIndex("rose");
    const std::uint16_t coin = content().petalIndex(kTraderCoinPetal);

    {
        NetClient client;
        CHECK(joinAs(h, client, "merchant"));
        h.step(3, {&client});
        TradeOutcome outcome;
        standBeside(h, client, "trader");
        client.requestTrade(rose, Rarity::Common);
        CHECK(awaitTrade(h, client, outcome));
        CHECK(outcome.success);

        // The next is refused in the panel's own words, and costs nothing.
        standBeside(h, client, "trader");
        client.requestTrade(rose, Rarity::Common);
        CHECK(awaitTrade(h, client, outcome));
        CHECK(!outcome.success);
        CHECK_EQ(outcome.reason, std::string("You'll be able to trade again in 23 hours"));
        client.disconnect();
        h.step(10, {});
    }

    // A new connection is the same account, and the wait came with it.
    NetClient client;
    CHECK(joinAs(h, client, "merchant"));
    h.step(3, {&client});
    CHECK(client.traderCooldownRemainingMillis() > kTraderCooldownMillis - 60000.0);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Common), 4u);
    CHECK_EQ(client.profile().stackCount(coin, Rarity::Common), 1u);

    // Measured on the server's tick clock: half an hour short of the day...
    TradeOutcome outcome;
    h.clock += kTraderCooldownMillis - 30.0 * 60000.0;
    h.step(3, {&client});
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.reason, std::string("You'll be able to trade again in 30 minutes"));

    // ...and past it, the trader deals again.
    h.clock += 31.0 * 60000.0;
    h.step(3, {&client});
    standBeside(h, client, "trader");
    client.requestTrade(rose, Rarity::Common);
    CHECK(awaitTrade(h, client, outcome));
    CHECK(outcome.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(rose, Rarity::Common) == 3u && p.stackCount(coin, Rarity::Common) == 2u;
    }));
    removeDataDir(dir);
}

// ---------------------------------------------------------------------------
// On screen
// ---------------------------------------------------------------------------

namespace {

constexpr int kFrame = 200;

/// How many pixels of the oracle's pupil colour sit left and right of `cx`.
struct PupilSides {
    int left = 0;
    int right = 0;
    int outsideSocket = 0;
};

PupilSides pupilSides(const std::vector<std::uint8_t>& pixels, double cx, double cy,
                      double socketRadius) {
    PupilSides out;
    for (int y = 0; y < kFrame; ++y) {
        for (int x = 0; x < kFrame; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
            if (pixels[i] != 0xEE || pixels[i + 1] != 0xEE || pixels[i + 2] != 0xEE) continue;
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            if (dx < 0) ++out.left;
            else ++out.right;
            if (dx * dx + dy * dy > socketRadius * socketRadius) ++out.outsideSocket;
        }
    }
    return out;
}

const SpriteCache& shippedSprites() {
    static const SpriteCache sprites = [] {
        ensureShippedContent();
        SpriteCache cache;
        cache.build(content(), dataDir());
        return cache;
    }();
    return sprites;
}

} // namespace

TEST(the_oracle_painter_puts_its_pupil_where_it_looks_and_keeps_it_in_the_socket) {
    CHECK(ensureShippedContent());
    const std::uint16_t oracle = content().mobIndex("oracle");
    const SpriteCache& sprites = shippedSprites();
    CHECK(sprites.mobArt(oracle) == MobArt::Oracle);
    CHECK(sprites.mobDrawable(oracle));

    // A body 100 across in a 200 frame: the socket is 15/28 of the radius.
    const double diameter = 100.0;
    const double socket = 15.0 / 28.0 * diameter * 0.5;
    const auto frame = [&](Vec2 gaze) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        sprites.drawMob(canvas, oracle, Rarity::Common, kFrame * 0.5, kFrame * 0.5, diameter, 0.0,
                        0.0, false, 0.0, gaze);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };

    const PupilSides east = pupilSides(frame({1.0, 0.0}), kFrame * 0.5, kFrame * 0.5, socket);
    const PupilSides west = pupilSides(frame({-1.0, 0.0}), kFrame * 0.5, kFrame * 0.5, socket);
    CHECK(east.right > east.left * 2);
    CHECK(west.left > west.right * 2);
    // A gaze longer than one is clamped: the pupil runs to the rim and stops.
    const PupilSides wild = pupilSides(frame({40.0, 0.0}), kFrame * 0.5, kFrame * 0.5, socket);
    CHECK(wild.right > 0);
    CHECK_EQ(wild.outsideSocket, 0);
    CHECK_EQ(east.outsideSocket, 0);
}

TEST(an_npc_is_drawn_with_its_eye_on_its_facing_and_an_invulnerable_bar) {
    CHECK(ensureShippedContent());
    const std::uint16_t oracle = content().mobIndex("oracle");
    const Vec2 at{1000.0, 1000.0};
    const auto frame = [&](net::EntityKind kind, double angle) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        WorldView view;
        view.setRealm(Realm::Overworld);
        RemoteEntity npc;
        npc.netId = 42;
        npc.kind = kind;
        npc.typeIndex = oracle;
        npc.rarity = Rarity::Common;
        npc.position = npc.targetPosition = at;
        npc.angle = npc.targetAngle = angle;
        npc.needsSnap = false;
        npc.radius = content().mobStats(oracle, Rarity::Common).radius;
        npc.healthFraction = 0.5;
        view.seedForTest(npc);
        WorldRenderer renderer;
        renderer.setContent(&content());
        renderer.setSprites(&shippedSprites());
        Camera camera;
        camera.setViewport(kFrame, kFrame);
        camera.userZoom = 1.0;
        camera.snapTo(at);
        renderer.draw(canvas, view, camera, at, 0.0);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };
    const auto healthPixels = [](const std::vector<std::uint8_t>& pixels) {
        const std::uint8_t r = (ui::kHealth >> 16) & 0xFF;
        const std::uint8_t g = (ui::kHealth >> 8) & 0xFF;
        const std::uint8_t b = ui::kHealth & 0xFF;
        int found = 0;
        for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
            if (pixels[i] == r && pixels[i + 1] == g && pixels[i + 2] == b) ++found;
        }
        return found;
    };

    // The body is upright; the facing is where the EYE goes.
    const double socket = 15.0 / 28.0 * content().mobStats(oracle, Rarity::Common).radius;
    const PupilSides lookingWest =
        pupilSides(frame(net::EntityKind::Npc, kPi), kFrame * 0.5, kFrame * 0.5, socket + 1.0);
    const PupilSides lookingEast =
        pupilSides(frame(net::EntityKind::Npc, 0.0), kFrame * 0.5, kFrame * 0.5, socket + 1.0);
    CHECK(lookingWest.left > lookingWest.right * 2);
    CHECK(lookingEast.right > lookingEast.left * 2);

    // The mob's plate, with the bar in its invulnerable state: no green, and
    // the spawn-shield yellow a flower's bar turns, the whole width of the bar
    // even though the wire says half -- where the same creature as a mob wears
    // half a green bar.
    const auto colourPixels = [](const std::vector<std::uint8_t>& pixels, std::uint32_t colour) {
        const std::uint8_t r = (colour >> 16) & 0xFF;
        const std::uint8_t g = (colour >> 8) & 0xFF;
        const std::uint8_t b = colour & 0xFF;
        int found = 0;
        for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
            if (pixels[i] == r && pixels[i + 1] == g && pixels[i + 2] == b) ++found;
        }
        return found;
    };
    constexpr std::uint32_t kInvulnerable = 0xFAFFC9u;
    const std::vector<std::uint8_t> npcFrame = frame(net::EntityKind::Npc, 0.0);
    const std::vector<std::uint8_t> mobFrame = frame(net::EntityKind::Mob, 0.0);
    CHECK_EQ(healthPixels(npcFrame), 0);
    CHECK(colourPixels(npcFrame, kInvulnerable) > 0);
    CHECK(healthPixels(mobFrame) > 0);
    CHECK_EQ(colourPixels(mobFrame, kInvulnerable), 0);
    // Full width: yellow on both sides of the body's centre line.
    int left = 0;
    int right = 0;
    for (int y = 0; y < kFrame; ++y) {
        for (int x = 0; x < kFrame; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
            if (npcFrame[i] != 0xFA || npcFrame[i + 1] != 0xFF || npcFrame[i + 2] != 0xC9) continue;
            if (x < kFrame / 2 - 10) ++left;
            if (x > kFrame / 2 + 10) ++right;
        }
    }
    CHECK(left > 0);
    CHECK(right > 0);
}

namespace {

/// White pixels inside a flower's two eye sockets -- the ellipses at (+-7,
/// -4.8), 3.2 by 6.5, in its radius-25 art space -- split by which side of the
/// sockets' middles they sit on: across when `vertical` is false, down when
/// it is true. `unit` is pixels to the art unit, about the frame's centre.
struct EyeSplit {
    int before = 0;
    int after = 0;
};

EyeSplit eyeSplit(const std::vector<std::uint8_t>& pixels, double unit, bool vertical) {
    EyeSplit out;
    const double c = kFrame * 0.5;
    for (int y = 0; y < kFrame; ++y) {
        for (int x = 0; x < kFrame; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
            if (pixels[i] != 0xFF || pixels[i + 1] != 0xFF || pixels[i + 2] != 0xFF) continue;
            const double ax = (x + 0.5 - c) / unit;
            const double ay = (y + 0.5 - c) / unit;
            for (const double eye : {-7.0, 7.0}) {
                const double dx = ax - eye;
                const double dy = ay + 4.8;
                if (dx * dx / (3.2 * 3.2) + dy * dy / (6.5 * 6.5) > 1.0) continue;
                const double along = vertical ? dy : dx;
                if (along < 0.0) ++out.before;
                else ++out.after;
            }
        }
    }
    return out;
}

} // namespace

TEST(the_trader_painter_borders_only_the_rings_outer_edge_and_moves_its_eyes_like_a_flower) {
    CHECK(ensureShippedContent());
    const std::uint16_t trader = content().mobIndex("trader");
    const SpriteCache& sprites = shippedSprites();
    CHECK(sprites.mobArt(trader) == MobArt::Trader);
    CHECK(sprites.mobDrawable(trader));

    // 180 across in a 200 frame. The body's radius is the ring's outer edge,
    // 33.75 art units -- the flower in the middle is 25 of them.
    const double diameter = 180.0;
    const double unit = diameter * 0.5 / 33.75;
    const double c = kFrame * 0.5;
    const auto frame = [&](Vec2 gaze) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        sprites.drawMob(canvas, trader, Rarity::Common, c, c, diameter, 0.0, 0.0, false, 0.0, gaze);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };
    const auto rgbAt = [&](const std::vector<std::uint8_t>& pixels, double angle, double art) {
        const int x = static_cast<int>(c + std::cos(angle) * art * unit);
        const int y = static_cast<int>(c + std::sin(angle) * art * unit);
        const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
        return (static_cast<std::uint32_t>(pixels[i]) << 16) |
               (static_cast<std::uint32_t>(pixels[i + 1]) << 8) | pixels[i + 2];
    };

    const std::vector<std::uint8_t> picture = frame({1.0, 0.0});
    for (int i = 0; i < 12; ++i) {
        const double spoke = kTau * i / 12;
        // Halfway between two spokes, just outside the flower's ring, is where
        // two petals overlap -- and where the capture drew both their outlines
        // across the white. It is white.
        CHECK_EQ(rgbAt(picture, spoke + kPi / 12, 27.6), 0xFFFFFFu);
        // The ring's outer edge wears the basic petal's grey, on every petal.
        CHECK_EQ(rgbAt(picture, spoke, 34.0), 0xCFCFCFu);
        // And the flower's own ring is a player's: the flower yellow at 0.8.
        CHECK_EQ(rgbAt(picture, spoke + kPi / 12, 25.0), 0xCCB94Fu);
    }

    // The eyes travel as a flower's do: two units across, 4.4 down.
    const EyeSplit east = eyeSplit(frame({1.0, 0.0}), unit, false);
    const EyeSplit west = eyeSplit(frame({-1.0, 0.0}), unit, false);
    const EyeSplit down = eyeSplit(frame({0.0, 1.0}), unit, true);
    const EyeSplit up = eyeSplit(frame({0.0, -1.0}), unit, true);
    CHECK(east.after > east.before * 2);
    CHECK(west.before > west.after * 2);
    CHECK(down.after > down.before * 2);
    CHECK(up.before > up.after * 2);
}

TEST(the_trader_is_drawn_upright_with_its_eyes_on_its_facing) {
    CHECK(ensureShippedContent());
    const std::uint16_t trader = content().mobIndex("trader");
    const Vec2 at{1000.0, 1000.0};
    // Three screen pixels to the world unit, so the eyes are big enough to
    // count pixels in.
    constexpr double kZoom = 3.0;
    const double radius = content().mobStats(trader, Rarity::Common).radius;
    const double unit = radius / 33.75 * kZoom;
    const auto frame = [&](double angle) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        WorldView view;
        view.setRealm(Realm::Overworld);
        RemoteEntity npc;
        npc.netId = 43;
        npc.kind = net::EntityKind::Npc;
        npc.typeIndex = trader;
        npc.rarity = Rarity::Common;
        npc.position = npc.targetPosition = at;
        npc.angle = npc.targetAngle = angle;
        npc.needsSnap = false;
        npc.radius = radius;
        npc.healthFraction = 1.0;
        view.seedForTest(npc);
        WorldRenderer renderer;
        renderer.setContent(&content());
        renderer.setSprites(&shippedSprites());
        Camera camera;
        camera.setViewport(kFrame, kFrame);
        camera.userZoom = kZoom;
        camera.snapTo(at);
        renderer.draw(canvas, view, camera, at, 0.0);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };
    // Facing west, the pupils sit west in their sockets; facing east, east.
    // Were the body turned instead, the eyes would swing round the middle
    // and neither would hold.
    const EyeSplit west = eyeSplit(frame(kPi), unit, false);
    const EyeSplit east = eyeSplit(frame(0.0), unit, false);
    CHECK(west.before > west.after * 2);
    CHECK(east.after > east.before * 2);
}

// ---------------------------------------------------------------------------
// The titan: the universal forge, wearing a ring of its own
// ---------------------------------------------------------------------------

namespace {

/// Where the fixture's titan stands: the middle of the field, well clear of
/// the door and every wall -- it is a thousand units across, and its ring
/// orbits forty past its edge.
constexpr double kTitanX = kTileSize * 12.0;
constexpr double kTitanY = kTileSize * 12.0;

std::string titanWorld(const std::string& name) {
    return npcWorld(name, fixtureNpc(kTitanX, kTitanY, "titan", "universal"));
}

/// Waits for the titan's answer and hands it back with `pending` cleared, as
/// the panel reads it.
bool awaitForge(Harness& h, const std::vector<NetClient*>& clients, NetClient& client,
                TitanForgeOutcome& out) {
    if (!h.stepUntil(clients, [&] { return client.titanForgeOutcome().pending; }, 200)) {
        return false;
    }
    out = client.titanForgeOutcome();
    client.titanForgeOutcome().pending = false;
    return true;
}

/// Wears `petal` in loadout slot `slot` of `username`'s record.
void seedWorn(const std::string& path, const std::string& username, std::size_t slot,
              const char* petal, Rarity rarity) {
    Database db;
    std::string error;
    db.load(path, error);
    const Account* account = db.findUser(username);
    if (account == nullptr) return;
    PlayerRecord& record = db.progress(account->id);
    if (record.loadout.size() < kLoadoutSlots) record.loadout.resize(kLoadoutSlots);
    StoredItem item;
    item.petalType = petal;
    item.rarity = rarity;
    record.loadout[slot] = item;
    db.markDirty();
    db.save();
}

/// Stands `username`'s flower just off the titan's east edge -- inside its
/// ring's band, where the petals sweep through it.
void standAtTitan(Harness& h, const std::vector<NetClient*>& clients, const std::string& username) {
    World& world = h.server.world();
    const Entity titan = npcWearing(world, "titan");
    const Entity body = bodyNamed(world, username);
    if (titan == NULL_ENTITY || body == NULL_ENTITY) return;
    world.get<Transform>(body).position =
        world.get<Transform>(titan).position + Vec2{world.get<Body>(titan).radius + 60.0, 0.0};
    h.step(1, clients);
}

/// Every petal on the field whose owner is `owner`.
std::vector<Entity> petalsOf(World& world, Entity owner) {
    std::vector<Entity> out;
    Query<PetalInstance> petals{world};
    petals.each([&](Entity e, PetalInstance& petal) {
        if (petal.owner == owner) out.push_back(e);
    });
    return out;
}

int liveMobsOf(World& world, const char* id) {
    int count = 0;
    Query<MobTag, MobType> mobs{world};
    mobs.each([&](Entity e, MobTag&, MobType& type) {
        if (type.configIndex == content().mobIndex(id) && !world.has<Dead>(e)) ++count;
    });
    return count;
}

} // namespace

TEST(the_shipped_titan_stands_in_clear_jungle_space_with_next_tier_petals) {
    CHECK(ensureShippedContent());
    const std::uint16_t titan = content().mobIndex("titan");
    CHECK(titan != kInvalidIndex);
    if (titan == kInvalidIndex) return;
    const MobConfig& config = content().mob(titan);
    CHECK(config.npc.present);
    CHECK(config.npc.service == NpcService::Titan);
    CHECK(config.npc.team == Team::Players);
    CHECK_EQ(config.image, std::string("$titan"));
    CHECK(config.noEggDrop);
    // It cruises about its point on the oracle's leashed flight.
    
    CHECK(content().mobStats(titan, Rarity::Unique).speed > 0.0);
    // A full bar of universals, every one a petal this build has.
    CHECK(config.npc.wearsPetals());
    CHECK_EQ(config.npc.petals.size(), static_cast<std::size_t>(kLoadoutActiveSlots));
    for (const std::uint16_t petal : config.npc.petals) CHECK(petal < content().petalCount());
    CHECK(config.npc.petalRarity == Rarity::Unique);

    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    Realm jungle = Realm::Overworld;
    for (const MapData& map : maps.maps()) {
        if (map.id() == "jungle") jungle = map.realm();
    }
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK(warnings.empty());
    int titans = 0;
    int prestigeTitans = 0;
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (site.mobIndex != titan) continue;
        // The prestige jungle is the same map, titan and all.
        if (maps.isPrestige(site.realm)) {
            ++prestigeTitans;
            continue;
        }
        ++titans;
        CHECK(site.realm == jungle);
        CHECK_NEAR(site.position.x, 27080.0, 1e-6);
        CHECK_NEAR(site.position.y, 28040.0, 1e-6);
        // Universal on its plate, as florr's titan is the top tier on its.
        CHECK(site.rarity == Rarity::Unique);
        // On open ground, body AND ring, as far as its cruise takes it -- the
        // leash and the curve back past it, the bound the oracle's cruise is
        // held to: it meets walls with its centre only, so its body and ring
        // would sweep through any wall closer than that.
        const double radius = content().mobStats(titan, site.rarity).radius;
        const double ringReach = radius + kPetalOrbitRestGap + 20.0;
        const double cruiseReach = ringReach + kNpcLeashRadius + 200.0;
        const Vec2 placed = terrain.resolveCircle(site.position, cruiseReach, site.realm);
        CHECK_NEAR(placed.x, site.position.x, 1e-6);
        CHECK_NEAR(placed.y, site.position.y, 1e-6);
        // Clear of every target dummy, whose hits would otherwise be the
        // titan's on everybody's screen.
        for (const NpcSystem::Site& other : npcs.sites()) {
            if (other.mobIndex == titan || other.realm != site.realm) continue;
            CHECK(distance(other.position, site.position) > cruiseReach + 200.0);
        }
    }
    CHECK_EQ(titans, 1);
    CHECK_EQ(prestigeTitans, 1);
}

TEST(permanent_titan_forge_prices_repeat_independent_petals_and_keeps_other_accounts) {
    const std::string dir = titanWorld("titan-permanent");
    Harness h("titan-permanent", [](const std::string& path) {
        seedAccount(path, "smith");
        seedStack(path, "smith", "petal_rose", Rarity::Super, 145);
        seedStack(path, "smith", "petal_wing", Rarity::Super, 10);
        seedStack(path, "smith", "petal_fragment", Rarity::Super, 10);
        seedAccount(path, "rival");
        seedStack(path, "rival", "petal_rose", Rarity::Unique, 3);
        seedWorn(path, "rival", 0, "rose", Rarity::Unique);
        seedStack(path, "rival", "petal_rose", Rarity::Universal, 1);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    NetClient client, rival;
    CHECK(joinAs(h, client, "smith")); CHECK(joinAs(h, rival, "rival"));
    const std::vector<NetClient*> both{&client, &rival};
    h.step(3, both); standAtTitan(h, both, "smith");
    const auto rose = content().petalIndex("rose");
    const auto wing = content().petalIndex("wing");
    const int remaining[] = {135, 120, 105, 90};
    for (unsigned n = 0; n < 4; ++n) {
        client.requestTitanForge(rose); TitanForgeOutcome out;
        CHECK(awaitForge(h, both, client, out)); CHECK(out.success);
        CHECK(awaitProfile(h, client, [&](const Profile& p) {
            return p.stackCount(rose, Rarity::Super) == static_cast<unsigned>(remaining[n]) &&
                   p.stackCount(rose, Rarity::Unique) == n + 1 &&
                   p.titanForgeCount(rose) == std::min(2u, n + 1);
        }));
    }
    client.requestTitanForge(wing); TitanForgeOutcome out;
    CHECK(awaitForge(h, both, client, out)); CHECK(out.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(wing, Rarity::Super) == 0 && p.stackCount(wing, Rarity::Unique) == 1;
    }));
    client.requestTitanForge(content().petalIndex("fragment"));
    CHECK(awaitForge(h, both, client, out)); CHECK(out.success);
    CHECK_EQ(out.petalIndex, content().petalIndex("timed_mjolnir"));
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(content().petalIndex("timed_mjolnir"), Rarity::Unique) == 1;
    }));
    client.requestTitanForge(content().petalIndex("fragment"));
    CHECK(awaitForge(h,both,client,out));CHECK(out.success);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.stackCount(content().petalIndex("timed_mjolnir"),Rarity::Unique)==2 && p.stackCount(content().petalIndex("fragment"),Rarity::Super)==0;}));
    CHECK_EQ(rival.profile().stackCount(rose, Rarity::Unique), 3u);
    CHECK_EQ(rival.profile().stackCount(rose, Rarity::Universal), 1u);
    CHECK(!rival.profile().loadout[0].empty());
    const auto* user = h.server.database().findUser("smith"); CHECK(user != nullptr);
    if (user) {
        const auto* record = h.server.database().findProgress(user->id); CHECK(record != nullptr);
        if (record) CHECK_EQ(record->extra["titanForgeCounts"]["rose"].asInt(), 2);
    }
    removeDataDir(dir);
}

TEST(permanent_titan_forge_refuses_far_dead_invalid_short_and_blood_sacrifice) {
    const std::string dir = titanWorld("titan-permanent-refuse");
    Harness h("titan-permanent-refuse", [](const std::string& path) {
        seedAccount(path, "smith");
        seedStack(path, "smith", "petal_rose", Rarity::Super, 9);
        seedStack(path, "smith", "petal_blood_sacrifice", Rarity::Super, 600);
        seedStack(path, "smith", "petal_wing", Rarity::Super, 10);
        seedStack(path, "smith", "petal_wing", Rarity::Unique, kMaxStackCount);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    NetClient client; CHECK(joinAs(h, client, "smith")); h.step(3, {&client});
    const auto rose = content().petalIndex("rose"); TitanForgeOutcome out;
    client.requestTitanForge(rose); CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    standAtTitan(h, {&client}, "smith");
    client.requestTitanForge(rose); CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    client.requestTitanForge(0xFFFE); CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    client.requestTitanForge(content().petalIndex("blood_sacrifice"));
    CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    const auto wing = content().petalIndex("wing");
    client.requestTitanForge(wing); CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    CHECK_EQ(client.profile().stackCount(wing, Rarity::Super), 10u);
    CHECK_EQ(client.profile().titanForgeCount(wing), 0u);
    CHECK_EQ(client.profile().stackCount(rose, Rarity::Super), 9u);
    CHECK_EQ(client.profile().titanForgeCount(rose), 0u);
    const auto body = bodyNamed(h.server.world(), "smith");
    h.server.world().add<Dead>(body);
    client.requestTitanForge(rose); CHECK(awaitForge(h, {&client}, client, out)); CHECK(!out.success);
    removeDataDir(dir);
}

TEST(permanent_titan_forge_ledger_survives_save_reload) {
    const auto path = tempUnique("titan-ledger", ".json");
    { Database db; std::string error; CHECK(db.load(path, error));
      auto& record = db.progress("ledger-account");
      record.extra["titanForgeCounts"]["rose"] = 2;
      db.markDirty(); CHECK(db.save()); }
    { Database db; std::string error; CHECK(db.load(path, error));
      CHECK_EQ(db.progress("ledger-account").extra["titanForgeCounts"]["rose"].asInt(), 2); }
    std::remove(path.c_str());
    CHECK_EQ(titanForgeCost(0), 10); CHECK_EQ(titanForgeCost(1), 15);
    CHECK_EQ(titanForgeCost(2), 15); CHECK_EQ(titanForgeCost(2000), 15);
}

TEST(the_titans_ring_orbits_it_as_a_flowers_does_and_kills_mobs_but_spares_flowers) {
    const std::string dir = titanWorld("titan-ring");
    Harness h("titan-ring", [](const std::string& path) { seedAccount(path, "boss", true); },
              dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    NetClient client;
    CHECK(joinAs(h, client, "boss"));
    World& world = h.server.world();
    const Entity titan = npcWearing(world, "titan");
    CHECK(titan != NULL_ENTITY);
    if (titan == NULL_ENTITY) { removeDataDir(dir); return; }
    const double radius = world.get<Body>(titan).radius;
    // It cruises, as the oracle does -- and is held still here, so the ring
    // is measured about a centre that stays put.
    CHECK_EQ(world.get<Npc>(titan).cruises, content().mob(content().mobIndex("titan")).beeFlight);
    world.get<Npc>(titan).cruises = false;

    // Ten petals out, every one the titan's, universal, on the players' side,
    // and orbiting forty past its edge -- a flower's rest radius past its own.
    CHECK(h.stepUntil({&client}, [&] { return petalsOf(world, titan).size() == 10u; }, 200));
    // Wearing a ring makes it no flower: it keeps its mob's body, pool and
    // bite at its own tier, where a flower's are set by its level.
    const MobStats stats = content().mobStats(content().mobIndex("titan"), Rarity::Universal);
    CHECK_NEAR(world.get<Body>(titan).radius, stats.radius, 1e-9);
    CHECK_NEAR(world.get<Health>(titan).max, stats.health, 1e-6);
    CHECK_NEAR(world.get<ContactDamage>(titan).amount, stats.damage, 1e-6);
    CHECK(radius > 400.0);
    // Two seconds: the ring step's swing out to a ring that wide has died away.
    h.step(60, {&client});
    const std::vector<Entity> ring = petalsOf(world, titan);
    CHECK_EQ(ring.size(), std::size_t(10));
    const Vec2 centre = world.get<Transform>(titan).position;
    const double orbit = radius + kPetalOrbitRestGap;
    for (const Entity petal : ring) {
        CHECK(world.get<PetalInstance>(petal).rarity == Rarity::Unique);
        CHECK(world.get<Faction>(petal).team == Team::Players);
        CHECK_NEAR(distance(world.get<Transform>(petal).position, centre), orbit, 12.0);
    }
    // And it turns.
    const Vec2 before = world.get<Transform>(ring[0]).position;
    h.step(5, {&client});
    CHECK(distance(world.get<Transform>(ring[0]).position, before) > 20.0);

    // A flower standing in the ring's band is swept by it a whole turn and
    // loses nothing; a mob in it dies. The rock is put down on screen, just
    // outside the band and clear of the flower, and only then moved into it:
    // put down in the band, it could die before anything saw it alive.
    standAtTitan(h, {&client}, "boss");
    const Entity body = bodyNamed(world, "boss");
    const double full = world.get<Health>(body).max;
    const double bearing = 0.35;
    const Vec2 outside = centre + Vec2::fromAngle(bearing, orbit + 140.0);
    char command[96];
    std::snprintf(command, sizeof command, "/admin spawn rock common %.0f %.0f", outside.x,
                  outside.y);
    client.sendChat(command);
    CHECK(h.stepUntil({&client}, [&] { return liveMobsOf(world, "rock") > 0; }, 30));
    {
        Query<MobTag, MobType, Transform> mobs{world};
        mobs.each([&](Entity, MobTag&, MobType& type, Transform& transform) {
            if (type.configIndex == content().mobIndex("rock")) {
                transform.position = centre + Vec2::fromAngle(bearing, orbit);
            }
        });
    }
    CHECK(h.stepUntil({&client}, [&] { return liveMobsOf(world, "rock") == 0; },
                      kOneRingTurnTicks * 2));
    h.step(kOneRingTurnTicks, {&client});
    CHECK_NEAR(world.get<Health>(body).current, full, 1e-9);

    // Clearing it takes its ring with it, rather than leaving ten petals
    // orbiting nothing; the map's titan is back with a ring of its own.
    client.sendChat("/admin clear_npcs");
    CHECK(h.stepUntil({&client}, [&] {
        const Entity now = npcWearing(world, "titan");
        return now != NULL_ENTITY && now != titan;
    }, 30));
    CHECK(petalsOf(world, titan).empty());
    removeDataDir(dir);
}

TEST(the_titan_painter_draws_its_cog_and_scowl_and_moves_its_glints) {
    CHECK(ensureShippedContent());
    const std::uint16_t titan = content().mobIndex("titan");
    const SpriteCache& sprites = shippedSprites();
    CHECK(sprites.mobArt(titan) == MobArt::Titan);
    CHECK(sprites.mobDrawable(titan));

    // The teeth's tips on the drawn radius: 90 pixels, so an art unit is
    // 90/27.35 of a pixel.
    const double diameter = 180.0;
    const double unit = diameter * 0.5 / 27.35;
    const auto frame = [&](Vec2 gaze) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        sprites.drawMob(canvas, titan, Rarity::Common, kFrame * 0.5, kFrame * 0.5, diameter, 0.0,
                        0.0, false, 0.0, gaze);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };
    const auto rgbAt = [&](const std::vector<std::uint8_t>& pixels, double ax, double ay) {
        const int x = static_cast<int>(kFrame * 0.5 + ax * unit);
        const int y = static_cast<int>(kFrame * 0.5 + ay * unit);
        const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
        if (pixels[i + 3] == 0) return 0x01000000u;   // nothing drawn there
        return (static_cast<std::uint32_t>(pixels[i]) << 16) |
               (static_cast<std::uint32_t>(pixels[i + 1]) << 8) | pixels[i + 2];
    };
    const std::vector<std::uint8_t> picture = frame({1.0, 0.0});
    // A tooth's flat top is cog out to the tip; between two teeth, past the
    // root, there is nothing.
    CHECK_EQ(rgbAt(picture, 26.8, 0.0), 0x535353u);
    const double valley = kPi / 20.0;
    CHECK_EQ(rgbAt(picture, 26.5 * std::cos(valley), 26.5 * std::sin(valley)), 0x01000000u);
    CHECK_EQ(rgbAt(picture, 0.0, 17.0), 0x666666u);
    // No frame round the eye: beside it is face. Then the eye; and above the
    // brow the scowl cuts the eye off, so the top of the socket is face.
    CHECK_EQ(rgbAt(picture, -11.8, -4.0), 0x666666u);
    CHECK_EQ(rgbAt(picture, -8.6, -2.0), 0x111111u);
    CHECK_EQ(rgbAt(picture, -7.0, -10.4), 0x666666u);
    CHECK_EQ(rgbAt(picture, 7.0, -10.4), 0x666666u);

    // The glints travel as a flower's pupils do, and never leave their
    // sockets: glint pixels split by which side of their own eye's middle
    // they sit on.
    const auto glints = [&](Vec2 gaze) {
        const std::vector<std::uint8_t> pixels = frame(gaze);
        PupilSides out;
        for (int y = 0; y < kFrame; ++y) {
            for (int x = 0; x < kFrame; ++x) {
                const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
                if (pixels[i] != 0xEE || pixels[i + 1] != 0xEE || pixels[i + 2] != 0xEE) continue;
                const double ax = (x + 0.5 - kFrame * 0.5) / unit;
                const double ay = (y + 0.5 - kFrame * 0.5) / unit;
                const double dx = ax - (ax < 0.0 ? -7.0 : 7.0);
                const double dy = ay + 5.0;
                if (dx < 0.0) ++out.left;
                else ++out.right;
                if (dx * dx / 9.0 + dy * dy / 36.0 > 1.0) ++out.outsideSocket;
            }
        }
        return out;
    };
    const PupilSides east = glints({1.0, 0.0});
    const PupilSides west = glints({-1.0, 0.0});
    CHECK(east.right > east.left * 2);
    CHECK(west.left > west.right * 2);
    CHECK_EQ(east.outsideSocket, 0);
    CHECK_EQ(west.outsideSocket, 0);
}

// ---------------------------------------------------------------------------
// The assembler
// ---------------------------------------------------------------------------

namespace {

/// One `npcs` object drawn as an AREA (a rectangle, not a point): the NPC it
/// names stands somewhere inside it, picked when it is placed.
std::string fixtureNpcArea(double x, double y, double w, double h, const std::string& npc,
                           const std::string& rarity) {
    std::string out = fixtureNpc(x, y, npc, rarity);
    const std::string point = "\"point\": true, \"rotation\": 0, \"width\": 0, \"height\": 0,";
    out.replace(out.find(point), point.size(),
                "\"rotation\": 0, \"width\": " + std::to_string(w) + ", \"height\": " +
                    std::to_string(h) + ",");
    return out;
}

std::string assemblerWorld(const std::string& name, const char* rarity = "epic") {
    return npcWorld(name, fixtureNpc(kOracleX, kOracleY, "assembler", rarity));
}

/// Waits for the assembler's answer and hands it back with `pending`
/// cleared, as the panel reads it.
bool awaitAssemble(Harness& h, NetClient& client, AssembleOutcome& out) {
    if (!h.stepUntil({&client}, [&] { return client.assembleOutcome().pending; }, 200)) {
        return false;
    }
    out = client.assembleOutcome();
    client.assembleOutcome().pending = false;
    return true;
}

/// The index of the first recipe of mob `mob` that makes `output`, or -1.
int recipeMaking(std::uint16_t mob, const char* output) {
    const std::uint16_t petal = content().petalIndex(output);
    const std::vector<AssemblyRecipe>& recipes = content().mob(mob).npc.recipes;
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        for (const AssemblyRecipe::Stack& made : recipes[i].outputs) {
            if (made.petal == petal) return static_cast<int>(i);
        }
    }
    return -1;
}

/// Makes the fixture's assembler offer the recipe that makes `output`, as if
/// it had rolled it, and returns that recipe's index.
std::uint8_t offerRecipe(Harness& h, const char* output) {
    World& world = h.server.world();
    const Entity npc = npcWearing(world, "assembler");
    const int index = recipeMaking(content().mobIndex("assembler"), output);
    if (npc != NULL_ENTITY && index >= 0) {
        world.get<Npc>(npc).recipe = static_cast<std::uint8_t>(index);
    }
    return static_cast<std::uint8_t>(std::max(0, index));
}

std::vector<Entity> assemblersIn(World& world) {
    std::vector<Entity> out;
    Query<NpcTag, Npc> npcs{world};
    npcs.each([&](Entity e, NpcTag&, Npc& npc) {
        if (npc.service == NpcService::Assembler) out.push_back(e);
    });
    return out;
}

} // namespace

TEST(the_shipped_assembler_makes_a_sawblade_out_of_a_mimic_and_a_ram) {
    CHECK(ensureShippedContent());
    const std::uint16_t assembler = content().mobIndex("assembler");
    CHECK(assembler != kInvalidIndex);
    if (assembler == kInvalidIndex) return;
    const MobConfig& config = content().mob(assembler);
    CHECK(config.npc.present);
    CHECK(config.npc.service == NpcService::Assembler);
    CHECK(config.npc.team == Team::Players);
    CHECK_EQ(config.image, std::string("$assembler"));
    CHECK(config.noEggDrop);
    CHECK(!content().mobStats(assembler, Rarity::Common).ambient);
    // Completely still (user, 2026-10-14): no cruise, no turning.
    CHECK(config.npc.still);
    CHECK(!config.beeFlight);
    CHECK_EQ(content().mobStats(assembler, Rarity::Common).speed, 0.0);

    // The user's mimic + RAM = sawblade, and every one of florr's own recipes
    // (its client's table, 0x10234f910) whose petals all exist here -- ten of
    // its twenty (user, 2026-10-14) -- and two squares = pentagon (user,
    // 2026-10-26).
    CHECK_EQ(config.npc.recipes.size(), std::size_t(13));
    const int saw = recipeMaking(assembler, "sawblade");
    CHECK(saw >= 0);
    if (saw < 0) return;
    const AssemblyRecipe& recipe = config.npc.recipes[static_cast<std::size_t>(saw)];
    CHECK_EQ(recipe.inputs.size(), std::size_t(2));
    CHECK_EQ(recipe.takes(content().petalIndex("mimic")), std::int64_t(1));
    CHECK_EQ(recipe.takes(content().petalIndex("ram")), std::int64_t(1));
    CHECK_EQ(recipe.outputs.size(), std::size_t(1));
    // florr's counts come with them: three honey for a wax, a hundred
    // thousand rocks for a moon, three batteries for a battery and a bolt.
    const int wax = recipeMaking(assembler, "wax");
    CHECK(wax >= 0);
    if (wax >= 0) {
        CHECK_EQ(config.npc.recipes[static_cast<std::size_t>(wax)].takes(
                     content().petalIndex("honey")), std::int64_t(3));
    }
    const int moon = recipeMaking(assembler, "moon_rock");
    CHECK(moon >= 0);
    if (moon >= 0) {
        CHECK_EQ(config.npc.recipes[static_cast<std::size_t>(moon)].takes(
                     content().petalIndex("rock")), std::int64_t(100000));
    }
    const int bolt = recipeMaking(assembler, "lightning");
    CHECK(bolt >= 0);
    if (bolt >= 0) {
        const AssemblyRecipe& r = config.npc.recipes[static_cast<std::size_t>(bolt)];
        CHECK_EQ(r.outputs.size(), std::size_t(2));
        CHECK_EQ(r.takes(content().petalIndex("battery")), std::int64_t(3));
    }
    // The reference pentagon costs two squares and has its own base stats.
    const int pentagon = recipeMaking(assembler, "pentagon");
    CHECK(pentagon >= 0);
    if (pentagon >= 0) {
        const AssemblyRecipe& r = config.npc.recipes[static_cast<std::size_t>(pentagon)];
        CHECK_EQ(r.inputs.size(), std::size_t(1));
        CHECK_EQ(r.takes(content().petalIndex("square")), std::int64_t(2));
        CHECK_EQ(r.outputs.size(), std::size_t(1));
        const PetalConfig& five = content().petal(content().petalIndex("pentagon"));
        CHECK_EQ(five.damage, 20.0);
        CHECK_EQ(five.health, 20.0);
    }
}

TEST(an_assembler_is_a_hornets_size_at_every_tier) {
    // User, 2026-10-14: "the assembler mob size should be same as hornet" --
    // the hornet's size on the mob ladder, so an apex assembler is an apex
    // hornet's size.
    CHECK(ensureShippedContent());
    const std::uint16_t assembler = content().mobIndex("assembler");
    const std::uint16_t hornet = content().mobIndex("hornet");
    CHECK(assembler != kInvalidIndex && hornet != kInvalidIndex);
    if (assembler == kInvalidIndex || hornet == kInvalidIndex) return;
    Terrain terrain;
    World world;
    NpcSystem npcs;
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        const Rarity rarity = static_cast<Rarity>(tier);
        const double expected = std::min(60.0,content().mobStats(hornet, rarity).radius);
        CHECK_NEAR(NpcSystem::npcRadius(content(), assembler, rarity), expected, 1e-9);
        const Entity e = npcs.spawnNpc(world, terrain, content(), assembler, rarity,
                                       {2000.0 + 300.0 * tier, 2000.0}, Realm::Overworld, 0.0);
        CHECK_NEAR(world.get<Body>(e).radius, expected, 1e-9);
    }
}

TEST(an_npc_recipe_list_is_read_and_its_mistakes_are_reported) {
    ContentRegistry registry;
    std::string error;
    // A recipe naming a petal that does not exist is dropped whole -- half a
    // recipe would make something out of less than it says -- and so is one
    // taking more petals than the window has seats for. A petal named twice
    // is taken twice. Recipes on anything but an assembler are refused.
    const std::string mobs = R"({
        "maker": { "name": "Maker", "health": 10, "npc": { "service": "assembler",
            "recipes": [
                { "from": ["basic", "rock"], "to": "rock" },
                { "from": ["basic", "nothing"], "to": "rock" },
                { "from": ["basic", "basic", "basic", "basic", "basic", "basic"], "to": "rock" },
                { "from": ["basic", "basic"], "to": "unheard" },
                { "from": ["rock", "rock"], "to": "basic" },
                "garbage",
                { "from": [{ "petal": "rock", "count": 10 }],
                  "to": [{ "petal": "basic", "count": 2 }, "rock"] },
                { "from": [{ "petal": "rock", "count": 0 }], "to": "basic" }
            ] } },
        "seer": { "name": "Seer", "health": 10, "npc": { "service": "oracle",
            "recipes": [ { "from": ["basic"], "to": "rock" } ] } }
    })";
    const std::string petals = R"({ "basic": { "name": "Basic" }, "rock": { "name": "Rock" } })";
    const std::string dir = tempDir("florr-npc-recipes-" + std::to_string(::getpid()));
    CHECK(test::loadFixtureContent(registry, dir, mobs, petals, error));
    const NpcSpec& maker = registry.mob(registry.mobIndex("maker")).npc;
    CHECK(maker.service == NpcService::Assembler);
    CHECK_EQ(maker.recipes.size(), std::size_t(3));
    if (maker.recipes.size() == 3) {
        CHECK_EQ(maker.recipes[0].outputs.size(), std::size_t(1));
        CHECK_EQ(maker.recipes[0].outputs[0].petal, registry.petalIndex("rock"));
        CHECK_EQ(maker.recipes[0].takes(registry.petalIndex("basic")), std::int64_t(1));
        CHECK_EQ(maker.recipes[1].takes(registry.petalIndex("rock")), std::int64_t(2));
        CHECK_EQ(maker.recipes[1].outputs[0].petal, registry.petalIndex("basic"));
        // Counted stacks, and a list of outputs in the order given.
        const AssemblyRecipe& counted = maker.recipes[2];
        CHECK_EQ(counted.takes(registry.petalIndex("rock")), std::int64_t(10));
        CHECK_EQ(counted.outputs.size(), std::size_t(2));
        if (counted.outputs.size() == 2) {
            CHECK_EQ(counted.outputs[0].petal, registry.petalIndex("basic"));
            CHECK_EQ(counted.outputs[0].count, std::int64_t(2));
            CHECK_EQ(counted.outputs[1].petal, registry.petalIndex("rock"));
            CHECK_EQ(counted.outputs[1].count, std::int64_t(1));
        }
    }
    CHECK(registry.mob(registry.mobIndex("seer")).npc.recipes.empty());
    const auto warned = [&](const char* needle) {
        for (const std::string& line : registry.warnings()) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    };
    CHECK(warned("nothing"));
    CHECK(warned("unheard"));
    CHECK(warned("takes 6 petals"));
    CHECK(warned("npc recipe 5"));
    CHECK(warned("only an assembler's"));
    CHECK(warned("count is not a whole number"));
    removeDataDir(dir);
}

TEST(an_npc_area_with_every_rarity_is_one_roaming_site_per_tier) {
    CHECK(ensureShippedContent());
    const double cell = kTileSize;
    const std::string dir = npcWorld(
        "npc-area", fixtureNpcArea(cell * 4, cell * 4, cell * 16, cell * 16, "assembler", "All") +
                        "," + fixtureNpc(kOracleX, kOracleY, "oracle", "rare"));
    if (dir.empty()) { CHECK(false); return; }
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dir, &terrain, error));
    if (maps.maps().empty()) { CHECK(false); removeDataDir(dir); return; }
    int areas = 0;
    for (const MapElement& element : maps.maps()[0].elements()) {
        if (element.kind != MapElementKind::Npc || element.npcId != "assembler") continue;
        ++areas;
        CHECK(element.npcEveryRarity);
        CHECK(element.npcRoams());
    }
    CHECK_EQ(areas, 1);
    const Rect area{cell * 4, cell * 4, cell * 16, cell * 16};
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK(warnings.empty());
    // A site for every ladder tier, and the oracle's point beside them.
    CHECK_EQ(npcs.sites().size(), std::size_t{1} + 1);
    std::vector<int> perTier(kRarityCount, 0);
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (site.mobIndex != content().mobIndex("assembler")) {
            CHECK(!site.roams);
            continue;
        }
        CHECK(site.roams);
        ++perTier[static_cast<std::size_t>(rarityIndex(site.rarity))];
    }
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        CHECK_EQ(perTier[static_cast<std::size_t>(tier)], tier==rarityIndex(Rarity::Unique)?1:0);
    }
    CHECK_EQ(perTier[static_cast<std::size_t>(rarityIndex(Rarity::Universal))], 0);

    // Placed: inside the area, on open ground, one of each tier -- and one
    // removed comes back, at a spot of its own, while the rest stay put.
    World world;
    npcs.placeMissing(world, terrain, content(), 0.0);
    std::vector<Entity> standing = assemblersIn(world);
    CHECK_EQ(standing.size(), std::size_t{1});
    Entity epic = NULL_ENTITY;
    for (const Entity e : standing) {
        const Vec2 at = world.get<Transform>(e).position;
        CHECK(area.contains(at));
        CHECK(!terrain.blocked(at, Realm::Overworld));
        if (world.get<Npc>(e).rarity == Rarity::Unique) epic = e;
    }
    CHECK(epic != NULL_ENTITY);
    if (epic == NULL_ENTITY) { removeDataDir(dir); return; }
    world.destroy(epic);
    npcs.placeMissing(world, terrain, content(), 0.0);
    standing = assemblersIn(world);
    CHECK_EQ(standing.size(), std::size_t{1});
    int epics = 0;
    for (const Entity e : standing) epics += world.get<Npc>(e).rarity == Rarity::Unique ? 1 : 0;
    CHECK_EQ(epics, 1);
    removeDataDir(dir);
}

TEST(the_computer_holds_one_assembler_of_every_tier_somewhere_inside_it) {
    CHECK(ensureShippedContent());
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    Realm computer = Realm::Overworld;
    bool haveComputer = false;
    for (const MapData& map : maps.maps()) {
        if (map.id() != "cyberspace") continue;
        computer = map.realm();
        haveComputer = true;
    }
    CHECK(haveComputer);
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    CHECK(warnings.empty());
    // The prestige computer is the same map and holds a set of its own.
    bool found = false;
    const Realm prestigeComputer = maps.prestigeRealm(computer, found);
    CHECK(found);
    std::vector<int> perTier(kRarityCount, 0);
    std::vector<int> prestigePerTier(kRarityCount, 0);
    for (const NpcSystem::Site& site : npcs.sites()) {
        if (content().mob(site.mobIndex).id != "cyber_assembler") continue;
        CHECK(site.roams);
        if (site.realm == prestigeComputer) {
            ++prestigePerTier[static_cast<std::size_t>(rarityIndex(site.rarity))];
            continue;
        }
        CHECK(site.realm == computer);
        ++perTier[static_cast<std::size_t>(rarityIndex(site.rarity))];
    }
    for (int tier = 0; tier < kLadderRarityCount; ++tier) {
        CHECK_EQ(perTier[static_cast<std::size_t>(tier)], tier==rarityIndex(Rarity::Unique)?1:0);
        CHECK_EQ(prestigePerTier[static_cast<std::size_t>(tier)], tier==rarityIndex(Rarity::Unique)?1:0);
    }

    // Each put down on open, dry ground in the computer, its body clear of
    // the walls.
    World world;
    npcs.placeMissing(world, terrain, content(), 0.0);
    std::vector<Entity> standing;
    for (const Entity e : assemblersIn(world)) {
        if (world.get<Transform>(e).realm == computer) standing.push_back(e);
    }
    CHECK_EQ(standing.size(), std::size_t{1});
    for (const Entity e : standing) {
        const Transform& transform = world.get<Transform>(e);
        CHECK(transform.realm == computer);
        CHECK(!terrain.blocked(transform.position, computer));
        CHECK(!terrain.inWater(transform.position, computer));
        CHECK(!terrain.resolveWall(transform.position, world.get<Body>(e).radius, computer)
                   .collided);
    }
}

TEST(an_assembler_never_moves_or_turns_whoever_walks_round_or_into_it) {
    // A machine, not a creature: put down facing east, its drawing's own
    // pose, it holds that spot and that facing while a flower circles it and
    // shoves into it -- the flower is what gives way.
    const std::string dir = assemblerWorld("assembler-still");
    Harness h("assembler-still", [](const std::string& path) { seedAccount(path, "builder"); },
              dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    NetClient client;
    CHECK(joinAs(h, client, "builder"));
    h.step(3, {&client});
    World& world = h.server.world();
    const Entity npc = npcWearing(world, "assembler");
    const Entity player = onlyPlayer(world);
    if (npc == NULL_ENTITY || player == NULL_ENTITY) { CHECK(false); removeDataDir(dir); return; }
    CHECK(world.get<Npc>(npc).still);
    CHECK(!world.get<Npc>(npc).cruises);
    const Vec2 home = world.get<Transform>(npc).position;
    CHECK_EQ(world.get<Transform>(npc).angle, 0.0);
    const double radius = world.get<Body>(npc).radius;

    for (const double bearing : {kPi * 0.5, kPi, -kPi * 0.5, 0.25}) {
        // Beside it, then pressed halfway into it.
        world.get<Transform>(player).position = home + Vec2::fromAngle(bearing, radius + 80.0);
        h.step(10, {&client});
        world.get<Transform>(player).position = home + Vec2::fromAngle(bearing, radius * 0.5);
        h.step(10, {&client});
        const Transform& self = world.get<Transform>(npc);
        CHECK_NEAR(self.position.x, home.x, 1e-9);
        CHECK_NEAR(self.position.y, home.y, 1e-9);
        CHECK_EQ(self.angle, 0.0);
        // Pushed back out to its skin.
        CHECK(distance(world.get<Transform>(player).position, home) >= radius);
    }
    // Long enough for any cruise or glance to have shown itself.
    world.get<Transform>(player).position = home + Vec2{0.0, radius + 2000.0};
    h.step(150, {&client});
    CHECK_NEAR(world.get<Transform>(npc).position.x, home.x, 1e-9);
    CHECK_NEAR(world.get<Transform>(npc).position.y, home.y, 1e-9);
    CHECK_EQ(world.get<Transform>(npc).angle, 0.0);
    removeDataDir(dir);
}

TEST(an_assembly_takes_one_of_each_input_per_run_at_the_assemblers_tier) {
    const std::string dir = assemblerWorld("assembler-make");
    Harness h("assembler-make", [](const std::string& path) {
        seedAccount(path, "builder");
        seedStack(path, "builder", "petal_mimic", Rarity::Unique, 3);
        seedStack(path, "builder", "petal_ram", Rarity::Unique, 2);
        seedStack(path, "builder", "petal_mimic", Rarity::Epic, 4);
        seedStack(path, "builder", "petal_ram", Rarity::Epic, 4);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t mimic = content().petalIndex("mimic");
    const std::uint16_t ram = content().petalIndex("ram");
    const std::uint16_t saw = content().petalIndex("sawblade");

    NetClient client;
    CHECK(joinAs(h, client, "builder"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(ram, Rarity::Unique) == 2u;
    }));

    AssembleOutcome outcome;
    const std::uint8_t recipe = offerRecipe(h, "sawblade");
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 2);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(outcome.success);
    CHECK(outcome.rarity == Rarity::Unique);
    CHECK_EQ(outcome.recipe, recipe);
    CHECK_EQ(outcome.made, 2);
    // Two of each in, two sawblades out, all at the assembler's tier; the
    // rare stacks are another tier's, and untouched.
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(mimic, Rarity::Unique) == 1u && p.stackCount(ram, Rarity::Unique) == 0u &&
               p.stackCount(saw, Rarity::Unique) == 2u;
    }));
    CHECK_EQ(client.profile().stackCount(mimic, Rarity::Epic), 4u);
    CHECK_EQ(client.profile().stackCount(ram, Rarity::Epic), 4u);
    CHECK_EQ(client.profile().stackCount(saw, Rarity::Epic), 0u);
    // No wait: the next one is a matter of having the petals.
    CHECK_EQ(client.oracleCooldownRemainingMillis(), 0.0);
    CHECK_EQ(client.traderCooldownRemainingMillis(), 0.0);
    removeDataDir(dir);
}

TEST(the_assembler_refuses_a_short_bag_and_a_flower_not_standing_at_it) {
    const std::string dir = assemblerWorld("assembler-refuse");
    Harness h("assembler-refuse", [](const std::string& path) {
        seedAccount(path, "builder");
        seedStack(path, "builder", "petal_mimic", Rarity::Unique, 5);
        seedStack(path, "builder", "petal_ram", Rarity::Unique, 2);
        seedStack(path, "builder", "petal_mimic", Rarity::Epic, 5);
        seedStack(path, "builder", "petal_ram", Rarity::Epic, 5);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t mimic = content().petalIndex("mimic");
    const std::uint16_t ram = content().petalIndex("ram");
    const std::uint16_t saw = content().petalIndex("sawblade");

    NetClient client;
    CHECK(joinAs(h, client, "builder"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(ram, Rarity::Unique) == 2u;
    }));

    AssembleOutcome outcome;
    const std::uint8_t recipe = offerRecipe(h, "sawblade");
    // Three runs need three RAM: all or nothing, so not one mimic goes.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 3);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);
    CHECK_EQ(outcome.made, 0);
    CHECK(outcome.reason.find("RAM") != std::string::npos);
    // A recipe it does not offer -- another of its mob's -- and no runs at all.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, static_cast<std::uint8_t>(recipe == 0 ? 1 : 0), 1);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);
    CHECK(outcome.reason.find("does not make") != std::string::npos);
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 0);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);
    // The flower is at the ETERNAL assembler: an Epic run is asking for one that
    // is not there, whatever the bag holds.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Epic, recipe, 1);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);
    // Walked away: no assembler in reach.
    {
        World& world = h.server.world();
        const Entity player = onlyPlayer(world);
        const Entity npc = npcWearing(world, "assembler");
        if (player != NULL_ENTITY && npc != NULL_ENTITY) {
            const Vec2 at = world.get<Transform>(npc).position;
            world.get<Transform>(player).position =
                at + Vec2{0.0, world.get<Body>(npc).radius + kNpcServiceReach +
                                   kNpcServiceSlack + 400.0};
        }
        h.step(1, {&client});
    }
    client.requestAssemble(Rarity::Unique, recipe, 1);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);

    h.step(5, {&client});
    CHECK_EQ(client.profile().stackCount(mimic, Rarity::Unique), 5u);
    CHECK_EQ(client.profile().stackCount(ram, Rarity::Unique), 2u);
    CHECK_EQ(client.profile().stackCount(mimic, Rarity::Epic), 5u);
    CHECK_EQ(client.profile().stackCount(saw, Rarity::Unique), 0u);
    CHECK_EQ(client.profile().stackCount(saw, Rarity::Epic), 0u);

    // Back at it, the two RAM make two.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 2);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(outcome.success);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(saw, Rarity::Unique) == 2u && p.stackCount(mimic, Rarity::Unique) == 3u;
    }));
    removeDataDir(dir);
}

TEST(florrs_counted_recipes_take_and_make_their_counts) {
    // battery x3 -> battery + lightning: three in, one of each out, per run.
    const std::string dir = assemblerWorld("assembler-counts", "rare");
    Harness h("assembler-counts", [](const std::string& path) {
        seedAccount(path, "builder");
        seedStack(path, "builder", "petal_battery", Rarity::Unique, 7);
    }, dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    const std::uint16_t battery = content().petalIndex("battery");
    const std::uint16_t bolt = content().petalIndex("lightning");

    NetClient client;
    CHECK(joinAs(h, client, "builder"));
    h.step(3, {&client});
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(battery, Rarity::Unique) == 7u;
    }));
    AssembleOutcome outcome;
    const std::uint8_t recipe = offerRecipe(h, "lightning");
    // Three runs take nine: refused, nothing taken.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 3);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(!outcome.success);
    // Two take six and give two of each back.
    standBeside(h, client, "assembler");
    client.requestAssemble(Rarity::Unique, recipe, 2);
    CHECK(awaitAssemble(h, client, outcome));
    CHECK(outcome.success);
    CHECK_EQ(outcome.made, 2);
    CHECK(awaitProfile(h, client, [&](const Profile& p) {
        return p.stackCount(battery, Rarity::Unique) == 3u && p.stackCount(bolt, Rarity::Unique) == 2u;
    }));
    removeDataDir(dir);
}

TEST(each_assembler_rolls_one_recipe_and_the_client_is_told_which) {
    // florr's assemblers each offer one recipe (user, 2026-10-14: "one
    // random recipe each"): rolled when the assembler is put down, and sent
    // with it, so the window shows the recipe this one makes.
    CHECK(ensureShippedContent());
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, content(), warnings);
    World world;
    npcs.placeMissing(world, terrain, content(), 0.0);
    const std::size_t recipes =
        content().mob(content().mobIndex("assembler")).npc.recipes.size();
    std::vector<int> seen(recipes, 0);
    for (const Entity e : assemblersIn(world)) {
        const std::uint8_t recipe = world.get<Npc>(e).recipe;
        CHECK(recipe < recipes);
        if (recipe < recipes) ++seen[recipe];
    }
    // Ten rolls over eleven recipes are not all the same one.
    int distinct = 0;
    for (const int n : seen) distinct += n > 0 ? 1 : 0;
    CHECK(distinct >= 2);

    // On the wire: the client's copy of the NPC carries the server's roll.
    const std::string dir = assemblerWorld("assembler-roll");
    Harness h("assembler-roll", [](const std::string& path) { seedAccount(path, "builder"); },
              dir, 0);
    if (!h.ready) { CHECK(false); removeDataDir(dir); return; }
    NetClient client;
    CHECK(joinAs(h, client, "builder"));
    standBeside(h, client, "assembler");
    const Entity npc = npcWearing(h.server.world(), "assembler");
    CHECK(npc != NULL_ENTITY);
    const std::uint32_t netId =
        npc != NULL_ENTITY ? h.server.world().get<NetId>(npc).value : 0u;
    CHECK(h.stepUntil({&client}, [&] {
        return client.view().entities().count(netId) != 0;
    }, 100));
    const auto seenBy = client.view().entities().find(netId);
    CHECK(seenBy != client.view().entities().end());
    if (seenBy != client.view().entities().end() && npc != NULL_ENTITY) {
        CHECK_EQ(seenBy->second.npcRecipe, h.server.world().get<Npc>(npc).recipe);
        CHECK_EQ(seenBy->second.npcNextRecipe, h.server.world().get<Npc>(npc).nextRecipe);
        CHECK(seenBy->second.npcRecipeRefreshAtMillis > 0);
    }
    removeDataDir(dir);
}

TEST(the_assembler_painter_draws_florrs_machine_and_turns_its_gears) {
    CHECK(ensureShippedContent());
    const std::uint16_t assembler = content().mobIndex("assembler");
    const SpriteCache& sprites = shippedSprites();
    CHECK(sprites.mobArt(assembler) == MobArt::Assembler);
    CHECK(sprites.mobDrawable(assembler));

    // A radius of 80 pixels: florr's 200-unit box at 80/90 of a pixel a
    // unit, centred on the frame.
    const double diameter = 160.0;
    const double unit = diameter * 0.5 / 90.0;
    const auto frame = [&](double clockMs) {
        Canvas canvas = Canvas::createVirtual(kFrame, kFrame);
        MobMotion motion;
        motion.clockMs = clockMs;
        motion.seed = 7;
        sprites.drawMob(canvas, assembler, Rarity::Common, kFrame * 0.5, kFrame * 0.5, diameter, 0.0,
                        0.0, false, 0.0, {1.0, 0.0}, &motion);
        return canvas.getImageData(0, 0, kFrame, kFrame);
    };
    // A design-box point (florr's numbers) to the pixel it lands on.
    const auto pixelOf = [&](double dx, double dy) {
        const int x = static_cast<int>(kFrame * 0.5 + (dx - 100.0) * unit);
        const int y = static_cast<int>(kFrame * 0.5 + (dy - 100.0) * unit);
        return static_cast<std::size_t>((y * kFrame + x) * 4);
    };
    const auto rgbAt = [&](const std::vector<std::uint8_t>& pixels, double dx, double dy) {
        const std::size_t i = pixelOf(dx, dy);
        if (pixels[i + 3] == 0) return 0x01000000u;
        return (static_cast<std::uint32_t>(pixels[i]) << 16) |
               (static_cast<std::uint32_t>(pixels[i + 1]) << 8) | pixels[i + 2];
    };
    const std::vector<std::uint8_t> still = frame(0.0);
    CHECK_EQ(rgbAt(still, 100.0, 100.0), 0x999999u);   // the body's middle
    CHECK_EQ(rgbAt(still, 100.0, 13.0), 0x4D4D4Du);    // the tread, between links
    CHECK_EQ(rgbAt(still, 100.0, 150.125), 0x00FF00u); // the lamp
    CHECK_EQ(rgbAt(still, 164.5, 37.5), 0x000000u);    // the port
    CHECK_EQ(rgbAt(still, 100.0, 197.0), 0x01000000u); // past the tread: nothing

    // A tenth of a second on, the gears have turned 0.3 rad: their housing
    // changes, and the body away from the gears and the dials does not.
    const std::vector<std::uint8_t> later = frame(100.0);
    int gearChanged = 0;
    int bodyChanged = 0;
    for (int y = 0; y < kFrame; ++y) {
        for (int x = 0; x < kFrame; ++x) {
            const double dx = 100.0 + (x + 0.5 - kFrame * 0.5) / unit;
            const double dy = 100.0 + (y + 0.5 - kFrame * 0.5) / unit;
            const std::size_t i = static_cast<std::size_t>((y * kFrame + x) * 4);
            const bool differs = still[i] != later[i] || still[i + 1] != later[i + 1] ||
                                 still[i + 2] != later[i + 2] || still[i + 3] != later[i + 3];
            if (!differs) continue;
            if (dx > 148.0 && dx < 190.0 && dy > 55.0 && dy < 140.0) ++gearChanged;
            else if (dx > 45.0 && dx < 140.0 && dy > 62.0 && dy < 138.0) ++bodyChanged;
        }
    }
    CHECK(gearChanged > 20);
    CHECK_EQ(bodyChanged, 0);
}

TEST(pyramid_map_scrolls_reveal_one_visit_without_inventory_and_refill) {
    Harness h("pyramid-scrolls", [](const std::string& path) {
        seedAccount(path, "watcher"); seedAccount(path, "explorer");
    }, dataDir(), 0);
    CHECK(h.ready); if (!h.ready) return;
    NetClient watcher, explorer; CHECK(joinAs(h, watcher, "watcher"));
    h.step(3, {&watcher});
    bool found = false; const auto realm = h.server.worldMaps().realmOfId("pyramid", found); CHECK(found);
    auto scrolls = [&] {
        std::vector<Entity> result; Query<Replicated,Transform> q{h.server.world()};
        q.each([&](Entity e, Replicated& r, Transform& t) {
            if(t.realm==realm && r.kind==net::EntityKind::Effect &&
               r.typeIndex==static_cast<std::uint16_t>(GroundEffectKind::PyramidMap)) result.push_back(e);
        }); return result;
    };
    CHECK_EQ(scrolls().size(), 40u);
    for (auto e : scrolls()) {
        const auto at = h.server.world().get<Transform>(e).position;
        CHECK(!h.server.terrain().blocked(at, realm));
        CHECK_NEAR(distance(h.server.terrain().resolveCircle(at,30,realm),at), 0, 0.01);
    }
    CHECK(connectClient(h, explorer)); explorer.requestLogin("explorer", "password7");
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return explorer.status()==NetClient::Status::LoggedIn;}));
    explorer.joinGame(1280,720,"pyramid:spawn","explorer");
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return explorer.status()==NetClient::Status::Playing;}));
    const auto before = explorer.profile().inventory;
    auto& world=h.server.world(); const auto body=bodyNamed(world,"explorer"); CHECK(body!=NULL_ENTITY);
    if(body==NULL_ENTITY)return;
    CHECK(!explorer.pyramidMapRevealed);
    const auto picked=scrolls().front();
    world.get<Transform>(body).position=world.get<Transform>(picked).position;
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return explorer.pyramidMapRevealed;}));
    CHECK(!world.isAlive(picked)); CHECK_EQ(scrolls().size(),39u);
    CHECK(!watcher.pyramidMapRevealed);
    CHECK_EQ(explorer.profile().inventory.size(),before.size());
    h.step(903,{&watcher,&explorer}); CHECK_EQ(scrolls().size(),40u);
    world.get<Health>(body).current=0;world.add<Dead>(body);
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return explorer.dead();}));
    explorer.requestRespawn();
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return !explorer.dead() && explorer.pyramidMapRevealed;}));
    CHECK(explorer.pyramidMapRevealed);
    explorer.leaveGame(); h.step(4,{&watcher,&explorer}); CHECK(!explorer.pyramidMapRevealed);
    explorer.joinGame(1280,720,"pyramid:spawn","explorer");
    CHECK(h.stepUntil({&watcher,&explorer},[&]{return explorer.status()==NetClient::Status::Playing;}));
    CHECK(!explorer.pyramidMapRevealed);
}

TEST(permanent_titan_forge_broadcasts_eternal_theme_to_other_accounts) {
    const auto dir=titanWorld("titan-broadcast");
    Harness h("titan-broadcast",[](const std::string& path){
        seedAccount(path,"smith");seedAccount(path,"watcher");
        seedStack(path,"smith","petal_rose",Rarity::Super,10);
    },dir,0);
    CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient smith,watcher;CHECK(joinAs(h,smith,"smith"));CHECK(joinAs(h,watcher,"watcher"));
    const std::vector<NetClient*> both{&smith,&watcher};h.step(3,both);standAtTitan(h,both,"smith");
    const auto oldLanguage=ui::language();ui::setLanguage(ui::Language::En);
    const auto before=watcher.chat().size();smith.requestTitanForge(content().petalIndex("rose"));
    TitanForgeOutcome out;CHECK(awaitForge(h,both,smith,out));CHECK(out.success);
    CHECK(h.stepUntil(both,[&]{for(std::size_t n=before;n<watcher.chat().size();++n)
        if(watcher.chat()[n].text.find("has been forged by")!=std::string::npos)return true;return false;},60));
    char color[8];std::snprintf(color,sizeof color,"#%06x",rarityColor(Rarity::Unique));
    bool seen=false;for(std::size_t n=before;n<watcher.chat().size();++n){const auto& text=watcher.chat()[n].text;
        if(text.find("has been forged by")==std::string::npos)continue;seen=true;
        CHECK(text.find(color)!=std::string::npos);CHECK(text.find("Eternal")!=std::string::npos);
        CHECK(text.find("@smith")!=std::string::npos);CHECK(text.find("forged x1")!=std::string::npos);
        CHECK(text.find("consumed x10")!=std::string::npos);}
    CHECK(seen);ui::setLanguage(oldLanguage);removeDataDir(dir);
}

TEST(timed_hammer_leases_expire_equipped_and_bag_copies_independently) {
    PlayerRecord record;record.loadout.resize(kLoadoutSlots);addHammerLease(record,1000);addHammerLease(record,1001000);
    CHECK_EQ(record.itemCount(kTitanOutputRarity,"petal_timed_mjolnir"),2);
    StoredItem item;item.petalType="timed_mjolnir";item.rarity=kTitanOutputRarity;
    record.loadout[0]=item;record.addItem(kTitanOutputRarity,"petal_timed_mjolnir",-1);
    CHECK_EQ(expireHammerLeases(record,1002000),0);
    CHECK_EQ(expireHammerLeases(record,3601001),1);CHECK(!record.loadout[0]);
    CHECK_EQ(record.itemCount(kTitanOutputRarity,"petal_timed_mjolnir"),1);
    CHECK_EQ(record.extra["titanHammerLeases"].size(),1u);
    CHECK_EQ(expireHammerLeases(record,4601001),1);
    CHECK_EQ(record.itemCount(kTitanOutputRarity,"petal_timed_mjolnir"),0);
    CHECK_EQ(titanForgeCost(0,"fragment"),15);CHECK_EQ(titanForgeCost(1,"fragment"),15);CHECK_EQ(titanForgeCost(2000,"fragment"),15);
    CHECK_EQ(titanForgeCost(0,"rose"),10);CHECK_EQ(titanForgeCost(2,"rose"),15);
}
TEST(timed_hammer_legacy_deadline_and_offline_expiry_are_preserved) {
    PlayerRecord record;record.addItem(kTitanOutputRarity,"petal_timed_mjolnir",1);
    record.extra["titanHammerExpiresAt"]=Json(10000.0);
    CHECK_EQ(expireHammerLeases(record,9000),0);
    CHECK_NEAR(record.extra["titanHammerLeases"][0]["expiresAt"].asDouble(),10000,1e-8);
    const Json saved=record.extra;PlayerRecord restored=record;restored.extra=saved;
    CHECK_EQ(expireHammerLeases(restored,11000),1);
    CHECK_EQ(restored.itemCount(kTitanOutputRarity,"petal_timed_mjolnir"),0);
}

TEST(feedback_bulk_craft_stops_at_target_and_keeps_equipped_petals) {
    const auto dir=titanWorld("bulk-craft");
    Harness h("bulk-craft",[](const std::string& path){seedAccount(path,"bulk");
        seedStack(path,"bulk","petal_basic",Rarity::Common,100);seedStack(path,"bulk","petal_rose",Rarity::Common,100);
        seedStack(path,"bulk","petal_wing",Rarity::Rare,20);seedWorn(path,"bulk",0,"basic",Rarity::Common);
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient client;CHECK(joinAs(h,client,"bulk"));const auto basic=content().petalIndex("basic"),rose=content().petalIndex("rose"),wing=content().petalIndex("wing");
    client.requestBulkCraft(Rarity::Rare);
    CHECK(h.stepUntil({&client},[&]{return client.profile().stackCount(basic,Rarity::Common)<5 && client.profile().stackCount(rose,Rarity::Common)<5;},120));
    CHECK_EQ(client.profile().stackCount(wing,Rarity::Rare),20u);CHECK_EQ(client.profile().stackCount(basic,Rarity::Epic),0u);
    CHECK(!client.profile().loadout[0].empty());CHECK_EQ(client.profile().loadout[0].petalIndex,basic);removeDataDir(dir);
}
TEST(feedback_expired_timed_hammer_is_removed_on_login_without_touching_permanent_hammer) {
    const auto dir=titanWorld("timed-login");
    Harness h("timed-login",[](const std::string& path){seedAccount(path,"lease");
        seedStack(path,"lease","petal_timed_mjolnir",Rarity::Unique,1);seedStack(path,"lease","petal_mjolnir",Rarity::Unique,1);
        Database db;std::string error;CHECK(db.load(path,error));const auto* user=db.findUser("lease");CHECK(user);if(user){
            auto& record=db.progress(user->id);Json item=Json::object();item["expiresAt"]=Json(1.0);item["slot"]=-1;
            record.extra["titanHammerLeases"]=Json::array();record.extra["titanHammerLeases"].push(item);db.markDirty();CHECK(db.save());}
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient client;CHECK(joinAs(h,client,"lease"));
    CHECK_EQ(client.profile().stackCount(content().petalIndex("timed_mjolnir"),Rarity::Unique),0u);
    CHECK_EQ(client.profile().stackCount(content().petalIndex("mjolnir"),Rarity::Unique),1u);removeDataDir(dir);
}

TEST(urgent_inventory_messages_do_not_run_bulk_crafting_or_consume_payload) {
    const auto dir=titanWorld("urgent-loadout-dispatch");
    Harness h("urgent-loadout-dispatch",[](const std::string& path){
        seedAccount(path,"switcher");
        seedStack(path,"switcher","petal_basic",Rarity::Common,100);
        seedWorn(path,"switcher",0,"basic",Rarity::Common);
        seedWorn(path,"switcher",10,"rose",Rarity::Common);
    },dir,0);
    CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient client;CHECK(joinAs(h,client,"switcher"));
    const auto basic=content().petalIndex("basic"),rose=content().petalIndex("rose");
    client.saveLoadoutPreset(0);h.step(5,{&client});
    client.swapLoadoutSlots(0,10);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.loadout[0].petalIndex==rose;}));
    client.swapLoadoutRows();
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.loadout[0].petalIndex==basic;}));
    client.swapLoadoutSlots(0,10);h.step(5,{&client});
    client.loadLoadoutPreset(0);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.loadout[0].petalIndex==basic && p.loadout[10].petalIndex==rose;}));
    client.setLoadoutSlot(1,basic,Rarity::Common);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.loadout[1].petalIndex==basic && p.stackCount(basic,Rarity::Common)==99;}));
    CHECK_EQ(client.profile().stackCount(basic,Rarity::Uncommon),0u);
    client.requestBulkCraft(Rarity::Uncommon);
    CHECK(awaitProfile(h,client,[&](const Profile& p){return p.stackCount(basic,Rarity::Common)<5;}));
    CHECK_EQ(client.profile().loadout[0].petalIndex,basic);
    CHECK_EQ(client.profile().loadout[1].petalIndex,basic);
    removeDataDir(dir);
}

TEST(daily_bonus_cycle_expiry_missed_day_and_duplicate_claim) {
    PlayerRecord record;const std::int64_t start=20000LL*86400000;
    for(int day=0;day<8;++day){
        const auto now=start+day*86400000;
        CHECK(claimDropBonus(record,now));
        CHECK_EQ(activeDropBonus(record,now),day%4==3?3:2);
        CHECK(!claimDropBonus(record,now+1000));
        CHECK_EQ(activeDropBonus(record,now+3600000),1);
    }
    CHECK(claimDropBonus(record,start+10*86400000));
    CHECK_EQ(dropBonusStreak(record,start+10*86400000),1);
    CHECK_EQ(activeDropBonus(record,start+10*86400000),2);
    Json saved;std::string error;CHECK(Json::parse(record.extra.dump(0),saved,error));PlayerRecord restored;restored.extra=saved;
    CHECK_EQ(activeDropBonus(restored,start+10*86400000+1000),2);
}
TEST(daily_bonus_claim_is_account_scoped_and_reported_over_network) {
    const auto dir=titanWorld("daily-bonus");Harness h("daily-bonus",[](const std::string& path){seedAccount(path,"bonus");seedAccount(path,"plain");},dir,0);
    CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient bonus,plain;CHECK(joinAs(h,bonus,"bonus"));CHECK(joinAs(h,plain,"plain"));
    bonus.requestDailyDropBonus(true);
    CHECK(h.stepUntil({&bonus,&plain},[&]{return bonus.dropBonusMultiplier==2 && bonus.dropBonusStreak==1;}));
    CHECK_EQ(plain.dropBonusMultiplier,1);
    const auto until=bonus.dropBonusUntil;bonus.requestDailyDropBonus(true);h.step(8,{&bonus,&plain});
    CHECK_EQ(bonus.dropBonusStreak,1);CHECK(std::abs(bonus.dropBonusUntil-until)<1000);
    removeDataDir(dir);
}

TEST(shop_locked_rotation_survives_login_and_purchase_charges_exact_quote) {
    const auto dir=titanWorld("shop-lock-purchase");const auto old=shopRotation(shopClockNow())-10;
    Harness h("shop-lock-purchase",[&](const std::string& path){seedAccount(path,"buyer");
        Database db;std::string error;CHECK(db.load(path,error));auto& r=db.progress(db.findUser("buyer")->id);
        r.stars=100000;r.extra["lockedShopRotation"]=Json(static_cast<double>(old));
        // Stored stock deliberately differs from regenerating this rotation.
        // This simulates adding content between lock and server restart.
        captureShopLock(r,old-1);r.extra["lockedShopOffersRotation"]=static_cast<double>(old);
        db.markDirty();CHECK(db.save());
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient buyer;CHECK(joinAs(h,buyer,"buyer"));CHECK_EQ(buyer.lockedShopRotation,old);
    CHECK_EQ(buyer.lockedShopOffers.size(),std::size_t(kShopOfferCount));
    const auto offer=shopOffers(old-1).front();
    CHECK_EQ(buyer.lockedShopOffers.front().petalIndex,offer.petalIndex);
    CHECK(buyer.lockedShopOffers.front().rarity==offer.rarity);
    CHECK_NEAR(buyer.lockedShopOffers.front().price,offer.price,1e-9);
    const auto owned=buyer.profile().stackCount(offer.petalIndex,offer.rarity);
    const auto stars=buyer.profile().stars;buyer.requestBuyPetal(offer.petalIndex,offer.rarity,0);
    CHECK(awaitProfile(h,buyer,[&](const Profile& p){return p.stackCount(offer.petalIndex,offer.rarity)==owned+1;}));
    CHECK_NEAR(buyer.profile().stars,stars-offer.price,1e-6);
    buyer.toggleShopLock();CHECK(h.stepUntil({&buyer},[&]{return buyer.lockedShopRotation==-1;}));
    CHECK(buyer.lockedShopOffers.empty());
    buyer.toggleShopLock();CHECK(h.stepUntil({&buyer},[&]{return buyer.lockedShopRotation==shopRotation(shopClockNow());}));
    CHECK_EQ(kShopRotationSeconds,3600);
    const auto current=shopOffers(buyer.lockedShopRotation).front();
    const auto beforeBulk=buyer.profile().stackCount(current.petalIndex,current.rarity);
    const auto beforeStars=buyer.profile().stars;
    buyer.requestBuyPetal(current.petalIndex,current.rarity,0,5);
    CHECK(awaitProfile(h,buyer,[&](const Profile& p){return p.stackCount(current.petalIndex,current.rarity)==beforeBulk+5;}));
    CHECK_NEAR(buyer.profile().stars,beforeStars-current.price*5,1e-6);
    CHECK_EQ(starsForKill(Rarity::Mythic),1);CHECK_EQ(starsForKill(Rarity::Ultra),5);CHECK_EQ(starsForKill(Rarity::Super),25);
    for(std::uint16_t p=0;p<content().petalCount();++p)if(shopSellsPetal(p)){
        CHECK(shopPrice(p,Rarity::Rare)>=1);CHECK(shopPrice(p,Rarity::Super)>=shopPrice(p,Rarity::Rare));
        CHECK(!shopSellsRarity(Rarity::Unique));
    }
    removeDataDir(dir);
}

TEST(loadout_presets_autosave_edits_without_shift_or_manual_save) {
    const auto dir=titanWorld("preset-autosave");Harness h("preset-autosave",[](const std::string& path){
        seedAccount(path,"auto");seedStack(path,"auto","petal_basic",Rarity::Common,100);
        seedStack(path,"auto","petal_rose",Rarity::Common,1);seedWorn(path,"auto",0,"basic",Rarity::Common);
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient c;CHECK(joinAs(h,c,"auto"));const auto basic=content().petalIndex("basic"),rose=content().petalIndex("rose");
    c.loadLoadoutPreset(1);CHECK(awaitProfile(h,c,[](const Profile& p){return p.loadout[0].empty();}));
    c.setLoadoutSlot(0,rose,Rarity::Common);CHECK(awaitProfile(h,c,[&](const Profile& p){return p.loadout[0].petalIndex==rose;}));
    c.loadLoadoutPreset(2);CHECK(awaitProfile(h,c,[](const Profile& p){return p.loadout[0].empty();}));
    c.setLoadoutSlot(0,basic,Rarity::Common);CHECK(awaitProfile(h,c,[&](const Profile& p){return p.loadout[0].petalIndex==basic;}));
    c.loadLoadoutPreset(1);CHECK(awaitProfile(h,c,[&](const Profile& p){return p.loadout[0].petalIndex==rose;}));
    CHECK_EQ(c.profile().stackCount(basic,Rarity::Common),101u);CHECK_EQ(c.profile().stackCount(rose,Rarity::Common),0u);
    removeDataDir(dir);
}

TEST(row_swap_keeps_primary_when_reserve_is_empty_and_equips_nonempty_reserves){
    const auto dir=titanWorld("row-swap-empty");Harness h("row-swap-empty",[](const std::string& path){
        seedAccount(path,"rows");seedWorn(path,"rows",0,"basic",Rarity::Common);
        seedWorn(path,"rows",1,"rose",Rarity::Common);seedWorn(path,"rows",11,"leaf",Rarity::Common);
        seedWorn(path,"rows",12,"stinger",Rarity::Common);
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient c;CHECK(joinAs(h,c,"rows"));
    const auto basic=content().petalIndex("basic"),rose=content().petalIndex("rose"),leaf=content().petalIndex("leaf"),stinger=content().petalIndex("stinger");
    c.swapLoadoutRows();CHECK(awaitProfile(h,c,[&](const Profile& p){return p.loadout[1].petalIndex==leaf&&p.loadout[2].petalIndex==stinger;}));
    CHECK_EQ(c.profile().loadout[0].petalIndex,basic);CHECK(c.profile().loadout[10].empty());
    CHECK_EQ(c.profile().loadout[11].petalIndex,rose);CHECK(c.profile().loadout[12].empty());
    c.swapLoadoutRows();CHECK(awaitProfile(h,c,[&](const Profile& p){return p.loadout[1].petalIndex==rose;}));
    CHECK_EQ(c.profile().loadout[0].petalIndex,basic);CHECK_EQ(c.profile().loadout[2].petalIndex,stinger);
    CHECK_EQ(c.profile().loadout[11].petalIndex,leaf);CHECK(c.profile().loadout[10].empty());CHECK(c.profile().loadout[12].empty());
    removeDataDir(dir);
}

namespace { std::int64_t hyperForgeTestNow=1000; }
TEST(hyper_forge_network_publishes_holder_protection_and_replaces_online_owner) {
    const auto dir=titanWorld("hyper-network");
    Harness h("hyper-network",[](const std::string& path){
        seedAccount(path,"alpha");seedStack(path,"alpha","petal_rose",Rarity::Unique,5);
        seedAccount(path,"beta");seedStack(path,"beta","petal_rose",Rarity::Unique,5);
    },dir,0);CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient alpha,beta;CHECK(joinAs(h,alpha,"alpha"));CHECK(joinAs(h,beta,"beta"));
    std::vector<NetClient*> both{&alpha,&beta};h.step(3,both);standAtTitan(h,both,"alpha");standAtTitan(h,both,"beta");
    auto& db=const_cast<Database&>(h.server.database());hyperForgeTestNow=1000;
    db.setClock(+[](){return hyperForgeTestNow;});const auto rose=content().petalIndex("rose");
    TitanForgeOutcome out;alpha.requestTitanForge(rose,true);CHECK(awaitForge(h,both,alpha,out));CHECK(out.success);
    CHECK(awaitProfile(h,alpha,[&](const Profile&p){return p.stackCount(rose,Rarity::Apex)==1&&p.stackCount(rose,Rarity::Unique)==0;}));
    beta.requestTitanHolder(rose);CHECK(h.stepUntil(both,[&]{const auto* holder=beta.titanHolder(rose);return holder&&*holder=="alpha";},120));
    CHECK(beta.hyperProtectionLeft(rose)>14LL*3600000);
    CHECK_EQ(beta.hyperInputCost(rose),5);
    beta.requestTitanForge(rose,true);CHECK(awaitForge(h,both,beta,out));CHECK(!out.success);
    CHECK_EQ(beta.profile().stackCount(rose,Rarity::Unique),5u);
    hyperForgeTestNow=1000+15LL*3600000;
    beta.requestTitanForge(rose,true);CHECK(awaitForge(h,both,beta,out));CHECK(out.success);
    CHECK(awaitProfile(h,alpha,[&](const Profile&p){return p.stackCount(rose,Rarity::Apex)==0&&p.stackCount(rose,Rarity::Unique)==4;}));
    CHECK(awaitProfile(h,beta,[&](const Profile&p){return p.stackCount(rose,Rarity::Apex)==1&&p.stackCount(rose,Rarity::Unique)==0;}));
    removeDataDir(dir);
}
TEST(hyper_hammer_network_forges_mjolnir_from_ten_eternal_fragments) {
    const auto dir=titanWorld("hyper-hammer-network");
    Harness h("hyper-hammer-network",[](const std::string& path){seedAccount(path,"smith");seedStack(path,"smith","petal_fragment",Rarity::Unique,10);},dir,0);
    CHECK(h.ready);if(!h.ready){removeDataDir(dir);return;}
    NetClient client;CHECK(joinAs(h,client,"smith"));std::vector<NetClient*> clients{&client};h.step(3,clients);standAtTitan(h,clients,"smith");
    client.requestTitanForge(content().petalIndex("fragment"),true);TitanForgeOutcome out;CHECK(awaitForge(h,clients,client,out));CHECK(out.success);
    CHECK_EQ(out.petalIndex,content().petalIndex("mjolnir"));
    CHECK(awaitProfile(h,client,[&](const Profile&p){return p.stackCount(out.petalIndex,Rarity::Apex)==1&&p.stackCount(content().petalIndex("fragment"),Rarity::Unique)==0;}));removeDataDir(dir);
}
TEST(hyper_garden_and_jungle_network_quotes_and_holdings_are_independent){
    const double cell=kTileSize;
    const auto map=fixtureMap(24,24,fixtureDoor("spawn","Spawn",cell*2,cell*2,cell,cell,true,0.0),
        std::string(),std::string(),{},{},{},fixtureNpc(kTitanX,kTitanY,"titan","unique"));
    const auto dir=stageDataDir("two-titans",{{"garden",map},{"jungle",map}});
    Harness h("two-titans",[](const std::string& path){seedAccount(path,"smith");seedStack(path,"smith","petal_rose",Rarity::Unique,10);},dir,0);
    CHECK(h.ready);if(!h.ready)return;NetClient client;CHECK(joinAs(h,client,"smith"));std::vector<NetClient*> clients{&client};
    standAtTitan(h,clients,"smith");const auto rose=content().petalIndex("rose");TitanForgeOutcome out;
    client.requestTitanForge(rose,true);CHECK(awaitForge(h,clients,client,out));CHECK(out.success);
    CHECK(awaitProfile(h,client,[&](const Profile&p){return p.stackCount(rose,Rarity::Apex)==1;}));
    client.requestTitanHolder(rose);CHECK(h.stepUntil(clients,[&]{const auto* holder=client.titanHolder(rose);return holder&&!holder->empty()&&client.hyperHoldings()==1;},120));
    client.leaveGame();h.step(4,clients);client.joinGame(1280,720,"jungle:spawn","smith");
    CHECK(h.stepUntil(clients,[&]{return client.status()==NetClient::Status::Playing;},200));standAtTitan(h,clients,"smith");
    client.requestTitanHolder(rose);CHECK(h.stepUntil(clients,[&]{const auto* holder=client.titanHolder(rose);return holder&&holder->empty()&&client.hyperInputCost(rose)==5;},120));
    client.requestTitanForge(rose,true);CHECK(awaitForge(h,clients,client,out));CHECK(out.success);
    CHECK(awaitProfile(h,client,[&](const Profile&p){return p.stackCount(rose,Rarity::Apex)==2&&p.stackCount(rose,Rarity::Unique)==0;}));
    const auto& db=h.server.database();CHECK_EQ(hyperForgeState(db,"rose").claims,1);CHECK_EQ(hyperForgeState(db,"rose","garden").claims,1);
    removeDataDir(dir);
}
TEST(hyper_operator_exchange_waits_for_cutover_and_broadcasts_once_without_double_charging) {
    const auto oldLanguage=ui::language();ui::setLanguage(ui::Language::En);
    const auto dir=titanWorld("operator-hammer");
    const auto gate=testsupport::tempUnique("operator-gate",".flag");
    const auto ready=testsupport::tempUnique("operator-ready",".flag");
    {std::ofstream marker(gate);marker<<"maintenance";}
    std::string id;
    Harness h("operator-hammer",[&](const std::string& path){
        seedAccount(path,"smith");seedStack(path,"smith","petal_fragment",Rarity::Unique,10);
        Database db;std::string error;CHECK(db.load(path,error));id=db.findUser("smith")->id;
        Json job=Json::object();job["id"]="test-once";job["userId"]=id;
        job["status"]="pending";job["maintenanceGate"]=gate;job["readyFile"]=ready;
        db.rawTable("operatorHyperForgeRequest")=job;db.markDirty();CHECK(db.save());
    },dir,0);
    CHECK(h.ready);if(!h.ready)return;
    auto saved=[&](){Json value;std::string error;CHECK(Json::parseFile(h.dbPath,value,error));return value;};
    h.step(3,{});CHECK_EQ(saved()["operatorHyperForgeRequest"]["status"].asString(),std::string("pending"));
    std::remove(gate.c_str());h.step(3,{});
    CHECK_EQ(saved()["operatorHyperForgeRequest"]["status"].asString(),std::string("pending"));
    {std::ofstream marker(ready);marker<<"ready";}
    h.step(3,{});auto result=saved();
    CHECK_EQ(result["operatorHyperForgeRequest"]["status"].asString(),std::string("done"));
    CHECK_EQ(result["players"][id]["inventory"]["apex"]["petal_mjolnir"].asInt(),1);
    CHECK_EQ(result["players"][id]["inventory"]["unique"]["petal_fragment"].asInt(),0);
    NetClient client;CHECK(joinAs(h,client,"smith"));std::vector<NetClient*> clients{&client};
    CHECK(h.stepUntil(clients,[&]{return sawText(client,"has been forged by");},60));
    h.step(5,clients);result=saved();
    CHECK(result["operatorHyperForgeRequest"]["announced"].asBool());
    CHECK_EQ(result["players"][id]["inventory"]["apex"]["petal_mjolnir"].asInt(),1);
    int messages=0;for(const auto& message:client.chat())if(message.text.find("has been forged by")!=std::string::npos)++messages;
    CHECK_EQ(messages,1);
    std::remove(ready.c_str());removeDataDir(dir);ui::setLanguage(oldLanguage);
}

TEST(hyper_operator_temporary_grant_keeps_five_materials_and_the_other_protected_owner){
    const auto oldLanguage=ui::language();ui::setLanguage(ui::Language::En);
    const auto dir=titanWorld("temporary-hammer");std::string id,owner;
    Harness h("temporary-hammer",[&](const std::string& path){
        seedAccount(path,"smith");seedAccount(path,"owner");
        seedStack(path,"smith","petal_timed_mjolnir",Rarity::Unique,5);
        seedStack(path,"owner","petal_fragment",Rarity::Unique,10);
        Database db;std::string error;CHECK(db.load(path,error));id=db.findUser("smith")->id;owner=db.findUser("owner")->id;
        CHECK(forgeHyper(db,owner,"fragment",db.nowMillis()).ok);
        Json job=Json::object();job["id"]="temporary-once";job["userId"]=id;job["mode"]="temporary15h";job["status"]="pending";
        db.rawTable("operatorHyperForgeRequest")=job;db.markDirty();CHECK(db.save());
    },dir,0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(joinAs(h,client,"smith"));std::vector<NetClient*> clients{&client};
    CHECK(h.stepUntil(clients,[&]{return sawText(client,"has been forged by");},60));
    h.step(5,clients);Json saved;std::string error;CHECK(Json::parseFile(h.dbPath,saved,error));
    CHECK_EQ(saved["operatorHyperForgeRequest"]["status"].asString(),std::string("done"));
    CHECK(saved["operatorHyperForgeRequest"]["announced"].asBool());
    CHECK_EQ(saved["players"][id]["inventory"]["unique"]["petal_timed_mjolnir"].asInt(),5);
    CHECK_EQ(saved["players"][id]["inventory"]["apex"]["petal_mjolnir"].asInt(),1);
    CHECK(saved["players"][id]["temporaryHyperHammer"]["active"].asBool());
    const auto expires=saved["players"][id]["temporaryHyperHammer"]["expiresAt"].asInt64();
    CHECK(expires-saved["operatorHyperForgeRequest"]["completedAt"].asInt64()<=15LL*3600000);
    CHECK(expires-saved["operatorHyperForgeRequest"]["completedAt"].asInt64()>15LL*3600000-1000);
    CHECK_EQ(saved["hyperForges"]["mjolnir"]["owner"].asString(),owner);
    CHECK_EQ(saved["players"][owner]["inventory"]["apex"]["petal_mjolnir"].asInt(),1);
    ui::setLanguage(oldLanguage);removeDataDir(dir);
}

TEST(shop_lock_snapshot_keeps_stock_and_discount_but_requotes_current_prices) {
    CHECK(ensureShippedContent());
    PlayerRecord record;const auto rotation=shopRotation(shopClockNow());
    record.extra["lockedShopRotation"]=static_cast<double>(rotation);
    captureShopLock(record,rotation);
    const auto captured=lockedShopOffers(record);CHECK(!captured.empty());
    const Json persisted=record.extra;
    record.extra["lockedShopOffers"][0]["price"]=1; // stale price is never authoritative
    const auto quoted=lockedShopOffers(record);
    CHECK_EQ(quoted.front().petalIndex,captured.front().petalIndex);
    CHECK(quoted.front().rarity==captured.front().rarity);
    CHECK_EQ(quoted.front().discountPercent,captured.front().discountPercent);
    CHECK_NEAR(quoted.front().price,captured.front().price,1e-9);
    record.extra=persisted;record.extra["lockedShopRotation"]=-1;
    CHECK(lockedShopOffers(record).empty());
}
