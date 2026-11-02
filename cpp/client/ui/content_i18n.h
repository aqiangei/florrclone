#pragma once
// Content-facing Chinese/English names (petals, mobs, rarities, talents).
// Machine ids and rarityName() stay English; only display helpers live here.

#include <string>

#include "shared/game/rarity.h"
#include "shared/game/skills.h"

namespace flix {
namespace ui {

const char* rarityTr(Rarity r);
const char* skillTr(SkillId id, int tier = 0);
const char* petalNameTr(const std::string& id, const char* fallback);
const char* petalDescTr(const std::string& id, const char* fallback);
const char* mobNameTr(const std::string& id, const char* fallback);
const char* mobDescTr(const std::string& id, const char* fallback);
/// Spawn-picker / biome tab label for a machine id (`garden`, `ant_hell`, …).
const char* biomeTr(const std::string& id, const char* fallback);

const char* tipHp();
const char* tipDamage();
const char* tipBodyDamage();
const char* tipCritDamage();
const char* tipFlowerHealth();
const char* tipHeal();
const char* tipHealPerSec();
const char* tipHealBonus();
const char* tipLifesteal();
const char* tipShield();
const char* tipPoison();
const char* tipArmor();
const char* tipHoldUpgrade();
const char* tipHoldRefund();
const char* tipMoreTp();
const char* tipReset();
const char* tipAccept();
const char* tipDecline();
const char* tipContinue();
const char* tipRefresh();
const char* tipSummon();
const char* tipDefaultSpawn();
const char* tipPvpArena();
const char* tipMaze();
const char* tipLightning();

} // namespace ui
} // namespace flix
