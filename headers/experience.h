#pragma once

#include <windows.h>

namespace Ghidra {
    struct D2UnitStrc;
    struct D2GameStrc;
}

extern DWORD CALC_Experience;
DWORD __stdcall ApplyExperienceBonus(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame);

// Game.exe 0x57e3f0 takes unit in ECX, merc level in EAX, then game and base XP on the stack.
__declspec(naked) inline DWORD ExperienceIntercept() {
    __asm {
        push ebp
        mov ebp, esp
        push ecx
        push dword ptr [ebp + 0x0c]
        push dword ptr [ebp + 0x08]
        call dword ptr [CALC_Experience]
        push dword ptr [ebp + 0x08]
        push dword ptr [ebp - 0x04]
        push eax
        call ApplyExperienceBonus
        mov esp, ebp
        pop ebp
        ret 8
    }
}
