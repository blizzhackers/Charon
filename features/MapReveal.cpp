/**
 * Reveals the automap when moving to new areas.
 */

#define _USE_MATH_DEFINES

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <iostream>
#include <cmath>

REMOTEFUNC(DWORD __stdcall, GetAutomapSize, (void), 0x45A710);
REMOTEFUNC(D2::Types::AutomapCell* __fastcall, NewAutomapCell, (), 0x457C30);
REMOTEFUNC(void __fastcall, AddAutomapCell, (D2::Types::AutomapCell* aCell, D2::Types::AutomapCell** node), 0x457B00);
REMOTEFUNC(void __stdcall, RevealAutomapRoom, (D2::Types::Room1* pRoom1, DWORD dwClipFlag, D2::Types::AutomapLayer* aLayer), 0x458F40);
REMOTEFUNC(D2::Types::AutomapLayer2* __fastcall, GetLayer, (DWORD dwLevelNo), 0x61E470);
REMOTEREF(D2::Types::AutomapLayer*, AutomapLayer, 0x7A5164);

struct RevealData {
    bool isRevealed = false;
    D2::Types::Room2* room2 = nullptr;
    D2::Types::AutomapLayer* layer = nullptr;
};

// This feature class registers itself.
class : public Feature {
    DWORD currentLevel = 0;
    DWORD revealStart = 0;
    bool inGame = false;

    std::map<DWORD, RevealData> revealdata;

public:
    void init() {
        MemoryPatch(0x4DC000) << ASM::RET; // Disable fade effects from switching areas (so we can reveal sooner)
    }

    void gameLoop() {
        if (!inGame) {
            revealdata.clear();
            inGame = true;
        }
        D2::Types::UnitAny* me = D2::PlayerUnit;

        if (Settings["revealLevel"] && me && me->pPath && me->pPath->pRoom1 && me->pPath->pRoom1->pRoom2 && me->pPath->pRoom1->pRoom2->pLevel) {
            DWORD levelno = me->pPath->pRoom1->pRoom2->pLevel->dwLevelNo;
            if (levelno != currentLevel) {
                currentLevel = levelno;
                revealStart = GetTickCount();
            }

            if (GetTickCount() - revealStart > 200) {
                RevealCurrentLevel();
            }
        }
    }

    void oogLoop() {
        currentLevel = 0;
        inGame = false;
    }

    bool EnsureRoom(D2::Types::Room2* current) {
        if (current->pRoom1 != nullptr) {
            return true;
        }

        D2::AddRoomData(current->pLevel->pMisc->pAct, current->pLevel->dwLevelNo, current->dwPosX, current->dwPosY, current->pRoom1);
        return current->pRoom1 != nullptr;
    }

    void RevealCurrentLevel() {
        D2::Types::UnitAny* me = D2::PlayerUnit;
        int c = 20;

        if (me) for (D2::Types::Level* level = me->pAct->pMisc->pLevelFirst; level != nullptr; level = level->pNextLevel) {
            if (level && level->dwLevelNo > 0) {
                if (!revealdata[level->dwLevelNo].isRevealed && level->pRoom2First != nullptr && AutomapLayer->nLayerNo == GetLayer(level->dwLevelNo)->nLayerNo) {
                    revealdata[level->dwLevelNo].layer = AutomapLayer;
                    revealdata[level->dwLevelNo].room2 = level->pRoom2First;
                    revealdata[level->dwLevelNo].isRevealed = true;
                }

                D2::Types::Room2* room2 = revealdata[level->dwLevelNo].room2;

                for (; c > 0 && room2 != nullptr; c--) {
                    if (room2->pLevel && room2->pLevel->pMisc && room2->pLevel->pMisc->pAct) {
                        if (EnsureRoom(room2)) {
                            RevealAutomapRoom(room2->pRoom1, TRUE, revealdata[level->dwLevelNo].layer);
                            DrawPresets(room2, revealdata[level->dwLevelNo].layer);
                            // Not sure that we need to unload the room again... we're going to need it again later.
                            // D2::RemoveRoomData(room2->pLevel->pMisc->pAct, room2->pLevel->dwLevelNo, room2->dwPosX, room2->dwPosY, room2->pRoom1);
                        }
                    }

                    room2 = revealdata[level->dwLevelNo].room2 = revealdata[level->dwLevelNo].room2->pRoom2Next;
                }
            }
        }
    }

