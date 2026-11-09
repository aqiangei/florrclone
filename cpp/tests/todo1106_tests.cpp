#include "test.h"
#include "server_harness.h"
#include "fixture_content.h"
#include "server/playtime_board.h"
#include "server/systems/petals.h"
#include "server/replication.h"
#include "shared/game/realm.h"
#include "server/systems/movement.h"
#include "server/systems/mob_ai.h"
#include "server/systems/npcs.h"
#include "server/systems/spawning.h"
#include "client/ui/menu_widgets.h"
#include <map>
#include "server/systems/combat.h"

using namespace flix;
using namespace flix::testsupport;

TEST(todo1106_geometric_armour_matches_bone_above_super) {
    const auto& c=shippedContent();
    double previous=0;
    for(const auto r:{Rarity::Super,Rarity::Unique,Rarity::Apex}){
        const auto armour=c.petalStats(c.petalIndex("icosahedron"),r).petalArmor;
        CHECK_NEAR(armour,c.petalStats(c.petalIndex("bone"),r).petalArmor,1e-6);
        CHECK(armour>previous);previous=armour;
    }
}

namespace {
Entity flower(World& w) {
    const auto p = w.create();
    w.add<PlayerTag>(p); w.add<Transform>(p, Transform{{5000,5000},0});
    w.add<Body>(p,Body{25,1}); w.add<Health>(p,Health{100,100});
    w.add<Faction>(p,Faction{Team::Players,false});
    w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<Loadout>(p); w.add<PlayerInput>(p);
    return p;
}
void wear(World& w, Entity p, int slot, const char* id, Rarity rarity) {
    auto& s = w.get<Loadout>(p).slots[slot]; s.configIndex = shippedContent().petalIndex(id); s.rarity=rarity;
}
}

TEST(todo1106_magic_eye_spends_half_mana_and_deals_lightning_with_inverted_defend) {
    const auto& c = shippedContent();
    World w;
    PetalSystem petals;
    CombatSystem combat;
    CommandBuffer commands{w};
    EventQueue events;
    const auto p = flower(w);
    wear(w, p, 0, "magic_eye", Rarity::Mythic);
    wear(w, p, 1, "mana_orb", Rarity::Mythic);
    wear(w, p, 2, "magic_eye", Rarity::Mythic);
    w.get<PlayerInput>(p).current.flags = net::InputAttack | net::InputDefend;
    const auto tick = [&](double now) {
        events.clear();
        petals.run(w, c, now, 0.05, commands, nullptr, &events);
        commands.flush();
        SpatialGrid grid;
        Query<Transform, Body>{w}.each([&](Entity e, Transform& at, Body& body) {
            grid.insert(e, at.realm, at.position, body.radius);
        });
        combat.run(w, grid, c, now, 0.05, commands, events);
        commands.flush();
    };
    const auto target = [&](double x, Team team) {
        const auto e = w.create();
        w.add<MobTag>(e);
        w.add<Transform>(e, Transform{{x, 5000}, 0.0});
        w.add<Body>(e, Body{30, 1});
        w.add<Health>(e, Health{100000, 100000});
        w.add<Faction>(e, Faction{team, false});
        w.add<Armor>(e, Armor{10000.0});
        return e;
    };
    const auto ally = target(5030, Team::Players);
    for (int i = 0; i < 120; ++i) tick(1000.0 + i * 50);
    CHECK(w.has<ManaPool>(p));
    const double before = w.get<ManaPool>(p).current;
    CHECK(before > 1.0);

    const auto first = target(5075, Team::Hostiles);
    const auto second = target(5305, Team::Hostiles);
    tick(7000.0);
    CHECK_NEAR(w.get<ManaPool>(p).current, before * 0.5, 1e-6);
    CHECK_NEAR(w.get<Health>(ally).current, 100000.0, 1e-6);
    const double remaining = 100000.0 - before * 0.5 * 25.0;
    for (const auto mob : {first, second}) {
        CHECK_NEAR(w.get<Health>(mob).current, remaining, 1e-6);
    }
    int strikes = 0;
    for (const auto& event : events.events()) {
        if (event.kind != net::EventKind::Lightning) continue;
        ++strikes;
        CHECK_EQ(event.flag & (net::LightningMagicEye | net::LightningChained),
                 net::LightningMagicEye | net::LightningChained);
        CHECK_NEAR(distance(event.position, w.get<Transform>(p).position), 0.0, 1e-8);
        CHECK_EQ(event.points.size(), 2u);
    }
    CHECK_EQ(strikes, 1);

    tick(7050.0);
    CHECK_NEAR(w.get<ManaPool>(p).current, before * 0.5, 1e-6);
    CHECK_NEAR(w.get<Health>(first).current, remaining, 1e-6);
}

TEST(todo1106_mana_orbs_stay_close_and_recharge_in_all_attack_modes) {
    const auto& content = shippedContent();
    for (const int flags : {0, int(net::InputAttack), int(net::InputDefend),
                            int(net::InputAttack | net::InputDefend)}) {
        World world;
        PetalSystem petals;
        CommandBuffer commands{world};
        const auto player = flower(world);
        wear(world, player, 0, "mana_orb", Rarity::Unique);
        world.get<PlayerInput>(player).current.flags = flags;
        const auto tick = [&](int first, int end) {
            for (int i = first; i < end; ++i) {
                petals.run(world, content, 1000 + i * 50, .05, commands);
                commands.flush();
            }
        };
        tick(0, 120);
        auto& pool = world.get<ManaPool>(player);
        pool.current = pool.max;
        tick(120, 240);

        const auto& spawned = world.get<Loadout>(player).spawned;
        CHECK_EQ(spawned.size(), 1u);
        if (spawned.size() != 1u) continue;
        const auto& origin = world.get<Transform>(player).position;
        const double radius = distance(origin, world.get<Transform>(spawned.front()).position);
        CHECK_NEAR(radius, 25.0 + kPetalOrbitDefendGap, 1.0);

        const auto& stats = content.petalStats(content.petalIndex("mana_orb"), Rarity::Unique);
        world.get<ManaPool>(player).current = 0.0;
        tick(240, 252);
        CHECK_NEAR(world.get<ManaPool>(player).current, stats.mana, 1e-6);
    }
}

