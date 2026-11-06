#include "test.h"
#include "test_data.h"
#include "server_harness.h"
#include "server/systems/petals.h"
#include "shared/game/dark_mark.h"

using namespace flix;
using Peer = testsupport::GameServerPeer;
namespace {
struct MarkFixture {
    GameServer server;
    net::Connection caster{-1, 81, "127.0.0.1"};
    net::Connection victim{-1, 82, "127.0.0.2"};
    Entity owner = NULL_ENTITY, target = NULL_ENTITY;
    bool ready = false;
    MarkFixture() {
        ServerConfig config;
        config.port = 47889; config.botCount = 0;
        config.dataDir = testsupport::dataDir();
        config.databasePath = testsupport::tempDir("dark-mark") + "/inventory.json";
        std::string error;
        ready = server.start(config, error);
        CHECK(ready);
        if (!ready) return;
        join(caster); join(victim);
        Query<PlayerTag, PlayerAccount> players{server.world()};
        players.each([&](Entity e, PlayerTag&, PlayerAccount& a) {
            if (a.connection == 81) owner = e;
            if (a.connection == 82) target = e;
        });
        CHECK(owner != NULL_ENTITY); CHECK(target != NULL_ENTITY);
        ready = owner && target;
        if (!ready) return;
        for (const Entity e : {owner, target}) server.world().get<PlayerProgress>(e).level = 20;
        Peer::markClock(server, 1000000);
    }
    void send(net::Connection& connection, ByteWriter& w) {
        ByteReader reader(w.data(), w.size()); server.onMessage(connection, reader); CHECK(reader.ok());
    }
    void join(net::Connection& connection) {
        server.onConnect(connection);
        ByteWriter hello; hello.u8(static_cast<std::uint8_t>(net::ClientMessage::Hello));
        hello.u16(net::kProtocolVersion); hello.u32(content().contentHash()); send(connection, hello);
        ByteWriter guest; guest.u8(static_cast<std::uint8_t>(net::ClientMessage::RegisterGuest)); send(connection, guest);
        ByteWriter join; join.u8(static_cast<std::uint8_t>(net::ClientMessage::JoinGame));
        join.u16(1280); join.u16(720); join.str("garden:spawn"); join.str("Mark QA"); join.boolean(false); send(connection, join);
    }
    void kill() {
        server.world().get<Health>(target).current = 0;
        server.world().ensure<Dead>(target);
        Peer::reportDeaths(server);
    }
    void returnCommand() {
        ByteWriter w; w.u8(static_cast<std::uint8_t>(net::ClientMessage::Chat)); w.str("/mark-return"); send(victim, w);
    }
};
}

