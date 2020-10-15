#ifdef _DEBUG

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <unordered_map>
#include <random>
#include <cmath>

#pragma pack(push)
#pragma pack(1)

struct IncompleteGameData {
    BYTE unk[4];                                // 0x0
    DWORD seed;                                 // 0x4
    BYTE unk2[0x20];                            // 0x8
    WORD nServerToken;                          // 0x28
    char szGameName[0x10];                      // 0x2A
    char szGamePassword[0x10];                  // 0x3A
    char szGameDescription[0x20];               // 0x4A
    BYTE nGameType;                             // 0x6A
    BYTE unk3[2];                               // 0x6B
    BYTE nDifficulty;                           // 0x6D
    BYTE unk4[2];                               // 0x6E
    BOOL bExpansion;                            // 0x70
    DWORD dwGameType;                           // 0x74
    WORD wItemFormat;                           // 0x78
    BYTE unk5[0x1CB6];                          // 0x7A
    BYTE nBossFlagList[0x40];                   // 0x1D30
    BYTE unk6[0x64];                            // 0x1D70
    IncompleteGameData* pPrev;                  // 0x1DD4
    IncompleteGameData* pNext;                  // 0x1DD8
    BYTE unk7[0xC];                             // 0x1DDC
}; // Total Size: 0x1DE8

struct ItemGenerationData {
    D2::Types::UnitAny* pUnit;                  // 0x0
    ItemGenerationData* pNext;                  // 0x4
    IncompleteGameData* pGame;                  // 0x8
    int nItemLevel;                             // 0xC
    DWORD unk0x10;                              // 0x10
    int nItemClassId;                           // 0x14
    int dwMode = 3;                             // 0x18
    int nPosX;                                  // 0x1c
    int nPosY;                                  // 0x20
    D2::Types::Room1 *pDrlgRoom;                // 0x24
    short usually_one = 1;                      // 0x28
    WORD wItemFormat;                           // 0x2a
    BOOL somethingCustom;                       // 0x2c
    D2::ItemQuality ItemQuality;                // 0x30
    int nPriceMaybe;                            // 0x34
    DWORD durability;                           // 0x38
    DWORD maxDurability;                        // 0x3C
    DWORD dwFileIndex;                          // 0x40
    DWORD dwFlag;                               // 0x44
    int nInitSeed;                              // 0x48
    int nModSeed;                               // 0x4C
    bool bGrade;                                // 0x50
    BYTE unk0x51[7];                            // 0x51
    char szCustomName[16];                      // 0x58
    DWORD field_0x68[6];                        // 0x68
    DWORD dwFlag2;                              // 0x80
};

struct theTCThingEx {
    long somethingClassic, somethingExpansion;
    short classId;
    long d, e, f, g;
};

struct theTCThing {
    short unk1, TCLevel;
    long count;
    long somethingClassic;
    long somethingExpansion;
    long unk[6];
    struct {
        theTCThingEx data[100];
    }*pSomething;
};

#pragma pack(pop)

union GameFlagsType {
    DWORD raw = 0;
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
    } flags;
};

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
REMOTEFUNC(char* __stdcall, GetTCData, (WORD nTreasureClass, int level), 0x654E00);
REMOTEFUNC(void __fastcall, RollItemToDrop, (IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, D2::Types::UnitAny* pAttacker, char* pTCData, int param_5, int level, bool bIsItemFindDrop, D2::Types::UnitAny** pUnitTable, void* pUnitIndex, int param_10), 0x55A6D0);
REMOTEFUNC(void __fastcall, DropItemByType, (IncompleteGameData* game, D2::Types::UnitAny* pUnit, DWORD dwItemCode, D2::ItemQuality nType), 0x585970);
REMOTEFUNC(D2::Types::UnitAny* __fastcall, SpawnItemWithStruct, (IncompleteGameData* pGame, ItemGenerationData* pItemUnkownServer, int probablyZero), 0x558D90);
REMOTEFUNC(D2::Types::Room1* __fastcall, FindBestSpotToSpawnItem, (D2::Types::Room1* pDrlgRoom, POINT* pUnitPosition, POINT* pNewPosition, WORD param_2_00, BYTE param_5), 0x555DA0);
REMOTEFUNC(DWORD __stdcall, GetItemClassIdByCode, (DWORD dwItemCode), 0x633680);
REMOTEFUNC(void __fastcall, PlaySoundMaybe, (D2::Types::UnitAny* pUnit, short nUpdateType, D2::Types::UnitAny* pUpdateUnit), 0x553380);

