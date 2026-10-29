#pragma once

#include "opengl_device.h"
#include <array>
#include <cstdarg>
#include <cstring>

namespace CharonGL {
    struct GameOptions {
        int perspective, perspectiveCapable, lowQuality, gamma, vsync, blendedShadows;
    };
    static_assert(sizeof(GameOptions) == 24, "Game graphics options layout");
    struct Light { std::uint8_t intensity, red, green, blue; };
    struct LightVertex { Light light; std::int32_t x, y; };
    static_assert(sizeof(LightVertex) == 12, "Ground light vertex layout");

    extern GameOptions* gameOptions;
    extern std::array<std::uint32_t, 256> palette;
    extern std::array<const std::uint8_t*, 72> paletteTables;
    extern Light globalLight;
    extern std::uint8_t constantAlpha;
    extern Blend currentBlend;
    extern bool videoPlaying;

    // Copy views keep original packed/unaligned data out of C++ object aliasing.
    template<class T> T Read(const void* data, std::size_t offset) {
        T value;
        std::memcpy(&value, static_cast<const std::uint8_t*>(data) + offset, sizeof(value));
        return value;
    }
    template<class T> void Write(void* data, std::size_t offset, const T& value) {
        std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value));
    }
    inline std::uint32_t LitColor(Light light) {
        return Rgba(unsigned(light.intensity) * light.red / 255,
            unsigned(light.intensity) * light.green / 255,
            unsigned(light.intensity) * light.blue / 255);
    }
    inline Light UnpackLight(std::uint32_t packed) {
        return {std::uint8_t(packed), std::uint8_t(packed >> 8),
            std::uint8_t(packed >> 16), std::uint8_t(packed >> 24)};
    }
    void SelectDrawMode(int mode);
    bool PerspectiveEnabled();
    int PerspectiveScale();
    void Project(std::int32_t x, std::int32_t y, std::int32_t bias, int& screenX, int& screenY);
    void ClearSpriteCache();
    void ClearTileCache();
    void InitializeTileCacheThread();
    void __fastcall InvalidateTile(void* tile);

    BOOL __fastcall DrawImage(void*, int, int, std::uint32_t, int, const std::uint8_t*);
    void __fastcall DrawShiftedImage(void*, int, int, std::uint32_t, int, int);
    void __fastcall DrawVerticalCropImage(void*, int, int, int, int, int);
    void __fastcall DrawShadow(void*, int, int);
    void __fastcall DrawImageFast(void*, int, int, std::uint8_t);
    void __fastcall DrawClippedImage(void*, int, int, const int*, int);
    void __fastcall DrawString(int, int, const char*, va_list);
    void __fastcall DrawPerspectiveImage(void*, int, int, std::uint32_t, int, int, const std::uint8_t*);
    void __fastcall DrawGroundTile(void*, LightVertex*, int, int, int, int, int, int, void*);
    BOOL __fastcall DrawWallTile(void*, int, int, const Light*, int);
    BOOL __fastcall DrawTransWallTile(void*, int, int, const Light*, int, int);
    BOOL __fastcall DrawShadowTile(void*, int, int, std::uint8_t, int);
    void __fastcall DrawRect(const RECT*, std::uint8_t);
    void __fastcall DrawRectEx(const RECT*, std::uint8_t);
    void __fastcall DrawSolidRect(const RECT*, std::uint8_t);
    void __fastcall DrawSolidSquare(const POINT*, std::uint8_t, std::uint8_t);
    void __fastcall DrawSolidRectEx(int, int, int, int, std::uint8_t, int);
    void __fastcall DrawSolidRectAlpha(int, int, int, int, std::uint8_t, std::uint8_t);
    void __fastcall DrawLine(int, int, int, int, std::uint8_t, std::uint8_t);
    void __fastcall DrawLight(const void*, const void*, int, int);
    void __fastcall PlayCutScene(const char*, int, void*);
    void __fastcall DecodeSmacker(const char*, void*, int);
    void __fastcall PlayerSmacker(void*);
    void __fastcall CloseSmacker(void*);
    void ClearVideoResources();
    bool FinishVideoFrame();

    bool Active();
    void ToggleFullscreen();
    LPARAM MapMouseMessage(HWND window, UINT message, LPARAM position);
}
