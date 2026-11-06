#pragma once
#include "shared/game/config.h"
#include "shared/core/world.h"
#include <algorithm>
namespace flix {
// Functional parts, not copies: software, memory, bus, power, controller,
// geometry. Raw slots are counted so Mimic cannot create fictitious hardware.
inline unsigned computerPart(const std::string& id) {
 if(id=="javascript"||id=="c++")return 1;
 if(id=="ram")return 2;
 if(id=="computer_chip")return 4;
 if(id=="capacitor")return 8;
 if(id=="cursor")return 16;
 if(id=="cube"||id=="icosahedron"||id=="pentagon")return 32;
 return 0;
}
inline bool computerWeapon(const std::string& id) {
 return computerPart(id)!=0||id=="spaceship"||id=="glitch"||id=="bomb";
}
inline int computerPartCount(unsigned mask){int n=0;for(;mask;mask>>=1)n+=mask&1;return n;}
inline void applyComputerSet(const PetalConfig& config, unsigned mask, PetalStats& stats){
 if(!computerWeapon(config.id))return;
 const int n=computerPartCount(mask);
 if(n>=3){if(stats.damage>0)stats.damage*=1.12;stats.lightningDamagePerHit*=1.12;}
 if(n>=4&&std::isfinite(stats.health)&&stats.health>1)stats.health*=1.15;
 if(n>=5)stats.reloadMillis*=.9;
}
inline PetalStats computerSetStats(const ContentRegistry& registry, World& world, Entity owner,
 std::uint16_t index, Rarity rarity, PetalStats stats){
 if(const auto* mods=world.tryGet<PlayerModifiers>(owner))
  applyComputerSet(registry.petal(index),mods->computerParts[static_cast<std::size_t>(rarityIndex(rarity))],stats);
 return stats;
}
}
