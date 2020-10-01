/**
 * Just some common stuff.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <string>
#include <sstream>

extern HINSTANCE hInst;
extern HWND hwndSettings;
extern wchar_t saveDir[512];
extern wchar_t settingsPath[512];
extern std::wstring filterParams[6];

class GameOutput : public std::wostream {
    class GameOutputBuffer : public std::wstringbuf {
    public:
        virtual int sync();
    } buf;
public:
    GameOutput();
};

std::wstring COLOR(BYTE color);

const DWORD DEFAULT_FONT = 1;
const std::wstring version = L"Charon v1.3.0";
extern GameOutput gamelog;

namespace D2 {
    extern int ScreenWidth, ScreenHeight;
}

#pragma warning(disable : 4100)