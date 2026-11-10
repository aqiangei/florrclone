#include "test.h"
#include "test_data.h"
#include "server/systems/combat.h"
#include "server/systems/petals.h"
using namespace flix;
namespace {
struct CombatFeedback {
    const ContentRegistry& c=testsupport::shippedContent();
    World w; CommandBuffer commands{w}; EventQueue events; SpatialGrid grid;
    PetalSystem petals; CombatSystem combat; Entity player;
    CombatFeedback() {
        player=w.create(); w.add<PlayerTag>(player); w.add<Transform>(player,Transform{{5000,5000},0});
        w.add<Health>(player,Health{10000,10000}); w.add<Body>(player,Body{25,1});
        w.add<Faction>(player,Faction{Team::Players,false}); w.add<PlayerModifiers>(player);
        w.add<PetalRing>(player); w.add<Loadout>(player); w.add<PlayerInput>(player); w.add<Motion>(player);
    }
    void equip(int slot,const char* id,Rarity rarity=Rarity::Unique) {
        w.get<Loadout>(player).slots[slot]={c.petalIndex(id),rarity};
    }
    void prepare() {for(int i=0;i<200;++i){petals.run(w,c,i*50,.05,commands);commands.flush();}}
    Entity mob(Vec2 at) {
        auto e=w.create(); w.add<MobTag>(e); w.add<Transform>(e,Transform{at,0});
        w.add<Body>(e,Body{1,1}); w.add<Health>(e,Health{1e12,1e12});
        w.add<Faction>(e,Faction{Team::Hostiles,false}); w.add<NetId>(e,NetId{77}); return e;
    }
    void hit(double now) {
        grid.clear(); Query<Transform,Body> q{w};
        q.each([&](Entity e,Transform& t,Body& b){grid.insert(e,t.realm,t.position,b.radius);});
        combat.run(w,grid,c,now,.05,commands,events); commands.flush();
    }
};
}

TEST(hyper_magic_bubble_follows_movement_with_more_push_than_eternal) {
    const auto pop = [](Rarity rarity) {
        CombatFeedback r;
        r.equip(0, "mana_orb", Rarity::Apex);
        r.equip(1, "magic_bubble", rarity);
        r.prepare();
        auto& input = r.w.get<PlayerInput>(r.player).current;
        input.flags = net::InputDefend;
        input.moveStrength = 1.0;
        input.moveAngle = 0.0;
        input.aimAngle = kPi * 0.5;
        input.aimDistance = 200;
        r.petals.run(r.w, r.c, 10000.0, .05, r.commands);
        r.commands.flush();
        return r.w.get<Motion>(r.player).velocity;
    };
    const auto eternal = pop(Rarity::Unique);
    CHECK(eternal.x > 0.0);
    CHECK_NEAR(eternal.y, 0.0, 1e-9);
    for (const auto rarity : {Rarity::Apex, Rarity::PermanentHyper}) {
        const auto hyper = pop(rarity);
        CHECK_NEAR(hyper.x, eternal.x * 1.10, 1e-9);
        CHECK_NEAR(hyper.y, 0.0, 1e-9);
    }
}

TEST(magic_eye_eternal_uses_seven_mana_orbs_against_a_super_bee) {
    CombatFeedback r;
    r.equip(0, "magic_eye");
    for (int slot = 1; slot <= 7; ++slot) r.equip(slot, "mana_orb");
    r.prepare();

    auto& mana = r.w.get<ManaPool>(r.player);
    const double maxMana = mana.max;
    CHECK_NEAR(maxMana, 103488.0, 1e-6);
    mana.current = maxMana;
    const auto bee = r.mob({5075.0, 5000.0});
    const double health = r.c.mobStats(r.c.mobIndex("bee"), Rarity::Super).health;
    r.w.get<Health>(bee) = {health, health};
    r.w.get<PlayerInput>(r.player).current.flags = net::InputAttack | net::InputDefend;
    r.petals.run(r.w, r.c, 10000.0, .05, r.commands, nullptr, &r.events);
    r.commands.flush();
    r.hit(10000.0);
    CHECK_NEAR(r.w.get<ManaPool>(r.player).current, maxMana * 0.5, 1e-6);
    CHECK_NEAR(health - r.w.get<Health>(bee).current, 4346496.0, 1e-3);
}

