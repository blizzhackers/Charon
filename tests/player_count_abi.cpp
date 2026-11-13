#include "headers/player_count.h"
#include "headers/player_count_patch.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

static_assert(sizeof(void*) == 4, "The game ABI is x86 only.");

static CharonPlayers::State state;
static int expectedCount;
static bool acceptCommand = true;
static DWORD stackBefore, stackAfter, frameBefore, frameAfter;
static DWORD ebxAfter, esiAfter, ediAfter;
static DWORD patchedCount;

__declspec(naked) void __fastcall AcceptedStub(DWORD) {
    __asm {
        mov patchedCount, ecx
        mov eax, 0x11223344
        mov ecx, 0x22334455
        mov edx, 0x33445566
        mov ebx, 0x44556677
        mov esi, 0x55667788
        mov edi, 0x66778899
        mov ebp, 0x778899aa
        ret
    }
}

__declspec(naked) void __stdcall ProbePatch(DWORD, DWORD) {
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
        push dword ptr [ebp + 12]
        call dword ptr [ebp + 8]
        mov stackAfter, esp
        mov frameAfter, ebp
        mov ebxAfter, ebx
        mov esiAfter, esi
        mov ediAfter, edi
        mov esp, stackBefore
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 8
    }
}

void CheckCommandPatch() {
    auto memory = static_cast<BYTE*>(VirtualAlloc(nullptr, 128, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READ));
    assert(memory);
    const DWORD address = reinterpret_cast<DWORD>(memory);
    // Native-style frame and saved registers, followed by an outstanding
    // cdecl atoi argument and its parsed result in EAX.
    MemoryPatch(address) << BYTESEQ{0x55, 0x8B, 0xEC, 0x53, 0x57, 0x56, 0x68}
        << DWORD(0xDEADBEEF) << BYTESEQ{0x8B, 0x45, 0x08};
    const DWORD patch = address + 14;
    CharonPlayers::InstallCommandPatch(patch, AcceptedStub);
    // Match the original parser's ESI/EDI/EBX restoration and frame reset.
    // If cleanup is lost, execution still returns safely, then register checks fail.
    MemoryPatch(patch + 42) << BYTESEQ{0x5E, 0x5F, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
    for (DWORD count : {1u, 8u, 64u}) { // manual commands remain uncapped
        ProbePatch(address, count);
        assert(patchedCount == count);
        assert(stackAfter == stackBefore && frameAfter == frameBefore);
        assert(ebxAfter == 0x12345678 && esiAfter == 0x23456789 && ediAfter == 0x3456789a);
    }
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    OriginalBytes.clear();
}

void __stdcall ObserveCommand(wchar_t* wide, char* narrow, int offset) {
    assert(offset == 0);
    assert(std::strncmp(narrow, "/players ", 9) == 0 && narrow[9] == '0' + expectedCount);
    assert(std::wcsncmp(wide, L"/players ", 9) == 0 && wide[9] == L'0' + expectedCount);
    for (int i = 10; i < 260; ++i) assert(narrow[i] == 0 && wide[i] == 0);
    narrow[0] = '!'; // the native parser may rewrite this buffer
    wide[0] = L'!';
    if (acceptCommand) state.Accept(expectedCount);
}

__declspec(naked) void ParserStub() {
    __asm {
        mov eax, dword ptr [esp + 8]
        push eax
        push dword ptr [esp + 8]
        push ecx
        call ObserveCommand
        mov eax, 0x11223344
        mov ecx, 0x22334455
        mov edx, 0x33445566
        ret 8
    }
}

void __stdcall ProductionAdapter(CharonPlayers::Command* command) {
    CharonPlayers::InvokeParser(reinterpret_cast<DWORD>(&ParserStub), *command);
}

__declspec(naked) void __stdcall Probe(CharonPlayers::Command*) {
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
        push dword ptr [ebp + 8]
        call ProductionAdapter
        mov stackAfter, esp
        mov frameAfter, ebp
        mov ebxAfter, ebx
        mov esiAfter, esi
        mov ediAfter, edi
        mov esp, stackBefore
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}

int main() {
    CheckCommandPatch();
    for (int count = 1; count <= 8; ++count) {
        int validated = 0;
        assert(CharonPlayers::ScriptCount(count, validated) && validated == count);
        CharonPlayers::Command command(count);
        expectedCount = count;
        const auto before = state.revision;
        Probe(&command);
        assert(state.Acknowledges(before, count));
        assert(!state.Acknowledges(state.revision, count));
        assert(!state.Acknowledges(before, count == 8 ? 1 : count + 1));
        assert(stackAfter == stackBefore && frameAfter == frameBefore);
        assert(ebxAfter == 0x12345678 && esiAfter == 0x23456789 && ediAfter == 0x3456789a);
    }
    int count;
    for (double invalid : {0.0, 9.0, -1.0, 1.5, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) assert(!CharonPlayers::ScriptCount(invalid, count));
    for (int mode = -1; mode <= 10; ++mode)
        assert(CharonPlayers::Supported(mode) == (mode == 0 || mode == 1 || mode == 6 || mode == 8));
    const auto before = state.revision;
    state.ApplySaved(3);
    assert(state.configured == 3 && state.revision == before && !state.Acknowledges(before, 3));
    acceptCommand = false;
    expectedCount = 3;
    CharonPlayers::Command refused(3);
    Probe(&refused);
    assert(!state.Acknowledges(before, 3));
    state.revision = UINT32_MAX;
    state.Accept(3);
    assert(state.Acknowledges(UINT32_MAX, 3));
    std::puts("Player-count ABI checks passed (buffers, ECX, stack, registers, validation, acknowledgement).");
}
