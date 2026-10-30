/**
 * Client-side game layouts and entry points used by the Lua game API.
 *
 * Diablo II 1.14d, from redivivus (address-keyed). Only client memory is used:
 * the API must work in TCP/IP games hosted elsewhere, so nothing here touches
 * D2Game or a pGame. Offsets carry static_asserts; ABIs were checked against
 * the disassembly (stack purge / register use) unless noted.
 */
#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace Game {

#pragma pack(push, 1)

struct Unit;
struct Room1;
struct Room2;
struct Level;

namespace UnitType {
enum : uint32_t { Player = 0, Monster = 1, Object = 2, Missile = 3, Item = 4, Warp = 5 };
}
const uint32_t UnitTypeCount = 6;

struct DynamicPath {
    uint32_t x;              // 00 16.16 fixed point world subtiles
    uint32_t y;              // 04
    uint32_t screenX, screenY; // 08
    int16_t targetX, targetY; // 10
    uint8_t pad14[0x1C - 0x14];
    Room1* room;             // 1C
};
static_assert(offsetof(DynamicPath, room) == 0x1C, "DynamicPath room");

struct StaticPath {
    Room1* room;             // 00
    uint32_t screenX, screenY; // 04
    int32_t x, y;            // 0C world subtiles, or grid cell for carried items
};
static_assert(offsetof(StaticPath, x) == 0x0C, "StaticPath position");

struct Inventory {
    uint8_t pad00[0x0C];
    Unit* firstItem;         // 0C
    uint8_t pad10[0x20 - 0x10];
    Unit* cursorItem;        // 20
};
static_assert(offsetof(Inventory, cursorItem) == 0x20, "Inventory cursor");

struct SkillsTxt;
struct Skill {
    SkillsTxt* txt;          // 00 (skill id is the first WORD)
    Skill* next;             // 04
    uint8_t pad08[0x28 - 0x08];
    int32_t baseLevel;       // 28
    int32_t bonusLevel;      // 2C
    int32_t quantity;        // 30
    int32_t ownerGuid;       // 34 item granting the skill, -1 innate
    int32_t charges;         // 38
};
static_assert(offsetof(Skill, charges) == 0x38, "Skill layout");

struct SkillList {
    void* pool;              // 00
    Skill* first;            // 04
    Skill* left;             // 08
    Skill* right;            // 0C
    Skill* current;          // 10
};

struct Unit {
    uint32_t type;           // 00
    int32_t classId;         // 04
    void* pool;              // 08
    uint32_t guid;           // 0C
    uint32_t mode;           // 10
    void* data;              // 14
    uint8_t act;             // 18
    uint8_t pad19[3];
    void* drlgAct;           // 1C
    uint32_t seed[3];        // 20
    void* path;              // 2C DynamicPath (player, monster, missile) or StaticPath
    uint8_t pad30[0x5C - 0x30];
    void* stats;             // 5C
    Inventory* inventory;    // 60
    uint8_t pad64[0x94 - 0x64];
    uint32_t ownerType;      // 94 valid for missiles when flagsEx & 0x400
    uint32_t ownerGuid;      // 98
    uint8_t pad9C[0xA8 - 0x9C];
    SkillList* skills;       // A8
    uint8_t padAC[0xC4 - 0xAC];
    uint32_t flags;          // C4
    uint32_t flagsEx;        // C8
    uint8_t padCC[0xE4 - 0xCC];
    Unit* hashNext;          // E4
    Unit* roomNext;          // E8
};
static_assert(offsetof(Unit, path) == 0x2C && offsetof(Unit, stats) == 0x5C, "Unit layout");
static_assert(offsetof(Unit, skills) == 0xA8 && offsetof(Unit, hashNext) == 0xE4, "Unit layout");

