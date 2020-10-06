/**
 * Borrowed this from d2bs. Thanks noah!
 */
#pragma once
#ifndef _D2STRUCTS_H
#define _D2STRUCTS_H

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4201)
#pragma optimize("", off)

typedef void(*VOIDFUNC)();

namespace D2 {
    enum class UnitType : DWORD {
        UNIT_PLAYER = 0,
        UNIT_MONSTER = 1,
        UNIT_OBJECT = 2,
        UNIT_MISSILE = 3,
        UNIT_ITEM = 4,
        UNIT_ROOMTILE = 5,
    };

    enum class ItemQuality : DWORD {
        LOW = 1,
        NORMAL = 2,
        SUPERIOR = 3,
        MAGIC = 4,
        SET = 5,
        RARE = 6,
        UNIQUE = 7,
        CRAFTED = 8,
    };

    enum class QuestId : DWORD {
        SPOKE_TO_WARRIV = 0,
        DEN_OF_EVIL = 1,
        SISTERS_BURIAL_GROUNDS = 2,
        TOOLS_OF_THE_TRADE = 3,
        SEARCH_FOR_CAIN = 4,
        FORGOTTEN_TOWER = 5,
        KILL_ANDARIEL = 6,
        ACT_2_UNLOCK = 7,
        SPOKE_TO_JERHYN = 8,
        RADAMANTS_LAIR = 9,
        HORADRIC_STAFF = 10,
        TAINTED_SUN = 11,
        ARCANE_SANCTUARY = 12,
        SUMMONER = 13,
        KILL_DURIEL = 14,
        ACT_3_UNLOCK = 15,
        SPOKE_TO_HRATLI = 16,
        LAM_ESENS_TOME = 17,
        KHALIMS_WILL = 18,
        BLADE_OF_OLD_RELIGION = 19,
        GOLDEN_BIRD = 20,
        BLACKENED_TEMPLE = 21,
        KILL_MEPHISTO = 22,
        ACT_4_UNLOCK = 23,
        SPOKE_TO_TYRAEL = 24,
        FALLEN_ANGEL = 25,
        KILL_DIABLO = 26,
        HELL_FORGE = 27,
        ACT_5_UNLOCK = 28,
        RESPEC = 29,
        UNKNOWN1 = 30,
        UNKNOWN2 = 31,
        UNKNOWN3 = 32,
        UNKNOWN4 = 33,
        UNKNOWN5 = 34,
        SEIGE_ON_HAGGORATH = 35,
        RESCUE_ON_MOUNT_ARREAT = 36,
        PRISON_OF_ICE = 37,
        BETRAYAL_OF_HAGGORATH = 38,
        RITE_OF_PASSAGE = 39,
        KILL_BAAL = 40,
    };

    namespace Types {
        struct UnitAny;
        struct Room1;
        struct Room2;
        struct Level;
        struct Act;
        struct ActMisc;
        struct RosterUnit;
        struct OverheadMsg;
        struct Skill;

        struct SplitText {
            wchar_t* lpwszText;
            SplitText* lpsNext;
        };

        struct InventoryInfo {
            int nLocation;
            int nMaxXCells;
            int nMaxYCells;
        };

        struct GameStructInfo {
            BYTE _1[0x1F];             // 0x00 // was 0x1B
            char szGameName[0x18];     // 0x1F
            char szGameServerIp[0x56]; // 0x37
            char szAccountName[0x30];  // 0x8D
            char szCharName[0x18];     // 0xBD
            char szRealmName[0x18];    // 0xD5
            BYTE _2[0x158];            // 0xED
            char szGamePassword[0x18]; // 0x245
        };

        struct AutomapCell {
            DWORD fSaved;       // 0x00
            WORD nCellNo;       // 0x04
            WORD xPixel;        // 0x06
            WORD yPixel;        // 0x08
            WORD wWeight;       // 0x0A
            AutomapCell* pLess; // 0x0C
            AutomapCell* pMore; // 0x10
        };

        struct GfxCell {
            DWORD flags;    // 0x00
            DWORD width;    // 0x04
            DWORD height;   // 0x08
            DWORD xoffs;    // 0x0C
            DWORD yoffs;    // 0x10
            DWORD _2;       // 0x14
            DWORD lpParent; // 0x18
            DWORD length;   // 0x1C
            BYTE cols;      // 0x20
        };

        struct UnitInteraction {
            DWORD dwMoveType;      // 0x00
            UnitAny* lpPlayerUnit; // 0x04
            UnitAny* lpTargetUnit; // 0x08
            DWORD dwTargetX;       // 0x0C
            DWORD dwTargetY;       // 0x10
            DWORD _1;              // 0x14
            DWORD _2;              // 0x18
            Skill* pSkill;
        };

        struct CellFile {
            DWORD dwVersion; // 0x00
            struct {
                WORD dwFlags;
                BYTE mylastcol;
                BYTE mytabno : 1;
            };                   // 0x04
            DWORD eFormat;       // 0x08
            DWORD termination;   // 0x0C
            DWORD numdirs;       // 0x10
            DWORD numcells;      // 0x14
            GfxCell* cells[255]; // 0x18
        };

        struct CellContext {
            DWORD _1[13];        // 0x00
            CellFile* pCellFile; // 0x34
            DWORD _2[4];         // 0x38
        };

        struct AutomapLayer {
            DWORD nLayerNo;           // 0x00
            DWORD fSaved;             // 0x04
            AutomapCell* pFloors;     // 0x08
            AutomapCell* pWalls;      // 0x0C
            AutomapCell* pObjects;    // 0x10
            AutomapCell* pExtras;     // 0x14
            AutomapLayer* pNextLayer; // 0x18
        };

        struct AutomapLayer2 {
            DWORD _1[2];    // 0x00
            DWORD nLayerNo; // 0x08
        };

