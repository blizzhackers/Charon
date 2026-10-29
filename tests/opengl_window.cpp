#define NOMINMAX
#include "headers/opengl_window.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <crtdbg.h>

#ifdef NDEBUG
#error Build window checks with assertions enabled.
#endif

using namespace CharonGL;

static bool Same(const RECT& a, const RECT& b) { return EqualRect(&a,&b) != FALSE; }
static bool SamePoint(POINT a, POINT b) { return a.x == b.x && a.y == b.y; }

static void CheckMouseGeometry() {
    const Size canvases[] = {{640,480},{800,600}};
    for (const auto canvas : canvases) {
        const Size outputs[] = {canvas,{canvas.width*2,canvas.height*2},
            {canvas.width*3/2,canvas.height*3/2},{1920,1080},{900,1600},
            {1001,777},{200,150}};
        for (const auto output : outputs) {
            const auto view = FitViewport(canvas,output);
            assert(view.width > 0 && view.height > 0);
            const POINT clients[] = {{-100,-200},{view.x,view.y},
                {view.x+view.width/2,view.y+view.height/2},
                {view.x+view.width-1,view.y+view.height-1},
                {output.width+100,output.height+200}};
            for (const auto client : clients) {
                const auto x = (std::int64_t(client.x)-view.x)*canvas.width/view.width;
                const auto y = (std::int64_t(client.y)-view.y)*canvas.height/view.height;
                const POINT expected{LONG(std::clamp(x,std::int64_t(0),std::int64_t(canvas.width-1))),
                    LONG(std::clamp(y,std::int64_t(0),std::int64_t(canvas.height-1)))};
                assert(SamePoint(MapMouse(client,canvas,output),expected));
                assert(SamePoint(MapMouse(client,canvas,output,false),expected));
                const auto curved = MapMouse(client,canvas,output,true);
                assert(curved.x >= 0 && curved.x < canvas.width);
                assert(curved.y >= 0 && curved.y < canvas.height);
            }
            const POINT center{canvas.width/2,canvas.height/2};
            const auto curvedCenter = MapCursor(center,canvas,output,true);
            assert(curvedCenter.x == view.x+LONG(std::round(view.width*0.5)));
            assert(curvedCenter.y == view.y+LONG(std::round(view.height*0.5)));
            const auto centerClient = MapCursor(center,canvas,output);
            assert(SamePoint(MapMouse(centerClient,canvas,output,true),MapMouse(centerClient,canvas,output)));

            // Nearest-pixel cursor warps have at most half a client pixel of error; curvature amplifies it by up to 1.3.
            const int toleranceX = (std::max)(1,int(std::ceil(0.65*canvas.width/view.width)));
            const int toleranceY = (std::max)(1,int(std::ceil(0.65*canvas.height/view.height)));
            const int xs[] = {0,1,canvas.width/4,canvas.width/2,canvas.width*3/4,canvas.width-2,canvas.width-1};
            const int ys[] = {0,1,canvas.height/4,canvas.height/2,canvas.height*3/4,canvas.height-2,canvas.height-1};
            for (const int x : xs) for (const int y : ys) {
                const POINT logical{LONG(x),LONG(y)};
                const POINT expected{view.x+LONG(std::int64_t(x)*view.width/canvas.width),
                    view.y+LONG(std::int64_t(y)*view.height/canvas.height)};
                assert(SamePoint(MapCursor(logical,canvas,output),expected));
                const auto client = MapCursor(logical,canvas,output,true);
                assert(client.x >= view.x && client.x < view.x+view.width);
                assert(client.y >= view.y && client.y < view.y+view.height);
                const auto mapped = MapMouse(client,canvas,output,true);
                assert(std::abs(mapped.x-x) <= toleranceX);
                assert(std::abs(mapped.y-y) <= toleranceY);
            }
            for (const int x : {0,canvas.width-1}) for (const int y : {0,canvas.height-1}) {
                const auto corner = MapCursor({LONG(x),LONG(y)},canvas,output,true);
                assert(corner.x > view.x && corner.x < view.x+view.width-1);
                assert(corner.y > view.y && corner.y < view.y+view.height-1);
                const auto edge = MapMouse(corner,canvas,output,true);
                assert(std::abs(edge.x-x) <= toleranceX && std::abs(edge.y-y) <= toleranceY);
            }
            assert(SamePoint(MapCursor({-100,-200},canvas,output,true),MapCursor({0,0},canvas,output,true)));
            assert(SamePoint(MapCursor({canvas.width+100,canvas.height+200},canvas,output,true),
                MapCursor({canvas.width-1,canvas.height-1},canvas,output,true)));
        }
    }
    struct InvalidGeometry { Size canvas, output; };
    const InvalidGeometry invalid[] = {{{0,480},{800,600}},{{640,0},{800,600}},
        {{-640,480},{800,600}},{{640,480},{0,600}},{{640,480},{800,-600}},
        {{640,480},{1,500}},{{480,640},{500,1}}};
    for (const auto geometry : invalid) for (const bool curved : {false,true}) {
        assert(SamePoint(MapMouse({10,20},geometry.canvas,geometry.output,curved),{}));
        assert(SamePoint(MapCursor({10,20},geometry.canvas,geometry.output,curved),{}));
    }
}

