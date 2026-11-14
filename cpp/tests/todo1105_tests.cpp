#include "test.h"
#include "test_data.h"
#include "server/systems/petals.h"
using namespace flix;

TEST(todo1105_swapping_to_mimic_reloads_even_when_the_resolved_petal_is_unchanged) {
    const auto& c=testsupport::shippedContent();World w;PetalSystem petals;CommandBuffer commands{w};
    const auto p=w.create();w.add<PlayerTag>(p);w.add<Transform>(p,Transform{{5000,5000},0});
    w.add<Body>(p,Body{25,1});w.add<Health>(p,Health{100,100});w.add<Loadout>(p);
    w.add<PlayerModifiers>(p);w.add<PetalRing>(p);w.add<PlayerInput>(p);
    for(int slot=0;slot<2;++slot)w.get<Loadout>(p).slots[slot]={c.petalIndex("basic"),Rarity::Common};
    petals.run(w,c,0,.05,commands);commands.flush();petals.run(w,c,10000,.05,commands);commands.flush();
    std::vector<Entity> old;Query<PetalInstance> q{w};
    q.each([&](Entity e,PetalInstance& petal){if(petal.slot==1)old.push_back(e);});CHECK(!old.empty());
    w.get<Loadout>(p).slots[1]={c.petalIndex("mimic"),Rarity::Common};
    petals.run(w,c,10050,.05,commands);commands.flush();
    for(auto e:old)CHECK(!w.isAlive(e));
    CHECK(w.get<Loadout>(p).slots[1].reloadReadyAtMillis>10050);
    int visible=0;q.each([&](Entity,PetalInstance& petal){if(petal.slot==1)++visible;});CHECK_EQ(visible,0);
    petals.run(w,c,20000,.05,commands);commands.flush();
    CHECK_EQ(w.get<PetalSlotState>(p).slots[1].equippedIndex,c.petalIndex("mimic"));
    q.each([&](Entity,PetalInstance& petal){if(petal.slot==1){++visible;CHECK_EQ(petal.configIndex,c.petalIndex("basic"));}});
    CHECK(visible>0);
}

TEST(todo1106_new_talents_preserve_indices_costs_and_refund) {
    CHECK_EQ(static_cast<int>(SkillId::MovementSpeed), 19);
    CHECK_EQ(skillFromKey("prestige"), SkillId::Prestige);
    CHECK_EQ(skillFromKey("petDamage"), SkillId::PetDamage);
    SkillSet skills;
    CHECK(!skills.prestigeUnlocked());
    CHECK_NEAR(skills.effectScale(SkillId::PetDamage), 1.0, 1e-12);
    CHECK_EQ(skillPurchaseCost(skills, SkillId::PetDamage, 9), 156);
    skills.set(SkillId::Prestige, 0);
    skills.set(SkillId::PetDamage, 9);
    CHECK(skills.prestigeUnlocked());
    CHECK_EQ(skills.spent(), 381);
    CHECK_NEAR(skills.effectScale(SkillId::PetDamage), 2.5, 1e-12);
    CHECK_NEAR(skills.healthScale(SkillId::Summoner), 1.0, 1e-12);
    skillRefundTo(skills, SkillId::PetDamage, -1);
    CHECK_EQ(skills.spent(), 225);
    CHECK_NEAR(skills.effectScale(SkillId::PetDamage), 1.0, 1e-12);
    skills.clear();
    CHECK(!skills.prestigeUnlocked());
}