        struct LevelTxt {
            DWORD dwLevelNo;           // 0x00
            DWORD _1[60];              // 0x04
            BYTE _2;                   // 0xF4
            char szName[40];           // 0xF5
            char szEntranceText[40];   // 0x11D
            char szLevelDesc[41];      // 0x145
            wchar_t wName[40];         // 0x16E
            wchar_t wEntranceText[40]; // 0x1BE
            BYTE nObjGroup[8];         // 0x196
            BYTE nObjPrb[8];           // 0x19E
        };

        struct ControlText // size = 0x20
        {
            wchar_t* wText[5];  // 0x00 for each field
            DWORD dwColor;      // 0x14
            DWORD dwAlign;      // 0x18
            ControlText* pNext; // 0x1C
        };

        struct Control {
            DWORD dwType;     // 0x00
            DWORD* _1;        // 0x04 // unsure? definitely a ptr but not obvious, usually points to 6 when dwType is 6 I think
            DWORD dwDisabled; // 0x08
            DWORD dwPosX;     // 0x0C
            DWORD dwPosY;     // 0x10
            DWORD dwSizeX;    // 0x14
            DWORD dwSizeY;    // 0x18
            // I think _2 thru _9 are a handler table of some sort
            DWORD* _2;                  // 0x1C // some sort of function (maybe click?)
            DWORD _3;                   // 0x20
            DWORD* _4;                  // 0x24 // some sort of function
            DWORD* _5;                  // 0x28
            DWORD _6;                   // 0x2C
            DWORD* _7;                  // 0x30 // ptr to something...
            DWORD* _8;                  // 0x34 // another random ptr... mostly dead ends when I examined them
            DWORD _9;                   // 0x38
            Control* pNext;             // 0x3C
            DWORD _10;                  // 0x40
            DWORD unkState;             // 0x44 _11 0 when button avail to be clicked 1 when greyed - still need to look at this more
            ControlText* pFirstText;    // 0x48
            ControlText* pLastText;     // 0x4C
            ControlText* pSelectedText; // 0x50
            DWORD dwSelectEnd;          // 0x54
            DWORD dwSelectStart;        // 0x58
            union {
                struct {                // Textboxes
                    wchar_t wText[256]; // 0x5C
                    DWORD dwCursorPos;
                    DWORD dwIsCloaked;
                };
                struct {                 // Buttons
                    DWORD _12[2];        // 0x5C
                    wchar_t wText2[256]; // 0x6C
                };
            };
        };

#pragma pack(push)
#pragma pack(1)

        struct BnetData {
            DWORD dwId;                 // 0x00
            DWORD dwId2;                // 0x04
            BYTE _1[0x10];              // 0x08
            DWORD dwId3;                // 0x18
            WORD Unk3;                  // 0x1C
            BYTE _2;                    // 0x1E
            char szGameName[0x16];      // 0x1F
            WORD _3;                    // 0x35
            char szGameIP[0x10];        // 0x37
            BYTE _5[0x42];              // 0x47
            DWORD dwId4;                // 0x89
            char szAccountName[0x30];   // 0x8D
            char szPlayerName[0x18];    // 0xBD
            char szRealmName[0x08];     // 0xD5
            BYTE _8[0x111];             // 0xDD
            BYTE nCharClass;            // 0x1EE
            BYTE nCharFlags;            // 0x1EF
            BYTE nMaxDiff;              // 0x1F0
            BYTE _9[0x1F];              // 0x1F1
            BYTE CreatedGameDifficulty; // 0x210
            void* _10;                  // 0x211
            BYTE _11[0x15];             // 0x215
            WORD _12;                   // 0x22A
            BYTE _13;                   // 0x22C
            char szRealmName2[0x18];    // 0x22D
            char szGamePass[0x18];      // 0x245
            char szGameDesc[0x104];     // 0x25D
            char channelname[0x20];     //+0x35f
            BYTE _14[0x40];             //+0x37f
            BYTE charlevel;             //+0x3bf
            BYTE ladderflag;            //+0x3c0
            DWORD passhash;             //+0x3c1
            BYTE passlength;            //+0x3c5
        };

        struct RoomTile {
            Room2* pRoom2;   // 0x00
            RoomTile* pNext; // 0x04
            DWORD _2[2];     // 0x08
            DWORD* nNum;     // 0x10
        };

        struct RosterUnit {
            char szName[16];    // 0x00
            DWORD dwUnitId;     // 0x10
            DWORD dwPartyLife;  // 0x14
            DWORD _1;           // 0x18
            DWORD dwClassId;    // 0x1C
            WORD wLevel;        // 0x20
            WORD wPartyId;      // 0x22
            DWORD dwLevelId;    // 0x24
            DWORD Xpos;         // 0x28
            DWORD Ypos;         // 0x2C
            DWORD dwPartyFlags; // 0x30
            BYTE* _5;           // 0x34
            DWORD _6[11];       // 0x38
            WORD _7;            // 0x64
            char szName2[16];   // 0x66
            WORD _8;            // 0x76
            DWORD _9[2];        // 0x78
            RosterUnit* pNext;  // 0x80
        };

        struct QuestInfo {
            void* pBuffer; // 0x00
            DWORD _1;      // 0x04
        };

        struct Waypoint {
            BYTE flags; // 0x00
        };

        struct PlayerData {
            char szName[0x10];            // 0x00
            QuestInfo* pNormalQuest;      // 0x10
            QuestInfo* pNightmareQuest;   // 0x14
            QuestInfo* pHellQuest;        // 0x18
            Waypoint* pNormalWaypoint;    // 0x1c
            Waypoint* pNightmareWaypoint; // 0x20
            Waypoint* pHellWaypoint;      // 0x24
        };

