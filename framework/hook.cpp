#include <windows.h>
#include "../headers/hook.h"
#include <algorithm>
#include <cstdlib>

std::unordered_map<DWORD, BYTE> OriginalBytes;

namespace {
[[noreturn]] void PatchFailure(DWORD address, DWORD error) {
    char message[160];
    sprintf_s(message, "Charon: memory patch failed at 0x%08lx (Win32 error %lu).\n", address, error);
    OutputDebugStringA(message);
    // Continuing after a partial instruction write can execute corrupt code.
    TerminateProcess(GetCurrentProcess(), error ? error : ERROR_WRITE_FAULT);
    std::abort();
}

DWORD WriteBytes(DWORD address, const BYTE* bytes, DWORD length, bool saveOriginal = true) {
    if (!length) return 0;
    if (address > MAXDWORD - (length - 1)) PatchFailure(address, ERROR_INVALID_ADDRESS);

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    DWORD written = 0;
    while (written < length) {
        const DWORD current = address + written;
        const DWORD chunk = (std::min)(length - written,
            systemInfo.dwPageSize - current % systemInfo.dwPageSize);
        DWORD protection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(current), chunk, PAGE_EXECUTE_READWRITE, &protection))
            PatchFailure(current, GetLastError());
        if (saveOriginal) {
            try {
                for (DWORD i = 0; i < chunk; ++i)
                    OriginalBytes.try_emplace(current + i, *reinterpret_cast<const BYTE*>(current + i));
            } catch (...) {
                PatchFailure(current, ERROR_NOT_ENOUGH_MEMORY);
            }
        }
        std::memcpy(reinterpret_cast<void*>(current), bytes + written, chunk);
        DWORD ignored = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(current), chunk, protection, &ignored))
            PatchFailure(current, GetLastError());
        if (!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(current), chunk))
            PatchFailure(current, GetLastError());
        written += chunk;
    }
    return length;
}

template <class T>
DWORD SetData(DWORD address, const T& value) {
    return WriteBytes(address, reinterpret_cast<const BYTE*>(&value), sizeof(T));
}

DWORD PatchBranch(const BYTE* instruction, DWORD opcodeLength, DWORD address, DWORD target) {
    BYTE bytes[6]{};
    std::memcpy(bytes, instruction, opcodeLength);
    const DWORD length = opcodeLength + sizeof(DWORD);
    // x86 relative displacements wrap in the 32-bit address space.
    const DWORD displacement = target - address - length;
    std::memcpy(bytes + opcodeLength, &displacement, sizeof(displacement));
    return WriteBytes(address, bytes, length);
}

DWORD PatchCall(BYTE instruction, DWORD address, DWORD target) {
    return PatchBranch(&instruction, 1, address, target);
}

DWORD PatchCallW(const BYTE instruction[2], DWORD address, DWORD target) {
    return PatchBranch(instruction, 2, address, target);
}

DWORD SetBytes(DWORD address, BYTE value, DWORD length) {
    const BYTESEQ bytes(length, value);
    return WriteBytes(address, bytes.data(), length);
}
}

DWORD RevertBytes(DWORD address, DWORD length) {
    if (!length) return 0;
    if (address > MAXDWORD - (length - 1)) PatchFailure(address, ERROR_INVALID_ADDRESS);
    // Untracked bytes remain untouched, including a wholly unpatched range.
    for (DWORD offset = 0; offset < length;) {
        const auto original = OriginalBytes.find(address + offset);
        if (original == OriginalBytes.end()) { ++offset; continue; }
        BYTESEQ bytes;
        const DWORD start = offset;
        while (offset < length) {
            const auto saved = OriginalBytes.find(address + offset);
            if (saved == OriginalBytes.end()) break;
            bytes.push_back(saved->second);
            ++offset;
        }
        WriteBytes(address + start, bytes.data(), static_cast<DWORD>(bytes.size()), false);
    }
    return length;
}

BYTES::BYTES(BYTE value, size_t length) {
    this->value = value;
    this->length = length;
}

NOP_TO::NOP_TO(LPVOID addr) {
    this->addr = (DWORD)addr;
}

NOP_TO::NOP_TO(DWORD addr) {
    this->addr = addr;
}

CALL::CALL(LPVOID pFunc) {
    this->pFunc = pFunc;
}

JUMP::JUMP(LPVOID pFunc) {
    this->pFunc = pFunc;
}

JUMP::JUMP(DWORD pFunc) {
    this->pFunc = (LPVOID)pFunc;
}

JUMP_EQUAL::JUMP_EQUAL(LPVOID pFunc) {
    this->pFunc = pFunc;
}

JUMP_EQUAL::JUMP_EQUAL(DWORD pFunc) {
    this->pFunc = (LPVOID)pFunc;
}

REVERT::REVERT(size_t length) {
    this->length = length;
}

template <class T>
MemoryPatch& MemoryPatch::d(const T data) {
    pAddr += SetData(pAddr, data);
    return *this;
}

MemoryPatch::MemoryPatch(DWORD dwAddr) {
    pAddr = dwAddr;
}

MemoryPatch& MemoryPatch::operator << (const bool data) { return d<bool>(data); }
MemoryPatch& MemoryPatch::operator << (const char data) { return d<char>(data); }
MemoryPatch& MemoryPatch::operator << (const wchar_t data) { return d<wchar_t>(data); }
MemoryPatch& MemoryPatch::operator << (const unsigned char data) { return d<unsigned char>(data); }
MemoryPatch& MemoryPatch::operator << (const short data) { return d<short>(data); }
MemoryPatch& MemoryPatch::operator << (const unsigned short data) { return d<unsigned short>(data); }
MemoryPatch& MemoryPatch::operator << (const int data) { return d<int>(data); }
MemoryPatch& MemoryPatch::operator << (const unsigned int data) { return d<unsigned int>(data); }
MemoryPatch& MemoryPatch::operator << (const long data) { return d<long>(data); }
MemoryPatch& MemoryPatch::operator << (const unsigned long data) { return d<unsigned long>(data); }
MemoryPatch& MemoryPatch::operator << (const long long data) { return d<long long>(data); }
MemoryPatch& MemoryPatch::operator << (const unsigned long long data) { return d<unsigned long long>(data); }
MemoryPatch& MemoryPatch::operator << (const float data) { return d<float>(data); }
MemoryPatch& MemoryPatch::operator << (const double data) { return d<double>(data); }

MemoryPatch& MemoryPatch::operator << (const BYTES bytes) {
    pAddr += SetBytes(pAddr, bytes.value, bytes.length);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const NOP_TO address) {
    if (address.addr < pAddr) {
        PatchFailure(pAddr, ERROR_INVALID_ADDRESS);
    }
    else {
        DWORD len = address.addr - pAddr;
        pAddr += SetBytes(pAddr, ASM::NOP, len);
    }
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const CALL call) {
    pAddr += PatchCall(ASM::CALL, pAddr, (DWORD)call.pFunc);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const JUMP jump) {
    pAddr += PatchCall(ASM::JUMP, pAddr, (DWORD)jump.pFunc);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const JUMP_EQUAL jump) {
    BYTE ins[]{ 0x0F, 0x84 };
    pAddr += PatchCallW(ins, pAddr, (DWORD)jump.pFunc);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const REVERT revert) {
    pAddr += RevertBytes(pAddr, revert.length);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (BYTESEQ bytes) {
    pAddr += WriteBytes(pAddr, bytes.data(), static_cast<DWORD>(bytes.size()));
    return *this;
}
