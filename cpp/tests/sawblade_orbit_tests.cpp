#include "test.h"
#include "test_data.h"
#include "server/systems/petals.h"
#include "shared/game/npc.h"

using namespace flix;

TEST(sawblade_uses_the_same_rest_attack_and_defend_orbit_as_basic) {
    ContentRegistry registry; std::string error;
    CHECK(registry.loadFiles(testsupport::dataFile("mobs.json"),
                             testsupport::dataFile("petals.json"), error));
    const auto saw=registry.petalIndex("sawblade"), basic=registry.petalIndex("basic");
    CHECK(saw!=kInvalidIndex && basic!=kInvalidIndex);
    CHECK(!registry.petal(saw).defendOnly);
    for (const auto rarity : {Rarity::Common, Rarity::Super}) {
        World world; CommandBuffer commands{world}; PetalSystem system;
        const Entity player=world.create();
        world.add<PlayerTag>(player);
        world.add<Transform>(player,Transform{{1000,1000},0});
        world.add<Motion>(player); world.add<Knockback>(player);
        world.add<Body>(player,Body{kPlayerBaseRadius,1});
        world.add<Health>(player,Health{110,110,0,0});
        world.add<Faction>(player,Faction{Team::Players,false});
        world.add<PlayerProgress>(player); world.add<PlayerInput>(player);
        world.add<PlayerModifiers>(player); world.add<PlayerLocation>(player);
        world.add<Loadout>(player); world.add<PetalRing>(player);
        world.add<Afflictions>(player); world.add<HitCooldowns>(player);
        world.get<Loadout>(player).slots[0].configIndex=basic;
        world.get<Loadout>(player).slots[0].rarity=rarity;
        world.get<Loadout>(player).slots[1].configIndex=saw;
        world.get<Loadout>(player).slots[1].rarity=rarity;
        double now=1000;
        for(const auto flags : {0, int(net::InputAttack), int(net::InputDefend)}) {
            world.get<PlayerInput>(player).current.flags=flags;
            for(int tick=0;tick<360;++tick) {
                now+=net::kTickMillis;
                system.run(world,registry,now,net::kTickSeconds,commands);
                commands.flush();
            }
            double radii[2]={}; int found=0;
            Query<PetalInstance,Transform> petals{world};
            petals.each([&](Entity,PetalInstance& petal,Transform& at){
                if(petal.owner==player && petal.slot<2) {
                    radii[petal.slot]=(at.position-Vec2{1000,1000}).length();++found;
                }
            });
            CHECK_EQ(found,2); CHECK(radii[0]>0);
            CHECK_NEAR(radii[0],radii[1],0.01);
        }
    }
}