static void CheckPresentation(HWND window) {
    assert(Device::Ready());
    assert(Device::CanvasSize().width == 800 && Device::CanvasSize().height == 600);
    assert(Device::BeginFrame());
    Device::Clear(20,40,60);
    assert(Device::Finish(Filter::Nearest));
    std::vector<std::uint8_t> pixels;
    Size output{};
    assert(Device::ReadOutput(pixels,output));
    RECT client{};
    assert(GetClientRect(window,&client));
    assert(output.width == client.right && output.height == client.bottom);
    const auto center = (std::size_t(output.height/2)*output.width+output.width/2)*4;
    assert(pixels[center] == 20 && pixels[center+1] == 40 && pixels[center+2] == 60);
    assert(Device::Present());
}

int main() {
    CheckMouseGeometry();
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    // Exercise real Win32 placement without showing a window or taking focus.
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"CharonWindowTest";
    assert(RegisterClassW(&wc));
    HWND window = CreateWindowW(wc.lpszClassName,L"Charon window check",WS_OVERLAPPEDWINDOW,
        110,120,840,660,nullptr,nullptr,wc.hInstance,nullptr);
    assert(window);
    assert(!Window::Attach(nullptr));
    assert(Window::Attach(window));
    assert(Device::Create(window,{800,600}));
    RECT original{}, full{}, restored{};
    assert(GetWindowRect(window,&original));
    const LONG_PTR style = GetWindowLongPtr(window,GWL_STYLE);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    assert(GetMonitorInfo(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor));
    RECT clipBefore{}, clipAfter{};
    assert(GetClipCursor(&clipBefore));
    for (int i=0;i<3;++i) {
        assert(Window::SetFullscreen(true) && Window::Fullscreen());
        assert(GetWindowRect(window,&full) && Same(full,monitor.rcMonitor));
        assert(!(GetWindowLongPtr(window,GWL_STYLE) & WS_OVERLAPPEDWINDOW));
        assert(!IsWindowVisible(window));
        CheckPresentation(window);
        // Recreating the GPU on the same HWND must preserve windowed placement.
        assert(Window::Attach(window));
        Window::RepairCursorClip();
        assert(GetClipCursor(&clipAfter) && Same(clipBefore,clipAfter));
        assert(Window::SetFullscreen(false) && !Window::Fullscreen());
        assert(GetWindowRect(window,&restored) && Same(original,restored));
        assert(GetWindowLongPtr(window,GWL_STYLE) == style);
        CheckPresentation(window);
        assert(Window::SetFullscreen(false));
    }
    // Restore the latest windowed placement, including a user's move/resize.
    assert(SetWindowPos(window,nullptr,200,210,1000,750,SWP_NOZORDER | SWP_NOACTIVATE));
    assert(GetWindowRect(window,&original));
    assert(Window::SetFullscreen(true));
    assert(Window::SetFullscreen(false));
    assert(GetWindowRect(window,&restored) && Same(original,restored));
    Device::Destroy();
    Window::Detach(window);
    assert(!Window::SetFullscreen(true));
    assert(DestroyWindow(window));
    assert(UnregisterClassW(wc.lpszClassName,wc.hInstance));

    const RECT client{300,200,1900,1400};
    assert(Window::IsStaleCursorClip({300,200,1100,800},client));
    assert(Window::IsStaleCursorClip({300,200,940,680},client));
    // Alt+Enter can move the window; 640/800 transitions can leave the older lock.
    assert(Window::IsStaleCursorClip({400,200,1200,800},client));
    assert(Window::IsStaleCursorClip({300,200,940,680},{300,200,1100,800}));
    // A valid /lockmouse clip, desktop clip, or unrelated partial lock stays intact.
    assert(!Window::IsStaleCursorClip(client,client));
    assert(!Window::IsStaleCursorClip({0,0,2560,1440},client));
    assert(!Window::IsStaleCursorClip({300,200,700,500},client));
    assert(!Window::IsStaleCursorClip({300,200,1100,800},{300,200,1100,800}));
    assert(!Window::IsStaleCursorClip({},client));
    const RECT negativeClient{-1600,-100,0,1100};
    assert(Window::IsStaleCursorClip({-1600,-100,-800,500},negativeClient));
    std::puts("Window checks passed: fullscreen bounds, placement restoration, live GPU presentation and PlugY clip detection.");
}