TEST(todo1106_stick_summons_use_hyper_stats_and_talent_without_changing_count) {
    const auto& c = testsupport::shippedContent();
    for (const char* id : {"mysterious_stick", "magic_stick"}) {
        double baseHealth = 0, baseDamage = 0;
        std::size_t baseCount = 0;
        for (int variant = 0; variant < 3; ++variant) {
            World w; PetalSystem petals; CommandBuffer commands{w};
            const auto p = w.create();
            w.add<PlayerTag>(p); w.add<Transform>(p, Transform{{5000,5000},0});
            w.add<Body>(p, Body{25,1}); w.add<Health>(p, Health{100,100});
            w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
            w.add<PlayerInput>(p); w.add<Faction>(p,Faction{Team::Players,false});
            w.add<PlayerSkillTree>(p);
            if(variant == 2) w.get<PlayerSkillTree>(p).skills.set(SkillId::PetDamage,9);
            w.get<Loadout>(p).slots[0] = LoadoutSlot{c.petalIndex(id), variant ? Rarity::Apex : Rarity::Unique};
            w.get<Loadout>(p).slots[1] = LoadoutSlot{c.petalIndex("mana_orb"), Rarity::Unique};
            w.get<PlayerInput>(p).current.flags = net::InputAttack;
            petals.run(w,c,0,.05,commands); commands.flush();
            auto& mana = w.get<ManaPool>(p);
            mana.current = mana.max;
            std::vector<Entity> pets;
            for(int i=0;i<400 && pets.empty();++i) {
                petals.run(w,c,i*50,.05,commands); commands.flush();
                pets = w.get<PetalSlotState>(p).slots[0].pets;
            }
            CHECK(!pets.empty()); if(pets.empty()) continue;
            const auto& owner = w.get<Transform>(p);
            for (const auto pet : pets) {
                CHECK(distance(owner.position, w.get<Transform>(pet).position) <=
                      w.get<Body>(p).radius + 4.0 * w.get<Body>(pet).radius + 8.001);
            }

            if(variant == 0) {
                baseCount=pets.size(); baseHealth=w.get<Health>(pets[0]).max;
                baseDamage=w.get<ContactDamage>(pets[0]).amount;
                CHECK_EQ(baseCount, 2u);
                const auto& storm = c.mob(c.mobIndex("sandstorm"));
                const double sourceDamageScale = std::string_view(id) == "magic_stick" ? 1.25 : 0.55;
                CHECK_NEAR(baseDamage, storm.damage * petalStatScale(Rarity::Unique) * 1.5 *
                                       sourceDamageScale, 1e-6);
            } else {
                CHECK_EQ(pets.size(), baseCount*3);
                for(auto pet : pets) {
                    CHECK(w.get<MobType>(pet).rarity == Rarity::Apex);
                    CHECK_NEAR(w.get<Health>(pet).max, baseHealth*1.65, 1e-6);
                    CHECK_NEAR(w.get<ContactDamage>(pet).amount, baseDamage*1.65*(variant==2?2.5:1.0), 1e-6);
                    CHECK_NEAR(w.get<Pet>(pet).damageTalentScale, variant==2?2.5:1.0, 1e-12);
                }
                if (variant == 2) {
                    const auto seed=w.create();w.add<ContactDamage>(seed,ContactDamage{200,50});
                    w.add<MobRingPetal>(seed,MobRingPetal{pets[0]});
                    w.add<MobPetalRing>(pets[0]).seats.push_back(seed);
                    w.get<PlayerSkillTree>(p).skills.set(SkillId::PetDamage, -1);
                    petals.run(w,c,19950,.05,commands); commands.flush();
                    CHECK_NEAR(w.get<ContactDamage>(seed).amount,80,1e-6);
                    for(auto pet : pets) if(w.isAlive(pet)) {
                        CHECK_NEAR(w.get<ContactDamage>(pet).amount, baseDamage*1.65, 1e-6);
                        CHECK_NEAR(w.get<Pet>(pet).damageTalentScale, 1.0, 1e-12);
                    }
                }
            }
        }
    }
}

