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
#include "script_missiles.h"

namespace Game {

#pragma pack(push, 1)

struct Unit;
struct Room1;
struct Room2;
struct Level;

// Client table: *0x744304 + B64 pointer / B6C count; 0x4D3553..0x4D3567.
struct MissilesTxt {
    uint8_t pad00[0x08];
    int16_t clientDo;        // 08 client frame handler
    uint8_t pad0A[0x0C - 0x0A];
    int16_t serverDo;        // 0C server handler index in the shared client table
    uint8_t pad0E[0x183 - 0x0E];
    uint8_t collideType;     // 183 player/monster collision mode
    uint8_t pad184[0x18A - 0x184];
    uint8_t size;            // 18A point / cross / 3x3; 0x620569
    uint8_t pad18B[0x1A4 - 0x18B];
};
static_assert(offsetof(MissilesTxt, clientDo) == 0x08, "MissilesTxt client handler");
static_assert(offsetof(MissilesTxt, serverDo) == 0x0C, "MissilesTxt server handler");
static_assert(offsetof(MissilesTxt, collideType) == 0x183, "MissilesTxt collision mode");
static_assert(offsetof(MissilesTxt, size) == 0x18A, "MissilesTxt footprint");
static_assert(sizeof(MissilesTxt) == 0x1A4, "MissilesTxt stride");

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
static_assert(offsetof(Unit, data) == 0x14, "Unit data");

struct PlayerData {
    uint8_t pad00[0x150];
    uint32_t pending;        // 150 deferred action present (0x460780 / 0x48162C)
    uint32_t queuedAction;   // 154 client action opcode, not a skill id (0x4810A0)
    uint32_t targetType;     // 158 deferred target unit type (0x48164B)
    uint32_t targetGuid;     // 15C deferred target guid (0x48165D)
};
static_assert(offsetof(PlayerData, pending) == 0x150 && offsetof(PlayerData, queuedAction) == 0x154, "PlayerData action");
static_assert(offsetof(PlayerData, targetType) == 0x158 && offsetof(PlayerData, targetGuid) == 0x15C, "PlayerData target");

inline void CancelQueuedInteraction(Unit* player, void (__fastcall* clear)(Unit*)) {
    if (player && player->type == UnitType::Player && player->data
        && static_cast<PlayerData*>(player->data)->queuedAction == 0x13)
        clear(player);
}

// Partial MonStats.txt row: DAMAGE_ApplyCold 0x57AFE6 sign-extends aColdEffect[difficulty].
struct MonStatsCold {
    uint8_t pad00[0x168];   // 00
    int8_t coldEffect[3];   // 168 signed cold slow percentage, indexed by difficulty
};
static_assert(offsetof(MonStatsCold, coldEffect) == 0x168, "MonStats cold effect");

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
// Main-thread count of full quest records applied by the client (S2C 0x28/type 6).
inline uint32_t QuestRecordRevision = 0;
inline Unit** const PlayerUnit = reinterpret_cast<Unit**>(0x7A6A70);
// D2Client's count of client frames: ClientSideGameLoop (0x44C790) adds one
// per frame at 0x44C7F0..0x44C806. A frame is one step of the game's logic,
// 25 a second at the stock rate; in single player the server steps in
// lockstep with it at whatever rate the game is set to run.
inline const uint32_t* const ClientFrame = reinterpret_cast<const uint32_t*>(0x7A0498);
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
// 0x468990: no arguments, plain RET; checks leaving/death, then ECX=1 and tail-jumps
// to QUESTLOG_ToggleQuestScreen (0x4A3FE0), which preserves ESI and returns without stack arguments.
inline auto const ToggleQuestLog = reinterpret_cast<void(__stdcall*)()>(0x468990);
inline auto const GetCoords = reinterpret_cast<void(__stdcall*)(Unit*, POINT*)>(0x620870);
// Client shop eligibility: rejects quest/hammer items and vendor-owned stock.
// 0x62A130 reads one stack Unit* and returns with RET 4; no UI or server state.
inline auto const CanSellItem = reinterpret_cast<BOOL(__stdcall*)(Unit*)>(0x62A130);
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
// INVENTORY_GetEquipActionForBodySlot: what a click on a body slot would do
// with `item` on the cursor. 0 nothing (ITEMS_CheckBodyLocationFit says the
// item does not go there, or, unless skipRequirements, ITEM_CheckEquipRequirements
// says the unit cannot wear it); 1 into the empty slot; 5 swap with what is
// there; in the weapon hands also 2 and 7, a two-handed weapon against one or
// two held items, and 6, onto a stack. All four arguments on the stack (RET 0x10).
inline auto const EquipActionForBodySlot = reinterpret_cast<int32_t(__stdcall*)(Unit* unit, uint32_t bodyLoc, Unit* item, int skipRequirements)>(0x63DE60);
// The inventory screen's handlers for a click on a body slot, by body
// location 1..10 (ClickItemHandler reads the table at 0x49157A: player in
// ECX, inventory in EDX, the body location pushed, and the handler pops it).
// With an item on the cursor they ask the function above, send the request
// for its answer (0x1A, 0x1D, 0x1B, 0x1E) and take the item off the cursor.
inline auto const BodySlotClick = reinterpret_cast<uint32_t(__fastcall* const*)(Unit* player, Inventory* inventory, uint32_t bodyLoc)>(0x721E58);
// The cursor's mode, as GetItemType (0x468830) returns it: in 6 and 8 a click
// on a body slot uses the cursor on the item worn there instead (0x27, 0x4C).
inline auto const CursorMode = reinterpret_cast<const int32_t*>(0x7A6AF0);
// What the character screen shows as Defense, and what the hit roll
// (DAMAGE_RollAttackHit, 0x57D9B0) starts from. One stack argument, RET 4.
inline auto const GetDefense = reinterpret_cast<int32_t(__stdcall*)(Unit*)>(0x6223F0);
// Chance to block in percent, capped at 75, as the character screen shows it
// and DAMAGE_CheckBlockAndEvasion rolls it. Two stack arguments, RET 8.
inline auto const GetBlockRate = reinterpret_cast<int32_t(__stdcall*)(Unit*, BOOL expansion)>(0x622720);
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
// Game.exe 1.14d: pickup branch 0x548C69 uses these with distance <= 4 and mask 0x804.
// Disassembly confirms stack arguments and RET 8 / RET 12, respectively.
inline auto const UnitDistance = reinterpret_cast<int(__stdcall*)(Unit*, Unit*)>(0x641530);
inline auto const InteractingCollision = reinterpret_cast<BOOL(__stdcall*)(Unit*, Unit*, int mask)>(0x622B50);
inline auto const CoordsMapToScreen = reinterpret_cast<void(__stdcall*)(int* x, int* y)>(0x643260);

// Entry points with custom register contracts (assembly adapters in script_game.cpp).
BOOL MovePlayerTo(Unit* player, int x, int y, int action);
// Client-only game type getter 0x44DB30: no args, EAX result, plain RET.
int ClientGameType();
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
