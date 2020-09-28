#ifdef _DEBUG

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <unordered_map>
#include <random>

#pragma pack(push)
#pragma pack(1)

struct IncompleteGameData {
    BYTE unk[4];                // 0
    DWORD seed;                 // 4
    BYTE unk2[32];              // 8
    WORD nServerToken;          // 40
    char szGameName[16];        // 42
    char szGamePassword[16];    // 58
    char szGameDescription[32]; // 74
    BYTE nGameType;             // 106
    BYTE unk3[2];               // 107
    BYTE nDifficulty;           // 109
    BYTE unk4[2];               // 110
    BOOL bExpansion;            // 112
    DWORD dwGameType;           // 116
    BYTE unk5[7370];            // 120
    BYTE nBossFlagList[64];     // 7472
    BYTE unk6[100];             // 7536
    IncompleteGameData* pPrev;  // 7636
    IncompleteGameData* pNext;  // 7640
    BYTE unk7[12];              // 7644
}; // Total Size: 7656

#pragma pack(pop)

REMOTEFUNC(BOOL __fastcall, CowsCubeOutputOriginal, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit), 0x594140);
REMOTEFUNC(void __stdcall, UnitLocation, (D2::Types::UnitAny* pUnit, POINT* pPoint), 0x620870);
REMOTEFUNC(void __fastcall, FindSpawnablePosition, (D2::Types::Room1* pDrlgRoom, POINT* pos, DWORD param_1_00, DWORD param_2_00, D2::Types::Room1** pRoomsNear, DWORD param_6, int param_7), 0x545340);
REMOTEFUNC(BOOL __stdcall, SetsModeWithFlagsC4Update, (D2::Types::UnitAny* pUnit, DWORD dwMode), 0x624690);
REMOTEFUNC(DWORD __fastcall, LinkPortal, (void* ptGame, int unused, D2::Types::UnitAny* ptObject, DWORD levelEndID, DWORD levelStartID), 0x56CF40);
REMOTEFUNC(void __stdcall, SetObjectFlags, (D2::Types::UnitAny* ptObject, BYTE flags), 0x622300);
REMOTEFUNC(BYTE __stdcall, GetObjectFlags, (D2::Types::UnitAny* ptObject), 0x6222C0);
REMOTEFUNC(void __stdcall, RoomPortalFlag, (D2::Types::Room1* param_1, DWORD flag), 0x61AED0);
REMOTEFUNC(D2::Types::UnitAny* __fastcall, SpawnUniquePack, (IncompleteGameData* pGame, D2::Types::Room1* pRoom, BOOL zero, DWORD dwClassId, void* subtiles, int x, int y, BOOL flag), 0x5A43E0);
REMOTEFUNC(D2::Types::UnitAny* __fastcall, SpawnMonster, (IncompleteGameData* pGame, D2::Types::Room1* pRoom, int x, int y, DWORD dwClassId, DWORD guid, DWORD seed, BOOL isChampion, BOOL isSuperUnique, DWORD superUniqueId, const char (&mods)[9]), 0x5A4440);
REMOTEFUNC(DWORD __fastcall, SpawnPortal, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::Room1* pDrlgRoom, int nX, int nY, DWORD eD2LevelId, D2::Types::UnitAny** param_7, int nClassId, DWORD param_9), 0x56D130);
REMOTEFUNC(void __fastcall, OpenPortal, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, DWORD LevelId), 0x5A9930);
REMOTEFUNC(DWORD __stdcall, GetAct, (int levelId), 0x6427f0);

ASMPTR CowsCubeOutputHook = 0x565a80;
ASMPTR CubeKeysHook = 0x565a90;
ASMPTR CubeOrgansHook = 0x565aa0;
ASMPTR UberBaalAIPointer = 0x73D330, UberBaalAI = 0x5FD200, BaalAI = 0x5FCFE0;
ASMPTR UberMephAIPointer = 0x73D340, UberMephAI = 0x5F81C0, MephAI = 0x5E9170;
ASMPTR UberDiabloAIPointer = 0x73D350, UberDiabloAI = 0x05E9DF0, DiabloAI = 0x5F78B0;

DWORD guid = 0x4FFFFFFF;
int seed = 0;

union GameFlagsType {
    DWORD flags = 0;
    struct {
        bool sandsPortal : 1;
        bool durielSpawned : 1;
        bool denPortal : 1;
        bool lillithSpawned : 1;
        bool furnacePortal : 1;
        bool izualSpawned : 1;
        bool tristramPortal : 1;
        bool uberDiabloSpawned : 1;
        bool uberBaalSpawned : 1;
        bool uberMephSpawned : 1;
        bool uberDiabloKilled : 1;
        bool uberBaalKilled : 1;
        bool uberMephKilled : 1;
        bool torchDropped : 1;
        bool dCloneSpawned : 1;
    };
};

std::unordered_map<DWORD, GameFlagsType> gameFlags;

DWORD GetActFromRoom(D2::Types::Room1* a) {
    return GetAct(a->pRoom2->pLevel->dwLevelNo);
}

D2::Types::UnitAny* PortalTo(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, int levelId, bool isBlue = false) {
    D2::Types::UnitAny* pPortal;
    D2::Types::Room1* target;
    POINT pos;

    if (GetActFromRoom(pUnit->pPath->pRoom1) == GetAct(levelId)) {
        UnitLocation(pUnit, &pos);
        FindSpawnablePosition(pUnit->pPath->pRoom1, &pos, 3, 0xC09, &target, 4, 100);
        SpawnPortal(pGame, nullptr, target, pos.x, pos.y, levelId, &pPortal, isBlue ? 0x3b : 0x3c, 0);
        if (pPortal) pPortal->pObjectData->Type = levelId;
        return pPortal;
    }

    return nullptr;
}

