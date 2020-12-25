//
// Created by jaenster on 15/11/2020.
//

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "iostream"

namespace WorldEvent {

    bool isSpawned = false;
    bool shouldSpawn = false;
    REMOTEFUNC(
            D2::Types::UnitAny * __stdcall,
            SERVER_SpawnMonsterInternal,
            (D2::Types::Room1 * pRoom, DWORD * unknown, int nX, int nY, int nGUID, BOOL bSetOrNotUnknown),
            0x5a09e0
    )

    [[maybe_unused]] int __stdcall myTest(int classid) {
        if (!isSpawned && shouldSpawn) {
            isSpawned = true;
            return 333;
        }
        return classid;
    }

    __declspec(naked) D2::Types::UnitAny *  intercept2() {
        static ASMPTR continueIf = 0x5a4a5d;
        static ASMPTR skipIf = 0x5a4e65;
        __asm {

            // original code
            MOV        EDI,EAX
            CMP        EBX, 333
            JE itIsDiablo

            // original code
        OriginalCode:
            CMP        EDI, 0   // if (edi == 0) {
            JZ         skip

            JMP continueIf      // (.. in if)

        itIsDiablo:
            MOV EDI, 0
            JMP OriginalCode

        skip:
            JMP skipIf     // else

        }
    }

    __declspec(naked) D2::Types::UnitAny *  intercept() {
        __asm {
            // backup
            push eax

            // first argument
            push ebx
            call myTest
            mov ebx, eax

            // restore
            pop eax
            jmp SERVER_SpawnMonsterInternal
            //jmp back
        }
    }

    REMOTEFUNC(void __fastcall, NET_D2GS_SERVER_Send_0x5A_EventMessages,(int param_1,char *param_2),0x53c850)
    REMOTEFUNC(int  __fastcall, PLAYER_GetClientFromUnitData,(D2::Types::UnitAny *pUnit),0x5531c0);


    REMOTEFUNC(D2::Types::UnitAny * __fastcall,
               SERVER_FindUnitFromTable,
               (D2::Types::IncompleteGameData *pGame,int eD2UnitType,DWORD dwFindGUID),
               0x552f60
               )

   REMOTEFUNC(void __fastcall, D2GS_Client_RawPacketRecv,(char *pBytes,size_t nSize),0x45f7b0)
    D2::Types::UnitAny* __fastcall checkIfSoj(D2::Types::IncompleteGameData *pGame,int eD2UnitType,DWORD dwFindGUID) {
        D2::Types::UnitAny* unit =  SERVER_FindUnitFromTable(pGame, eD2UnitType, dwFindGUID);
        if (unit->dwTxtFileNo == 522 && unit->pItemData->dwQuality == 7 && unit->pItemData->dwFileIndex == 122) {
            int count = Settings["sojCount"] += 1;
            {
                char *aPacket = new char[40];
                aPacket[0] = 0x5A; // right hand skill
                *(BYTE *) &aPacket[1] = 0x11;
                *(BYTE *) &aPacket[2] = 4;
                *(DWORD *) &aPacket[3] = count;

                D2GS_Client_RawPacketRecv(aPacket, 40);
            }

            int random = pGame->seed % 100;

            if (!shouldSpawn && count > random) {
                shouldSpawn = true;
                {
                    char *aPacket = new char[40];
                    aPacket[0] = 0x5A; // right hand skill
                    *(BYTE *) &aPacket[1] = 0x12;
                    *(BYTE *) &aPacket[2] = 4;
                    D2GS_Client_RawPacketRecv(aPacket, 40);
                }
            }

            SaveSettings();
        }
        return unit; // 522
    }

    class : public Feature {
    public:
        void init() {
//            MemoryPatch(0x5a4a4e) << CALL(SERVER_SpawnMonsterInternalOverride);

            // Patching to spawn dia clone
            MemoryPatch(0x5a4a4e) << CALL(intercept);
            MemoryPatch(0x5a4a53) << CALL(intercept2) << NOP_TO(0x5a4a5d);

            MemoryPatch(0x579566) << CALL(checkIfSoj);
        }

        void gameLoop() {

        }
        void oogLoop() {
            isSpawned = false;
        }
    } feature;

}
