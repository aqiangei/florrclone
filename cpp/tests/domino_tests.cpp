#include "test.h"
#include "test_data.h"
#include "server/systems/combat.h"
#include "server/systems/petals.h"
#include "client/world_view.h"
#include "shared/game/domino.h"
using namespace flix;

TEST(domino_rolls_preserve_the_damage_budget_and_hyper_reduces_blanks) {
    const auto& c = testsupport::shippedContent();
    const auto index = c.petalIndex("domino");
    const auto e = c.petalStats(index,Rarity::Unique), h = c.petalStats(index,Rarity::Apex);
    CHECK_NEAR(c.petalStats(index,Rarity::Common).damage,9,1e-9);
    CHECK_NEAR(h.damage,e.damage*1.5,1e-6);
    CHECK_NEAR(h.reloadMillis,e.reloadMillis,1e-6);
    Rng rng(19281);
    int blanks[2]{};
    for (int tier=0; tier<2; ++tier) {
        const Rarity rarity = tier ? Rarity::Apex : Rarity::Unique;
        double sum=0; bool seen[49]{};
        for (int n=0; n<100000; ++n) {
            const auto pips=rollDominoPips(rng,rarity); CHECK(pips<49); seen[pips]=true;
            const double scale=dominoDamageMultiplier(pips,rarity);
            sum+=scale; if(scale==0) ++blanks[tier];
        }
        CHECK_NEAR(sum/100000,1,0.015);
        for (bool present:seen) CHECK(present);
    }
    CHECK(blanks[0]>24000 && blanks[0]<29000);
    CHECK(blanks[1]>3000 && blanks[1]<5000);
}

TEST(domino_actual_damage_and_network_pips_match_and_reload_rolls_again) {
    const auto& c=testsupport::shippedContent();
    World w; CommandBuffer commands{w}; EventQueue events; PetalSystem petals;
    std::uint32_t nextId=1;petals.allocateNetId=[&]{return nextId++;};
    const auto owner=w.create();
    w.add<PlayerTag>(owner); w.add<Transform>(owner,Transform{{10000,10000},0});
    w.add<Motion>(owner); w.add<Body>(owner,Body{25,1}); w.add<Health>(owner,Health{100,100});
    w.add<PlayerModifiers>(owner); w.add<PlayerProgress>(owner); w.add<PetalRing>(owner);
    w.add<PlayerInput>(owner); w.add<PlayerLocation>(owner);
    w.add<NetId>(owner,NetId{900000}); w.add<Replicated>(owner,Replicated{net::EntityKind::Player,0,0,Rarity::Common});
    auto& load=w.add<Loadout>(owner); load.slots[0].configIndex=c.petalIndex("domino"); load.slots[0].rarity=Rarity::Common;
    petals.run(w,c,1000,0,commands); petals.run(w,c,3600,0,commands);
    CHECK_EQ(w.get<Loadout>(owner).spawned.size(),std::size_t(1));
    if(w.get<Loadout>(owner).spawned.empty()) return;
    const auto petal=w.get<Loadout>(owner).spawned.front();
    w.get<PetalInstance>(petal).dominoPips=48; // six at each end
    const auto at=w.get<Transform>(petal).position;
    const auto mob=w.create(); w.add<MobTag>(mob);w.add<Transform>(mob,Transform{at,0});
    w.add<Body>(mob,Body{15,1}); w.add<Health>(mob,Health{1000,1000});
    w.add<Faction>(mob,Faction{Team::Hostiles,false});
    SpatialGrid grid; grid.insert(mob,Realm::Overworld,at,15);
    CombatSystem combat; combat.run(w,grid,c,3700,0,commands,events);
    CHECK_NEAR(w.get<Health>(mob).current,964,1e-6);
    Replicator replicator; ClientView view; Replicator::Frame frame;
    frame.tick=1;frame.nowMillis=3700;frame.events=&events;
    ByteWriter out;replicator.build(w,owner,view,frame,out);
    ByteReader reader(out.data(),out.size());CHECK_EQ(reader.u8(),static_cast<std::uint8_t>(net::ServerMessage::Snapshot));
    WorldView client;CHECK(client.applySnapshot(reader));CHECK(reader.ok());
    const auto id=w.get<NetId>(petal).value;
    CHECK(client.entities().count(id)>0);
    if(client.entities().count(id))CHECK_EQ(client.entities().at(id).petalVariant,48);
    w.get<PetalInstance>(petal).dominoPips=0;
    combat.run(w,grid,c,3800,0,commands,events);
    CHECK_NEAR(w.get<Health>(mob).current,964,1e-6); // a blank really deals no damage
    bool changed=false;
    for(int i=0;i<12;++i) {
        const auto current=w.get<Loadout>(owner).spawned.front(); w.get<Health>(current).current=0;
        petals.run(w,c,4000+i*2000,0,commands);commands.flush();
        petals.run(w,c,5500+i*2000,0,commands);commands.flush();
        CHECK(!w.get<Loadout>(owner).spawned.empty());
        if(w.get<Loadout>(owner).spawned.empty())break;
        changed |= w.get<PetalInstance>(w.get<Loadout>(owner).spawned.front()).dominoPips!=48;
    }
    CHECK(changed);
}