TEST(wave_meteor_health_is_lower_only_for_ultra_and_super_placed_inside) {
    const auto& registry = shippedContent();
    const auto meteor = registry.petalIndex("meteor");
    for (const Rarity tier : {Rarity::Rare, Rarity::Ultra, Rarity::Super, Rarity::Unique, Rarity::Apex}) {
        for (const bool inWave : {false, true}) {
            World world;
            PetalSystem petals;
            CommandBuffer commands{world};
            const Entity player = flower(world);
            if (inWave) world.add<WavePlayer>(player);
            wear(world, player, 0, "meteor", tier);
            world.get<PlayerInput>(player).current.flags = net::InputDefend;
            Query<PlantedMeteor> planted{world};
            for (int tick = 0; tick < 300 && planted.count() == 0; ++tick) {
                petals.run(world, registry, 1000 + tick * 50, .05, commands);
                commands.flush();
            }
            const auto rocks = planted.collect();
            CHECK_EQ(rocks.size(), std::size_t(1));
            if (rocks.empty()) continue;
            const auto& health = world.get<Health>(rocks.front());
            const double scale = inWave && (tier == Rarity::Ultra || tier == Rarity::Super) ? 0.8 : 1.0;
            CHECK_NEAR(health.max, registry.petalStats(meteor, tier).health * scale, 1e-6);
            CHECK_NEAR(health.current, health.max, 1e-6);
            CHECK_NEAR(world.get<Body>(rocks.front()).radius, 180.0, 1e-9);
        }
    }
}

TEST(todo1106_meteor_plants_in_all_stances_and_hyper_last_two_seconds_longer) {
    const auto& content = shippedContent();
    for (const auto rarity : {Rarity::Common, Rarity::Apex}) {
        for (const int flags : {int(net::InputAttack), int(net::InputDefend),
                                int(net::InputAttack | net::InputDefend)}) {
            World world;
            PetalSystem petals;
            CommandBuffer commands{world};
            const auto player = flower(world);
            wear(world, player, 0, "meteor", rarity);
            world.get<PlayerInput>(player).current.flags = flags;

            Query<PlantedMeteor> planted{world};
            int tick = 0;
            const auto step = [&] {
                petals.run(world, content, 1000 + tick++ * 50, .05, commands);
                commands.flush();
            };
            while (tick < 300 && planted.count() == 0) step();
            const auto rocks = planted.collect();
            CHECK_EQ(rocks.size(), 1u);
            if (rocks.size() != 1u) continue;

            const auto rock = rocks.front();
            CHECK_EQ(world.get<PlantedMeteor>(rock).owner, player);
            const Vec2 origin = world.get<Transform>(player).position;
            const Vec2 plantedOffset = world.get<Transform>(rock).position - origin;
            Vec2 orbitOffset;
            Query<PetalInstance, Transform>{world}.each(
                [&](Entity, PetalInstance& ammo, Transform& at) {
                    if (ammo.owner == player && ammo.configIndex == content.petalIndex("meteor")) {
                        orbitOffset = at.position - origin;
                    }
                });
            CHECK(orbitOffset.length() > 1.0);
            CHECK_NEAR(wrapAngle(plantedOffset.angle() - orbitOffset.angle()), 0.0, 1e-6);
            CHECK_NEAR(plantedOffset.length(), std::max(orbitOffset.length() + 32.0, 162.0), 1e-6);
            CHECK_NEAR(world.get<Body>(rock).radius, 180.0, 1e-9);
            const double duration = rarity == Rarity::Apex ? 22.0 : 20.0;
            CHECK_NEAR(world.get<Lifetime>(rock).remainingSeconds, duration, 1e-9);
            world.get<PlayerInput>(player).current.flags = 0;
            for (int i = 0; i < 401; ++i) step();
            CHECK(world.isAlive(rock) == (rarity == Rarity::Apex));
            for (int i = 0; i < 40; ++i) step();
            CHECK(!world.isAlive(rock));
        }
    }
}

TEST(todo1106_mana_talent_scales_actual_passive_recovery_without_capacity_growth) {
    const auto& c=shippedContent(); double recovered[2]={}, capacity[2]={};
    for(int talented=0;talented<2;++talented){
        World w;PetalSystem petals;CommandBuffer commands{w};const auto p=flower(w);
        wear(w,p,0,"magic_leaf",Rarity::Unique);wear(w,p,1,"mana_orb",Rarity::Unique);
        auto& skills=w.add<PlayerSkillTree>(p).skills;
        if(talented)skills.set(SkillId::ManaRegen,8);
        for(int tick=0;tick<100;++tick){petals.run(w,c,1000+tick*50,.05,commands);commands.flush();}
        capacity[talented]=w.get<ManaPool>(p).max;
        w.get<ManaPool>(p).current=0;
        petals.run(w,c,6000,.05,commands);commands.flush();
        recovered[talented]=w.get<ManaPool>(p).current;
    }
    CHECK(recovered[0]>0);CHECK_NEAR(recovered[1],2*recovered[0],1e-6);
    CHECK_NEAR(capacity[0],capacity[1],1e-6);
}

TEST(todo1106_account_qq_is_private_saved_and_clearable_and_daily_claim_is_idempotent) {
    Harness h("todo1106-account", [](const std::string& path) {
        seedUser(path, "alice", "password7");
        seedUser(path, "bob", "password7");
    }, dataDir(), 0);
    CHECK(h.ready); if (!h.ready) return;
    NetClient alice, bob;
    CHECK(connectClient(h, alice)); CHECK(connectClient(h, bob));
    alice.requestLogin("alice", "password7"); bob.requestLogin("bob", "password7");
    CHECK(h.stepUntil({&alice, &bob}, [&] { return alice.haveSession() && bob.haveSession(); }));
    alice.requestAccountQQ(true, "qq-note-not-validated");
    CHECK(h.stepUntil({&alice, &bob}, [&] { return alice.accountQQRevision > 0; }));
    CHECK_EQ(alice.accountQQ, std::string("qq-note-not-validated"));
    CHECK_EQ(bob.accountQQRevision, 0u);
    bob.requestAccountQQ(false);
    CHECK(h.stepUntil({&alice, &bob}, [&] { return bob.accountQQRevision > 0; }));
    CHECK(bob.accountQQ.empty());
    const auto* user = h.server.database().findUser("alice"); CHECK(user != nullptr); if (!user) return;
    const auto id = user->id;
    CHECK_EQ(h.server.database().findProgress(id)->extra["qq"].asString(), alice.accountQQ);
    const double stars = h.server.database().findProgress(id)->stars;
    alice.requestDailyLoginClaim(); alice.requestDailyLoginClaim();
    h.step(10, {&alice, &bob});
    CHECK_NEAR(h.server.database().findProgress(id)->stars, stars, 1e-9);
    const auto revision = alice.accountQQRevision;
    alice.requestAccountQQ(true, "");
    CHECK(h.stepUntil({&alice, &bob}, [&] { return alice.accountQQRevision > revision; }));
    CHECK(h.server.database().findProgress(id)->extra["qq"].asString().empty());
}

