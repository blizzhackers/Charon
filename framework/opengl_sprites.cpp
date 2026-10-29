#include "headers/opengl_game.h"
#include "headers/remote.h"
#include "headers/opengl_sprite_decode.h"
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

namespace CharonGL {
static_assert(sizeof(void*) == 4, "The original sprite loader and records use the x86 ABI");
namespace {
    const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    REMOTEFUNC(int __stdcall, SPRITECACHE_GetOrLoadSprite, (void*, int, int), (imageBase + 0x2001f0));

    // All GPU/cache operations run on the context thread. Borrow frame bytes only
    // during this draw; cache entries own a snapshot so recycled/mutated frames remain safe.
    struct Frame {
        int width, height, left, top;
        std::uint32_t flip;
        const std::uint8_t* encoded = nullptr;
        std::size_t encodedSize = 0;
    };
    struct Geometry {
        int width, height, texWidth, texHeight;
        float u0 = 0, v0, u1, v1 = 1;
    };
    struct Entry {
        int width, height;
        std::uint32_t flip;
        int outline;
        std::array<std::uint8_t, 256> remap, brighter;
        std::vector<std::uint8_t> encoded;
        Image image;
        std::uint64_t lastUse;
        std::size_t bytes;
    };
    std::unordered_multimap<std::uint64_t, std::unique_ptr<Entry>> cache;
    std::size_t cacheBytes = 0;
    std::uint64_t clock = 0;
    Image font;
    std::array<int,96> fontWidths{};
    constexpr std::size_t cacheLimit = 32 * 1024 * 1024;

