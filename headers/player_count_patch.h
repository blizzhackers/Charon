#pragma once

#include "hook.h"

namespace CharonPlayers {

// Replace 1.14d 0x47C4E4..0x47C50E after atoi returned EAX. Its cdecl
// argument is still on the stack; discard it before preserving registers.
// Shared with the executable parser-patch ABI regression.
inline void InstallCommandPatch(DWORD address, void(__fastcall* accepted)(DWORD)) {
    MemoryPatch(address)
        << BYTESEQ{0x83, 0xC4, 0x04}
        << ASM::PUSHAD
        << ASM::MOV_ECX_EAX
        << CALL(accepted)
        << ASM::POPAD
        << NOP_TO(address + 42);
}

}