struct MonsterData {
    uint8_t* monStats;       // 00 MonStatsTxt*
    uint8_t pad04[0x16 - 0x04];
    uint16_t typeFlags;      // 16 low byte: 2 superunique, 4 champion, 8 unique, 0x10 minion
    uint8_t pad18[0x1C - 0x18];
    uint8_t enchants[9];     // 1C
    uint8_t pad25;
    uint16_t superUniqueId;  // 26
    uint16_t nameMask;       // 28
    uint8_t pad2A[2];
    const wchar_t* name;     // 2C
    uint8_t pad30[0x3C - 0x30];
    int32_t ownerGuid;       // 3C
};
static_assert(offsetof(MonsterData, name) == 0x2C && offsetof(MonsterData, ownerGuid) == 0x3C, "MonsterData layout");

struct ItemData {
    uint32_t quality;        // 00 1 inferior .. 8 crafted
    uint8_t pad04[0x18 - 0x04];
    uint32_t flags;          // 18 0x10 identified, 0x800 socketed, 0x400000 ethereal, 0x4000000 runeword
    uint8_t pad1C[0x2C - 0x1C];
    uint32_t itemLevel;      // 2C
    uint8_t pad30[0x44 - 0x30];
    uint8_t bodyLocation;    // 44
    uint8_t page;            // 45 0 inventory, 3 cube, 4 stash
    uint8_t pad46[0x64 - 0x46];
    Unit* nextInInventory;   // 64
};
static_assert(offsetof(ItemData, page) == 0x45 && offsetof(ItemData, nextInInventory) == 0x64, "ItemData layout");

struct CollisionGrid {
    int32_t x, y, width, height; // 00 subtiles
    int32_t tileRect[4];     // 10
    uint16_t* cells;         // 20 row-major, width * height
};
static_assert(offsetof(CollisionGrid, cells) == 0x20, "CollisionGrid layout");

struct Room1 {
    Room1** nearRooms;       // 00
    uint8_t pad04[0x10 - 0x04];
    Room2* room2;            // 10
    uint8_t pad14[0x20 - 0x14];
    CollisionGrid* collision; // 20
    int32_t nearCount;       // 24
    uint8_t pad28[0x4C - 0x28];
    int32_t x, y, width, height; // 4C subtiles
    uint8_t pad5C[0x74 - 0x5C];
    Unit* firstUnit;         // 74
    uint8_t pad78[0x7C - 0x78];
    Room1* next;             // 7C
};
static_assert(offsetof(Room1, collision) == 0x20 && offsetof(Room1, firstUnit) == 0x74, "Room1 layout");

struct PresetUnit {
    uint32_t mode;           // 00
    int32_t classId;         // 04
    int32_t localX;          // 08
    PresetUnit* next;        // 0C
    void* mapAi;             // 10
    uint32_t type;           // 14
    int32_t localY;          // 18
};
static_assert(offsetof(PresetUnit, localY) == 0x18, "PresetUnit layout");

struct RoomTile {
    Room2* destination;      // 00
    RoomTile* next;          // 04
    BOOL enabled;            // 08
    void* tiles;             // 0C
    int32_t* lvlWarp;        // 10 LvlWarpTxt*, first field is the warp id
};

struct Room2 {
    void* orth;              // 00
    uint8_t pad04[0x08 - 0x04];
    Room2** nearRooms;       // 08
    uint8_t pad0C[0x24 - 0x0C];
    Room2* next;             // 24 next in level
    uint32_t flags;          // 28
    int32_t nearCount;       // 2C
    Room1* room1;            // 30
    int32_t tileX, tileY, tileWidth, tileHeight; // 34
    uint8_t pad44[0x4C - 0x44];
    RoomTile* tiles;         // 4C
    uint8_t pad50[0x58 - 0x50];
    Level* level;            // 58
    PresetUnit* presets;     // 5C
};
static_assert(offsetof(Room2, room1) == 0x30 && offsetof(Room2, level) == 0x58, "Room2 layout");