        struct CollMap {
            DWORD dwPosGameX;  // 0x00
            DWORD dwPosGameY;  // 0x04
            DWORD dwSizeGameX; // 0x08
            DWORD dwSizeGameY; // 0x0C
            DWORD dwPosRoomX;  // 0x10
            DWORD dwPosRoomY;  // 0x14
            DWORD dwSizeRoomX; // 0x18
            DWORD dwSizeRoomY; // 0x1C
            WORD* pMapStart;   // 0x20 - Array of collision flags [localx + localy * dwSizeGameX]
            /* Collision Flags

                Binary             Bit Hex    Description
                0b0000000000000001   0 0x0001 Block Walk
                0b0000000000000010   1 0x0002 Block Sight
                0b0000000000000100   2 0x0004 Block Missiles
                0b0000000000001000   3 0x0008 Block Player (but not merc)
                0b0000000000010000   4 0x0010 Alternate Floor (doors, entry/exit, some waypoints)
                0b0000000000100000   5 0x0020 Blank (returned if the subtile is invalid)
                0b0000000001000000   6 0x0040 Unknown
                0b0000000010000000   7 0x0080 Players
                0b0000000100000000   8 0x0100 NPCs & Monsters
                0b0000001000000000   9 0x0200 Items
                0b0000010000000000  10 0x0400 Objects (probably blocking)
                0b0000100000000000  11 0x0800 Closed door
                0b0001000000000000  12 0x1000 Set for units sometimes, but not always
                0b0010000000000000  13 0x2000 Friendly NPCs
                0b0100000000000000  14 0x4000 Unknown
                0b1000000000000000  15 0x8000 Corpses (also used by portals, but dead monsters are mask 0x8000)
            */
            WORD* pMapEnd;     // 0x22
            WORD getCollision(DWORD localx, DWORD localy, WORD mask);
        };

        struct PresetUnit {
            DWORD _1;                // 0x00
            DWORD dwTxtFileNo;       // 0x04
            DWORD dwPosX;            // 0x08
            PresetUnit* pPresetNext; // 0x0C
            DWORD _3;                // 0x10
            DWORD dwType;            // 0x14
            DWORD dwPosY;            // 0x18
        };

        struct Level {
            DWORD _1[4];        // 0x00
            Room2* pRoom2First; // 0x10
            DWORD _2[2];        // 0x14
            DWORD dwPosX;       // 0x1C
            DWORD dwPosY;       // 0x20
            DWORD dwSizeX;      // 0x24
            DWORD dwSizeY;      // 0x28
            DWORD _3[96];       // 0x2C
            Level* pNextLevel;  // 0x1AC
            DWORD _4;           // 0x1B0
            ActMisc* pMisc;     // 0x1B4
            DWORD _5[6];        // 0x1BC
            DWORD dwLevelNo;    // 0x1D0
            DWORD _6[3];        // 0x1D4
            union {
                DWORD RoomCenterX[9];
                DWORD WarpX[9];
            }; // 0x1E0
            union {
                DWORD RoomCenterY[9];
                DWORD WarpY[9];
            };                   // 0x204
            DWORD dwRoomEntries; // 0x228
        };

        struct Room2 {
            DWORD _1[2];        // 0x00
            Room2** pRoom2Near; // 0x08
            DWORD _2[5];        // 0x0C
            struct {
                DWORD dwRoomNumber;  // 0x00
                DWORD _1;            // 0x04
                DWORD* pdwSubNumber; // 0x08
            } *pType2Info;          // 0x20
            Room2* pRoom2Next;       // 0x24
            DWORD dwRoomFlags;       // 0x28
            DWORD dwRoomsNear;       // 0x2C
            Room1* pRoom1;           // 0x30
            DWORD dwPosX;            // 0x34
            DWORD dwPosY;            // 0x38
            DWORD dwSizeX;           // 0x3C
            DWORD dwSizeY;           // 0x40
            DWORD _3;                // 0x44
            DWORD dwPresetType;      // 0x48
            RoomTile* pRoomTiles;    // 0x4C
            DWORD _4[2];             // 0x50
            Level* pLevel;           // 0x58
            PresetUnit* pPreset;     // 0x5C
            WORD getCollision(DWORD localx, DWORD localy, WORD mask);
            DWORD getWorldX();
            DWORD getWorldY();
            DWORD getWorldWidth();
            DWORD getWorldHeight();
        };

#pragma pack(pop)

        struct Room1 {
            Room1** pRoomsNear;  // 0x00
            DWORD _1[3];         // 0x04
            Room2* pRoom2;       // 0x10
            DWORD _2[3];         // 0x14
            CollMap* Coll;       // 0x20
            DWORD dwRoomsNear;   // 0x24
            DWORD _3[9];         // 0x28
            DWORD dwXStart;      // 0x4C
            DWORD dwYStart;      // 0x50
            DWORD dwXSize;       // 0x54
            DWORD dwYSize;       // 0x58
            DWORD _4[6];         // 0x5C
            UnitAny* pUnitFirst; // 0x74
            DWORD _5;            // 0x78
            Room1* pRoomNext;    // 0x7C
            WORD getCollision(DWORD localx, DWORD localy, WORD mask);
        };

        struct ActMisc {
            DWORD _1[37];           // 0x00
            DWORD dwStaffTombLevel; // 0x94
            DWORD _2[245];          // 0x98
            Act* pAct;              // 0x46C
            DWORD _3[3];            // 0x470
            Level* pLevelFirst;     // 0x47C
        };

        struct Act {
            DWORD _1[3];     // 0x00
            DWORD dwMapSeed; // 0x0C
            Room1* pRoom1;   // 0x10
            DWORD dwAct;     // 0x14
            DWORD _2[12];    // 0x18
            ActMisc* pMisc;  // 0x48
        };

