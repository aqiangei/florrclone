#include "test.h"
#include "test_data.h"
#include "server_harness.h"
#include "server/game_server.h"
#include "shared/game/components.h"

using namespace flix;

TEST(ant_hell_respawn_keeps_death_realm_and_position_even_if_corpse_is_removed) {
    GameServer server;
    ServerConfig config;
    config.port = 47889;
    config.botCount = 0;
    config.dataDir = testsupport::dataDir();
    config.databasePath = testsupport::tempDir("respawn-realm") + "/inventory.json";
    std::string error;
    CHECK(server.start(config, error));
    if (!error.empty()) return;
    net::Connection connection(-1, 81, "127.0.0.1");
    server.onConnect(connection);
    const auto deliver = [&](ByteWriter& w) {
        ByteReader reader(w.data(), w.size());
        server.onMessage(connection, reader);
        CHECK(reader.ok());
    };
    ByteWriter hello;
    hello.u8(static_cast<std::uint8_t>(net::ClientMessage::Hello));
    hello.u16(net::kProtocolVersion);
    hello.u32(content().contentHash());
    deliver(hello);
    ByteWriter guest;
    guest.u8(static_cast<std::uint8_t>(net::ClientMessage::RegisterGuest));
    deliver(guest);
    ByteWriter join;
    join.u8(static_cast<std::uint8_t>(net::ClientMessage::JoinGame));
    join.u16(1280); join.u16(720);
    join.str("ant_hell:spawn"); join.str("Respawn QA"); join.boolean(false);
    deliver(join);
    const auto body = [&] {
        Entity found = NULL_ENTITY;
        Query<PlayerTag, PlayerAccount> players{server.world()};
        players.each([&](Entity e, PlayerTag&, PlayerAccount& a) {
            if (a.connection == 81) found = e;
        });
        return found;
    };
    Entity flower = body();
    CHECK(flower != NULL_ENTITY);
    if (flower == NULL_ENTITY) return;
    const Transform death = server.world().get<Transform>(flower);
    const auto* map = server.worldMaps().forRealm(death.realm);
    CHECK(map != nullptr && map->id() == "ant_hell");
    for (int removed = 0; removed < 2; ++removed) {
        server.world().get<Health>(flower).current = 0;
        server.world().add<Dead>(flower);
        testsupport::GameServerPeer::reportDeaths(server);
        if (removed) server.world().destroy(flower);
        ByteWriter respawn;
        respawn.u8(static_cast<std::uint8_t>(net::ClientMessage::Respawn));
        deliver(respawn);
        testsupport::GameServerPeer::flushCommands(server);
        flower = body();
        CHECK(flower != NULL_ENTITY);
        if (flower == NULL_ENTITY) return;
        const auto at = server.world().get<Transform>(flower);
        CHECK(at.realm == death.realm);
        CHECK_NEAR(at.position.x, death.position.x, 0.01);
        CHECK_NEAR(at.position.y, death.position.y, 0.01);
        std::printf("  respawn removed=%d entity=%u health=%.1f\n", removed, static_cast<unsigned>(flower), server.world().get<Health>(flower).current);
        CHECK(server.world().get<Health>(flower).alive());
    }
    server.onDisconnect(connection, "test complete");
}
