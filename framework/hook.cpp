#include <windows.h>
#include "../headers/hook.h"
#include <iostream>

std::unordered_map<DWORD, BYTE> OriginalBytes;

long calcRelAddr(DWORD instructionAddress, DWORD targetAddress, DWORD instructionLength) {
    return (long)targetAddress - (long)instructionAddress - (long)instructionLength;
}

template <class T>
DWORD SetData(DWORD pAddr, const T& value) {
    DWORD dwOld, dwSize = sizeof(T);

    if (VirtualProtect((LPVOID)pAddr, dwSize, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwSize; i++) {
            if (!OriginalBytes.count(pAddr + i)) {
                OriginalBytes[pAddr + i] = *(BYTE*)(pAddr + i);
            }
        }

        T* addr = (T*)pAddr;
        *addr = value;
        VirtualProtect((LPVOID)pAddr, dwSize, dwOld, &dwOld);
        return dwSize;
    }

    return 0;
}

DWORD PatchCall(BYTE instruction, DWORD pAddr, DWORD pFunc) {
    DWORD dwOld, dwLen = sizeof(BYTE) + sizeof(DWORD);

    if (VirtualProtect((LPVOID)pAddr, dwLen, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwLen; i++) {
            if (!OriginalBytes.count(pAddr + i)) {
                OriginalBytes[pAddr + i] = *(BYTE*)(pAddr + i);
            }
        }

        BYTE* bytes = (BYTE*)pAddr;
        bytes[0] = instruction;
        *(long*)(bytes + 1) = calcRelAddr(pAddr, pFunc, dwLen);
        VirtualProtect((LPVOID)pAddr, dwLen, dwOld, &dwOld);
        return dwLen;
    }

    return 0;
}

DWORD PatchCallW(const BYTE instruction[2], DWORD pAddr, DWORD pFunc) {
    DWORD dwOld, dwLen = sizeof(BYTE) * 2 + sizeof(DWORD);

    if (VirtualProtect((LPVOID)pAddr, dwLen, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwLen; i++) {
            if (!OriginalBytes.count(pAddr + i)) {
                OriginalBytes[pAddr + i] = *(BYTE*)(pAddr + i);
            }
        }

        BYTE* bytes = (BYTE*)pAddr;
        bytes[0] = instruction[0];
        bytes[1] = instruction[1];
        *(long*)(bytes + 2) = calcRelAddr(pAddr, pFunc, dwLen);
        VirtualProtect((LPVOID)pAddr, dwLen, dwOld, &dwOld);
        return dwLen;
    }

    return 0;
}

DWORD SetBytes(DWORD pAddr, BYTE value, DWORD dwLen) {
    DWORD dwOld;

    if (dwLen > 0 && VirtualProtect((LPVOID)pAddr, dwLen, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwLen; i++) {
            if (!OriginalBytes.count(pAddr + i)) {
                OriginalBytes[pAddr + i] = *(BYTE*)(pAddr + i);
            }
        }

        std::memset((LPVOID)pAddr, value, dwLen);
        VirtualProtect((LPVOID)pAddr, dwLen, dwOld, &dwOld);
        return dwLen;
    }

    return 0;
}

DWORD RevertBytes(DWORD pAddr, DWORD dwLen) {
    DWORD dwOld;

    if (dwLen > 0 && VirtualProtect((LPVOID)pAddr, dwLen, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwLen; i++) {
            try {
                *(BYTE*)(pAddr + i) = OriginalBytes.at(pAddr + i);
            } catch (...) { }
        }
        VirtualProtect((LPVOID)pAddr, dwLen, dwOld, &dwOld);
        return dwLen;
    }

    return 0;
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

SKIP::SKIP(size_t length) {
    this->length = length;
}

REWIND::REWIND(size_t length) {
    this->length = length;
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

JUMP_NOT_EQUAL::JUMP_NOT_EQUAL(LPVOID pFunc) {
    this->pFunc = pFunc;
}

JUMP_NOT_EQUAL::JUMP_NOT_EQUAL(DWORD pFunc) {
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
        char msg[128];
        sprintf_s(msg, "Your NOP_TO is already past the target address: at 0x%08x, target 0x%08x", pAddr, address.addr);
        std::cout << msg << std::endl;
        MessageBoxA(NULL, msg, "MemoryPatch Error", MB_OK);
        ExitProcess((UINT)-1);
    }
    else {
        DWORD len = address.addr - pAddr;
        pAddr += SetBytes(pAddr, ASM::NOP, len);
    }
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const SKIP offset) {
    pAddr += offset.length;
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const REWIND offset) {
    pAddr -= offset.length;
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

MemoryPatch& MemoryPatch::operator << (const JUMP_NOT_EQUAL jump) {
    BYTE ins[]{ 0x0F, 0x85 };
    pAddr += PatchCallW(ins, pAddr, (DWORD)jump.pFunc);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (const REVERT revert) {
    pAddr += RevertBytes(pAddr, revert.length);
    return *this;
}

MemoryPatch& MemoryPatch::operator << (BYTESEQ bytes) {
    DWORD dwOld, dwSize = bytes.size();

    if (dwSize > 0 && VirtualProtect((LPVOID)pAddr, dwSize, PAGE_READWRITE, &dwOld)) {
        for (DWORD i = 0; i < dwSize; i++) {
            if (!OriginalBytes.count(pAddr + i)) {
                OriginalBytes[pAddr + i] = *(BYTE*)(pAddr + i);
            }
        }

        BYTE* addr = (BYTE*)pAddr;
        for (size_t c = 0; c < dwSize; c++) {
            addr[c] = bytes[c];
        }
        VirtualProtect((LPVOID)pAddr, dwSize, dwOld, &dwOld);
        pAddr += dwSize;
    }

    return *this;
}