struct Level {
    uint32_t drlgType;       // 00 1 maze, 2 preset, 3 wilderness
    uint8_t pad04[0x10 - 0x04];
    Room2* firstRoom;        // 10
    uint8_t pad14[0x1C - 0x14];
    int32_t tileX, tileY, tileWidth, tileHeight; // 1C
    uint8_t pad2C[0x1AC - 0x2C];
    Level* next;             // 1AC
    uint8_t pad1B0[0x1D0 - 0x1B0];
    int32_t id;              // 1D0
};
static_assert(offsetof(Level, next) == 0x1AC && offsetof(Level, id) == 0x1D0, "Level layout");

struct ControlMsgOwner;

// UnderMouse context consumed by the client's input-command helpers.
struct InputContext {
    uint32_t flags;          // 1 left, 2 right, 4 initial, 8 repeat, 0x20 shift, 0x40 run
    Unit* player;
    Unit* target;
    int32_t x, y;
    int32_t moveAction;
    int32_t unitMoveAction;
    Skill* skill;
};
static_assert(sizeof(InputContext) == 0x20, "D2UnderMouseStrc is 0x20 bytes");

#pragma pack(pop)

// Globals.
inline Unit** const PlayerUnit = reinterpret_cast<Unit**>(0x7A6A70);
// D2Client's tables of units announced by the game server. The redivivus name
// "ServerSide" means server-synchronized: this is client memory, present in
// TCP/IP games. Index [type][guid & 127], chained through Unit::hashNext.
inline Unit* (*const SyncedUnits)[128] = reinterpret_cast<Unit* (*)[128]>(0x7A5E70);
// Client-only units (local effects); same layout.
inline Unit* (*const LocalUnits)[128] = reinterpret_cast<Unit* (*)[128]>(0x7A5270);
inline uint8_t* const Difficulty = reinterpret_cast<uint8_t*>(0x7A060C);
inline BOOL* const ExpansionGame = reinterpret_cast<BOOL*>(0x7A04F4);
inline uint32_t* const UIFlags = reinterpret_cast<uint32_t*>(0x7A27C0);
const int UIFlagCount = 0x26;