REMOTEFUNC(void __fastcall, MephAI, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5F78B0);
REMOTEFUNC(void __fastcall, DiabloAI, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5E9170);
REMOTEFUNC(void __fastcall, BaalAI, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5FCFE0);

ASMPTR UberBaalAIPointer = 0x73D330, UberBaalAI = 0x5FD200;
ASMPTR UberMephAIPointer = 0x73D340, UberMephAI = 0x5F81C0;
ASMPTR UberDiabloAIPointer = 0x73D350, UberDiabloAI = 0x05E9DF0;

ASMPTR CowsCubeOutputHook = 0x565a80, CubeKeysHook = 0x565a90, CubeOrgansHook = 0x565aa0;
ASMPTR RoomInit_Original = 0x542b40, RoomInit_Rejoin = 0x542b46;
ASMPTR KillMonster_Original = 0x57CCB0, KillMonster_Rejoin = 0x57CCB6;

DWORD guid = 0x4FFFFFFF;
int seed = 0;
std::unordered_map<DWORD, GameFlagsType> gameFlags;
std::random_device rd; // obtain a random number from hardware
std::mt19937 gen(rd()); // seed the generator

std::vector<const char*> keyCodes = {
    "pk1 ",
    "pk2 ",
    "pk3 ",
    "dhn ",
    "bey ",
    "mbr ",
};

DWORD GetActFromRoom(D2::Types::Room1* a) {
    return GetAct(a->pRoom2->pLevel->dwLevelNo);
}

D2::Types::UnitAny* PortalTo(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, int levelId, bool isBlue = false) {
    D2::Types::UnitAny* pPortal;
    D2::Types::Room1* target;
    POINT pos;

    if (GetActFromRoom(pUnit->pPath->pRoom1) == GetAct(levelId)) {
        UnitLocation(pUnit, &pos);
        FindSpawnablePosition(pUnit->pPath->pRoom1, &pos, 3, 0x400, &target, 4, 100);
        SpawnPortal(pGame, nullptr, target, pos.x, pos.y, levelId, &pPortal, isBlue ? 0x3b : 0x3c, 0);
        if (pPortal) pPortal->pObjectData->Type = (BYTE) levelId;
        return pPortal;
    }

    return nullptr;
}

template <class T>
T randomElement(std::vector<T> v) {
    std::uniform_int_distribution<> distr(0, v.size() - 1); // define the range
    return v[distr(gen)];
}

// Toggle for debug
const bool KEY_ORGAN_DEBUG = false;

BOOL __fastcall CubeKeys_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (pGame->nDifficulty != 2) return PlaySoundMaybe(pUnit, 0x14, pUnit), 0;
    std::vector<int> p;

    if (!gameFlags[pGame->seed].flags.denPortal) {
        p.push_back(133);
    }
    if (!gameFlags[pGame->seed].flags.sandsPortal) {
        p.push_back(134);
    }
    if (!gameFlags[pGame->seed].flags.furnacePortal) {
        p.push_back(135);
    }

    if (p.size() > 0) {
        int levelTarget = randomElement(p);
        if (PortalTo(pGame, pUnit, levelTarget)) {
            switch (levelTarget) {
            case 133:
                gameFlags[pGame->seed].flags.denPortal = true;
                return !KEY_ORGAN_DEBUG;
            case 134:
                gameFlags[pGame->seed].flags.sandsPortal = true;
                return !KEY_ORGAN_DEBUG;
            case 135:
                gameFlags[pGame->seed].flags.furnacePortal = true;
                return !KEY_ORGAN_DEBUG;
            }
        }
    }

    return PlaySoundMaybe(pUnit, 0x14, pUnit), 0;
}

