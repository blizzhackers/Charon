#include "headers/utilities.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstddef>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

static_assert(sizeof(CustomPacketHeader) == 4 && offsetof(CustomPacketHeader, size) == 2,
    "Preserve the existing custom packet wire layout.");
static_assert(sizeof(FlyingTextPacket) == 32, "Preserve the flying-text wire layout.");

static int nativeCalls;
static std::size_t NativeSize(char* bytes, std::size_t size, std::size_t* expected) {
    ++nativeCalls;
    assert(bytes && bytes[0] == 0x01 && size == 1);
    *expected = 8;
    return 1;
}

int main() {
    using namespace CustomPackets;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    auto* allocation = static_cast<char*>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(allocation);
    DWORD oldProtection;
    assert(VirtualProtect(allocation + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS,
        &oldProtection));

    char valid[sizeof(CustomPacketHeader) + sizeof(FlyingTextPacket)] = {};
    CustomPacketHeader header;
    header.size = sizeof(valid);
    std::memcpy(valid, &header, sizeof(header));
    valid[sizeof(header)] = FLYING_TEXT_PACKET_ID;

    // Each truncated prefix ends at an inaccessible page, catching accidental reads.
    for (std::size_t prefix = 0; prefix <= sizeof(valid); ++prefix) {
        char* bytes = allocation + info.dwPageSize - prefix;
        std::memcpy(bytes, valid, prefix);
        std::size_t expected = 777;
        const auto status = ReadFrame(bytes, prefix, expected);
        assert(status == (prefix == sizeof(valid) ? FrameStatus::Complete : FrameStatus::Incomplete));
        assert(ResolveIncomingSize(bytes, prefix, &expected, NativeSize) ==
            (prefix == sizeof(valid) ? 1u : 0u));
        if (prefix >= sizeof(header)) assert(expected == sizeof(valid));
    }
    assert(nativeCalls == 0);

    for (int malformed : { -32768, -1, 0, 1, 2, 3, 513, 32767 }) {
        header.size = static_cast<std::int16_t>(malformed);
        std::memcpy(valid, &header, sizeof(header));
        std::size_t expected = 777;
        assert(ReadFrame(valid, sizeof(valid), expected) == FrameStatus::Invalid);
        assert(ResolveIncomingSize(valid, sizeof(valid), &expected, NativeSize) == 0);
        assert(expected == 777);
    }

    char maximum[MaxSize] = {};
    for (std::size_t length : { sizeof(CustomPacketHeader), MaxSize }) {
        header.size = static_cast<std::int16_t>(length);
        std::memcpy(maximum, &header, sizeof(header));
        std::size_t expected = 0;
        assert(ReadFrame(maximum, length, expected) == FrameStatus::Complete);
        assert(expected == length);
        assert(ResolveIncomingSize(maximum, length, &expected, NativeSize) == 1);
    }
    std::size_t serverExpected = 0;
    assert(ReadFrame(maximum, MaxClientToServerSize, serverExpected) == FrameStatus::Incomplete);
    header.size = MaxClientToServerSize;
    std::memcpy(maximum, &header, sizeof(header));
    assert(ReadFrame(maximum, MaxClientToServerSize, serverExpected) == FrameStatus::Complete);

    char native = 0x01;
    std::size_t expected = 0;
    assert(ReadFrame(&native, 1, expected) == FrameStatus::Native);
    assert(ResolveIncomingSize(&native, 1, &expected, NativeSize) == 1);
    assert(nativeCalls == 1 && expected == 8);
    assert(ReadFrame(nullptr, 0, expected) == FrameStatus::Incomplete);

    char flying[sizeof(FlyingTextPacket)] = {};
    flying[0] = FLYING_TEXT_PACKET_ID;
    for (std::size_t prefix = 0; prefix <= sizeof(flying); ++prefix) {
        char* bytes = allocation + info.dwPageSize - prefix;
        std::memcpy(bytes, flying, prefix);
        assert(IsCompletePayload(bytes, prefix, FLYING_TEXT_PACKET_ID, sizeof(FlyingTextPacket)) ==
            (prefix == sizeof(flying)));
    }
    flying[0] = 2;
    assert(!IsCompletePayload(flying, sizeof(flying), FLYING_TEXT_PACKET_ID, sizeof(FlyingTextPacket)));
    assert(!IsCompletePayload(nullptr, sizeof(flying), FLYING_TEXT_PACKET_ID, sizeof(FlyingTextPacket)));

    assert(VirtualFree(allocation, 0, MEM_RELEASE));
    std::puts("custom packet parser checks passed");
}