TEST(hyper_stick_gets_its_squad_when_magic_stick_is_due_to_fill_the_cap) {
    const auto& content = testsupport::shippedContent();
    World world;
    PetalSystem petals;
    CommandBuffer commands{world};
    const auto player = world.create();
    world.add<PlayerTag>(player);
    world.add<Transform>(player, Transform{{5000, 5000}, 0});
    world.add<Body>(player, Body{25, 1});
    world.add<Health>(player, Health{100, 100});
    world.add<PlayerModifiers>(player);
    world.add<PetalRing>(player);
    world.add<Loadout>(player);
    world.add<PlayerInput>(player);
    auto tick = [&](double now) {
        petals.run(world, content, now, .05, commands);
        commands.flush();
    };
    auto petCount = [&](int slot) {
        return world.get<PetalSlotState>(player).slots[slot].pets.size();
    };

    world.get<Loadout>(player).slots[0] = {content.petalIndex("magic_stick"), Rarity::Apex};
    world.get<Loadout>(player).slots[2] = {content.petalIndex("mana_orb"), Rarity::Apex};
    tick(0);
    world.get<Loadout>(player).slots[1] = {content.petalIndex("mysterious_stick"), Rarity::Apex};
    tick(1000);
    tick(2500);
    tick(3500);

    // Model 24 surviving temporary storms away from the hatch point. At 4500
    // the magic stick is due to cast six more before the ordinary stick acts.
    const auto sandstorm = content.mobIndex("sandstorm");
    for (int i = 0; i < 24; ++i) {
        const auto pet = world.create();
        world.add<Pet>(pet, Pet{player, 0, 20000});
        world.add<MobType>(pet, MobType{sandstorm, Rarity::Apex, 1.0});
        world.get<PetalSlotState>(player).slots[0].pets.push_back(pet);
    }
    auto refillMana = [&] {
        auto& mana = world.get<ManaPool>(player);
        CHECK(mana.max >= content.petalStats(content.petalIndex("magic_stick"), Rarity::Apex).petManaCost);
        mana.current = mana.max;
    };
    refillMana();
    tick(4500);
    CHECK_EQ(petCount(0), 24u);
    CHECK_EQ(petCount(1), 6u);
    CHECK_NEAR(world.get<ManaPool>(player).current, world.get<ManaPool>(player).max, 1e-9);
    if (petCount(1) != 6u) return;

    const auto squad = world.get<PetalSlotState>(player).slots[1].pets;
    for (const auto pet : squad) CHECK_EQ(world.get<MobType>(pet).rarity, Rarity::Apex);
    world.destroy(squad.front());
    refillMana();
    tick(6500);
    CHECK_EQ(petCount(0), 24u);
    CHECK_EQ(petCount(1), 5u);
    tick(7499);
    CHECK_EQ(petCount(1), 5u);
    tick(7500);
    CHECK_EQ(petCount(1), 6u);
    for (std::size_t i = 1; i < squad.size(); ++i) CHECK(world.isAlive(squad[i]));

    // Once the ordinary stick is removed, Magic Stick can use all 30 places.
    world.get<Loadout>(player).slots[1] = {};
    refillMana();
    tick(8500);
    CHECK_EQ(petCount(0), 30u);
    CHECK_EQ(petCount(1), 0u);
    CHECK(world.get<ManaPool>(player).current < world.get<ManaPool>(player).max);
}

TEST(todo1106_totem_counter_tracks_active_stacks_and_clears_on_swap) {
    const auto& c = testsupport::shippedContent();
    World w; PetalSystem petals; CommandBuffer commands{w};
    const auto p = w.create();
    w.add<PlayerTag>(p); w.add<Transform>(p, Transform{{5000,5000},0});
    w.add<Body>(p, Body{25,1}); w.add<Health>(p, Health{100,100});
    w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
    w.add<PlayerInput>(p);
    w.get<Loadout>(p).slots[0] = LoadoutSlot{c.petalIndex("totem"), Rarity::Uncommon};
    const auto step = [&](double now) { petals.run(w,c,now,.05,commands); commands.flush(); };
    for(int i=0;i<200;++i) step(i*50);
    CHECK_EQ(w.get<PetalSlotState>(p).slots[0].counter, 0.0);
    petals.onKill(w,c,p,10000); petals.onKill(w,c,p,11000);
    step(11050);
    CHECK_EQ(w.get<PetalSlotState>(p).slots[0].counter, 2.0);
    step(20000);
    CHECK_EQ(w.get<PetalSlotState>(p).slots[0].counter, 1.0);
    step(21000);
    CHECK_EQ(w.get<PetalSlotState>(p).slots[0].counter, 0.0);
    w.get<Loadout>(p).slots[0] = LoadoutSlot{c.petalIndex("basic"), Rarity::Common};
    step(21050);
    CHECK_EQ(w.get<PetalSlotState>(p).slots[0].counter, -1.0);
}

