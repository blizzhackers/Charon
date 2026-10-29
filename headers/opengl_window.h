#pragma once

#include "opengl_device.h"

namespace CharonGL::Window {
    bool Attach(HWND window);
    void Detach(HWND window);
    bool Fullscreen();
    bool SetFullscreen(bool fullscreen);
    bool IsStaleCursorClip(const RECT& clip, const RECT& client);
    void RepairCursorClip();
}
