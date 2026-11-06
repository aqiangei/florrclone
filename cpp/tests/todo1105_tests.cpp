#include "test.h"
#include "test_data.h"
#include "server/systems/petals.h"
using namespace flix;
TEST(todo1105_orphaned_ring_petals_are_removed_without_touching_planted_meteors) {
    const auto& content = testsupport::shippedContent();
    World world;
    PetalSystem petals;
    CommandBuffer commands{world};
    const Entity previousOwner = world.create();
    const Entity orphan = world.create();
    PetalInstance instance;
    instance.owner = previousOwner;
    world.add<PetalInstance>(orphan, instance);
    world.destroy(previousOwner);

    const Entity meteor = world.create();
    world.add<PlantedMeteor>(meteor, PlantedMeteor{previousOwner});
    world.add<Lifetime>(meteor, Lifetime{1.0});
    petals.run(world, content, 50, 0.05, commands);
    commands.flush();
    CHECK(!world.isAlive(orphan));
    CHECK(world.isAlive(meteor));
}

TEST(todo1105_duplicated_magic_capacity_stacks_and_resets) {
 const auto& c=testsupport::shippedContent();
 for(const char* id:{"magic_cactus","mana_orb"}) {
  World w; PetalSystem petals; const auto p=w.create();
  w.add<PlayerTag>(p);w.add<Transform>(p);w.add<Body>(p,Body{25,1});
  w.add<Health>(p,Health{100,100});w.add<PlayerModifiers>(p);w.add<PetalRing>(p);
  w.add<Loadout>(p);w.add<PlayerSkillTree>(p);
  auto& slot=w.get<Loadout>(p).slots[0];slot.configIndex=c.petalIndex(id);slot.rarity=Rarity::Ultra;
  petals.foldModifiers(w,c);const double base=w.get<PlayerModifiers>(p).maxMana;CHECK(base>0);
  auto& skills=w.get<PlayerSkillTree>(p).skills;
  if(const auto* gate=skillGate(SkillId::Duplicator)) skills.set(gate->parent,gate->parentLevel-1);
  skills.set(SkillId::Duplicator,2);
  CHECK_EQ(petalCopies(1,&skills,0,rarityIndex(slot.rarity),c.petal(slot.configIndex).stackable),2);
  petals.foldModifiers(w,c);CHECK_NEAR(w.get<PlayerModifiers>(p).maxMana,base*2,1e-9);
  slot.broken=true;petals.foldModifiers(w,c);CHECK_NEAR(w.get<PlayerModifiers>(p).maxMana,base*2,1e-9);
  skills.set(SkillId::Duplicator,-1);petals.foldModifiers(w,c);CHECK_NEAR(w.get<PlayerModifiers>(p).maxMana,base,1e-9);
 }
}


TEST(todo1105_blueberries_fit_two_equal_tier_mana_suppliers) {
 const auto& c=testsupport::shippedContent();
 for(Rarity r:{Rarity::Ultra,Rarity::Super,Rarity::Unique,Rarity::Apex}) {
  const auto berry=c.petalStats(c.petalIndex("magic_peas"),r);
  const auto leaf=c.petalStats(c.petalIndex("magic_leaf"),r);
  const auto orb=c.petalStats(c.petalIndex("mana_orb"),r);
  const double cost=berry.requiredMana*1000.0/berry.reloadMillis;
  CHECK(berry.reloadMillis>=250);
  CHECK(cost<=2*leaf.passiveManaPerSecond);
  CHECK(cost<=2*orb.mana*1000.0/(orb.reloadMillis+1500.0));
 }
}

TEST(todo1105_egg_replaces_missing_pet_without_recalling_survivors) {
 const auto& c=testsupport::shippedContent();World w;PetalSystem petals;CommandBuffer commands{w};
 const auto p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{5000,5000},0});
 w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100});w.add<PlayerModifiers>(p);
 w.add<PetalRing>(p);w.add<Loadout>(p);w.add<PlayerInput>(p);w.add<Faction>(p,Faction{Team::Players,false});
 w.get<Loadout>(p).slots[0].configIndex=c.petalIndex("egg");w.get<Loadout>(p).slots[0].rarity=Rarity::Apex;
 for(int i=0;i<400;++i){petals.run(w,c,i*50,.05,commands);commands.flush();}
 auto pets=w.get<PetalSlotState>(p).slots[0].pets;CHECK(pets.size()>=2);if(pets.size()<2)return;
 const auto survivor=pets[1];w.add<Dead>(pets[0]);w.get<Health>(pets[0]).current=0;
 for(int i=400;i<800;++i){petals.run(w,c,i*50,.05,commands);commands.flush();}
 CHECK(w.isAlive(survivor));CHECK(!w.has<Dead>(survivor));
}

#include "shared/game/difficulty.h"
#include "shared/game/super_population.h"
TEST(todo1105_farm_density_and_hel_respawn_remain_separate) {
 CHECK_EQ(kSuperFarmPopulation,54);
 SuperPopulation farm(kSuperFarmPopulation),hel(36,120000);
 farm.observe(54,0);farm.observe(53,100);hel.observe(36,0);hel.observe(35,100);
 CHECK(farm.ready(30100));CHECK(!hel.ready(30100));CHECK(!hel.ready(120099));CHECK(hel.ready(120100));
}
