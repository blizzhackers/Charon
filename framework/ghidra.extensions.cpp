/**
 *
 * @Author Jaenster
 * @Description

 Please read headers/ghidra/readme.md first

 This file is automatically read by the ghidra-tooling script and these methods are automaticlly attached with header to ghidra.h
                                        https://github.com/jaenster/ghidra-tooling/


 There are 2 ways of defining a method
 1) Ghidra::D2RoomExStrc::getWorldX()
 2) Within correct namespace block

 Do not:
 - Put anything at the same line as the definition. (dont put comments before the line of code)
 - Dont use depth (1 level max) namespace tags. My regex isn't that fancy
 - I dont want to write a full c++ parser so please keep the function definitions a bit clean
 - Dont write templated functions (might add this in the future)

 Please do not create function signatures separately from the definition. As it's signature gets included in Ghidra.h anyway
 IDE's dont always get external changes, so if script is running with live reloading, simply open ghidra.h for your IDE
 to automatically update the file

 */

#include "headers/D2Structs.h" // DPOINT
#include "headers/ghidra.h"
namespace Ghidra {

    D2UnitPlayer* D2UnitStrc::asPlayer() {
        return reinterpret_cast<D2UnitPlayer *>(this);
    }

    D2UnitMonster* D2UnitStrc::asMonster() {
        return reinterpret_cast<D2UnitMonster *>(this);
    }

    D2UnitMissile* D2UnitStrc::asMissile() {
        return reinterpret_cast<D2UnitMissile *>(this);
    }

    D2UnitObject* D2UnitStrc::asObject() {
        return reinterpret_cast<D2UnitObject *>(this);
    }

    D2UnitItem* D2UnitStrc::asItem() {
        return reinterpret_cast<D2UnitItem *>(this);
    }

    D2UnitWarp* D2UnitStrc::asWarp() {
        return reinterpret_cast<D2UnitWarp *>(this);
    }

    DWORD D2RoomExStrc::getWorldX() {
        return pRoom->sCoords.dwXStart;
    }

    DWORD D2RoomExStrc::getWorldY() {
        return pRoom->sCoords.dwYStart;
    }

    DWORD D2RoomExStrc::getWorldWidth() {
        return pRoom->sCoords.dwXSize;
    }


    DWORD D2RoomExStrc::getWorldHeight() {
        return pRoom->sCoords.dwYSize;
    }

    DWORD D2RoomExStrc::getCollision(DWORD localx, DWORD localy, WORD mask) {
        // untested, converted blindly from current version
        return pRoom->pCollisionGrid->pMapStart[localx + localy * pRoom->pCollisionGrid->World.WorldSize.x] & mask;
    }

    DPOINT D2PresetUnitStrc::pos(D2RoomExStrc* pRoom, DPOINT adjust) {
        return { adjust.x + (double)pRoom->sCoords.WorldPosition.x * 5 + (double)this->nPosX, adjust.y + (double)pRoom->sCoords.WorldPosition.y * 5 + (double)this->nPosY };
    }

    DPOINT D2UnitStrc::pos() {
        return this->pos(DPOINT(0.0,0.0));
    }

    DPOINT D2UnitStrc::pos(DPOINT adjust) {
        if (this->eUnitType == UNIT_OBJECT || this->eUnitType == UNIT_WARP || this->eUnitType == UNIT_ITEM) {
            return { adjust.x + (double)this->pPath.pStaticPath->nPosX, adjust.y + (double)this->pPath.pStaticPath->nPosY };
        }

        return { 0, 0}; // ToDo; need to fix dynamic path as its wrong defined in ghidra
    }

    DPOINT D2UnitStrc::getTargetPos(DPOINT adjust) {
        if (this->eUnitType == UNIT_OBJECT || this->eUnitType == UNIT_WARP || this->eUnitType == UNIT_ITEM) {
            return this->pos();
        }

        return { 0, 0}; // ToDo; need to fix dynamic path as its wrong defined in ghidra
    }
}