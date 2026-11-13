#pragma once

namespace Game {
// 1.14d patch_d2.mpq: these handlers/type select only damaging persistent
// fire (67, 69, 131, 230, 240, 331, 423, 681), never blood or death visuals.
// Client maker 0x4D3630 randomly substitutes 104/105 for the damaging patch,
// preserving its exact position and lifetime (including Countess fire 331).
constexpr int GroundFireSize(int classId, int clientDo, int serverDo, int collideType, int size) {
    if (clientDo != 5 || collideType != 3 || size < 1 || size > 3) return 0;
    if (serverDo == 5) return size;
    return (classId == 104 || classId == 105) && serverDo == 1 && size == 1 ? size : 0;
}
}
