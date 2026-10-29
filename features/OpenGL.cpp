#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/opengl_game.h"
#include "headers/opengl_window.h"
#include <windowsx.h>
#include <algorithm>

namespace CharonGL {
GameOptions* gameOptions = nullptr;
std::array<std::uint32_t, 256> palette{};
std::array<const std::uint8_t*, 72> paletteTables{};
Light globalLight{255,255,255,255};
std::uint8_t constantAlpha = 255;
Blend currentBlend = Blend::Replace;
bool videoPlaying = false;

namespace {
REMOTEREF(CRITICAL_SECTION*, RENDERER_SelectedRendererCore, 0x7c9134);
REMOTEREF(int, RENDERER_nWindowWidth, 0x7c9138);
REMOTEREF(int, RENDERER_nWindowHeight, 0x7c913c);
REMOTEREF(HWND, gnGlideWindowHandle, 0x7c9140);
REMOTEREF(int, gnRenderState, 0x7c9144);
REMOTEREF(GameOptions*, pD2GFXRenderedDataStrc, 0x7c9148);
REMOTEREF(void*, D2GfxRendereInitFunctionsStrc, 0x7c914c);
REMOTEREF(const std::uint8_t**, gpPaletteTable, 0x7c9150);
REMOTEREF(void*, gpfnSpriteCacheValidateCallback, 0x89db5c);
REMOTEREF(std::int32_t*, gpPerspectiveLookupTable, 0x880b6c);
REMOTEREF(std::uint8_t*, gpGlideLightingTable, 0x880b5c);
REMOTEREF(int, gnGlidePerspectiveScale, 0x880b64);
REMOTEREF(int, gbGlideInScene, 0x87efe4);
REMOTEREF(int, ghGlideWindow, 0x87efc8);
REMOTEREF(int, gbWINDOWED, 0x87efd4);
REMOTEREF(int, nScaleFactor, 0x7b57e4);
REMOTEREF(std::uint8_t, gbGlideGlobalLightRed, 0x880b5a);
REMOTEREF(std::uint8_t, gbGlideGlobalLightGreen, 0x880b58);
REMOTEREF(std::uint8_t, gbGlideGlobalLightBlue, 0x880b59);
REMOTEPTR(std::uint8_t, gnTextureMemoryUsageBytes, 0x7c90c8);
bool inScene = false, suspended = false, readyToPresent = false, applyWindowMode = false;
HWND gameWindow = nullptr;
float gammaValue = 1.0f;
std::array<std::int32_t,8192> perspectiveLut{};
std::array<std::uint8_t,65536> lightingLut{};
CRITICAL_SECTION coreLock;
bool lockInitialized = false;

bool CurvedScreen() {
    return Settings["openGLFilter"] % static_cast<unsigned>(Filter::Count) == static_cast<unsigned>(Filter::CrtCurved);
}

using TileEviction = void (__fastcall*)(void*);
// This leaf installs an eviction callback despite its database name.
REMOTEFUNC(void __fastcall, SPRITECACHE_SetMaxSize, (TileEviction callback), 0x5fe000);
REMOTEFUNC(void __stdcall, CELCMP_SetPixelDataFreeCallback, (TileEviction callback), 0x601a30);
REMOTEFUNC(void __fastcall, AdjustPerspectivePosition, (int,int,int,int*,int*), 0x50dd60);
REMOTEFUNC(void __fastcall, PerspectiveScalePosition, (int,int,int,int*,int*,BOOL), 0x50de10);
REMOTEFUNC(void __fastcall, SetPerspectiveScale, (int,int), 0x50dd50);
REMOTEFUNC(void __fastcall, SetDefaultPerspectiveFactor, (), 0x50df00);
REMOTEFUNC(void __stdcall, RENDERER_GLIDE_BuildPerspectiveLUT, (), 0x50dcd0);
REMOTEFUNC(void __stdcall, RENDERER_GLIDE_BuildLightingTable, (), 0x50db70);
REMOTEFUNC(void __stdcall, RENDERER_GLIDE_InitPerspectiveTables, (), 0x50c760);
REMOTEFUNC(void __cdecl, RENDERER_GLIDE_InitGroundTileVertices, (), 0x50d540);

void __fastcall ClearCaches() { ClearSpriteCache(); ClearTileCache(); }
BOOL __fastcall Initialize(HINSTANCE) { return TRUE; }
BOOL __fastcall InitPerspective(GameOptions* options, void* helpers) {
    if (!options || !helpers) { Device::Fail("Missing game graphics options/helpers"); return FALSE; }
    if (gpfnSpriteCacheValidateCallback) { Device::Fail("Unexpected sprite validation callback"); return FALSE; }
    gameOptions = options;
    pD2GFXRenderedDataStrc = options; D2GfxRendereInitFunctionsStrc = helpers;
    gpPerspectiveLookupTable = perspectiveLut.data(); gpGlideLightingTable = lightingLut.data();
    // These recovered, call-free CPU leaves initialize the original math tables.
    RENDERER_GLIDE_BuildPerspectiveLUT();
    RENDERER_GLIDE_BuildLightingTable();
    RENDERER_GLIDE_InitPerspectiveTables();
    RENDERER_GLIDE_InitGroundTileVertices();
    if (!lockInitialized) { InitializeCriticalSection(&coreLock); lockInitialized = true; }
    RENDERER_SelectedRendererCore = &coreLock;
    SPRITECACHE_SetMaxSize(InvalidateTile);
    CELCMP_SetPixelDataFreeCallback(nullptr);
    return TRUE;
}
BOOL __fastcall DestroyWindow() {
    if (inScene) { inScene = false; gbGlideInScene = 0; LeaveCriticalSection(&coreLock); }
    readyToPresent = false;
    ClearCaches(); Device::Destroy(); gameWindow = nullptr;
    gnGlideWindowHandle = nullptr; ghGlideWindow = 0;
    return TRUE;
}
BOOL __fastcall Release() {
    ClearVideoResources();
    DestroyWindow(); SPRITECACHE_SetMaxSize(nullptr); CELCMP_SetPixelDataFreeCallback(nullptr);
    if (lockInitialized) { DeleteCriticalSection(&coreLock); lockInitialized = false; }
    RENDERER_SelectedRendererCore = nullptr; gpPerspectiveLookupTable = nullptr; gpGlideLightingTable = nullptr;
    pD2GFXRenderedDataStrc = nullptr; D2GfxRendereInitFunctionsStrc = nullptr;
    gameOptions = nullptr; return TRUE;
}
BOOL __fastcall CreateGameWindow(HWND window,int mode) {
    if (Device::Ready()) return TRUE; // Original slot 3 is a no-op once initialized.
    if (mode < 0 || mode > 2) { Device::Fail("Unsupported logical resolution mode (expected 0, 1 or 2)"); return FALSE; }
    const Size canvas = mode == 0 ? Size{640,480} : Size{800,600};
    RENDERER_nWindowWidth = canvas.width; RENDERER_nWindowHeight = canvas.height; gnGlideWindowHandle = window;
    gameWindow = window; suspended = false;
    if (!Device::Create(window,canvas)) { gameWindow = nullptr; gnGlideWindowHandle = nullptr; return FALSE; }
    if (!Window::Attach(window)) { Device::Fail("Unable to capture game window placement"); DestroyWindow(); return FALSE; }
    applyWindowMode = true;
    InitializeTileCacheThread();
    ghGlideWindow = 1; gnRenderState = 0;
    if (gameOptions) Device::SetVsync(gameOptions->vsync != 0);
    return TRUE;
}
BOOL __fastcall ResizeWindow(HWND window,int mode) {
    if (!Device::Ready() || window != gameWindow) { DestroyWindow(); return CreateGameWindow(window,mode); }
    if (mode < 0 || mode > 2) { Device::Fail("Unsupported logical resolution mode"); return FALSE; }
    const Size canvas = mode == 0 ? Size{640,480} : Size{800,600};
    ClearCaches();
    if (!Device::ResizeCanvas(canvas)) return FALSE;
    RENDERER_nWindowWidth = canvas.width; RENDERER_nWindowHeight = canvas.height;
    return TRUE;
}
void __fastcall EndCutScene(HWND window,int mode,int state) {
    if (state) { DestroyWindow(); suspended = true; }
    else { suspended = !CreateGameWindow(window,mode); }
    gnRenderState = suspended ? 1 : 0;
}
BOOL __fastcall BeginScene(BOOL clear,std::uint8_t red,std::uint8_t green,std::uint8_t blue) {
    if (suspended || !Device::Ready() || inScene || !lockInitialized) return FALSE;
    EnterCriticalSection(&coreLock);
    if (!Device::BeginFrame()) { LeaveCriticalSection(&coreLock); return FALSE; }
    inScene = true; gbGlideInScene = 1;
    readyToPresent = false;
    if (clear) {
        // Original packs nibbles into a GR_COLORFORMAT_RGBA word.
        const unsigned color=(((unsigned(red)<<4 | green)<<4 | blue)<<4);
        Device::Clear(std::uint8_t(color>>24),std::uint8_t(color>>16),std::uint8_t(color>>8));
    }
    globalLight = {255,255,255,255};
    return TRUE;
}
void ApplyWindowMode() {
    if (!Active() || !applyWindowMode) return;
    // Common creation applies one final SetWindowPos after the renderer callback.
    if (!Window::SetFullscreen(Settings["openGLFullscreen"] != 0)) {
        Device::Fail("Unable to apply game display mode");
        Settings["openGLFullscreen"] = Window::Fullscreen();
    }
    applyWindowMode = false;
}
BOOL __fastcall FinishScene() {
    if (!inScene) return FALSE;
    ApplyWindowMode();
    const Filter filter = static_cast<Filter>(Settings["openGLFilter"] % static_cast<unsigned>(Filter::Count));
    const bool result = Device::Finish(filter,gammaValue);
    readyToPresent = result;
    inScene = false; gbGlideInScene = 0; LeaveCriticalSection(&coreLock);
    return result;
}
BOOL __fastcall PresentScene() {
    if (!readyToPresent) return FALSE;
    readyToPresent = false;
    if (gameOptions) Device::SetVsync(gameOptions->vsync != 0);
    return Device::Present();
}
BOOL __fastcall GetBackBuffer(std::uint8_t* rgb) {
    if (!rgb) return FALSE;
    std::vector<std::uint8_t> rgba;
    if (!Device::ReadCanvas(rgba)) return FALSE;
    for (std::size_t i=0,j=0;i<rgba.size();i+=4,j+=3) {
        rgb[j]=rgba[i]&0xf8; rgb[j+1]=rgba[i+1]&0xfc; rgb[j+2]=rgba[i+2]&0xf8;
    }
    return TRUE;
}
int __fastcall ActivateWindow(BOOL,BOOL) { return 0; }
BOOL __fastcall SetOption(int option,std::intptr_t value) {
    if (option == 10) { if (!value) return FALSE; const int scene=inScene ? 1 : 0; std::memcpy(reinterpret_cast<void*>(value),&scene,4); return TRUE; }
    if (option == 11) { gbWINDOWED = static_cast<int>(value); return TRUE; }
    return FALSE;
}
BOOL __fastcall BeginCutScene() {
    if (!Active()) return FALSE;
    gammaValue=1.0f;
    return ResizeWindow(gameWindow,0);
}
int __fastcall CheckCutScene() { return videoPlaying ? 1 : 0; }
void* __fastcall GetRenderStatistics() { return gnTextureMemoryUsageBytes; }
int __fastcall GetScreenSize(int* width,int* height) {
    const Size size{RENDERER_nWindowWidth,RENDERER_nWindowHeight};
    if (width) *width=size.width; if (height) *height=size.height; return size.width;
}
void __fastcall UpdateScaleFactor(int scale) { nScaleFactor = scale; }
int __fastcall SetGamma(int gamma) { gammaValue=(std::max)(0.01f,gamma/155.0f); return 0; }
BOOL __fastcall CheckGamma() { return TRUE; }
void __fastcall SetPalette(const PALETTEENTRY* colors) {
    if (!colors) { Device::Fail("Null palette"); return; }
    for (std::size_t i=0;i<palette.size();++i) palette[i]=Rgba(colors[i].peRed,colors[i].peGreen,colors[i].peBlue);
}
void __fastcall SetPaletteTable(const std::uint8_t* const* tables) {
    if (!tables) { Device::Fail("Null palette table"); return; }
    std::memcpy(paletteTables.data(),tables,sizeof(paletteTables)); gpPaletteTable = paletteTables.data();
}
void __fastcall SetGlobalLight(std::uint8_t red,std::uint8_t green,std::uint8_t blue) {
    globalLight={255,red,green,blue}; gbGlideGlobalLightRed = red; gbGlideGlobalLightGreen = green; gbGlideGlobalLightBlue = blue;
}
void __fastcall ClearScreen(BOOL) { Device::Clear(); }
void __fastcall DebugFillBackBuffer(int,int) {} // Original slot 52 is a RET-only leaf.
}

bool FinishVideoFrame() {
    ApplyWindowMode();
    const Filter filter = static_cast<Filter>(Settings["openGLFilter"] % static_cast<unsigned>(Filter::Count));
    if (gameOptions) Device::SetVsync(gameOptions->vsync != 0);
    return Device::Finish(filter,gammaValue) && Device::Present();
}

void SelectDrawMode(int mode) {
    switch(mode) {
    case 0: constantAlpha=64; currentBlend=Blend::Alpha; break;
    case 1: constantAlpha=128; currentBlend=Blend::Alpha; break;
    case 2: constantAlpha=192; currentBlend=Blend::Alpha; break;
    case 3: constantAlpha=255; currentBlend=Blend::Add; break;
    case 4: constantAlpha=255; currentBlend=Blend::Multiply; break;
    case 5: case 7: currentBlend=Blend::Replace; break;
    default: currentBlend=Blend::Alpha; break;
    }
}
bool PerspectiveEnabled() { return gameOptions && gameOptions->perspective && gameOptions->perspectiveCapable; }
int PerspectiveScale() { return gnGlidePerspectiveScale; }
void Project(std::int32_t x,std::int32_t y,std::int32_t bias,int& screenX,int& screenY) { AdjustPerspectivePosition(x,y,bias,&screenX,&screenY); }
bool Active() { return Device::Ready() && !suspended; }
void ToggleFullscreen() {
    const bool full = !Window::Fullscreen();
    if (!Window::SetFullscreen(full)) { Device::Fail("Unable to change game display mode"); return; }
    Settings["openGLFullscreen"] = full;
    SaveSettings();
}
LPARAM MapMouseMessage(HWND window,UINT message,LPARAM position) {
    if (!Active() || message < WM_MOUSEFIRST || message > WM_MOUSELAST) return position;
    POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
    const bool screen = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
    if (screen) ScreenToClient(window,&point);
    RECT client{}; GetClientRect(window,&client);
    point=MapMouse(point,Device::CanvasSize(),{client.right,client.bottom},CurvedScreen());
    if (screen) ClientToScreen(window,&point);
    return MAKELPARAM(static_cast<short>(point.x),static_cast<short>(point.y));
}

namespace {
struct RendererTable {
    decltype(&Initialize) Initialize;
    decltype(&InitPerspective) InitPerspective;
    decltype(&Release) Release;
    decltype(&CreateGameWindow) CreateGameWindow;
    decltype(&DestroyWindow) DestroyWindow;
    decltype(&EndCutScene) EndCutScene;
    decltype(&BeginScene) BeginScene;
    decltype(&FinishScene) FinishScene;
    decltype(&PresentScene) PresentScene;
    decltype(&ResizeWindow) ResizeWindow;
    decltype(&GetBackBuffer) GetBackBuffer;
    decltype(&ActivateWindow) ActivateWindow;
    decltype(&SetOption) SetOption;
    decltype(&BeginCutScene) BeginCutScene;
    decltype(&PlayCutScene) PlayCutScene;
    decltype(&CheckCutScene) CheckCutScene;
    decltype(&DecodeSmacker) DecodeSmacker;
    decltype(&PlayerSmacker) PlayerSmacker;
    decltype(&CloseSmacker) CloseSmacker;
    decltype(&GetRenderStatistics) GetRenderStatistics;
    decltype(&GetScreenSize) GetScreenSize;
    decltype(&UpdateScaleFactor) UpdateScaleFactor;
    decltype(&SetGamma) SetGamma;
    decltype(&CheckGamma) CheckGamma;
    decltype(SetPerspectiveScale) setOrigin;
    decltype(AdjustPerspectivePosition) adjustPosition;
    decltype(PerspectiveScalePosition) scalePosition;
    decltype(SetDefaultPerspectiveFactor) defaultPerspective;
    decltype(&SetPalette) SetPalette;
    decltype(&SetPaletteTable) SetPaletteTable;
    decltype(&SetGlobalLight) SetGlobalLight;
    decltype(&DrawGroundTile) DrawGroundTile;
    decltype(&DrawPerspectiveImage) DrawPerspectiveImage;
    decltype(&DrawImage) DrawImage;
    decltype(&DrawShiftedImage) DrawShiftedImage;
    decltype(&DrawVerticalCropImage) DrawVerticalCropImage;
    decltype(&DrawShadow) DrawShadow;
    decltype(&DrawImageFast) DrawImageFast;
    decltype(&DrawClippedImage) DrawClippedImage;
    decltype(&DrawWallTile) DrawWallTile;
    decltype(&DrawTransWallTile) DrawTransWallTile;
    decltype(&DrawShadowTile) DrawShadowTile;
    decltype(&DrawRect) DrawRect;
    decltype(&DrawRectEx) DrawRectEx;
    decltype(&DrawSolidRect) DrawSolidRect;
    decltype(&DrawSolidSquare) DrawSolidSquare;
    decltype(&DrawSolidRectEx) DrawSolidRectEx;
    decltype(&DrawSolidRectAlpha) DrawSolidRectAlpha;
    decltype(&DrawLine) DrawLine;
    decltype(&ClearScreen) ClearScreen;
    decltype(&DrawString) DrawString;
    decltype(&DrawLight) DrawLight;
    decltype(&DebugFillBackBuffer) DebugFillBackBuffer;
    decltype(&ClearCaches) ClearCaches;
};
static_assert(sizeof(void*) == 4, "Renderer seam requires x86");
static_assert(sizeof(RendererTable) == 216, "54 renderer callbacks");
static_assert(offsetof(RendererTable, FinishScene) == 0x1c, "Finish slot");
static_assert(offsetof(RendererTable, PresentScene) == 0x20, "Present slot");
static_assert(offsetof(RendererTable, ClearCaches) == 0xd4, "Last slot");
const RendererTable renderer = {
    Initialize, InitPerspective, Release, CreateGameWindow, DestroyWindow, EndCutScene, BeginScene, FinishScene, PresentScene, ResizeWindow, GetBackBuffer, ActivateWindow, SetOption, BeginCutScene, PlayCutScene, CheckCutScene, DecodeSmacker, PlayerSmacker, CloseSmacker, GetRenderStatistics, GetScreenSize, UpdateScaleFactor, SetGamma, CheckGamma, SetPerspectiveScale, AdjustPerspectivePosition, PerspectiveScalePosition, SetDefaultPerspectiveFactor, SetPalette, SetPaletteTable, SetGlobalLight, DrawGroundTile, DrawPerspectiveImage, DrawImage, DrawShiftedImage, DrawVerticalCropImage, DrawShadow, DrawImageFast, DrawClippedImage, DrawWallTile, DrawTransWallTile, DrawShadowTile, DrawRect, DrawRectEx, DrawSolidRect, DrawSolidSquare, DrawSolidRectEx, DrawSolidRectAlpha, DrawLine, ClearScreen, DrawString, DrawLight, DebugFillBackBuffer, ClearCaches
};
}

namespace {
BOOL WINAPI SetCanvasCursorPosition(int x,int y) {
    if (!Active()) return SetCursorPos(x,y);
    RECT client{};
    if (!GetClientRect(gameWindow,&client)) return FALSE;
    const Size canvas=Device::CanvasSize();
    if (canvas.width <= 0 || canvas.height <= 0) return FALSE;
    POINT point=MapCursor({x,y},canvas,{client.right,client.bottom},CurvedScreen());
    if (!ClientToScreen(gameWindow,&point)) return FALSE;
    return SetCursorPos(point.x,point.y);
}
int __fastcall SelectNativeRenderer(D2::Types::IniConfigStrc* config) {
    config->b3DFX = config->bWINDOW = true;
    config->bD3D = config->bOPENGL = config->bRAVE = false;
    return 4;
}
BOOL WINAPI KeepOutputWindowSize(HWND,HWND,int,int,int,int,UINT) { return TRUE; }
class : public Feature {
public:
    void init() override {
        Settings.try_emplace("openGLFullscreen",1);
        for (const char* key : {"openGL","alwaysD3D","alwaysD3DFilter","alwaysD3DStartFull"}) Settings.erase(key);
        // Install native callbacks in the mode-4 renderer selector.
        MemoryPatch(0x72da90) << DATA(&renderer);
        // Select mode 4 without overwriting the startup failure handler.
        MemoryPatch(0x405cb3) << BYTESEQ{0x8d,0x0e} << CALL(SelectNativeRenderer)
            << BYTESEQ{0x89,0x45,0x08} << JUMP(0x405ced);
        // Create a normal window; native presentation manages fullscreen.
        MemoryPatch(0x405cf1) << BYTESEQ{0xb3,0x01} << NOP_TO(0x405cf7);
        // Logical resolution changes must not resize the physical output window.
        MemoryPatch(0x4f5b8b) << CALL(KeepOutputWindowSize) << ASM::NOP;
        // Use the logical branch even in windowed mode; our warp applies scaling and screen origin.
        MemoryPatch(0x4687b1) << BYTESEQ{0xeb,0x48};
        // Map explicit cursor warps through the canvas viewport.
        MemoryPatch(0x468758) << CALL(SetCanvasCursorPosition) << ASM::NOP;
        MemoryPatch(0x4687fd) << CALL(SetCanvasCursorPosition) << ASM::NOP;
        // Map cursor clamping in gameplay and menus through the same viewport.
        MemoryPatch(0x4688bd) << CALL(SetCanvasCursorPosition) << ASM::NOP;
        MemoryPatch(0x4fa742) << CALL(SetCanvasCursorPosition) << ASM::NOP;
    }
    void oogLoop() override { if (Active()) Window::RepairCursorClip(); }
    void gameLoop() override { if (Active()) Window::RepairCursorClip(); }
    bool windowMessage(HWND window,UINT message,WPARAM key,LPARAM flags) override {
        if (message == WM_NCDESTROY) Window::Detach(window);
        if (window != gameWindow) return true;
        if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR) &&
            key == VK_RETURN && (flags & 0x20000000)) {
            if (message == WM_SYSKEYDOWN && !(flags & 0x40000000)) ToggleFullscreen();
            return false;
        }
        return true;
    }
    void deinit() override { Release(); }
} feature;
}
}
