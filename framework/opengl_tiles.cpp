#include "../headers/opengl_game.h"
#include "../headers/opengl_tile_decode.h"
#include "../headers/remote.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace CharonGL {
namespace {
    using namespace TileDecode;
    using Invalidator = void (__fastcall*)(void*);
    REMOTEREF(Invalidator, gpfnSpriteCacheFreeEntry, 0x88cb1c);
    REMOTEFUNC(BOOL __stdcall, SPRITECACHE_AllocEntry, (void*,int,int), 0x5fdea0);
    struct Corner { std::int32_t x, y, z; };
    REMOTEPTR(Corner, gaGlideWallCornerTable, 0x72f3a0);
    struct Block {
        std::int16_t x, y;
        std::uint16_t format;
        std::vector<std::uint8_t> encoded;
        Image image;
    };
    struct Tile {
        std::int32_t direction;
        std::vector<Block> blocks;
        Image floor;
        std::size_t bytes = 0;
        std::uint64_t use = 0;
    };
    std::mutex cacheMutex;
    std::unordered_map<void*, std::shared_ptr<Tile>> tiles;
    std::vector<std::shared_ptr<Tile>> retiredTiles;
    std::size_t cacheBytes = 0;
    std::uint64_t useCounter = 0;
    DWORD renderThread = 0;
    DWORD earlyInvalidationThread = 0;
    bool asynchronousEviction = false;
    constexpr std::size_t maxTileBytes = 16 * 1024 * 1024;
    constexpr std::size_t maxCacheBytes = 128 * 1024 * 1024;

