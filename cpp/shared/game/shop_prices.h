#pragma once
#include <string_view>

namespace flix {

struct ShopSuperPrice {
    std::string_view id;
    int stars;
};

inline constexpr ShopSuperPrice kShopSuperPrices[] = {
    {"arctic_amethyst", 5500},
    {"arctic_banana", 5500},
    {"arctic_blueberries", 8000},
    {"arctic_centipede_egg", 13500},
    {"arctic_corium", 22500},
    {"arctic_cryo_bomb", 8000},
    {"arctic_ears", 5500},
    {"arctic_electron_missile", 8000},
    {"arctic_emerald", 5500},
    {"arctic_fission", 22500},
    {"arctic_frost", 8000},
    {"arctic_frost_digger_egg", 36000},
    {"arctic_frozen_mark", 5500},
    {"arctic_fusion", 22500},
    {"arctic_ice", 5500},
    {"arctic_ice_cube_egg", 13500},
    {"arctic_ice_dragon_egg", 45000},
    {"arctic_ice_hornet_egg", 13500},
    {"arctic_ice_rose", 8000},
    {"arctic_icicle", 5500},
    {"arctic_igloo_egg", 22500},
    {"arctic_lightsaber", 5500},
    {"arctic_nitro", 5500},
    {"arctic_nitrogen", 8000},
    {"arctic_nuke", 45000},
    {"arctic_nuke_egg", 45000},
    {"arctic_pinecone", 8000},
    {"arctic_plutonium", 22500},
    {"arctic_sapphire", 5500},
    {"arctic_snow_stick", 8000},
    {"arctic_snowball", 5500},
    {"arctic_snowbush_egg", 13500},
    {"arctic_snowflake", 5500},
    {"arctic_snowman_egg", 13500},
    {"arctic_spider_cave_egg", 36000},
    {"arctic_spider_digger_egg", 36000},
    {"arctic_spider_egg", 13500},
    {"arctic_spider_yoba_egg", 13500},
    {"arctic_splitter", 22500},
    {"arctic_stalagmite_egg", 13500},
    {"arctic_stick", 8000},
    {"arctic_tick_egg", 13500},
    {"arctic_visaber", 8000},
    {"magic_golden_leaf", 22500},
};

} // namespace flix
