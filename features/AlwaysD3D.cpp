#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <d3d.h>
#include <iostream>
#include <chrono>
#include <thread>

REMOTEREF(DWORD, RENDERER_nWindowWidth, 0x7c9138);
REMOTEREF(DWORD, RENDERER_nWindowHeight, 0x7c913c);
REMOTEREF(DWORD, ScreenSizeX, 0x71146c);
REMOTEREF(DWORD, ScreenSizeY, 0x711470);
REMOTEREF(DWORD, nScreenMode, 0x7a5218);
REMOTEREF(LPDIRECTDRAW7, g_pDirectDraw7, 0x970e60);
REMOTEREF(LPDIRECTDRAWSURFACE7, g_pD3DSurfaceBackbuffer, 0x970e68);
REMOTEREF(LPDIRECTDRAWSURFACE7, D3DSurfaceSecondary, 0x970e6c);
REMOTEREF(LPDIRECT3DDEVICE3, g_pD3DDevice, 0x970e78);
REMOTEREF(LPDIRECT3DVIEWPORT3, g_pD3DViewport, 0x970e7c);
REMOTEFUNC(void, RENDERER_D3D_InitGroundTileCache, (), 0x6b8f10);

LPDIRECTDRAWSURFACE7 D3DSurfaceBackground = nullptr;
bool bD3DFull = false;
bool repaintsides = false;
DWORD dwOldStyle = 0;
DWORD dwOldStyleEx = 0;

void D3D_DirectDrawScreenSetup() {
    RENDERER_nWindowWidth = 800;
    RENDERER_nWindowHeight = 600;
    g_pDirectDraw7->SetCooperativeLevel(D2::hwndD2GFX_Window, DDSCL_NORMAL | DDSCL_NOWINDOWCHANGES);
    if (!dwOldStyle) {
        dwOldStyle = GetWindowLong(D2::hwndD2GFX_Window, GWL_STYLE);
        dwOldStyleEx = GetWindowLong(D2::hwndD2GFX_Window, GWL_EXSTYLE);
        dwOldStyle &= ~(WS_MAXIMIZEBOX);
        dwOldStyle &= ~(WS_EX_TOPMOST);
        SetWindowLong(D2::hwndD2GFX_Window, GWL_STYLE, dwOldStyle);
        SetWindowLong(D2::hwndD2GFX_Window, GWL_EXSTYLE, dwOldStyleEx);
    }
}

void D3D_DirectDrawPostSetup_Intercept() {
    RENDERER_D3D_InitGroundTileCache();
}

void ScreenSizeHook() {
    ScreenSizeX = RENDERER_nWindowWidth;
    ScreenSizeY = RENDERER_nWindowHeight;
    nScreenMode = 1;
}

void __stdcall GetWindowSizeByResolutionMode(int res, long* nWidth, long* nHeight) {
    *nWidth = RENDERER_nWindowWidth;
    *nHeight = RENDERER_nWindowHeight;
}