TEST(practice_dps_tracks_actual_sources_on_one_dummy_and_expires) {
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    CombatFeedback r;
    const auto makeDummy = [&]() {
        const auto e = r.mob({5000, 5000});
        r.w.remove<MobTag>(e);
        r.w.add<NpcTag>(e);
        Npc npc;
        npc.configIndex = content().mobIndex("target_dummy");
        r.w.add<Npc>(e, npc);
        return e;
    };
    const auto dummy = makeDummy();
    const auto otherDummy = makeDummy();
    const auto ordinaryMob = r.mob({5000, 5000});
    const auto orb = content().petalIndex("light");
    const auto eye = content().petalIndex("magic_eye");
    CHECK(orb != kInvalidIndex);
    CHECK(eye != kInvalidIndex);
    const auto petal = r.w.create();
    PetalInstance instance;
    instance.owner = r.player;
    instance.configIndex = orb;
    instance.rarity = Rarity::Super;
    r.w.add<PetalInstance>(petal, instance);
    const auto lightning = r.w.create();
    GroundEffect effect;
    effect.owner = r.player;
    r.w.add<GroundEffect>(lightning, effect);
    r.w.add<LightningTarget>(lightning, LightningTarget{dummy, true, Rarity::Unique});

    r.combat.applyDamage(r.w, dummy, petal, 40.0, 1000.0);
    r.combat.applyDamage(r.w, dummy, petal, 60.0, 2000.0);
    r.combat.applyDamage(r.w, dummy, lightning, 200.0, 2000.0, DamageKind::Lightning);
    r.combat.applyDamage(r.w, dummy, r.player, 50.0, 2000.0, DamageKind::Reflect);
    r.combat.applyDamage(r.w, otherDummy, petal, 900.0, 2000.0);
    r.combat.applyDamage(r.w, ordinaryMob, petal, 700.0, 2000.0);
    CHECK_NEAR(r.w.get<Health>(dummy).current, 1e12, 1e-3);

    const auto rows = r.combat.practiceDamage(r.player, 2500.0);
    CHECK_EQ(rows.size(), 3u);
    if (rows.size() != 3u) return;
    CHECK_EQ(rows[0].petal, eye);
    CHECK(rows[0].rarity == Rarity::Unique);
    CHECK_NEAR(rows[0].amount, 200.0, 1e-6);
    CHECK_EQ(rows[1].petal, orb);
    CHECK_NEAR(rows[1].amount, 100.0, 1e-6);
    CHECK(rows[2].source == CombatSystem::PracticeSource::Reflect);
    CHECK_NEAR(rows[2].amount, 50.0, 1e-6);
    CHECK(r.combat.practiceDamage(r.player, 12000.0).empty());
    r.combat.resetPracticeDamage(r.player);
    CHECK(r.combat.practiceDamage(r.player, 2500.0).empty());
}

TEST(triangle_stacks_with_its_own_tier_before_talent) {
    for (const auto tier : {Rarity::Common, Rarity::Super, Rarity::Unique}) {
        CombatFeedback r;
        r.equip(0, "triangle", tier);
        r.equip(1, "triangle", tier);
        r.equip(2, "triangle", tier == Rarity::Super ? Rarity::Unique : Rarity::Super);
        r.equip(kLoadoutActiveSlots, "triangle", tier);
        auto& skills = r.w.add<PlayerSkillTree>(r.player).skills;
        skills.set(SkillId::Damage, 2);
        r.prepare();
        std::array<Entity, 2> targets{};
        Query<PetalInstance> petals{r.w};
        petals.each([&](Entity e, PetalInstance& petal) {
            if (petal.slot < targets.size())
                targets[petal.slot] = r.mob(r.w.get<Transform>(e).position);
        });
        const double bonus = tier == Rarity::Unique ? 26244 : tier == Rarity::Super ? 8748 : 4;
        const double base = r.c.petalStats(r.c.petalIndex("triangle"), tier).damage;
        const double expected = (base + bonus) * skills.effectScale(SkillId::Damage);
        r.hit(10000);
        for (const Entity target : targets) {
            CHECK(target != NULL_ENTITY);
            if (target != NULL_ENTITY)
                CHECK_NEAR(1e12 - r.w.get<Health>(target).current, expected, 1e-3);
        }
    }
}

