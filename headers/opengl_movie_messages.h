#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace CharonGL {
    enum class MovieMessage { None, Dispatched, Click, Quit };
    inline MovieMessage PumpMovieMessage(DWORD& clickTimestamp) {
        MSG message{};
        if (!PeekMessageA(&message,nullptr,0,0,PM_REMOVE)) return MovieMessage::None;
        if (message.message == WM_QUIT) {
            PostQuitMessage(int(message.wParam));
            return MovieMessage::Quit;
        }
        const bool clicked = message.message == WM_CHAR || message.message == WM_LBUTTONDOWN ||
            message.message == WM_RBUTTONDOWN || message.message == WM_MBUTTONDOWN;
        if (clicked) clickTimestamp = GetTickCount();
        TranslateMessage(&message);
        DispatchMessageA(&message);
        return clicked ? MovieMessage::Click : MovieMessage::Dispatched;
    }
}
