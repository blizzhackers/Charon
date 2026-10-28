/**
 * Displays units on the map and automap.
 */

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <iostream>

DWORD ItemRarityColor[32] = { 255, 29, 30, 32, 151, 132, 111, 155, 111 };

// This feature class registers itself.
class : public Feature {
public:
    void init() {
        // gamelog << COLOR(4) << "Installing unit tracker..." << std::endl;
    }

    void gameAutomapPostDraw() {
        if (Settings["showMissiles"]) {
            // Client side tracks missiles
            for (D2::Types::MissileUnit* unit : D2::ClientSideUnitHashTables.missiles.all()) {
                unit->DrawAutomapDot(0x99);
            }
        }

        if (Settings["debugMode"]) {

            // Server side tracks objects
            for (D2::Types::ObjectUnit* unit : D2::ServerSideUnitHashTables.objects.all()) {
                unit->DrawAutomapDot(0x69);
            }

            if (D2::pPlayerUnit->getLevel() != nullptr) {
                for (D2::Types::Room2* room : D2::pPlayerUnit->getLevel()->getAllRoom2()) {
                    for (D2::Types::PresetUnit* unit : room->getAllPresetUnits()) {
                        unit->pos(room).DrawAutomapDot(0x84);
                    }
                }
            }
        }

        if (Settings["showItems"]) {
            // Server side tracks items
            for (D2::Types::ItemUnit* unit : D2::ServerSideUnitHashTables.items.all()) {
                if (unit->dwMode == 3 || unit->dwMode == 5) {
                    if (unit->pItemData->dwFlags & 0x4000000) {
                        unit->DrawAutomapX(ItemRarityColor[7], 3);
                    }
                    else if (unit->pItemData->dwQuality > 3 || (unit->pItemData->dwFlags & 0x400800 && unit->pItemData->dwQuality > 2 && D2::STATSEX_GetCurrentStat(unit, 194, 0) >= 2)) { // @todo check number of sockets
                        unit->DrawAutomapX(ItemRarityColor[unit->pItemData->dwQuality], 3);
                    }
                    else {
                        D2::Types::ItemTxt* txt = D2::TXT_Items_GetLine(unit->dwTxtFileNo);
                        if (txt->nType >= 96 && txt->nType <= 102 || txt->nType == 74) {
                            unit->DrawAutomapX(169, 3);
                        }
                    }
                }
            }
        }

        if (Settings["showMonsters"]) {
            // Server side tracks enemies
            for (D2::Types::NonPlayerUnit* unit : D2::ServerSideUnitHashTables.nonplayers.all()) {
                if (unit->isPlayerHostile() && unit->unitHP() > 0) {
                    if (unit->isAttackable()) {
                        if (unit->pMonsterData->fUnique || unit->pMonsterData->fChamp) {
                            unit->DrawAutomapX(0x0C);
                        }
                        else if (unit->pMonsterData->fMinion) {
                            unit->DrawAutomapX(0x0B);
                        }
                        else {
                            unit->DrawAutomapX(0x0A);
                        }
                    }
                    else {
                        unit->DrawAutomapX(0x1B);
                    }
                }
            }
        }
    }
} feature;
