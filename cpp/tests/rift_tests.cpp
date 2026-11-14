#include "test.h"
#include "server_harness.h"
#include "client/ui/i18n.h"

using namespace flix;
using namespace flix::testsupport;

TEST(rift_elimination_ends_the_round_and_champion_claims_all_rewards_once) {
    Harness h("rift-round", [](const std::string& path) {
        seedUser(path, "riftwinner", "password7");
        seedUser(path, "riftsecond", "password7");
        seedStack(path, "riftwinner", "petal_basic", Rarity::Super, 5);
    }, dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient winner, second;
    const std::vector<NetClient*> clients{&winner, &second};
    if (!connectClient(h, winner) || !connectClient(h, second)) {
        CHECK(false);
        return;
    }
    winner.requestLogin("riftwinner", "password7");
    second.requestLogin("riftsecond", "password7");
    CHECK(h.stepUntil(clients, [&] {
        return winner.status() == NetClient::Status::LoggedIn &&
               second.status() == NetClient::Status::LoggedIn;
    }));
    winner.joinGame(1280, 720, "pvp", "Winner", false);
    second.joinGame(1280, 720, "", "Second", false);
    CHECK(h.stepUntil(clients, [&] {
        return winner.status() == NetClient::Status::Playing &&
               second.status() == NetClient::Status::Playing;
    }));
    CHECK(!winner.riftOpen());
    const auto& maps = h.server.worldMaps();
    bool found = false;
    const Realm main = maps.realmOfId("rift_main", found);
    CHECK(found);
    if (!found) return;
    const auto* map = maps.forRealm(Realm::Overworld);
    const MapElement* pad = nullptr;
    for (const auto& entry : map->elements()) {
        if (entry.padId == "rift") pad = &entry;
    }
    CHECK(pad != nullptr);
    if (!pad) return;
    const auto bodies = Query<PlayerTag, Transform>{h.server.world()}.collect();
    CHECK_EQ(bodies.size(), std::size_t{2});
    if (bodies.size() != 2) return;
    for (const auto body : bodies) {
        CHECK(h.server.world().get<Transform>(body).realm == Realm::Overworld);
        h.server.world().get<Transform>(body).position = pad->centre();
        h.server.world().get<Health>(body).invulnerableUntilMillis =
            std::numeric_limits<double>::infinity();
    }
    h.step(25, clients);
    CHECK(!winner.riftOpen());
    for (const auto body : bodies) CHECK(h.server.world().isAlive(body));

    h.clock += 4.0 * 60 * 60 * 1000;
    CHECK(h.stepUntil(clients, [&] { return winner.riftOpen() && second.riftOpen(); }));
    // Keep each flower within the charging radius despite player shoves and
    // wandering ambient mobs during this accelerated-clock test.
    bool bothEntered = false;
    for (int attempt = 0; attempt < 100 && !bothEntered; ++attempt) {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            if (auto* at = h.server.world().tryGet<Transform>(bodies[i])) {
                at->position = pad->centre() + Vec2{i == 0 ? -32.0 : 32.0, 0};
            }
        }
        h.step(1, clients);
        bothEntered = winner.view().realm() == main && second.view().realm() == main;
    }
    CHECK(bothEntered);
    if (winner.view().realm() != main || second.view().realm() != main) return;
    CHECK_EQ(winner.profile().inventory.size(), std::size_t{0});
    for (int slot = 0; slot < 5; ++slot) {
        const auto expected = content().petalIndex(slot == 4 ? "rose" : "basic");
        CHECK_EQ(winner.profile().loadout[slot].petalIndex, expected);
        CHECK(winner.profile().loadout[slot].rarity == Rarity::Common);
    }
    h.clock += 3.0 * 60 * 1000;
    CHECK(h.stepUntil(clients, [&] { return !winner.riftOpen(); }));
    auto& world = h.server.world();
    Entity secondBody = NULL_ENTITY;
    Query<PlayerTag, NetId>{world}.each([&](Entity body, PlayerTag&, NetId& id) {
        if (id.value == second.view().self().netId) secondBody = body;
    });
    CHECK(secondBody != NULL_ENTITY);
    if (secondBody == NULL_ENTITY) return;
    world.get<Health>(secondBody).current = 0;
    world.add<Dead>(secondBody, Dead{NULL_ENTITY});
    CHECK(h.stepUntil(clients, [&] { return sawText(second, "你已被淘汰"); }));
    h.clock += 16000;
    second.requestRespawn();
    second.sendChat("/rift revive");
    h.step(10, clients);
    CHECK(second.view().realm() != main);
    const Realm victory = maps.realmOfId("rift_victory", found);
    CHECK(found);
    CHECK(h.stepUntil(clients, [&] { return winner.view().realm() == victory; }));
    const auto prizeDrops = [&] {
        std::vector<Entity> drops;
        Query<DropItem, Transform>{world}.each([&](Entity e, DropItem& item, Transform& at) {
            if (at.realm == victory && item.riftPrize) drops.push_back(e);
        });
        return drops;
    };
    const auto championBody = [&] {
        Entity result = NULL_ENTITY;
        Query<PlayerTag, Transform>{world}.each([&](Entity e, PlayerTag&, Transform& at) {
            if (at.realm == victory) result = e;
        });
        return result;
    };
    CHECK_EQ(prizeDrops().size(), std::size_t{12});
    const auto* loser = GameServerPeer::sessionOf(h.server, "riftsecond");
    if (loser && !prizeDrops().empty()) {
        const auto original = world.get<Transform>(loser->entity);
        world.get<Transform>(loser->entity) = world.get<Transform>(prizeDrops().front());
        h.step(1, clients);
        CHECK_EQ(prizeDrops().size(), std::size_t{12});
        world.get<Transform>(loser->entity) = original;
    }
    winner.sendChat("/rift claim");
    h.step(8, clients);
    CHECK_EQ(prizeDrops().size(), std::size_t{12});
    CHECK_EQ(winner.profile().inventory.size(), std::size_t{0});
    if (prizeDrops().empty() || championBody() == NULL_ENTITY) return;
    world.get<Transform>(championBody()).position = world.get<Transform>(prizeDrops().front()).position;
    CHECK(h.stepUntil(clients, [&] { return prizeDrops().size() == 11; }));
    CHECK(winner.view().realm() == victory);

    // A fresh connection must restore only the uncollected petals, even
    // after the results phase has expired.
    winner.disconnect();
    const std::vector<NetClient*> remaining{&second};
    CHECK(h.stepUntil(remaining, [&] { return prizeDrops().empty(); }));
    h.clock += 4.0 * 60 * 1000;
    h.step(2, remaining);
    NetClient returning;
    CHECK(connectClient(h, returning));
    returning.requestLogin("riftwinner", "password7");
    CHECK(h.stepUntil({&returning}, [&] { return returning.status() == NetClient::Status::LoggedIn; }));
    returning.joinGame(1280, 720, "", "Winner", false);
    CHECK(h.stepUntil({&returning}, [&] { return returning.view().realm() == victory; }));
    CHECK_EQ(prizeDrops().size(), std::size_t{11});
    CHECK_EQ(returning.profile().inventory.size(), std::size_t{0});
    for (int i = 0; i < 11 && !prizeDrops().empty(); ++i) {
        if (championBody() == NULL_ENTITY) break;
        world.get<Transform>(championBody()).position = world.get<Transform>(prizeDrops().front()).position;
        h.step(3, {&returning});
    }
    CHECK(h.stepUntil({&returning}, [&] { return returning.view().realm() == Realm::Overworld; }));
    CHECK(prizeDrops().empty());
    unsigned total = 0;
    unsigned permanentHyper = 0;
    unsigned fission = 0;
    for (const auto& stack : returning.profile().inventory) {
        if (stack.rarity == Rarity::Unique) total += stack.count;
        if (stack.rarity == Rarity::PermanentHyper) permanentHyper += stack.count;
        if (stack.rarity == Rarity::Unique && stack.petalIndex == content().petalIndex("fission")) fission += stack.count;
    }
    CHECK_EQ(total, 11u);
    CHECK_EQ(fission, 1u);
    CHECK_EQ(permanentHyper, 1u);
    const auto* account = GameServerPeer::authorityDb(h.server).findProgress(
        GameServerPeer::sessionOf(h.server, "riftwinner")->userId);
    CHECK(account && account->extra["riftPrizes"].items().empty());
}

