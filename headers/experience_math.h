#pragma once

#include <cstdint>

namespace CharonExperience {
    // Existing level-99 total; native player XP adds the award before clamping.
    inline constexpr std::uint32_t MaximumExperience = 3520485254u;
    // Native mercenary sharing multiplies the signed award by 86 before >> 8.
    inline constexpr std::uint32_t MaximumMercenaryAward = 0x7fffffffu / 86;

    inline std::uint32_t Award(std::uint32_t experience, std::uint32_t bonus, std::uint32_t minimum,
        std::uint32_t currentExperience, bool player) {
        std::uint64_t award = minimum > 8000 ? MaximumExperience : std::uint64_t(experience) * bonus;
        if (award && award < minimum) award = minimum;
        std::uint32_t room = currentExperience < MaximumExperience ? MaximumExperience - currentExperience : 0;
        if (!player && room > MaximumMercenaryAward) room = MaximumMercenaryAward;
        return award > room ? room : static_cast<std::uint32_t>(award);
    }
}