    std::int32_t Wrap(std::int64_t value) {
        const auto bits = std::uint32_t(value);
        std::int32_t result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    int Add(int a, int b) { return Wrap(std::int64_t(a) + b); }
    int Sub(int a, int b) { return Wrap(std::int64_t(a) - b); }
    int Scale(int value) { return Wrap(std::int64_t(value) * PerspectiveScale()) / 256; }
    int Half(int value) { return value >= 0 ? value / 2 : int((std::int64_t(value) - 1) / 2); }

    bool Load(void* gfx, Frame& out) {
        if (!gfx) { Device::Fail("OpenGL sprite: null graphics record"); return false; }
        if (!SPRITECACHE_GetOrLoadSprite(gfx, 0, 1)) return false;
        const void* frame = Read<const void*>(gfx, 0x3c);
        const void* file = Read<const void*>(gfx, 0x34);
        if (!frame || (file && Read<std::uint32_t>(file, 8) != 0)) {
            Device::Fail("OpenGL sprite: missing frame or unsupported DC6 format"); return false;
        }
        out.flip = Read<std::uint32_t>(frame, 0);
        out.width = Read<int>(frame, 4); out.height = Read<int>(frame, 8);
        out.left = Read<int>(frame, 12); out.top = Read<int>(frame, 16);
        const auto length = Read<std::uint32_t>(frame, 28);
        if (out.width <= 0 || out.height <= 0 || out.width > 256 || out.height > 256 ||
            out.flip > 1 || !length || length > 1024 * 1024) {
            Device::Fail("OpenGL sprite: invalid dimensions, orientation or RLE length"); return false;
        }
        const auto bytes = static_cast<const std::uint8_t*>(frame) + 32;
        out.encoded = bytes; out.encodedSize = length;
        return true;
    }

    int PowerSize(int value) {
        int size = 8;
        while (size < value && size < 256) size *= 2;
        return size;
    }
    bool SizeFor(const Frame& frame, Geometry& g, bool outline = false) {
        g.width = frame.width + (outline ? 2 : 0);
        g.height = frame.height + (outline ? 2 : 0);
        // Original uploads clamp to 256 and cannot safely contain larger outlines.
        if (g.width > 256 || g.height > 256) {
            Device::Fail("OpenGL sprite: expanded outline exceeds the recovered texture bounds"); return false;
        }
        g.texWidth = PowerSize(g.width); g.texHeight = PowerSize(g.height);
        g.texWidth = (std::max)(g.texWidth, g.texHeight / 8);
        g.texHeight = (std::max)(g.texHeight, g.texWidth / 8);
        g.u0 = 0; g.v1 = 1;
        g.u1 = PerspectiveEnabled() ? 1.0f : float(g.width) / g.texWidth;
        g.v0 = PerspectiveEnabled() ? 0.0f : 1.0f - float(g.height) / g.texHeight;
        return true;
    }

    void HashBytes(std::uint64_t& hash, const void* source, std::size_t size) {
        const auto bytes = static_cast<const std::uint8_t*>(source);
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    Image Texture(const Frame& frame, const Geometry& g, const std::uint8_t* remap = nullptr,
        bool brighter = false, int outline = -1) {
        std::array<std::uint8_t, 256> map{}, bright{};
        if (brighter && !paletteTables[70]) {
            Device::Fail("OpenGL sprite: brighter palette table is absent"); return {};
        }
        for (unsigned i = 0; i < 256; ++i) {
            map[i] = remap ? remap[i] : std::uint8_t(i);
            bright[i] = brighter ? paletteTables[70][i] : std::uint8_t(i);
        }
        std::uint64_t hash = 14695981039346656037ull;
        HashBytes(hash, &frame.width, sizeof(frame.width));
        HashBytes(hash, &frame.height, sizeof(frame.height));
        HashBytes(hash, &frame.flip, sizeof(frame.flip));
        HashBytes(hash, &outline, sizeof(outline));
        HashBytes(hash, map.data(), map.size()); HashBytes(hash, bright.data(), bright.size());
        HashBytes(hash, frame.encoded, frame.encodedSize);
        const auto range = cache.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it) {
            auto& e = *it->second;
            if (e.width == frame.width && e.height == frame.height && e.flip == frame.flip &&
                e.outline == outline && e.remap == map && e.brighter == bright && e.encoded.size() == frame.encodedSize &&
                std::memcmp(e.encoded.data(), frame.encoded, frame.encodedSize) == 0) {
                e.lastUse = ++clock;
                return e.image;
            }
        }
        Sprite::Decoded decoded;
        if (!Sprite::Decode(frame.encoded, frame.encodedSize, frame.width,
            frame.height, frame.flip, decoded)) {
            Device::Fail("OpenGL sprite: malformed bounded RLE stream"); return {};
        }
        auto pixels = outline < 0 ? Sprite::Remap(decoded, map.data(), bright.data()) :
            Sprite::Outline(decoded, std::uint8_t(outline));
        std::vector<std::uint8_t> padded(std::size_t(g.texWidth) * g.texHeight, 0);
        for (int y = 0; y < g.height; ++y)
            std::memcpy(padded.data() + std::size_t(g.texHeight - g.height + y) * g.texWidth,
                pixels.data() + std::size_t(y) * g.width, g.width);
        const auto image = Device::UploadIndices(padded.data(), {g.texWidth, g.texHeight});
        if (!image.name) return {};
        auto entry = std::make_unique<Entry>();
        entry->width = frame.width; entry->height = frame.height; entry->flip = frame.flip;
        entry->outline = outline; entry->remap = map; entry->brighter = bright;
        entry->encoded.assign(frame.encoded, frame.encoded + frame.encodedSize); entry->image = image; entry->lastUse = ++clock;
        entry->bytes = sizeof(Entry) + frame.encodedSize + padded.size();
        while (!cache.empty() && (cacheBytes + entry->bytes > cacheLimit || cache.size() >= 2048)) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (it->second->lastUse < oldest->second->lastUse) oldest = it;
            cacheBytes -= oldest->second->bytes;
            Device::DeleteImage(oldest->second->image);
            cache.erase(oldest);
        }
        cacheBytes += entry->bytes;
        cache.emplace(hash, std::move(entry));
        return image;
    }