std::random_device rd; // obtain a random number from hardware
std::mt19937 gen(rd()); // seed the generator

template <class T>
T randomElement(std::vector<T> v) {
    std::uniform_int_distribution<> distr(0, v.size() - 1); // define the range
    return v[distr(gen)];
}

BOOL __fastcall CubeKeys_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (pGame->nDifficulty != 2) return 0;
    std::vector<int> p;

    if (!gameFlags[pGame->seed].denPortal) {
        p.push_back(133);
    }
    if (!gameFlags[pGame->seed].sandsPortal) {
        p.push_back(134);
    }
    if (!gameFlags[pGame->seed].furnacePortal) {
        p.push_back(135);
    }

    if (p.size() > 0) {
        int levelTarget = randomElement(p);
        if (PortalTo(pGame, pUnit, levelTarget)) {
            switch (levelTarget) {
            case 133:
                gameFlags[pGame->seed].denPortal = true;
                return 1;
            case 134:
                gameFlags[pGame->seed].sandsPortal = true;
                return 1;
            case 135:
                gameFlags[pGame->seed].furnacePortal = true;
                return 1;
            }
        }
    }

    return 0;
}

BOOL __fastcall CubeOrgans_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (pGame->nDifficulty != 2 && !gameFlags[pGame->seed].tristramPortal) return 0;

    if (PortalTo(pGame, pUnit, 136)) {
        gameFlags[pGame->seed].tristramPortal = true;
    }

    return 0;
}

BOOL __fastcall CowsCubeOutput_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    CubeKeys_Intercept(pGame, pUnit); return 0;
    return CowsCubeOutputOriginal(pGame, pUnit);
}

ASMPTR RoomInit_Original = 0x542b40, RoomInit_Rejoin = 0x542b46;

__declspec(naked) void __fastcall RoomInit_Relocated(IncompleteGameData* pGame, D2::Types::Room1* pRoom1) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x18
        jmp RoomInit_Rejoin
    }
}

void __fastcall RoomInit_Hook(IncompleteGameData* pGame, D2::Types::Room1* pRoom1) {
    RoomInit_Relocated(pGame, pRoom1);
    D2::Types::Level* level = pRoom1->pRoom2->pLevel;
    D2::Types::Room2* room = pRoom1->pRoom2;
    D2::Types::PresetUnit* unit;


    switch (level->dwLevelNo) {
    case 133: // Den
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].lillithSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 707, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].lillithSpawned = true;
                }
            }
        }
        break;
    case 134: // Sands
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            if (unit->dwTxtFileNo == 402 && !gameFlags[pGame->seed].durielSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 708, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].durielSpawned = true;
                }
            }
        }
        break;
    case 135: // Furnace
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].izualSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 706, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].izualSpawned = true;
                }
            }
        }
        break;
    case 136: // Uber Tristram
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            switch (unit->dwTxtFileNo) {
            case 26: // Cain cage original, uber spawns
                char mods[9] = { 1 };
                if (!gameFlags[pGame->seed].uberMephSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 704, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].uberMephSpawned = true;
                    }
                }
                if (!gameFlags[pGame->seed].uberBaalSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 709, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].uberBaalSpawned = true;
                    }
                }
                if (!gameFlags[pGame->seed].uberDiabloSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 705, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].uberDiabloSpawned = true;
                    }
                }
                break;
            }
        }
        break;
    }
}

ASMPTR KillMonster_Original = 0x57CCB0, KillMonster_Rejoin = 0x57CCB6;

__declspec(naked) void __fastcall KillMonster_Relocated(IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, D2::Types::UnitAny* pAttacker, BOOL bRemoveFromOwner) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x20
        jmp KillMonster_Rejoin
    }
}

void __fastcall KillMonster_Hook(IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, D2::Types::UnitAny* pAttacker, BOOL bRemoveFromOwner) {
    switch (pVictim->dwTxtFileNo) {
    case 704:
        gameFlags[pGame->seed].uberMephKilled = true;
        break;
    case 705:
        gameFlags[pGame->seed].uberDiabloKilled = true;
        break;
    case 709:
        gameFlags[pGame->seed].uberBaalKilled = true;
        break;
    }

    if (gameFlags[pGame->seed].uberMephKilled && gameFlags[pGame->seed].uberDiabloKilled && gameFlags[pGame->seed].uberBaalKilled && !gameFlags[pGame->seed].torchDropped) {
        gamelog << "Torch drop!" << std::endl;
        gameFlags[pGame->seed].torchDropped = true;
    }

    KillMonster_Relocated(pGame, pVictim, pAttacker, bRemoveFromOwner);
}

namespace Ubers {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(CowsCubeOutputHook) << JUMP(CowsCubeOutput_Intercept);
            MemoryPatch(CubeKeysHook) << JUMP(CubeKeys_Intercept);
            MemoryPatch(CubeOrgansHook) << JUMP(CubeOrgans_Intercept);

            // These are temporary AI replacements.
            // They don't do extra uber stuff.
            MemoryPatch(UberBaalAIPointer) << BaalAI;
            MemoryPatch(UberMephAIPointer) << MephAI;
            MemoryPatch(UberDiabloAIPointer) << DiabloAI;

            MemoryPatch(RoomInit_Original) << JUMP(RoomInit_Hook);
            MemoryPatch(KillMonster_Original) << JUMP(KillMonster_Hook);
        }
    } feature;

}

#endif