    bool DrainRetired() {
        std::vector<std::shared_ptr<Tile>> retired;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (!renderThread) {
                renderThread = GetCurrentThreadId();
                if (earlyInvalidationThread && earlyInvalidationThread != renderThread) asynchronousEviction = true;
            }
            if (renderThread != GetCurrentThreadId()) {
                Device::Fail("Terrain drawing called outside the OpenGL context thread");
                return false;
            }
            retired.swap(retiredTiles);
        }
        for (const auto& tile : retired) {
            if (tile->floor.name) Device::DeleteImage(tile->floor);
            for (const auto& block : tile->blocks)
                if (block.image.name) Device::DeleteImage(block.image);
        }
        return true;
    }

    std::shared_ptr<Tile> LoadTile(void* source) {
        if (!source) { Device::Fail("Null terrain tile"); return {}; }
        if (!DrainRetired()) return {};
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto found = tiles.find(source);
            if (found != tiles.end()) {
                found->second->use = ++useCounter;
                return found->second;
            }
            if (asynchronousEviction) {
                Device::Fail("Asynchronous tile eviction requires CPU-residency synchronization");
                return {};
            }
        }
        // This CPU-only loader may synchronously re-enter InvalidateTile.
        if (gpfnSpriteCacheFreeEntry != &InvalidateTile) {
            Device::Fail("Native tile invalidator must be installed before CPU residency loads");
            return {};
        }
        if (!SPRITECACHE_AllocEntry(source, 1, 0)) return {};
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (asynchronousEviction) {
            Device::Fail("Tile residency changed on another thread during terrain load");
            return {};
        }
        const auto size = Read<std::uint32_t>(source, 0x4c);
        const auto count = Read<std::int32_t>(source, 0x50);
        const auto* base = Read<const std::uint8_t*>(source, 0x54);
        if (!base || count <= 0 || size > maxTileBytes || std::uint64_t(count) * 20 > size) {
            Device::Fail("Invalid resident terrain block allocation"); return {};
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(base);
        const auto end = std::uint64_t(begin) + size;
        if (end > UINTPTR_MAX) { Device::Fail("Terrain block address overflow"); return {}; }
        std::vector<std::uintptr_t> starts;
        starts.reserve(count);
        for (int i = 0; i < count; ++i) {
            const auto address = reinterpret_cast<std::uintptr_t>(Read<const void*>(base + i * 20, 16));
            if (address < begin + std::size_t(count) * 20 || address >= end) {
                Device::Fail("Terrain block source lies outside resident tile data"); return {};
            }
            starts.push_back(address);
        }
        auto sortedStarts = starts;
        std::sort(sortedStarts.begin(), sortedStarts.end());
        auto result = std::make_shared<Tile>();
        result->direction = Read<std::int32_t>(source, 0);
        result->blocks.reserve(count);
        for (int i = 0; i < count; ++i) {
            auto limit = std::uintptr_t(end);
            const auto next = std::upper_bound(sortedStarts.begin(), sortedStarts.end(), starts[i]);
            if (next != sortedStarts.end()) limit = *next;
            const auto* record = base + i * 20;
            Block block{Read<std::int16_t>(record, 0), Read<std::int16_t>(record, 2),
                Read<std::uint16_t>(record, 8), {}, {}};
            const auto* pixels = reinterpret_cast<const std::uint8_t*>(starts[i]);
            if (result->bytes + (limit - starts[i]) > maxTileBytes) {
                Device::Fail("Terrain block payload exceeds native cache entry bound"); return {};
            }
            block.encoded.assign(pixels, pixels + (limit - starts[i]));
            result->bytes += block.encoded.size() + 1024;
            result->blocks.push_back(std::move(block));
        }
        result->bytes += 32768;
        result->use = ++useCounter;
        while (!tiles.empty() && (tiles.size() >= 4096 || cacheBytes + result->bytes > maxCacheBytes)) {
            auto oldest = std::min_element(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) {
                return a.second->use < b.second->use;
            });
            retiredTiles.push_back(oldest->second);
            cacheBytes -= oldest->second->bytes;
            tiles.erase(oldest);
        }
        cacheBytes += result->bytes;
        tiles.emplace(source, result);
        return result;
    }

    bool FloorImage(Tile& tile) {
        if (tile.floor.name) return true;
        std::array<std::uint8_t, 256 * 128> pixels{};
        for (const auto& block : tile.blocks) {
            if (!(block.format & 1)) continue;
            if (block.x < 0 || block.x > 224 || block.y < 0 || block.y > 113) {
                Device::Fail("Floor block destination exceeds 256 by 128 canvas"); return false;
            }
            auto* destination = pixels.data() + block.y * 256 + block.x;
            const bool valid = block.format & 4
                ? Rle(block.encoded.data(), block.encoded.size(), destination, 32, 15, 256)
                : Diamond(block.encoded.data(), block.encoded.size(), destination, 256);
            if (!valid) { Device::Fail("Malformed floor block encoding"); return false; }
        }
        tile.floor = Device::UploadIndices(pixels.data(), {256, 128});
        return tile.floor.name != 0;
    }
    bool WallImage(Block& block) {
        if (block.image.name) return true;
        std::array<std::uint8_t, 32 * 32> pixels{};
        if (!Rle(block.encoded.data(), block.encoded.size(), pixels.data(), 32, 32, 32)) {
            Device::Fail("Malformed 32 by 32 wall block encoding"); return false;
        }
        block.image = Device::UploadIndices(pixels.data(), {32, 32});
        return block.image.name != 0;
    }
    void TerrainAlpha(std::uint8_t alpha) {
        currentBlend = alpha == 255 ? Blend::Replace : Blend::Alpha;
        // Replace does not overwrite the retained constant-alpha state.
        if (alpha != 255) constantAlpha = alpha;
    }
    bool Submit(Primitive primitive, const Vertex* vertices, std::size_t count,
        Image image = {}, bool key = false) {
        // Smooth the completed frame only, not individual legacy Glide terrain passes.
        return Device::Draw(primitive, vertices, count, image, palette.data(), currentBlend,
            constantAlpha, key, false);
    }
    Vertex MakeVertex(int x, int y, float u, float v, std::uint32_t color) {
        return {float(x), float(y), u, v, color};
    }
    struct Bounds { int left, top, right, bottom, shift; };
    Bounds TileBounds(int panels, bool perspective) {
        const auto size = Device::CanvasSize();
        Bounds bounds{perspective ? 0 : -32, 0, size.width,
            size.height - (perspective ? 47 : 15), 0};
        if (perspective && (panels == 1 || panels == 2)) {
            bounds.left += size.width / 4;
            bounds.right -= size.width / 4;
            bounds.shift = (panels == 1 ? -1 : 1) * (size.width / 4);
        } else if (panels == 1) bounds.right = size.width / 2;
        else if (panels == 2) bounds.left = size.width / 2 - 32;
        return bounds;
    }
    bool Inside(const Vertex& vertex, const Bounds& bounds) {
        return vertex.x >= bounds.left && vertex.x < bounds.right &&
            vertex.y >= bounds.top && vertex.y < bounds.bottom;
    }
    Light MidLight(Light a, Light b) {
        return {std::uint8_t((unsigned(a.intensity) + b.intensity) / 2),
            std::uint8_t((unsigned(a.red) + b.red) / 2),
            std::uint8_t((unsigned(a.green) + b.green) / 2),
            std::uint8_t((unsigned(a.blue) + b.blue) / 2)};
    }
    bool DrawMesh(std::array<Vertex, 15>& mesh, const Bounds& bounds, Image image) {
        if (!Inside(mesh[0], bounds) && !Inside(mesh[12], bounds) &&
            !Inside(mesh[14], bounds) && !Inside(mesh[2], bounds)) return false;
        for (auto& vertex : mesh) vertex.x += float(bounds.shift);
        for (unsigned column = 0; column < 2; ++column) {
            std::array<Vertex, 10> strip;
            for (unsigned row = 0; row < 5; ++row) {
                strip[row * 2] = mesh[row * 3 + column];
                strip[row * 2 + 1] = mesh[row * 3 + column + 1];
            }
            if (!Submit(Primitive::TriangleStrip, strip.data(), strip.size(), image, true)) return false;
        }
        return true;
    }
    Vertex ProjectVertex(std::int32_t x, std::int32_t y, std::int32_t z,
        unsigned row, unsigned column, std::uint32_t color) {
        int screenX, screenY;
        Project(x, y, z, screenX, screenY);
        return MakeVertex(screenX, screenY, float(column) / 2, float(row) / 4, color);
    }
    bool DrawFlat(Block& block, int x, int y, const Bounds& bounds,
        std::uint32_t left, std::uint32_t right) {
        const auto px = Add(x, block.x), py = Add(y, block.y);
        const auto bottom = Add(py, 32), edge = Add(px, 32);
        if (px < bounds.left || px >= bounds.right || bottom < bounds.top || bottom >= bounds.bottom) return false;
        if (!WallImage(block)) return false;
        const Vertex quad[]{MakeVertex(px, py, 0, 0, left), MakeVertex(px, bottom, 0, 1, left),
            MakeVertex(edge, bottom, 1, 1, right), MakeVertex(edge, py, 1, 0, right)};
        return Submit(Primitive::TriangleFan, quad, 4, block.image, true);
    }
    BOOL Wall(void* source, int x, int y, const Light* lights, int panels, std::uint8_t alpha) {
        if (!lights) { Device::Fail("Wall tile has no edge lights"); return FALSE; }
        auto tile = LoadTile(source);
        if (!tile) return FALSE;
        TerrainAlpha(alpha);
        const bool perspective = PerspectiveEnabled();
        const auto bounds = TileBounds(panels, perspective);
        if (perspective && (tile->direction < 0 || tile->direction > 9)) {
            Device::Fail("Perspective wall direction exceeds recovered table"); return FALSE;
        }
        bool drawn = false;
        for (auto& block : tile->blocks) {
            const int column = Sar(block.x, 5);
            if (column < 0 || column > 4) { Device::Fail("Wall light column outside six edge lights"); return FALSE; }
            const Light aLight = Read<Light>(lights, column * sizeof(Light));
            const Light bLight = Read<Light>(lights, (column + 1) * sizeof(Light));
            const auto aColor = LitColor(aLight), bColor = LitColor(bLight);
            if (!perspective) { drawn = DrawFlat(block, x, y, bounds, aColor, bColor) || drawn; continue; }
            // CPU-only original initialization shifts X/Y once; no game table payload is copied into Charon.
            auto a = Read<Corner>(gaGlideWallCornerTable, (tile->direction * 6 + column) * sizeof(Corner));
            auto b = Read<Corner>(gaGlideWallCornerTable, (tile->direction * 6 + column + 1) * sizeof(Corner));
            a.x = Add(a.x, Mul(x, 8192)); b.x = Add(b.x, Mul(x, 8192));
            a.y = Add(a.y, Mul(y, 8192)); b.y = Add(b.y, Mul(y, 8192));
            a.z = Sub(a.z, block.y); b.z = Sub(b.z, block.y);
            const Corner mid{Midpoint(a.x, b.x), Midpoint(a.y, b.y), Midpoint(a.z, b.z)};
            const Corner corners[]{a, mid, b};
            const std::uint32_t colors[]{aColor, LitColor(MidLight(aLight, bLight)), bColor};
            std::array<Vertex, 15> mesh;
            for (unsigned row = 0; row < 5; ++row)
                for (unsigned col = 0; col < 3; ++col)
                    mesh[row * 3 + col] = ProjectVertex(corners[col].x, corners[col].y,
                        Sub(corners[col].z, int(row) * 8), row, col, colors[col]);
            if (!WallImage(block)) return FALSE;
            drawn = DrawMesh(mesh, bounds, block.image) || drawn;
        }
        return drawn ? TRUE : FALSE;
    }
    bool ValidRect(const RECT* rect) {
        if (!rect || rect->left > rect->right || rect->top > rect->bottom) {
            Device::Fail("Invalid primitive rectangle"); return false;
        }
        return rect->left != rect->right && rect->top != rect->bottom;
    }
    void Solid(int x1, int y1, int x2, int y2, std::uint8_t index, std::uint8_t alpha) {
        const auto size = Device::CanvasSize();
        x1 = std::clamp(x1, 0, size.width); x2 = std::clamp(x2, 0, size.width);
        y1 = std::clamp(y1, 0, size.height); y2 = std::clamp(y2, 0, size.height);
        if (x1 == x2 || y1 == y2) return;
        if (y1 > y2) { Device::Fail("Primitive bottom precedes top after clipping"); return; }
        currentBlend = Blend::Alpha;
        constantAlpha = alpha;
        const auto color = palette[index] | 0xff000000u;
        const Vertex quad[]{MakeVertex(x1, y1, 0, 0, color), MakeVertex(x1, y2, 0, 0, color),
            MakeVertex(x2, y2, 0, 0, color), MakeVertex(x2, y1, 0, 0, color)};
        Submit(Primitive::TriangleFan, quad, 4);
    }
}

