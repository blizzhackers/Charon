// From the repo root in an x86 Native Tools Command Prompt for VS 2022:
// cl /nologo /std:c++17 /EHsc /W4 /Od /RTC1 tests\experience_abi.cpp /Fe:"%TEMP%\experience_abi.exe" /Fo:"%TEMP%\experience_abi.obj"
// "%TEMP%\experience_abi.exe"
// Repeat with /O2 in place of /Od /RTC1.
#include "../headers/experience.h"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

static_assert(sizeof(void*) == 4, "The game ABI is x86 only.");

static Ghidra::D2UnitStrc* observedUnit;
static Ghidra::D2GameStrc* observedGame;
static DWORD observedXp;
static int observedLevel;
static Ghidra::D2UnitStrc* bonusUnit;
static Ghidra::D2GameStrc* bonusGame;
static DWORD bonusXp;
static DWORD stackBefore, stackAfter;
static DWORD frameBefore, frameAfter;
static DWORD ebxAfter, esiAfter, ediAfter;

__declspec(naked) DWORD OriginalExperienceStub() {
    __asm {
        mov observedUnit, ecx
        mov observedLevel, eax
        mov eax, dword ptr [esp + 4]
        mov observedGame, eax
        mov eax, dword ptr [esp + 8]
        mov observedXp, eax
        xor eax, observedLevel
        mov ecx, 0x11223344
        mov edx, 0x55667788
        ret 8
    }
}

DWORD CALC_Experience = reinterpret_cast<DWORD>(&OriginalExperienceStub);

DWORD __stdcall ApplyExperienceBonus(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame) {
    bonusXp = exp;
    bonusUnit = pUnit;
    bonusGame = pGame;
    return exp + 37;
}

__declspec(naked) DWORD __stdcall InvokeExperience(Ghidra::D2UnitStrc*, Ghidra::D2GameStrc*, DWORD, int) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ebx, 0x12345678
        mov esi, 0x23456789
        mov edi, 0x3456789a
        mov stackBefore, esp
        mov frameBefore, ebp
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        mov ecx, dword ptr [ebp + 8]
        mov eax, dword ptr [ebp + 20]
        call ExperienceIntercept
        mov stackAfter, esp
        mov frameAfter, ebp
        mov ebxAfter, ebx
        mov esiAfter, esi
        mov ediAfter, edi
        // Restore the frame even if a cleanup assertion will fail.
        mov esp, stackBefore
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 16
    }
}

static void CheckCase(DWORD unit, DWORD game, DWORD baseXp, int level) {
    auto pUnit = reinterpret_cast<Ghidra::D2UnitStrc*>(unit);
    auto pGame = reinterpret_cast<Ghidra::D2GameStrc*>(game);
    DWORD rawXp = baseXp ^ static_cast<DWORD>(level);
    assert(InvokeExperience(pUnit, pGame, baseXp, level) == rawXp + 37);
    assert(observedUnit == pUnit && observedGame == pGame);
    assert(observedXp == baseXp && observedLevel == level);
    assert(bonusXp == rawXp && bonusUnit == pUnit && bonusGame == pGame);
    assert(stackAfter == stackBefore && frameAfter == frameBefore);
    assert(ebxAfter == 0x12345678 && esiAfter == 0x23456789 && ediAfter == 0x3456789a);
}

int main() {
    CheckCase(0x12345000, 0x23456000, 0x10203040, 23);
    CheckCase(0x34567000, 0x45678000, 0x80706050, 89);
    std::puts("Experience ABI: 2 cases passed (registers, arguments, result, stack cleanup).");
}