BOOL __fastcall CubeOrgans_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (pGame->nDifficulty != 2 && !gameFlags[pGame->seed].flags.tristramPortal) return PlaySoundMaybe(pUnit, 0x14, pUnit), 0;

    if (PortalTo(pGame, pUnit, 136)) {
        gameFlags[pGame->seed].flags.tristramPortal = true;
        return !KEY_ORGAN_DEBUG;
    }

    return PlaySoundMaybe(pUnit, 0x14, pUnit), 0;
}

BOOL __fastcall CowsCubeOutput_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    return CowsCubeOutputOriginal(pGame, pUnit);
}

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
            if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].flags.lillithSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 707, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].flags.lillithSpawned = true;
                }
            }
        }
        break;
    case 134: // Sands
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            if (unit->dwTxtFileNo == 402 && !gameFlags[pGame->seed].flags.durielSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 708, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].flags.durielSpawned = true;
                }
            }
        }
        break;
    case 135: // Furnace
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].flags.izualSpawned) {
                char mods[9] = { 1 };
                if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 706, guid++, 0, false, false, 0, mods)) {
                    gameFlags[pGame->seed].flags.izualSpawned = true;
                }
            }
        }
        break;
    case 136: // Uber Tristram
        for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
            switch (unit->dwTxtFileNo) {
            case 26: // Cain cage original, uber spawns
                char mods[9] = { 1 };
                if (!gameFlags[pGame->seed].flags.uberMephSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 704, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].flags.uberMephSpawned = true;
                    }
                }
                if (!gameFlags[pGame->seed].flags.uberBaalSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 709, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].flags.uberBaalSpawned = true;
                    }
                }
                if (!gameFlags[pGame->seed].flags.uberDiabloSpawned) {
                    if (SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 705, guid++, 0, false, false, 0, mods)) {
                        gameFlags[pGame->seed].flags.uberDiabloSpawned = true;
                    }
                }
                break;
            }
        }
        break;
    }
}

__declspec(naked) void __fastcall KillMonster_Relocated(IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, D2::Types::UnitAny* pAttacker, BOOL bRemoveFromOwner) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x20
        jmp KillMonster_Rejoin
    }
}

D2::Types::UnitAny* SpawnItem(IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, const char szItemCode[5], int itemLevel, D2::ItemQuality nQuality, int dwTxtFileNo = -1) {
    ItemGenerationData pItemGen = { 0 };
    POINT pos, newPos;

    UnitLocation(pVictim, &pos);
    pItemGen.pDrlgRoom = FindBestSpotToSpawnItem(pVictim->pPath->pRoom1, &pos, &newPos, 1, 1);
    pItemGen.wItemFormat = pGame->wItemFormat;
    pItemGen.nItemClassId = GetItemClassIdByCode(*(DWORD*)szItemCode);
    pItemGen.nPosX = newPos.x;
    pItemGen.nPosY = newPos.y;
    pItemGen.ItemQuality = nQuality;
    pItemGen.pGame = pGame;
    pItemGen.nItemLevel = itemLevel;

    if (dwTxtFileNo >= 0) {
        pItemGen.somethingCustom = 1;
        pItemGen.dwFileIndex = dwTxtFileNo;
    }

    pItemGen.nInitSeed = rand() % 666;
    pItemGen.nModSeed = rand() % 666;

    return SpawnItemWithStruct(pGame, &pItemGen, 1);
}

