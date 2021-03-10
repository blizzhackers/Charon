/**
 *
 * @Author Jaenster
 * @see readme.me
 * @Description

 This contains classes that are spliced out of the ghidra files.

 Like the game only has a D2UnitStrc, but i want to have type specific functions
 As you cant drop() a monster, while an item doesnt have health

*/
#pragma once

#include "./main.h"

namespace Ghidra {

    class D2UnitPlayer : public D2UnitStrc {
    public:
        D2UnitDataPlayerStrc *getUnitData() {
            if (this->eUnitType != UNIT_PLAYER) return nullptr;
            return this->pUnitData.pUnitDataPlayer;
        }

        D2DynamicPathStrc* getPath() {
            return this->pPath.pDynamicPath;
        }
    };

    class D2UnitMonster : public D2UnitStrc {
    public:
        D2UnitDataMonsterStrc *getUnitData() {
            if (this->eUnitType != UNIT_MONSTER) return nullptr;
            return this->pUnitData.pUnitDataMonster;
        }

        D2DynamicPathStrc* getPath() {
            return this->pPath.pDynamicPath;
        }
    };

    class D2UnitObject : public D2UnitStrc {
    public:
        D2UnitDataObjectStrc *getUnitData() {
            if (this->eUnitType != UNIT_OBJECT) return nullptr;
            return this->pUnitData.pUnitDataObject;
        }
    };

    class D2UnitMissile : public D2UnitStrc {
    public:
        D2UnitDataMissileStrc *getUnitData() {
            if (this->eUnitType != UNIT_MISSILE) return nullptr;
            return this->pUnitData.pUnitDataMissile;
        }
    };

    class D2UnitItem : public D2UnitStrc {
    public:
        D2UnitDataItemStrc *getUnitData() {
            if (this->eUnitType != UNIT_ITEM) return nullptr;
            return this->pUnitData.pUnitDataItem;
        }
    };

    class D2UnitWarp : public D2UnitStrc {
    public:
        D2UnitDataObjectStrc *getUnitData() {
            return nullptr;
        }
    };
}
