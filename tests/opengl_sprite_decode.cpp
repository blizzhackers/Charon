#include "opengl_sprite_decode.h"
#include <cassert>
#include <array>
#include <iostream>

int main() {
    using namespace CharonGL::Sprite;
    Decoded image;
    const std::uint8_t rows[] = {2, 0, 7, 0x80, 0x81, 1, 9, 0x80};
    assert(Decode(rows, sizeof(rows), 2, 2, 0, image));
    assert((image.indices == std::vector<std::uint8_t>{0,9,0,7}));
    assert((image.literal == std::vector<std::uint8_t>{0,1,1,1}));
    std::array<std::uint8_t,256> map{}, bright{};
    for (unsigned i = 0; i < 256; ++i) map[i] = bright[i] = std::uint8_t(i);
    map[0] = 12; bright[12] = 33;
    assert((Remap(image, map.data(), bright.data()) == std::vector<std::uint8_t>{0,9,33,7}));
    assert(Decode(rows, sizeof(rows), 2, 2, 1, image));
    assert((image.indices == std::vector<std::uint8_t>{0,7,0,9}));
    const std::uint8_t dot[] = {1, 5, 0x80};
    assert(Decode(dot, sizeof(dot), 1, 1, 0, image));
    assert((Outline(image, 8) == std::vector<std::uint8_t>{0,8,0,8,0,8,0,8,0}));
    const std::uint8_t skip[] = {0x82,0x80};
    assert(!Decode(skip, sizeof(skip), 1, 1, 0, image));
    const std::uint8_t overflow[] = {2,1,2,0x80};
    assert(!Decode(overflow, sizeof(overflow), 1, 1, 0, image));
    assert(!Decode(dot, 1, 1, 1, 0, image));
    assert(!Decode(dot, 2, 1, 1, 0, image));
    assert(!Decode(dot, sizeof(dot), 0, 1, 0, image));
    assert(!Decode(dot, sizeof(dot), 1, 257, 0, image));
    assert(!Decode(dot, sizeof(dot), 1, 1, 2, image));
    const std::uint8_t empty[] = {0,0x80};
    assert(Decode(empty, sizeof(empty), 2, 1, 0, image));
    assert((Remap(image, map.data(), nullptr) == std::vector<std::uint8_t>{0,0}));
    std::cout << "sprite decode: orientation, coverage/remap, outline and malformed bounds passed\n";
}