void __fastcall KillMonster_Hook(IncompleteGameData* pGame, D2::Types::UnitAny* pVictim, D2::Types::UnitAny* pAttacker, BOOL bRemoveFromOwner) {
    KillMonster_Relocated(pGame, pVictim, pAttacker, bRemoveFromOwner);

    if (pVictim->pPath->pRoom1->pRoom2->pLevel->dwLevelNo == 136) {
        switch (pVictim->dwTxtFileNo) {
        case 704:
            gameFlags[pGame->seed].flags.uberMephKilled = true;
            break;
        case 705:
            gameFlags[pGame->seed].flags.uberDiabloKilled = true;
            break;
        case 709:
            gameFlags[pGame->seed].flags.uberBaalKilled = true;
            break;
        }
    }
    else if (pVictim->dwTxtFileNo == 705) { // Diablo clone drops anni :)
        SpawnItem(pGame, pVictim, "cm1 ", 90, D2::ItemQuality::UNIQUE, 381);
    }
    else if constexpr (KEY_ORGAN_DEBUG && rand() % 2 == 0) {
        SpawnItem(pGame, pVictim, randomElement(keyCodes), 90, D2::ItemQuality::NORMAL);
    }

    if (gameFlags[pGame->seed].flags.uberMephKilled && gameFlags[pGame->seed].flags.uberDiabloKilled && gameFlags[pGame->seed].flags.uberBaalKilled && !gameFlags[pGame->seed].flags.torchDropped) {
        if (SpawnItem(pGame, pVictim, "cm2 ", 90, D2::ItemQuality::UNIQUE, 400)) {
            SpawnItem(pGame, pVictim, "std ", 90, D2::ItemQuality::NORMAL);
            gameFlags[pGame->seed].flags.torchDropped = true;
        }
    }
}

const char minionMods[9] = { 0 };

void __fastcall UberMephAIReplacement(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams) {
    // @TODO: Spawn rates are aggressive, needs tweaking
    const std::vector<DWORD> ids = { 725, 726, 727, 728, 729, 730 }, chances = { 0, 0, 0, 1, 2, 3 };
    D2::Types::UnitAny* pTarget = pAiParams->pTarget;
    int c = randomElement(chances);

    while (c--) {
        SpawnMonster(pGame, pTarget->pPath->pRoom1, pTarget->pPath->xPos, pTarget->pPath->yPos, randomElement(ids), guid++, rand(), false, false, 0, minionMods);
    }

    MephAI(pGame, pUnit, pAiParams);
}

void __fastcall UberDiabloAIReplacement(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams) {
    // @TODO: Spawn rates are aggressive, needs tweaking
    const std::vector<DWORD> chances = { 0, 0, 0, 1 };
    D2::Types::UnitAny* pTarget = pAiParams->pTarget;
    int c = randomElement(chances);

    while (c--) {
        SpawnMonster(pGame, pTarget->pPath->pRoom1, pTarget->pPath->xPos, pTarget->pPath->yPos, 711, guid++, rand(), false, false, 0, minionMods);
    }

    DiabloAI(pGame, pUnit, pAiParams);
}

void __fastcall UberBaalAIReplacement(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams) {
    // @TODO: Spawn rates are aggressive, needs tweaking
    const std::vector<DWORD> ids = { 731, 732 }, chances = { 0, 0, 0, 1, 2, 3 };
    D2::Types::UnitAny* pTarget = pAiParams->pTarget;
    int c = randomElement(chances);

    while (c--) {
        SpawnMonster(pGame, pTarget->pPath->pRoom1, pTarget->pPath->xPos, pTarget->pPath->yPos, randomElement(ids), guid++, rand(), false, false, 0, minionMods);
    }

    BaalAI(pGame, pUnit, pAiParams);
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
            MemoryPatch(UberMephAI) << JUMP(UberMephAIReplacement);
            MemoryPatch(UberDiabloAI) << JUMP(UberDiabloAIReplacement);
            MemoryPatch(UberBaalAI) << JUMP(UberBaalAIReplacement);

            MemoryPatch(RoomInit_Original) << JUMP(RoomInit_Hook);
            MemoryPatch(KillMonster_Original) << JUMP(KillMonster_Hook);

            // Ignore durability modification on custom spawn items
            MemoryPatch(0x559009) << NOP_TO(0x559025);
        }

    } feature;

}

#endif
