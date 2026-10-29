#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

const char CUSTOM_PACKET_ID = 0x2B;

struct CustomPacketHeader {
    std::uint8_t id = CUSTOM_PACKET_ID;
    std::int16_t size = sizeof(CustomPacketHeader);
};

namespace CustomPackets {
    constexpr std::size_t MaxSize = 0x200;
    constexpr std::size_t MaxClientToServerSize = MaxSize - sizeof(std::int32_t);
    enum class FrameStatus { Native, Incomplete, Invalid, Complete };

    inline FrameStatus ReadFrame(const char* bytes, std::size_t available, std::size_t& expected) {
        if (!bytes || available == 0) return FrameStatus::Incomplete;
        if (static_cast<unsigned char>(bytes[0]) != CUSTOM_PACKET_ID) return FrameStatus::Native;
        if (available < sizeof(CustomPacketHeader)) return FrameStatus::Incomplete;

        CustomPacketHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        if (header.size < static_cast<int>(sizeof(header)) || header.size > MaxSize)
            return FrameStatus::Invalid;

        expected = static_cast<std::size_t>(header.size);
        return available < expected ? FrameStatus::Incomplete : FrameStatus::Complete;
    }

    template <class NativeResolver>
    std::size_t ResolveIncomingSize(char* bytes, std::size_t available, std::size_t* expected,
        NativeResolver nativeResolver) {
        std::size_t frameSize = 0;
        const auto status = ReadFrame(bytes, available, frameSize);
        if (status == FrameStatus::Native) return nativeResolver(bytes, available, expected);
        if (!expected) return 0;
        if (frameSize != 0) *expected = frameSize;
        // Native 0x52b920 returns a boolean; 0 leaves incomplete input queued.
        return status == FrameStatus::Complete ? 1 : 0;
    }

    inline bool IsCompletePayload(const char* bytes, std::size_t available,
        unsigned char id, std::size_t required) {
        return bytes && available >= required && required != 0 &&
            static_cast<unsigned char>(bytes[0]) == id;
    }

    bool DispatchClientPacket(char* bytes, std::size_t size);
}