TEST(eternal_triangle_counts_each_additional_eternal_and_ignores_super) {
    for (const int copies : {1, 3}) {
        CombatFeedback r;
        for (int slot = 0; slot < copies; ++slot) r.equip(slot, "triangle", Rarity::Unique);
        r.equip(copies, "triangle", Rarity::Super);
        r.prepare();
        Entity triangle = NULL_ENTITY;
        Query<PetalInstance>{r.w}.each([&](Entity e, PetalInstance& petal) {
            if (petal.slot == 0) triangle = e;
        });
        CHECK(triangle != NULL_ENTITY);
        if (triangle == NULL_ENTITY) continue;
        const auto target = r.mob(r.w.get<Transform>(triangle).position);
        r.hit(10000);
        CHECK_NEAR(1e12 - r.w.get<Health>(target).current, 32805 + (copies - 1) * 26244, 1e-3);
    }
}

TEST(electric_web_uses_lightning_numbers_and_ruby_repeats) {
    CombatFeedback r;
    auto mob=r.mob({5000,5000});
    auto field=r.w.create(); r.w.add<Transform>(field,Transform{{5000,5000},0});
    GroundEffect effect; effect.kind=GroundEffectKind::ElectricWeb; effect.owner=r.player;
    effect.radius=100; effect.damagePerHit=10; effect.damageIntervalMillis=1000;
    r.w.add<GroundEffect>(field,effect);
    auto& mods=r.w.get<PlayerModifiers>(r.player); mods.lightningRepeatChance=1; mods.maxLightningRepeats=3;
    r.hit(1000);
    CHECK_NEAR(r.w.get<Health>(mob).current,1e12-40,1e-3);
    int numbers=0;
    for(const auto& event:r.events.events()) if(event.kind==net::EventKind::Damage && event.netId==77) {
        CHECK((event.flag&net::DamageLightning)!=0); ++numbers;
    }
    CHECK_EQ(numbers,4);
}

TEST(todo1106_zero_radius_electric_web_expires_and_multiple_slots_are_bounded) {
    CombatFeedback r;
    const auto field=r.w.create();r.w.add<Transform>(field,Transform{{5000,5000},0});
    r.w.add<GroundEffect>(field,GroundEffect{GroundEffectKind::ElectricWeb,r.player,0});
    r.w.add<Lifetime>(field,Lifetime{.1});
    r.hit(1000);r.hit(1050);r.hit(1100);
    CHECK(!r.w.isAlive(field)||r.w.has<Dead>(field));
    for(int i=0;i<5;++i)r.equip(i,"electric_web");
    r.w.get<PlayerInput>(r.player).current.flags=net::InputDefend;
    int peak=0;
    for(int tick=0;tick<800;++tick){
        r.petals.run(r.w,r.c,2000+tick*50,.05,r.commands);r.commands.flush();
        int live=0;Query<GroundEffect> fields{r.w};fields.without<Dead>();
        fields.each([&](Entity,GroundEffect& e){if(e.owner==r.player&&e.kind==GroundEffectKind::ElectricWeb)++live;});
        peak=std::max(peak,live);CHECK(live<=8);
    }
    CHECK(peak>0);
}

TEST(third_eye_extends_electric_web_throw_without_enlarging_its_field) {
    double before=0,after=0,baseRadius=0,extendedRadius=0;
    for(bool eye:{false,true}) {
        CombatFeedback r; r.equip(0,"electric_web"); if(eye)r.equip(1,"third_eye"); r.prepare();
        r.w.get<PlayerInput>(r.player).current.flags=net::InputAttack;
        r.petals.run(r.w,r.c,10000,.05,r.commands);r.commands.flush();
        Query<WebFlight> q{r.w}; int count=0;
        q.each([&](Entity,WebFlight& f){++count;if(eye){after=(f.to-f.from).length();extendedRadius=f.radius;}
            else{before=(f.to-f.from).length();baseRadius=f.radius;}});
        CHECK(count>0);
    }
    CHECK(after>before); CHECK_NEAR(baseRadius,extendedRadius,1e-9);
}

