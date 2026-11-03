#include "client/ui/i18n.h"

#include "client/ui/menus.h"

namespace flix {
namespace ui {
namespace {

Language gLanguage = Language::Zh;

struct Entry {
    const char* en;
    const char* zh;
};

// Indexed by Msg. Keep in lockstep with the enum.
constexpr Entry kTable[] = {
    {"Settings", "设置"},
    {"Controls", "控制"},
    {"Graphics", "画面"},
    {"Advanced", "高级"},
    {"Credits", "致谢"},
    {"Language", "语言"},
    {"Chinese", "中文"},
    {"English", "English"},
    {"Controls", "控制"},
    {"Hold K or L + 1-0: load a saved loadout", "按住 K 或 L + 1-0：读取已保存配装"},
    {"Shift + K or L + 1-0: save your loadout there", "配装更改自动保存；Shift组合键仅用于复制"},
    {"Save Controls", "保存按键"},
    {"Reset to Default", "恢复默认"},
    {"Number Keys Use Items (off = swap loadout)", "数字键使用物品（关闭=切换配装）"},
    {"Use Mouse Controls (K toggles in-game)", "鼠标控制（游戏中按 K 切换）"},
    {"Request Mobile (touch joystick & attack/retract buttons)", "移动端操控（摇杆与攻击/收回按钮）"},
    {"Movement helper (aim line toward cursor)", "移动辅助（指向光标的指示线）"},
    {"Invert attack (hold = idle)", "反转攻击（按住=常态）"},
    {"Invert defend (hold = idle)", "反转防御（按住=常态）"},
    {"Classic Loadout Bar (smaller slots, wider gaps)", "经典配装栏（更小格子、更宽间距）"},
    {"Show Hitboxes", "显示碰撞箱"},
    {"Hide Other Players' Petals", "隐藏其他玩家花瓣"},
    {"Hide Other Players' Pets", "隐藏其他玩家宠物"},
    {"Screen Shake", "屏幕震动"},
    {"Show Performance Stats", "显示性能统计"},
    {"Dynamic Skybox", "动态天空"},
    {"Mob Death Animation", "怪物死亡动画"},
    {"Anti-aliasing", "抗锯齿"},
    {"GPU Acceleration", "GPU 加速"},
    {"Low Latency Canvas (may flicker)", "低延迟画布（可能闪烁）"},
    {"Disable Ultra+ Particles", "关闭 Ultra+ 粒子"},
    {"Show Console Logs", "显示控制台日志"},
    {"Render Resolution: ", "渲染分辨率："},
    {"Mob Animation FPS: ", "怪物动画帧率："},
    {"Interpolation: ", "插值："},
    {"Reset Tutorial", "重置教程"},
    {"Are you sure?", "确定吗？"},
    {"Server IP:", "服务器 IP："},
    {"Show Console Logs on Screen", "在屏幕上显示控制台日志"},
    {"Show Admin Commands", "显示管理员指令"},
    {"Show Admins on Leaderboard", "排行榜显示管理员"},
    {"Enable Debug Menu button (J in-game)", "启用调试菜单按钮（游戏中按 J）"},
    {"Grant Admin", "授予管理员"},
    {"Admin Granted", "已是管理员"},

    {"Move up", "向上移动"},
    {"Move down", "向下移动"},
    {"Move left", "向左移动"},
    {"Move right", "向右移动"},
    {"Inventory", "背包"},
    {"Crafting", "合成"},
    {"Skills", "天赋"},
    {"Toggle mouse controls", "切换鼠标控制"},
    {"Toggle hitboxes", "切换碰撞箱"},
    {"Toggle debug menu", "切换调试菜单"},
    {"Zoom in", "放大"},
    {"Zoom out", "缩小"},
    {"Chat", "聊天"},
    {"Extend petals", "伸出花瓣"},
    {"Retract petals", "收回花瓣"},
    {"Swap loadout rows", "切换配装行"},

    {"Craft", "合成"},
    {"Craft All", "一键合成"},
    {"Talents", "天赋"},
    {"Mob Gallery", "图鉴"},
    {"Shop", "商店"},
    {"Skins", "皮肤"},
    {"Leaderboard", "排行榜"},
    {"Changelog", "更新日志"},
    {"Notifications", "通知"},
    {"Guild", "公会"},
    {"Debug", "调试"},
    {"Database", "数据库"},
    {"Admin", "管理"},
    {"Assets", "资源"},
    {"Account", "账户"},
    {"Stack", "堆叠"},
    {"No guild", "无公会"},
    {"Drops:", "掉落："},
    {"Signed in as", "当前登录"},
    {"Log Out", "退出登录"},

    {"Login", "登录"},
    {"Register", "注册"},
    {"Guest", "游客"},
    {"Already have an account? Login", "已有账户？去登录"},
    {"Pick a username and password later, in the Account menu", "稍后可在「账户」菜单设置用户名和密码"},
    {"Only one tab can play an account at a time.", "同一账户同时只能在一个标签页游玩。"},
    {"Reconnecting...", "正在重连…"},
    {"Ready", "准备"},
    {"Spawn At:", "出生点："},
    {"Spawn At (Prestige):", "出生点（声望）："},
    {"Resets: ", "重置："},

    {"Online", "在线"},
    {"You were destroyed by:", "你被击败于："},
    {"Collected this run", "本局收集"},
    {"Close", "关闭"},
    {"Continue", "继续"},
    {"Flower", "花朵"},
    {"You", "你"},
    {"Unnamed", "未命名"},
    {"(lobby)", "（大厅）"},
    {"Accept", "接受"},
    {"Decline", "拒绝"},
    {"Refresh", "刷新"},
    {"Claimed · Day ", "已领取 · 第"},
    {"Ready to claim!", "可领取！"},
    {"Next: ", "下次："},
    {"Next: now", "下次：现在"},
    {"Drag a petal to equip it", "拖动花瓣以装备"},
    {"Skin Studio", "皮肤工坊"},
    {"Create", "创建"},
    {"Browse", "浏览"},
    {"(guest)", "（游客）"},
    {"Register Offline", "离线注册"},
    {"Squad invite", "小队邀请"},
    {"Squad invite (guild)", "小队邀请（公会）"},
    {"@%s invited you", "@%s 邀请了你"},
    {"Guild invite", "公会邀请"},
    {"Circle", "圆"},
    {"Ellipse", "椭圆"},
    {"Rect", "矩形"},
    {"Polygon", "多边形"},
    {"Curve", "曲线"},
    {"Line", "直线"},
    {"Visual editor", "可视化编辑"},
    {"Text editor", "文本编辑"},
    {"Unequip", "卸下"},
    {"Live preview", "实时预览"},
    {"Drag the handles to shape it", "拖动手柄调整形状"},
    {"Selected shape", "选中的形状"},
    {"Nothing selected — add a shape below.", "未选中 — 在下方添加形状。"},
    {"Radius", "半径"},
    {"Width", "宽度"},
    {"Height", "高度"},
    {"Rotation", "旋转"},
    {"Outline width", "描边宽度"},
    {"Add point", "加点"},
    {"Remove point", "删点"},
    {"Drag the two round handles to bend it;", "拖动两个圆点手柄弯曲曲线；"},
    {"a fill closes the curve into a blob.", "填充会把曲线收成色块。"},
    {"Fill", "填充"},
    {"Outline", "描边"},
    {"Canvas commands", "画布命令"},
    {"Skin name", "皮肤名称"},
    {"Reset", "重置"},
    {"Publish", "发布"},
    {"Published skins — equip one and everyone sees it.", "已发布皮肤 — 装备后所有人可见。"},
    {"Nothing published yet. Make one in the Create tab.", "还没有发布的皮肤。去「创建」页做一个。"},
    {"by ", "作者 "},
    {"Equip", "装备"},
    {"Equipped", "已装备"},
    {"Delete \"%s\"?", "删除「%s」？"},
    {"Cancel", "取消"},
    {"Delete", "删除"},
    {"Add shape", "添加形状"},

    {"Local", "本地"},
    {"Global", "全服"},
    {"Squad", "小队"},
    {"Guild", "公会"},
    {"Whisper", "私聊"},
    {"Admin", "管理"},
    {"Press Enter to chat...", "按 Enter 聊天…"},
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<std::size_t>(Msg::Count),
              "i18n table out of sync with Msg");

} // namespace

void setLanguage(Language lang) { gLanguage = lang; }
Language language() { return gLanguage; }

const char* tr(Msg id) {
    const auto i = static_cast<std::size_t>(id);
    if (i >= static_cast<std::size_t>(Msg::Count)) return "";
    const Entry& e = kTable[i];
    return gLanguage == Language::Zh ? e.zh : e.en;
}

const char* menuTitle(MenuId id) {
    switch (id) {
        case MenuId::Inventory: return tr(Msg::Inventory);
        case MenuId::Crafting: return tr(Msg::Craft);
        case MenuId::Talents: return tr(Msg::Talents);
        case MenuId::Gallery: return tr(Msg::MobGallery);
        case MenuId::Shop: return tr(Msg::Shop);
        case MenuId::Skins: return tr(Msg::Skins);
        case MenuId::Leaderboard: return tr(Msg::Leaderboard);
        case MenuId::Settings: return tr(Msg::Settings);
        case MenuId::Changelog: return tr(Msg::Changelog);
        case MenuId::Notifications: return tr(Msg::Notifications);
        case MenuId::Guild: return tr(Msg::Guild);
        case MenuId::Debug: return tr(Msg::Debug);
        case MenuId::AdminDb: return tr(Msg::Database);
        case MenuId::AdminDashboard: return tr(Msg::Admin);
        case MenuId::Assets: return tr(Msg::Assets);
        case MenuId::Account: return tr(Msg::Account);
        default: return "";
    }
}

const char* controlTitle(ControlAction action) {
    switch (action) {
        case ControlAction::MoveUp: return tr(Msg::MoveUp);
        case ControlAction::MoveDown: return tr(Msg::MoveDown);
        case ControlAction::MoveLeft: return tr(Msg::MoveLeft);
        case ControlAction::MoveRight: return tr(Msg::MoveRight);
        case ControlAction::Inventory: return tr(Msg::Inventory);
        case ControlAction::Crafting: return tr(Msg::Crafting);
        case ControlAction::Skills: return tr(Msg::Skills);
        case ControlAction::ToggleMouseControls: return tr(Msg::ToggleMouseControls);
        case ControlAction::ToggleHitboxes: return tr(Msg::ToggleHitboxes);
        case ControlAction::ToggleDebugMenu: return tr(Msg::ToggleDebugMenu);
        case ControlAction::ZoomIn: return tr(Msg::ZoomIn);
        case ControlAction::ZoomOut: return tr(Msg::ZoomOut);
        case ControlAction::Chat: return tr(Msg::Chat);
        case ControlAction::ExtendPetals: return tr(Msg::ExtendPetals);
        case ControlAction::RetractPetals: return tr(Msg::RetractPetals);
        case ControlAction::SwapLoadoutRows: return tr(Msg::SwapLoadoutRows);
        default: return "";
    }
}

} // namespace ui
} // namespace flix
