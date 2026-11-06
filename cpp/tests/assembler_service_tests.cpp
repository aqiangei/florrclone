#include "test.h"
#include "client/ui/accelerating_repeat.h"
#include "server/systems/combat.h"
#include "server/systems/npcs.h"
#include "server/systems/petals.h"
#include "shared/game/spatial.h"
#include "test_data.h"
#include "server_harness.h"
#include <map>

using namespace flix;

TEST(assembler_all_player_tiers_have_safe_sites_and_no_hyper) {
    const auto& registry = testsupport::shippedContent();
    WorldMaps maps; Terrain terrain; std::string error;
    CHECK(maps.load(testsupport::dataDir(),&terrain,error));
    NpcSystem npcs; std::vector<std::string> warnings;
    npcs.loadSites(maps,registry,warnings);
    World w; npcs.placeMissing(w,terrain,registry,0);
    bool found=false; const auto realm=maps.realmOfId("cyberspace",found); CHECK(found);
    std::array<int,kRarityCount> counts{};
    std::vector<std::pair<Vec2,double>> positions;
    for(const auto& site:npcs.sites()) {
        if(site.realm!=realm || registry.mob(site.mobIndex).npc.service!=NpcService::Assembler) continue;
        CHECK(w.isAlive(site.entity)); if(!w.isAlive(site.entity)) continue;
        const auto& at=w.get<Transform>(site.entity); const auto radius=w.get<Body>(site.entity).radius;
        ++counts[rarityIndex(site.rarity)];
        CHECK(!terrain.blocked(at.position,realm));
        for(const auto& previous:positions) CHECK(distanceSq(at.position,previous.first)>(radius+previous.second)*(radius+previous.second));
        positions.push_back({at.position,radius});
    }
    for(int tier=0;tier<kRarityCount;++tier) CHECK_EQ(counts[tier],tier<=rarityIndex(Rarity::Unique)?1:0);
}

TEST(assembler_conversion_cycles_cannot_mint_material_value) {
    const auto& c=testsupport::shippedContent();
    // Positive value witness: each same-tier recipe conserves or consumes
    // value, so no composition or cycle can produce a net material gain.
    const std::map<std::string,double> value={{"glass",1},{"sand",1},{"air",1},{"bubble",1},
        {"moon_rock",100000},{"rock",1},{"honey",1},{"wax",3},{"rose",1},{"light",1},
        {"dahlia",2},{"battery",2},{"lightning",4},{"pollen",2},{"triangle",1},
        {"cutter",10},{"mimic",20},{"ram",1},{"sawblade",21},{"square",1},
        {"pentagon",2},{"cogwheel",1},{"fragment",1}};
    for(const char* id:{"assembler","cyber_assembler","super_assembler"}) {
        for(const auto& recipe:c.mob(c.mobIndex(id)).npc.recipes) {
            double input=0,output=0;
            for(const auto& s:recipe.inputs) input+=value.at(c.petal(s.petal).id)*s.count;
            for(const auto& s:recipe.outputs) output+=value.at(c.petal(s.petal).id)*s.count;
            CHECK(!recipe.outputRarity.has_value());
            CHECK(output<=input);
        }
    }
}

TEST(assembler_quantity_hold_accelerates_and_stops_on_release_or_drag_off) {
    ui::AcceleratingRepeat hold;
    CHECK_EQ(hold.update(1, true, true, 0), 1);
    CHECK_EQ(hold.update(1, false, true, .2), 0);
    CHECK_EQ(hold.update(1, false, true, .36), 1);
    CHECK_EQ(hold.update(1, false, true, 3.1), 10);
    CHECK_EQ(hold.update(1, false, true, 5.1), 100);
    CHECK_EQ(hold.update(1, false, false, 6), 0);
    CHECK_EQ(hold.update(1, false, true, 7), 0);
    CHECK_EQ(hold.update(-1, true, true, 8), -1);
    CHECK_EQ(hold.update(0, false, true, 9), 0);
    CHECK_EQ(hold.update(-1, false, true, 10), 0);
}