TEST(ghost_announcements_start_at_hyper) {
    Harness h("ghost-announcements", [](const std::string& path) {
        seedUser(path, "boss", "password7", true);
    }, dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient client;
    CHECK(connectClient(h, client));
    client.requestLogin("boss", "password7");
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::LoggedIn; }));
    client.joinGame(1280, 720, "", "Boss", false);
    CHECK(h.stepUntil({&client}, [&] { return client.status() == NetClient::Status::Playing; }));
    const auto language = ui::language();
    ui::setLanguage(ui::Language::En);
    for (const auto rarity : {Rarity::Super, Rarity::Unique, Rarity::Apex, Rarity::Ascendant}) {
        const auto mark = client.chatSequence();
        const std::string label = std::string(rarityLabel(rarity)) + " Ghost";
        GameServerPeer::announceGhost(h.server, rarity);
        h.step(3, {&client});
        const bool shouldAnnounce = rarity == Rarity::Apex || rarity == Rarity::Ascendant;
        CHECK_EQ(sawTextSince(client, mark, label + " has spawned"), shouldAnnounce);
        CHECK_EQ(sawTextSince(client, mark, label + " has been summoned"), shouldAnnounce);
        CHECK_EQ(sawTextSince(client, mark, label + " has been defeated"), shouldAnnounce);
    }
    ui::setLanguage(language);
}
