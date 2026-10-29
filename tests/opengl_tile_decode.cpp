// Synthetic format/geometry and callback-state checks; no original game data.
#include "../headers/opengl_game.h"
#include "../headers/opengl_tile_decode.h"
#include <cassert>
#include <climits>
#include <thread>
#include <vector>

namespace CharonGL {
    GameOptions* gameOptions = nullptr;
    std::array<std::uint32_t, 256> palette{};
    std::array<const std::uint8_t*, 72> paletteTables{};
    Light globalLight{};
    std::uint8_t constantAlpha = 255;
    Blend currentBlend = Blend::Replace;
    bool videoPlaying = false;
    bool PerspectiveEnabled() { return false; }
    void Project(std::int32_t, std::int32_t, std::int32_t, int& x, int& y) { x = y = 0; }
    namespace Device {
        std::vector<Vertex> submitted;
        Primitive lastPrimitive;
        Blend lastBlend;
        std::uint8_t lastAlpha;
        bool lastLinear;
        unsigned draws = 0;
        unsigned failures = 0;
        bool Ready() { return true; }
        Size CanvasSize() { return {640, 480}; }
        void Fail(const char*) { ++failures; }
        void DeleteImage(Image) {}
        Image UploadIndices(const std::uint8_t*, Size) { return {}; }
        bool Draw(Primitive primitive, const Vertex* vertices, std::size_t count,
            Image, const std::uint32_t*, Blend blend, std::uint8_t alpha, bool, bool linear) {
            submitted.assign(vertices, vertices + count);
            lastPrimitive = primitive; lastBlend = blend; lastAlpha = alpha;
            lastLinear = linear; ++draws;
            return true;
        }
    }
}

// Seed the private cache to exercise real floor callbacks without game residency calls.
#include "../framework/opengl_tiles.cpp"

int main() {
    using namespace CharonGL;
    using namespace CharonGL::TileDecode;
    std::array<std::uint8_t, 32 * 32> pixels{};
    std::vector<std::uint8_t> stream{2, 3, 11, 12, 13, 4, 0, 0, 0};
    for (unsigned row = 1; row < 32; ++row) { stream.push_back(0); stream.push_back(0); }
    assert(Rle(stream.data(), stream.size(), pixels.data(), 32, 32, 32));
    assert(pixels[0] == 0 && pixels[2] == 11 && pixels[4] == 13 && pixels[5] == 0);
    assert(!Rle(stream.data(), stream.size() - 1, pixels.data(), 32, 32, 32));
    const std::uint8_t overflow[]{31,2,1,2,0,0};
    assert(!Rle(overflow, sizeof(overflow), pixels.data(), 32, 1, 32));
    const std::uint8_t skipOverflow[]{33,0,0,0};
    assert(!Rle(skipOverflow, sizeof(skipOverflow), pixels.data(), 32, 1, 32));
    const std::uint8_t truncated[]{0,3,1,2};
    assert(!Rle(truncated, sizeof(truncated), pixels.data(), 32, 1, 32));
    std::array<std::uint8_t, 256> diamond;
    diamond.fill(23); pixels.fill(0);
    assert(Diamond(diamond.data(), diamond.size(), pixels.data(), 32));
    unsigned painted = 0;
    for (auto pixel : pixels) painted += pixel == 23;
    assert(painted == 256 && pixels[14] == 23 && pixels[13] == 0 && pixels[7 * 32] == 23);
    assert(!Diamond(diamond.data(), 255, pixels.data(), 32));
    assert(Add(INT_MAX, 1) == INT_MIN && Sub(INT_MIN, 1) == INT_MAX);
    assert(Mul(INT_MAX, 8192) == -8192 && Sar(-33, 5) == -2);
    assert(Sar(-33, 0) == -33 && Sar(-33, 32) == -33);
    assert(Midpoint(-3, 0) == -2 && Midpoint(INT_MAX, 1) == -1073741824);
    float u, v;
    GroundUv(0, 0, u, v); assert(u == 80.0f / 256 && v == 2.0f / 128);
    GroundUv(5, 5, u, v); assert(u == 80.0f / 256 && v == 78.0f / 128);
    GroundUv(2, 3, u, v); assert(u == 96.0f / 256 && v == 40.0f / 128);
    assert(LitColor({128,255,129,1}) == Rgba(128,64,0));

    int source = 0;
    auto floor = std::make_shared<Tile>();
    floor->floor = {1,256,128,true};
    tiles.emplace(&source,floor);
    std::array<LightVertex,36> lights{};
    GameOptions options{};
    gameOptions = &options;
    for (int lowQuality : {0,1}) {
        options.lowQuality = lowQuality;
        for (void* filterRequest : {static_cast<void*>(nullptr),reinterpret_cast<void*>(1)}) {
            Device::draws = 0; Device::lastLinear = true;
            DrawGroundTile(&source,lights.data(),0,0,0,0,255,0,filterRequest);
            assert(Device::draws == (lowQuality ? 1u : 5u));
            assert(!Device::lastLinear); // Legacy edge-floor filtering must not blur the canvas.
            assert(Device::lastBlend == Blend::Replace);
        }
    }
    ClearTileCache(); gameOptions = nullptr;

    palette[7] = Rgba(10,20,30);
    const POINT center{20,30};
    DrawSolidSquare(&center, 2, 7);
    assert(Device::submitted.size() == 4 && Device::submitted[0].x == 19 && Device::submitted[0].y == 29);
    assert(Device::submitted[2].x == 22 && Device::submitted[2].y == 32);
    assert(currentBlend == Blend::Alpha && constantAlpha == 255);
    DrawSolidRectEx(-10, -20, 50, 60, 7, 1);
    assert(Device::submitted[0].x == 0 && Device::submitted[0].y == 0 && constantAlpha == 128);
    DrawLine(10,10,10,10,7,255);
    assert(Device::lastPrimitive == Primitive::Points && Device::submitted.size() == 1);
    assert(currentBlend == Blend::Replace && constantAlpha == 0);
    DrawLine(-10,10,650,10,7,192);
    assert(Device::submitted[0].x == -10 && Device::submitted[1].x == 650 && constantAlpha == 192);
    DrawSolidRectAlpha(0,10,20,0,7,255);
    assert(Device::failures == 1);
    std::vector<std::uint8_t> player(48 * 48 * 8);
    for (unsigned i = 0; i < 48 * 48; ++i) { player[i*8+4] = 128; player[i*8+5] = 255; }
    currentBlend = Blend::Add;
    DrawLight(nullptr, player.data(), 0, 0);
    assert(Device::submitted.size() == 48 * 48 * 8 && currentBlend == Blend::Add && constantAlpha == 0);
    assert(Device::submitted[0].color == Rgba(127,0,0));
    assert(Device::submitted[(24*48+24)*8].color == Rgba(0,0,0));
    InvalidateTile(nullptr); // Same-thread preload eviction before context ownership is known.
    InitializeTileCacheThread();
    assert(Device::failures == 1);
    std::thread foreign([] { InvalidateTile(nullptr); });
    foreign.join();
    InitializeTileCacheThread();
    assert(Device::failures == 2); // A foreign CPU eviction must never be accepted as synchronized.
    return 0;
}
