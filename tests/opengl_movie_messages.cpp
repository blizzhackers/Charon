#include "headers/opengl_movie_messages.h"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Build movie-message checks with assertions enabled.
#endif

static DWORD timestamp = 0, previousTimestamp = 0;
static bool clickObserved = false;
static LRESULT CALLBACK MovieWindow(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (message == WM_CHAR) clickObserved = timestamp != previousTimestamp;
    return DefWindowProcA(window,message,wparam,lparam);
}

int main() {
    using namespace CharonGL;
    WNDCLASSA cls{};
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "CharonMoviePumpTest";
    cls.lpfnWndProc = MovieWindow;
    assert(RegisterClassA(&cls));
    const HWND window = CreateWindowA(cls.lpszClassName,"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
    assert(window);
    assert(PumpMovieMessage(timestamp) == MovieMessage::None);
    previousTimestamp = timestamp = GetTickCount()-1000;
    assert(PostMessageA(window,WM_CHAR,'x',0));
    assert(PumpMovieMessage(timestamp) == MovieMessage::Click);
    assert(clickObserved); // The window handler must see the timestamp already updated.
    assert(PostThreadMessageA(GetCurrentThreadId(),WM_CHAR,'x',0));
    assert(PumpMovieMessage(timestamp) == MovieMessage::Click);
    assert(PostThreadMessageA(GetCurrentThreadId(),WM_APP,0,0));
    assert(PumpMovieMessage(timestamp) == MovieMessage::Dispatched);
    PostQuitMessage(134);
    assert(PumpMovieMessage(timestamp) == MovieMessage::Quit);
    MSG quit{};
    assert(PeekMessageA(&quit,nullptr,0,0,PM_REMOVE));
    assert(quit.message == WM_QUIT && quit.wParam == 134);
    assert(PumpMovieMessage(timestamp) == MovieMessage::None);
    assert(DestroyWindow(window));
    assert(UnregisterClassA(cls.lpszClassName,cls.hInstance));
    std::puts("Movie pump preserves WM_QUIT and exit code.");
}
