#define _USE_MATH_DEFINES

#include "../headers/D2Structs.h"
#include "../headers/common.h"
#include "../headers/feature.h"
#include "../headers/utilities.h"
#include "../headers/remote.h"
#include <iostream>
#include <cmath>


REMOTEFUNC(void __stdcall, D2Drawline, (int X1, int Y1, int X2, int Y2, DWORD dwColor, DWORD dwAlpha), 0x4F6380)
REMOTEFUNC(void __stdcall, D2DrawRectangle, (int X1, int Y1, int X2, int Y2, DWORD dwColor, DWORD dwAlpha), 0x4F6340)
REMOTEFUNC(long __fastcall, GetMouseXOffset, (), 0x45AFC0)
REMOTEFUNC(long __fastcall, GetMouseYOffset, (), 0x45AFB0)
REMOTEREF(POINT, AutomapOffset, 0x7A5198)
REMOTEREF(int, Divisor, 0x711254)

namespace D2 {
    int ScreenWidth = 0, ScreenHeight = 0;

    namespace Defs {
        typedef POINT p_t;
    }
}

DPOINT xvector = { 16.0, 8.0 }, yvector = { -16.0, 8.0 };

void DrawRectangle(POINT a, POINT b, DWORD dwColor) {
    if (
        a.x >= 0 && a.x < D2::ScreenWidth ||
        b.x >= 0 && b.x < D2::ScreenWidth ||
        a.y >= 0 && a.y < D2::ScreenHeight ||
        b.y >= 0 && b.y < D2::ScreenHeight
        ) {
        D2DrawRectangle(a.x, a.y, b.x, b.y, dwColor, 0xFF);
    }
}

void DrawLine(POINT a, POINT b, DWORD dwColor) {
    if (
        a.x >= 0 && a.x < D2::ScreenWidth ||
        b.x >= 0 && b.x < D2::ScreenWidth ||
        a.y >= 0 && a.y < D2::ScreenHeight ||
        b.y >= 0 && b.y < D2::ScreenHeight
    ) {
        D2Drawline(a.x, a.y, b.x, b.y, dwColor, 0xFF);
    }
}

POINT WorldToScreen(DPOINT pos) {
    POINT ret{
        (long)(pos.x * xvector.x + pos.y * yvector.x) - GetMouseXOffset(),
        (long)(pos.x * xvector.y + pos.y * yvector.y) - GetMouseYOffset()
    };
    return ret;
}

POINT WorldToScreen(D2::Types::Path *path, DPOINT adjust) {
    return WorldToScreen({ (double)path->xPos + adjust.x + (double)path->xOffset / 65536.0, (double)path->yPos + adjust.y + (double)path->yOffset / 65536.0 });
}

POINT WorldToAutomap(DPOINT pos) {
    POINT ret{
        (long)((pos.x * xvector.x + pos.y * yvector.x) / (double)Divisor) - AutomapOffset.x + 8,
        (long)((pos.x * xvector.y + pos.y * yvector.y) / (double)Divisor) - AutomapOffset.y - 8
    };
    return ret;
}

POINT WorldToAutomap(D2::Types::Path *path, DPOINT adjust) {
    return WorldToAutomap({ (double)path->xPos + adjust.x + (double)path->xOffset / 65536.0, (double)path->yPos + adjust.y + (double)path->yOffset / 65536.0 });
}

void DrawDot(POINT pos, DWORD dwColor) {
    DrawLine({ pos.x - 1, pos.y }, { pos.x + 1, pos.y }, dwColor);
    DrawLine({ pos.x, pos.y - 1 }, { pos.x, pos.y + 1 }, dwColor);
}