TEST(real_hammers_hit_wild_mobs_harder_and_keep_duel_damage) {
    struct HammerCase { const char* id; Rarity tier; double wildBase; double duelBase; double recoil; };
    for (const auto& test : {HammerCase{"timed_mjolnir", Rarity::Unique, 60.0, 12.0, 0.50},
                             {"mjolnir", Rarity::Unique, 60.0, 12.0, 0.50},
                             {"mjolnir", Rarity::Apex, 80.0, 60.0, 0.35}}) {
        for (const bool wild : {false, true}) {
            CombatFeedback r;
            r.w.get<Faction>(r.player).friendlyFireEnabled = true;
            r.equip(0, test.id, test.tier);
            r.prepare();
            Entity hammer = NULL_ENTITY;
            Query<PetalInstance>{r.w}.each([&](Entity e, PetalInstance& p) {
                if (p.slot == 0) hammer = e;
            });
            CHECK(hammer != NULL_ENTITY);
            if (hammer == NULL_ENTITY) continue;
            const auto target = r.mob(r.w.get<Transform>(hammer).position + Vec2{wild ? 0.0 : 1.0, 0.0});
            if (wild) {
                r.w.add<ContactDamage>(target, ContactDamage{10000.0, kMobHitIntervalMillis});
            } else {
                r.w.remove<MobTag>(target);
                r.w.add<PlayerTag>(target);
                r.w.get<Faction>(target) = Faction{Team::Players, true};
            }
            const double durability = r.w.get<Health>(hammer).current;
            r.hit(10000.0);
            CHECK_NEAR(1e12 - r.w.get<Health>(target).current,
                       (wild ? test.wildBase : test.duelBase) * petalStrengthScale(test.tier), 1e-5);
            if (wild) {
                CHECK_NEAR(durability - r.w.get<Health>(hammer).current, 10000.0 * test.recoil, 1e-7);
            }
        }
    }
}

TEST(real_mjolnir_contact_receives_the_equipped_ruby_bonus) {
    double baseline=0,boosted=0;
    for(bool ruby:{false,true}) {
        CombatFeedback r; r.equip(0,"mjolnir"); if(ruby)r.equip(1,"ruby"); r.prepare();
        Entity hammer=NULL_ENTITY; Query<PetalInstance> q{r.w};
        q.each([&](Entity e,PetalInstance& p){if(p.slot==0)hammer=e;});
        CHECK(hammer!=NULL_ENTITY); if(hammer==NULL_ENTITY)return;
        auto mob=r.mob(r.w.get<Transform>(hammer).position);
        for(int i=0;i<100;++i)r.hit(10000+i*50);
        const double dealt=1e12-r.w.get<Health>(mob).current;
        if(ruby)boosted=dealt;else baseline=dealt;
        CHECK(dealt>0);
        for(const auto& event:r.events.events()) if(event.kind==net::EventKind::Damage && event.netId==77)
            CHECK((event.flag&net::DamageLightning)!=0);
    }
    CHECK(boosted>baseline*1.2);
}

TEST(uranium_has_real_enemy_poison_and_bounded_self_poison_at_every_tier) {
    for(auto rarity:{Rarity::Common,Rarity::Unique,Rarity::Apex}) {
        CombatFeedback r; r.equip(0,"uranium",rarity);r.prepare();
        Entity cloud=NULL_ENTITY; Query<GroundEffect> q{r.w};
        q.each([&](Entity e,GroundEffect& f){if(f.kind==GroundEffectKind::Radiation)cloud=e;});
        CHECK(cloud!=NULL_ENTITY); if(cloud==NULL_ENTITY)continue;
        const auto f=r.w.get<GroundEffect>(cloud);
        CHECK(f.damagePerSecond>0);CHECK(f.ownerPoisonPerSecond>0);
        CHECK(f.ownerPoisonPerSecond<=r.w.get<Health>(r.player).max*.04);
        auto mob=r.mob(r.w.get<Transform>(cloud).position);
        const double before=r.w.get<Health>(r.player).current; r.hit(10000);r.hit(10050);
        CHECK(r.w.get<Health>(mob).current<1e12);
        CHECK(r.w.get<Health>(r.player).current<before);
        CHECK(r.w.get<Health>(r.player).current>before*.9);
    }
}

