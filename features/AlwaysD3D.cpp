#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <d3d.h>
#include <iostream>
#include <chrono>
#include <thread>

REMOTEREF(DWORD, InternalWidth, 0x7c9138);
REMOTEREF(DWORD, InternalHeight, 0x7c913c);
REMOTEREF(DWORD, ScreenWidth, 0x71146c);
REMOTEREF(DWORD, ScreenHeight, 0x711470);
REMOTEREF(DWORD, ScreenMode, 0x7a5218);
REMOTEREF(DWORD, Dunno, 0x7a2858);
REMOTEREF(BOOL, FixAspect, 0x72da7c);
REMOTEREF(LPDIRECTDRAW7, IDirectDraw, 0x970e60);
REMOTEREF(LPDIRECT3D3, IDirect3D, 0x970e64);
REMOTEREF(LPDIRECTDRAWSURFACE7, D3DSurfacePrimary, 0x970e68);
REMOTEREF(LPDIRECTDRAWSURFACE7, D3DSurfaceSecondary, 0x970e6c);
REMOTEREF(LPDIRECT3DDEVICE3, Direct3DDevice, 0x970e78);
REMOTEREF(LPDIRECT3DVIEWPORT3, Direct3DViewport, 0x970e7c);
REMOTEREF(DWORD, GameType, 0x7a0610);
REMOTEPTR(DWORD, unknownInBeginScene, 0x970e54);
REMOTEFUNC(void, D3D_DirectDrawPostSetup, (), 0x6b8f10);

LPDIRECTDRAWSURFACE7 D3DSurfaceBackground = nullptr;
bool bD3DFull = false;
bool repaintsides = false;
DWORD dwOldStyle = 0;
DWORD dwOldStyleEx = 0;

void D3D_DirectDrawScreenSetup() {
    InternalWidth = 800;
    InternalHeight = 600;
    IDirectDraw->SetCooperativeLevel(D2::hWnd, DDSCL_NORMAL);
    if (!dwOldStyle) {
        dwOldStyle = GetWindowLong(D2::hWnd, GWL_STYLE);
        dwOldStyleEx = GetWindowLong(D2::hWnd, GWL_EXSTYLE);
        dwOldStyle &= ~(WS_MAXIMIZEBOX);
        dwOldStyle &= ~(WS_EX_TOPMOST);
        SetWindowLong(D2::hWnd, GWL_STYLE, dwOldStyle);
        SetWindowLong(D2::hWnd, GWL_EXSTYLE, dwOldStyleEx);
    }
}

void D3D_DirectDrawPostSetup_Intercept() {
    D3D_DirectDrawPostSetup();
}

void ScreenSizeHook() {
    ScreenWidth = InternalWidth;
    ScreenHeight = InternalHeight;
    ScreenMode = 1;
}

void __stdcall GetWindowSizeByResolutionMode(int res, long* nWidth, long* nHeight) {
    *nWidth = InternalWidth;
    *nHeight = InternalHeight;
}

void ToggleFullscreen() {
    bD3DFull = !bD3DFull;

    if (bD3DFull) {
        ShowWindow(D2::hWnd, SW_RESTORE);
        SetWindowLong(D2::hWnd, GWL_STYLE, dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        ShowWindow(D2::hWnd, SW_MAXIMIZE);
    }
    else {
        ShowWindow(D2::hWnd, SW_RESTORE);
        SetWindowLong(D2::hWnd, GWL_STYLE, dwOldStyle);
    }

    repaintsides = true;
    SetWindowPos(D2::hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
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
        IDirectDraw->CreateClipper(0, &Clipper, NULL);
        Clipper->SetHWnd(0, D2::hWnd);
        ppSurface[0]->SetClipper(Clipper);
        Clipper->Release();
    }

    if (D3DSurfaceBackground == nullptr) {
        DDSURFACEDESC2 caps2{ sizeof(DDSURFACEDESC2) };
        caps2.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
        caps2.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
        caps2.dwWidth = 32;
        caps2.dwHeight = 32;
        IDirectDraw->CreateSurface(&caps2, &D3DSurfaceBackground, NULL);
    }

    return ret;
}

HRESULT __stdcall CreateSecondarySurfaceIntercept(IDirectDrawSurface7* dds, LPDDSCAPS2 ddsc, LPDIRECTDRAWSURFACE7* ppSurface) {
    DDSURFACEDESC2 caps{ sizeof(DDSURFACEDESC2) };
    caps.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    caps.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE;
    caps.dwWidth = InternalWidth;
    caps.dwHeight = InternalHeight;
    return IDirectDraw->CreateSurface(&caps, ppSurface, NULL);
}

void ClearScreen() {
    D3DRECT r{ 0, 0, (long)InternalWidth, (long)InternalHeight };
    Direct3DViewport->Clear2(1, &r, D3DCLEAR_TARGET, 0xFF000000, 0, 0);
}

// Keeps the game at a steady framerate without using too much CPU.
// D2 doesn't do a great job at it by default, so we're helping out.
void throttle() {
    // Single player doesn't need throttling. It runs at 25 fps already.
    if (State["ingame"] && GameType < 1) {
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
    Direct3DDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, filterType);
    Direct3DDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_POINT);

    return true;
}

BOOL __fastcall FlipSurfaces() {
    RECT windowrect, targetrect{ 0 };

    throttle();

    GetClientRect(D2::hWnd, &windowrect);
    MapWindowPoints(D2::hWnd, NULL, (LPPOINT)&windowrect, 2);

    long newWidth = (windowrect.bottom - windowrect.top) * InternalWidth / InternalHeight;
    targetrect.top = windowrect.top;
    targetrect.bottom = windowrect.bottom;
    targetrect.left = (windowrect.left + windowrect.right - newWidth) / 2;
    targetrect.right = targetrect.left + newWidth;

    if (repaintsides) {
        D3DSurfacePrimary->Blt(&windowrect, D3DSurfaceBackground, NULL, DDBLT_WAIT, NULL);
        repaintsides = false;
    }

    DDBLTFX fx{ 0 };
    fx.dwSize = sizeof(DDBLTFX);
    D3DSurfacePrimary->Blt(&targetrect, D3DSurfaceSecondary, NULL, DDBLT_WAIT, &fx);

    return TRUE;
}

void __fastcall UpdateD2INI(D2::Types::IniConfigStrc* d2ini) {
    d2ini->bASPECT = d2ini->bD3D = true;
    d2ini->bWINDOW = d2ini->b3DFX = d2ini->bOPENGL = d2ini->bRAVE = false;
}

BOOL __stdcall SetWindowPosStub(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    return true;
}

namespace AlwaysD3D {

    class : public Feature {
        bool startFull = false;
    public:
        void init() {
            if (Settings["alwaysD3D"]) {
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
            if (startFull) {
                ToggleFullscreen();
                startFull = false;
            }
        }
    } feature;

}
