#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/ghidra.h"

using namespace Ghidra;

REMOTEFUNC(BOOL __stdcall, ITEM_EditItemData_eItemFlag, (D2UnitStrc* pItem, eD2ItemFlag eItemFlag, BOOL bSet), 0x6280d0);
REMOTEFUNC(void __fastcall, SERVER_PickToCursor, (D2GameStrc* pGame, D2UnitStrc* pPlayer, DWORD nItemGUID, int* param_2_00), 0x55cf50)

ASMPTR NET_D2GS_SERVER_Send_0x9C_ITEMEX_original = 0x53eae0, NET_D2GS_SERVER_Send_0x9C_ItemWorld_rejoin = 0x53eae9;

__declspec(naked) void __fastcall NET_D2GS_SERVER_Send_0x9C_ItemWorld_relocated(D2ClientStrc* pClient, D2UnitStrc* pItem, undefined4 nAction, eD2ItemFlag eItemFlag, uint param_5) {
    __asm {
        PUSH       EBP
        MOV        EBP, ESP
        SUB        ESP, 0x100

        JMP NET_D2GS_SERVER_Send_0x9C_ItemWorld_rejoin
    }
};

void __fastcall NET_D2GS_SERVER_Send_0x9C_ItemWorld_hook(D2ClientStrc* pClient, D2UnitStrc* pItem, undefined4 nAction, eD2ItemFlag eItemFlag, uint param_5) {
    if (nAction == 0 && Settings["itemQOL"]) {// Dropped on ground
        ITEM_EditItemData_eItemFlag(pItem, ITEMFLAG_IDENTIFIED, 1);
    }

    NET_D2GS_SERVER_Send_0x9C_ItemWorld_relocated(pClient, pItem, nAction, eItemFlag, param_5);
};

/*
ASMPTR NET_D2GS_SERVER_Send_0x19_STATCHANGE_CONTROL_GOLD_BYTE_DELTA_original = 0x53e9b0, NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_rejoin = 0x53e9b6;

__declspec(naked) void __fastcall NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_relocated(D2ClientStrc* pClient, uint nGold, uint nNewGold) {
    __asm {
        PUSH       EBP
        MOV        EBP, ESP
        SUB        ESP, 0x8

        JMP NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_rejoin
    }
};

void __fastcall NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_hook(D2ClientStrc* pClient, uint nNewGold, uint nGold) {
    NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_relocated(pClient, nNewGold, nGold);

    return;
};
*/

namespace ItemQoL {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(NET_D2GS_SERVER_Send_0x9C_ITEMEX_original) << JUMP(NET_D2GS_SERVER_Send_0x9C_ItemWorld_hook);
           // MemoryPatch(NET_D2GS_SERVER_Send_0x19_STATCHANGE_CONTROL_GOLD_BYTE_DELTA_original) << JUMP(NET_D2GS_SERVER_Send_0x19_0x1D_0x1E_0x1F_GoldPickup_hook);
        }
    } feature;

}
