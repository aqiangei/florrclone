// The changelog overlay: what changed, newest release first.
//
// An overlay rather than one of the tall list panels -- it hangs under the top
// icon row at a fixed 600x500, which is why its bounds come from
// `ChangelogPanel::bounds` and not from the shared list anchor.
//
// The table below began as the browser build's CHANGELOG array
// (src/changelog.ts, in git history; it ends at August 26, 2026), taken
// verbatim and in its own order -- oldest first -- and is walked backwards to
// paint. The current release is appended from the deployment notes at build time.
// Move the previous entry into this history when preparing a new release.
//
// Row pitch is written as literals rather than derived from the face's line
// height: the reference advances 25px under a date and 24px under a bullet
// whatever the face's own leading is, and deriving it would put every row of a
// 7896px list a little further out of place than the row above it.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "release_notes.h"
#include "shared/core/json.h"
#include "client/ui/text.h"

namespace flix {

using namespace flix::ui;

namespace {

constexpr double kPadding = 20.0;
/// Chrome above the scrolling body: the title row and the header buttons.
constexpr double kHeaderHeight = 40.0;
constexpr double kScrollbarWidth = 10.0;
/// A date row advances 25, a bullet 24, and 15 separates two entries.
constexpr double kDatePitch = 25.0;
constexpr double kBulletPitch = 24.0;
constexpr double kEntryGap = 15.0;
constexpr double kChangeSize = 14.0;
/// Bullet glyph to change text.
constexpr double kBulletIndent = 20.0;
/// One wheel notch is ~100px of deltaY in a browser, and this panel consumes
/// that unscaled, so the same gesture covers the ground it covers in the
/// reference.
constexpr double kWheelStep = 100.0;

/// The card's own colours come from kChangelogSkin; this is the border shade
/// reused as a rule and a scrollbar thumb on the body.
constexpr std::uint32_t kBodyBorder = kChangelogSkin.border;
constexpr std::uint32_t kLinkFill = 0xD8F7FFu;

constexpr int kMaxChanges = 32;

struct ChangelogEntry {
    const char* date;
    const char* changes[kMaxChanges];

  };

constexpr ChangelogEntry kChangelog[] = {
    {"October 18, 2025",
     {"Added changelog"}},
    {"October 19, 2025",
     {"Added 3 new biomes",
      "New admin petal: Sparkle",
      "New admin command: spawn_special_mobs"}},
    {"October 22, 2025",
     {"New petal: Lightning",
      "New petal: Iris",
      "It is discovered that mobs have round hitboxes, so the setting is now more accurate"}},
    {"October 24, 2025",
     {"Overhauled the settings menu",
      "Ultra+ petals now have particle effects"}},
    {"November 20, 2025",
     {"New biome: Jungle"}},
    {"November 21, 2025",
     {"Some recolors and tweaks to the graphics",
      "New petal: Jelly",
      "fixes"}},
    {"November 22, 2025",
     {"New petal: Yucca",
      "New petal: Leaf",
      "Target dummy now shows DPS"}},
    {"November 23, 2025",
     {"Added skills system"}},
    {"November 29, 2025",
     {"New mob: Item Spawner",
      "New petal: Cutter",
      "New petal: Lightning Cutter",
      "New petal: Wing",
      "New biome: Sewers"}},
    {"December 3, 2025",
     {"Some UI changes",
      "Recolored some biomes",
      "New petal: Square",
      "A global notification is now shown when a rare petal is crafted"}},
    {"December 10, 2025",
     {"New petal: Blood Leaf, it damages the player when exploding",
      "Healing and self-damage now scales by rarity",
      "Cactus and Poison Cactus now give max health to the player"}},
    {"December 11, 2025",
     {"New petal: Ant Egg",
      "New petal: Fire Ant Egg",
      "Some rebalancing of mobs"}},
    {"December 15, 2025",
     {"All mobs now have eggs",
      "Added mob gallery",
      "Some optimizations"}},
    {"December 18, 2025",
     {"New ant hell map"}},
    {"December 20, 2025",
     {"Added shop, note that you cannot buy petals with real money, only stars",
      "Stars are awarded for killing mobs or claiming codes",
      "New admin command: generate_code"}},
    {"December 23, 2025",
     {"Added notifications system, check it to see when there might be an event",
      "Fixed a negative petal health bug",
      "New admin command: clear_notifications",
      "New admin command: notification"}},
    {"December 26, 2025",
     {"Changed title screen UI",
      "New admin command: give"}},
    {"December 31, 2025",
     {"New petal: Splitter",
      "New petal: Gas",
      "New petal: Bulb",
      "Petals now have physics and are attracted to mobs"}},
    {"January 19, 2026",
     {"Changed the map of most biomes",
      "Some biomes are temporarily disabled for now"}},
    {"January 22, 2026",
     {"Ocean is back!",
      "New petal: Starfish",
      "New petal: Sponge",
      "New mob: Starfish",
      "New mob: Sponge"}},
    {"January 31, 2026",
     {"New biome: MATRIX",
      "˜´∑ µø∫Ú øßå",
      "˜ªº˜˚¬ˆçß••",
      "New petal: ∆å√åßç®ˆπ†"}},
    {"February 16, 2026",
     {"New petal: Glass",
      "New petal: Third Eye",
      "New petal: Corn",
      "Commands are now suggested in the chat"}},
    {"February 20, 2026",
     {"Buffed droprates of ultras by 20x",
      "Changed the UI of mobs"}},
    {"February 21, 2026",
     {"Bugfix"}},
    {"February 22, 2026",
     {"Fixed an animation loop bug"}},
    {"February 22-March 25, 2026",
     {"Fixed performance issues",
      "Your settings now save",
      "Fixed teleporter system",
      "Backend changes",
      "FPS should be higher now, and ping should be lower"}},
    {"March 27, 2026",
     {"New petal: Faster",
      "More bug fixes and UI improvements"}},
    {"April 2, 2026",
     {"New mob: Sun",
      "New petal: Pollen",
      "Bulb now emits light",
      "Nerfed blood leaf",
      "Fixed some bugs"}},
    {"April 5, 2026",
     {"You now have a second row of loadout slots"}},
    {"April 12, 2026",
     {"Changed the UI of the skills panel",
      "Changed the UI of the inventory",
      "Changed the UI of the crafting panel",
      "Added second chance skill",
      "Fixed players getting damaged during invulnerability"}},
    {"April 13, 2026",
     {"Added apex rarity",
      "Recolored unique and apex petals"}},
    {"April 17, 2026",
     {"New petal: Powder",
      "New petal: Peas",
      "New mob: Desert Centipede",
      "New mob: Centipede"}},
    {"April 18, 2026",
     {"Faster now stacks additively instead of multiplicatively",
      "Fixed players glitching into walls when going very fast",
      "Added squads",
      "Added guilds",
      "New mob: Worker Ant",
      "New mob: Baby Ant",
      "New mob: Worker Fire Ant",
      "New mob: Baby Fire Ant",
      "A new portal has opened in desert",
      "Ant hell background looks much nicer now"}},
    {"April 19, 2026",
     {"Sun no longer drops eggs (to fix an exploit)",
      "Sun now drops glass, rock, sand, pollen, speed boost, and shield",
      "Added PVP mode",
      "Added Daily Streak",
      "Any obtained sun egg is now deleted"}},
    {"April 22, 2026",
     {"New mob: Ant Hole",
      "New mob: Fire Ant Hole"}},
    {"April 23, 2026",
     {"New petal: Magnet",
      "New petal: Air",
      "New petal: Soil",
      "Changed Ant Hole and Fire Ant Hole drops"}},
    {"April 24, 2026",
     {"Optimize the game",
      "Added API keys(you can now create discord bot)",
      "Secured the server",
      "Roach now looks better",
      "Added ultra zones"}},
    {"April 26, 2026",
     {"Changed keybinds for using items from 1-10 to U+1-10",
      "New petal: Yin Yang",
      "New petal: Lentil",
      "New petal: Bubble",
      "Clover now gives luck, and increases the rarity of mobs around you",
      "Changed petal attraction system",
      "Special petals no longer work in inactive loadout slots"}},
    {"April 30, 2026",
     {"Server now restarts every 24 hours",
      "Optimizations and bug fixes",
      "The keybinds for using items can now be swapped between U+1-10 and 1-10 in the settings menu",
      "New setting: Show Admin Commands",
      "Fixed show hitboxes not working",
      "Fixed moth rendering bug",
      "Ant Hell can now spawn bosses",
      "Added a bridge to the Mythic zone in Garden",
      "Changed sewers wall textures"}},
    {"May 3, 2026",
     {"New petal: Antennae",
      "New petal: Observer"}},
    {"May 4, 2026",
     {"Pollen drops on the ground now",
      "Changed mob spawning algorithm",
      "Bee now drops pollen"}},
    {"May 20, 2026",
     {"New petal: Bomb",
      "New petal: Flower",
      "New petal: Raindrop",
      "Optimize game"}},
    {"June 2, 2026",
     {"Added discord link"}},
    {"June 19, 2026",
     {"Server crashes should now be fixed(hopefully)",
      "Admin names are no longer shown on leaderboard",
      "Added skins menu"}},
    {"June 30, 2026",
     {"Fixed unobtainable petals being in the shop",
      "Bugfix(from discord/youtube bug reports)",
      "New link: link:https://flowrix.sussybite.dev"}},
    {"July 7, 2026",
     {"Patched exploits to get sun egg",
      "Changed bee AI",
      "Changed most mob speeds",
      "Clover now increases craft chance",
      "Added debug menu",
      "Added maze mode(testing phase, if you find exploits, please bug report in discord)",
      "Petals collected in maze increase in rarity by 1 outside of maze",
      "Petals collected outside of maze decrease by 1 in maze",
      "Only Mythic- petals are allowed in maze",
      "Maze changes each day",
      "Added absorbing(only for petals collected in maze)"}},
    {"July 8, 2026",
     {"Maze now has a seperate leveling system than the main game",
      "Made maze larger",
      "Fixed some server bugs",
      "Added mobile support",
      "Added absorb talents"}},
    {"July 9, 2026",
     {"New desert background",
      "Server and client optimizations"}},
    {"July 14, 2026",
     {"Fix client FPS",
      "New setting: GPU Acceleration",
      "Patched some glitches where players could get into walls",
      "Admin give and spawn mob commands can now have amounts",
      "New admin command: killall"}},
    {"July 20, 2026",
     {"Reworked Rose",
      "New petal: Dahlia",
      "New petal: Azalea",
      "Sandstorm now spins faster",
      "Fixed mobile login",
      "Fixed common mobs dropping better petals than uncommon mobs"}},
    {"July 26, 2026",
     {"Fixed petals reloading instantly(there are no bugs)",
      "Fixed inventory getting corrupted"}},
    {"July 29, 2026",
     {"New mob: Evil Centipede",
      "New mob: Queen Ant",
      "New mob: Digger",
      "New petal: Shell",
      "New petal: Uranium",
      "New petal: Pincer",
      "New petal: Web",
      "New petal: Guided Missile",
      "New petal: Blue Iris",
      "New petal: Stick",
      "New petal: Moon",
      "New petal: Lotus",
      "New petal: Heaviest",
      "New petal: Rice",
      "Poison damage numbers are purple now",
      "Added stalling mechanics to the game",
      "Honey slows, not a lure petal"}},
    {"August 1, 2026",
     {"Target dummies now spawn less often at garden spawn",
      "Pets and target dummies no longer have bossbars"}},
    {"August 2, 2026",
     {"Made pets smaller",
      "Nerf digger egg"}},
    {"August 3, 2026",
     {"Patched duping exploit"}},
    {"August 4, 2026",
     {"Removed some petals that were duped"}},
    {"August 6, 2026",
     {"Patched a cheat",
      "Fixed apex mobs despawning",
      "Admins can now post images in chat",
      "/spawn command now announces boss mobs"}},
    {"August 7, 2026",
     {"All players can post images in chat now(use <img src=\"Image Link\">)",
      "Fix crafting bugs",
      "Added curves to skin editor"}},
    {"August 10, 2026",
     {"New mob: Glitch Flower",
      "Reworked flower petal",
      "Added corruption",
      "New admin command: /corrupt"}},
    {"August 17, 2026",
     {"Apex mobs now have 100x HP and XP",
      "Server optimizations"}},
    {"August 23, 2026",
     {"Fix bugs",
      "Magnet no longer instantly attracts petals without playing animation",
      "Jungle coming soon",
      "Looting requirements changed"}},
    {"August 24, 2026",
     {"Balanced all mob HP/damage",
      "Changed some mob sizes"}},
    {"August 26, 2026",
     {"Fixed server crash bug"}},
    {"September 20, 2026",
      {"New map for many biomes",
      "Jellyfish now spawns lightning",
      "New mob: Leech",
      "New mob: Crab",
      "New mob: Leafbug",
      "New mob: Firefly",
      "New mob: Bush",
      "New mob: Mantis",
      "New mob: Golden Leafbug",
      "New mob: Magic Firefly",
      "Added ant hell ultra zone",
      "Fixes skins not working",
      "Changed loadout style",
      "Changed petal style",
      "Changed petal rarity colors",
      "Nerfed apex mob HP",
      "You can now change your password",
      "Slightly change hornet color and AI",
      "Dandelion now shoots projectiles",
      "Added Grant Admin button in singleplayer mode",
      "Squad menu changes",
      "New petal: Root",
      "New petal: Battery",
      "New petal: Bur",
      "Added Reload talents",
      "Sponge now shows damage",
      "Added chat bubble",
      "Changed Rock visual"
    }},
    {"September 21, 2026",
     {"Added magic petals",
      "Splitter now splits you the moment it is equipped, and clicking the "
      "loaded petal swaps you to your other half",
      "Fixed some bugs",
    }},
    {"September 25, 2026",
     {
      "Sewers overhaul",
      "New petal: Fang",
      "New petal: Pearl",
      "New petal: Claw",
      "New petal: Poo",
      "New petal: Talisman",
      "Mob aggro range bugs fixed",
      "Patched some pet duping glitches",
      "Reload talent now stops at unique; an apex tier is refunded",
      "Only a centipede's or leech's head shows a bossbar or a kill message",
    }},
    {"September 26, 2026",
     {
      "Added Pet HP talents",
      "Fixed Petal HP talent applying only 190%",
      "Pet health and damage now scale with rarity like petals, not like mobs",
      "Pet HP bars now say \"summon\"",
      "Looting a mob now takes at least 1% of its health",
      "New petal: Oranges",
      "New petal: Grapes",
      "New magic petal: Blueberries",
      "Fixed peas and grapes only shooting when attacking",
      "New petal: Salt",
      "Added oracle"
    }},
    {"September 29, 2026",
      {
      "New petal: Wax",
      "New mob: Bumble Bee",
      "Bugfixes and optimizations"
    }},
    {"October 3, 2026",
      {
      "Added Universal rarity petals",
      "Added Titan NPC",
      "Changed the size of every single mob in the game",
      "Made Computer mobs smaller",
      "Changed chat style",
      "Added different chat channels",
      "Fire ant hole now pops instead of acting like regular ant hole",
      "Queen ants always spawn from ant hole now"
      }
    },
    {"October 4, 2026",
      {
      "Added Duplicator and Triplicator talents",
      "Make moon work like florr",
      "Added Magnetism talent after Legendary Absorption"
      }
    },
    {"October 7, 2026",
      {
      "Added termites",
      "New petal: Rubber",
      "New petal: Plank",
      "New petal: Relic",
      "New petal: Tomato",
      "New petal: Compass",
      "New petal: Carrot"
      }
    },
    {"October 10, 2026",
      {
        "Added debug console for certain devices",
        "DO NOT USE DEBUG CONSOLE FOR CHEATING OR YOU WILL GET BANNED",
        "New petal: Sacrifice",
        "All consumable items are converted to petals",
        "Fixed target dummies being too large",
        "Fixed pets being too large"
      }
    },
    {"October 11, 2026",
      {
        "Added termite ant hell",
        "Completely reworked pets",
        "Added screen shake",
        "Added damage flash"
      }
    },
    {"October 13, 2026",
      {
        "New petal: Sawblade",
        "New petal: Mimic",
        "New petal: Ram",
        "New petal: Cursor",
        "New petal: Computer Chip",
        "New petal: Champion's Crown",
        "Added new drops for Computer mobs"
      }
    },
    {"October 14, 2026",
      {
        "Added Assembler",
        "Added C++ mob and petal"
      }
    },
    {"October 16, 2026",
      {
        "Added super zones in Prestige biomes",
        "Added Prestige talent",
        "Added Prestiged biomes"
      }
    },
    {"October 18, 2026",
      {
        "New mob: Ant Egg",
        "New mob: Fire Ant Egg",
        "New petal: Amulet"
      }
    },
    {"October 22, 2026",
      {
        "Added pyramid",
        "Rate limited and added moderation to chat",
        "New petal: Bandage",
        "Bugfixes and polished some UI"
      }
    },
    {"October 25, 2026",
      {
        "Changed petal physics",
        "New petal: Basil",
      }
    },
    {"October 28, 2026",
      {
        "New petal: Spaceship",
        "New petal: Cube",
        "New petal: Pentagon",
        "Improved rates of many zones",
        "Clover from soldier ant rate increased from 0.1 to 0.4",
        "Optimized server performance",
        "Fixed ocean mob AI",
        "Fixed most top UI not sliding in",
        "Admins now can level up infinitely, past level 500",
        "Added admin tiers",
        "Reworked bubble petal"
      }
    },
    {"October 30, 2026",
     {"New petal: Icosahedron",
      "Buffed Icosahedron with armor that scales with rarity"}},
    {"2026年11月11日 · 刷怪配比与治疗修复",
     {"修复持续清怪后静止生物逐渐挤满刷怪区的问题，覆盖普通及声望地图。",
      "固定名额的高阶生物由各自名额补位，避免同时补出一只普通怪。",
      "恢复龟背竹的基础范围回血，详情显示治疗量与半径。",
      "同一帧多次命中时保留最强击退，避免重和水母的击退被较弱碰撞覆盖。",
      "声望沙漠火蚁穴守军的活动距离从洞口外 600 扩至 1200。",
      "花瓣详情补充发射物伤害和耐久天赋的适用说明。",
      "撤回上次铲子调整，当前装备腐化时不能开始下潜，被动腐化不限制。",
      "服务器指引支持点击弹窗外关闭，并在20秒倒计时结束后自动关闭。",
      "重瓣复制出的翅膀等单体花瓣各自损坏、各自重装。",
      "电脑系列花瓣详情补充单瓣用途、套装组件和触发条件。",
      "经验吸收天赋也提高打怪所得经验，吸收界面分开显示基础经验和天赋加成。",
      "三服皮肤工坊可浏览和穿戴其他服的公开皮肤，显示来源服。",
      "修复记录活跃时长时超凡锻造被取消，明确显示持有上限和泰坦查询状态。",
      "修复闪电起点在视野外时，命中视野内玩家却不显示特效的问题。",
      "统一火兵蚁、白兵蚁及对应宠物蛋的中文名称。",
      "议会统一按实际记录的有效游玩时长计分。"}},
    {"2026年11月3日",
     {"修复副槽与K/L方案切换，避免误触合成。",
      "金字塔地图死亡后保留，离开时重置。",
      "白蚁丘缩小洞内面积，修复墙体碰撞。",
      "白蚁丘入口生成权重降低80%，不再向外刷怪。",
      "白蚁丘需靠近中心，按F或点击按钮进入。",
      "玩家可站入Moon；怪物身体阻挡范围缩小。",
      "蜈蚣身体节恢复叶子与魔法叶子掉落。",
      "每日Bonus一小时：2/2/2/3倍循环。",
      "普通怪独立掉落60/90次，超级及以上45/60次。",
      "商店每6小时刷新，锁定后保留商品。",
      "商店花瓣采用更低的分档星星价格。",
      "新增稀有掉落：灵魂分裂，分裂与合并不影响组队。",
      "补齐天赋效果、批量合成和白蚁丘队友显示。",
      "修复静止怪物与各群系超级怪分布。",
      "K/L配装更改自动保存，无需Shift。",
      "普通九图各维持3只永恒E，死后5分钟补充。",
      "E血伤为同种S的3倍，速度不额外提高。"}},
    {"2026年11月3日 · 天赋修复",
     {"筹码自身90%独立闪避，闪电例外、免疫中毒。",
      "每图永恒E上限3只，死亡冷却5分钟。",
      "恢复Super怪的S花瓣掉落，永恒提高S掉率。",
      "永恒E死亡后5分钟补充，存活期间不替换。",
      "中文加载页显示下载进度、启动阶段与重试。",
      "每级获得2 TP，已有等级自动补齐双倍点数。",
      "花瓣血量天赋立即作用于已装备的花瓣。",
      "保留受损比例与冷却，不会因加退点回满血。"}},
    {"2026年11月3日 · 装备与掉落修复",
     {"垃圾袋显示什么就掉什么，每个可见种类至少掉一个，保留30次独立判定。",
      "R切换配装时跳过空副槽，不再卸下主槽花瓣。",
      "第一槽复制天赋的额外花瓣正确显示在装备栏。",
      "骰子在攻击时增加装备槽摇动动画。",
      "腐化装备后改变角色外观，并对20级以上玩家启用对战。",
      "法老王冠改为佩戴头饰，移除悬浮在脸上的物品图标。",
      "分开毒素与幸运天赋节点，避免重叠。",
      "队友栏过滤当前控制的自己，修复重复自身血条。",
      "锯片恢复普通轨道攻击距离。",
      "机械花与赌徒使用可破坏并重装的真实花瓣环。",
      "商店每小时刷新，每轮固定出售两件超级花瓣。",
      "伤害数字只显示自己造成的伤害及自身受伤提示。"}},
    {"2026年11月3日 · 商店与王冠补充",
     {"商店支持批量购买，显示数量与总价，并由服务器校验扣款。",
      "修复泡泡飞行切装临时缩小碰撞半径，并增加卡墙恢复保护。",
      "补齐法老王冠每秒召唤亡灵木乃伊甲虫、存活六秒，重复佩戴不叠加。"}},
    {"2026年11月3日 · 公会与视野",
     {"其他玩家公会显示在血条左下角，隐藏名字时仍可随血条显示。",
      "缩放最低可达25%，装备视野加成继续生效，服务器同步放宽可见范围。"}},
    {"2026年11月3日 · 转化机与商店价格修复",
     {"修复登录注册真实点击无法输入，保留中文输入占位提示。",
      "iOS键盘手势重聚焦、中文组合输入和粘贴修复，避开刘海与手势区。",
      "移动端真实触点长按0.4秒查看数据，松手不误装备或换槽。",
      "右上角开屏公告补充私服说明、其他版本链接及作者署名。",
      "手机和平板加载时可点闪烁的横屏全屏入口，进入游戏后保留10秒，设置也可退出全屏。",
      "永久永恒首次10个同种超级材料，后续固定15个；第二次旧价返10个，第三次起每次返40个。",
      "商店普通花瓣原价，稀有溢价最高10倍；超级拟态6000星，仍享折扣。",
      "转化机（重构机）每20分钟随机更换配方，排除当前配方。",
      "交互范围内有玩家时延后刷新，离开后再更换并重新计时。",
      "修复月岩配方名称，移除不存在材料的旧配方，按机器稀有度筛选有效配方。",
      "刷新后保持位置和稀有度，通过新网络编号同步配方，避免客户端显示旧配方。"}},
    {"2026年11月3日 · 超凡与无开屏公告",
     {"取消进入游戏时自动弹出公告，中文日志继续保留。",
      "九张正常地图各有独立超凡名额，死亡后30分钟补充，不替换存活怪物。",
      "泰坦新增超凡争夺：5个同种永恒锻造1个超凡，全服同种唯一；旧持有者返4个永恒。",
      "首次保护15小时，每次被争夺缩短1小时，最低1小时；离线持有者也正常处理。",
      "超凡雷神之锤用5把永恒锤子争夺，首保15小时逐次递减；永恒碎片不再换超凡锤。",
      "新增伤害和吸收两条成长天赋，保留旧天赋与点数。",
      "修复第一槽复制黄金叶子不计入减冷却，服务端与冷却显示使用相同递乘计算。",
      "魔法树枝每次耗蓝召唤两只持续20秒的沙暴，可配合魔法花瓣持续召唤，玩家召唤总上限不变。"}},
    {"2026年11月3日 · 树枝与分裂奖励",
     {"普通树枝、魔法树枝统一不可摧毁，保留原有展开及召唤行为。",
      "修复移动端非全屏横屏的触摸坐标偏移，按下和松手统一按画布实际位置换算。",
      "超凡怪物血量提高至此前2倍，伤害、移速、掉落与刷新规则不变；花粉不改。",
      "灵魂分裂结算保留实际贡献的身体，不再随机换成另一个身体的配装；死亡或身体移除时仍可恢复同账号奖励归属。"}},
    {"2026年11月4日 · 双泰坦争夺与掉落修复",
     {"花园出生点东侧新增泰坦，与丛林泰坦独立；同种超凡可各存在一份，互不夺取。",
      "每处泰坦同种超凡首次需5个永恒，此后逐次增加1个，最高10个。",
      "被夺取按原投入返还，净损失2个永恒；首次保护15小时，逐次递减至最低1小时。",
      "超凡怪基础永恒掉落期望为每只0.5个；Bonus生效时为0.75或1个。",
      "修复蓝莓实际闪电伤害和零伤害显示，详情补全数值及不连锁说明。",
      "主动填写的名字自动保存，修复恢复登录途中刷新导致游客凭据丢失。",
      "蛋类详情新增召唤物生命与伤害，明确蛋壳破碎不会收回已孵化生物。"}},
    {"2026年11月4日 · 电脑地图与前序问题收尾",
     {"首页新增独立电脑地图，保留工厂；接入电脑群系、对应花瓣和转换配方。",
      "新增红宝石：闪电独立判定追加打击，仅最强生效，追加次数有上限。",
      "机械导弹无触角也会直线发射；修复电池与闪电在慢帧或目标移动时不结算伤害。",
      "铲子充能后防御可钻地2秒，普通怪失去目标；蠕虫和沙漠蜈蚣仍能攻击，不能穿墙。",
      "超凡怪相对此前线上血量、伤害、护甲各翻倍；经验、移速、掉率不变，花粉不改。",
      "超凡固定消耗5个永恒，被夺取返还4个；每账号花园与丛林合计最多5份，超限旧物保留。",
      "包含此前待发布的非全屏横屏触控坐标、木棍不可摧毁和分裂身体奖励归属修复。",
      "保留名字、登录和游客凭据；不再开屏展示公告，左上角可查看中文更新日志。"}},
    {"2026年11月4日 · 假人测试与高阶蚁穴",
     {"测试假人新增个人总伤害，保留十秒DPS；停止攻击不清零，重新加入角色后重置。",
      "Egg等战斗召唤物可以主动锁定地图假人并持续攻击，不再只是路过时偶尔命中。",
      "修复高阶蚁穴被秒杀时不释放波次：普通蚂蚁低一阶，半血以下生成一只同阶蚁后。",
      "满怪时蚁穴波次等待空位后补出；蚁穴随从不占地图自然高阶怪的固定名额。",
      "蛋类召唤物生命提高至此前2倍，伤害不变；蚂蚁蛋和甲虫蛋孵化前1秒受保护。",
      "移动端改为完整帧一次上屏并限制最高2倍像素密度，减少黑白闪屏和显存压力。"}},    {"2026年11月4日 · 转换机预告与超凡怪调整",
     {"永恒怪生命提高为此前线上2倍，其他属性不变；超凡怪仍按单独调整。",
      "超凡花瓣包括花粉：伤害、生命、治疗、闪电和加血等战斗成长控制在永恒1.5倍内；数量、冷却和触发机制不变。",
      "转换机补齐参考全部配方，新增1个齿轮加20个同阶碎片换1个同阶锯片；每20分钟依次轮换。",
      "转换界面显示下一配方和倒计时，站在旁边不再拖延刷新。",
      "超凡怪相对上一版血量乘6，伤害和护甲乘2；经验、掉率、移速不变。",
      "超凡怪造成有效伤害即可参与战利品资格，仍保留名额上限。",
      "分裂切换身体保留正在持续的移动与防御；失效击杀者不再抹掉其他合格贡献者的战利品。"}},
    {"2026年11月4日 · S刷怪区与锤子材料修正",
     {"S怪不再显示首领大血条或全服出现／击杀播报；正常地图高威胁区最多额外8只S，小地图标S。",
      "S怪生命为此前一半，基础20次独立掉落并保留掉S；死亡30秒补位，不替换存活怪。",
      "15个S碎片兑换1小时限时E锤，多把独立计时、离线照算，不能用于H争夺。",
      "10个E碎片锻造H锤，固定耗10、被夺返9；首保15小时逐次递减，最低1小时。",
      "H锤计入两处泰坦合计最多5份H；花园与丛林独立，同种可各一份。"}},

    {"2026年11月4日 · 战斗机制与画面优化",
     {"限时永恒锤消耗15个S碎片且不返还；E/H锤闪电伤害减半，超凡锤不接受限时锤作为材料。",
      "工厂、电脑地图各固定一台永恒转换机，不占自然高阶怪名额。",
      "缩小野怪体型并保留质量；修复豌豆异常击飞、Wax挤怪及蛋类跨墙孵化。",
      "补齐胡蜂弹药与发射行为、丛林蜈蚣身体；白蚁丘限制同阶重复堆积，保留有人洞穴。",
      "蜘蛛网采用复用绘制缓存，降低大量蛛网场景绘制开销。",
      "垃圾袋恢复原版袋身，内容物柔和显示并由袋口包裹；可见内容和掉落不变。",
      "骰子同步真实1至6点结果；首页新增GitHub开源链接。",
      "商店价格统一上调至此前1.75倍，批量购买和锁定商品沿用统一定价。"}},

    {"2026年11月4日 · 商店锁定与输入修复",
     {"锁定保存完整商品，更新内容池不再换货；恢复更新前仍保持锁定玩家的原商品。",
      "修复退格一次删除两个字，电脑输入法候选框跟随光标定位。"}},

    {"2026年11月5日 · 商店、议会与操作更新（补记）",
     {"商店取消购买数量限制；议会采用等级、活跃时长与花瓣权重。",
      "新增 Q/E 和触控转速调整；维护期间保留有效加成剩余时间。",
      "调整 S 区密度、召唤生存和电脑配件组合；改进地图入口与垃圾袋美术。"}},
    {"2026年11月6日 · 玩法与体验更新（14:45 已上线，补记）",
     {"垃圾袋恢复内容展示；C++ 获取机会降低，伤害和生命调整。",
      "修正分裂魔力容量、分身回魔与贴墙落点；Egg 补充缺失宠物并分离站位。",
      "加强超凡召唤与法老王冠，补充宠物伤害和移动速度天赋。",
      "蜂蜜可投掷引怪，新增陨石机制；修正铲子、海绵与超凡蠕虫行为。",
      "增加 S 刷怪区、蚁区补位、海洋泡泡及下水道苍蝇。",
      "降低花粉等发射物负载；无仇恨蜘蛛不放网，小 JS 撞墙消失。",
      "传送回对应入口；火蚁穴被秒杀仍产出守军。",
      "议会投票限时24小时、新议题在前，授权管理者可提前结束。",
      "增加泰坦解除入口、图腾层数、低清入口，修正聊天输入和移动端列表滚动。",
      "调整转换配方，修正电网、红宝石、三眼、铀与野怪友伤。",
      "粉末各稀有度加速差异化，与泡泡叠加；魔力数值和装备说明同步。",
      "分服尚未开放；手机真机兼容性仍持续收集反馈。"}},
    {"2026年11月6日 · 机械触角黑屏紧急修复",
     {"修复 S/E/H 机械触角导致镜头缩放变成负数、进地图或切分裂后黑屏。",
      "异常镜头值自动恢复安全缩放，无需重置账号或丢弃装备。",
      "普通缩小视角最低为100%，额外视野由触角装备和天赋提供。",
      "同步开放双服，账号进度共享；在线列表显示所在服务器。",
      "永恒及以上 Boss 公告跨服同步；非本地图只提示已在某处生成。",
      "补齐左上角更新日志；黑屏留言附可跳转的修复进度。"}},

    {"2026年11月7日 · 入口与体验修复（凌晨版）",
     {"恢复原网址入口，保留游客凭据和账户找回页面。",
      "修复工厂、电脑、竞技场和超级地图的大厅背景。",
      "修正多处传送门和落点，工厂转换机覆盖普通至永恒。",
      "修正排行榜和在线名单滚轮，聊天滚动与设置互不干扰。",
      "压缩账户面板，修正回蓝天赋拖动范围和合成数量控件。",
      "K/L 配装各自记忆天赋，升阶资格共用。设置可显示自身数值。",
      "高阶怪血条显示百分比，视野外闲置普通怪减少模拟工作。",
      "分服回退为单服，保留原网址与账号进度。"}},
    {"2026年11月7日 · 地图扩展与进服修复",
     {"保留原网址。修复经主入口连接时，新游客共用注册额度的问题。",
      "下载队列能自动重试，票据过期自动恢复；仍按顺序分配下载名额。",
      "资源下载、引擎启动、进入世界分别反馈状态。",
      "地图长宽各扩大两倍，地形、碰撞、出生点、传送门和装饰同步调整。",
      "竞技场和每日迷宫一同扩大。保持 20 TPS，不设玩家人数上限。",
      "降低普通怪密度、远景同步频率和高频齐射负载。",
      "密集掉落显示轻量标记；近处优先显示花瓣图标。",
      "豌豆、蓝莓和葡萄降低弹速并提高耐久，豌豆和葡萄延长发射间隔。",
      "豌豆和葡萄提高单发伤害。永恒及以上蓝莓单发闪电伤害降低 25%。",
      "大厅地图代表花瓣从左侧飞入。议会新议题开放给排名前 20。",
      "超级合成提示仅自己可见，只显示有产出的花瓣。",
      "手机聊天移到左下按钮右侧，滑动消息不弹键盘。",
      "手机天赋树拖动更灵敏，轻点聊天输入栏即可打字。",
      "手机点玩家议会在当前页面打开，支持返回原网址。",
      "打通电脑地图 JavaScript 刷怪区的五个封闭隔间。",
      "主动放弃超凡后，需等其他玩家接手才能重新争夺。",
      "组队码改为三位纯数字，有效码可直接从原队换到目标队。",
      "两种木棍召唤的沙暴伤害提高 50%。魔法木棍耗蓝减少 30%。沙暴在身边生成。"}},
    {"2026年11月7日 · 一服与二服同步更新",
     {"一服与二服同步更新，各服存档独立。",
      "开屏指引列出三个入口、账号找回说明与 QQ 群。",
      "野怪体积统一缩小。同种超凡约等于更新前的永恒。",
      "多人围战减少索敌、碰撞和掉落结算的重复计算。",
      "掉落堆叠只索引当帧发生击杀的地图，保留领取归属。",
      "后台编码和写盘，减少定时存档对游戏的影响。",
      "增加服务器帧耗时、怪物、弹幕和掉落数量采样。",
      "超凡花瓣补齐专属机制，详情可查看效果和神秘提示。",
      "泰坦选材时可预览机制。低阶拟态无法复制这些效果。",
      "超凡触角为基础视野的 11 倍，永恒为 10 倍。",
      "纳扎尔护符换用蓝白邪眼图标。",
      "陨石落地后阻挡野怪走动、击退和挤碰穿越。",
      "转速盘优先贴装备栏，触屏时避开摇杆和攻击区。",
      "连续签到奖励为 500、1000、1500、2000、2500 星。",
      "已领过当天签到的玩家仍可查看原实际到账数。",
      "工厂和电脑转换机分开站位。S 锯片改用 28 碎片和 2 齿轮。",
      "声望图自然刷出的究极怪有 25% 概率升为超级。",
      "天赋点随战斗升级同步，地狱蜘蛛移动时会迈步。",
      "客户端资源按文件内容缓存，更新时复用未变化的文件。",
      "下载中断可续传，启动失败可重新获取资源。"}},
    {"2026年11月7日 · 下载与玩法追加更新",
     {"一服与二服同步更新，各服存档独立。",
      "客户端支持从静态镜像分段下载。中断的分段可以回到源站继续。",
      "大量掉落时，快照和拾取只处理相关区域。掉落数量和拾取归属按原规则结算。",
      "追加十种超凡花瓣专属效果，详情可查看对应的神秘提示。",
      "更新日志与通知的滚轮方向与背包一致。",
      "地狱七个混刷区提高地狱蜈蚣的出现概率。"}},

    {"2026年11月8日 · 超凡与战斗修复",
     {"补齐超凡花瓣的专属机制与神秘提示，永恒和超凡雷神之锤加强。",
      "翻转防御时按攻击可发射飞船，蜂蜜可在移动中投出。",
      "法老王冠在窄处也能召唤。衣鱼修正朝向，移动时摆腿。",
      "恢复野怪体型，去除血条百分比，降低垃圾袋出红宝石的概率。"}},
    {"2026年11月8日 08:00 · 双服同步更新",
     {"闪电碰到目标即连锁。修复丛林蜈蚣离屏后只剩头部的问题。",
      "普通和声望金字塔每四小时换地形，玩家回到新图出生点。",
      "魔眼戴在花朵的两只眼睛上。法力宝珠在各攻击姿态下贴近花朵。",
      "陨石可在攻击或防御时投放。增加回血和吸收经验，灯泡可有限叠加。",
      "提高工厂最深处刷怪密度和声望地图的超级怪比例。",
      "新增 Wave 五波试炼与木桩伤害统计。"}},
    {"2026年11月8日 10:30 · 刷怪与索敌减负",
     {"缩小刷怪补位的检查范围，复用蜂蜜诱饵的索敌索引。"}},
    {"2026年11月8日 · 一服二服更新",
     {"泰坦新增金色永久超凡兑换，使用同种永恒花瓣，雷神之锤、限时锤、碎片及血祭不参与。",
      "新增木桩伤害榜和 Wave 入口，修复手机界面缩放、等级文字、工艺滚动条、怪物朝向及返回大厅残影。",
      "普通和声望金字塔同步四图轮换，修正入口、出口和当轮刷怪。调整声望丛林补怪、永恒三角、Digger 和超凡奖励。",
      "减少密集战斗的接触、索敌、移动及快照队列开销。"}},
    {"2026年11月8日 · 一服二服追加更新",
     {"优化服务端 tick 唤醒和密集弹幕的碰撞检测，减少战斗高峰的等待。",
      "Wave 试炼改为无限波次，符合参与条件每波可领 7,500 星星，每日最多 150,000，离场时显示结算。",
      "设置、账户、物品列表、在线名单等增加可用鼠标或触屏拖动的滚动条。",
      "声望下水道的自然刷怪目标密度降低约一成。"}},
    {"2026年11月8日 · 侏罗纪与战斗减负",
     {"从丛林进入普通或声望侏罗纪，探索蕨林、河岸与火山。",
      "金字塔轮换沿用原有美术，调整迷宫与刷怪区。",
      "减少密集接触、弹幕生成和闪电命中的重复计算。",
      "高负载时进入慢速保护，输入与界面保持及时反馈。",
      "骨头生命和初始护甲提高 20%，二十面体护甲同步。",
      "挖掘者锯齿动画放慢，法力宝珠不可被摧毁。",
      "扩大神谕等 NPC 的服务切换范围。",
      "商店按原分档成本加价 25%，调整月亮石定价。",
      "会长可在成员行转让，或任命仅能邀人的副会长。",
      "Wave 普通波怪有 25% 机会进入正常掉落结算。",
      "Wave 永恒与超凡掉落各有冷却，超凡从第 10 波起掉落。",
      "Wave 内究极、超级陨石落地生命降低 20%。",
      "修复声望下水道深区补怪和近处怪物唤醒。",
      "固定怪会挡住玩家，生效的灯泡可吸引丛林瓢虫。",
      "超凡腐化解除后 8 秒内仍可被反击。",
      "超凡腐化相关 PvP 死亡后可回出生点继续。"}},
    {"2026年11月8日 · 月岩定价与丛林刷新",
     {"提高商店月岩价格。究极月岩标价 9,000 星，超级月岩标价 45,000 星，折扣按标价计算。",
      "普通和声望丛林清怪后优先在附近补位，普通丛林最深两处刷怪带密度提高 20%。",
      "丛林瓢虫会主动索敌，灯泡仍可扩大索敌范围。"}},
    {"2026年11月9日 · 波次、配装与掉落显示",
     {"普通与声望下水道的挖掘者出现概率减半。挖掘者基础生命由 250 降至 200，基础伤害由 30 降至 24。",
      "重绘灯泡和蜡块。骰子环绕时会立体翻转，黄蜂系怪物会在尾部显示待发射弹体。",
      "HUD 显示第二次机会和绷带剩余时间，Wave 结算解释星星为零的原因。",
      "统一血祭的 Boss 名额判断，召唤失败会退回花瓣。修正玩家和怪物与蜡块的推挤。",
      "提高电脑端天赋盘滚轮灵敏度，将后段的输出、续航和进度天赋分组排紧。",
      "Wave 顶部显示准备和波次倒计时，怪物分批进场，可靠近倒地队友救援。每五波只出一只首领，另有泡泡和沙尘暴奖励波，参与通关可领永恒限时锤。",
      "侏罗纪新增化石采集路线和河道桥，迅猛龙会跃击、翼龙会远射、霸王龙会蓄势冲锋。",
      "丛林超级木桩支持 10 秒排位测试，历史成绩和配装默认私密，可单独公开或撤回。",
      "修正蚁穴火蚁贴墙和受推挤时的墙体碰撞。",
      "自然刷怪点被玩家挡住时，会在同一区域找安全位置补怪。高阶锯片接触伤害提高 35%，各档锯片重装更快。",
      "超凡与永久超凡法老王冠改为召唤永恒木乃伊甲虫。公会成员的完整地图名显示在名字下方。",
      "放宽近处低阶掉落的显示，究极及以上掉落默认显示粒子。新版客户端直接下载游戏资源。"}},
    {"2026年11月9日 · 花瓣、皮肤与战斗优化",
     {"魔法棉花现在会消耗法力吸收伤害，花瓣提示显示每点法力能吸收的伤害。",
      "法老王冠说明现在会显示实际召唤稀有度。永恒召唤超级木乃伊甲虫，超凡与永久超凡召唤永恒。",
      "皮肤工坊新增内置皮肤页，所有玩家都可预览、装备南瓜和机器人，或切回默认外观。",
      "简化游戏加载页，保留下载进度、重连、低清模式和服务器指引。",
      "修正超凡法老甲虫的掉落。永恒王冠纳入原有永恒掉落额度，图鉴同步显示概率。",
      "永恒、超凡和永久超凡蓝莓的单颗闪电伤害降低 35%。",
      "调整超凡花瓣机制。魔法叶在低法力时加快自身回复，法力宝珠、玫瑰和贝壳蓄力更快，缺一点也能补。魔法棉花提高防御时的法力吸收效率。",
      "优化炸弹和花瓣的接触查找，减少密集战斗中重复的碰撞检查。"}},
    {"2026年11月9日 · 商店、Wave 与操作体验",
     {"修正第一段加载画面的花瓣旋转，五枚基础花瓣会始终绕着花脸。",
      "泰坦的永久永恒页可一键锻造背包中全部符合配方的超级材料，完成后显示产量和耗材。",
      "连续签到奖励提高为 10,000、20,000、30,000、40,000、50,000 星。",
      "修正 Egg 格子叠图和 Light 的中文名，并修复旧版浏览器无法发起下载排队请求的问题。",
      "提高声望地狱最深区的超级怪比例，明确超凡骨头的防御提示。",
      "超凡骰子对野怪的非暴击接触伤害提高 25%，机甲导弹提示显示受召唤物伤害天赋加成的单发伤害。",
      "电脑按 M 放大或收起小地图，移动端点小地图即可放大。",
      "丛林最深区在同一房间缺怪时会从原刷怪区补位。纳扎尔甲虫和闪光瓢虫会主动索敌，有实体的静止怪恢复玩家碰撞。",
      "投出的蜂蜜比同阶陨石更耐打，8 秒后自然消失。吸收页的经验进度和预计收益分开显示。",
      "骨头护甲受击不再衰减。高阶锯片与超凡岩石接触野怪时可追加一次半伤害，也会承受额外反伤。",
      "普通地狱深区增加刷怪密度并缩短补怪时间。电池会保留放电间隔内的一次实际受伤触发。",
      "装备触角等视野花瓣时，镜头倍率平滑切换；超广视野等待整帧绘制。小幅转向和摇杆推力变化会更早发给服务端。",
      "商店购买确认框可以直接输入数量，并显示将扣除的星星总额。",
      "缩小移动端转速盘的外观，保留加减键的触控区域。",
      "每日掉落 Bonus 可随时暂停和继续。慢速保护期间，在线打图时的倒计时按模拟进度消耗。",
      "Wave 面板新增房间人数和每日星星上限指示灯。每轮二十波中的第十七波为黄金叶虫奖励波，仍共用每日一把限时锤的上限。",
      "减少登录时的重复密码校验，降低后台存档和在线名单的开销。"}},
    {"2026年11月10日 · 三服恢复更新",
     {"新皮肤保存为草稿，提交并审核通过后才能穿戴或公开。",
      "皮肤工坊支持授权人员在线下架公开皮肤。授权人员可用 /server-status 查看当前服的在线人数和 TPS。",
      "商店购买确认框可以直接输入数量，例如 330。",
      "公会标签取消 5 位要求，支持更长或更短的英文字母及数字标签。",
      "略微回调永恒及以上蓝莓的单颗闪电伤害，较当前提高约 7.7%，装填和耗蓝沿用原值。",
      "皮肤工坊新增「复制 AI 提示词」。上传参考图后可让 AI 生成画布命令，再粘贴回工坊。",
      "花瓣回收换星已移除。",
      "旧浏览器也会优先下载约 6.7 MB 的压缩游戏资源。",
      "开屏服务器指引突出今后的正式访问域名 ashflor.com，请提前收藏。",
      "玩家召唤的生物最大生命提高至原来的 5 倍。",
      "电池说明区分 0.5 秒放电间隔和用完 3 格后的重装时间。",
      "修正第一段加载画面的花瓣旋转，五枚基础花瓣会始终绕着花脸。",
      "泰坦的永久永恒页可一键锻造背包中全部符合配方的超级材料，完成后显示产量和耗材。",
      "连续签到奖励提高为 10,000、20,000、30,000、40,000、50,000 星。",
      "修正 Egg 格子叠图和 Light 的中文名，并修复旧版浏览器无法发起下载排队请求的问题。",
      "提高声望地狱最深区的超级怪比例，明确超凡骨头的防御提示。",
      "超凡骰子对野怪的非暴击接触伤害提高 25%，机甲导弹提示显示受召唤物伤害天赋加成的单发伤害。",
      "电脑按 M 放大或收起小地图，移动端点小地图即可放大。",
      "丛林最深区在同一房间缺怪时会从原刷怪区补位。纳扎尔甲虫和闪光瓢虫会主动索敌，有实体的静止怪恢复玩家碰撞。",
      "投出的蜂蜜比同阶陨石更耐打，8 秒后自然消失。吸收页的经验进度和预计收益分开显示。",
      "骨头护甲受击不再衰减。高阶锯片与超凡岩石接触野怪时可追加一次半伤害，也会承受额外反伤。",
      "普通地狱深区增加刷怪密度并缩短补怪时间。电池会保留放电间隔内的一次实际受伤触发。",
      "装备触角等视野花瓣时，镜头倍率平滑切换；超广视野等待整帧绘制。小幅转向和摇杆推力变化会更早发给服务端。",
      "商店购买确认框可以直接输入数量，并显示将扣除的星星总额。",
      "缩小移动端转速盘的外观，保留加减键的触控区域。",
      "每日掉落 Bonus 可随时暂停和继续。慢速保护期间，在线打图时的倒计时按模拟进度消耗。",
      "Wave 面板新增房间人数和每日星星上限指示灯。每轮二十波中的第十七波为黄金叶虫奖励波，仍共用每日一把限时锤的上限。",
      "减少登录时的重复密码校验，降低后台存档和在线名单的开销。"}},
    {"2026年11月10日 · 召唤、议会与服务器体验",
     {"普通 Stick 的补召间隔缩短到 3 秒，魔法木棍会给常驻 Stick 保留缺员名额。",
      "侏罗纪入口和化石路线增加恐龙刷新，入口可遇到剑龙。",
      "修复声望地狱在常规刷怪额度用满时容易不出怪的问题，提高中深区超级怪比例。",
      "死亡面板只统计服务端实际入账的掉落，修正分身拾取和异常特效造成的数量误报。",
      "超凡魔法泡泡按移动方向冲刺，推力提高 10%。",
      "简化账户登录文案，聊天过滤补充少量明确的中文辱骂短语。",
      "泰坦超凡争夺在后台准备存档，确认写入后才结算归属。改密时的密码计算也移到后台。",
      "恢复蜂蜡原版的背包和落地图案。",
      "三服玩家议会共用排行榜与议题；服务器指引可使用无端口地址，留言板直达北京。"}},
    {"2026年11月10日 · 蓝莓、铲子与 Wave",
     {"蓝莓每颗耗蓝翻倍，耐久提高 20%。",
      "修正轻花瓣的子瓣数值。伤害和耐久由同槽的子瓣分摊。",
      "铲子下潜时暂停主动花瓣攻击，可穿过地面怪物。出土后至少间隔 3 秒才能再下潜。",
      "转换机按实际时间每 10 分钟切到预告配方。",
      "Wave 从第 11 波起提高怪物生命、接触伤害及通关星星。",
      "Wave 内击杀可触发图腾叠层和回血，离开房间时清除叠层。",
      "电池受到附近玩家有效攻击时会向攻击者放电。",
      "公会可设置两名副会长，副会长可邀请成员。",
      "恢复蛋和甲虫蛋重瓣在装备槽里的扇形图标。",
      "修复部分旧版浏览器下载后绘制中断，加载页保留重试和低清入口。",
      "切换到键盘移动时隐藏鼠标移动箭头。公会创建页补充标签可用字符的提示。",
      "游戏内排行榜短时间复用查询结果，减少连开榜单和翻页时的服务器开销。"}},

    {"2026年11月12日 · 网络、议会与玩法修复",
     {"设置中隐藏其他玩家的花瓣或召唤物后，服务端也会减少发送对应实体和闪电特效。拥堵时暂停生成更多世界快照，成批到达的控制输入优先采用最新一条。",
      "普通地图可见范围的自然补怪等待上限由 12 秒降到 8 秒。",
      "皮肤工坊可按皮肤名或作者搜索，授权审核可查看和处理三服提交的作品。",
      "游戏内等级、有效游玩时间和超凡持有榜显示自己的整榜名次。议会登录和投票减少等待。",
      "修复争夺超凡时存档同时更新导致的取消。一键锻造结果保留在操作提示和通知中。",
      "声望图部分自然补位的超级生物有机会升为永恒。修正静止生物累积和深区补怪。",
      "修复服务器繁忙时声望工厂最深处一直没有怪的问题。",
      "电池换成棕黑外壳和银灰接头，重瓣按扇形排列，并显示剩余放电格数。金叶提供的闪电重装缩短更准确。",
      "细长花瓣的重瓣采用扇形排列。装备栏的多粒三角花瓣朝内排列。",
      "魔法木棍沙暴的体伤提高 25%，永恒和超凡三角的同阶叠伤提高 25%。",
      "召唤物可以追到离主人更远的野怪，沙暴跟随距离放宽。",
      "旧 iPad 默认以低清模式启动，减少进入游戏时的画面开销。",
      "蜂蜜基础耐久为同级陨石的两倍，投出后持续 12 秒，小蚂蚁会优先追向视线内的诱饵。",
      "两种训练木桩的体伤统一到同级中档，限时锤对野怪的闪电伤害提高。"}},
    {"2026年11月12日 · 泰坦争夺、木桩与皮肤审核",
     {"修复旧持有人持续获得经验时，合法的泰坦超凡争夺反复提交失败。",
      "木桩由服务端按实际命中计算个人累计伤害和最近 10 秒 DPS，网络积压恢复后可补齐读数。",
      "修复点开跨服皮肤审核时可能导致服务器进程退出的问题。"}},

};

const std::vector<ChangelogEntry>& changelog() {
    struct Entries {
        std::string title;
        std::vector<std::string> changes;
        std::vector<ChangelogEntry> rows{std::begin(kChangelog), std::end(kChangelog)};

        Entries() {
            Json notes;
            std::string error;
            if (!Json::parse(kReleaseNotesJson, notes, error)) return;
            title = notes["title"].asString();
            if (title.empty()) return;
            for (const auto& line : notes["changes"].items()) {
                if (line.isString() && changes.size() < kMaxChanges) changes.push_back(line.asString());
            }
            ChangelogEntry release{title.c_str(), {}};
            for (std::size_t i = 0; i < changes.size(); ++i) release.changes[i] = changes[i].c_str();
            rows.push_back(release);
        }
    };
    static const Entries entries;
    return entries.rows;
}

int changeCount(const ChangelogEntry& entry) {
    int count = 0;
    while (count < kMaxChanges && entry.changes[count] != nullptr) ++count;
    return count;
}

double entryHeight(const ChangelogEntry& entry) {
    return kDatePitch + kBulletPitch * changeCount(entry) + kEntryGap;
}

/// One clickable link, recorded while painting so the click pass has the same
/// geometry the glyphs were drawn at. Recorded UNCLIPPED, as the reference
/// does; `render` gates the hit on the content rect instead.
struct LinkHit {
    Rect rect;
    std::string url;
};

/// A run of a change line: literal text, or a link when `url` is set.
struct Segment {
    std::string text;
    std::string url;
};

/// Thumb-drag state for the scrollbar.
///
/// At file scope because it lives entirely between one press and the release
/// that ends it, and because the panel's own declaration sits in a header
/// every panel shares -- widening it for one panel's transient would cost
/// every other panel a recompile and tell a reader nothing.
struct ThumbDrag {
    bool active = false;
    double startY = 0;
    double startOffset = 0;
};

ThumbDrag& thumbDrag() {
    static ThumbDrag drag;
    return drag;
}

bool isSpaceByte(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool hasScheme(const std::string& url) {
    const auto starts = [&url](const char* prefix) {
        const std::size_t n = std::char_traits<char>::length(prefix);
        if (url.size() < n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (std::tolower(static_cast<unsigned char>(url[i])) != prefix[i]) return false;
        }
        return true;
    };
    return starts("http://") || starts("https://");
}

std::string normalizeUrl(const std::string& raw) {
    return hasScheme(raw) ? raw : "https://" + raw;
}

/// `new URL(u).host + pathname`, with a trailing slash trimmed: a link is
/// shown by where it goes, not by how it was written.
std::string linkLabel(const std::string& raw) {
    const std::string url = normalizeUrl(raw);
    const std::size_t afterScheme = url.find("//");
    std::string rest = afterScheme == std::string::npos ? url : url.substr(afterScheme + 2);
    // Neither the query nor the fragment is part of a host or a pathname.
    rest = rest.substr(0, rest.find_first_of("?#"));
    if (rest.size() > 1 && rest.back() == '/') rest.pop_back();
    return rest;
}

/// Splits a change line on `link:<token>`, exactly as the reference's regex
/// does. A bare "link:" with no token after it is not a match and stays text.
std::vector<Segment> parseChange(const std::string& change) {
    std::vector<Segment> segments;
    std::size_t last = 0;
    std::size_t from = 0;
    while (true) {
        const std::size_t at = change.find("link:", from);
        if (at == std::string::npos) break;
        std::size_t end = at + 5;
        while (end < change.size() && !isSpaceByte(change[end])) ++end;
        if (end == at + 5) {
            from = end;
            continue;
        }
        if (at > last) segments.push_back({change.substr(last, at - last), {}});
        const std::string raw = change.substr(at + 5, end - at - 5);
        segments.push_back({linkLabel(raw), normalizeUrl(raw)});
        last = from = end;
    }
    if (last < change.size()) segments.push_back({change.substr(last), {}});
    if (segments.empty()) segments.push_back({change, {}});
    return segments;
}

/// The one glyph in the build that is filled and THEN stroked.
///
/// The reference paints the bullet with fillText followed by strokeText, so
/// its black outline sits ON TOP of the white dot instead of behind it. At
/// 14px that outline is most of what the bullet looks like, which is why the
/// order is reproduced rather than routed through ui::text().
void drawBullet(Canvas& canvas, double x, double y, double strokeWidth) {
    TextStyle style;
    style.size = kChangeSize;
    style.fill = kPaper;
    style.stroke = kInk;
    // Clamped, not passed through: a negative strokeWidth means "derive one"
    // to paintRun, where here it has always meant "no outline".
    style.strokeWidth = std::max(0.0, strokeWidth);
    paintRun(canvas, "•", x, y + (ascent(kChangeSize) + descent(kChangeSize)) * 0.5, style,
             1.0, 1.0, /*fillFirst=*/true);
}

/// Paints one change line. `strokeWidth` is the BULLET outline, threaded by
/// reference because a link underline leaves it at 1 for every bullet after
/// it, which the reference does by leaking ctx.lineWidth. The line's own text
/// takes the shared outline.
void drawChange(Canvas& canvas, const std::string& change, double x, double y,
                double& strokeWidth, std::vector<LinkHit>& links) {
    double penX = x;
    for (const Segment& segment : parseChange(change)) {
        if (segment.text.empty()) continue;
        const double width = measure(segment.text, kChangeSize);

        TextStyle style;
        style.size = kChangeSize;
        style.fill = segment.url.empty() ? kPaper : kLinkFill;
        text(canvas, segment.text, penX, y, style);

        if (!segment.url.empty()) {
            canvas.save();
            setStroke(canvas, kLinkFill);
            canvas.setLineWidth(1.0f);
            canvas.beginPath();
            canvas.moveTo(static_cast<float>(penX), static_cast<float>(y + 17.0));
            canvas.lineTo(static_cast<float>(penX + width), static_cast<float>(y + 17.0));
            canvas.stroke();
            canvas.restore();
            strokeWidth = 1.0;
            links.push_back({Rect{penX, y, width, 18.0}, segment.url});
        }
        penX += width;
    }
}

} // namespace

int changelogReleaseCount() { return static_cast<int>(changelog().size()); }

double ChangelogPanel::preferredWidth() { return 600.0; }

void ChangelogPanel::reset() {
    scroll_ = {};
    thumbDrag() = {};
}

bool ChangelogPanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Rect panel = ctx.bounds;
    const Vec2 mouse = ctx.mouse();

