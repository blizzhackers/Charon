#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"

using D2::Types::RunesTable;
using D2::Types::ItemRatioTable;
using D2::Types::UniqueItemsTable;
using D2::Types::TreasureClassExTable;
using D2::Types::BINField;

ASMPTR CreateTxtTableArray_Rejoin = 0x6122f9;
ASMPTR CreateTxtTableArray_Original = 0x6122f0;

__declspec(naked) void* __stdcall CreateTxtTableArray_Relocated(void* pMemory, char* szTableName, BINField* pBinFieldInput, int* nTxtTableSize, int nLineLength) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x11c
        jmp CreateTxtTableArray_Rejoin
    }
}

void* __stdcall CreateTxtTableArray_Intercept(void* pMemory, char* szTableName, BINField* pBinFieldInput, int* nTxtTableSize, int nLineLength) {
    void* table;
    table = CreateTxtTableArray_Relocated(pMemory, szTableName, pBinFieldInput, nTxtTableSize, nLineLength);

    if (Settings["ladderItems"] && strcmp(szTableName, "runes") == 0) {
        int tableSize = *nTxtTableSize;
        RunesTable* runes = (RunesTable*)table;
        runes = (RunesTable*)table;

        gamelog << COLOR(2) << "Enabling Ladder Runewords" << std::endl;

        for (int c = 0; c < tableSize; c++) {
            runes[c].Server = 0;
        }
    }
    else if (Settings["rebalanceDrops"] && strcmp(szTableName, "itemratio") == 0) {
        int tableSize = *nTxtTableSize;
        ItemRatioTable* itemratio = (ItemRatioTable*)table;

        gamelog << COLOR(3) << "Tweaking Item Drop Rates for Single Player" << std::endl;

        for (int c = 0; c < tableSize; c++) {
            itemratio[c].unique.divisor = itemratio[c].set.divisor = itemratio[c].rare.divisor = itemratio[c].magic.divisor;
            itemratio[c].unique.min = itemratio[c].set.min = itemratio[c].rare.min = itemratio[c].magic.min;
            itemratio[c].unique.value = itemratio[c].magic.value * 3;
            itemratio[c].set.value = itemratio[c].rare.value = itemratio[c].magic.value * 2;
        }
    }
    else if (Settings["ladderItems"] && strcmp(szTableName, "uniqueitems") == 0) {
        int tableSize = *nTxtTableSize;
        UniqueItemsTable* uniqueitems = (UniqueItemsTable*)table;

        gamelog << COLOR(2) << "Enabling Ladder Unique Items" << std::endl;

        for (int c = 0; c < tableSize; c++) {
            uniqueitems[c].ladder = false;
        }
    }
    else if (Settings["rebalanceDrops"] && strcmp(szTableName, "treasureclassex") == 0) {
        TreasureClassExTable* treasureclassex = (TreasureClassExTable*)table;
        TreasureClassExTable* runes = treasureclassex + 25;

        gamelog << COLOR(3) << "Tweaking Rune Drop Rates for Single Player" << std::endl;

        for (int c = 1; c < 17; c++) {
            runes[c].prob[2] = 2 + ((c * c * c * 798) >> 12);
        }

        // Adjust Countess drop rates.
        strcpy_s(treasureclassex[837].items[0], "Runes 5");
        strcpy_s(treasureclassex[838].items[0], "Runes 11");
        strcpy_s(treasureclassex[839].items[0], "Runes 17");
        treasureclassex[837].nodrop = treasureclassex[838].nodrop = treasureclassex[839].nodrop;
    }

    return table;
}

namespace TxtOverride {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(CreateTxtTableArray_Original) << JUMP(CreateTxtTableArray_Intercept);
        }
    } feature;

}