        struct Path {
            WORD xOffset;         // 0x00
            WORD xPos;            // 0x02
            WORD yOffset;         // 0x04
            WORD yPos;            // 0x06
            DWORD _1[2];          // 0x08
            WORD xTarget;         // 0x10
            WORD yTarget;         // 0x12
            DWORD _2[2];          // 0x14
            Room1* pRoom1;        // 0x1C
            Room1* pRoomUnk;      // 0x20
            DWORD _3[3];          // 0x24
            UnitAny* pUnit;       // 0x30
            DWORD dwFlags;        // 0x34
            DWORD _4;             // 0x38
            DWORD dwPathType;     // 0x3C
            DWORD dwPrevPathType; // 0x40
            DWORD dwUnitSize;     // 0x44
            DWORD _5[4];          // 0x48
            UnitAny* pTargetUnit; // 0x58
            DWORD dwTargetType;   // 0x5C
            DWORD dwTargetId;     // 0x60
            BYTE bDirection;      // 0x64
        };

        struct ItemPath {
            DWORD _1[3];  // 0x00
            DWORD dwPosX; // 0x0C
            DWORD dwPosY; // 0x10
            // Use Path for the rest
        };

        struct Stat {
            WORD wSubIndex;    // 0x00
            WORD wStatIndex;   // 0x02
            DWORD dwStatValue; // 0x04
        };

        struct StatVector {
            Stat* pStats;
            WORD wCount;
            WORD wSize;
        };

        // Credits to SVR, http://phrozenkeep.hugelaser.com/forum/viewtopic.php?f=8&t=31458&p=224066
        struct StatList {
            DWORD _1;              // 0x00
            UnitAny* pUnit;        // 0x04
            DWORD dwUnitType;      // 0x08
            DWORD dwUnitId;        // 0x0C
            DWORD dwFlags;         // 0x10
            DWORD _2[4];           // 0x14
            StatVector StatVec;    // 0x24
            StatList* pPrevLink;   // 0x2C
            DWORD _3;              // 0x30
            StatList* pPrev;       // 0x34
            DWORD _4;              // 0x38
            StatList* pNext;       // 0x3C
            StatList* pSetList;    // 0x40
            DWORD _5;              // 0x44
            StatVector SetStatVec; // 0x48
            DWORD _6[2];           // 0x50
            DWORD StateBits[6];    // 0x58
        };

        struct Inventory {
            DWORD dwSignature;    // 0x00
            BYTE* bGame1C;        // 0x04
            UnitAny* pOwner;      // 0x08
            UnitAny* pFirstItem;  // 0x0C
            UnitAny* pLastItem;   // 0x10
            DWORD _1[2];          // 0x14
            DWORD dwLeftItemUid;  // 0x1C
            UnitAny* pCursorItem; // 0x20
            DWORD dwOwnerId;      // 0x24
            DWORD dwItemCount;    // 0x28
        };

        struct Light {
            DWORD _1[3];         // 0x00
            DWORD dwType;        // 0x0C
            DWORD _2[7];         // 0x10
            DWORD dwStaticValid; // 0x2C
            int* pnStaticMap;    // 0x30
        };

        struct SkillInfo {
            WORD wSkillId; // 0x00
        };

        struct Skill {
            SkillInfo* pSkillInfo; // 0x00
            Skill* pNextSkill;     // 0x04
            DWORD _1[8];           // 0x08
            DWORD dwSkillLevel;    // 0x28
            DWORD _2[2];           // 0x2C
            DWORD ItemId;          // 0x34 0xFFFFFFFF if not a charge
            DWORD ChargesLeft;     // 0x38
            DWORD IsCharge;        // 0x3C 1 for charge, else 0
        };                         // size = 0x40

        struct Info {
            BYTE* pGame1C;      // 0x00
            Skill* pFirstSkill; // 0x04
            Skill* pLeftSkill;  // 0x08
            Skill* pRightSkill; // 0x0C
        };

        struct ItemData {
            DWORD dwQuality;            // 0x00
            DWORD dwSeed[2];            // 0x04
            DWORD dwItemFlags;          // 0x0C 1 = Owned by player, 0xFFFFFFFF = Not owned
            DWORD dwFingerPrint;        // 0x10 Initial seed
            DWORD _1;                   // 0x14 CommandFlags?
            DWORD dwFlags;              // 0x18
            DWORD _2[2];                // 0x1C
            DWORD dwActionStamp;        // 0x24 Changes when an item is changed
            DWORD dwFileIndex;          // 0x28 Data file index UniqueItems.txt etc.
            DWORD dwItemLevel;          // 0x2C
            WORD wItemFormat;           // 0x30
            WORD wRarePrefix;           // 0x32
            WORD wRareSuffix;           // 0x34
            WORD wAutoPrefix;           // 0x36
            WORD wMagicPrefix[3];       // 0x38
            WORD wMagicSuffix[3];       // 0x3E
            BYTE BodyLocation;          // 0x44 Not always cleared
            BYTE ItemLocation;          // 0x45 Non-body/belt location (Body/Belt == 0xFF)
            WORD _4;                    // 0x46
            BYTE bEarLevel;             // 0x48
            BYTE bInvGfxIdx;            // 0x49
            char szPlayerName[16];      // 0x4A Personalized / Ear name
            Inventory* pOwnerInventory; // 0x5C Socketed Items owner Inv
            DWORD _10;                  // 0x60
            UnitAny* pNextInvItem;      // 0x64 Next item in socketed item if OwnerInventory is set
            BYTE GameLocation;          // 0x68 Location per docs.d2bs.org (unit.location)
            BYTE NodePage;              // 0x69 Actual location, this is the most reliable by far
            WORD _12;                   // 0x6A
            WORD _13[12];               // 0x6C
            UnitAny* pOwner;            // 0x84
        };

