#pragma once
#include <array>
#include <functional>
#include "shared/game/components.h"
#include "shared/game/config.h"

namespace flix {
// Server-owned ring; these are independent bodies, never part of the hull's collider.
struct FlowerMobRing {
    std::array<Entity,10> petals{};
    std::array<double,10> readyAt{};
    std::array<double,10> reloadMillis{};
    double phase=0, cogPhase=0, attack=0;
    bool initialized=false;
};
struct FlowerMobPetal {
    double critChance=0, critMultiplier=1, damageMax=0;
};
inline bool isFlowerMob(const std::string& id) {
    return id=="gambler" || id=="mecha_flower";
}
void stepFlowerMobs(World&,const ContentRegistry&,double now,double dt,
                    const std::function<std::uint32_t()>& allocateNetId);
Vec2 flowerMobPursuit(const World&,Entity,const Vec2& offset,double bodyRadius,
                      double targetRadius,double speed,bool gambler);
}
FLIX_COMPONENT(flix::FlowerMobRing);
FLIX_COMPONENT(flix::FlowerMobPetal);
