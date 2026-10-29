// Build x86 with framework/hook.cpp; no game or graphics driver is required.
#include "../headers/hook.h"
#include <cassert>
#include <cstdio>
#include <string>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif
static_assert(sizeof(void*) == 4, "Patches target the game's x86 ABI.");

void ExpectFailure(const wchar_t* argument, DWORD expected) {
    wchar_t executable[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, executable, MAX_PATH));
    std::wstring command = L"\"" + std::wstring(executable) + L"\" " + argument;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child));
    assert(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeProcess(child.hProcess, &code));
    assert(code == expected);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        if (!wcscmp(argv[1], L"invalid")) MemoryPatch(1) << ASM::NOP << ASM::RET;
        if (!wcscmp(argv[1], L"backward")) MemoryPatch(8) << NOP_TO(DWORD(7));
        if (!wcscmp(argv[1], L"overflow")) RevertBytes(MAXDWORD, 2);
        return 99;
    }
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const DWORD page = system.dwPageSize;
    auto memory = static_cast<BYTE*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    memset(memory, 0xcc, page * 2);
    const DWORD address = reinterpret_cast<DWORD>(memory);
    DWORD ignored = 0;
    assert(VirtualProtect(memory, page, PAGE_EXECUTE_READ, &ignored));
    assert(VirtualProtect(memory + page, page, PAGE_READONLY, &ignored));

    // Scalar and sequence writes advance the stream; the first original survives overlaps.
    MemoryPatch(address) << BYTE(0xb8) << DWORD(42) << ASM::RET << BYTESEQ{} << NOP_TO(address + 6);
    assert(reinterpret_cast<int(*)()>(memory)() == 42);
    MemoryPatch(address + 1) << DWORD(84);
    assert(reinterpret_cast<int(*)()>(memory)() == 84);
    assert(OriginalBytes.at(address + 1) == 0xcc);
    MemoryPatch(address) << REVERT(6);
    for (int i = 0; i < 6; ++i) assert(memory[i] == 0xcc);

    // Rel32 CALL/JMP/JZ encodings, including a displacement that wraps modulo 2^32.
    MemoryPatch(address) << CALL(reinterpret_cast<void*>(address + 0x123))
        << JUMP(address - 0x321) << JUMP_EQUAL(address + 0x456);
    assert(memory[0] == 0xe8 && memory[5] == 0xe9 && memory[10] == 0x0f && memory[11] == 0x84);
    DWORD displacement = 0;
    memcpy(&displacement, memory + 1, 4); assert(displacement == 0x123 - 5);
    memcpy(&displacement, memory + 6, 4); assert(displacement == DWORD(0 - 0x321 - 10));
    memcpy(&displacement, memory + 12, 4); assert(displacement == 0x456 - 16);

    // A cross-page write preserves each page's distinct original protection.
    MemoryPatch(address + page - 2) << BYTES(ASM::NOP, 4);
    MEMORY_BASIC_INFORMATION region{};
    assert(VirtualQuery(memory, &region, sizeof(region)) && region.Protect == PAGE_EXECUTE_READ);
    assert(VirtualQuery(memory + page, &region, sizeof(region)) && region.Protect == PAGE_READONLY);
    MemoryPatch(address + page - 2) << REVERT(4);
    for (DWORD i = page - 2; i < page + 2; ++i) assert(memory[i] == 0xcc);
    RevertBytes(address + page + 8, 2); // An untracked revert is a harmless no-op.
    assert(memory[page + 8] == 0xcc);
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    OriginalBytes.clear();

    ExpectFailure(L"invalid", ERROR_INVALID_ADDRESS);
    ExpectFailure(L"backward", ERROR_INVALID_ADDRESS);
    ExpectFailure(L"overflow", ERROR_INVALID_ADDRESS);
    puts("memory patch checks passed");
}
