#pragma once

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace CharonPlayers {

struct State {
    DWORD configured = 1;
    uint32_t revision = 0;

    void ApplySaved(DWORD count) { configured = count; }
    void Accept(DWORD count) { configured = count; ++revision; }
    bool Acknowledges(uint32_t before, int count) const {
        return revision != before && configured == static_cast<DWORD>(count);
    }
};

// Client 0x47C4B4..0x47C4D9 accepts single player and local hosts, never joins.
inline bool Supported(int mode) { return mode == 0 || mode == 1 || mode == 6 || mode == 8; }

inline bool ScriptCount(double value, int& count) {
    if (!(value >= 1 && value <= 8) || value != std::floor(value)) return false;
    count = static_cast<int>(value);
    return true;
}

struct Command {
    char narrow[260] = {};
    wchar_t wide[260] = {};

    explicit Command(int count) {
        std::memcpy(narrow, "/players 1", sizeof("/players 1"));
        std::memcpy(wide, L"/players 1", sizeof(L"/players 1"));
        narrow[9] = static_cast<char>('0' + count);
        wide[9] = static_cast<wchar_t>(L'0' + count);
    }
};

// 1.14d 0x47C420: ECX=wide, stack(ANSI, leading-space offset), RET 8.
// Normal Enter call 0x47CC40..0x47CC4D; preserves EBX/ESI/EDI/EBP.
// Buffers are mutable/padded because the parser rewrites and copies 255 bytes.
// Evidence: work/speedrun-refine-20261031/px-client-evidence.txt.
inline void InvokeParser(DWORD parser, Command& command) {
    wchar_t* wide = command.wide;
    char* narrow = command.narrow;
    __asm {
        push 0
        push narrow
        mov ecx, wide
        call parser
    }
}

// Charon-owned acknowledgement, not a read of D2Game's player-count override.
State Configured();

}
