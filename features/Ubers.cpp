/**
 * Uber Tristram and Diablo Clone
 *
 * by Nishi with help from Jaenster
 */
#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/D2Structs.h"
#include <unordered_map>
#include <random>
#include <cmath>
#include <climits>

using D2::Types::IncompleteGameData;

#pragma pack(push)
#pragma pack(1)

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

struct WEdata {
    BYTE  type = 0x5A;
    BYTE  fct = 0x11;
    BYTE  id = 4;
    DWORD param;
    WORD  z = 0;
    BYTE  uk[31] = { 0 };
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
        bool dCloneAnnounced : 1;
        bool dCloneKilled : 1;
    } flags;
};

REMOTEFUNC(D2::Types::UnitAny* __fastcall, SERVER_SpawnMonster, (IncompleteGameData* pGame, D2::Types::Room1* pRoom, int x, int y, DWORD dwClassId, DWORD guid, DWORD seed, BOOL isChampion, BOOL isSuperUnique, DWORD superUniqueId, const char (&mods)[9]), 0x5A4440);
REMOTEFUNC(void __fastcall, SpawnMonsterAtRoomPos, (IncompleteGameData* pGame, D2::Types::Room1* pRoom, int32_t x, int32_t y, DWORD dwClassId, DWORD dwMode, DWORD dwUnkOne, DWORD dwUnkZero), 0x5b2f20);
REMOTEFUNC(D2::Types::UnitAny* __fastcall, ITEM_CreateItemInstance, (IncompleteGameData* pGame, ItemGenerationData* pItemUnkownServer, int probablyZero), 0x558D90);
REMOTEFUNC(D2::Types::Room1* __fastcall, FindBestSpotToSpawnItem, (D2::Types::Room1* pDrlgRoom, POINT* pUnitPosition, POINT* pNewPosition, WORD param_2_00, BYTE param_5), 0x555DA0);
REMOTEFUNC(DWORD __stdcall, TXT_Items_ConvertItemCodeToItemClassId, (DWORD dwItemCode), 0x633680);
REMOTEFUNC(void __fastcall, SetupUpdateEvent_AttachEventSound, (D2::Types::UnitAny* pUnit, short nUpdateType, D2::Types::UnitAny* pUpdateUnit), 0x553380);
REMOTEFUNC(void __fastcall, ForEachClient, (IncompleteGameData* pGame, void* func, void *param), 0x52ded0);
void* NET_D2GS_SERVER_Send_0x5A_SYSTEMMSG = (void*)0x53c850;

REMOTEFUNC(void __fastcall, AITHINK_Fn1_Mephisto, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5F78B0);
REMOTEFUNC(void __fastcall, AITHINK_Fn1_Diablo, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5E9170);
REMOTEFUNC(void __fastcall, AI_Fn1_BaalCrab, (IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams), 0x5FCFE0);

ASMPTR AI_Fn1_UberBaal = 0x5FD200;
ASMPTR AITHINK_Fn1_UberMephisto = 0x5F81C0;
ASMPTR AITHINK_Fn1_UberDiablo = 0x05E9DF0;

ASMPTR CUBE_SpecialOutput_Unused2 = 0x565a90, CUBE_SpecialOutput_Unused3 = 0x565aa0;
ASMPTR SERVER_KillMonster_Original = 0x57CCB0, KillMonster_Rejoin = 0x57CCB6;

DWORD guid = 0x4FFFFFFF;
int seed = 0;
std::unordered_map<DWORD, GameFlagsType> gameFlags;
std::random_device rd2; // obtain a random number from hardware
std::mt19937 gen2(rd2()); // seed the generator

std::vector<const char*> keyCodes = {
    "pk1 ",
    "pk2 ",
    "pk3 ",
    "dhn ",
    "bey ",
    "mbr ",
};

DWORD GetActFromRoom(D2::Types::Room1* a) {
    return D2::GetActNoFromLevelNumber(a->pRoom2->pLevel->dwLevelNo);
}

D2::Types::UnitAny* PortalTo(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, int levelId, bool isBlue = false) {
    D2::Types::UnitAny* pPortal;
    D2::Types::Room1* target;
    POINT pos;

    if (GetActFromRoom(pUnit->pPath->pRoom1) == D2::GetActNoFromLevelNumber(levelId)) {
        D2::GetCoords(pUnit, &pos);
        D2::FindSpawnableLocation(pUnit->pPath->pRoom1, &pos, 3, 0x400, &target, 4, 100);
        D2::SERVER_SpawnPortal(pGame, nullptr, target, pos.x, pos.y, levelId, &pPortal, isBlue ? 0x3b : 0x3c, 0);
        if (pPortal) pPortal->pObjectData->Type = (BYTE) levelId;
        return pPortal;
    }

    return nullptr;
}

template <class T>
T randomElement(std::vector<T> v) {
    std::uniform_int_distribution<> distr(0, v.size() - 1); // define the range
    return v[distr(gen2)];
}