TEST(todo1106_hyper_damage_and_mana_talent_are_distinct_from_utility_growth) {
    const auto& c = shippedContent();
    for (const auto* id : {"rock", "dust", "dice", "sawblade", "clover"}) {
        const auto index = c.petalIndex(id); CHECK(index != kNoPetal);
        const auto e = c.petalStats(index, Rarity::Unique), h = c.petalStats(index, Rarity::Apex);
        CHECK(h.damage >= e.damage * 2.0);
        CHECK(h.health <= e.health * 1.5 + 1e-6);
    }
    SkillSet skills;
    CHECK_EQ(skills.effectScale(SkillId::ManaRegen), 1.0);
    skills.set(SkillId::ManaRegen, 8);
    CHECK_EQ(skills.effectScale(SkillId::ManaRegen), 2.0);
    CHECK(c.mobStats(c.mobIndex("leech"), Rarity::Common).chaseSpeed >
          c.mobStats(c.mobIndex("soldier_ant"), Rarity::Common).chaseSpeed);
}

TEST(todo1106_bulk_craft_keeps_results_private) {
    Harness h("todo1106-bulk", [](const std::string& path) {
        seedUser(path,"crafter","password7"); seedUser(path,"reader","password7");
        seedStack(path,"crafter","petal_rose",Rarity::Ultra,10000);
        seedStack(path,"crafter","petal_rock",Rarity::Ultra,10000);
        seedStack(path,"crafter","petal_basic",Rarity::Ultra,10000);
        seedStack(path,"crafter","petal_leaf",Rarity::Ultra,10000);
        seedStack(path,"crafter","petal_stinger",Rarity::Ultra,10000);
        seedStack(path,"crafter","petal_magnet",Rarity::Ultra,5);
    },dataDir(),0);
    CHECK(h.ready); if(!h.ready)return;
    NetClient crafter, reader; CHECK(connectClient(h,crafter));CHECK(connectClient(h,reader));
    crafter.requestLogin("crafter","password7");reader.requestLogin("reader","password7");
    CHECK(h.stepUntil({&crafter,&reader},[&]{return crafter.haveSession()&&reader.haveSession();}));
    crafter.sendChat("global-before-craft");
    CHECK(h.stepUntil({&crafter,&reader},[&]{return sawText(reader,"global-before-craft");}));
    crafter.requestBulkCraft(Rarity::Super);
    CHECK(h.stepUntil({&crafter,&reader},[&]{return sawText(crafter,"一键合成完成");}));
    CHECK(!sawText(reader,"一键合成"));
    CHECK(!sawText(reader,"color: yellow;\">@crafter"));
    CHECK(!sawText(reader,"超级 玫瑰 已被"));
    CHECK(sawText(crafter,"超级 玫瑰 已被"));
    CHECK(sawText(crafter,"color: yellow;\">@crafter"));
    if (!crafter.profile().stackCount(content().petalIndex("magnet"),Rarity::Super))
        CHECK(!sawText(crafter,"超级 磁铁 已被"));
    CHECK(crafter.profile().stackCount(content().petalIndex("rose"),Rarity::Super)>0);
    crafter.requestNotifications(50,0);
    h.step(10,{&crafter,&reader});
    CHECK(crafter.notifications().empty());
    reader.sendChat("global-after-craft");
    CHECK(h.stepUntil({&crafter,&reader},[&]{return sawText(crafter,"global-after-craft");}));
}