void DrawAutomapX(DPOINT arg, DWORD dwColor, double size) {
    POINT a = WorldToAutomap({ (double)arg.x - size, (double)arg.y }), b = WorldToAutomap({ (double)arg.x + size, (double)arg.y });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToAutomap({ (double)arg.x, (double)arg.y - size }), b = WorldToAutomap({ (double)arg.x, (double)arg.y + size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

void DrawWorldX(DPOINT arg, DWORD dwColor, double size) {
    POINT a = WorldToScreen({ (double)arg.x - size, (double)arg.y }), b = WorldToScreen({ (double)arg.x + size, (double)arg.y });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToScreen({ (double)arg.x, (double)arg.y - size }), b = WorldToScreen({ (double)arg.x, (double)arg.y + size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

void DrawAutomapX(D2::Types::ItemPath* arg, DWORD dwColor, double size) {
    POINT a = WorldToAutomap({ (double)arg->dwPosX - size, (double)arg->dwPosY }), b = WorldToAutomap({ (double)arg->dwPosX + size, (double)arg->dwPosY });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToAutomap({ (double)arg->dwPosX, (double)arg->dwPosY - size }), b = WorldToAutomap({ (double)arg->dwPosX, (double)arg->dwPosY + size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

void DrawWorldX(D2::Types::ItemPath* arg, DWORD dwColor, double size) {
    POINT a = WorldToScreen({ (double)arg->dwPosX - size, (double)arg->dwPosY }), b = WorldToScreen({ (double)arg->dwPosX + size, (double)arg->dwPosY });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToScreen({ (double)arg->dwPosX, (double)arg->dwPosY - size }), b = WorldToScreen({ (double)arg->dwPosX, (double)arg->dwPosY + size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

void DrawAutomapX(D2::Types::Path *arg, DWORD dwColor, double size) {
    POINT a = WorldToAutomap(arg, { -size, 0 }), b = WorldToAutomap(arg, { size, 0 });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToAutomap(arg, { 0, -size }), b = WorldToAutomap(arg, { 0, size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

void DrawWorldX(D2::Types::Path* arg, DWORD dwColor, double size) {
    POINT a = WorldToScreen(arg, { -size, 0 }), b = WorldToScreen(arg, { size, 0 });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
    a = WorldToScreen(arg, { 0, -size }), b = WorldToScreen(arg, { 0, size });
    DrawLine({ a.x, a.y }, { b.x, b.y }, dwColor);
}

template <int len>
void DrawAutomapShape(DPOINT points[len], DWORD dwColor) {
    POINT pos[2];

    pos[0] = WorldToAutomap(points[len - 1]);
    pos[1] = WorldToAutomap(points[0]);
    DrawLine(pos[0], pos[1], dwColor);

    for (int c = 1; c < len; c++) {
        pos[0] = pos[1];
        pos[1] = WorldToAutomap(points[c]);
        DrawLine(pos[0], pos[1], dwColor);
    }
}

template <int len>
void DrawWorldShape(DPOINT points[len], DWORD dwColor) {
    POINT pos[2];

    pos[0] = WorldToScreen(points[len - 1]);
    pos[1] = WorldToScreen(points[0]);
    DrawLine(pos[0], pos[1], dwColor);

    for (int c = 1; c < len; c++) {
        pos[0] = pos[1];
        pos[1] = WorldToScreen(points[c]);
        DrawLine(pos[0], pos[1], dwColor);
    }
}

void DrawAutomapRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, double angle) {
    POINT pos[2];
    double interval = M_PI * 2 / sides, i = angle;

    pos[0] = WorldToAutomap({ center.x + cos(i) * radius, center.y + sin(i) * radius });
    i += interval;
    pos[1] = WorldToAutomap({ center.x + cos(i) * radius, center.y + sin(i) * radius });
    i += interval;
    DrawLine(pos[0], pos[1], dwColor);

    for (int c = 1; c < sides; c++, i += interval) {
        pos[0] = pos[1];
        pos[1] = WorldToAutomap({ center.x + cos(i) * radius, center.y + sin(i) * radius });
        DrawLine(pos[0], pos[1], dwColor);
    }
}

void DrawAutomapRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, DPOINT target) {
    DrawAutomapRadialShape(center, radius, sides, dwColor, atan2(target.y - center.y, target.x - center.x));
}

void DrawWorldRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, double angle) {
    POINT pos[2];
    double interval = M_PI * 2 / sides, i = angle;

    pos[0] = WorldToScreen({ center.x + cos(i) * radius, center.y + sin(i) * radius });
    i += interval;
    pos[1] = WorldToScreen({ center.x + cos(i) * radius, center.y + sin(i) * radius });
    i += interval;
    DrawLine(pos[0], pos[1], dwColor);

    for (int c = 1; c < sides; c++, i += interval) {
        pos[0] = pos[1];
        pos[1] = WorldToScreen({ center.x + cos(i) * radius, center.y + sin(i) * radius });
        DrawLine(pos[0], pos[1], dwColor);
    }
}

void DrawWorldRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, DPOINT target) {
    DrawWorldRadialShape(center, radius, sides, dwColor, atan2(target.y - center.y, target.x - center.x));
}

DWORD unitHP(D2::Types::UnitAny* unit) {
    return D2::GetUnitStat(unit, 6, 0) >> 8;
}

bool isFriendly(D2::Types::UnitAny* unit) {
    return D2::GetUnitStat(unit, 172, 0) == 2;
}

bool isHostile(D2::Types::UnitAny* unit) {
    return D2::GetUnitStat(unit, 172, 0) == 0;
}

bool isAttackable(D2::Types::UnitAny* unit) {
    return unit->dwFlags & 0x4;
}

bool isEnemy(D2::Types::UnitAny* unit) {
    return unitHP(unit) > 0 && isHostile(unit) && isAttackable(unit);
}

std::vector<D2::Types::UnitAny*> getUnits(int type, D2::Types::UnitHashTable* lookupTables) {
    if (type < 0 || type > 5) {
        throw "Type needs to be between 0 and 5";
    }
    D2::Types::UnitAny* unit;

    std::vector<D2::Types::UnitAny*> vector;
    for (int c = 0; c < 128; c++) {
        for (unit = lookupTables[type].table[c]; unit; unit = unit->pListNext) {
            vector.push_back(unit);
        }
    }

    return vector;
}
std::vector<D2::Types::UnitAny*> getUnits(int type) {
    D2::Types::UnitHashTable* lookupTables = D2::ServerSideUnitHashTables;
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 5:
        break;
    case 3:
        lookupTables = D2::ClientSideUnitHashTables;
        break;
    default:
        throw "Type needs to be between 0 and 5";
    }
    return getUnits(type, lookupTables);
}

namespace D2 {
    namespace Types {
        WORD CollMap::getCollision(DWORD localx, DWORD localy, WORD mask) {
            return pMapStart[localx + localy * dwSizeGameX] & mask;
        }

        WORD Room1::getCollision(DWORD localx, DWORD localy, WORD mask) {
            return Coll->pMapStart[localx + localy * Coll->dwSizeGameX] & mask;
        }

        WORD Room2::getCollision(DWORD localx, DWORD localy, WORD mask) {
            return pRoom1->Coll->pMapStart[localx + localy * pRoom1->Coll->dwSizeGameX] & mask;
        }

        DWORD Room2::getWorldX() {
            return pRoom1->dwXStart;
        }

        DWORD Room2::getWorldY() {
            return pRoom1->dwYStart;
        }

        DWORD Room2::getWorldWidth() {
            return pRoom1->dwXSize;
        }

        DWORD Room2::getWorldHeight() {
            return pRoom1->dwYSize;
        }
    }
}
