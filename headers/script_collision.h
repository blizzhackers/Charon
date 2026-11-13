#pragma once

#include <cstdint>

namespace ScriptMap {

// PLAYER_COLLISION_DEFAULT: walls, no-player areas, objects, doors and no-path cells.
constexpr uint16_t BlockMask = 0x1C09;

inline uint16_t SnapshotCollision(uint16_t collision, bool ownCenter) {
    const uint16_t bits = collision & BlockMask;
    // PATH_RemoveUnitCollision (0x649560 -> 0x64EC10) clears the player's
    // 0x1000 center before searching; retain every other blocker.
    return ownCenter && !(bits & ~0x1000) ? 0 : bits;
}

}
