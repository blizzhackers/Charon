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

ASMPTR DEBUGPRINT = 0x8846DC;
REMOTEFUNC(void __fastcall, dFloorDraw, (void* unknown), 0x4DED10);

void __fastcall _drawFloor(void* unknown) {
    if (!Settings["debugMode"]) {
        dFloorDraw(unknown);
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

REMOTEFUNC(void __fastcall, DRAW_WORLD_UnderFloor, (void* param1), 0x4df480)
void __fastcall DRAW_UnderFloor_Intercept(void* param1) {

    // ToDo; only visiable background behind floor is in baals chamber, as it has some floating floors
    // It draws some weird white backgrounds in act 5, that are not visable as the floor draws over it
    // Should only draw when in chamber, and disable for the rest of the levels. Waste of resource

    if (!Settings["debugMode"]) {
        DRAW_WORLD_UnderFloor(param1);
    }
}

BOOL ShouldDrawAncientsBackground() {
    return !Settings["debugMode"];
}

REMOTEFUNC(void, DRAW_Background_Ancients, (), 0x476460);
__declspec(naked) void DRAW_Background_Ancients_Intercept() {
    __asm {
        pushad
        call ShouldDrawAncientsBackground
        test eax, eax
        popad

        jz skip
        jmp DRAW_Background_Ancients

        skip:
        ret
    }
}

BOOL CanDraw() {
    return !Settings["debugMode"] || Settings["debugModeType"] != DebugMode::HIDDEN;
}

BOOL CanDrawWalls() {
    return !Settings["debugMode"] || Settings["debugModeWalls"];
}

BOOL CanDrawShadows() {
    return !Settings["debugMode"];
}

BOOL CanDrawRoofs() {
    return !Settings["disableRoofs"] && CanDrawWalls();
}

__declspec(naked) void DrawUnit_intercept() {
    static const ASMPTR DrawUnit_rejoin = 0x471ec6;
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x70

        pushad
        call CanDraw
        test eax, eax
        popad
        jz skip

        jmp DrawUnit_rejoin

        skip:
        mov esp, ebp
        pop ebp
        ret 0x10
    }
}

_declspec(naked) void DrawWalls_intercept() {
    static const ASMPTR DrawWalls_rejoin = 0x4df3aa, DrawWalls_jump = 0x4df3c3, DrawWalls_skip = 0x4df3d6;
    __asm {
        pushad
        call CanDrawWalls
        test eax, eax
        popad
        jz skip

        cmp bl, 0xff
        jz jump
        jmp DrawWalls_rejoin

        jump:
        jmp DrawWalls_jump

        skip:
        jmp DrawWalls_skip
    }
}

__declspec(naked) void DrawShadows_intercept() {
    static const ASMPTR dShadowPass_original = 0x4df510;
    __asm {
        pushad
        call CanDrawShadows
        test eax, eax
        popad
        jz skip

        jmp dShadowPass_original

        skip:
        ret
    }
}

__declspec(naked) void DrawRoofs_intercept() {
    static const ASMPTR dRoofDraw_original = 0x4dea70;
    __asm {
        pushad
        call CanDrawRoofs
        test eax, eax
        popad
        jz skip

        jmp dRoofDraw_original

        skip :
        ret
    }
}

namespace DebugMode {

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
            MemoryPatch(0x471ec0) << JUMP(DrawUnit_intercept);
            MemoryPatch(0x4df3a5) << JUMP(DrawWalls_intercept);
            MemoryPatch(0x476cf3) << CALL(DrawShadows_intercept);
            MemoryPatch(0x476d0b) << CALL(DrawRoofs_intercept);

            // Redirect the drawing of the background behind the floor
            MemoryPatch(0x476cd5) << CALL(DRAW_UnderFloor_Intercept);

            // Intercept drawing ancient's background
            MemoryPatch(0x476c93) << CALL(DRAW_Background_Ancients_Intercept);

            HotkeyCallbacks[VK_F10] = [&](LPARAM options) -> BOOL {
                toggleDebug();
                return FALSE;
            };
        }

        void gameUnitPreDraw() {
            if (Settings["debugMode"]) {
                D2::Types::Room1* current = D2::pPlayerUnit->getRoom1();
                D2::Types::CollMap* coll = current->Coll;
                WORD* p = coll->pMapStart;
                DWORD color, x, y;

                for (x = 0; x < coll->dwSizeGameX; x++) {
                    for (y = 0; y < coll->dwSizeGameY; y++) {
                        color = coll->getCollision(x, y, 0x4) ? 0x62 : coll->getCollision(x, y, 0xC09) ? 0x4B : coll->getCollision(x, y, 0x180) ? 0x8E : coll->getCollision(x, y, 0x10) ? 0x4 : 0x18;
                        DPOINT( (double)coll->dwPosGameX + (double)x + 0.5, (double)coll->dwPosGameY + (double)y + 0.5 ).DrawWorldX(color, 0.5);
                    }
                }

                for (unsigned int c = 0; c < current->dwRoomsNear; c++) {
                    coll = current->pRoomsNear[c]->Coll;
                    p = coll->pMapStart;
                    for (x = 0; x < coll->dwSizeGameX; x++) {
                        for (y = 0; y < coll->dwSizeGameY; y++) {
                            color = coll->getCollision(x, y, 0x4) ? 0x62 : coll->getCollision(x, y, 0xC09) ? 0x4B : coll->getCollision(x, y, 0x180) ? 0x8E : coll->getCollision(x, y, 0x10) ? 0x4 : 0x18;
                            DPOINT( (double)coll->dwPosGameX + (double)x + 0.5, (double)coll->dwPosGameY + (double)y + 0.5 ).DrawWorldX(color, 0.5);
                        }
                    }
                }

                // Server side tracks enemies
                for (D2::Types::NonPlayerUnit* unit : D2::ServerSideUnitHashTables.nonplayers.all()) {
                    if (unit->pPath && unit->unitHP() > 0) {
                        POINT pos = unit->pos().toScreen(), target = unit->getTargetPos().toScreen();

                        if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                            DrawLine(pos, target, 0x99);
                        }
                    }
                }

                // Client side tracks missiles
                for (D2::Types::MissileUnit* unit : D2::ClientSideUnitHashTables.missiles.all()) {
                    unit->DrawWorldX(0x99, 0.5);
                    POINT pos = unit->pos().toScreen(), target = unit->getTargetPos().toScreen();

                    if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight && target.x >= 0 && target.y >= 0 && target.x < D2::ScreenWidth && target.y < D2::ScreenHeight) {
                        DrawLine(pos, target, 0x83);
                    }
                }
            }
        }

        void gameUnitPostDraw() {
            if (Settings["debugMode"]) {
                wchar_t msg[512];
                DWORD fontNum = 12, width = 0, height = 0;
                POINT pos;
                D2::SetAndReturnLastFont(fontNum);

                for (D2::Types::PlayerUnit* unit : D2::ServerSideUnitHashTables.players.all()) {
                    unit->DrawWorldX(0x9B);
                }

                // Server side tracks objects
                for (D2::Types::ObjectUnit* unit : D2::ServerSideUnitHashTables.objects.all()) {
                    DPOINT dpos = unit->pos();
                    dpos.DrawWorldX(0x69);
                    pos = dpos.toScreen();
                    swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                    height = D2::TEXT_CalcTextDimensions(msg, &width, &fontNum);
                    D2::DRAW_text(msg, pos.x - (width >> 1) - 4, pos.y - height + 8, 8, 1);
                }

                // Server side tracks enemies
                for (D2::Types::NonPlayerUnit* unit : D2::ServerSideUnitHashTables.nonplayers.all()) {
                    if (unit->pPath) {
                        if (unit->isAttackable()) {
                            switch (D2::STATSEX_GetCurrentStat(unit, 172, 0)) {
                            case 0: // hostile
                                if (unit->pMonsterData->fUnique || unit->pMonsterData->fChamp) {
                                    unit->DrawWorldX(0x0C);
                                }
                                else if (unit->pMonsterData->fMinion) {
                                    unit->DrawWorldX(0x0B);
                                }
                                else {
                                    unit->DrawWorldX(0x0A);
                                }
                                break;
                            case 2: // friendly
                                unit->DrawWorldX(0x84);
                                break;
                            default: // neutral
                                unit->DrawWorldX(0x99);
                            }
                        }
                        else {
                            unit->DrawWorldX(0x1B);
                        }

                        pos = unit->pos().toScreen();

                        if (pos.x >= 0 && pos.y >= 0 && pos.x < D2::ScreenWidth && pos.y < D2::ScreenHeight) {
                            swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                            height = D2::TEXT_CalcTextDimensions(msg, &width, &fontNum);
                            D2::DRAW_text(msg, pos.x - (width >> 1), pos.y + height + 2, alignColors[D2::STATSEX_GetCurrentStat(unit, 172, 0)], 0);

                            if (unit->pMonsterData->fSuper) {
                                swprintf_s(msg, L"%d", unit->pMonsterData->wUniqueNo);
                                height = D2::TEXT_CalcTextDimensions(msg, &width, &fontNum);
                                D2::DRAW_text(msg, pos.x + 8, pos.y + 5, 3, 0);
                            }
                        }
                    }
                }

                if (D2::pPlayerUnit->getLevel() != nullptr) {
                    for (D2::Types::Room2* room : D2::pPlayerUnit->getLevel()->getAllRoom2()) {
                        for (D2::Types::PresetUnit* unit : room->getAllPresetUnits()) {
                            DPOINT dpos = unit->pos(room);
                            dpos.DrawWorldX(0x84);
                            POINT wpos = dpos.toScreen();
                            swprintf_s(msg, L"%d", unit->dwTxtFileNo);
                            height = D2::TEXT_CalcTextDimensions(msg, &width, &fontNum);
                            D2::DRAW_text(msg, wpos.x - (width >> 1) - 4, wpos.y + height + 2, 2, 1);
                        }
                    }
                }
            }
        }
    } feature;

}