TEST(todo1106_super_craft_keeps_results_private) {
    Harness h("quiet-super",[](const std::string& path){
        seedUser(path,"smith","password7");
        seedUser(path,"observer","password7");
        seedStack(path,"smith","petal_rose",Rarity::Ultra,20000);
    },dataDir(),0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient client, observer;
    CHECK(connectClient(h,client));CHECK(connectClient(h,observer));
    client.requestLogin("smith","password7");observer.requestLogin("observer","password7");
    CHECK(h.stepUntil({&client,&observer},[&]{return client.haveSession()&&observer.haveSession();}));
    client.requestCraft(content().petalIndex("rose"),Rarity::Ultra,20000);
    CHECK(h.stepUntil({&client,&observer},[&]{return client.craftOutcome().pending;},400));
    CHECK(client.craftOutcome().crafted>0);
    client.requestNotifications(50,0);
    CHECK(h.stepUntil({&client,&observer},[&]{return !client.notificationsPending();}));
    CHECK(sawText(client,"超级 玫瑰 已被"));
    CHECK(sawText(client,"color: yellow;\">@smith"));
    CHECK(sawText(client,"color: #00ff00;\">smith"));
    CHECK(!sawText(observer,"超级 玫瑰 已被"));
    CHECK(!sawText(observer,"color: yellow;\">@smith"));
    CHECK(client.notifications().empty());
}

TEST(todo1106_playtime_board_uses_recorded_activity_and_persists) {
    HarnessFiles files("todo1106-playtime");
    seedUser(files.dbPath,"active","password7");
    seedUser(files.dbPath,"idle","password7");
    seedUser(files.dbPath,"admin","password7",true);
    {
        Database db;std::string error;CHECK(db.load(files.dbPath,error));
        db.progress(db.findUser("active")->id).extra["councilActiveSeconds"]=7200;
        db.progress(db.findUser("idle")->id).extra["councilActiveSeconds"]=60;
        db.progress(db.findUser("admin")->id).extra["councilActiveSeconds"]=9000;
        db.markDirty();db.save();
    }
    Database db;std::string error;CHECK(db.load(files.dbPath,error));
    auto rows=playtimeBoard(db,false);CHECK_EQ(rows.size(),2u);
    CHECK_EQ(rows[0].name,std::string("active"));CHECK_EQ(rows[0].seconds,7200.0);
    rows=playtimeBoard(db,true);CHECK_EQ(rows.size(),3u);CHECK_EQ(rows[0].name,std::string("admin"));
}

TEST(todo1106_arena_respawn_uses_spawn_instead_of_corpse) {
    Harness h("todo1106-arena",{},dataDir(),0);
    CHECK(h.ready);if(!h.ready)return;
    NetClient client;CHECK(connectClient(h,client));client.requestRegister("gladiator","password9");
    CHECK(h.stepUntil({&client},[&]{return client.haveSession();}));
    client.joinGame(1280,720,kArenaSpawnChoice,"gladiator");
    CHECK(h.stepUntil({&client},[&]{return client.status()==NetClient::Status::Playing&&client.view().self().netId!=0;}));
    auto& world=h.server.world();const auto old=bodyNamed(world,"gladiator");
    CHECK(old!=NULL_ENTITY);if(old==NULL_ENTITY)return;
    world.get<Transform>(old).position=kArenaSpawn+Vec2{300,0};
    world.get<Health>(old).current=0;world.add<Dead>(old,Dead{NULL_ENTITY});
    CHECK(h.stepUntil({&client},[&]{return client.dead();}));
    client.requestRespawn();
    CHECK(h.stepUntil({&client},[&]{const auto e=bodyNamed(world,"gladiator");return e!=NULL_ENTITY&&e!=old;}));
    const auto reborn=bodyNamed(world,"gladiator");
    CHECK(reborn!=NULL_ENTITY);if(reborn==NULL_ENTITY)return;
    CHECK_NEAR(distance(world.get<Transform>(reborn).position,kArenaSpawn),0,1);
    CHECK(world.has<ManaPool>(reborn));
    if (world.has<ManaPool>(reborn)) CHECK_NEAR(world.get<ManaPool>(reborn).current, 0, 1e-9);
}

TEST(todo1106_square_hatches_without_waiting_an_hour_and_hyper_egg_really_is_hyper) {
    const auto& c = shippedContent();
    for (const auto* id : {"square", "egg"}) {
        World w; PetalSystem petals; CommandBuffer commands{w};
        const auto p = w.create();
        w.add<PlayerTag>(p); w.add<Transform>(p, Transform{{5000, 5000}, 0});
        w.add<Body>(p, Body{25, 1}); w.add<Health>(p, Health{100, 100});
        w.add<PlayerModifiers>(p); w.add<PetalRing>(p); w.add<PlayerInput>(p);
        auto& slot = w.add<Loadout>(p).slots[0];
        slot.configIndex = c.petalIndex(id); slot.rarity = Rarity::Apex;
        for (int tick = 0; tick < 100; ++tick) { petals.run(w, c, 1000 + tick * 50, .05, commands); commands.flush(); }
        int count = 0;
        Query<Pet, MobType> pets{w};
        pets.each([&](Entity, Pet& pet, MobType& type) {
            if (pet.owner != p) return;
            ++count;
            CHECK(type.rarity == Rarity::Apex);
        });
        CHECK(count > 0);
    }
}

TEST(todo1106_mecha_antennae_trade_vision_for_missile_aim_and_hyper_is_distinct) {
    const auto& c=shippedContent();const auto observer=c.petalIndex("observer"),antennae=c.petalIndex("antennae");
    for(int i=0;i<=rarityIndex(Rarity::Apex);++i){
        const auto r=static_cast<Rarity>(i);
        CHECK(c.petalStats(observer,r).cameraZoom>=c.petalStats(antennae,r).cameraZoom);
        CHECK(c.petalStats(observer,r).cameraZoom>0);
    }
    CHECK(c.petalStats(observer,Rarity::Apex).cameraZoom<c.petalStats(observer,Rarity::Unique).cameraZoom);
    for(bool enabled:{false,true}){
        World w;PetalSystem petals;CommandBuffer commands{w};const auto p=flower(w);
        w.add<PlayerVisuals>(p);wear(w,p,0,"mecha_missile",Rarity::Super);
        if(enabled)wear(w,p,1,"observer",Rarity::Super);
        for(int i=0;i<100;++i){petals.run(w,c,1000+i*50,.05,commands);commands.flush();}
        w.get<PlayerInput>(p).current.flags=net::InputAttack;
        petals.run(w,c,6000,.05,commands);commands.flush();int count=0;
        Query<Projectile> q{w};q.each([&](Entity,Projectile& shot){if(shot.owner!=p)return;++count;
            CHECK_EQ(shot.seekRange,enabled?1200.0:0.0);if(enabled)CHECK_NEAR(shot.seekCone,kPi,1e-9);
        });CHECK(count>0);
    }
}

TEST(todo1106_guided_projectiles_ignore_other_realms) {
    World w;Terrain terrain;MovementSystem movement;
    const auto shot=w.create();w.add<ProjectileTag>(shot);w.add<Transform>(shot,Transform{{5000,5000},0});
    w.add<Motion>(shot,Motion{{500,0}});w.add<Faction>(shot,Faction{Team::Players,false});
    Projectile p;p.remainingDistance=2000;p.seekRange=1200;p.seekCone=kPi;w.add<Projectile>(shot,p);
    const auto mob=w.create();w.add<MobTag>(mob);w.add<Transform>(mob,Transform{{5000,5100},0,Realm::Arena});
    w.add<Faction>(mob,Faction{Team::Hostiles,false});w.add<Health>(mob,Health{100,100});
    movement.run(w,terrain,1000,.05);
    CHECK_NEAR(w.get<Motion>(shot).velocity.y,0,1e-9);
}

TEST(todo1106_squad_code_and_invitation_coexist) {
    SquadRoster roster;Rng rng{42};const auto a=SquadMemberId::ofSession(1),b=SquadMemberId::ofSession(2),c=SquadMemberId::ofSession(3);
    auto* squad = roster.create(a, false, rng);
    auto* previous = roster.create(b, false, rng);
    CHECK(squad && previous);
    if (!squad || !previous) return;
    const std::string id = squad->id, previousId = previous->id;
    const std::string code = roster.issueCode(*squad, rng);
    CHECK_EQ(code.size(), 3u);
    CHECK(code.find_first_not_of("0123456789") == std::string::npos);
    CHECK(code != roster.issueCode(*previous, rng));
    SquadRoster::Departure left;
    CHECK(!roster.joinPublic(id,b).empty());
    CHECK(!roster.joinWithCode("wrong", b, left).empty());
    CHECK_EQ(roster.forMember(b)->id, previousId);
    CHECK(roster.joinWithCode(code, b, left).empty());
    CHECK(left.disbanded);
    CHECK_EQ(left.squadId, previousId);
    CHECK_EQ(roster.forMember(b)->id, id);
    CHECK(roster.invite(a,c,1000).empty());
    std::string accepted;
    CHECK(roster.accept(c,1001,accepted).empty());
    CHECK_EQ(accepted,id);
    CHECK(roster.joinWithCode(code,SquadMemberId::ofSession(4),left).empty());
    const auto extra = SquadMemberId::ofSession(5);
    CHECK(roster.create(extra, false, rng));
    CHECK(!roster.joinWithCode(code,extra,left).empty());
    CHECK(!left.wasMember);
    CHECK(roster.forMember(extra)->id != id);
}

TEST(todo1106_private_squad_code_joins_through_real_chat_protocol) {
    Harness h("todo1106-squad-code",{},dataDir(),0);CHECK(h.ready);if(!h.ready)return;
    NetClient a,b;CHECK(connectClient(h,a));CHECK(connectClient(h,b));
    a.requestRegister("leader","password9");b.requestRegister("member","password9");
    CHECK(h.stepUntil({&a,&b},[&]{return a.haveSession()&&b.haveSession();}));
    a.joinGame(1280,720,"garden","leader");b.joinGame(1280,720,"garden","member");
    CHECK(h.stepUntil({&a,&b},[&]{return a.status()==NetClient::Status::Playing&&b.status()==NetClient::Status::Playing;}));
    b.sendChat("/squad-create");
    CHECK(h.stepUntil({&a,&b},[&]{return b.squad().inSquad;}));
    const std::string oldId = b.squad().id;
    CHECK_EQ(b.squad().joinCode.size(), 3u);
    CHECK(b.squad().joinCode.find_first_not_of("0123456789") == std::string::npos);
    b.sendChat("/squad-code " + oldId.substr(6));
    CHECK(h.stepUntil({&a,&b},[&]{return sawText(b,"请输入三位数字组队码");}));
    CHECK_EQ(b.squad().id, oldId);
    a.sendChat("/squad-code");
    CHECK(h.stepUntil({&a,&b},[&]{return sawText(a,"组队码：");}));
    std::string code;
    const std::string prefix = "组队码：";
    for (const auto& line : a.chat()) {
        const auto position = line.text.find(prefix);
        if (position != std::string::npos) code = line.text.substr(position + prefix.size(), 3);
    }
    CHECK_EQ(code.size(), 3u);
    CHECK_EQ(a.squad().joinCode, code);
    b.sendChat("/squad-code " + code);
    CHECK(h.stepUntil({&a,&b},[&]{return b.squad().inSquad&&a.squad().members.size()==2;}));
    CHECK_EQ(a.squad().id,b.squad().id);CHECK(!a.squad().isPublic);
    CHECK_EQ(b.squad().joinCode, code);
    CHECK(b.squad().id != oldId);
}

TEST(todo1106_initial_mana_is_partial_and_reequipping_does_not_refill) {
    World w; PetalSystem petals; CommandBuffer commands{w}; const auto& c=shippedContent();
    const auto p=flower(w); wear(w,p,0,"mana_orb",Rarity::Unique);
    petals.run(w,c,1000,0,commands);commands.flush();
    CHECK(w.has<ManaPool>(p));const auto max=w.get<ManaPool>(p).max;CHECK(max>0);
    CHECK_NEAR(w.get<ManaPool>(p).current,max*.4,1e-6);
    w.get<Loadout>(p).slots[0].configIndex=kNoPetal;
    petals.run(w,c,1050,0,commands);commands.flush();
    CHECK_NEAR(w.get<ManaPool>(p).current,0,1e-6);
    wear(w,p,0,"mana_orb",Rarity::Unique);
    petals.run(w,c,1100,0,commands);commands.flush();
    CHECK_NEAR(w.get<ManaPool>(p).current,0,1e-6);
    const auto respawn=flower(w);wear(w,respawn,0,"mana_orb",Rarity::Unique);
    w.add<ManaPool>(respawn);
    petals.run(w,c,1150,0,commands);commands.flush();
    CHECK_NEAR(w.get<ManaPool>(respawn).current,0,1e-6);
}

TEST(todo1106_spaceship_hatches_actual_hyper_with_two_pet_cap_and_modest_stats) {
    const auto& c=shippedContent();double damages[2]={},radii[2]={};
    for(int high=0;high<2;++high){
        World w;PetalSystem petals;CommandBuffer commands{w};const auto p=flower(w);
        const auto rarity=high?Rarity::Apex:Rarity::Unique;wear(w,p,0,"spaceship",rarity);
        petals.run(w,c,1000,0,commands);commands.flush();
        ShotLanding landing;landing.owner=p;landing.slot=0;landing.petalIndex=c.petalIndex("spaceship");
        landing.rarity=rarity;landing.at={5100,5000};
        for(int n=0;n<5;++n)petals.landShots(w,c,{landing},nullptr);
        int count=0;Query<Pet,MobType,Body,ContactDamage> pets{w};
        pets.each([&](Entity,Pet& pet,MobType& type,Body& body,ContactDamage& damage){
            if(pet.owner!=p)return;++count;CHECK(type.rarity==rarity);
            damages[high]=damage.amount;radii[high]=body.radius;
        });CHECK_EQ(count,2);
    }
    CHECK_NEAR(damages[1]/damages[0],1.1,1e-6);CHECK(radii[1]>radii[0]);
    CHECK(c.petalStats(c.petalIndex("spaceship"),Rarity::Apex).reloadMillis>=6500);
}

TEST(todo1106_guild_transfer_requires_current_leader_and_preserves_members) {
    Harness h("todo1106-guild-transfer",[](const std::string& path){
        seedUser(path,"leader","password7");seedUser(path,"member","password7");
    });CHECK(h.ready);if(!h.ready)return;
    NetClient boss,member;CHECK(loginAs(h,boss,"leader","password7"));CHECK(loginAs(h,member,"member","password7"));
    boss.requestGuildCreate("ALPHA");CHECK(h.stepUntil({&boss,&member},[&]{return boss.guild().joined;}));
    boss.requestGuildInvite("member");CHECK(h.stepUntil({&boss,&member},[&]{return member.guildInvite().waiting;}));
    member.requestGuildAccept();CHECK(h.stepUntil({&boss,&member},[&]{return member.guild().joined;}));
    member.sendChat("/guild-transfer member");h.step(10,{&boss,&member});CHECK_EQ(boss.guild().leader,std::string("leader"));
    boss.sendChat("/guild-transfer nobody");h.step(10,{&boss,&member});CHECK_EQ(boss.guild().leader,std::string("leader"));
    boss.sendChat("/guild-transfer member");CHECK(h.stepUntil({&boss,&member},[&]{return boss.guild().leader=="member"&&member.guild().leader=="member";}));
    CHECK_EQ(boss.guild().members.size(),2u);
    boss.sendChat("/guild-transfer leader");h.step(10,{&boss,&member});CHECK_EQ(boss.guild().leader,std::string("member"));
}

TEST(todo1106_portals_resolve_and_sewer_exit_matches_pipe) {
    WorldMaps maps;Terrain terrain;std::string error;
    CHECK(maps.load(dataDir(),&terrain,error));Rng rng{123};int count=0,sewers=0;
    for (std::size_t i = 0; i < maps.maps().size(); ++i) {
        const Realm realm = worldRealm(static_cast<int>(i));
        const auto extent = terrain.realmExtent(realm);
        CHECK_NEAR(extent.x, terrain.tileCols(realm) * 512.0, 1e-6);
        CHECK_NEAR(extent.y, terrain.tileRows(realm) * 512.0, 1e-6);
    }
    CHECK_NEAR(terrain.realmExtent(Realm::Arena).x, 12000.0, 1e-6);
    CHECK_NEAR(kMazeCellSize, 2000.0, 1e-6);
    CHECK(distance(kArenaSpawn, kArenaCentre) <= kArenaRadius);
    for(const auto& map:maps.maps()) for(const auto& pad:map.elements()) {
        if(pad.kind!=MapElementKind::Teleporter)continue;
        ++count;WorldMaps::Destination destination;
        CHECK(maps.resolveTeleporter(pad,rng,terrain,destination));
        CHECK(!terrain.blocked(destination.position,destination.realm));
        if(map.id()=="sewers"||map.id()=="prestige_sewers") {
            ++sewers;CHECK_NEAR(pad.centre().x,6144 * kWorldMapScale,0.01);CHECK_NEAR(pad.centre().y,22135.42 * kWorldMapScale,0.01);
        }
        std::printf("PORTAL %s %.2f %.2f -> %s resolved\n",map.id().c_str(),pad.centre().x,pad.centre().y,pad.targetMap.c_str());
    }
    CHECK(count>20);CHECK_EQ(sewers,2);
}

TEST(todo1106_cyberspace_javascript_rooms_open_to_the_main_route) {
    WorldMaps maps;
    Terrain terrain;
    std::string error;
    CHECK(maps.load(dataDir(), &terrain, error));
    bool found = false;
    const Realm realm = maps.realmOfId("cyberspace", found);
    CHECK(found);
    if (!found) return;
    struct Passage { int x, road, room; };
    const Passage passages[] = {{17, 66, 70}, {19, 66, 69}, {25, 66, 69},
                                {18, 75, 72}, {25, 75, 72}};
    for (const auto& [x, road, room] : passages) {
        const Vec2 from = Terrain::tileCenter(x, road);
        const Vec2 to = Terrain::tileCenter(x, room);
        CHECK(!terrain.segmentBlocked(from, to, realm));
        for (int y = std::min(road, room); y <= std::max(road, room); ++y)
            CHECK(!terrain.resolveWall(Terrain::tileCenter(x, y), kPlayerBaseRadius, realm).collided);
    }
}

TEST(todo1106_projectiles_use_summon_projectile_talent) {
    const auto& c=shippedContent();
    for(const auto* id:{"peas","magic_peas","grapes"}) {
        const auto& spec = c.petal(c.petalIndex(id)).projectile;
        CHECK_NEAR(spec.speed, 640, 1e-6);
        CHECK_NEAR(spec.distance / spec.speed, 1000.0 / 640.0, 1e-6);
        const bool blueberries = std::string_view(id) == "magic_peas";
        const int reload = std::string_view(id) == "peas" ? 3500 :
            std::string_view(id) == "grapes" ? 2250 : 250;
        for (const auto rarity : {Rarity::Common, Rarity::Ultra, Rarity::Apex})
            CHECK_NEAR(c.petalStats(c.petalIndex(id), rarity).reloadMillis, reload, 1e-6);
        const auto shotTier = blueberries ? Rarity::Apex : Rarity::Common;
        if (blueberries) {
            const auto index = c.petalIndex(id);
            CHECK_NEAR(c.petalStats(index, Rarity::Super).lightningDamagePerHit,
                       120.0 * petalStrengthScale(Rarity::Super), 1e-6);
            CHECK_NEAR(c.petalStats(index, Rarity::Unique).lightningDamagePerHit, 383818.5, 1e-6);
        }
        double damage[3]={};
        for(int variant=0;variant<3;++variant){
            World w;PetalSystem petals;CommandBuffer commands{w};const auto p=flower(w);
            wear(w,p,0,id,shotTier);wear(w,p,1,"mana_orb",Rarity::Apex);auto& skills=w.add<PlayerSkillTree>(p).skills;
            if(variant==1)skills.set(SkillId::PetDamage,9);
            if(variant==2)skills.set(SkillId::Damage,9);
            w.get<PlayerInput>(p).current.flags=net::InputAttack;
            for(int tick=0;tick<140&&damage[variant]==0;++tick){
                petals.run(w,c,1000+tick*50,.05,commands);commands.flush();
                Query<ProjectileTag,Projectile> shots{w};shots.each([&](Entity,ProjectileTag&,Projectile& shot){damage[variant]=shot.damage;});
            }
            std::printf("PROJECTILE %s variant %d damage %.3f\n",id,variant,damage[variant]);CHECK(damage[variant]>0);
        }
        if (blueberries) CHECK_NEAR(damage[0], 767637.0, 1e-6);
        CHECK_NEAR(damage[1],2.5*damage[0],1e-6);
        CHECK_NEAR(damage[2],damage[0],1e-6);
    }
}
TEST(todo1106_factory_machines_cover_common_through_eternal) {
    const auto& c=shippedContent();WorldMaps maps;Terrain terrain;std::string error;CHECK(maps.load(dataDir(),&terrain,error));
    NpcSystem npcs;std::vector<std::string> warnings;npcs.loadSites(maps,c,warnings);World w;npcs.placeMissing(w,terrain,c,0);
    for(const auto* id:{"computer","prestige_computer"}){
        bool found=false;const auto realm=maps.realmOfId(id,found);CHECK(found);std::array<int,kRarityCount> tiers{};
        for(const auto& site:npcs.sites())if(site.realm==realm&&c.mob(site.mobIndex).npc.service==NpcService::Assembler){
            CHECK(w.isAlive(site.entity));++tiers[rarityIndex(site.rarity)];
            if(w.isAlive(site.entity))CHECK(!terrain.blocked(w.get<Transform>(site.entity).position,realm));
        }
        for(int tier=0;tier<kRarityCount;++tier)CHECK_EQ(tiers[tier],tier<=rarityIndex(Rarity::Unique)?1:0);
    }
}
TEST(todo1106_conversion_cycles_cannot_create_material_value) {
    const auto& c=shippedContent();
    const std::map<std::string,double> value={{"glass",1},{"sand",1},{"air",1},{"bubble",1},
        {"moon_rock",20},{"rock",1},{"honey",1},{"wax",3},{"rose",1},{"light",1},
        {"dahlia",2},{"battery",2},{"lightning",4},{"pollen",2},{"triangle",1},
        {"cutter",10},{"mimic",20},{"ram",1},{"sawblade",21},{"square",1},
        {"pentagon",2},{"cogwheel",1},{"fragment",1}};
    int checked=0;
    for(const char* id:{"assembler","cyber_assembler","super_assembler"})
        for(const auto& recipe:c.mob(c.mobIndex(id)).npc.recipes){
            double input=0,output=0;
            for(const auto& row:recipe.inputs)input+=value.at(c.petal(row.petal).id)*row.count;
            for(const auto& row:recipe.outputs)output+=value.at(c.petal(row.petal).id)*row.count;
            CHECK(!recipe.outputRarity.has_value());CHECK(output<=input);++checked;
        }
    CHECK(checked>=26);
}

TEST(todo1106_roach_sprint_slows_as_health_falls) {
    const auto& c=shippedContent();double speed[3]={};
    for(int stage=0;stage<3;++stage){
        World w;Terrain terrain;SpatialGrid grid;CommandBuffer commands{w};MobAiSystem ai(w);
        const auto p=flower(w);w.get<Transform>(p).position={5300,5000};w.add<Motion>(p);
        const auto m=w.create();const auto index=c.mobIndex("roach");const auto stats=c.mobStats(index,Rarity::Common);
        w.add<MobTag>(m);w.add<Transform>(m,Transform{{5000,5000},0});w.add<Motion>(m);
        w.add<Body>(m,Body{stats.radius,stats.mass});w.add<Health>(m,Health{stats.health*(1.0-stage*.4),stats.health});
        w.add<MobType>(m,MobType{index,Rarity::Common,1});w.add<Faction>(m,Faction{Team::Hostiles,false});
        auto& brain=w.add<MobAi>(m);brain.kind=AiKind::Neutral;brain.aggroRange=1000;brain.target=p;
        w.add<Bounty>(m).credit(p,1);
        grid.insert(p,Realm::Overworld,{5300,5000},25);grid.insert(m,Realm::Overworld,{5000,5000},stats.radius);
        for(int tick=0;tick<40;++tick){ai.run(w,terrain,grid,{{{5300,5000},Realm::Overworld}},10000+tick*50,.05,commands);commands.flush();}
        speed[stage]=w.get<Motion>(m).velocity.length();CHECK(speed[stage]>0);
    }
    CHECK(speed[0]>speed[1]);CHECK(speed[1]>speed[2]);CHECK(speed[2]>=speed[0]*.5);
}

TEST(todo1106_proximity_index_preserves_exact_activation_boundaries) {
    Rng rng{191};PlayerProximityIndex index(kMobActiveRadius);
    for(const int count:{0,1,15,16,150}){
        std::vector<RealmPoint> players;
        for(int i=0;i<count;++i)players.push_back({{rng.range(-100,65000),rng.range(-100,65000)},i%2?Realm::Arena:Realm::Overworld});
        index.rebuild(players);
        for(int n=0;n<2000;++n){
            const Vec2 at{rng.range(-100,65000),rng.range(-100,65000)};const auto realm=n%2?Realm::Arena:Realm::Overworld;
            bool expected=players.empty();for(const auto& p:players)if(p.realm==realm&&distanceSq(p.position,at)<=kMobActiveRadius*kMobActiveRadius)expected=true;
            CHECK_EQ(index.near(at,realm),expected);
        }
    }
    std::vector<RealmPoint> players(16,{{10000,10000},Realm::Overworld});index.rebuild(players);
    CHECK(index.near({10000+kMobActiveRadius,10000},Realm::Overworld));
    CHECK(!index.near({10000+kMobActiveRadius+.01,10000},Realm::Overworld));
    CHECK(!index.near({10000,10000},Realm::Arena));
}

TEST(todo1107_berry_ranges_and_blueberry_tooltip_use_current_stats) {
    const auto& c = shippedContent();
    for (const auto* id : {"peas", "grapes", "magic_peas"}) {
        const auto& petal = c.petal(c.petalIndex(id));
        CHECK_NEAR(petal.projectile.distance, 1000, 1e-6);
    }
    std::string error;
    CHECK(loadContent(testsupport::dataDir(), error));
    const auto blueberry = content().petalIndex("magic_peas");
    const auto base = c.petalStats(blueberry, Rarity::Unique).lightningDamagePerHit;
    CHECK(base > 0);
    SkillSet skills;
    skills.set(SkillId::Damage, 2);
    const auto damageText = std::to_string(std::llround(base * skills.effectScale(SkillId::Damage)));
    const auto lines = ui::petalTooltipLines(blueberry, Rarity::Unique, skills, 1.0);
    CHECK(std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
        auto actual = line.altText;
        actual.erase(std::remove(actual.begin(), actual.end(), ','), actual.end());
        return actual.find(damageText) != std::string::npos;
    }));
}