int randomNumber(int min, int max) {
    std::uniform_int_distribution<> distr(min, max); // define the range
    return distr(gen2);
}

// Toggle for debug
const bool KEY_ORGAN_DEBUG = false;

BOOL __fastcall CubeKeys_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (!Settings["ladderItems"]) {
        return FALSE;
    }

    if (pGame->nDifficulty != 2) return SetupUpdateEvent_AttachEventSound(pUnit, 0x14, pUnit), 0;
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

    return SetupUpdateEvent_AttachEventSound(pUnit, 0x14, pUnit), 0;
}

BOOL __fastcall CubeOrgans_Intercept(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit) {
    if (!Settings["ladderItems"]) {
        return FALSE;
    }

    if (pGame->nDifficulty != 2 && !gameFlags[pGame->seed].flags.tristramPortal) return SetupUpdateEvent_AttachEventSound(pUnit, 0x14, pUnit), 0;

    if (PortalTo(pGame, pUnit, 136)) {
        gameFlags[pGame->seed].flags.tristramPortal = true;
        return !KEY_ORGAN_DEBUG;
    }

    return SetupUpdateEvent_AttachEventSound(pUnit, 0x14, pUnit), 0;
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

    D2::GetCoords(pVictim, &pos);
    pItemGen.pDrlgRoom = FindBestSpotToSpawnItem(pVictim->pPath->pRoom1, &pos, &newPos, 1, 1);
    pItemGen.wItemFormat = pGame->wItemFormat;
    pItemGen.nItemClassId = TXT_Items_ConvertItemCodeToItemClassId(*(DWORD*)szItemCode);
    pItemGen.nPosX = newPos.x;
    pItemGen.nPosY = newPos.y;
    pItemGen.ItemQuality = nQuality;
    pItemGen.pGame = pGame;
    pItemGen.nItemLevel = itemLevel;

    if (dwTxtFileNo >= 0) {
        pItemGen.somethingCustom = 1;
        pItemGen.dwFileIndex = dwTxtFileNo;
    }

    // Use the hardware-seeded mt19937 (gen2/rd2) declared above instead of CRT
    // rand(): there is no srand() anywhere in Charon, so bare rand() produces
    // the same sequence after every Game.exe launch — making affixes like
    // randclassskill cycle the same classes (Ama, Barb, Sorc, …) on each
    // fresh process. The full 32-bit seed space is exposed because nInitSeed
    // / nModSeed are DWORDs in ItemGenerationData.
    pItemGen.nInitSeed = randomNumber(0, INT_MAX);
    pItemGen.nModSeed  = randomNumber(0, INT_MAX);

    return ITEM_CreateItemInstance(pGame, &pItemGen, 1);
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
    const std::vector<DWORD> ids = { 725, 726, 727, 728, 729, 730 };
    D2::Types::UnitAny* pTarget = pAiParams->pTarget;

    if (randomNumber(0, 9) < 3) {
        SpawnMonsterAtRoomPos(pGame, pTarget->pPath->pRoom1, pTarget->pPath->xPos, pTarget->pPath->yPos, randomElement(ids), 8, 1, 0);
    }

    AITHINK_Fn1_Mephisto(pGame, pUnit, pAiParams);
}

void __fastcall UberDiabloAIReplacement(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams) {
    D2::Types::UnitAny* pTarget = pAiParams->pTarget;

    if (randomNumber(0, 9) == 0) {
        SpawnMonsterAtRoomPos(pGame, pTarget->pPath->pRoom1, pTarget->pPath->xPos, pTarget->pPath->yPos, 711, 1, 1, 0);
    }

    AITHINK_Fn1_Diablo(pGame, pUnit, pAiParams);
}

void __fastcall UberBaalAIReplacement(IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::AIParams* pAiParams) {
    const std::vector<DWORD> ids = { 731, 732 };

    if (randomNumber(0, 9) < 3) {
        SpawnMonsterAtRoomPos(pGame, pUnit->pPath->pRoom1, pUnit->pPath->xPos, pUnit->pPath->yPos, randomElement(ids), 1, 1, 0);
    }

    AI_Fn1_BaalCrab(pGame, pUnit, pAiParams);
}

__declspec(naked) void __fastcall SPAWN_UniqueMonster_Relocated(IncompleteGameData* pGame, D2::Types::Room1* pRoom, int32_t nX, int32_t nY, uint32_t dwUniqueId) {
    static ASMPTR SPAWN_UniqueMonster_Rejoin = 0x5a49b9;
    __asm {
        push   ebp        //5a49b0 -> 55
        mov    ebp, esp    //5a49b1 -> 8b ec
        sub    esp, 0x110  //5a49b3 -> 81 ec 10 01 00 00
        jmp    SPAWN_UniqueMonster_Rejoin
    };
}