        struct ItemTxt {
            char szFlippyFile[32];
            char szInvFile[32];
            char szUniqueInvFile[32];
            char szSetInvFile[32];
            char szCode[4];
            int dwNormCode;
            int dwUberCode;
            int dwUltraCode;
            int dwAlternateGfx;
            int dwPSpell;
            short wState;
            short wCState1;
            short wCState2;
            short wStat1;
            short wStat2;
            short wStat3;
            int dwCalc1;
            int dwCalc2;
            int dwCalc3;
            int dwLen;
            short wSpellDesc;
            short wSpellDescStr;
            int dwSpellDescCalc;
            int dwBetterGem;
            int dwWClass;
            int dw2HandedWClass;
            int dwTMogType;
            int dwMinAc;
            int dwMaxAc;
            int dwGambleCost;
            int dwSpeed;
            int dwBitfield1;
            int dwCost;
            int dwMinStack;
            int dwMaxStack;
            int dwSpawnStack;
            int dwGemOffset;
            short wNameStr;
            short wVersion;
            short wAutoPrefix;
            short wMissileType;
            char bRarity;
            char bLevel;
            char bMinDam;
            char bMaxDam;
            char bMinMisDam;
            char bMaxMisDam;
            char b2HandMinDam;
            char b2HandMaxDam;
            short bRangeAdder;
            short wStrBonus;
            short wDexBonus;
            short wReqStr;
            short wReqDex;
            char bAbsorbs;
            char bInvWidth;
            char bInvHeight;
            char bBlock;
            char bDurability;
            char bNoDurability;
            char bMissile;
            char bComponent;
            char bRArm;
            char bLArm;
            char bTorso;
            char bLegs;
            char bRSPad;
            char bLSPad;
            char b2Handed;
            char bUseable;
            union {
                short wType;
                short nType;
            };
            short wType2;
            short bSubType;
            short wDropSound;
            short wUseSound;
            char bDropSfxFrame;
            char bUnique;
            char bQuest;
            char bQuestDiffCheck;
            char bTransparent;
            char bTransTbl;
            char pad0x12E;
            char bLightRadius;
            char bBelt;
            char bAutoBelt;
            char bStackable;
            char bSpawnable;
            char bSpellIcon;
            char bDurWarning;
            char bQntWarning;
            char bHasInv;
            char bGemSockets;
            char bTransmogrify;
            char bTMogMin;
            char bTMogMax;
            char bHitClass;
            char b1or2Handed;
            char bGemApplyType;
            char bLevelReq;
            char bMagicLvl;
            char bTransform;
            char bInvTrans;
            char bCompactSave;
            char bSkipName;
            char bNameable;
            char bAkaraMin;
            char bGheedMin;
            char bCharsiMin;
            char bFaraMin;
            char bLysanderMin;
            char bDrognanMin;
            char bHraltiMin;
            char bAlkorMin;
            char bOrmusMin;
            char bElzixMin;
            char bAshearaMin;
            char bCainMin;
            char bHalbuMin;
            char bJamellaMin;
            char bMalahMin;
            char bLarzukMin;
            char bDrehyaMin;
            char bAkaraMax;
            char bGheedMax;
            char bCharsiMax;
            char bFaraMax;
            char bLysanderMax;
            char bDrognanMax;
            char bHraltiMax;
            char bAlkorMax;
            char bOrmusMax;
            char bElzixMax;
            char bAshearaMax;
            char bCainMax;
            char bHalbuMax;
            char bJamellaMax;
            char bMalahMax;
            char bLarzukMax;
            char bDrehyaMax;
            char bAkaraMagicMin;
            char bGheedMagicMin;
            char bCharsiMagicMin;
            char bFaraMagicMin;
            char bLysanderMagicMin;
            char bDrognanMagicMin;
            char bHraltiMagicMin;
            char bAlkorMagicMin;
            char bOrmusMagicMin;
            char bElzixMagicMin;
            char bAshearaMagicMin;
            char bCainMagicMin;
            char bHalbuMagicMin;
            char bJamellaMagicMin;
            char bMalahMagicMin;
            char bLarzukMagicMin;
            char bDrehyaMagicMin;
            char bAkaraMagicMax;
            char bGheedMagicMax;
            char bCharsiMagicMax;
            char bFaraMagicMax;
            char bLysanderMagicMax;
            char bDrognanMagicMax;
            char bHraltiMagicMax;
            char bAlkorMagicMax;
            char bOrmusMagicMax;
            char bElzixMagicMax;
            char bAshearaMagicMax;
            char bCainMagicMax;
            char bHalbuMagicMax;
            char bJamellaMagicMax;
            char bMalahMagicMax;
            char bLarzukMagicMax;
            char bDrehyaMagicMax;
            char bAkaraMagicLvl;
            char bGheedMagicLvl;
            char bCharsiMagicLvl;
            char bFaraMagicLvl;
            char bLysanderMagicLvl;
            char bDrognanMagicLvl;
            char bHraltiMagicLvl;
            char bAlkorMagicLvl;
            char bOrmusMagicLvl;
            char bElzixMagicLvl;
            char bAshearaMagicLvl;
            char bCainMagicLvl;
            char bHalbuMagicLvl;
            char bJamellaMagicLvl;
            char bMalahMagicLvl;
            char bLarzukMagicLvl;
            char bDrehyaMagicLvl;
            char pad0x19B;
            int dwNightmareUpgrade;
            int dwHellUpgrade;
            char bPermStoreItem;
            char bMultibuy;
            BYTE field_0x1a6;
            BYTE field_0x1a7;
        };

        struct MonsterTxt {
            BYTE _1[0x6];      // 0x00
            WORD nLocaleTxtNo; // 0x06
            WORD flag;         // 0x08
            WORD _1a;          // 0x0A
            union {
                DWORD flag1; // 0x0C
                struct {
                    BYTE flag1a;    // 0x0C
                    BYTE flag1b;    // 0x0D
                    BYTE flag1c[2]; // 0x0E
                };
            };
            BYTE _2[0x22];              // 0x10
            WORD velocity;              // 0x32
            BYTE _2a[0x52];             // 0x34
            WORD tcs[3][4];             // 0x86
            BYTE _2b[0x52];             // 0x9E
            wchar_t szDescriptor[0x3c]; // 0xF0
            BYTE _3[0x1a0];             // 0x12C
        };