TEST(todo1107_map_scale_and_density_use_original_physical_units) {
    CHECK_NEAR(kWorldMapScale, 1.0, 1e-9);
    CHECK_NEAR(kTileSize, 256, 1e-9);
    CHECK_NEAR(kTargetMobDensity * 60000 * 60000, 9000, 1e-9);
    CHECK_EQ(kMaxLiveMobs, 3000);
}

TEST(jungle_ladybug_acquires_players_and_a_bulb_extends_its_range) {
    const auto& c = shippedContent();
    const auto index = c.mobIndex("jungle_ladybug");
    CHECK(index != kInvalidIndex);
    const auto stats = c.mobStats(index, Rarity::Rare);
    CHECK(stats.ai == AiKind::Hostile);
    for (int scenario = 0; scenario < 3; ++scenario) {
        const bool lit = scenario == 2;
        const double distanceFactor = scenario == 0 ? 1.0 : 1.55;
        World world;
        Terrain terrain;
        SpatialGrid grid;
        CommandBuffer commands{world};
        PetalSystem petals;
        MobAiSystem ai(world);
        const auto player = flower(world);
        if (lit) {
            wear(world, player, 0, "bulb", Rarity::Common);
            world.get<PlayerInput>(player).current.flags = net::InputDefend;
            for (int tick = 0; tick < 120; ++tick) {
                petals.run(world, c, 1000.0 + tick * 50, 0.05, commands);
                commands.flush();
            }
            CHECK(world.get<PlayerModifiers>(player).aggroRangeScale > 1.0);
        }

        const auto observer = world.get<Transform>(player);
        const auto mob = world.create();
        world.add<MobTag>(mob);
        world.add<MobType>(mob, MobType{index, Rarity::Rare, 1});
        world.add<Transform>(mob, Transform{observer.position - Vec2{stats.aggroRange * distanceFactor, 0}, 0});
        world.add<Motion>(mob);
        world.add<Health>(mob, Health{stats.health, stats.health});
        world.add<Body>(mob, Body{stats.radius, stats.mass});
        world.add<Faction>(mob, Faction{Team::Hostiles, false});
        auto& brain = world.add<MobAi>(mob);
        brain.kind = stats.ai;
        brain.aggroRange = stats.aggroRange;
        grid.insert(player, observer.realm, observer.position, 25);

        ai.run(world, terrain, grid, {{observer.position, observer.realm}}, 7000, .05, commands);
        commands.flush();
        CHECK_EQ(world.get<MobAi>(mob).target, scenario == 1 ? NULL_ENTITY : player);
    }
}

