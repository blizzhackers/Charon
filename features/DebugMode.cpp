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
            forUnits(1, [&](D2::Types::UnitAny* unit) -> void {
                if (unit->pPath && unitHP(unit) > 0) {
                    POINT pos = WorldToScreen(unit->pPath), target = WorldToScreen({ (double)unit->pPath->xTarget, (double)unit->pPath->yTarget });

                    if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                        DrawLine(pos, target, 0x99);
                    }
                }
            }, D2::ServerSideUnitHashTables);

            // Client side tracks missiles
            forUnits(3, [&](D2::Types::UnitAny* unit) -> void {
                DrawWorldX(unit->pPath, 0x99);
                POINT pos = WorldToScreen(unit->pPath), target = WorldToScreen({ (double)unit->pPath->xTarget, (double)unit->pPath->yTarget });

                if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                    DrawLine(pos, target, 0x83);
                }
            }, D2::ClientSideUnitHashTables);
        }
    }

    void gameUnitPostDraw() {
        if (Settings["debugMode"]) {
            wchar_t msg[512];
            DWORD fontNum = 12, width = 0, height = 0;
            POINT pos;
            D2::SetFont(fontNum);

            // Server side tracks enemies
            forUnits(1, [&](D2::Types::UnitAny* unit) -> void {
                if (unit->pPath) {
                    POINT pos = WorldToScreen(unit->pPath);

                    if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight) {
                        swprintf_s(msg, L"%s\n%s\n%s\ndwTxtFileNo: %d", (isAttackable(unit) ? L"Combat" : L"Non-Combat"), align[D2::GetUnitStat(unit, 172, 0)], D2::GetUnitName(unit), unit->dwTxtFileNo);
                        height = D2::GetTextSize(msg, &width, &fontNum);
                        D2::DrawGameText(msg, pos.x - (width >> 1), pos.y + height, 0, 1);
                    }
                }
            }, D2::ServerSideUnitHashTables);

            // Client side tracks missiles
            forUnits(3, [&](D2::Types::UnitAny* unit) -> void {
                pos = WorldToScreen(unit->pPath);
                swprintf_s(msg, L"dwOwnerType: %d", unit->dwOwnerType);
                height = D2::GetTextSize(msg, &width, &fontNum);
                D2::DrawGameText(msg, pos.x - (width >> 1), pos.y + height, 0, 1);
            }, D2::ClientSideUnitHashTables);
            
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
} featuer;