    const double viewport = panel.h - kHeaderHeight;
    double contentHeight = 0;
    const auto& entries = changelog();
    for (const ChangelogEntry& entry : entries) contentHeight += entryHeight(entry);
    scroll_.contentHeight = contentHeight;
    scroll_.viewHeight = viewport;
    const double maxScroll = scroll_.maxOffset();
    const bool scrollable = contentHeight > viewport;

    const Rect track{panel.right() - kScrollbarWidth - 5.0, panel.y + kHeaderHeight,
                     kScrollbarWidth, panel.h - kHeaderHeight - 5.0};

    if (panel.contains(mouse)) scroll_.offset -= ctx.wheel() * kWheelStep;
    // Let the list follow the finger throughout the body, outside the thumb track.
    const Rect body{panel.x, panel.y + kHeaderHeight, track.x - panel.x, panel.h - kHeaderHeight};
    scroll_.offset -= touchScroll(ctx.window, body, scrollable);

    ThumbDrag& drag = thumbDrag();
    if (ctx.pressed() && scrollable && track.contains(mouse)) {
        drag.active = true;
        drag.startY = mouse.y;
        drag.startOffset = scroll_.offset;
    }
    if (!ctx.window.mouseDown(MouseButton::Left)) drag.active = false;
    if (drag.active) {
        // The reference maps the drag over panelHeight - 45, not over the
        // track: the thumb runs slightly ahead of the cursor by design.
        const double ratio = (mouse.y - drag.startY) / (panel.h - 45.0);
        scroll_.offset = drag.startOffset + ratio * maxScroll;
    }
    scroll_.offset = clamp(scroll_.offset, 0.0, maxScroll);