TEST(todo1107_hostile_acquires_earlier_and_closes_the_gap) {
    const auto& c = shippedContent();
    World world;
    Terrain terrain;
    SpatialGrid grid;
    CommandBuffer commands{world};
    MobAiSystem ai(world);
    const auto player = flower(world);
    const auto mob = world.create();
    const auto index = c.mobIndex("soldier_ant");
    const auto stats = c.mobStats(index, Rarity::Common);
    const Vec2 at = world.get<Transform>(player).position - Vec2{stats.aggroRange * 1.22, 0};
    world.add<MobTag>(mob);
    world.add<MobType>(mob, MobType{index, Rarity::Common, 1});
    world.add<Transform>(mob, Transform{at, 0});
    world.add<Motion>(mob);
    world.add<Health>(mob, Health{stats.health, stats.health});
    world.add<Body>(mob, Body{stats.radius, stats.mass});
    world.add<Faction>(mob, Faction{Team::Hostiles, false});
    auto& brain = world.add<MobAi>(mob);
    brain.kind = AiKind::Hostile;
    brain.aggroRange = stats.aggroRange;

    const auto observer = world.get<Transform>(player);
    grid.insert(player, observer.realm, observer.position, 25);
    for (int i = 0; i < 90; ++i) {
        ai.run(world, terrain, grid, {{observer.position, observer.realm}}, 1000 + i * 50, .05, commands);
        commands.flush();
        if (i == 0) CHECK_EQ(world.get<MobAi>(mob).target, player);
    }
    CHECK_EQ(world.get<MobAi>(mob).target, player);
    CHECK(world.get<Motion>(mob).velocity.x > stats.chaseSpeed * 1.15);
}