void InitializeTileCacheThread() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto thread = GetCurrentThreadId();
    if (renderThread && renderThread != thread) {
        Device::Fail("Terrain cache cannot migrate to a different OpenGL context thread");
        return;
    }
    renderThread = thread;
    if (earlyInvalidationThread && earlyInvalidationThread != thread) asynchronousEviction = true;
    if (asynchronousEviction) Device::Fail("Tile eviction observed outside the OpenGL context thread");
}

void __fastcall InvalidateTile(void* source) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto thread = GetCurrentThreadId();
    if (renderThread) {
        if (thread != renderThread) asynchronousEviction = true;
    } else if (!earlyInvalidationThread) earlyInvalidationThread = thread;
    else if (earlyInvalidationThread != thread) asynchronousEviction = true;
    const auto found = tiles.find(source);
    if (found == tiles.end()) return;
    retiredTiles.push_back(found->second);
    cacheBytes -= found->second->bytes;
    tiles.erase(found);
}

void ClearTileCache() {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& entry : tiles) retiredTiles.push_back(entry.second);
        tiles.clear();
        cacheBytes = 0;
    }
    if (GetCurrentThreadId() == renderThread && Device::Ready()) DrainRetired();
}

void __fastcall DrawGroundTile(void* source, LightVertex* lights, int, int, int, int,
    int alpha, int, void*) {
    if (!lights) { Device::Fail("Ground tile has no vertex lights"); return; }
    auto tile = LoadTile(source);
    if (!tile || !FloorImage(*tile)) return;
    TerrainAlpha(std::uint8_t(alpha));
    std::array<Vertex, 36> mesh;
    for (unsigned i = 0; i < mesh.size(); ++i) {
        const auto light = Read<LightVertex>(lights, i * sizeof(LightVertex));
        mesh[i] = MakeVertex(light.x, light.y, 0, 0, LitColor(light.light));
        GroundUv(i / 6, i % 6, mesh[i].u, mesh[i].v);
    }
    if (gameOptions && gameOptions->lowQuality) {
        const Vertex quad[]{mesh[0], mesh[5], mesh[35], mesh[30]};
        Submit(Primitive::TriangleFan, quad, 4, tile->floor, true);
    } else for (unsigned row = 0; row < 5; ++row) {
        std::array<Vertex, 12> strip;
        for (unsigned col = 0; col < 6; ++col) {
            strip[col * 2] = mesh[row * 6 + col];
            strip[col * 2 + 1] = mesh[(row + 1) * 6 + col];
        }
        if (!Submit(Primitive::TriangleStrip, strip.data(), strip.size(), tile->floor, true)) return;
    }
}