        struct MonsterData {
            BYTE _1[22]; // 0x00
            struct {
                BYTE fUnk : 1;
                BYTE fNormal : 1;
                BYTE fChamp : 1;
                BYTE fBoss : 1;
                BYTE fMinion : 1;
            }; // 0x16
            BYTE _2[5];
            BYTE anEnchants[9]; // 0x1C
            WORD wUniqueNo;     // 0x26
            DWORD _5;           // 0x28
            struct {
                wchar_t wName[28];
            }; // 0x2C
        };

        struct ObjectTxt {
            char szName[0x40];     // 0x00
            wchar_t wszName[0x40]; // 0x40
            BYTE _1[4];            // 0xC0
            BYTE nSelectable0;     // 0xC4
            BYTE _2[0x87];         // 0xC5
            BYTE nOrientation;     // 0x14C
            BYTE _2b[0x19];        // 0x14D
            BYTE nSubClass;        // 0x166
            BYTE _3[0x11];         // 0x167
            BYTE nParm0;           // 0x178
            BYTE _4[0x39];         // 0x179
            BYTE nPopulateFn;      // 0x1B2
            BYTE nOperateFn;       // 0x1B3
            BYTE _5[8];            // 0x1B4
            DWORD nAutoMap;        // 0x1BB
        };

        struct ObjectData {
            ObjectTxt* pTxt; // 0x00
            union {
                BYTE Type; // 0x04 (0x0F would be a Exp Shrine)
                struct {
                    BYTE _1 : 7;
                    BYTE ChestLocked : 1;
                };
            };
            DWORD _2[8];        // 0x08
            char szOwner[0x10]; // 0x28
        };

        struct ObjectPath {
            Room1* pRoom1; // 0x00
            DWORD _1[2];   // 0x04
            DWORD dwPosX;  // 0x0C
            DWORD dwPosY;  // 0x10
            // Leaving rest undefined, use Path
        };

        struct UnitAny {
            DWORD dwType;      // 0x00
            DWORD dwTxtFileNo; // 0x04
            DWORD _1;          // 0x08
            DWORD dwUnitId;    // 0x0C
            DWORD dwMode;      // 0x10
            union {
                PlayerData* pPlayerData;
                ItemData* pItemData;
                MonsterData* pMonsterData;
                ObjectData* pObjectData;
                // TileData *pTileData doesn't appear to exist anymore
            };               // 0x14
            DWORD dwAct;     // 0x18
            Act* pAct;       // 0x1C
            DWORD dwLoSeed;  // 0x20
            DWORD dwHiSeed;  // 0x24
            DWORD dwInitSeed;         // 0x28
            union {
                Path* pPath;
                ItemPath* pItemPath;
                ObjectPath* pObjectPath;
            };                        // 0x2C
            BYTE* pAnimSeq;           // 0x30
            DWORD dwSeqFrameCount;    // 0x34
            DWORD dwSeqFrame;         // 0x38
            DWORD dwAnimSpeed;        // 0x3C
            DWORD dwSeqMode;          // 0x40
            DWORD dwGfxFrame;         // 0x44
            DWORD dwFrameRemain;      // 0x48
            WORD wFrameRate;          // 0x4C
            WORD wActionFrame;        // 0x4E
            BYTE* pGfxUnk;            // 0x50
            DWORD* pGfxInfo;          // 0x54
            DWORD* pGfxInfoCopy;      // 0x58
            StatList* pStats;         // 0x5C
            Inventory* pInventory;    // 0x60
            Light* ptLight;           // 0x64
            DWORD dwStartLightRadius; // 0x68
            WORD nPl2ShiftIdx;        // 0x6C
            WORD nUpdateType;         // 0x6E
            UnitAny* pUpdateUnit;     // 0x70 - Used when updating unit.
            DWORD* pQuestRecord;      // 0x74
            DWORD bSparklyChest;      // 0x78 bool
            DWORD* pTimerArgs;        // 0x7C
            DWORD dwSoundSync;        // 0x80
            DWORD _6[2];              // 0x84
            WORD wX;                  // 0x8C
            WORD wY;                  // 0x8E
            DWORD _7;                 // 0x90
            DWORD dwOwnerType;        // 0x94
            DWORD dwOwnerId;          // 0x98
            DWORD _8[2];              // 0x9C
            OverheadMsg* pOMsg;       // 0xA4
            Info* pInfo;              // 0xA8
            void *pCombat;            // 0xAC
            DWORD dwLastHitClass;     // 0xB0
            DWORD unk0xB4;            // 0xB4
            union {
                char sDropItemCode[4];
                DWORD dwDropItemCode;
            };                        // 0xB8
            DWORD unk0xBC;            // 0xBC
            DWORD unk0xC0;            // 0xC0
            DWORD dwFlags;            // 0xC4
            DWORD dwFlags2;           // 0xC8
            DWORD _10[5];             // 0xCC
            UnitAny* pChangedNext;    // 0xE0
            UnitAny* pListNext;       // 0xE4
            UnitAny* pRoomNext;       // 0xE8
            void* pMsgFirst;          // 0xEC
            void* pMsgLast;           // 0xF0
        };

        struct UnitHashTable {
            UnitAny* table[128];
        };

        struct WardenClientRegion_t {
            DWORD cbAllocSize;             //+00
            DWORD offsetFunc1;             //+04
            DWORD offsetRelocAddressTable; //+08
            DWORD nRelocCount;             //+0c
            DWORD offsetWardenSetup;       //+10
            DWORD _2[2];
            DWORD offsetImportAddressTable; //+1c
            DWORD nImportDllCount;          //+20
            DWORD nSectionCount;            //+24
        };

