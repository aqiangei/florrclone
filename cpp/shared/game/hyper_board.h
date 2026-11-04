#pragma once
#include <cstdint>
#include <string>

namespace flix {
inline constexpr std::uint32_t kHyperBoardPageSize = 100;
struct HyperHoldingRow {
    std::string name;
    std::string petal;
    std::uint32_t count = 0;
    std::uint32_t ownerTotal = 0;
};
}
