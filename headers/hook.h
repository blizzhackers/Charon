/**
 * Utilities for hooking into the goodies.
 */
#pragma once

#include <windows.h>
#include <cstring>
#include <vector>
#include <unordered_map>

extern std::unordered_map<DWORD, BYTE> OriginalBytes;

class BYTES {
    BYTE value;
    size_t length;
public:
    BYTES(BYTE value, size_t length);
    friend class MemoryPatch;
};

class NOP_TO {
    DWORD addr;
public:
    NOP_TO(LPVOID addr);
    NOP_TO(DWORD addr);
    friend class MemoryPatch;
};

typedef std::vector<BYTE> BYTESEQ;

class CALL {
    LPVOID pFunc;
public:
    CALL(LPVOID pFunc);
    friend class MemoryPatch;
};

class JUMP {
    LPVOID pFunc;
public:
    JUMP(LPVOID pFunc);
    JUMP(DWORD pFunc);
    friend class MemoryPatch;
};

class JUMP_EQUAL {
    LPVOID pFunc;
public:
    JUMP_EQUAL(LPVOID pFunc);
    JUMP_EQUAL(DWORD pFunc);
    friend class MemoryPatch;
};

typedef JUMP_EQUAL JUMP_ZERO;

class REVERT {
    size_t length;
public:
    REVERT(size_t length);
    friend class MemoryPatch;
};

DWORD RevertBytes(DWORD pAddr, DWORD dwLen);

// Writes checked x86 patches and retains the first bytes for reversible settings.
class MemoryPatch {
    DWORD pAddr = NULL;

    template <class T> MemoryPatch& d(const T data);

public:
    MemoryPatch(DWORD dwAddr);
    MemoryPatch& operator << (const bool data);
    MemoryPatch& operator << (const char data);
    MemoryPatch& operator << (const wchar_t data);
    MemoryPatch& operator << (const unsigned char data);
    MemoryPatch& operator << (const short data);
    MemoryPatch& operator << (const unsigned short data);
    MemoryPatch& operator << (const int data);
    MemoryPatch& operator << (const unsigned int data);
    MemoryPatch& operator << (const long data);
    MemoryPatch& operator << (const unsigned long data);
    MemoryPatch& operator << (const long long data);
    MemoryPatch& operator << (const unsigned long long data);
    MemoryPatch& operator << (const float data);
    MemoryPatch& operator << (const double data);
    MemoryPatch& operator << (const BYTES bytes);
    MemoryPatch& operator << (const NOP_TO address);
    MemoryPatch& operator << (const CALL call);
    MemoryPatch& operator << (const JUMP jump);
    MemoryPatch& operator << (const JUMP_EQUAL jump);
    MemoryPatch& operator << (const REVERT revert);
    MemoryPatch& operator << (BYTESEQ bytes);
};

template <class T> BYTESEQ DATA(const T& data) {
    BYTESEQ bytes;

    for (size_t c = 0; c < sizeof(T); c++) {
        bytes.push_back(((BYTE*)&data)[c]);
    }

    return bytes;
}

namespace ASM {
    const BYTE NOP = 0x90;
    const BYTE CALL = 0xE8;
    const BYTE JUMP = 0xE9;
    const BYTE RET = 0xC3;
    const BYTE PUSHAD = 0x60;
    const BYTE POPAD = 0x61;
    const BYTESEQ TEST_AL{ 0x84, 0xC0 };
    const BYTESEQ MOV_ECX_EDI{ 0x89, 0xF9 };
    const BYTESEQ MOV_ECX_EAX{ 0x89, 0xC1 };
}