        struct SMemBlock_t {
            DWORD _1[6];
            DWORD cbSize; //+18
            DWORD _2[31];
            BYTE data[1]; //+98
        };

        struct WardenClient_t {
            WardenClientRegion_t* pWardenRegion; //+00
            DWORD cbSize;                        //+04
            DWORD nModuleCount;                  //+08
            DWORD param;                         //+0c
            DWORD fnSetupWarden;                 //+10
        };

        struct WardenIATInfo_t {
            DWORD offsetModuleName;
            DWORD offsetImportTable;
        };

#pragma pack(push)
#pragma pack(1)

        struct NPCMenu {
            DWORD dwNPCClassId;
            DWORD dwEntryAmount;
            WORD wEntryId1;
            WORD wEntryId2;
            WORD wEntryId3;
            WORD wEntryId4;
            WORD _1;
            DWORD dwEntryFunc1;
            DWORD dwEntryFunc2;
            DWORD dwEntryFunc3;
            DWORD dwEntryFunc4;
            BYTE _2[5];
        };

        struct OverheadMsg {
            DWORD _1;
            DWORD dwTrigger;
            DWORD _2[2];
            char Msg[232];
        };

#pragma pack(pop)

        struct D2MSG {
            HWND myHWND;
            char lpBuf[256];
        };

        struct InventoryLayout {
            BYTE SlotWidth;
            BYTE SlotHeight;
            BYTE unk1;
            BYTE unk2;
            DWORD Left;
            DWORD Right;
            DWORD Top;
            DWORD Bottom;
            BYTE SlotPixelWidth;
            BYTE SlotPixelHeight;
        };

        struct MpqTable;

        struct sgptDataTable {
            MpqTable* pPlayerClass;
            DWORD dwPlayerClassRecords;
            MpqTable* pBodyLocs;
            DWORD dwBodyLocsRecords;
            MpqTable* pStorePage;
            DWORD dwStorePageRecords;
            MpqTable* pElemTypes;
        };

        struct MessageHandlerList {
            DWORD message;
            DWORD unk_4;
            DWORD(__stdcall* handler)(void*);
            struct MessageHandlerList* next;
        };

        struct MessageHandlerHashTable {
            struct MessageHandlerList** table;
            DWORD length;
        };

        struct WindowHandlerHashTable {
            struct WindowHandlerList** table;
            DWORD length;
        };

        struct WindowHandlerList {
            DWORD unk_0;
            HWND hWnd;
            DWORD unk_8;
            struct MessageHandlerHashTable* msgHandlers;
            struct WindowHandlerList* next;
        };

        // Not sure of the location of handler and this struct inside Info.
        // Could be this struct is later and handler is earlier, but this is the safest
        // for now.
        struct TransactionDialogsLine_t {
            wchar_t text[120];          // 0x000
            DWORD unk[6];               // 0x0F0
            void(__stdcall* handler)(); // 0x108
            DWORD bMaybeSelectable;     // 0x10C
        };

        struct TransactionDialogsInfo_t {
            DWORD unk[0x14];                          // 0x000
            DWORD numLines;                           // 0x050
            DWORD unk_2[0x5];                         // 0x054
            TransactionDialogsLine_t dialogLines[10]; // 0x068
            void* something;                          // 0xB08
        };

        // This is incomplete! Not the correct length! Use only for pointers!
        struct IniConfigStrc {
            char bD2ExpMpqExistsOrLanugageCode[4];
            BOOL bCRASHAPP;
            bool bWINDOW;
            bool bASPECT;
            bool b3DFX;
            bool bOPENGL;
            bool bRAVE;
            bool bD3D;
            bool bPERSPECTIVE;
            bool bQUALITY;
            int nGAMMA;
            bool bVSYNC;
        };

        struct RunesTable { // Taken from PlugY v1.14
            char	Name[0x40];		//+00
            char	RuneName[0x40];	//+40
            BYTE	Complete;		//+80
            BYTE	Server;			//+81
            WORD	RuneNameID;		//+82
            WORD	zero;			//+84 (=0x0000)
            WORD	Itypes[6];		//+86
            WORD	Etypes[3];		//+92
            int		Runes[6];		//+98
            DWORD	t1code1;		//+B0
            DWORD	t1param1;		//+B4
            DWORD	t1min1;			//+B8
            DWORD	t1max1;			//+BC
            DWORD	t1code2;		//+C0
            DWORD	t1param2;		//+C4
            DWORD	t1min2;			//+C8
            DWORD	t1max2;			//+CC
            DWORD	t1code3;		//+D0
            DWORD	t1param3;		//+D4
            DWORD	t1min3;			//+D8
            DWORD	t1max3;			//+DC
            DWORD	t1code4;		//+E0
            DWORD	t1param4;		//+E4
            DWORD	t1min4;			//+E8
            DWORD	t1max4;			//+EC
            DWORD	t1code5;		//+F0
            DWORD	t1param5;		//+F4
            DWORD	t1min5;			//+F8
            DWORD	t1max5;			//+FC
            DWORD	t1code6;		//+100
            DWORD	t1param6;		//+104
            DWORD	t1min6;			//+108
            DWORD	t1max6;			//+10C
            DWORD	t1code7;		//+110
            DWORD	t1param7;		//+114
            DWORD	t1min7;			//+118
            DWORD	t1max7;			//+11C
        };

        struct ItemRatioTable {
            struct {
                DWORD value, divisor, min;
            } unique, rare, set, magic;
            struct {
                DWORD value, divisor;
            } hiQuality, normal;
            WORD version;
            BYTE uber, classSpecific;
        };

