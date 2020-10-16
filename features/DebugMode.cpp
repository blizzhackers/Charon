/**
 * Enables a bunch of debug enhancements.
 * It's crazy! Seriously!
 */

#include "headers/common.h"
#include "headers/feature.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <iostream>

const wchar_t* align[] = { L"Hostile", L"Neutral", L"Friendly" };
const DWORD alignColors[] = { 1, 5, 2 };

ASMPTR EnableDebugPrint = 0x8846DC;
REMOTEFUNC(void __fastcall, DrawFloor, (void* unknown), 0x4DED10);

void __fastcall _drawFloor(void* unknown) {
    if (!Settings["debugMode"]) {
        DrawFloor(unknown);
    }
}

// This is based on the actual source for printf... uses varargs.
int __stdcall printf_newline(const char* format, ...) {
    va_list arg;
    int done;

    va_start(arg, format);
    done = vfprintf(stdout, format, arg);
    va_end(arg);

    // We want to force a newline after debug prints :)
    puts("");

    return done;
}

// This feature class registers itself.
class : public Feature {
public:
    void toggleDebug() {
        Settings["debugMode"] = !Settings["debugMode"];
        SaveSettings();

        if (Settings["debugMode"]) {
            gamelog << COLOR(2) << "Debugging on." << std::endl;
        }
        else {
            gamelog << COLOR(1) << "Debugging off." << std::endl;
        }
    }

    void init() {
        MemoryPatch(0x476CDC) << CALL(_drawFloor); // Allow disabling the floor.
        MemoryPatch(0x51A480) << JUMP(printf_newline); // Enable even more console debug prints

        HotkeyCallbacks[VK_F10] = [&](LPARAM options) -> BOOL {
            toggleDebug();
            return FALSE;
        };
    }

    void gameUnitPreDraw() {
        if (Settings["debugMode"]) {
            D2::Types::Room1* current = D2::PlayerUnit->pPath->pRoom1;
            D2::Types::CollMap* coll = current->Coll;
            WORD* p = coll->pMapStart;
            DWORD color, x, y;

            for (x = 0; x < coll->dwSizeGameX; x++) {
                for (y = 0; y < coll->dwSizeGameY; y++) {
                    color = coll->getCollision(x, y, 0x4) ? 0x62 : coll->getCollision(x, y, 0xC09) ? 0x4B : coll->getCollision(x, y, 0x180) ? 0x8E : coll->getCollision(x, y, 0x10) ? 0x4 : 0x18;
                    DrawWorldX({ (double)coll->dwPosGameX + (double)x + 0.5, (double)coll->dwPosGameY + (double)y + 0.5 }, color, 0.5);
                }
            }

            for (unsigned int c = 0; c < current->dwRoomsNear; c++) {
                coll = current->pRoomsNear[c]->Coll;
                p = coll->pMapStart;
                for (x = 0; x < coll->dwSizeGameX; x++) {
                    for (y = 0; y < coll->dwSizeGameY; y++) {
                        color = coll->getCollision(x, y, 0x4) ? 0x62 : coll->getCollision(x, y, 0xC09) ? 0x4B : coll->getCollision(x, y, 0x180) ? 0x8E : coll->getCollision(x, y, 0x10) ? 0x4 : 0x18;
                        DrawWorldX({ (double)coll->dwPosGameX + (double)x + 0.5, (double)coll->dwPosGameY + (double)y + 0.5 }, color, 0.5);
                    }
                }
            }

            // Server side tracks enemies
            forUnits(D2::UnitType::MONSTER, [&](D2::Types::UnitAny* unit) -> void {
                if (unit->pPath && unitHP(unit) > 0) {
                    POINT pos = WorldToScreen(unit->pPath), target = WorldToScreen({ (double)unit->pPath->xTarget, (double)unit->pPath->yTarget });

                    if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                        DrawLine(pos, target, 0x99);
                    }
                }
            });

            // Client side tracks missiles
            forUnits(D2::UnitType::MISSILE, [&](D2::Types::UnitAny* unit) -> void {
                DrawWorldX(unit->pPath, 0x99, 0.5);
                POINT pos = WorldToScreen(unit->pPath), target = WorldToScreen({ (double)unit->pPath->xTarget, (double)unit->pPath->yTarget });

                if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                    DrawLine(pos, target, 0x83);
                }
            });
        }
    }

    void gameUnitPostDraw() {
        if (Settings["debugMode"]) {
            wchar_t msg[512];
            DWORD fontNum = 12, width = 0, height = 0;
            POINT pos;
            D2::SetFont(fontNum);

            forUnits(D2::UnitType::OBJECT, [&](D2::Types::UnitAny* unit) -> void {
                DPOINT dpos = getPosition(unit);
                DrawWorldX(dpos, 0x69);
                pos = WorldToScreen(dpos);
                swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                height = D2::GetTextSize(msg, &width, &fontNum);
                D2::DrawGameText(msg, pos.x - (width >> 1) - 4, pos.y - height + 8, 8, 1);
            });

            forUnits(D2::UnitType::ROOMTILE, [&](D2::Types::UnitAny* unit) -> void {
                DrawWorldX(getPosition(unit), 0x9B);
            });

            // Server side tracks enemies
            forUnits(D2::UnitType::MONSTER, [&](D2::Types::UnitAny* unit) -> void {
                if (unit->pPath) {
                    if (isAttackable(unit)) {
                        switch (D2::GetUnitStat(unit, 172, 0)) {
                        case 0: // hostile
                            if (unit->pMonsterData->fUnique || unit->pMonsterData->fChamp) {
                                DrawWorldX(unit->pPath, 0x0C);
                            }
                            else if (unit->pMonsterData->fMinion) {
                                DrawWorldX(unit->pPath, 0x0B);
                            }
                            else {
                                DrawWorldX(unit->pPath, 0x0A);
                            }
                            break;
                        case 2: // friendly
                            DrawWorldX(unit->pPath, 0x84);
                            break;
                        default: // neutral
                            DrawWorldX(unit->pPath, 0x99);
                        }
                    }
                    else {
                        DrawWorldX(unit->pPath, 0x1B);
                    }

                    POINT pos = WorldToScreen(unit->pPath);

                    if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight) {
                        swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                        height = D2::GetTextSize(msg, &width, &fontNum);
                        D2::DrawGameText(msg, pos.x - (width >> 1), pos.y + height + 2, alignColors[D2::GetUnitStat(unit, 172, 0)], 0);

                        if (unit->pMonsterData->fSuper) {
                            swprintf_s(msg, L"%d", unit->pMonsterData->wUniqueNo);
                            height = D2::GetTextSize(msg, &width, &fontNum);
                            D2::DrawGameText(msg, pos.x + 8, pos.y + 5, 3, 0);
                        }
                    }
                }
            });

            for (D2::Types::Room2* room = D2::PlayerUnit->pPath->pRoom1->pRoom2->pLevel->pRoom2First; room != NULL; room = room->pRoom2Next) {
                for (D2::Types::PresetUnit* unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
                    DPOINT dpos = { (double)room->dwPosX * 5 + (double)unit->dwPosX, (double)room->dwPosY * 5 + (double)unit->dwPosY };
                    DrawWorldX(dpos, 0x84);
                    POINT wpos = WorldToScreen(dpos);
                    swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                    height = D2::GetTextSize(msg, &width, &fontNum);
                    D2::DrawGameText(msg, wpos.x - (width >> 1) - 4, wpos.y + height + 2, 2, 1);
                }
            }
        }
    }
} feature;
