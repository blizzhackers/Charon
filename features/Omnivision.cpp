/**
* @description patch the Line Of Sight, so we can see monsters on the other side of a wall
* @Author Jaenster
*/

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"

namespace Omnivision {
	
    __declspec(naked) void __fastcall GetRoomColors_relocated(D2::Types::Room1* pRoom, BYTE* gamma, BYTE* red, BYTE* green, BYTE* blue) {
        static ASMPTR GetRoomColors_Rejoin = 0x66bfd6;
        __asm {
            push ebp
            mov ebp, esp
            mov eax, dword ptr [ecx + 0x58]
            jmp GetRoomColors_Rejoin
        }
    }

    void __fastcall GetRoomColors_override(D2::Types::Room1* pRoom, BYTE* gamma, BYTE* red, BYTE* green, BYTE* blue) {
        if (Settings["debugMode"] && Settings["debugModeType"] == DebugMode::DARK) {
            *gamma = *red = *green = *blue = 1;
            return;
        }
        else if (Settings["omnivision"] || Settings["debugMode"]) {
            *gamma = *red = *green = *blue = 0xFF;
            return;
        }

        GetRoomColors_relocated(pRoom, gamma, red, green, blue);
    }

    DWORD __fastcall playerCanSee_override(DWORD value) {
        if (Settings["omnivision"] || Settings["debugMode"]) {
            return 1;
        }

        return value;
    }

    __declspec(naked) void playerCanSee_hook() {
        static ASMPTR DRAW_WORLD_IsUnitInLineOfSight = 0x4dc710;
        __asm {
            call DRAW_WORLD_IsUnitInLineOfSight
            mov ecx, eax
            call playerCanSee_override
            ret
        }
    }

    BOOL useLightRadius() {
        return !Settings["debugMode"] || Settings["debugModeType"] != DebugMode::DARK;
    }

    __declspec(naked) void lightUnitsOverride() {
        static ASMPTR LIGHTMAP_GetQualityLevel = 0x475780, lightUnitsOverride_rejoin = 0x475892, lightUnitsOverride_skip = 0x4758c6;
        __asm {
            pushad
            call useLightRadius
            test eax, eax
            popad
            jnz allow
            jmp lightUnitsOverride_skip

            allow:
            call LIGHTMAP_GetQualityLevel
            jmp lightUnitsOverride_rejoin
        }
    }

    class : public Feature {
    public:
        void init() {
            //gamelog << COLOR(4) << "Installing debug omnivision..." << std::endl;
            MemoryPatch(0x66bfd0) << JUMP(GetRoomColors_override);
            MemoryPatch(0x4dc864) << CALL(playerCanSee_hook);
            MemoryPatch(0x47588d) << JUMP(lightUnitsOverride);
        }

    } feature;

}