TEST(todo1106_idle_mob_sleeps_until_observed_or_hurt) {
    std::string error;
    const bool loaded = loadContent(testsupport::dataDir(), error);
    CHECK(loaded);
    if (!loaded) return;
    World world;
    Terrain terrain;
    SpatialGrid grid;
    CommandBuffer commands{world};
    MobAiSystem ai(world);
    MovementSystem movement;
    const auto player = flower(world);
    world.add<PlayerLocation>(player);
    const auto mob = world.create();
    world.add<MobTag>(mob);
    const Vec2 home{25000, 5000};
    world.add<Transform>(mob, Transform{home, 0});
    world.add<Motion>(mob).velocity = {100, 0};
    world.add<Body>(mob, Body{30, 1});
    world.add<Health>(mob, Health{100, 100});
    world.add<MobType>(mob, MobType{shippedContent().mobIndex("roach"), Rarity::Common, 1});
    world.add<Faction>(mob, Faction{Team::Hostiles, false});
    world.add<MobAi>(mob);
    const auto step = [&](double now) {
        const auto& at = world.get<Transform>(player);
        ai.run(world, terrain, grid, {{at.position, at.realm}}, now, .05, commands);
        commands.flush();
        movement.runWorldPhase(world, terrain, now, .05);
    };
    step(1000);
    CHECK(world.get<MobAi>(mob).sleeping);
    CHECK_NEAR(distance(home, world.get<Transform>(mob).position), 0, 1e-9);
    world.get<PlayerLocation>(player).viewport = {42000, 1080};
    step(1050);
    CHECK(!world.get<MobAi>(mob).sleeping);
    world.get<PlayerLocation>(player).viewport = {1920, 1080};
    world.get<Health>(mob).current = 99;
    step(1100);
    CHECK(!world.get<MobAi>(mob).sleeping);
    world.get<Health>(mob).current = 100;
    world.get<Transform>(player).position = home + Vec2{200, 0};
    step(1150);
    CHECK(!world.get<MobAi>(mob).sleeping);

    world.get<Transform>(mob).position = home;
    world.get<Transform>(player).position = home - Vec2{7000, 0};
    world.get<MobAi>(mob).kind = AiKind::Passive;
    step(1200);
    CHECK(world.get<MobAi>(mob).sleeping);
    CHECK_NEAR(distance(home, world.get<Transform>(mob).position), 0, 1e-9);
    world.get<PlayerLocation>(player).viewport = {14000, 1080};
    step(1250);
    CHECK(!world.get<MobAi>(mob).sleeping);

    world.get<PlayerLocation>(player).viewport = {1920, 1080};
    world.get<Transform>(mob).position = home;
    world.get<MobAi>(mob).kind = AiKind::Neutral;
    world.get<MobAi>(mob).aggroRange = 6500;
    const auto remote = flower(world);
    world.get<Transform>(remote).realm = Realm::Arena;
    world.get<PlayerModifiers>(remote).aggroRadiusBonus = 1000;
    grid.insert(player, Realm::Overworld, world.get<Transform>(player).position, 25);
    step(1300);
    CHECK(world.get<MobAi>(mob).sleeping);

    world.get<PlayerModifiers>(player).aggroRadiusBonus = 1000;
    step(1350);
    CHECK(!world.get<MobAi>(mob).sleeping);
    CHECK_EQ(world.get<MobAi>(mob).target, player);
}

