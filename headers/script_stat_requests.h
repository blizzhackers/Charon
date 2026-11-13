#pragma once
#include <cstdint>

namespace Script {
// Validate the whole request before sending any of its 32-point packets.
template <typename Send>
bool SendStatPoints(int stat, int count, int available, Send send) {
    if (stat < 0 || stat > 3 || count <= 0 || count > available) return false;
    while (count > 0) {
        const int batch = count > 32 ? 32 : count;
        const uint8_t packet[3] = { 0x3A, (uint8_t)stat, (uint8_t)(batch - 1) };
        send(packet, sizeof(packet));
        count -= batch;
    }
    return true;
}
}
