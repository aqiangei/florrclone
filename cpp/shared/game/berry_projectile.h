#pragma once

#include <cstdint>
#include <string_view>

namespace flix {

// Styles carried in the existing Explosion event's colour byte. Zero keeps
// the generic explosion. The high bits identify the visual owner to viewers.
enum class BerryShotStyle : std::uint8_t { None, Grape, Pea, Blueberry };
inline constexpr double kBerryLoadMillis = 150.0;
inline constexpr std::uint8_t kBerryShotStyleMask = 0x03;
inline constexpr std::uint8_t kBerryShotFromPet = 0x40;
inline constexpr std::uint8_t kBerryShotForeign = 0x80;

inline BerryShotStyle berryShotStyle(std::string_view id) {
    if (id == "grapes") return BerryShotStyle::Grape;
    if (id == "peas") return BerryShotStyle::Pea;
    if (id == "magic_peas") return BerryShotStyle::Blueberry;
    return BerryShotStyle::None;
}

} // namespace flix
