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
    AttackSpread, CleanseBerry, CommandApex, CrownReach,
    CutterShove, EarlySummon, Existing, FastMecha, FastOrbit,
    FrugalLeaf, GhostSight, GuardAggro, GuardAir, GuardBasil, GuardDisc,
    GuardLentil, GuardLotus, GuardMagnet, GuardOverheal, GuardSpread,
    LongMeteor, LongShip, LongSponge, LongTotem, LongWax,
    ManaCotton, Material, MoonEase, NazarWard, OpenEye,
    Preserved, QuickDig, RootRecovery, SplitRecovery, StarfishWindow, SteeredBubble,
    TameOrbit, WideArc, WideBlast, WideMagicEye, WideMonstera, WideRadiation,
    WideRelic, WildNoHeal, WildPoisonWindow, WildShove, WildShred, WildSlow,
    YuccaEmergency,
    CapacitorRelease, CornCover, FangRecovery, JellyWake, PlankIntercept, RockBrace,
    SaltRetort, SawbladeNotch, StingerFocus, TomatoRipening,
    BasicRally, BeanSolo, BloodRecovery, BroccoliBurst, CodeParity,
    CactusThorns, CardLastStand, ClawFollowup, ClayThrow, CogwheelLock,
    CoinSweep, ChipTracking, CoralFragments, CottonPadding, CubeBracing,
    DeadLeafShelter, DiceFloor, DustWake, GlassPhase, GlassPressure,
    GlitchJam, GoldenRecovery, HornetShot, ScriptCast, LaserPulse,
    LeafRecovery, LightRecovery, MagicCactusReserve, MagicNeedleReserve, PentagonShove,
    PokerOdds, RamRecovery, RiceCleanup, RubberGround, RubyEcho,
    SoilSettle, EvasionWake, TriangleStrike, WaspWeave,
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
    {"basic", HyperTechnique::BasicRally,
     "When defending below 35% flower health, Basic takes 8% less recoil from wild mobs.",
     "花朵生命低于 35% 且防御时，基础花瓣承受的野怪接触反伤减少 8%。", 0.92},
    {"basil", HyperTechnique::GuardBasil,
     "Below 35% health, defending improves the basil healing bonus by up to 10%.",
     "防御且生命低于 35% 时，罗勒提供的治疗加成最多提高 10%。", 1.1},
    {"battery", HyperTechnique::WideArc,
     "Each discharge searches 8% farther for its next target.",
     "每次放电寻找下一个目标的距离增加 8%。", 1.08},
    {"bean", HyperTechnique::BeanSolo,
     "On attack, hits 8% harder against wild mobs no other player has damaged.",
     "主动攻击时，对尚未被其他玩家打伤的野怪造成额外 8% 伤害。", 1.08},
    {"beetle_egg", HyperTechnique::Existing,
     "Hyper escorts retain their wild body-impact protection.",
     "超凡孵化物保留对野怪体撞的专属防护。", 0},
    {"blood_sacrifice", HyperTechnique::Preserved,
     "Hyper Blood Sacrifice follows its separate acquisition and summoning rules.",
     "超凡血祭沿用独立的获取和召唤规则。", 0},
    {"blood_stinger", HyperTechnique::BloodRecovery,
     "Attacking while below 30% health recovers 2% of the actual wild-mob damage, capped at 0.5% max health once per second.",
     "攻击且生命低于 30% 时，吸收对野怪实际伤害的 2%，每秒最多触发一次，额外回复不超过自身最大生命的 0.5%。", 0.02},
    {"bomb", HyperTechnique::WideBlast,
     "The explosion covers 12% more radius at the same damage.",
     "炸弹爆炸半径增加 12%，单次伤害相同。", 1.12},
    {"bone", HyperTechnique::BoneGuard,
     "Defending retains 55% of bone armor after recoil from wild mobs.",
     "防御时，承受接触野怪身体产生的反伤后，保留当前护甲的 55%。", 0.0},
    {"broccoli", HyperTechnique::BroccoliBurst,
     "Breaks into a burst of seeds with 12% of its base contact damage within 80 units of the break.",
     "被打碎时，种子在破碎点周围 80 单位内迸发，对野怪造成基础接触伤害的 12%。", 0.12},
    {"bubble", HyperTechnique::SteeredBubble,
     "While moving, a pop steers slightly toward your movement.",
     "移动时，泡泡的冲刺略向移动方向偏转。", 0.2},
    {"bulb", HyperTechnique::GuardAggro,
     "Defending makes the bulb’s existing pull on wild mobs 10% stronger.",
     "防御时，灯泡原有的引怪增量提高 10%。", 1.1},
    {"bur", HyperTechnique::WildShred,
     "Armor shred lasts 20% longer against wild mobs.",
     "对野怪施加的破甲延长 20%。", 1.2},
    {"c++", HyperTechnique::CodeParity,
     "Defending with three distinct Hyper computer parts reduces its wild-mob contact recoil by 12%.",
     "装备至少三类超凡电脑部件并防御时，C++ 受到的野怪接触反伤减少 12%。", 0.88},
    {"cactus", HyperTechnique::CactusThorns,
     "While defending below half health, returns 8% of health lost to a wild body hit. Uses the stronger reflection if Salt also applies.",
     "防御且生命低于一半时，将野怪体撞造成实际掉血的 8% 返还。与盐的反伤取较强者。", 0.08},
    {"capacitor", HyperTechnique::CapacitorRelease,
     "Defending when contact ends early spreads the stored discharge 15% farther. A fully charged discharge is unchanged.",
     "防御时，提前脱离接触释放的未满电闪电范围扩大 15%，满电放电沿用原范围。", 1.15},
    {"card", HyperTechnique::CardLastStand,
     "While defending, takes 12% less wild physical projectile damage when the card is at or below half durability.",
     "防御且扑克牌耐久不超过一半时，野怪物理弹幕对它的伤害减少 12%。", 0.88},
    {"carrot", HyperTechnique::CarrotBank,
     "Bouncing shots travel 20% farther at 90% of their usual speed.",
     "反弹弹道的总射程增加 20%，弹速为原来的 90%。", 0.0},
    {"champion_crown", HyperTechnique::CrownReach,
     "Hyper Basic loans reach allies 10% farther away.",
     "超凡基础花瓣可借给远 10% 的友方。", 1.1},
    {"claw", HyperTechnique::ClawFollowup,
     "On attack, wild targets at 75–80% health take 20% of Claw’s usual opening bonus.",
     "主动攻击时，对生命高于 75% 且不超过 80% 的野怪，额外造成原开场加成的 20%。", 0.2},
    {"clay", HyperTechnique::ClayThrow,
     "Attack throws one heavy clay shot up to 250 units. It hits wild mobs for 55% of base contact damage.",
     "攻击时投出一颗沉重黏土弹，射程 250 单位，对野怪造成基础接触伤害的 55%。", 0.55},
    {"clover", HyperTechnique::Existing,
     "Hyper Clover grants 1.15 Luck and has a 2.1 s base reload.",
     "超凡三叶草提供 1.15 幸运，基础装填 2.1 秒。", 0},
    {"cogwheel", HyperTechnique::CogwheelLock,
     "While defending, the ring pauses for 0.2 seconds every 2 seconds.",
     "防御时，花环每 2 秒暂停转动 0.2 秒。", 200},
    {"coin", HyperTechnique::CoinSweep,
     "A wild-mob finishing blow increases pickup range by 40 units for 4 seconds. Refreshes without stacking.",
     "用硬币击杀野怪后，拾取范围增加 40 单位，持续 4 秒。重复触发只刷新时间。", 40},
    {"compass", HyperTechnique::LocalCompass,
     "While defending, points to the closest Super or higher mob within 2,400 units when there is one.",
     "防御时，优先指向 2400 单位内最近的超级或更高阶野怪。", 0.0},
    {"computer_chip", HyperTechnique::ChipTracking,
     "With three distinct Hyper computer parts, expands Hyper Spaceship tracking range by 10%.",
     "装备至少三类超凡电脑部件时，超凡飞船的追踪范围提高 10%。", 1.1},
    {"coral", HyperTechnique::CoralFragments,
     "On breaking, scatters a 60-unit patch that slows wild mobs by up to 15% for a brief moment.",
     "被打碎时留下半径 60 单位的珊瑚碎片区，短暂让野怪减速最多 15%。", 0.85},
    {"corn", HyperTechnique::CornCover,
     "While defending, Corn itself takes 18% less physical damage from wild mob projectiles.",
     "防御时，玉米自身受到野怪弹幕的物理伤害降低 18%。", 0.82},
    {"corruption", HyperTechnique::Existing,
     "Hyper Corruption retains its PvP aura. Bandage can still work.",
     "超凡腐化保留 PvP 光环，绷带仍可生效。", 0},
    {"cotton", HyperTechnique::CottonPadding,
     "Defending reduces Cotton’s wear by 12% when it catches a wild physical hit for you.",
     "防御时，棉花替花朵吸收野怪物理伤害所损失的耐久减少 12%。", 0.88},
    {"cube", HyperTechnique::CubeBracing,
     "With four distinct Hyper computer parts, defending makes Cube take 10% less damage from wild physical projectiles.",
     "装备至少四类超凡电脑部件并防御时，立方体承受的野怪物理弹幕伤害减少 10%。", 0.9},
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
    {"dead_leaf", HyperTechnique::DeadLeafShelter,
     "While defending, worn Dead Leaf reduces wild recoil on your Hyper leaf-family petals by 10%.",
     "防御时，装备枯叶会让自身超凡叶、魔法叶、金叶和枯叶承受的野怪接触反伤减少 10%。", 0.9},
    {"dice", HyperTechnique::DiceFloor,
     "While defending, a non-critical hit on a wild mob above 90% health deals 8% more damage.",
     "防御时，对生命高于 90% 的野怪打出非暴击，伤害提高 8%。", 1.08},
    {"disc", HyperTechnique::GuardDisc,
     "While defending, takes 8% less remaining damage from wild body contact.",
     "防御时，野怪体撞造成的剩余伤害再降低 8%。", 0.92},
    {"domino", HyperTechnique::Existing,
     "Hyper Domino still rolls both ends independently, including blanks.",
     "超凡多米诺仍独立掷两端点数，允许空白。", 0},
    {"dust", HyperTechnique::DustWake,
     "While moving, contact slows an unslowed wild mob by up to 10% for 0.35 seconds.",
     "移动时，尘埃触碰未减速的野怪，使其减速最多 10%，持续 0.35 秒。", 0.9},
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
    {"glass", HyperTechnique::GlassPhase,
     "While defending, the delay between Glass hits on wild mobs is 10% shorter.",
     "防御时，玻璃命中野怪后的伤害间隔缩短 10%。", 0.9},
    {"glasss", HyperTechnique::GlassPressure,
     "While attacking, a hit on an already slowed wild mob has a 12% shorter recovery.",
     "主动攻击时，玻璃命中已减速野怪后的伤害间隔缩短 12%。", 0.88},
    {"glitch", HyperTechnique::GlitchJam,
     "Defending spends Glitch to lay a 75-unit interference field for 0.8 seconds, slowing wild mobs by up to 20%.",
     "防御时，消耗故障花瓣布下半径 75 单位的干扰区，持续 0.8 秒，野怪减速最多 20%。", 0.8},
    {"golden_leaf", HyperTechnique::GoldenRecovery,
     "When Golden Leaf breaks while defending, its own reload is 10% shorter.",
     "防御时金叶碎裂，自身装填时间缩短 10%。", 0.9},
    {"grapes", HyperTechnique::PatientGrapes,
     "On attack, venom shots gain 15% range but lose 15% speed.",
     "攻击射击时，毒弹射程增加 15%，弹速降低 15%。", 0.0},
    {"heavy", HyperTechnique::WildShove,
     "Defending pushes wild mobs 10% harder.",
     "防御时，对野怪的击退提高 10%。", 1.1},
    {"honey", HyperTechnique::DistantBait,
     "Throws the lure 25% farther with the same flight time.",
     "投掷诱饵的距离增加 25%，飞行时间相同。", 0.0},
    {"hornet_missile", HyperTechnique::HornetShot,
     "Attack fires a straight needle up to 425 units, for 65% of base contact damage against wild mobs.",
     "攻击时发射直线黄蜂导弹，射程 425 单位，对野怪造成基础接触伤害的 65%。", 0.65},
    {"icosahedron", HyperTechnique::LongRicochet,
     "Finds ricochet targets 15% farther from the petal.",
     "弹射时，可寻找距离花瓣远 15% 的目标。", 0.0},
    {"iris", HyperTechnique::WildPoisonWindow,
     "Defending stretches wild-mob poison 20% longer at the same total dose.",
     "防御时，对野怪的毒伤在总量不变的前提下延长 20%。", 1.2},
    {"javascript", HyperTechnique::ScriptCast,
     "With three distinct Hyper computer parts, attack fires one script bolt up to 330 units for 45% base contact damage against wild mobs.",
     "装备至少三类超凡电脑部件后，攻击会发射一枚脚本弹，射程 330 单位，对野怪造成基础接触伤害的 45%。", 0.45},
    {"jelly", HyperTechnique::JellyWake,
     "While defending, Jelly slows a wild mob by up to 15% for 0.4 seconds if it survives and is not already slowed.",
     "防御时，水母让撞到它后仍存活、且未被减速的野怪减速最多 15%，持续 0.4 秒。", 0.85},
    {"laser", HyperTechnique::LaserPulse,
     "Attack fires a brief straight pulse up to 390 units, for 45% base contact damage against wild mobs.",
     "攻击时放出一道短促直线脉冲，射程 390 单位，对野怪造成基础接触伤害的 45%。", 0.45},
    {"leaf", HyperTechnique::LeafRecovery,
     "Standing still with a neutral ring below half health shortens active wild-mob poison by 0.4 seconds once per 8 seconds.",
     "生命低于一半时，停步并放松花环，叶会缩短野怪中毒 0.4 秒，每 8 秒一次。", 400},
    {"lentil", HyperTechnique::GuardLentil,
     "Defending increases the existing attraction range by 10%.",
     "防御时，扁豆原有的牵引距离提高 10%。", 1.1},
    {"light", HyperTechnique::LightRecovery,
     "A broken Light grain reloads 8% sooner.",
     "光的一粒花瓣碎裂后，该粒装填缩短 8%。", 0.92},
    {"lightning", HyperTechnique::WideArc,
     "Lightning searches 12% farther for its next target.",
     "闪电寻找下一个目标的距离增加 12%。", 1.12},
    {"lotus", HyperTechnique::GuardLotus,
     "Defending shortens new poison from wild mobs by 10%.",
     "防御时，野怪新施加的中毒时间缩短 10%。", 0.9},
    {"magic_bubble", HyperTechnique::SteeredBubble,
     "When moving and aiming together, the magic pop banks partway toward your aim.",
     "移动且有瞄准方向时，魔法泡泡的冲刺会部分向瞄准方向偏转。", 0.3},
    {"magic_cactus", HyperTechnique::MagicCactusReserve,
     "While defending below 25% mana, restores 0.4% of its own mana-capacity bonus each second.",
     "防御且魔力低于 25% 时，每秒回复魔法仙人掌自身提供的魔力上限的 0.4%。", 0.004},
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
    {"magic_stinger", HyperTechnique::MagicNeedleReserve,
     "Below half mana, Magic Stinger costs 10% less mana to grow back.",
     "魔力低于一半时，魔法刺针重生所需魔力减少 10%。", 0.9},
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
     "The permanent Hyper hammer strikes wild mobs harder in melee, takes 65% less recoil from those hits, and has a 4-second base reload.",
     "永久超凡雷神锤接触野怪时雷击更强，承受的接触反伤降低 65%，基础装填为 4 秒。", 0},
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
    {"pentagon", HyperTechnique::PentagonShove,
     "While defending, hits push armored wild mobs 15% harder.",
     "防御时，对带护甲的野怪击退提高 15%。", 1.15},
    {"pharaoh_s_crown", HyperTechnique::Existing,
     "The Hyper crown summons Eternal mummy beetles instead of Super ones.",
     "超凡法老王冠召唤永恒木乃伊甲虫，取代超级甲虫。", 0},
    {"pincer", HyperTechnique::WildSlow,
     "Slow lasts 15% longer against wild mobs.",
     "对野怪施加的减速延长 15%。", 1.15},
    {"plank", HyperTechnique::PlankIntercept,
     "Defending raises Plank’s multiplier against wild physical projectiles from 20× to 22×.",
     "防御时，木板对野怪物理弹幕的伤害倍率由 20 倍提高到 22 倍。", 1.10},
    {"poker_chip", HyperTechnique::PokerOdds,
     "Below 25% of its own durability, gains 2 percentage points of evasion against wild physical hits.",
     "筹码自身耐久低于 25% 时，闪避野怪物理伤害的概率提高 2 个百分点。", 0.02},
    {"pollen", HyperTechnique::BroadPollen,
     "Defending spreads a 20% wider pollen cloud that lasts 20% less time.",
     "防御布置的花粉半径增加 20%，持续时间缩短 20%。", 0.0},
    {"poo", HyperTechnique::GuardAggro,
     "Defending makes its existing wild-mob aggro reduction 10% stronger.",
     "防御时，便便原有的减仇恨量提高 10%。", 1.1},
    {"privet_berry", HyperTechnique::CleanseBerry,
     "Defending uses the berry to shorten your current poison by 0.35 seconds.",
     "防御时，消耗果实，将当前中毒缩短 0.35 秒。", 350},
    {"ram", HyperTechnique::RamRecovery,
     "Original RAM reloads 10% faster after breaking when three distinct Hyper computer parts are equipped.",
     "装备至少三类超凡电脑部件时，内存原件碎裂后的装填时间缩短 10%。", 0.9},
    {"rice", HyperTechnique::RiceCleanup,
     "While actively attacking, Rice deals 8% more damage to wild mobs below 25% health.",
     "主动攻击时，米饭对血量低于 25% 的野怪伤害提高 8%。", 1.08},
    {"rock", HyperTechnique::RockBrace,
     "While defending, Rock loses 15% less durability to recoil from hitting wild mobs.",
     "防御时，石头接触野怪造成的耐久反伤降低 15%。", 0.85},
    {"root", HyperTechnique::RootRecovery,
     "The first armor layer builds 20% faster while defending with no layers.",
     "防御且没有护甲层时，根的首层护甲积攒速度提高 20%。", 1.2},
    {"rose", HyperTechnique::ReserveHeal,
     "Saves the heal until at least half of it is needed or you are below 40% health.",
     "损失生命达到治疗量的一半，或生命低于 40% 时，才消耗花瓣治疗。", 0.0},
    {"rubber", HyperTechnique::RubberGround,
     "While defending, Rubber loses 15% less durability to wild lightning.",
     "防御时，橡胶承受野怪闪电损失的耐久减少 15%。", 0.85},
    {"ruby", HyperTechnique::RubyEcho,
     "Lightning against wild mobs below half health has an 8% larger repeat chance with the same repeat cap.",
     "闪电打中生命低于一半的野怪时，红宝石重击概率相对提高 8%，次数上限相同。", 1.08},
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
    {"soil", HyperTechnique::SoilSettle,
     "Defending while stationary reduces the extra flower size from Soil by 15%.",
     "停步防御时，土壤额外增加的花朵体积减少 15%。", 0.85},
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
    {"talisman_of_evasion", HyperTechnique::EvasionWake,
     "While defending, dodging a wild body hit slows the unslowed attacker by up to 10% for 0.45 seconds.",
     "防御时闪开野怪体撞，会令尚未减速的攻击者减速最多 10%，持续 0.45 秒。", 0.9},
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
    {"triangle", HyperTechnique::TriangleStrike,
     "With another Hyper Triangle on the active bar, this one hits wild mobs 6% harder on attack.",
     "主槽还有另一枚超凡三角时，这枚超凡三角主动攻击野怪的伤害提高 6%。", 1.06},
    {"uranium", HyperTechnique::WideRadiation,
     "Radiation reaches 10% farther at the same self-damage.",
     "辐射半径增加 10%，自伤规则相同。", 1.1},
    {"wasp_missile", HyperTechnique::WaspWeave,
     "Attack fires a weaving dart up to 350 units for 55% base contact damage against wild mobs.",
     "攻击时发射一枚摆动弹，射程 350 单位，对野怪造成基础接触伤害的 55%。", 0.55},
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
    if (!isHyperPetal(rarity) || copiedFromOtherTier) return nullptr;
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
        case HyperTechnique::GuardSpread:
        case HyperTechnique::GuardAir:
        case HyperTechnique::GuardDisc:
        case HyperTechnique::BoneGuard:
        case HyperTechnique::RootRecovery:
            return chinese ? "收紧阵线时，仔细观察花朵身边。" : "Watch what happens close to the flower when you brace.";
        case HyperTechnique::AttackSpread:
        case HyperTechnique::WingReach:
        case HyperTechnique::OpenEye:
        case HyperTechnique::CutterShove:
            return chinese ? "把花环推出去，留意第一段交锋。" : "Send the ring outward and watch the first clash.";
        case HyperTechnique::SteeredBubble:
        case HyperTechnique::MoonEase:
        case HyperTechnique::QuickRecall:
            return chinese ? "试着走动，再改变手上的姿态。" : "Try moving, then change your stance.";
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
        case HyperTechnique::BasicRally:
            return chinese ? "重伤时把这枚最熟悉的花瓣留在身边。" : "Keep the most familiar petal close when your health runs low.";
        case HyperTechnique::BeanSolo:
            return chinese ? "试着独自接下一场战斗。" : "Try taking on a fresh opponent by yourself.";
        case HyperTechnique::BloodRecovery:
            return chinese ? "重伤时，刺入野怪的一针会带回一点生命。" : "At low health, see what the needle brings back from a hit.";
        case HyperTechnique::BroccoliBurst:
            return chinese ? "离野怪近一点，留意破碎时的种子。" : "Stay close to the mob and watch the seeds when Broccoli breaks.";
        case HyperTechnique::CodeParity:
            return chinese ? "组齐部件，再用它迎接撞来的野怪。" : "Complete the kit before bracing for a charging mob.";
        case HyperTechnique::CactusThorns:
            return chinese ? "危险时收环，看看谁先碰上尖刺。" : "Draw in the ring under pressure and see who touches the thorns first.";
        case HyperTechnique::CardLastStand:
            return chinese ? "留意半损的牌还能接下多少弹幕。" : "Watch how much fire a battered card can still take.";
        case HyperTechnique::ClawFollowup:
            return chinese ? "第一爪过后，再追一爪。" : "After the first scratch, go in for another.";
        case HyperTechnique::ClayThrow:
            return chinese ? "别贴得太近，沉重的弹丸需要一点飞行空间。" : "Give the heavy clay ball a little room to fly.";
        case HyperTechnique::CogwheelLock:
            return chinese ? "收环数一下齿轮停顿的节拍。" : "Draw in the ring and count the pause between turns.";
        case HyperTechnique::CoinSweep:
            return chinese ? "最后一击之后，留意地上的掉落。" : "After the finishing blow, watch the loot on the ground.";
        case HyperTechnique::ChipTracking:
            return chinese ? "飞船和芯片一起装，试试锁定更远的目标。" : "Equip a ship and a chip together, then try locking on from farther away.";
        case HyperTechnique::CoralFragments:
            return chinese ? "碎掉的珊瑚也能拖慢追兵。" : "Even broken coral can make pursuers slow down.";
        case HyperTechnique::CottonPadding:
            return chinese ? "收环后让棉花替你接住一击。" : "Draw in the ring and let Cotton catch the next hit.";
        case HyperTechnique::CubeBracing:
            return chinese ? "组装成套，再把这一面朝向弹幕。" : "Complete the kit, then turn this side toward incoming fire.";
        case HyperTechnique::DeadLeafShelter:
            return chinese ? "和其他叶片一起装备，防御时看谁留得更久。" : "Equip it beside other leaves and see which ones last longer when you guard.";
        case HyperTechnique::DiceFloor:
            return chinese ? "即使没掷出大奖，也有一小笔开场底注。" : "Even a bad roll can pay a little on the opening hit.";
        case HyperTechnique::DustWake:
            return chinese ? "跑动起来，观察追兵穿过尘埃的一瞬。" : "Keep moving and watch what happens when pursuers cross the dust.";
        case HyperTechnique::GlassPhase:
            return chinese ? "把玻璃留在近处，看它穿过怪物时的节奏。" : "Keep the glass close and watch its rhythm as it passes through a mob.";
        case HyperTechnique::GlassPressure:
            return chinese ? "先拖慢目标，再让玻璃穿过去。" : "Slow the target first, then let the glass pass through.";
        case HyperTechnique::GlitchJam:
            return chinese ? "靠近野怪收起花环，再观察脚下。" : "Draw in the ring near a mob and watch the ground.";
        case HyperTechnique::GoldenRecovery:
            return chinese ? "守住这片金叶，留意它什么时候回环。" : "Guard the golden leaf and see when it comes back to the ring.";
        case HyperTechnique::HornetShot:
            return chinese ? "等针尖转到目标方向，再直线发射。" : "Wait for the point to line up with the target before you fire.";
        case HyperTechnique::ScriptCast:
            return chinese ? "程序齐备后，攻击键会执行新的指令。" : "With the kit assembled, the attack key runs a new command.";
        case HyperTechnique::LaserPulse:
            return chinese ? "等花瓣和目标排成一线。" : "Wait for the petal and target to line up.";
        case HyperTechnique::LeafRecovery:
            return chinese ? "先停下来，让花环松一口气。" : "Stop moving and give the ring a moment to rest.";
        case HyperTechnique::LightRecovery:
            return chinese ? "留意最先熄灭的那粒光。" : "Watch the first point of light that goes out.";
        case HyperTechnique::MagicCactusReserve:
            return chinese ? "蓝条快空时，收起花环再观察。" : "When the mana bar is nearly empty, draw in the ring.";
        case HyperTechnique::MagicNeedleReserve:
            return chinese ? "剩一小格魔力时，再等针尖回来。" : "Let the needle come back while you are running low on mana.";
        case HyperTechnique::PentagonShove:
            return chinese ? "把五个角留给带甲的对手。" : "Save those five corners for an armored opponent.";
        case HyperTechnique::PokerOdds:
            return chinese ? "只剩最后一点耐久时再看赔率。" : "Check the odds when the chip is almost out of durability.";
        case HyperTechnique::RamRecovery:
            return chinese ? "组装成套，观察内存原件重启。" : "Complete the kit and watch the memory reboot.";
        case HyperTechnique::RiceCleanup:
            return chinese ? "盯住快见底的血条，米饭擅长收尾。" : "Watch for a nearly empty health bar. Rice is quick to finish.";
        case HyperTechnique::RubberGround:
            return chinese ? "迎着闪电收环，听它接地的那一声。" : "Draw in the ring against lightning and listen for it to ground.";
        case HyperTechnique::RubyEcho:
            return chinese ? "野怪血条过半之后，再数闪电回响。" : "When the mob is below half health, count the lightning echoes.";
        case HyperTechnique::SoilSettle:
            return chinese ? "扎稳脚跟，观察土壤收紧花身。" : "Plant your feet and watch the soil settle around the flower.";
        case HyperTechnique::EvasionWake:
            return chinese ? "收环躲开体撞后，看追兵的下一步。" : "Guard through a charge and watch the pursuer's next step.";
        case HyperTechnique::TriangleStrike:
            return chinese ? "两枚同阶三角一起出击，留意叠装的变化。" : "Attack with two Hyper Triangles and see how they work together.";
        case HyperTechnique::WaspWeave:
            return chinese ? "绕一点角度，让摆动的毒针自己接近目标。" : "Aim a little off center and let the dart weave toward the target.";
        default:
            return chinese ? "先看它原有的用途，再试一次最适合它的时机。" : "Try it at the moment that best suits its original role.";
    }
}

} // namespace flix