    bool CoarseVisible(const Frame& f, int x, int y) {
        const auto canvas = Device::CanvasSize();
        const int left = Add(x, f.left), bottom = Add(y, f.top);
        return left <= canvas.width && Add(left, f.width) >= 0 && bottom >= 0 &&
            Sub(bottom, f.height) < canvas.height;
    }
    bool Tint(std::uint32_t light, int tint, bool force, std::uint32_t& color) {
        if (!tint && !force) { color = LitColor(UnpackLight(light)); return true; }
        if (tint < 0 || tint >= 256 || !paletteTables[69]) {
            Device::Fail("OpenGL sprite: constant tint table or index is invalid"); return false;
        }
        const auto rgb = paletteTables[69] + std::size_t(tint) * 3;
        color = Rgba(rgb[0], rgb[1], rgb[2]);
        return true;
    }
    void Submit(const Vertex* vertices, Image image) {
        if (image.name) Device::Draw(Primitive::TriangleFan, vertices, 4, image,
            palette.data(), currentBlend, constantAlpha, true, false);
    }
    void Quad(const Frame& frame, const Geometry& g, int x, int y, Image image, std::uint32_t color) {
        int w = PerspectiveEnabled() ? g.texWidth : g.width;
        int h = PerspectiveEnabled() ? g.texHeight : g.height;
        int left = frame.left, top = Sub(frame.top, h);
        if (PerspectiveEnabled() && PerspectiveScale() != 256) {
            left = Scale(left); top = Scale(top); w = Scale(w); h = Scale(h);
        }
        left = Add(left, x); top = Add(top, y);
        const float l = float(left), t = float(top), r = float(Add(left,w)), b = float(Add(top,h));
        const Vertex vertices[] = {{l,t,g.u0,g.v0,color}, {l,b,g.u0,g.v1,color},
            {r,b,g.u1,g.v1,color}, {r,t,g.u1,g.v0,color}};
        Submit(vertices, image);
    }

    bool BuildFont() {
        if (font.name) return true;
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT typeface = CreateFontA(10,0,0,0,400,0,0,0,0,0,0,0,1,"Courier");
        HBITMAP bitmap = dc ? CreateCompatibleBitmap(dc,8,16) : nullptr;
        if (!dc || !typeface || !bitmap) {
            if (bitmap) DeleteObject(bitmap);
            if (typeface) DeleteObject(typeface);
            if (dc) DeleteDC(dc);
            Device::Fail("OpenGL debug font: GDI allocation failed"); return false;
        }
        const auto oldFont = SelectObject(dc,typeface), oldBitmap = SelectObject(dc,bitmap);
        SetTextColor(dc,RGB(255,255,255)); SetBkColor(dc,RGB(0,0,0)); SetBkMode(dc,OPAQUE);
        std::vector<std::uint8_t> atlas(128 * 128, 0);
        bool ok = GetCharWidthA(dc,32,127,fontWidths.data()) != FALSE;
        for (int glyph = 0; glyph < 96 && ok; ++glyph) {
            RECT rect{0,0,8,16};
            char ch = char(glyph + 32);
            DrawTextA(dc,&ch,1,&rect,0);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 8; ++x) {
                    const auto pixel = GetPixel(dc,x,y);
                    if (pixel == CLR_INVALID) ok = false;
                    if (pixel == RGB(255,255,255))
                        atlas[std::size_t((glyph / 16) * 16 + y) * 128 + (glyph % 16) * 8 + x] = 255;
                }
        }
        SelectObject(dc,oldBitmap); SelectObject(dc,oldFont);
        DeleteObject(bitmap); DeleteObject(typeface); DeleteDC(dc);
        if (!ok) { Device::Fail("OpenGL debug font: GDI glyph extraction failed"); return false; }
        // The original debug font descriptor at 0x72f2b4 is P_8 (format 5).
        font = Device::UploadIndices(atlas.data(), {128,128});
        return font.name != 0;
    }
}

void ClearSpriteCache() {
    for (auto& pair : cache) Device::DeleteImage(pair.second->image);
    cache.clear(); cacheBytes = 0; clock = 0;
    if (font.name) Device::DeleteImage(font);
    font = {};
}

BOOL __fastcall DrawImage(void* gfx, int x, int y, std::uint32_t light, int mode, const std::uint8_t* remap) {
    Frame frame;
    if (!Load(gfx,frame) || !CoarseVisible(frame,x,y)) return FALSE;
    Geometry g;
    if (!SizeFor(frame,g)) return TRUE;
    const auto image = Texture(frame,g,remap,mode == 7);
    if (image.name) {
        SelectDrawMode(mode);
        Quad(frame,g,x,y,image,LitColor(UnpackLight(light)));
    }
    return TRUE;
}

void __fastcall DrawShiftedImage(void* gfx, int x, int y, std::uint32_t light, int mode, int tint) {
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !CoarseVisible(frame,x,y) || !SizeFor(frame,g)) return;
    const auto image = Texture(frame,g);
    std::uint32_t color;
    if (image.name && Tint(light,tint,false,color)) {
        SelectDrawMode(mode); Quad(frame,g,x,y,image,color);
    }
}

