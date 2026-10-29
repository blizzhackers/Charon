/**
 * Enables a color swatch command: /swatch
 */

#include "headers/feature.h"
#include "headers/hook.h"
#include "headers/common.h"
#include "headers/remote.h"
#include <iostream>

wchar_t wHex[] = L"0123456789ABCDEF";

// This feature class registers itself.
class : public Feature {
public:
    void allPostDraw() {
        DWORD fontnum = 8;
        int height, width;
        D2::SetAndReturnLastFont(fontnum);
        if (State["drawSwatch"]) {
            wchar_t msg[3] = { 0 };
            int color, gridsize = 24;
            for (int x = 0; x < 32; x++) {
                for (int y = 0; y < 8; y++) {
                    color = (x << 3) | y;
                    DrawRectangle({ x * gridsize, y * gridsize }, { x * gridsize + gridsize, y * gridsize + gridsize }, color);
                    msg[0] = wHex[color >> 4];
                    msg[1] = wHex[color & 15];
                    D2::TEXT_CalcTextDimensions(msg, &width, &height);
                    D2::DRAW_text(msg, x * gridsize + (gridsize - width) / 2, (y + 1) * gridsize, 0, 0);
                }
            }
        }
    }
} feature;
