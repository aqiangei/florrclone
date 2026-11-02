// UI language: Chinese / English. Default Chinese (AshFlor .161).
//
// English source strings stay as Msg keys' fallback; tr() picks the active
// language. menuTitle / controlTitle wrap the menus that already have English
// labels in kMenus / kControls.

#pragma once

#include <cstdint>

namespace flix {

enum class MenuId : std::uint8_t;
enum class ControlAction : std::uint8_t;

namespace ui {

enum class Language : std::uint8_t { Zh = 0, En = 1 };

enum class Msg : std::uint16_t {
    // -- settings ------------------------------------------------------------
    Settings,
    TabControls,
    TabGraphics,
    TabAdvanced,
    TabCredits,
    Language,
    LanguageZh,
    LanguageEn,
    ControlsHeading,
    HoldLoadPreset,
    HoldSavePreset,
    SaveControls,
    ResetToDefault,
    NumberKeysUseItems,
    UseMouseControls,
    RequestMobile,
    MovementHelper,
    InvertAttack,
    InvertDefend,
    ClassicLoadoutBar,
    ShowHitboxes,
    HideOtherPetals,
    HideOtherPets,
    ScreenShake,
    ShowStats,
    DynamicSkybox,
    MobDeathAnimation,
    Antialiasing,
    GpuAcceleration,
    DesynchronizedCanvas,
    DisableUltraParticles,
    ShowConsoleLogs,
    RenderResolution,
    MobAnimationFps,
    Interpolation,
    ResetTutorial,
    AreYouSure,
    ServerIp,
    ShowConsoleLogsOnScreen,
    ShowAdminCommands,
    ShowAdminsOnLeaderboard,
    EnableDebugMenu,
    GrantAdmin,
    AdminGranted,

    // -- controls ------------------------------------------------------------
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Inventory,
    Crafting,
    Skills,
    ToggleMouseControls,
    ToggleHitboxes,
    ToggleDebugMenu,
    ZoomIn,
    ZoomOut,
    Chat,
    ExtendPetals,
    RetractPetals,
    SwapLoadoutRows,

    // -- menus / panels ------------------------------------------------------
    Craft,
    CraftAll,
    Talents,
    MobGallery,
    Shop,
    Skins,
    Leaderboard,
    Changelog,
    Notifications,
    Guild,
    Debug,
    Database,
    Admin,
    Assets,
    Account,
    Stack,
    NoGuild,
    Drops,
    SignedInAs,
    LogOut,

    // -- login / lobby -------------------------------------------------------
    Login,
    Register,
    Guest,
    AlreadyHaveAccount,
    PickUsernameLater,
    OnlyOneTab,
    Reconnecting,
    Ready,
    SpawnAt,
    SpawnAtPrestige,
    Resets,

    // -- in-game -------------------------------------------------------------
    Online,
    YouWereDestroyedBy,
    CollectedThisRun,
    Close,
    Continue,
    Flower,
    You,
    Unnamed,
    LobbyParen,
    Accept,
    Decline,
    Refresh,
    ClaimedDay,
    ReadyToClaim,
    NextIn,
    NextNow,
    DragPetalToEquip,
    SkinStudio,
    CreateTab,
    BrowseTab,
    GuestParen,
    RegisterOffline,
    SquadInvite,
    SquadInviteGuild,
    SquadInviteDetail,
    GuildInvite,
    SkinShapeCircle,
    SkinShapeEllipse,
    SkinShapeRect,
    SkinShapePolygon,
    SkinShapeCurve,
    SkinShapeLine,
    SkinVisualEditor,
    SkinTextEditor,
    SkinUnequip,
    SkinLivePreview,
    SkinDragHandles,
    SkinSelectedShape,
    SkinNothingSelected,
    SkinRadius,
    SkinWidth,
    SkinHeight,
    SkinRotation,
    SkinOutlineWidth,
    SkinAddPoint,
    SkinRemovePoint,
    SkinCurveHint1,
    SkinCurveHint2,
    SkinFill,
    SkinOutline,
    SkinCanvasCommands,
    SkinNamePlaceholder,
    SkinReset,
    SkinPublish,
    SkinBrowseHint,
    SkinBrowseEmpty,
    SkinBy,
    SkinEquip,
    SkinEquipped,
    SkinDeleteAsk,
    SkinCancel,
    SkinDelete,
    SkinAddShape,

    // -- chat ----------------------------------------------------------------
    ChatLocal,
    ChatGlobal,
    ChatSquad,
    ChatGuild,
    ChatWhisper,
    ChatAdmin,
    ChatPressEnter,

    Count,
};

void setLanguage(Language lang);
Language language();

/// Active-language string for `id`. Never null; falls back to English.
const char* tr(Msg id);

/// Ad-hoc English/Chinese pair for strings not worth a Msg entry.
inline const char* trEnZh(const char* en, const char* zh) {
    return language() == Language::Zh ? zh : en;
}

/// Translated panel title for a MenuId (empty for None).
const char* menuTitle(MenuId id);
/// Translated control-row caption for a ControlAction.
const char* controlTitle(ControlAction action);

} // namespace ui
} // namespace flix