void __fastcall DrawVerticalCropImage(void* gfx, int x, int y, int skipBottom, int drawHeight, int mode) {
    SelectDrawMode(mode);
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !SizeFor(frame,g)) return;
    const int sourceHeight = PerspectiveEnabled() ? g.texHeight : g.height;
    if (skipBottom < 0 || drawHeight <= 0 || std::int64_t(skipBottom) + drawHeight > sourceHeight) {
        Device::Fail("OpenGL sprite: invalid vertical crop"); return;
    }
    const auto image = Texture(frame,g);
    g.v0 += float(sourceHeight - skipBottom - drawHeight) / g.texHeight;
    g.v1 -= float(skipBottom) / g.texHeight;
    if (PerspectiveEnabled()) g.texHeight = drawHeight;
    else g.height = drawHeight;
    Quad(frame,g,x,Sub(y,skipBottom),image,Rgba(255,255,255));
}

void __fastcall DrawClippedImage(void* gfx, int x, int y, const int* clip, int mode) {
    SelectDrawMode(mode);
    if (!clip) { Device::Fail("OpenGL sprite: null clip bounds"); return; }
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !SizeFor(frame,g)) return;
    const auto canvas = Device::CanvasSize();
    const int L = Add(x,frame.left), B = Add(y,frame.top), W = frame.width, H = frame.height;
    const int l = (std::max)(0,clip[0]), r = (std::min)(canvas.width,clip[1]);
    const int t = (std::max)(0,Add(clip[2],2)), b = (std::min)(canvas.height,clip[3]);
    if (l > r || L >= r || l > Add(L,W) || t > b || t > B || Sub(B,H) > b) return;
    const int skipL = (std::max)(0,Sub(l,L));
    const int drawW = Add(L,W) < r ? W - skipL : Sub(Sub(r,skipL),L);
    const int skipB = B > b ? Sub(Sub(B,b),1) : 0;
    const int drawH = Sub(B,H) < t ? Add(Sub(Sub(B,skipB),t),1) : Sub(H,skipB);
    const int sourceH = PerspectiveEnabled() ? g.texHeight : H;
    if (skipL < 0 || skipB < 0 || drawW < 0 || drawH <= 0 ||
        std::int64_t(skipL) + drawW > g.texWidth || std::int64_t(skipB) + drawH > sourceH) {
        Device::Fail("OpenGL sprite: accepted clip exceeds the recovered texture bounds"); return;
    }
    const auto image = Texture(frame,g);
    g.u0 = float(skipL) / g.texWidth; g.u1 = float(skipL + drawW) / g.texWidth;
    g.v0 += float(sourceH - drawH - skipB) / g.texHeight;
    g.v1 -= float(skipB) / g.texHeight;
    if (PerspectiveEnabled()) { g.texWidth = drawW; g.texHeight = drawH; }
    else { g.width = drawW; g.height = drawH; }
    Quad(frame,g,Add(x,skipL),Sub(y,skipB),image,Rgba(255,255,255));
}

void __fastcall DrawShadow(void* gfx, int x, int y) {
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !SizeFor(frame,g)) return;
    const auto image = Texture(frame,g);
    if (!image.name) return;
    currentBlend = gameOptions && gameOptions->blendedShadows ? Blend::Alpha : Blend::Replace;
    if (currentBlend == Blend::Alpha) constantAlpha = 192;
    int w = PerspectiveEnabled() ? g.texWidth : g.width;
    int h = Half(PerspectiveEnabled() ? g.texHeight : g.height);
    int center = Add(frame.left,Half(frame.top)), top = Sub(Half(frame.top),h);
    if (PerspectiveEnabled() && PerspectiveScale() != 256) {
        center = Scale(center); top = Scale(top); w = Scale(w); h = Scale(h);
    }
    center = Add(center,x); top = Add(top,y);
    const auto black = Rgba(0,0,0);
    const Vertex vertices[] = {
        {float(Sub(center,h)),float(top),g.u0,g.v0,black},
        {float(center),float(Add(top,h)),g.u0,g.v1,black},
        {float(Add(center,w)),float(Add(top,h)),g.u1,g.v1,black},
        {float(Add(Sub(center,h),w)),float(top),g.u1,g.v0,black}};
    Submit(vertices,image);
}