    // Most of this stuff is based on the d2bs preset unit implementation.
    void GenerateCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, int mCell, D2::Types::AutomapLayer* layer) {
        D2::Types::AutomapCell* pCell = NewAutomapCell();
        pCell->nCellNo = (WORD)mCell;
        int pX = (pUnit->dwPosX + (pRoom2->dwPosX * 5));
        int pY = (pUnit->dwPosY + (pRoom2->dwPosY * 5));
        pCell->xPixel = (WORD)((((pX - pY) * 16) / 10) + 1);
        pCell->yPixel = (WORD)((((pY + pX) * 8) / 10) - 3);
        AddAutomapCell(pCell, &(layer->pObjects));
    }

    void GenerateMegaMarkerCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, D2::Types::AutomapLayer* layer) {
        GenerateCell(pRoom2, pUnit, 301, layer);
    }

    void GenerateMarkerCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, D2::Types::AutomapLayer* layer) {
        GenerateCell(pRoom2, pUnit, 300, layer);
    }

    void GenerateSuperchestCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, D2::Types::AutomapLayer* layer) {
        GenerateCell(pRoom2, pUnit, 318, layer);
    }

    void GenerateChestCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, D2::Types::AutomapLayer* layer) {
        GenerateCell(pRoom2, pUnit, 319, layer);
    }

    void GenerateOwnCell(D2::Types::Room2* pRoom2, D2::Types::PresetUnit* pUnit, D2::Types::AutomapLayer* layer) {
        D2::Types::ObjectTxt* obj = obj = D2::GetObjectText(pUnit->dwTxtFileNo);

        if (obj->nAutoMap > 0) {
            GenerateCell(pRoom2, pUnit, obj->nAutoMap, layer);
        }
    }

    void DrawPresets(D2::Types::Room2* pRoom2, D2::Types::AutomapLayer* layer) {
        for (D2::Types::PresetUnit* pUnit = pRoom2->pPreset; pUnit; pUnit = pUnit->pPresetNext) {
            switch (pUnit->dwType) {
            case 1: // NPCs
                switch (pUnit->dwTxtFileNo) {
                case 250: // Summoner
                case 256: // Izual
                    GenerateMarkerCell(pRoom2, pUnit, layer);
                    continue;
                }
                break;
            case 2: // Objects
                switch (pUnit->dwTxtFileNo) {
                case 580:
                    // Lower Kurast uber chests
                    if (pRoom2->pLevel->dwLevelNo == 79) {
                        GenerateChestCell(pRoom2, pUnit, layer);
                        continue;
                    }
                    break;
                case 371: // Countess Chest - not sure if this works though
                    GenerateSuperchestCell(pRoom2, pUnit, layer);
                    continue;
                case 152: // Tomb Orifice
                    GenerateMarkerCell(pRoom2, pUnit, layer);
                    continue;
                case 460: // Frozen Anya
                    GenerateCell(pRoom2, pUnit, 1468, layer);
                    continue;
                case 402: // Canyon Waypoint
                    if (pRoom2->pLevel->dwLevelNo == 46) {
                        //continue;
                    }
                    break;
                case 267: // Stashes in act 1, 2, 5 Get double rendered.
                    if (pRoom2->pLevel->dwLevelNo != 75 && pRoom2->pLevel->dwLevelNo != 103) {
                        continue;
                    }
                    break;
                case 268: // Wirt's Body
                    GenerateMarkerCell(pRoom2, pUnit, layer);
                    continue;
                case 376: // Hellforge gets double rendered.
                    if (pRoom2->pLevel->dwLevelNo == 107) {
                        continue;
                    }
                    break;
                }

                if (pUnit->dwTxtFileNo <= 574) {
                    GenerateOwnCell(pRoom2, pUnit, layer);
                }

                break;

            case 5:
                if (pRoom2->pLevel && pRoom2->pLevel->pMisc && pRoom2->pLevel->pMisc->dwStaffTombLevel) {
                    for (D2::Types::RoomTile* pRoomTile = pRoom2->pRoomTiles; pRoomTile; pRoomTile = pRoomTile->pNext) {
                        if (*(pRoomTile->nNum) == pUnit->dwTxtFileNo && pRoomTile->pRoom2->pLevel->dwLevelNo == pRoom2->pLevel->pMisc->dwStaffTombLevel) {
                            GenerateMegaMarkerCell(pRoom2, pUnit, layer);
                        }
                    }
                }

                break;
            }
        }
    }
} feature;
