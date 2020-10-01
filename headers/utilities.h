#define _USE_MATH_DEFINES

#pragma once

#include "D2Structs.h"
#include <map>
#include <cmath>

class DPOINT {
public:
    double x, y;
};

extern DPOINT xvector, yvector;
void DrawRectangle(POINT a, POINT b, DWORD dwColor);
void DrawLine(POINT a, POINT b, DWORD dwColor);
POINT WorldToScreen(DPOINT pos);
POINT WorldToScreen(D2::Types::Path* path, DPOINT adjust = { 0, 0 });
POINT WorldToAutomap(DPOINT pos);
POINT WorldToAutomap(D2::Types::Path* path, DPOINT adjust = { 0, 0 });
void DrawDot(POINT pos, DWORD dwColor = 30);
void DrawAutomapX(DPOINT arg, DWORD dwColor = 30, double size = 5);
void DrawWorldX(DPOINT arg, DWORD dwColor = 30, double size = 1);
void DrawAutomapX(D2::Types::ItemPath* arg, DWORD dwColor = 30, double size = 5);
void DrawWorldX(D2::Types::ItemPath* arg, DWORD dwColor = 30, double size = 1);
void DrawAutomapX(D2::Types::Path* arg, DWORD dwColor = 30, double size = 5);
void DrawWorldX(D2::Types::Path* arg, DWORD dwColor = 30, double size = 1);
template <int len> void DrawAutomapShape(DPOINT points[len], DWORD dwColor = 30);
template <int len> void DrawWorldShape(DPOINT points[len], DWORD dwColor = 30);
void DrawAutomapRadialShape(DPOINT center, int radius, int sides, DWORD dwColor = 30, double angle = 0);
void DrawAutomapRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, DPOINT target);
void DrawWorldRadialShape(DPOINT center, int radius, int sides, DWORD dwColor = 30, double angle = 0);
void DrawWorldRadialShape(DPOINT center, int radius, int sides, DWORD dwColor, DPOINT target);
DWORD unitHP(D2::Types::UnitAny* unit);
bool isFriendly(D2::Types::UnitAny* unit);
bool isHostile(D2::Types::UnitAny* unit);
bool isAttackable(D2::Types::UnitAny* unit);
bool isEnemy(D2::Types::UnitAny* unit);
std::list<D2::Types::UnitAny*> getUnits(int type);
