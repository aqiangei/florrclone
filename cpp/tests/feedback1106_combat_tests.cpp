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