BOOL __fastcall DrawWallTile(void* tile, int x, int y, const Light* lights, int panels) {
    return Wall(tile, x, y, lights, panels, 255);
}
BOOL __fastcall DrawTransWallTile(void* tile, int x, int y, const Light* lights, int panels, int alpha) {
    return Wall(tile, x, y, lights, panels, std::uint8_t(alpha));
}
BOOL __fastcall DrawShadowTile(void* source, int x, int y, std::uint8_t intensity, int panels) {
    auto tile = LoadTile(source);
    if (!tile) return FALSE;
    if (gameOptions && gameOptions->blendedShadows) {
        currentBlend = Blend::Alpha; constantAlpha = 192;
    } else currentBlend = Blend::Replace;
    auto light = globalLight;
    light.intensity = intensity;
    const auto color = LitColor(light);
    const bool perspective = PerspectiveEnabled();
    const auto bounds = TileBounds(panels, perspective);
    bool drawn = false;
    for (auto& block : tile->blocks) {
        if (!perspective) { drawn = DrawFlat(block, x, y, bounds, color, color) || drawn; continue; }
        const int col = Sar(block.x, 5), row = Sar(Add(block.y, 96), 5);
        const auto x0 = Sub(Add(Mul(x, 8192), Mul(Add(col, Mul(row, 2)), 65536)), 0x38000);
        const auto y0 = Add(Add(Mul(y, 8192), Mul(Sub(Mul(row, 2), col), 65536)), 0x18000);
        std::array<Vertex, 15> mesh;
        for (unsigned j = 0; j < 5; ++j)
            for (unsigned k = 0; k < 3; ++k)
                mesh[j * 3 + k] = ProjectVertex(Add(x0, Mul(int(j + k), 32768)),
                    Add(y0, Mul(int(j) - int(k), 32768)), 0, j, k, color);
        if (!WallImage(block)) return FALSE;
        drawn = DrawMesh(mesh, bounds, block.image) || drawn;
    }
    return drawn ? TRUE : FALSE;
}

