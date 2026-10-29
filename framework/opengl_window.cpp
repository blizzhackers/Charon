#include "headers/opengl_window.h"

namespace CharonGL::Window {
namespace {
    HWND window = nullptr;
    LONG_PTR windowedStyle = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    bool fullscreen = false;

    bool SetStyle(LONG_PTR style) {
        SetLastError(ERROR_SUCCESS);
        return SetWindowLongPtr(window,GWL_STYLE,style) != 0 || GetLastError() == ERROR_SUCCESS;
    }
}

bool Attach(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    if (window == hwnd) return true; // GPU recreation keeps the same window placement.
    WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
    if (!GetWindowPlacement(hwnd,&saved)) return false;
    window = hwnd;
    windowedStyle = GetWindowLongPtr(window,GWL_STYLE) & ~(WS_MINIMIZE | WS_MAXIMIZE);
    placement = saved;
    fullscreen = false;
    return true;
}

void Detach(HWND hwnd) {
    if (window != hwnd) return;
    window = nullptr;
    fullscreen = false;
}

bool Fullscreen() { return fullscreen; }

bool SetFullscreen(bool full) {
    if (!window || !IsWindow(window)) return false;
    if (fullscreen == full) return true;
    const UINT flags = SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    const LONG_PTR oldStyle = GetWindowLongPtr(window,GWL_STYLE);
    WINDOWPLACEMENT oldPlacement{sizeof(WINDOWPLACEMENT)};
    if (!GetWindowPlacement(window,&oldPlacement)) return false;
    if (!(oldStyle & WS_VISIBLE)) oldPlacement.showCmd = SW_HIDE;
    bool changed = false;
    if (full) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfo(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) return false;
        if (IsZoomed(window) || IsIconic(window)) ShowWindow(window,SW_RESTORE);
        const RECT& rect = monitor.rcMonitor;
        changed = SetStyle((windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP) &&
            SetWindowPos(window,HWND_NOTOPMOST,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,flags);
        if (changed) placement = oldPlacement;
    } else {
        changed = SetStyle(windowedStyle) && SetWindowPlacement(window,&placement) &&
            SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,flags | SWP_NOMOVE | SWP_NOSIZE);
    }
    if (!changed) {
        SetStyle(oldStyle);
        SetWindowPlacement(window,&oldPlacement);
        SetWindowPos(window,nullptr,0,0,0,0,flags | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        return false;
    }
    fullscreen = full;
    return true;
}

bool IsStaleCursorClip(const RECT& clip, const RECT& client) {
    const LONG width = clip.right-clip.left, height = clip.bottom-clip.top;
    // A PlugY lock can retain the old origin or canvas size across mode changes.
    const bool logicalSize = (width == 640 && height == 480) || (width == 800 && height == 600);
    return logicalSize && (width < client.right-client.left || height < client.bottom-client.top);
}

void RepairCursorClip() {
    if (!window || GetForegroundWindow() != window || IsIconic(window) || !IsWindowVisible(window)) return;
    RECT client{}, clip{};
    if (!GetClientRect(window,&client) || !GetClipCursor(&clip)) return;
    if (IsStaleCursorClip(clip,client)) ClipCursor(nullptr);
}
}
