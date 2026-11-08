#pragma once

#include <cstdint>
#include <string_view>

namespace flix {

inline constexpr double kBerryLoadMillis = 150.0;
// Old servers encoded the berry's launch style in the explosion flag.
inline constexpr std::uint8_t kLegacyBerryLaunchMask = 0x03;

inline bool hasBerryLoadAnimation(std::string_view id) {
    return id == "grapes" || id == "peas" || id == "magic_peas";
}

} // namespace flix