void ToggleFullscreen() {
    bD3DFull = !bD3DFull;

    if (bD3DFull) {
        ShowWindow(D2::hwndD2GFX_Window, SW_RESTORE);
        SetWindowLong(D2::hwndD2GFX_Window, GWL_STYLE, dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        ShowWindow(D2::hwndD2GFX_Window, SW_MAXIMIZE);
    }
    else {
        ShowWindow(D2::hwndD2GFX_Window, SW_RESTORE);
        SetWindowLong(D2::hwndD2GFX_Window, GWL_STYLE, dwOldStyle);
    }

    repaintsides = true;
    SetWindowPos(D2::hwndD2GFX_Window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void __cdecl LogFileDivert(char* format, ...) {
    int res;
    va_list list;
    va_start(list, format);
    res = _vfprintf_l(stdout, format, NULL, list);
    va_end(list);
    puts("");
    return;
}

HRESULT __stdcall CreatePrimarySurfaceIntercept(IDirectDraw7* ddi, LPDDSURFACEDESC2 ddsd, LPDIRECTDRAWSURFACE7* ppSurface, IUnknown* unknown) {
    DDSURFACEDESC2 caps{ sizeof(DDSURFACEDESC2) };
    ddsd->dwFlags = DDSD_CAPS;
    ddsd->dwBackBufferCount = 0;
    ddsd->dwDepth = 1;
    ddsd->ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE;

    HRESULT ret = ddi->CreateSurface(ddsd, ppSurface, unknown);

    if (ret == DD_OK) {
        IDirectDrawClipper* Clipper;
        g_pDirectDraw7->CreateClipper(0, &Clipper, NULL);
        Clipper->SetHWnd(0, D2::hwndD2GFX_Window);
        ppSurface[0]->SetClipper(Clipper);
        Clipper->Release();
    }

    if (D3DSurfaceBackground == nullptr) {
        DDSURFACEDESC2 caps2{ sizeof(DDSURFACEDESC2) };
        caps2.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
        caps2.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        caps2.dwWidth = 32;
        caps2.dwHeight = 32;
        g_pDirectDraw7->CreateSurface(&caps2, &D3DSurfaceBackground, NULL);
    }

    return ret;
}

HRESULT __stdcall CreateSecondarySurfaceIntercept(IDirectDrawSurface7* dds, LPDDSCAPS2 ddsc, LPDIRECTDRAWSURFACE7* ppSurface) {
    DDSURFACEDESC2 caps{ sizeof(DDSURFACEDESC2) };
    caps.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    caps.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE;
    caps.dwWidth = RENDERER_nWindowWidth;
    caps.dwHeight = RENDERER_nWindowHeight;
    return g_pDirectDraw7->CreateSurface(&caps, ppSurface, NULL);
}

void ClearScreen() {
    D3DRECT r{ 0, 0, (long)RENDERER_nWindowWidth, (long)RENDERER_nWindowHeight };
    g_pD3DViewport->Clear2(1, &r, D3DCLEAR_TARGET, 0xFF000000, 0, 0);
}

// Keeps the game at a steady framerate without using too much CPU.
// D2 doesn't do a great job at it by default, so we're helping out.
void throttle() {
    // Single player doesn't need throttling. It runs at 25 fps already.
    if (State["inGame"] && D2::eD2GSGameType_007a0610 == 0) {
        return;
    }

    using frameDuration = std::chrono::duration<int64_t, std::ratio<40, 1000>>; // Wait for 40ms (25 fps)
    using std::chrono::system_clock;
    using std::this_thread::sleep_until;
    static system_clock::time_point nextFrame = system_clock::now(), now;

    now = system_clock::now();
    if (now < nextFrame) {
        sleep_until(nextFrame);
        nextFrame += frameDuration{ 1 };
    }
    else {
        nextFrame = system_clock::now() + frameDuration{ 1 };
    }
}

std::wstring filterParams[6] = {
    L"Default",
    L"Point",
    L"Linear",
    L"Flat Cubic",
    L"Gaussian Cubic",
    L"Anisotropic",
};

int filterValues[6] = {
    0,
    D3DTFG_POINT,
    D3DTFG_LINEAR,
    D3DTFG_FLATCUBIC,
    D3DTFG_GAUSSIANCUBIC,
    D3DTFG_ANISOTROPIC,
};

DWORD BeginScene() {
    // This is the highest filtering that retains the sprite outlines properly IMO.
    // Gaussian Cubic seems to deform the outlines a bit.
    DWORD filterType = filterValues[Settings["alwaysD3DFilter"]];
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, filterType);
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, filterType);
    g_pD3DDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, filterType);

    return true;
}

BOOL __fastcall FlipSurfaces() {
    RECT windowrect, targetrect{ 0 };

    throttle();

    GetClientRect(D2::hwndD2GFX_Window, &windowrect);
    MapWindowPoints(D2::hwndD2GFX_Window, NULL, (LPPOINT)&windowrect, 2);

    long newWidth = (windowrect.bottom - windowrect.top) * RENDERER_nWindowWidth / RENDERER_nWindowHeight;
    targetrect.top = windowrect.top;
    targetrect.bottom = windowrect.bottom;
    targetrect.left = (windowrect.left + windowrect.right - newWidth) / 2;
    targetrect.right = targetrect.left + newWidth;

    if (repaintsides) {
        g_pD3DSurfaceBackbuffer->Blt(&windowrect, D3DSurfaceBackground, NULL, DDBLT_WAIT, NULL);
        repaintsides = false;
    }

    DDBLTFX fx{ 0 };
    fx.dwSize = sizeof(DDBLTFX);
    g_pD3DSurfaceBackbuffer->Blt(&targetrect, D3DSurfaceSecondary, NULL, DDBLT_WAIT, &fx);

    return TRUE;
}

void __fastcall UpdateD2INI(D2::Types::IniConfigStrc* d2ini) {
    d2ini->bASPECT = d2ini->bD3D = true;
    d2ini->bWINDOW = d2ini->b3DFX = d2ini->bOPENGL = d2ini->bRAVE = false;
}

BOOL __stdcall SetWindowPosStub(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    return true;
}