TEST(todo1106_loadout_presets_restore_independent_talents) {
    Harness h("preset-skills-1106", [](const std::string& path) {
        seedUser(path, "keeper", "password7");
        Database db;
        std::string error;
        db.load(path, error);
        const auto* account = db.findUser("keeper");
        if (!account) return;
        auto& record = db.progress(account->id);
        record.talentPointBonus = 100;
        record.skills.set(SkillId::Damage, 0);
        db.markDirty();
        db.save();
    }, dataDir(), 0);
    CHECK(h.ready);
    if (!h.ready) return;
    NetClient client;
    CHECK(connectClient(h, client));
    client.requestLogin("keeper", "password7");
    CHECK(h.stepUntil({&client}, [&] { return client.profile().skills.level(SkillId::Damage) == 0; }));
    client.saveLoadoutPreset(0);
    CHECK(h.stepUntil({&client}, [&] { return !client.profile().presets[0].empty(); }));
    client.requestResetSkills();
    CHECK(h.stepUntil({&client}, [&] { return client.profile().skills.level(SkillId::Damage) < 0; }));
    client.saveLoadoutPreset(10);
    CHECK(h.stepUntil({&client}, [&] { return !client.profile().presets[10].empty(); }));
    client.loadLoadoutPreset(0);
    CHECK(h.stepUntil({&client}, [&] { return client.profile().skills.level(SkillId::Damage) == 0; }));
    client.loadLoadoutPreset(10);
    CHECK(h.stepUntil({&client}, [&] { return client.profile().skills.level(SkillId::Damage) < 0; }));
}