    panelCard(canvas, panel, kChangelogSkin);

    TextStyle heading;
    heading.size = 20.0;
    heading.fill = kPaper;
    heading.baseline = Baseline::Top;
    text(canvas, menuTitle(MenuId::Changelog), panel.x + kPadding, panel.y + kPadding, heading);

    // No hover and no press state, exactly as the reference pill has none.
    const Rect closeRect = closeButtonRect(panel);
    panelClose(canvas, closeRect, closeRect.contains(mouse));

    const Rect view{panel.x + kPadding, panel.y + kHeaderHeight, panel.w - kPadding * 2,
                    panel.h - kHeaderHeight - kPadding};

    canvas.save();
    roundPath(canvas, view, 8.0);
    canvas.clip();

    // The date strip yields its right edge to the scrollbar when there is one.
    const double stripWidth = view.w - (scrollable ? kScrollbarWidth + 5.0 : 0.0);
    double contentY = view.y + kPadding - scroll_.offset;
    std::vector<LinkHit> links;

    // Newest first: the table is stored oldest-first and read backwards.
    for (auto row = entries.rbegin(); row != entries.rend(); ++row) {
        const ChangelogEntry& entry = *row;
        const double height = entryHeight(entry);
        // A row's glyphs reach past its pitch, so the cull keeps a line of
        // slack on each side rather than trusting the box.
        if (contentY + height < view.y - kBulletPitch || contentY > view.bottom() + kBulletPitch) {
            contentY += height;
            continue;
        }

        setFill(canvas, kPaper, 0.05);
        roundPath(canvas, Rect{view.x, contentY - 5.0, stripWidth, 10.0}, 8.0);
        canvas.fill();

        TextStyle date;
        date.size = 20.0;
        date.fill = kPaper;
        text(canvas, entry.date, view.x, contentY, date);
        contentY += kDatePitch;

        // The first bullet of an entry is outlined at 1px and the rest at
        // 0.5, which is what makes the top bullet of every group read darker.
        double strokeWidth = 1.0;
        const int changes = changeCount(entry);
        for (int c = 0; c < changes; ++c) {
            drawBullet(canvas, view.x, contentY, strokeWidth);
            strokeWidth = 0.5;
            drawChange(canvas, entry.changes[c], view.x + kBulletIndent, contentY, strokeWidth,
                       links);
            contentY += kBulletPitch;
        }
        contentY += kEntryGap;
    }
    canvas.restore();

    if (scrollable) {
        setFill(canvas, kPaper, 0.1);
        roundPath(canvas, track, 5.0);
        canvas.fill();
        // No minimum thumb height: on a 7896px list the reference's thumb is
        // 26px, and clamping it to a comfortable size would move it off the
        // scroll position it is meant to report.
        const double thumbHeight = (viewport - 5.0) * viewport / contentHeight;
        const double travelled = maxScroll > 0 ? scroll_.offset / maxScroll : 0.0;
        setFill(canvas, kBodyBorder);
        roundPath(canvas,
                      Rect{track.x, track.y + travelled * (track.h - thumbHeight),
                           track.w, thumbHeight},
                      5.0);
        canvas.fill();
    }

    if (!ctx.released()) return true;
    if (closeRect.contains(mouse)) return false;
    // Link rects are recorded unclipped, so the hit is gated on the body the
    // way the reference gates it.
    if (view.contains(mouse)) {
        for (const LinkHit& link : links) {
            if (!link.rect.contains(mouse)) continue;
            openExternalLink(link.url);
            break;
        }
    }
    return true;
}

} // namespace flix