TEST(imported_factory_assemblers_are_friendly_services_with_regional_recipes) {
    ContentRegistry registry;
    std::string error;
    CHECK(registry.loadFiles(testsupport::dataFile("mobs.json"),
                             testsupport::dataFile("petals.json"), error));
    WorldMaps maps;
    Terrain terrain;
    CHECK(maps.load(testsupport::dataDir(), &terrain, error));
    NpcSystem npcs;
    std::vector<std::string> warnings;
    npcs.loadSites(maps, registry, warnings);
    World world;
    CombatSystem combat;
    npcs.placeMissing(world, terrain, registry, 0);
    const auto type = registry.mobIndex("assembler");
    const auto& recipes = registry.mob(type).npc.recipes;
    CHECK(!recipes.empty());
    bool found = false;
    for (const auto& map : maps.maps()) {
        if (map.id() != "computer") continue;
        found = true;
        int services = 0;
        for (const auto& e : map.elements()) {
            if (e.kind == MapElementKind::Npc && e.npcId == "assembler") ++services;
            if (e.kind == MapElementKind::Spawn) {
                // Assembler placement must never return to combat spawn rows.
                for (const auto& row : e.mobDistribution) CHECK(row.name != "assembler");
            }
        }
        CHECK_EQ(services, 1);
        for (const auto& site : npcs.sites()) {
            if (site.realm != map.realm() || site.mobIndex != type) continue;
            const Entity npc = npcs.findService(world, NpcService::Assembler, site.position,
                                                site.realm, 100000, site.rarity);
            CHECK(npc != NULL_ENTITY);
            if (npc == NULL_ENTITY) continue;
            CHECK(world.has<NpcTag>(npc));
            CHECK(!world.has<MobTag>(npc));
            CHECK(world.get<Faction>(npc).team == Team::Players);
            const double hp = world.get<Health>(npc).current;
            combat.applyDamage(world, npc, NULL_ENTITY, 1e12, 0, DamageKind::Direct);
            combat.applyDamage(world, npc, NULL_ENTITY, 1e12, 0, DamageKind::Poison);
            CHECK_EQ(world.get<Health>(npc).current, hp);
            CHECK(!world.has<Dead>(npc));
            CHECK(world.get<Npc>(npc).recipe < recipes.size());
        }
    }
    CHECK(found);
}

TEST(ordinary_petals_break_on_contact_and_reload_despite_default_hit_interval) {
    ContentRegistry registry;
    std::string error;
    CHECK(registry.loadFiles(testsupport::dataFile("mobs.json"),
                             testsupport::dataFile("petals.json"), error));
    for (const char* id : {"basic", "stinger", "light", "glass"}) {
        World world;
        CommandBuffer commands{world};
        PetalSystem petals;
        CombatSystem combat;
        const Entity flower = world.create();
        world.add<PlayerTag>(flower);
        world.add<Transform>(flower, Transform{{1000,1000},0});
        world.add<Body>(flower, Body{25,1});
        world.add<Health>(flower, Health{100,100,0,0});
        world.add<Faction>(flower, Faction{Team::Players,false});
        world.add<PlayerModifiers>(flower);
        world.add<Loadout>(flower);
        world.add<PetalRing>(flower);
        world.add<PlayerInput>(flower);
        world.add<PlayerProgress>(flower);
        const auto index = registry.petalIndex(id);
        world.get<Loadout>(flower).slots[0].configIndex=index;
        petals.run(world,registry,1000,net::kTickSeconds,commands);commands.flush();
        petals.run(world,registry,20000,net::kTickSeconds,commands);commands.flush();
        CHECK(!world.get<Loadout>(flower).spawned.empty());
        if(world.get<Loadout>(flower).spawned.empty())continue;
        const Entity petal=world.get<Loadout>(flower).spawned.front();
        const auto at=world.get<Transform>(petal);
        // The movement phase normally places the orbit; keep the owner clear
        // of this deliberately forced petal/mob collision in the unit world.
        world.get<Transform>(flower).position={3000,3000};
        const Entity mob=world.create();
        world.add<MobTag>(mob);
        world.add<Transform>(mob,at);
        world.add<Body>(mob,Body{1,1});
        world.add<Health>(mob,Health{1e9,1e9,0,0});
        world.add<Faction>(mob,Faction{Team::Hostiles,false});
        world.add<ContactDamage>(mob,ContactDamage{1e6,0});
        SpatialGrid grid;
        Query<Transform,Body> bodies{world};
        bodies.each([&](Entity e,Transform& t,Body& b){grid.insert(e,t.realm,t.position,b.radius);});
        EventQueue events;
        combat.run(world,grid,registry,20100,net::kTickSeconds,commands,events);
        commands.flush();
        const bool breakable=registry.petalStats(index,Rarity::Common).breakable;
        CHECK(world.has<Dead>(petal)==breakable);
        petals.run(world,registry,20101,net::kTickSeconds,commands);commands.flush();
        CHECK(world.get<Loadout>(flower).slots[0].broken==breakable);
        if(!breakable)continue;
        const double ready=world.get<Loadout>(flower).slots[0].reloadReadyAtMillis;
        CHECK(ready>20101);
        petals.run(world,registry,ready-1,net::kTickSeconds,commands);commands.flush();
        CHECK(world.get<Loadout>(flower).spawned.empty());
        petals.run(world,registry,ready+1,net::kTickSeconds,commands);commands.flush();
        CHECK(!world.get<Loadout>(flower).spawned.empty());
        std::printf("  %s broke and reloaded after %.1fms\n",id,ready-20101);
    }
}