// D2Common / D2Client functions (client copies of shared game logic).
inline auto const GetCoords = reinterpret_cast<void(__stdcall*)(Unit*, POINT*)>(0x620870);
inline auto const GetUnitStat = reinterpret_cast<int32_t(__stdcall*)(Unit*, int stat, uint32_t layer)>(0x625480);
inline auto const GetBaseStat = reinterpret_cast<int32_t(__stdcall*)(void* statsEx, int stat, uint32_t layer)>(0x625350);
inline auto const GetUnitState = reinterpret_cast<uint32_t(__stdcall*)(Unit*, int state)>(0x639DF0);
inline auto const GetUnitName = reinterpret_cast<const wchar_t*(__fastcall*)(Unit*)>(0x464A60);
inline auto const GetLevelIdFromRoom = reinterpret_cast<int(__stdcall*)(Room1*)>(0x61A1B0);
inline auto const GetLevelsTxt = reinterpret_cast<uint8_t*(__stdcall*)(int levelId)>(0x61DB70);
inline auto const GetMonStatsTxt = reinterpret_cast<uint8_t*(__fastcall*)(int classId)>(0x451F80);
inline auto const GetSkillsTxtById = reinterpret_cast<SkillsTxt*(__fastcall*)(int skillId)>(0x45C4B0);
inline auto const GetItemsTxt = reinterpret_cast<uint8_t*(__stdcall*)(uint32_t classId)>(0x6335F0);
inline auto const GetObjectsTxt = reinterpret_cast<uint8_t*(__stdcall*)(int classId)>(0x640E90);
inline auto const GetSkillLevel = reinterpret_cast<int(__stdcall*)(Unit*, Skill*, BOOL applyBonus)>(0x6442A0);
inline auto const GetSkillByIdAndOwner = reinterpret_cast<Skill*(__fastcall*)(Unit*, int skillId, int ownerGuid)>(0x6439B0);
inline auto const CanUseSkill = reinterpret_cast<int(__fastcall*)(Unit*, Skill*)>(0x4D9FC0);
inline auto const PlayerCanBeInterrupted = reinterpret_cast<BOOL(__fastcall*)(Unit*, Skill*)>(0x464600);
inline auto const SelectSkillLeft = reinterpret_cast<void(__stdcall*)(Unit*, int skillId, int ownerGuid)>(0x643BC0);
inline auto const SelectSkillRight = reinterpret_cast<void(__stdcall*)(Unit*, int skillId, int ownerGuid)>(0x643C50);
inline auto const ExecuteAction = reinterpret_cast<void(__fastcall*)(int action, Unit*, int a, int b, uint32_t unused)>(0x481030);
inline auto const SkillElemMin = reinterpret_cast<int(__stdcall*)(Unit*, int skillId, int level, BOOL mastery)>(0x644D50);
inline auto const SkillElemMax = reinterpret_cast<int(__stdcall*)(Unit*, int skillId, int level, BOOL mastery)>(0x644E40);
inline auto const SkillPhysMin = reinterpret_cast<int(__stdcall*)(Unit*, int skillId, int level, BOOL weapon)>(0x647BC0);
inline auto const SkillPhysMax = reinterpret_cast<int(__stdcall*)(Unit*, int skillId, int level, BOOL weapon)>(0x647D00);
inline auto const SkillManaCost = reinterpret_cast<int(__stdcall*)(int skillId, int level)>(0x6459F0);
inline auto const SkillToHit = reinterpret_cast<uint32_t(__stdcall*)(Unit*, int skillId, int level)>(0x6449F0);
inline auto const SkillCalc = reinterpret_cast<int(__stdcall*)(Unit*, int calcOffset, int skillId, int level)>(0x646CA0);
inline auto const AddRoomData = reinterpret_cast<void(__stdcall*)(void* act, int levelId, int tileX, int tileY, Room1* hint)>(0x61A070);
inline auto const GetLevelAndAlloc = reinterpret_cast<Level*(__stdcall*)(void* drlg, int levelId)>(0x642BB0);
inline auto const InitLevel = reinterpret_cast<void(__stdcall*)(Level*)>(0x6424A0);
// Collision bits at a world subtile, resolved through the room's loaded neighbours.
inline auto const CheckCollision = reinterpret_cast<uint32_t(__stdcall*)(Room1* room, int x, int y, uint32_t mask)>(0x64CB30);
inline auto const CoordsMapToScreen = reinterpret_cast<void(__stdcall*)(int* x, int* y)>(0x643260);

// Entry points with custom register contracts (assembly adapters in script_game.cpp).
BOOL MovePlayerTo(Unit* player, int x, int y, int action);
void UseBeltSlot(Unit* player, int slot, bool toMercenary);
void InteractWithUnit(Unit* player, Unit* target);
const wchar_t* LocaleString(uint16_t id);
void SendPacket(const void* bytes, size_t size);

// Helpers.
Unit* Player();
Unit* FindUnit(uint32_t type, uint32_t guid);
bool Position(Unit* unit, double& x, double& y);
Room1* UnitRoom(Unit* unit);
int UnitLevelId(Unit* unit);
inline uint16_t SkillId(const Skill* skill) { return skill && skill->txt ? *reinterpret_cast<const uint16_t*>(skill->txt) : 0xFFFF; }
inline bool IsTown(int levelId) { return levelId == 1 || levelId == 40 || levelId == 75 || levelId == 103 || levelId == 109; }
inline bool UIOpen(int id) { return id >= 0 && id < UIFlagCount && UIFlags[id] != 0; }
// A leave was requested (D2CLIENT_ExitVar2) or the game loop is stopping (ExitVar3).
inline bool LeavingGame() { return *reinterpret_cast<BOOL*>(0x7A0674) || *reinterpret_cast<BOOL*>(0x7A0620); }

}