TEST(todo1106_hyper_egg_uses_hyper_rarity_and_expected_hatch_count) {
    const auto& c = testsupport::shippedContent();
    double eternalHealth = 0, eternalDamage = 0;
    std::size_t eternalCount = 0;
    for (auto rarity : {Rarity::Unique, Rarity::Apex}) {
        World w; PetalSystem petals; CommandBuffer commands{w};
        const auto p = w.create();
        w.add<PlayerTag>(p); w.add<Transform>(p, Transform{{5000,5000},0});
        w.add<Body>(p, Body{25,1}); w.add<Health>(p, Health{100,100});
        w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
        w.add<PlayerInput>(p); w.add<Faction>(p,Faction{Team::Players,false});
        w.get<Loadout>(p).slots[0] = LoadoutSlot{c.petalIndex("egg"), rarity};
        for(int i=0;i<400;++i) { petals.run(w,c,i*50,.05,commands); commands.flush(); }
        const auto pets = w.get<PetalSlotState>(p).slots[0].pets;
        CHECK(!pets.empty()); if(pets.empty()) return;
        if(rarity == Rarity::Unique) {
            eternalCount = pets.size();
            eternalHealth = w.get<Health>(pets[0]).max;
            eternalDamage = w.get<ContactDamage>(pets[0]).amount;
        } else {
            CHECK_EQ(pets.size(), eternalCount * 3);
            for(auto pet : pets) {
                CHECK(w.get<MobType>(pet).rarity == Rarity::Apex);
                CHECK_NEAR(w.get<Health>(pet).max, eternalHealth * 1.875, 1e-6);
                CHECK_NEAR(w.get<ContactDamage>(pet).amount, eternalDamage * 1.875, 1e-6);
            }
        }
    }
}

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

TEST(todo1105_hyper_clover_recovers_faster_and_grants_more_luck) {
    const auto& content = testsupport::shippedContent();
    const auto index = content.petalIndex("clover");
    CHECK(index != kInvalidIndex);
    const auto eternal = content.petalStats(index, Rarity::Unique);
    const auto hyper = content.petalStats(index, Rarity::Apex);
    // Direct damage follows the current Hyper damage rule, separately from luck.
    CHECK_NEAR(hyper.damage, eternal.damage * 2.0, 1e-7);
    CHECK(hyper.reloadMillis < eternal.reloadMillis);
    CHECK(hyper.modifiers.luck > eternal.modifiers.luck);
}