        struct UniqueItemsTable
        {
            WORD index;
            BYTE name[0x20];
            WORD namestr;
            WORD version;
            BYTE uk2[0x6];
            union {
                BYTE	flag;
                struct { // bit fields I guess?
                    bool enabled : 1;
                    bool nolimit : 1;
                    bool carry1 : 1;
                    bool ladder : 1;
                };
            };
            BYTE uk3[0x11F];
        };

        struct TreasureClassExTable {
            char treasureClass[32];
            long picks;
            short group;
            short level;
            short magic;
            short rare;
            short set;
            short unique;
            short unk1[2];
            short nodrop; // this might be a long, but nothing uses the upper byte
            short unk2;
            char items[10][64];
            long prob[10];
        };

        struct AIParams {
            void* pMonsterAiGeneral;
            void* fpAiFunction;
            UnitAny* pTarget;
            int field_0xc;
            int field_0x10;
            int nDistanceToTarget;
            BOOL bEngagedInCombat;
            MonsterTxt* pMonStats;
            void* pMonStats2;
        };

        enum class BINFieldType : int {
            FIELDTYPE_END = 0,
            FIELDTYPE_DATA_ASCII = 1,
            FIELDTYPE_DATA_DWORD = 2,
            FIELDTYPE_DATA_WORD = 3,
            FIELDTYPE_DATA_BYTE = 4,
            FIELDTYPE_UNKNOWN_5 = 5,
            FIELDTYPE_DATA_BYTE_2 = 6,
            FIELDTYPE_DATA_DWORD_2 = 8,
            FIELDTYPE_DATA_RAW = 9,
            FIELDTYPE_ASCII_TO_CODE = 10,
            FIELDTYPE_UNKNOWN_11 = 11,
            FIELDTYPE_UNKNOWN_12 = 12,
            FIELDTYPE_CODE_TO_BYTE = 13,
            FIELDTYPE_UNKNOWN_14 = 14,
            FIELDTYPE_CODE_TO_WORD = 15,
            FIELDTYPE_UNKNOWN_16 = 16,
            FIELDTYPE_NAME_TO_INDEX = 17,
            FIELDTYPE_NAME_TO_INDEX_2 = 18,
            FIELDTYPE_NAME_TO_DWORD = 19,
            FIELDTYPE_NAME_TO_WORD = 20,
            FIELDTYPE_NAME_TO_WORD_2 = 21,
            FIELDTYPE_KEY_TO_WORD = 22,
            FIELDTYPE_MONSTER_COMPS = 23,
            FIELDTYPE_UNKNOWN_24 = 24,
            FIELDTYPE_CALC_TO_DWORD = 25,
            FIELDTYPE_DATA_BIT = 26,
        };

        struct BINField {
            char* fieldName;
            BINFieldType type;
            DWORD	strLength;
            DWORD	offset;
            void* lookup;
        };

        // Exported from Ghidra
        struct RendererFunctionsStrc {
            VOIDFUNC field_0x0;
            VOIDFUNC fpAllocCoreStrc;
            VOIDFUNC fpRelease;
            VOIDFUNC field_0xc;
            VOIDFUNC field_0x10;
            VOIDFUNC field_0x14;
            VOIDFUNC fpBeginScene;
            VOIDFUNC fpEndScene;
            VOIDFUNC fpFlipBufferProbably;
            VOIDFUNC fpResizeWindow;
            VOIDFUNC fpGetBackBuffer;
            VOIDFUNC fpActivateWindow;
            VOIDFUNC fpSetOption;
            VOIDFUNC fpBeginCutScene;
            VOIDFUNC fpPlayCutScene;
            VOIDFUNC fpCheckCutScene;
            VOIDFUNC fpDecodeSmacker;
            VOIDFUNC fpPlayerSmacker;
            VOIDFUNC fpCloseSmacker;
            VOIDFUNC fpGetRenderStatistics;
            VOIDFUNC fpGetScreenSize;
            VOIDFUNC fpUpdateScaleFactorScaleFactor;
            VOIDFUNC fpSetGamma;
            VOIDFUNC fpCheckGamma;
            VOIDFUNC fpSetPerspectiveScale;
            VOIDFUNC fpAdjustPerspectivePosition;
            VOIDFUNC fpPerspecitveScalePosition;
            VOIDFUNC fpSetDefaultPerspectiveFactor;
            VOIDFUNC fpSetPalette;
            VOIDFUNC fpSetPaletteTable;
            VOIDFUNC fpSetGlobalLight;
            VOIDFUNC field_0x7c;
            VOIDFUNC field_0x80;
            VOIDFUNC fpDrawSprite;
            VOIDFUNC field_0x88;
            VOIDFUNC field_0x8c;
            VOIDFUNC field_0x90;
            VOIDFUNC field_0x94;
            VOIDFUNC field_0x98;
            VOIDFUNC field_0x9c;
            VOIDFUNC field_0xa0;
            VOIDFUNC field_0xa4;
            VOIDFUNC fpDrawRect;
            VOIDFUNC fpDrawRectEx;
            VOIDFUNC fpDrawSolidRect;
            VOIDFUNC fpDrawSolidSquare;
            VOIDFUNC fpDrawSolidRectEx;
            VOIDFUNC fpDrawSolidRectAlpha;
            VOIDFUNC fpDrawLine;
            VOIDFUNC fpClearScreen;
            VOIDFUNC fpDrawString;
            VOIDFUNC fpDrawLight;
            VOIDFUNC fpDebugFillBackBuffer;
            VOIDFUNC fpClearCaches;
        };
    }
}

#pragma warning(pop)
#pragma optimize("", on)

namespace Offset {
    extern DWORD Base, D2CLIENT, D2COMMON, D2GAME, D2LANG, D2NET, D2MULTI, D2LAUNCH, D2WIN, D2GFX, D2CMP, BNCLIENT, STORM;
    void DefineOffsets();
}

#endif