TEST(dark_mark_normal_transports_revives_returns_and_preserves_progress) {
    MarkFixture f; if (!f.ready) return;
    auto& w = f.server.world();
    const Transform before = w.get<Transform>(f.target);
    const auto stars = w.get<PlayerProgress>(f.target).stars;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    f.kill();
    CHECK(Peer::applyMark(f.server, f.target, f.owner, Rarity::Unique));
    CHECK(isStanding(w, f.target));
    CHECK(f.server.worldMaps().forRealm(w.get<Transform>(f.target).realm)->biome() == "hel");
    CHECK_NEAR(w.get<TeleporterState>(f.target).markHoldUntilMillis, 0, 0);
    CHECK_EQ(w.get<PlayerProgress>(f.target).stars, stars);
    CHECK_NEAR(w.get<Health>(f.target).current, w.get<Health>(f.target).max, 0);
    f.returnCommand();
    CHECK(w.get<Transform>(f.target).realm == before.realm);
    CHECK(distanceSq(w.get<Transform>(f.target).position, before.position) < 1);
    f.kill();
    CHECK(!Peer::applyMark(f.server, f.target, f.owner, Rarity::Apex));
    Peer::markClock(f.server, 1300000);
    CHECK(Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
}

TEST(dark_mark_hyper_hold_is_bounded_and_death_clears_it) {
    MarkFixture f; if (!f.ready) return;
    auto& w = f.server.world(); const Transform before = w.get<Transform>(f.target);
    f.kill(); CHECK(Peer::applyMark(f.server, f.target, f.owner, Rarity::Apex));
    const Realm hel = w.get<Transform>(f.target).realm;
    CHECK_NEAR(w.get<TeleporterState>(f.target).markHoldUntilMillis, 1030000, 0);
    f.returnCommand(); CHECK(w.get<Transform>(f.target).realm == hel);
    Peer::markTeleport(f.server, f.target, before.realm, before.position);
    CHECK(w.get<Transform>(f.target).realm == hel);
    Peer::markClock(f.server, 1029999);
    Peer::markTeleport(f.server, f.target, before.realm, before.position);
    CHECK(w.get<Transform>(f.target).realm == hel);
    Peer::markClock(f.server, 1030000); f.returnCommand();
    CHECK(w.get<Transform>(f.target).realm == before.realm);
    // A second hold after protection expires still ends immediately on death.
    Peer::markClock(f.server, 1300000); f.kill();
    CHECK(Peer::applyMark(f.server, f.target, f.owner, Rarity::Apex));
    f.kill(); CHECK_NEAR(w.get<TeleporterState>(f.target).markHoldUntilMillis, 0, 0);
}

TEST(dark_mark_rejects_higher_tier_low_level_other_realms_and_dungeons) {
    MarkFixture f; if (!f.ready) return;
    auto& w = f.server.world(); f.kill();
    w.get<Loadout>(f.target).slots[0] = {};
    auto& slot = w.get<Loadout>(f.target).slots[0];
    slot.configIndex = content().petalIndex("rose"); slot.rarity = Rarity::Apex;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Unique));
    CHECK(Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    slot.rarity = Rarity::Admin;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    slot.rarity = Rarity::Common;
    w.get<PlayerProgress>(f.target).level = 19;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    w.get<PlayerProgress>(f.target).level = 20;
    const Realm original = w.get<Transform>(f.target).realm;
    w.get<Transform>(f.target).realm = Realm::Arena;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    bool found = false; const Realm dungeon = f.server.worldMaps().realmOfId("swarm", found); CHECK(found);
    w.get<Transform>(f.target).realm = dungeon; w.get<Transform>(f.owner).realm = dungeon;
    CHECK(!Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
    w.get<Transform>(f.target).realm = original; w.get<Transform>(f.owner).realm = original;
    CHECK(Peer::canMark(f.server, f.target, f.owner, Rarity::Apex));
}

TEST(dark_mark_real_petal_arms_once_and_cancels_on_revival) {
    MarkFixture f; if (!f.ready) return;
    auto& w = f.server.world(); CommandBuffer commands{w};
    auto& petals = Peer::markPetals(f.server);
    auto& slot = w.get<Loadout>(f.owner).slots[0];
    slot.configIndex = content().petalIndex("dark_mark"); slot.rarity = Rarity::Apex;
    petals.run(w, content(), 1000000, 0, commands);
    petals.run(w, content(), 1021000, 0, commands);
    Entity mark = NULL_ENTITY;
    for (const auto e : w.get<Loadout>(f.owner).spawned) {
        if (w.get<PetalInstance>(e).configIndex == content().petalIndex("dark_mark")) mark = e;
    }
    CHECK(mark != NULL_ENTITY); if (!mark) return;
    f.kill(); w.get<Transform>(f.target).position = w.get<Transform>(mark).position;
    w.get<PlayerInput>(f.owner).current.flags = 1;
    Peer::markClock(f.server, 1021000); petals.run(w, content(), 1021000, 0, commands);
    CHECK(w.get<PetalInstance>(mark).markTarget == f.target);
    const Vec2 parked = w.get<Transform>(mark).position;
    w.get<Transform>(f.owner).position.x += 500;
    petals.run(w, content(), 1022000, 0.016, commands);
    CHECK(distanceSq(w.get<Transform>(mark).position, parked) < 1e-8);
    w.get<Health>(f.target).current = w.get<Health>(f.target).max; w.remove<Dead>(f.target);
    petals.run(w, content(), 1023000, 0, commands);
    CHECK_NEAR(w.get<Health>(mark).current, 0, 0);
    CHECK(w.get<Transform>(f.target).realm == Realm::Overworld);
}