void __fastcall DrawImageFast(void* gfx, int x, int y, std::uint8_t outline) {
    currentBlend = Blend::Alpha; constantAlpha = 64;
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !SizeFor(frame,g,true)) return;
    const auto image = Texture(frame,g,nullptr,false,outline);
    std::uint32_t color;
    if (image.name && Tint(0xffffffff,0,true,color)) Quad(frame,g,Sub(x,1),Add(y,1),image,color);
}

void __fastcall DrawPerspectiveImage(void* gfx, int x, int y, std::uint32_t light,
    int mode, int panel, const std::uint8_t* remap) {
    Frame frame; Geometry g;
    if (!Load(gfx,frame) || !SizeFor(frame,g)) return;
    const int w = PerspectiveEnabled() ? g.texWidth : g.width;
    const int h = PerspectiveEnabled() ? g.texHeight : g.height;
    const int X = Wrap(std::int64_t(x) + std::int64_t(frame.top)*4096 + std::int64_t(frame.left)*2048);
    const int Y = Wrap(std::int64_t(y) + std::int64_t(frame.top)*4096 - std::int64_t(frame.left)*2048);
    const int inputs[4][2] = {
        {Wrap(std::int64_t(X)-h*4096),Wrap(std::int64_t(Y)-h*4096)}, {X,Y},
        {Wrap(std::int64_t(X)+w*2048),Wrap(std::int64_t(Y)-w*2048)},
        {Wrap(std::int64_t(X)+w*2048-h*4096),Wrap(std::int64_t(Y)-w*2048-h*4096)}};
    const auto canvas = Device::CanvasSize();
    const int shift = panel == 1 ? -canvas.width/4 : panel == 2 ? canvas.width/4 : 0;
    const auto color = LitColor(UnpackLight(light));
    Vertex vertices[4]; bool visible = false;
    for (int i = 0; i < 4; ++i) {
        int px,py; Project(inputs[i][0],inputs[i][1],0,px,py);
        px = Add(px,shift);
        vertices[i] = {float(px),float(py),i < 2 ? g.u0 : g.u1,
            i == 0 || i == 3 ? g.v0 : g.v1,color};
        if (px >= 0 && px < canvas.width && py >= 0 && py < canvas.height - 47) visible = true;
    }
    if (!visible) return;
    const auto image = Texture(frame,g,remap);
    if (image.name) { SelectDrawMode(mode); Submit(vertices,image); }
}

void __fastcall DrawString(int x, int y, const char* format, va_list arguments) {
    if (!format) { Device::Fail("OpenGL debug font: null format"); return; }
    // Keep the original Win32 formatting dialect while bounding its documented 1024-byte output.
    char text[1024]{};
    const int length = wvsprintfA(text,format,arguments);
    if (length < 0 || length >= 512) { Device::Fail("OpenGL debug font: formatted text exceeds 511 bytes"); return; }
    if (!BuildFont()) return;
    currentBlend = Blend::Replace;
    int cursor = x;
    for (int i = 0; i < length; ++i) {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (cursor <= 640) {
            int advance = 0;
            if (cursor < 0) {
                // The original indexes the width table by raw signed char on this branch.
                if (ch >= fontWidths.size()) { Device::Fail("OpenGL debug font: negative-X glyph indexes outside font widths"); return; }
                advance = fontWidths[ch];
            } else if (ch >= 32 && ch <= 127) {
                const int glyph = ch - 32;
                const float u = float((glyph%16)*8)/128, v = float((glyph/16)*16)/128;
                const float l = float(cursor), r = float(Add(cursor,8)), t = float(y), b = float(Add(y,16));
                const auto white = Rgba(255,255,255);
                const Vertex quad[] = {{l,t,u,v,white},{l,b,u,v+16.0f/128,white},
                    {r,b,u+8.0f/128,v+16.0f/128,white},{r,t,u+8.0f/128,v,white}};
                Device::Draw(Primitive::TriangleFan,quad,4,font,palette.data(),Blend::Replace,255,true,false);
                advance = fontWidths[glyph];
            } else advance = fontWidths[0];
            cursor = Add(cursor,advance);
        }
        if (ch == '\n') { y = Add(y,12); cursor = x; }
    }
}
}
