#include "server/systems/flower_mobs.h"
#include "shared/game/constants.h"
#include "shared/net/protocol.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace flix {
Vec2 flowerMobPursuit(const World& world,Entity self,const Vec2& offset,double radius,
                      double targetRadius,double speed,bool gambler) {
    const double distance=offset.length(); if(distance<=0)return {};
    const auto* ring=world.tryGet<FlowerMobRing>(self);
    const double attack=ring ? ring->attack : 1;
    const double reach=radius+55+40*attack;
    const double radial=clamp((distance-reach-targetRadius*.4)/80.0,-1.0,gambler?2.0:1.0);
    const double side=(entityIndex(self)&1)?1.0:-1.0;
    const Vec2 direction=offset/distance;
    const Vec2 heading=direction*radial+Vec2{direction.y,-direction.x}*side;
    return heading*(speed/heading.length());
}

void stepFlowerMobs(World& world,const ContentRegistry& registry,double now,double dt,
                    const std::function<std::uint32_t()>& allocateNetId) {
    if(!(dt>0))return;
    Query<FlowerMobPetal,MobRingPetal> seeds{world};
    std::vector<Entity> orphaned;
    seeds.each([&](Entity e,FlowerMobPetal&,MobRingPetal& seed){
        if(!world.isAlive(seed.mob)||world.has<Dead>(seed.mob))orphaned.push_back(e);
    });
    for(const auto e:orphaned)world.destroy(e);
    Query<MobTag,MobType,Transform,Body,MobAi> mobs{world};
    std::vector<Entity> owners;
    mobs.each([&](Entity e,MobTag&,MobType& type,Transform&,Body&,MobAi&){
        if ((isFlowerMob(registry.mob(type.configIndex).id) || world.has<GhostTag>(e)) &&
            !world.has<Dead>(e) && !world.has<Pet>(e)) owners.push_back(e);
    });
    static constexpr const char* gambling[]={"card","dice","poker_chip","domino","clover"};
    for(const auto owner:owners) {
        const auto type=world.get<MobType>(owner);
        const bool gambler=registry.mob(type.configIndex).id=="gambler";
        const bool ghost=world.has<GhostTag>(owner);
        const auto at=world.get<Transform>(owner);
        const double radius=world.get<Body>(owner).radius;
        const auto ai=world.get<MobAi>(owner);
        auto ring=world.ensure<FlowerMobRing>(owner);
        if(!ring.initialized){ring.phase=entityIndex(owner)*2.399;ring.initialized=true;}
        const auto* target=world.tryGet<Transform>(ai.target);
        const auto* targetBody=world.tryGet<Body>(ai.target);
        const double distance=target?(target->position-at.position).length():1e30;
        const double targetRadius=targetBody?targetBody->radius:0;
        if(gambler){
            const double trigger=radius+95+targetRadius+9+(ring.attack>.5?12:0);
            const double wanted=target && distance<=trigger ? 1:0;
            ring.attack+=(wanted-ring.attack)*-std::expm1(-20*dt);
            ring.phase+=dt*1.8;
        } else if (ghost) {
            ring.phase += dt * 1.8;
        } else {
            ring.attack=target?1:0;
            const double phase=ring.cogPhase+1.8*dt;
            const double step=std::trunc(phase+1e-10);
            ring.cogPhase=phase-step;ring.phase+=step;
        }
        ring.phase=std::fmod(ring.phase,kTau);
        // The original Ghost sprite depicts five evenly spaced Basics around
        // its face. Orbit just past the edge of the ghost at every rarity.
        const double orbit=ghost ? radius + 18.0 : radius+55+40*ring.attack;
        const int count=ghost ? 5 : std::min(10,5+std::max(0,rarityIndex(type.rarity)));
        for(int i=0;i<count;++i){
            Entity petal=ring.petals[i];
            if(petal!=NULL_ENTITY && (!world.isAlive(petal)||world.has<Dead>(petal))){
                ring.petals[i]=NULL_ENTITY;petal=NULL_ENTITY;
                ring.readyAt[i]=now+ring.reloadMillis[i];
            }
            const double bearing=ring.phase+i*kTau/count;
            const Vec2 position=at.position+Vec2::fromAngle(bearing,orbit);
            if(petal==NULL_ENTITY && now>=ring.readyAt[i]){
                const auto index=registry.petalIndex(ghost ? "basic" : gambler ? gambling[i%5] : "cogwheel");
                if(index==kInvalidIndex)continue;
                const auto stats=registry.petalStats(index,type.rarity);
                petal=world.create();ring.petals[i]=petal;ring.reloadMillis[i]=stats.reloadMillis;
                world.add<MobRingPetal>(petal,MobRingPetal{owner,static_cast<std::size_t>(i),0});
                world.add<Transform>(petal,Transform{position,bearing,at.realm});
                world.add<Body>(petal,Body{9,1});
                world.add<Health>(petal,Health{stats.health,stats.health,0,0});
                world.add<Faction>(petal,world.get<Faction>(owner));
                world.add<ContactDamage>(petal,ContactDamage{stats.damage,0});
                world.add<HitCooldowns>(petal);world.add<Knockback>(petal);
                if(stats.petalEvasion>0)world.add<Evasion>(petal,Evasion{stats.petalEvasion});
                if(stats.petalPoisonImmune)world.add<PoisonImmune>(petal);
                FlowerMobPetal special;
                if(gambler && i%5==1){special.critChance=.05;special.critMultiplier=35;}
                if(gambler && i%5==3)special.damageMax=stats.damage*36;
                world.add<FlowerMobPetal>(petal,special);
                Replicated replicated;replicated.kind=net::EntityKind::Petal;
                replicated.typeIndex=index;replicated.rarity=type.rarity;
                replicated.spawnFlags=net::SpawnRingPetal;
                world.add<Replicated>(petal,replicated);
                if(allocateNetId)world.add<NetId>(petal,NetId{allocateNetId()});
            }
            if(petal!=NULL_ENTITY && world.isAlive(petal) && !world.has<Dead>(petal)){
                auto& transform=world.get<Transform>(petal);
                transform.position=position;transform.angle=bearing;transform.realm=at.realm;
            }
        }
        world.get<FlowerMobRing>(owner)=ring;
    }
}
}