void __fastcall DrawLine(int x1, int y1, int x2, int y2, std::uint8_t index, std::uint8_t alpha) {
    const auto size = Device::CanvasSize();
    if ((x1 < 0 && x2 < 0) || (x1 > size.width && x2 > size.width) ||
        (y1 < 0 && y2 < 0) || (y1 > size.height && y2 > size.height)) return;
    currentBlend = alpha == 255 ? Blend::Replace : Blend::Alpha;
    constantAlpha = alpha == 255 ? 0 : alpha;
    const auto color = palette[index] | 0xff000000u;
    const Vertex line[]{MakeVertex(x1, y1, 0, 0, color), MakeVertex(x2, y2, 0, 0, color)};
    Submit(x1 == x2 && y1 == y2 ? Primitive::Points : Primitive::Lines, line,
        x1 == x2 && y1 == y2 ? 1 : 2);
}
void __fastcall DrawRect(const RECT* rect, std::uint8_t index) {
    if (!ValidRect(rect)) return;
    const auto middleX = Add(rect->left, Signed((std::uint32_t(rect->right) - std::uint32_t(rect->left)) >> 1));
    const auto middleY = Add(rect->top, Signed((std::uint32_t(rect->bottom) - std::uint32_t(rect->top)) >> 1));
    DrawLine(middleX, rect->top, rect->right, middleY, index, 255);
    DrawLine(rect->right, middleY, middleX, rect->bottom, index, 255);
    DrawLine(middleX, rect->bottom, rect->left, middleY, index, 255);
    DrawLine(rect->left, middleY, middleX, rect->top, index, 255);
}
void __fastcall DrawRectEx(const RECT* rect, std::uint8_t index) {
    if (!ValidRect(rect)) return;
    const auto size = Device::CanvasSize();
    const int left = std::clamp(int(rect->left), 0, size.width - 1);
    const int right = std::clamp(int(rect->right), 0, size.width - 1);
    const int top = std::clamp(int(rect->top), 0, size.height - 1);
    const int bottom = std::clamp(int(rect->bottom), 0, size.height - 1);
    if (left == right || top == bottom) return;
    DrawLine(left, top, right, top, index, 255);
    DrawLine(right, top, right, bottom, index, 255);
    DrawLine(right, bottom, left, bottom, index, 255);
    DrawLine(left, bottom, left, top, index, 255);
}
void __fastcall DrawSolidRect(const RECT* rect, std::uint8_t index) {
    if (ValidRect(rect)) Solid(rect->left, rect->top, rect->right, rect->bottom, index, 255);
}
void __fastcall DrawSolidSquare(const POINT* point, std::uint8_t size, std::uint8_t index) {
    if (!size) return;
    if (!point) { Device::Fail("Null solid-square center"); return; }
    Solid(Sub(point->x, size - 1), Sub(point->y, size - 1), Add(point->x, size), Add(point->y, size), index, 255);
}
void __fastcall DrawSolidRectEx(int x1, int y1, int x2, int y2, std::uint8_t index, int mode) {
    const std::uint8_t alpha = mode == 0 ? 64 : mode == 1 ? 128 : mode == 2 ? 192 : 255;
    Solid(x1, y1, x2, y2, index, alpha);
}
void __fastcall DrawSolidRectAlpha(int x1, int y1, int x2, int y2, std::uint8_t index, std::uint8_t alpha) {
    Solid(x1, y1, x2, y2, index, alpha);
}
void __fastcall DrawLight(const void*, const void* player, int, int) {
    if (!player) { Device::Fail("Null player debug-light map"); return; }
    // Original constant colors are RRGGBB00, including the last cell's retained alpha.
    constantAlpha = 0;
    constexpr int offsets[][2]{{1,0},{2,0},{0,1},{1,1},{2,1},{3,1},{1,2},{2,2}};
    std::vector<Vertex> points;
    points.reserve(48 * 48 * 8);
    for (unsigned row = 0; row < 48; ++row) for (unsigned column = 0; column < 48; ++column) {
        const auto light = Read<Light>(player, (row * 48 + column) * 8 + 4);
        const auto color = row == 24 && column == 24 ? Rgba(0, 0, 0) :
            Rgba(unsigned(light.red) * light.intensity / 256, unsigned(light.green) * light.intensity / 256,
                unsigned(light.blue) * light.intensity / 256);
        const int x = 100 + 2 * int(column) - 2 * int(row), y = 100 + 2 * int(row + column);
        for (const auto& offset : offsets) points.push_back(MakeVertex(x + offset[0], y + offset[1], 0, 0, color));
    }
    Submit(Primitive::Points, points.data(), points.size());
}
}