TEST(todo1105_many_pollen_slots_replace_old_clouds_without_erasing_one_slot_cycle) {
    const auto& content = testsupport::shippedContent();
    for (int slots : {1, kLoadoutActiveSlots}) {
        World world;
        PetalSystem petals;
        CommandBuffer commands{world};
        const Entity player = world.create();
        world.add<PlayerTag>(player);
        world.add<Transform>(player, Transform{{5000, 5000}, 0});
        world.add<Body>(player, Body{25, 1});
        world.add<Health>(player, Health{100, 100});
        world.add<PlayerModifiers>(player);
        world.add<PetalRing>(player);
        world.add<Loadout>(player);
        world.add<PlayerInput>(player);
        world.add<Faction>(player, Faction{Team::Players, false});
        for (int i = 0; i < slots; ++i) {
            auto& slot = world.get<Loadout>(player).slots[i];
            slot.configIndex = content.petalIndex("pollen");
            slot.rarity = Rarity::Apex;
        }
        world.get<PlayerInput>(player).current.flags = net::InputAttack;
        Entity oldest = NULL_ENTITY;
        for (int tick = 0; tick < 125; ++tick) {
            petals.run(world, content, tick * net::kTickMillis, net::kTickSeconds, commands);
            Query<Dead>{world}.each([&](Entity entity, Dead&) { commands.destroy(entity); });
            commands.flush();
            const auto& active = world.get<PetalSlotState>(player).pollenClouds;
            if (oldest == NULL_ENTITY && !active.empty()) oldest = active.front();
        }
        int count = 0;
        Query<GroundEffect, Health> clouds{world};
        clouds.each([&](Entity, GroundEffect& cloud, Health&) {
            if (cloud.owner == player) ++count;
        });
        CHECK(oldest != NULL_ENTITY);
        CHECK_EQ(count, slots == 1 ? 12 : 18);
        CHECK_EQ(world.isAlive(oldest), slots == 1);
    }
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

TEST(egg_pet_hatches_outside_the_nearby_wild_mob) {
    const auto& content = testsupport::shippedContent();
    World world;
    PetalSystem petals;
    CommandBuffer commands{world};
    const Vec2 centre{5000, 5000};
    const Entity player = world.create();
    world.add<PlayerTag>(player);
    world.add<Transform>(player, Transform{centre, 0});
    world.add<Body>(player, Body{25, 1});
    world.add<Health>(player, Health{100, 100});
    world.add<PlayerModifiers>(player);
    world.add<PetalRing>(player);
    world.add<Loadout>(player);
    world.add<PlayerInput>(player);
    world.add<Faction>(player, Faction{Team::Players, false});
    world.get<Loadout>(player).slots[0] = LoadoutSlot{content.petalIndex("egg"), Rarity::Apex};
    const Entity mob = world.create();
    world.add<MobTag>(mob);
    world.add<Transform>(mob, Transform{centre, 0});
    world.add<Body>(mob, Body{230, 1});
    int hatched = 0;
    for (int tick = 0; tick < 400 && !hatched; ++tick) {
        petals.run(world, content, tick * net::kTickMillis, net::kTickSeconds, commands);
        commands.flush();
        Query<Pet, Transform, Body>{world}.each([&](Entity, Pet& pet, Transform& at, Body& body) {
            if (pet.owner != player) return;
            ++hatched;
            CHECK(distance(at.position, centre) >= 230.0 + body.radius);
        });
    }
    CHECK(hatched > 0);
}

TEST(todo1105_mecha_antennae_outreaches_ordinary_same_tier) {
 const auto& c=testsupport::shippedContent();
 for(Rarity r:{Rarity::Rare,Rarity::Ultra,Rarity::Super,Rarity::Unique,Rarity::Apex}) {
  CHECK(petalCameraZoom(c,c.petalIndex("observer"),r)<petalCameraZoom(c,c.petalIndex("antennae"),r));
 }
 CHECK(petalCameraZoom(c,c.petalIndex("observer"),Rarity::Unique)<petalCameraZoom(c,c.petalIndex("antennae"),Rarity::Super));
}
TEST(todo1105_hyper_leaf_regen_is_higher_than_super_orb_cycle) {
 const auto& c=testsupport::shippedContent();
 const auto leaf=c.petalStats(c.petalIndex("magic_leaf"),Rarity::Apex);
 const auto orb=c.petalStats(c.petalIndex("mana_orb"),Rarity::Super);
 CHECK(leaf.passiveManaPerSecond>orb.mana*1000.0/(orb.reloadMillis+orb.manaChargeMillis));
 CHECK_NEAR(leaf.passiveManaPerSecond,979.2,1e-6);
}

TEST(todo1105_honey_attack_detaches_and_limits_travel) {
 const auto& c=testsupport::shippedContent();
 World w; PetalSystem petals; CommandBuffer commands{w};
 const auto p=w.create();
 w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{5000,5000},0});
 w.add<Body>(p,Body{25,1}); w.add<Health>(p,Health{100,100});
 w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
 w.add<PlayerInput>(p); w.add<Faction>(p,Faction{Team::Players,false});
 w.get<Loadout>(p).slots[0].configIndex=c.petalIndex("honey");
 for(int i=0;i<100;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
 w.get<PlayerInput>(p).current.flags=net::InputAttack;
 petals.run(w,c,5000,.05,commands);commands.flush();
 Entity bait=NULL_ENTITY;
 Query<HoneyBait>{w}.each([&](Entity e,HoneyBait&){bait=e;});
 CHECK(bait!=NULL_ENTITY);
 if(bait==NULL_ENTITY)return;
 const Vec2 launched=w.get<Transform>(bait).position;
 const double remaining=w.get<HoneyBait>(bait).travel;
 w.get<PlayerInput>(p).current.flags=0;
 for(int i=1;i<=60;++i) {petals.run(w,c,5000+i*50,.05,commands);commands.flush();}
 CHECK_NEAR(distance(launched,w.get<Transform>(bait).position),remaining,1e-6);
 for(int i=61;i<=170;++i) {petals.run(w,c,5000+i*50,.05,commands);commands.flush();}
 CHECK(!w.isAlive(bait));
}

