#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace CharonGL::Sprite {
    struct Decoded {
        int width = 0, height = 0;
        std::vector<std::uint8_t> indices, literal;
    };

    // Native format codec, independent of the game's allocation and renderer metadata.
    inline bool Decode(const std::uint8_t* bytes, std::size_t length,
        int width, int height, std::uint32_t flip, Decoded& out) {
        out = {};
        if (!bytes || width <= 0 || height <= 0 || width > 256 || height > 256 || flip > 1)
            return false;
        Decoded result;
        result.width = width;
        result.height = height;
        result.indices.assign(std::size_t(width) * height, 0);
        result.literal.assign(result.indices.size(), 0);
        std::size_t cursor = 0;
        int x = 0, row = 0;
        while (row < height) {
            if (cursor == length) return false;
            const unsigned token = bytes[cursor++];
            if (token == 0x80) { x = 0; ++row; continue; }
            const unsigned count = token & 0x7f;
            if (count > unsigned(width - x)) return false;
            if (token & 0x80) { x += count; continue; }
            if (count > length - cursor) return false;
            const int y = flip & 1 ? row : height - 1 - row;
            for (unsigned i = 0; i < count; ++i, ++x) {
                const auto pixel = std::size_t(y) * width + x;
                result.indices[pixel] = bytes[cursor++];
                result.literal[pixel] = 1;
            }
        }
        out = std::move(result);
        return true;
    }

    inline std::vector<std::uint8_t> Remap(const Decoded& source,
        const std::uint8_t* remap, const std::uint8_t* brighter) {
        auto result = source.indices;
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (remap && source.literal[i]) result[i] = remap[result[i]];
            if (brighter && result[i]) result[i] = brighter[result[i]];
        }
        return result;
    }

    inline std::vector<std::uint8_t> Outline(const Decoded& source, std::uint8_t index) {
        const int pitch = source.width + 2;
        std::vector<std::uint8_t> result(std::size_t(pitch) * (source.height + 2), 0);
        for (int y = 0; y < source.height; ++y)
            for (int x = 0; x < source.width; ++x)
                if (source.indices[std::size_t(y) * source.width + x]) {
                    const auto center = std::size_t(y + 1) * pitch + x + 1;
                    result[center - pitch] = result[center + pitch] = index;
                    result[center - 1] = result[center + 1] = index;
                }
        return result;
    }
}