void __fastcall SPAWN_UniqueMonster(IncompleteGameData* pGame, D2::Types::Room1* pRoom, int32_t nX, int32_t nY, uint32_t dwUniqueId) {
    if (Settings["ladderItems"] && gameFlags[pGame->seed].flags.dCloneAnnounced && !gameFlags[pGame->seed].flags.dCloneSpawned) {
        char mods[9] = { 1 };
        if (SERVER_SpawnMonster(pGame, pRoom, nX, nY, 333, guid++, 0, false, false, 0, mods)) {
            gameFlags[pGame->seed].flags.dCloneSpawned = true;
        }
    }
    else {
        SPAWN_UniqueMonster_Relocated(pGame, pRoom, nX, nY, dwUniqueId);
    }
}

namespace Ubers {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(CUBE_SpecialOutput_Unused2) << JUMP(CubeKeys_Intercept);
            MemoryPatch(CUBE_SpecialOutput_Unused3) << JUMP(CubeOrgans_Intercept);

            // These are temporary AI replacements.
            // They don't do extra uber stuff.
            MemoryPatch(AITHINK_Fn1_UberMephisto) << JUMP(UberMephAIReplacement);
            MemoryPatch(AITHINK_Fn1_UberDiablo) << JUMP(UberDiabloAIReplacement);
            MemoryPatch(AI_Fn1_UberBaal) << JUMP(UberBaalAIReplacement);

            MemoryPatch(SERVER_KillMonster_Original) << JUMP(KillMonster_Hook);
            MemoryPatch(0x5a49b0) << JUMP(SPAWN_UniqueMonster) << NOP_TO(0x5a49b9);

            // Ignore durability modification on custom spawn items
            MemoryPatch(0x559009) << NOP_TO(0x559025);

            if (!Settings["sojLimit"]) {
                Settings["sojCount"] = randomNumber(80, 120);
                Settings["sojLimit"] = Settings["sojCount"] + randomNumber(80, 120);
            }
        }

        void gameServerLoop(IncompleteGameData* pGame) {
            if (pGame->nDifficulty == 2 && Settings["ladderItems"] && !gameFlags[pGame->seed].flags.dCloneAnnounced) {
                if (randomNumber(1, Settings["sojSaleTime"] * 25) == 1) {
                    WEdata packet;
 
                    Settings["sojCount"] = Settings["sojCount"] + 1;
                    packet.param = Settings["sojCount"];

                    if (Settings["sojCount"] >= Settings["sojLimit"]) {
                        packet.fct++;
                        gameFlags[pGame->seed].flags.dCloneAnnounced = true;
                    }

                    while (Settings["sojCount"] >= Settings["sojLimit"]) {
                        Settings["sojLimit"] += randomNumber(80, 120);
                    }

                    SaveSettings();
                    ForEachClient(pGame, NET_D2GS_SERVER_Send_0x5A_SYSTEMMSG, &packet);
                }
            }
        }

        void roomInit(IncompleteGameData* pGame, D2::Types::Room1* pRoom1) {
            D2::Types::Level* level = pRoom1->pRoom2->pLevel;
            D2::Types::Room2* room = pRoom1->pRoom2;
            D2::Types::PresetUnit* unit;


            switch (level->dwLevelNo) {
            case 133: // Den
                for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
                    if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].flags.lillithSpawned) {
                        char mods[9] = { 1 };
                        if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 707, guid++, 0, false, false, 0, mods)) {
                            gameFlags[pGame->seed].flags.lillithSpawned = true;
                        }
                    }
                }
                break;
            case 134: // Sands
                for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
                    if (unit->dwTxtFileNo == 402 && !gameFlags[pGame->seed].flags.durielSpawned) {
                        char mods[9] = { 1 };
                        if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 708, guid++, 0, false, false, 0, mods)) {
                            gameFlags[pGame->seed].flags.durielSpawned = true;
                        }
                    }
                }
                break;
            case 135: // Furnace
                for (unit = room->pPreset; unit != NULL; unit = unit->pPresetNext) {
                    if (unit->dwTxtFileNo == 397 && !gameFlags[pGame->seed].flags.izualSpawned) {
                        char mods[9] = { 1 };
                        if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 706, guid++, 0, false, false, 0, mods)) {
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
                            if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 704, guid++, 0, false, false, 0, mods)) {
                                gameFlags[pGame->seed].flags.uberMephSpawned = true;
                            }
                        }
                        if (!gameFlags[pGame->seed].flags.uberBaalSpawned) {
                            if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 709, guid++, 0, false, false, 0, mods)) {
                                gameFlags[pGame->seed].flags.uberBaalSpawned = true;
                            }
                        }
                        if (!gameFlags[pGame->seed].flags.uberDiabloSpawned) {
                            if (SERVER_SpawnMonster(pGame, pRoom1, room->dwPosX * 5 + unit->dwPosX, room->dwPosY * 5 + unit->dwPosY, 705, guid++, 0, false, false, 0, mods)) {
                                gameFlags[pGame->seed].flags.uberDiabloSpawned = true;
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }

    } feature;

}