// Coexist with PlugY's cursor lock.
//
// PlugY's lockMouseCursor (Windowed.cpp) reads ResolutionX/Y at
// D2Client + 0x31146C / 0x311470 — the exact bytes Charon writes to in
// ScreenSizeHook (0x71146C / 0x711470). When alwaysD3D stretches the
// window past 800x600 but ScreenSizeX/ScreenSizeY stay at 800x600 (we *must*
// keep them at the internal canvas size, otherwise the hundreds of D2
// internal call sites that use these globals as canvas dimensions for
// UI placement would draw off-surface), PlugY ends up calling
// ClipCursor with a rect that's smaller than the actual window — the
// cursor cannot leave the 800-px region anchored at the client's
// top-left. Match for GamingForFun's report: cursor cannot move past
// the right side of the game window when Charon + PlugY are loaded
// together.
//
// Detect a stale clip on each loop iteration and lift it. Only acts
// when the clip rect is strictly smaller than the window in either
// axis — so a deliberate /lockmouse via PlugY (or any clip that
// already matches the window) is left untouched.
void unclipIfStaleSmaller() {
    if (!D2::hwndD2GFX_Window) return;
    RECT clip, win;
    if (!GetClipCursor(&clip)) return;
    if (!GetWindowRect(D2::hwndD2GFX_Window, &win)) return;
    const LONG clipW = clip.right  - clip.left;
    const LONG clipH = clip.bottom - clip.top;
    const LONG winW  = win.right   - win.left;
    const LONG winH  = win.bottom  - win.top;
    if (clipW < winW || clipH < winH) {
        ClipCursor(NULL);
    }
}

namespace AlwaysD3D {

    class : public Feature {
        bool startFull = false, usingAD3D = false;
    public:
        void init() {
            if (Settings["alwaysD3D"]) {
                usingAD3D = true;
                // Prevent this MoveWindow call since it pushes the window off the screen.
                MemoryPatch(0x4f5b8b) << CALL(SetWindowPosStub) << ASM::NOP;

                MemoryPatch(0x405cb3)
                    << BYTESEQ{ 0xc7, 0x45, 0x08 }
                    << DWORD(6)
                    << BYTESEQ{ 0x8d, 0x0e }
                    << CALL(UpdateD2INI)
                    << NOP_TO(0x405ced); // Force this renderer
                MemoryPatch(0x405cf1)
                    << BYTESEQ{ 0xb3, 0x01, ASM::NOP } // Force this window mode
                << NOP_TO(0x405cf7);
                MemoryPatch(0x4f9050)
                    << BYTESEQ{ 0x31, 0xC0 }
                << ASM::RET; // Skip all videos; xor EAX and return
                MemoryPatch(0x6b5003)
                    << CALL(D3D_DirectDrawScreenSetup)
                    << NOP_TO(0x6b5046)
                    << SKIP(10)
                    << NOP_TO(0x6b50d4); // Override resolution, coop level, and screen mode
                MemoryPatch(0x6b5510) << CALL(D3D_DirectDrawPostSetup_Intercept);
                MemoryPatch(0x410610) << JUMP(LogFileDivert);
                MemoryPatch(0x6b5130) << CALL(CreatePrimarySurfaceIntercept);
                MemoryPatch(0x6b517d) << CALL(CreateSecondarySurfaceIntercept);
                MemoryPatch(0x6b1c30) << JUMP(FlipSurfaces);
                MemoryPatch(0x6b57be) << CALL(BeginScene) << BYTESEQ{ 0xc2, 0x08, 0x00 };
                MemoryPatch(0x4f5570) << JUMP(GetWindowSizeByResolutionMode);
                MemoryPatch(0x44ba30) << CALL(ScreenSizeHook) << NOP_TO(0x44ba7c);
                MemoryPatch(0x6b2030) << ASM::RET; // Disable min/mag filters.
                MemoryPatch(0x44f278) << NOP_TO(0x44f280); // Disable frame skipping


                HotkeyCallbacks[VK_RETURN] = [&](LPARAM options) -> BOOL {
                    if (options & 0x20000000) {
                        ToggleFullscreen();
                        return FALSE;
                    }

                    return TRUE;
                };

                startFull = Settings["alwaysD3DStartFull"];
            }
        }

        void allFinalDraw() {
            if (usingAD3D && startFull) {
                ToggleFullscreen();
                startFull = false;
            }
        }

        void oogLoop()  { if (usingAD3D) unclipIfStaleSmaller(); }
        void gameLoop() { if (usingAD3D) unclipIfStaleSmaller(); }
    } feature;

}