TEST(assembler_batches_charge_exact_recipe_inputs_and_refuse_invalid_requests) {
    testsupport::HarnessFiles files("assembler-direct");
    GameServer server;
    ServerConfig config;
    config.port=0;config.botCount=0;config.dataDir=testsupport::dataDir();
    config.databasePath=files.dbPath;
    std::string error;
    CHECK(server.start(config,error));
    if(!error.empty())return;
    net::Connection connection(-1,82,"127.0.0.1");
    server.onConnect(connection);
    const auto deliver=[&](ByteWriter& w){ByteReader reader(w.data(),w.size());server.onMessage(connection,reader);CHECK(reader.ok());};
    ByteWriter hello;hello.u8(static_cast<std::uint8_t>(net::ClientMessage::Hello));
    hello.u16(net::kProtocolVersion);hello.u32(content().contentHash());deliver(hello);
    ByteWriter guest;guest.u8(static_cast<std::uint8_t>(net::ClientMessage::RegisterGuest));deliver(guest);
    ByteWriter join;join.u8(static_cast<std::uint8_t>(net::ClientMessage::JoinGame));
    join.u16(1280);join.u16(720);join.str("");join.str("Assembler QA");join.boolean(false);deliver(join);
    Entity flower=NULL_ENTITY,npc=NULL_ENTITY;
    Query<PlayerTag,PlayerAccount> players{server.world()};
    players.each([&](Entity e,PlayerTag&,PlayerAccount& a){if(a.connection==82)flower=e;});
    Query<NpcTag,Npc> stations{server.world()};
    stations.each([&](Entity e,NpcTag&,Npc& n){if(n.service==NpcService::Assembler&&n.rarity==Rarity::Unique)npc=e;});
    CHECK(flower!=NULL_ENTITY&&npc!=NULL_ENTITY);
    if(flower==NULL_ENTITY||npc==NULL_ENTITY)return;
    const auto uid=server.world().get<PlayerAccount>(flower).userId;
    // Isolated test DB, no server loop or concurrent writer is running.
    auto& record=*const_cast<PlayerRecord*>(server.database().findProgress(uid));
    const auto at=server.world().get<Transform>(npc);
    server.world().get<Transform>(flower)=at;
    const auto& recipes=content().mob(server.world().get<Npc>(npc).configIndex).npc.recipes;
    const auto key=[](std::uint16_t p){return "petal_"+content().petal(p).id;};
    const auto request=[&](int recipe,int quantity){ByteWriter w;w.u8(static_cast<std::uint8_t>(net::ClientMessage::Assemble));w.u8(static_cast<std::uint8_t>(Rarity::Unique));w.u8(recipe);w.u16(quantity);deliver(w);};
    for(std::size_t i=0;i<recipes.size();++i){
        const auto& recipe=recipes[i];
        server.world().get<Npc>(npc).recipe=static_cast<std::uint8_t>(i);
        record.inventory=Json::object();
        std::map<std::string,int> expected;
        for(const auto& input:recipe.inputs)expected[key(input.petal)]+=static_cast<int>(input.count*3);
        for(const auto& [id,count]:expected)record.setItemCount(Rarity::Unique,id,count);
        request(static_cast<int>(i),3);
        for(const auto& input:recipe.inputs)expected[key(input.petal)]-=static_cast<int>(input.count*3);
        for(const auto& output:recipe.outputs)expected[key(output.petal)]+=static_cast<int>(output.count*3);
        for(const auto& [id,count]:expected)CHECK_EQ(record.itemCount(Rarity::Unique,id),count);
    }
    CHECK(!recipes.empty());
    server.world().get<Npc>(npc).recipe=0;
    record.inventory=Json::object();
    for(const auto& input:recipes[0].inputs)record.setItemCount(Rarity::Unique,key(input.petal),static_cast<int>(input.count*2));
    const auto before=record.inventory.dump();
    request(0,3);CHECK_EQ(record.inventory.dump(),before); // short bag: all or nothing
    request(0,0);CHECK_EQ(record.inventory.dump(),before);
    request(0,10000);CHECK_EQ(record.inventory.dump(),before);
    request(255,1);CHECK_EQ(record.inventory.dump(),before);
    server.world().get<Transform>(flower).position=at.position+Vec2{100000,100000};
    request(0,1);CHECK_EQ(record.inventory.dump(),before);
    server.world().get<Transform>(flower)=at;
    // Full output stacks must not consume any inputs or partially grant output.
    record.inventory=Json::object();
    for(const auto& input:recipes[0].inputs)
        record.setItemCount(Rarity::Unique,key(input.petal),static_cast<int>(input.count*2));
    for(const auto& output:recipes[0].outputs)
        record.setItemCount(Rarity::Unique,key(output.petal),kMaxStackCount);
    const auto full=record.inventory.dump();
    request(0,1); CHECK_EQ(record.inventory.dump(),full);
    // A forged request cannot obtain a Hyper conversion from an Eternal NPC.
    ByteWriter forged; forged.u8(static_cast<std::uint8_t>(net::ClientMessage::Assemble));
    forged.u8(static_cast<std::uint8_t>(Rarity::Apex)); forged.u8(0); forged.u16(1);
    deliver(forged); CHECK_EQ(record.inventory.dump(),full);
    std::printf("  %zu recipes: exact x3 costs/results; invalid batches and distance refused\n",recipes.size());
    server.onDisconnect(connection,"test complete");
}
