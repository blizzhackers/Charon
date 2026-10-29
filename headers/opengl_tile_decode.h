#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace CharonGL::TileDecode {
    // Decode only inside the caller-established block and destination bounds.
    inline bool Rle(const std::uint8_t* source, std::size_t size,
        std::uint8_t* output, unsigned width, unsigned rows, unsigned stride) {
        if (!source || !output || !width || stride < width) return false;
        std::size_t cursor = 0;
        for (unsigned row = 0; row < rows; ++row) {
            unsigned x = 0;
            for (;;) {
                if (size - cursor < 2) return false;
                const unsigned skip = source[cursor++], count = source[cursor++];
                if (!skip && !count) break;
                if (skip > width - x) return false;
                x += skip;
                if (count > width - x || count > size - cursor) return false;
                std::memcpy(output + row * stride + x, source + cursor, count);
                cursor += count;
                x += count;
            }
        }
        return true;
    }

    inline bool Diamond(const std::uint8_t* source, std::size_t size,
        std::uint8_t* output, unsigned stride) {
        if (!source || !output || size < 256 || stride < 32) return false;
        for (unsigned row = 0; row < 15; ++row) {
            const unsigned width = 4 * (row < 8 ? row + 1 : 15 - row);
            std::memcpy(output + row * stride + (32 - width) / 2, source, width);
            source += width;
        }
        return true;
    }

    inline std::int32_t Signed(std::uint32_t value) {
        std::int32_t result;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
    inline std::int32_t Add(std::int32_t a, std::int32_t b) {
        return Signed(std::uint32_t(a) + std::uint32_t(b));
    }
    inline std::int32_t Sub(std::int32_t a, std::int32_t b) {
        return Signed(std::uint32_t(a) - std::uint32_t(b));
    }
    inline std::int32_t Mul(std::int32_t a, std::uint32_t b) {
        return Signed(std::uint32_t(a) * b);
    }
    inline std::int32_t Sar(std::int32_t value, unsigned bits) {
        bits &= 31;
        if (!bits) return value;
        const auto raw = std::uint32_t(value);
        return Signed((raw >> bits) | (value < 0 ? (~std::uint32_t(0) << (32 - bits)) : 0));
    }
    inline std::int32_t Midpoint(std::int32_t a, std::int32_t b) {
        return Sar(Add(a, b), 1);
    }
    inline void GroundUv(unsigned row, unsigned column, float& u, float& v) {
        const int edge = column == 0 ? 1 : column == 5 ? -1 : 0;
        int a = edge, b = edge;
        if (row == 0) { --a; ++b; }
        if (row == 5) { ++a; --b; }
        u = float(16 * (int(column) - int(row)) + 80 + a) / 256;
        v = float(8 * (int(column) + int(row)) + b) / 128;
    }
}
