#pragma once

#include <algorithm>
#include <string_view>

#include "shared/game/rarity.h"

namespace flix {

enum class HyperTechnique {
    None, WideSight, ReserveHeal, SwiftHeal, ReserveShield, ReserveMana, RescueReach,
    DefensiveWeb, RapidWeb, BroadPollen, DistantBait, LocalCompass,
    WingReach, MissileSpeed, MagicMissileRange, CarrotBank, SteadyPeas,
    PatientGrapes, StableBlueberry, BoneGuard, QuickRecall, LongRicochet,
    AttackOrbit, AttackSpread, CleanseBerry, CommandApex, CrownReach,
    CutterShove, DiceOrbit, EarlySummon, Existing, FastMecha, FastOrbit,
    FrugalLeaf, GhostSight, GuardAggro, GuardAir, GuardBasil, GuardDisc,
    GuardLentil, GuardLotus, GuardMagnet, GuardOrbit, GuardOverheal, GuardSpread,
    IdleOrbit, LongMeteor, LongShip, LongSponge, LongTotem, LongWax,
    ManaCotton, Material, MoonEase, MovingOrbit, NazarWard, OpenEye,
    Preserved, QuickDig, RootRecovery, SplitRecovery, StarfishWindow, SteeredBubble,
    TameOrbit, WideArc, WideBlast, WideMagicEye, WideMonstera, WideRadiation,
    WideRelic, WildNoHeal, WildPoisonWindow, WildShove, WildShred, WildSlow,
    YuccaEmergency,
    CapacitorRelease, CornCover, FangRecovery, JellyWake, PlankIntercept, RockBrace,
    SaltRetort, SawbladeNotch, StingerFocus, TomatoRipening,
};

struct HyperTechniqueInfo {
    std::string_view id;
    HyperTechnique technique;
    const char* english;
    const char* chinese;
    double amount;
};

inline constexpr HyperTechniqueInfo kHyperTechniques[] = {
    {"air", HyperTechnique::GuardAir,
     "Defending reduces Air’s extra flower size by 12%.",
     "防御时，空气附加的花朵体积缩小 12%。", 0.88},
    {"amulet", HyperTechnique::GuardOverheal,
     "While defending, converts 5 more percentage points of overheal into shields.",
     "防御时，溢出治疗转护盾的比例提高 5 个百分点。", 0.05},
    {"ankh", HyperTechnique::Preserved,
     "Ankh retains its existing return-on-break rule.",
     "安卡保留原有的破碎回传规则。", 0},
    {"antennae", HyperTechnique::WideSight,
     "See 11 times the default width, up from 10 at Eternal.",
     "可见范围宽度为默认的 11 倍，永恒为 10 倍。", 0.0},
    {"bandage", HyperTechnique::Existing,
     "Hyper Bandage prolongs the existing Undead window.",
     "超凡绷带延长原有的亡灵时间。", 0},
    {"basic", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"basil", HyperTechnique::GuardBasil,
     "Below 35% health, defending improves the basil healing bonus by up to 10%.",
     "防御且生命低于 35% 时，罗勒提供的治疗加成最多提高 10%。", 1.1},
    {"battery", HyperTechnique::WideArc,
     "Each discharge searches 8% farther for its next target.",
     "每次放电寻找下一个目标的距离增加 8%。", 1.08},
    {"bean", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 8 units farther out.",
     "攻击时，本花瓣的环位向外延伸 8 单位。", 8},
    {"beetle_egg", HyperTechnique::Existing,
     "Hyper escorts retain their wild body-impact protection.",
     "超凡孵化物保留对野怪体撞的专属防护。", 0},
    {"blood_sacrifice", HyperTechnique::Preserved,
     "Hyper Blood Sacrifice follows its separate acquisition and summoning rules.",
     "超凡血祭沿用独立的获取和召唤规则。", 0},
    {"blood_stinger", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 14 units farther out.",
     "攻击时，本花瓣的环位向外延伸 14 单位。", 14},
    {"bomb", HyperTechnique::WideBlast,
     "The explosion covers 12% more radius at the same damage.",
     "炸弹爆炸半径增加 12%，单次伤害相同。", 1.12},
    {"bone", HyperTechnique::BoneGuard,
     "Defending retains 55% of bone armor after recoil from wild mobs.",
     "防御时，承受接触野怪身体产生的反伤后，保留当前护甲的 55%。", 0.0},
    {"broccoli", HyperTechnique::MovingOrbit,
     "Moving holds this petal 8 units farther out.",
     "移动时，本花瓣的环位向外延伸 8 单位。", 8},
    {"bubble", HyperTechnique::SteeredBubble,
     "While moving, a pop steers slightly toward your movement.",
     "移动时，泡泡的冲刺略向移动方向偏转。", 0.2},
    {"bulb", HyperTechnique::GuardAggro,
     "Defending makes the bulb’s existing pull on wild mobs 10% stronger.",
     "防御时，灯泡原有的引怪增量提高 10%。", 1.1},
    {"bur", HyperTechnique::WildShred,
     "Armor shred lasts 20% longer against wild mobs.",
     "对野怪施加的破甲延长 20%。", 1.2},
    {"c++", HyperTechnique::GuardOrbit,
     "Defending holds this petal 10 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 10 单位。", -10},
    {"cactus", HyperTechnique::GuardOrbit,
     "Defending holds this petal 10 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 10 单位。", -10},
    {"capacitor", HyperTechnique::CapacitorRelease,
     "Defending when contact ends early spreads the stored discharge 15% farther. A fully charged discharge is unchanged.",
     "防御时，提前脱离接触释放的未满电闪电范围扩大 15%，满电放电沿用原范围。", 1.15},
    {"card", HyperTechnique::GuardOrbit,
     "Defending holds the card 8 units closer to the flower.",
     "防御时，扑克牌的环位向内收拢 8 单位。", -8},
    {"carrot", HyperTechnique::CarrotBank,
     "Bouncing shots travel 20% farther at 90% of their usual speed.",
     "反弹弹道的总射程增加 20%，弹速为原来的 90%。", 0.0},
    {"champion_crown", HyperTechnique::CrownReach,
     "Hyper Basic loans reach allies 10% farther away.",
     "超凡基础花瓣可借给远 10% 的友方。", 1.1},
    {"claw", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 12 units farther out.",
     "攻击时，本花瓣的环位向外延伸 12 单位。", 12},
    {"clay", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 11 units farther out.",
     "攻击时，本花瓣的环位向外延伸 11 单位。", 11},
    {"clover", HyperTechnique::Existing,
     "Hyper Clover grants 1.15 Luck and has a 2.1 s base reload.",
     "超凡三叶草提供 1.15 幸运，基础装填 2.1 秒。", 0},
    {"cogwheel", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 9 units farther out.",
     "未攻防时，本花瓣的环位向外延伸 9 单位。", 9},
    {"coin", HyperTechnique::MovingOrbit,
     "Moving holds this petal 10 units farther out.",
     "移动时，本花瓣的环位向外延伸 10 单位。", 10},
    {"compass", HyperTechnique::LocalCompass,
     "While defending, points to the closest Super or higher mob within 2,400 units when there is one.",
     "防御时，优先指向 2400 单位内最近的超级或更高阶野怪。", 0.0},
    {"computer_chip", HyperTechnique::GuardOrbit,
     "Defending holds this petal 6 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 6 单位。", -6},
    {"coral", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 9 units farther out.",
     "未攻防时，本花瓣的环位向外延伸 9 单位。", 9},
    {"corn", HyperTechnique::CornCover,
     "While defending, Corn itself takes 18% less physical damage from wild mob projectiles.",
     "防御时，玉米自身受到野怪弹幕的物理伤害降低 18%。", 0.82},
    {"corruption", HyperTechnique::Existing,
     "Hyper Corruption retains its PvP aura. Bandage can still work.",
     "超凡腐化保留 PvP 光环，绷带仍可生效。", 0},
    {"cotton", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units farther out.",
     "防御时，本花瓣的环位向外延伸 8 单位。", 8},
    {"cube", HyperTechnique::GuardOrbit,
     "Defending holds this petal 12 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 12 单位。", -12},
    {"cursor", HyperTechnique::CommandApex,
     "Your own Hyper summons can also follow your pointer.",
     "自己的超凡召唤物也能服从光标指挥。", 0},
    {"cutter", HyperTechnique::CutterShove,
     "Attacking body slams nudge wild mobs outward.",
     "主动攻击时，花朵的撞击会额外推开野怪。", 28},
    {"dahlia", HyperTechnique::SwiftHeal,
     "Healing petals fly back 25% more strongly when called in.",
     "触发治疗后，回身加速度提高 25%。", 0.0},
    {"dandelion", HyperTechnique::WildNoHeal,
     "Healing lock lasts 15% longer against wild mobs.",
     "对野怪施加的禁疗延长 15%。", 1.15},
    {"dark_mark", HyperTechnique::Existing,
     "The Hyper mark follows its existing hold and return rules.",
     "超凡暗黑印记沿用现有的标记和回归规则。", 0},
    {"dead_leaf", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"dice", HyperTechnique::DiceOrbit,
     "On attack, a die showing less than six stays 8 units closer.",
     "主动攻击时，点数低于六的骰子收拢 8 单位。", -8},
    {"disc", HyperTechnique::GuardDisc,
     "While defending, takes 8% less remaining damage from wild body contact.",
     "防御时，野怪体撞造成的剩余伤害再降低 8%。", 0.92},
    {"domino", HyperTechnique::Existing,
     "Hyper Domino still rolls both ends independently, including blanks.",
     "超凡多米诺仍独立掷两端点数，允许空白。", 0},
    {"dust", HyperTechnique::AttackOrbit,
     "Attacking sweeps this dust 13 units farther out.",
     "攻击时，尘埃的环位向外延伸 13 单位。", 13},
    {"egg", HyperTechnique::Existing,
     "Hyper escorts retain their wild body-impact protection.",
     "超凡孵化物保留对野怪体撞的专属防护。", 0},
    {"electric_web", HyperTechnique::RapidWeb,
     "Thrown electric webs deploy in 0.28 seconds.",
     "投掷电网时，布网时间缩短到 0.28 秒。", 0.0},
    {"fang", HyperTechnique::FangRecovery,
     "While defending below 40% health, bites on wild mobs heal 15% more, with the extra healing capped at 2% max health per bite.",
     "生命低于 40% 且防御时，咬中野怪的吸血提高 15%，每次额外回复最多为自身最大生命的 2%。", 0.15},
    {"faster", HyperTechnique::FastOrbit,
     "Attacking improves the petal’s existing spin bonus by 10%.",
     "攻击时，加速花瓣原有的转速加成提高 10%。", 1.1},
    {"fragment", HyperTechnique::Material,
     "Ten Eternal Fragments forge the permanent Hyper hammer.",
     "十枚永恒碎片锻造永久超凡雷神锤。", 0},
    {"glass", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 10 units farther out.",
     "未攻防时，本花瓣的环位向外延伸 10 单位。", 10},
    {"glasss", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 7 units farther out.",
     "未攻防时，本花瓣的环位向外延伸 7 单位。", 7},
    {"glitch", HyperTechnique::GuardOrbit,
     "Defending holds this petal 10 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 10 单位。", -10},
    {"golden_leaf", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 8 units closer to the flower.",
     "未攻防时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"grapes", HyperTechnique::PatientGrapes,
     "On attack, venom shots gain 15% range but lose 15% speed.",
     "攻击射击时，毒弹射程增加 15%，弹速降低 15%。", 0.0},
    {"heavy", HyperTechnique::WildShove,
     "Defending pushes wild mobs 10% harder.",
     "防御时，对野怪的击退提高 10%。", 1.1},
    {"honey", HyperTechnique::DistantBait,
     "Throws the lure 25% farther with the same flight time.",
     "投掷诱饵的距离增加 25%，飞行时间相同。", 0.0},
    {"hornet_missile", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 15 units farther out.",
     "攻击时，本花瓣的环位向外延伸 15 单位。", 15},
    {"icosahedron", HyperTechnique::LongRicochet,
     "Finds ricochet targets 15% farther from the petal.",
     "弹射时，可寻找距离花瓣远 15% 的目标。", 0.0},
    {"iris", HyperTechnique::WildPoisonWindow,
     "Defending stretches wild-mob poison 20% longer at the same total dose.",
     "防御时，对野怪的毒伤在总量不变的前提下延长 20%。", 1.2},
    {"javascript", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 14 units farther out.",
     "攻击时，本花瓣的环位向外延伸 14 单位。", 14},
    {"jelly", HyperTechnique::JellyWake,
     "While defending, Jelly slows a wild mob by up to 15% for 0.4 seconds if it survives and is not already slowed.",
     "防御时，水母让撞到它后仍存活、且未被减速的野怪减速最多 15%，持续 0.4 秒。", 0.85},
    {"laser", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 16 units farther out.",
     "攻击时，本花瓣的环位向外延伸 16 单位。", 16},
    {"leaf", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"lentil", HyperTechnique::GuardLentil,
     "Defending increases the existing attraction range by 10%.",
     "防御时，扁豆原有的牵引距离提高 10%。", 1.1},
    {"light", HyperTechnique::IdleOrbit,
     "Keeping the ring neutral holds this petal 9 units closer to the flower.",
     "未攻防时，本花瓣的环位向花朵收拢 9 单位。", -9},
    {"lightning", HyperTechnique::WideArc,
     "Lightning searches 12% farther for its next target.",
     "闪电寻找下一个目标的距离增加 12%。", 1.12},
    {"lotus", HyperTechnique::GuardLotus,
     "Defending shortens new poison from wild mobs by 10%.",
     "防御时，野怪新施加的中毒时间缩短 10%。", 0.9},
    {"magic_bubble", HyperTechnique::SteeredBubble,
     "When moving and aiming together, the magic pop banks partway toward your aim.",
     "移动且有瞄准方向时，魔法泡泡的冲刺会部分向瞄准方向偏转。", 0.3},
    {"magic_cactus", HyperTechnique::GuardOrbit,
     "Defending holds this petal 12 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 12 单位。", -12},
    {"magic_cotton", HyperTechnique::ManaCotton,
     "Defending spends 10% of max mana to cushion 12% of damage from a wild physical hit.",
     "防御时，可消耗最大魔力的 10%，缓冲野怪物理一击的 12% 伤害。", 0.12},
    {"magic_eye", HyperTechnique::WideMagicEye,
     "Detects targets and casts in a 10% larger radius.",
     "魔眼侦测与施法半径增加 10%。", 1.1},
    {"magic_leaf", HyperTechnique::FrugalLeaf,
     "Defending above 75% mana lends a little of your reserve to a nearby ally.",
     "防御且魔力高于 75% 时，将少量自身魔力分给附近一名友方。", 0.01},
    {"magic_missile", HyperTechnique::MagicMissileRange,
     "The charged shot flies 15% farther for the same mana.",
     "充能飞弹的射程增加 15%，魔耗相同。", 0.0},
    {"magic_peas", HyperTechnique::StableBlueberry,
     "Defensive shots fly 15% faster without changing their range.",
     "防御射击时，弹速提高 15%，射程相同。", 0.0},
    {"magic_stick", HyperTechnique::Existing,
     "Hyper improves Sandstorm strength within the existing summon cap.",
     "超凡提高沙暴召唤物强度，仍受现有数量上限限制。", 0},
    {"magic_stinger", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 18 units farther out.",
     "攻击时，本花瓣的环位向外延伸 18 单位。", 18},
    {"magnet", HyperTechnique::GuardMagnet,
     "Defending gives the strongest Hyper Magnet 12% more pickup range.",
     "防御时，最强的超凡磁铁额外增加其拾取距离的 12%。", 0.12},
    {"mana_orb", HyperTechnique::ReserveMana,
     "Saves the orb until at least half its mana would fit, or you are below 25% mana.",
     "损失魔力达到回复量的一半，或剩余魔力低于 25% 时，才消耗魔力球。", 0.0},
    {"mecha_missile", HyperTechnique::FastMecha,
     "Straight shots fly 15% faster. Observer-guided shots keep their current speed.",
     "直射弹速提高 15%，机械触角引导的弹速保持原值。", 1.15},
    {"meteor", HyperTechnique::LongMeteor,
     "The placed meteor lasts up to 22 seconds.",
     "放下的陨石最多持续 22 秒。", 22},
    {"mimic", HyperTechnique::Existing,
     "Hyper copies need a Hyper source. The sawblade technique requires the original.",
     "拟态须复制超凡原件才可继承机制，锯片的新增机制仅限原件。", 0},
    {"missile", HyperTechnique::MissileSpeed,
     "Fires 20% faster in flight at the same maximum distance.",
     "弹速提高 20%，最大射程相同。", 0.0},
    {"mjolnir", HyperTechnique::Existing,
     "The permanent Hyper hammer has a 6-second base reload.",
     "永久超凡雷神锤的基础装填为 6 秒。", 0},
    {"monstera", HyperTechnique::WideMonstera,
     "Defending heals one nearby wounded ally for up to 1% of their health each second.",
     "防御时，每秒为附近一名受伤的友方恢复最多 1% 最大生命。", 0.01},
    {"moon_rock", HyperTechnique::MoonEase,
     "Defending gently reels the moon toward the flower.",
     "防御时，月岩会缓慢向花朵收回。", 90},
    {"mysterious_powder", HyperTechnique::Existing,
     "Each Hyper slot provides the existing 90% movement-speed bonus.",
     "每个超凡栏位提供现有的 90% 移速加成。", 0},
    {"mysterious_relic", HyperTechnique::WideRelic,
     "Defending shares incoming damage only with Relic allies within 600 units.",
     "防御时，只与 600 单位内持有遗物的队友分摊受到的伤害。", 600},
    {"mysterious_stick", HyperTechnique::Existing,
     "Hyper improves Sandstorm strength within the existing summon cap.",
     "超凡提高沙暴召唤物强度，仍受现有数量上限限制。", 0},
    {"nazar_amulet", HyperTechnique::NazarWard,
     "Defending cuts one wild physical hit of at least 30% max HP by 60%, once per 30 seconds.",
     "防御时，可把一次达到最大生命 30% 的野怪物理伤害削减 60%，每 30 秒一次。", 0.6},
    {"observer", HyperTechnique::Existing,
     "The wider Hyper scope still supports the Mecha Missile lock-on.",
     "超凡机械触角进一步扩大视野，并保留机甲导弹锁定。", 0},
    {"orange", HyperTechnique::AttackSpread,
     "Attacking spreads the orange segments 18% wider.",
     "攻击时，橙子瓣间距扩大 18%。", 1.18},
    {"paranormal_goggles", HyperTechnique::GhostSight,
     "While seeing ghosts, the goggles’ camera penalty is reduced from 25% to 15%.",
     "看见幽灵时，护目镜的视野缩放惩罚从 25% 减到 15%。", 1.15},
    {"pearl", HyperTechnique::QuickRecall,
     "Recalls to the ring in 80% of the usual time.",
     "收回珍珠时，回环所需时间缩短 20%。", 0.0},
    {"peas", HyperTechnique::SteadyPeas,
     "Defensive shots keep their course while you move.",
     "防御射击时，弹道不随自身移动方向偏移。", 0.0},
    {"pentagon", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 10 units farther out.",
     "攻击时，本花瓣的环位向外延伸 10 单位。", 10},
    {"pharaoh_s_crown", HyperTechnique::Existing,
     "The Hyper crown strengthens each summoned mummy beetle without adding more.",
     "超凡法老王冠强化每只召唤的木乃伊甲虫，数量不增加。", 0},
    {"pincer", HyperTechnique::WildSlow,
     "Slow lasts 15% longer against wild mobs.",
     "对野怪施加的减速延长 15%。", 1.15},
    {"plank", HyperTechnique::PlankIntercept,
     "Defending raises Plank’s multiplier against wild physical projectiles from 20× to 22×.",
     "防御时，木板对野怪物理弹幕的伤害倍率由 20 倍提高到 22 倍。", 1.10},
    {"poker_chip", HyperTechnique::GuardOrbit,
     "Defending holds this petal 6 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 6 单位。", -6},
    {"pollen", HyperTechnique::BroadPollen,
     "Defending spreads a 20% wider pollen cloud that lasts 20% less time.",
     "防御布置的花粉半径增加 20%，持续时间缩短 20%。", 0.0},
    {"poo", HyperTechnique::GuardAggro,
     "Defending makes its existing wild-mob aggro reduction 10% stronger.",
     "防御时，便便原有的减仇恨量提高 10%。", 1.1},
    {"privet_berry", HyperTechnique::CleanseBerry,
     "Defending uses the berry to shorten your current poison by 0.35 seconds.",
     "防御时，消耗果实，将当前中毒缩短 0.35 秒。", 350},
    {"ram", HyperTechnique::GuardOrbit,
     "Defending holds this petal 9 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 9 单位。", -9},
    {"rice", HyperTechnique::AttackOrbit,
     "Attacking holds this petal 7 units farther out.",
     "攻击时，本花瓣的环位向外延伸 7 单位。", 7},
    {"rock", HyperTechnique::RockBrace,
     "While defending, Rock loses 15% less durability to recoil from hitting wild mobs.",
     "防御时，石头接触野怪造成的耐久反伤降低 15%。", 0.85},
    {"root", HyperTechnique::RootRecovery,
     "The first armor layer builds 20% faster while defending with no layers.",
     "防御且没有护甲层时，根的首层护甲积攒速度提高 20%。", 1.2},
    {"rose", HyperTechnique::ReserveHeal,
     "Saves the heal until at least half of it is needed or you are below 40% health.",
     "损失生命达到治疗量的一半，或生命低于 40% 时，才消耗花瓣治疗。", 0.0},
    {"rubber", HyperTechnique::GuardOrbit,
     "Defending holds this petal 12 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 12 单位。", -12},
    {"ruby", HyperTechnique::GuardOrbit,
     "Defending holds this petal 7 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 7 单位。", -7},
    {"salt", HyperTechnique::SaltRetort,
     "Defending reflects 10% more damage back to same-tier or stronger wild mobs on direct hits.",
     "防御时，受到同阶或更高阶野怪的直接伤害，返还的伤害提高 10%。", 1.10},
    {"sand", HyperTechnique::GuardSpread,
     "Defending spreads this clump 20% wider.",
     "防御时，沙粒簇间距扩大 20%。", 1.2},
    {"sawblade", HyperTechnique::SawbladeNotch,
     "While defending, a real Sawblade can cut 5% of a wild mob’s armor for 0.7 seconds if it is not already shredded.",
     "防御时，原件锯片能削去尚未破甲的野怪 5% 护甲，持续 0.7 秒。", 0.05},
    {"shell", HyperTechnique::ReserveShield,
     "Saves the shell until its shield is gone or half a refill would fit.",
     "护盾耗尽，或剩余容量达到单次护盾量的一半时，才消耗贝壳。", 0.0},
    {"shovel", HyperTechnique::QuickDig,
     "Charges burrowing 15% sooner after reloading.",
     "装填完成后的潜地准备时间缩短 15%。", 0.85},
    {"soil", HyperTechnique::GuardOrbit,
     "Defending holds this petal 14 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 14 单位。", -14},
    {"spaceship", HyperTechnique::LongShip,
     "The carrier flies 15% farther before landing.",
     "飞船的投放射程增加 15%。", 1.15},
    {"splitter", HyperTechnique::SplitRecovery,
     "The shared switch reload is shortened by 8%.",
     "两半共同的切换装填缩短 8%。", 0.92},
    {"sponge", HyperTechnique::LongSponge,
     "Defending spreads newly caught damage over 12% more time.",
     "防御时，新接下的伤害分摊时间延长 12%。", 1.12},
    {"square", HyperTechnique::EarlySummon,
     "The first square arrives 10% earlier after this petal loads.",
     "本花瓣装填后，第一只方块的召唤准备缩短 10%。", 0.9},
    {"starfish", HyperTechnique::StarfishWindow,
     "Recovery starts below 80% health instead of 75%.",
     "海星的恢复阈值从 75% 提高到 80%。", 0.8},
    {"stinger", HyperTechnique::StingerFocus,
     "While attacking, the cluster tightens by 20% when its slot passes within 45° of your aim.",
     "攻击时，刺针簇转过瞄准方向的 45° 范围内时，簇间距收紧 20%。", 0.8},
    {"talisman_of_evasion", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"third_eye", HyperTechnique::OpenEye,
     "Extends the attack ring 10% farther while attacking.",
     "主动攻击时，第三只眼提供的额外攻击环距离提高 10%。", 1.1},
    {"timed_mjolnir", HyperTechnique::Material,
     "The temporary hammer is an Eternal item with its own timer.",
     "限时锤属于永恒，使用独立计时。", 0},
    {"tomato", HyperTechnique::TomatoRipening,
     "Once it has been out for 5 seconds, Tomato ripens one step ahead against wild mobs, with the same maximum.",
     "上场满 5 秒后，番茄对野怪提前获得一档成熟伤害，成熟上限相同。", 1000},
    {"totem", HyperTechnique::LongTotem,
     "The recovery earned by a kill lasts 11 seconds.",
     "击杀触发的图腾恢复持续 11 秒。", 11000},
    {"triangle", HyperTechnique::GuardOrbit,
     "Defending holds this petal 8 units closer to the flower.",
     "防御时，本花瓣的环位向花朵收拢 8 单位。", -8},
    {"uranium", HyperTechnique::WideRadiation,
     "Radiation reaches 10% farther at the same self-damage.",
     "辐射半径增加 10%，自伤规则相同。", 1.1},
    {"wasp_missile", HyperTechnique::MovingOrbit,
     "Moving holds this petal 13 units farther out.",
     "移动时，本花瓣的环位向外延伸 13 单位。", 13},
    {"wax", HyperTechnique::LongWax,
     "Deployed wax is placed 10% farther from the flower.",
     "布置蜂蜡时，落点比原来远 10%。", 1.1},
    {"web", HyperTechnique::DefensiveWeb,
     "Planting with defend spreads the web 15% wider.",
     "防御布网时，蛛网半径扩大 15%。", 0.0},
    {"wing", HyperTechnique::WingReach,
     "Attack lunges extend 20% farther beyond the ring.",
     "攻击时，冲出环外的距离增加 20%。", 0.0},
    {"yggdrasil", HyperTechnique::RescueReach,
     "Extends revival reach by 25%. The extra reach needs a clear path.",
     "复活距离增加 25%。额外距离需要无遮挡。", 0.0},
    {"yin_yang", HyperTechnique::TameOrbit,
     "Defending softens this petal’s reverse-spin contribution by 15%.",
     "防御时，阴阳的反向转动贡献减弱 15%。", 0.85},
    {"yucca", HyperTechnique::YuccaEmergency,
     "Below 30% health, restores a quarter of its usual healing with the ring neutral.",
     "生命低于 30% 时，中立姿态也能获得正常防御治疗量的四分之一。", 0.25},
};

inline const HyperTechniqueInfo* hyperTechniqueInfo(std::string_view id, Rarity rarity,
                                                     bool copiedFromOtherTier = false) {
    if (rarity != Rarity::Apex || copiedFromOtherTier) return nullptr;
    const auto* end = kHyperTechniques + sizeof(kHyperTechniques) / sizeof(*kHyperTechniques);
    const auto* entry = std::lower_bound(kHyperTechniques, end, id,
        [](const HyperTechniqueInfo& info, std::string_view key) { return info.id < key; });
    return entry != end && entry->id == id ? entry : nullptr;
}

inline HyperTechnique hyperTechnique(std::string_view id, Rarity rarity, bool copiedFromOtherTier) {
    const auto* info = hyperTechniqueInfo(id, rarity, copiedFromOtherTier);
    return info ? info->technique : HyperTechnique::None;
}

inline double hyperAmount(const HyperTechniqueInfo* info, HyperTechnique technique, double fallback = 1.0) {
    return info && info->technique == technique ? info->amount : fallback;
}

inline const char* hyperMysteryHint(HyperTechnique technique, bool chinese) {
    switch (technique) {
        case HyperTechnique::GuardOrbit:
        case HyperTechnique::GuardSpread:
        case HyperTechnique::GuardAir:
        case HyperTechnique::GuardDisc:
        case HyperTechnique::BoneGuard:
        case HyperTechnique::RootRecovery:
            return chinese ? "收紧阵线时，仔细观察花朵身边。" : "Watch what happens close to the flower when you brace.";
        case HyperTechnique::AttackOrbit:
        case HyperTechnique::AttackSpread:
        case HyperTechnique::WingReach:
        case HyperTechnique::OpenEye:
        case HyperTechnique::CutterShove:
            return chinese ? "把花环推出去，留意第一段交锋。" : "Send the ring outward and watch the first clash.";
        case HyperTechnique::MovingOrbit:
        case HyperTechnique::SteeredBubble:
        case HyperTechnique::MoonEase:
        case HyperTechnique::QuickRecall:
            return chinese ? "试着走动，再改变手上的姿态。" : "Try moving, then change your stance.";
        case HyperTechnique::IdleOrbit:
        case HyperTechnique::TameOrbit:
        case HyperTechnique::FastOrbit:
            return chinese ? "换个出手节奏，留意花环的走位。" : "Change your timing and watch how the ring moves.";
        case HyperTechnique::ReserveHeal:
        case HyperTechnique::ReserveShield:
        case HyperTechnique::ReserveMana:
        case HyperTechnique::StarfishWindow:
        case HyperTechnique::GuardBasil:
        case HyperTechnique::FrugalLeaf:
        case HyperTechnique::NazarWard:
        case HyperTechnique::ManaCotton:
            return chinese ? "资源见底或危险逼近时，再看它会做什么。" : "Watch for its response when you are low or in danger.";
        case HyperTechnique::RescueReach:
        case HyperTechnique::WideMonstera:
        case HyperTechnique::CrownReach:
        case HyperTechnique::WideRelic:
        case HyperTechnique::CommandApex:
            return chinese ? "队友或召唤物在附近时，试着拉开一点距离。" : "With allies or summons nearby, try leaving a little room.";
        case HyperTechnique::DefensiveWeb:
        case HyperTechnique::RapidWeb:
        case HyperTechnique::BroadPollen:
        case HyperTechnique::DistantBait:
        case HyperTechnique::LongMeteor:
        case HyperTechnique::LongWax:
            return chinese ? "先挑好落点，再用地形限制敌人。" : "Choose where it lands and use the ground to restrict enemies.";
        case HyperTechnique::WideSight:
        case HyperTechnique::GhostSight:
        case HyperTechnique::LocalCompass:
            return chinese ? "看向原本视野之外的目标。" : "Look for what used to lie outside your view.";
        case HyperTechnique::Material:
        case HyperTechnique::Preserved:
            return chinese ? "先查看它在泰坦处或掉落图鉴里的来历。" : "Check its origin at the Titan or in the drop gallery.";
        case HyperTechnique::MissileSpeed:
        case HyperTechnique::MagicMissileRange:
        case HyperTechnique::CarrotBank:
        case HyperTechnique::SteadyPeas:
        case HyperTechnique::PatientGrapes:
        case HyperTechnique::StableBlueberry:
        case HyperTechnique::FastMecha:
        case HyperTechnique::LongShip:
            return chinese ? "换个开火距离，观察弹道何时到达。" : "Fire from a different distance and watch when the shot arrives.";
        case HyperTechnique::RockBrace:
            return chinese ? "让石头替你迎接撞来的野怪。" : "Let the rock meet a charging mob.";
        case HyperTechnique::CornCover:
        case HyperTechnique::PlankIntercept:
            return chinese ? "弹幕来时先收环，试着用这一格挡住它。" : "Pull the ring in and put this slot between you and an incoming shot.";
        case HyperTechnique::StingerFocus:
            return chinese ? "盯紧瞄准线，等针尖转到你想打的方向。" : "Watch your aim line and wait for the needles to come around.";
        case HyperTechnique::FangRecovery:
            return chinese ? "生命告急时，留意贴身咬中的那一口。" : "When health is low, watch what one close bite gives back.";
        case HyperTechnique::JellyWake:
            return chinese ? "被它弹开后，野怪会慢一拍。" : "After the shove, watch the mob’s next step.";
        case HyperTechnique::SawbladeNotch:
            return chinese ? "等带甲的野怪贴上来，再顺着锯痕进攻。" : "Let an armored mob approach, then follow the cut.";
        case HyperTechnique::SaltRetort:
            return chinese ? "对手越危险，越值得收环等它先出手。" : "Against a dangerous mob, brace and let it strike first.";
        case HyperTechnique::CapacitorRelease:
            return chinese ? "电未蓄满便拉开距离，看看火花能跳多远。" : "Break contact before the charge fills and watch how far the sparks go.";
        case HyperTechnique::TomatoRipening:
            return chinese ? "先把果实护在环上，等它变熟再靠近野怪。" : "Keep the fruit on the ring a little longer before fighting.";
        default:
            return chinese ? "先看它原有的用途，再试一次最适合它的时机。" : "Try it at the moment that best suits its original role.";
    }
}

} // namespace flix