TEST(todo1106_uranium_reaches_new_outer_radius_without_hitting_beyond_it) {
    CombatFeedback r;r.equip(0,"uranium");r.prepare();
    Entity cloud=NULL_ENTITY;Query<GroundEffect> q{r.w};
    q.each([&](Entity e,GroundEffect& f){if(f.kind==GroundEffectKind::Radiation)cloud=e;});
    CHECK(cloud!=NULL_ENTITY);if(cloud==NULL_ENTITY)return;
    std::vector<Entity> older;
    q.each([&](Entity e,GroundEffect& f){if(f.kind==GroundEffectKind::Radiation&&e!=cloud)older.push_back(e);});
    for(const auto e:older)r.w.destroy(e);
    const auto centre=r.w.get<Transform>(cloud).position;
    CHECK_NEAR(r.w.get<GroundEffect>(cloud).radius,220,1e-6);
    const auto inside=r.mob(centre+Vec2{200,0}),outside=r.mob(centre+Vec2{250,0});
    r.hit(10000);r.hit(10050);
    CHECK(r.w.get<Health>(inside).current<1e12);
    CHECK_EQ(r.w.get<Health>(outside).current,1e12);
}

TEST(todo1106_hyper_contact_damage_exceeds_et_in_actual_combat) {
    for(const auto* id:{"rock","dust","sawblade","clover"}) {
        double dealt[2]={};
        for(int h=0;h<2;++h) {
            CombatFeedback r;r.equip(0,id,h?Rarity::Apex:Rarity::Unique);r.prepare();
            Entity petal=NULL_ENTITY;Query<PetalInstance> q{r.w};
            q.each([&](Entity e,PetalInstance& p){if(p.slot==0)petal=e;});
            CHECK(petal!=NULL_ENTITY);if(petal==NULL_ENTITY)continue;
            const auto mob=r.mob(r.w.get<Transform>(petal).position);
            r.hit(10000);dealt[h]=1e12-r.w.get<Health>(mob).current;
        }
        CHECK(dealt[0]>0);CHECK(dealt[1]>=dealt[0]*1.99);
    }
}

TEST(wild_mob_friendly_fire_is_refused_without_affecting_pets_or_players) {
    CombatFeedback r; auto a=r.mob({5000,5000}),b=r.mob({5000,5000});
    for(int mode=0;mode<3;++mode) {
        if(mode==1)r.w.get<Faction>(a).friendlyFireEnabled=true;
        if(mode==2)r.w.remove<Faction>(a);
        CHECK(r.combat.applyDamage(r.w,b,a,10,1000).refused);
        CHECK_NEAR(r.w.get<Health>(b).current,1e12,1e-3);
    }
    CHECK(!r.combat.applyDamage(r.w,r.player,a,10,1000).refused);
    r.w.add<Pet>(a,Pet{r.player}); r.w.add<Faction>(a,Faction{Team::Players,false});
    CHECK(!r.combat.applyDamage(r.w,b,a,10,1000).refused);
    auto seed=r.w.create();r.w.add<MobRingPetal>(seed,MobRingPetal{a});
    CHECK_EQ(CombatSystem::creditedPlayer(r.w,seed),r.player);
}

TEST(overlapping_wild_bodies_do_not_damage_each_other_over_repeated_combat_ticks) {
    CombatFeedback r;
    r.w.get<Transform>(r.player).position={8000,8000};
    auto a=r.mob({5000,5000}),b=r.mob({5000,5000});
    r.w.add<ContactDamage>(a,ContactDamage{100,50});
    r.w.add<ContactDamage>(b,ContactDamage{100,50});
    for(int i=0;i<100;++i)r.hit(1000+i*50);
    CHECK_NEAR(r.w.get<Health>(a).current,1e12,1e-3);
    CHECK_NEAR(r.w.get<Health>(b).current,1e12,1e-3);
    for(const auto& event:r.events.events())CHECK(event.kind!=net::EventKind::Damage);
}