TEST(todo1105_movement_talent_changes_authoritative_flower_speed) {
 const auto& c=testsupport::shippedContent();
 World w; PetalSystem petals;
 const auto p=w.create();
 w.add<PlayerTag>(p); w.add<Transform>(p); w.add<Body>(p,Body{25,1});
 w.add<Health>(p,Health{100,100}); w.add<PlayerModifiers>(p);
 w.add<PetalRing>(p); w.add<Loadout>(p); w.add<PlayerSkillTree>(p);
 petals.foldModifiers(w,c);
 CHECK_NEAR(w.get<PlayerModifiers>(p).speedScale,1.0,1e-9);
 for (int tier = 0; tier < 3; ++tier) {
  w.get<PlayerSkillTree>(p).skills.set(SkillId::MovementSpeed,tier);
  petals.foldModifiers(w,c);
  CHECK_NEAR(w.get<PlayerModifiers>(p).speedScale,1.0+0.02*(tier+1),1e-9);
 }
}

TEST(todo1105_shovel_defense_burrows_then_expires) {
 const auto& c=testsupport::shippedContent();
 World w; PetalSystem petals; CommandBuffer commands{w};
 const auto p=w.create();
 w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{5000,5000},0});
 w.add<Body>(p,Body{25,1}); w.add<Health>(p,Health{100,100});
 w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
 w.add<PlayerInput>(p); w.add<Faction>(p,Faction{Team::Players,false});
 auto& slot=w.get<Loadout>(p).slots[0];
 slot.configIndex=c.petalIndex("shovel"); slot.rarity=Rarity::Unique;
 for(int i=0;i<200;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
 w.get<PlayerInput>(p).current.flags=net::InputDefend;
 petals.run(w,c,10000,.05,commands);commands.flush();
 const auto* state=w.tryGet<BurrowState>(p);
 CHECK(state && state->active);
 if(!state)return;
 CHECK_NEAR(state->untilMillis,12000,1e-9);
 w.get<PlayerInput>(p).current.flags=0;
 petals.run(w,c,12000,.05,commands);commands.flush();
 CHECK(!w.get<BurrowState>(p).active);
}

TEST(todo1105_meteor_plants_on_attack_and_expires) {
 const auto& c=testsupport::shippedContent();
 World w; PetalSystem petals; CommandBuffer commands{w};
 const auto p=w.create();
 w.add<PlayerTag>(p); w.add<Transform>(p,Transform{{5000,5000},0});
 w.add<Body>(p,Body{25,1}); w.add<Health>(p,Health{100,100});
 w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p);
 w.add<PlayerInput>(p); w.add<Faction>(p,Faction{Team::Players,false});
 w.get<Loadout>(p).slots[0].configIndex=c.petalIndex("meteor");
 w.get<PlayerInput>(p).current.flags=0;
 for(int i=0;i<150;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
 Entity rock=NULL_ENTITY;
 Query<PlantedMeteor>{w}.each([&](Entity e,PlantedMeteor&){rock=e;});
 CHECK_EQ(rock,NULL_ENTITY);
 w.get<PlayerInput>(p).current.flags=net::InputAttack;
 petals.run(w,c,7500,.05,commands);commands.flush();
 Query<PlantedMeteor>{w}.each([&](Entity e,PlantedMeteor&){rock=e;});
 CHECK(rock!=NULL_ENTITY);
 if(rock==NULL_ENTITY)return;
 CHECK(w.has<LoosePetal>(rock));
 CHECK_NEAR(w.get<Body>(rock).radius,180,1e-9);
 w.get<PlayerInput>(p).current.flags=0;
 for(int i=151;i<560;++i) {petals.run(w,c,i*50,.05,commands);commands.flush();}
 CHECK(!w.isAlive(rock));
}
