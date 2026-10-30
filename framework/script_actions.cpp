/**
 * Lua `act` module: make the local player act through the client's own paths.
 *
 * Movement, casting and interaction use the client dispatcher that starts
 * local prediction and then sends the request (UNITMODE_ExecuteSkillAction), so
 * the player animates exactly as for a mouse click. Requests that the client
 * only forwards (stat/skill points, pickup, waypoint) send the same packets the
 * UI sends. Every call returns immediately; observe results through `game`.
 */
#include "headers/common.h"
#include "headers/remote.h"
#include "headers/script_game.h"
#include "headers/script_host.h"

namespace ScriptActions {

using namespace Game;

namespace {

enum Action {
    WalkToLocation = 0x01, WalkToUnit = 0x02, RunToLocation = 0x03, RunToUnit = 0x04,
    LeftOnLocation = 0x05, LeftOnUnit = 0x06, LeftOnUnitShift = 0x07,
    RightOnLocation = 0x0C, RightOnUnit = 0x0D, RightOnUnitShift = 0x0E,
    Interact = 0x13,
};

Unit* RequirePlayer() {
    Unit* player = Player();
    if (!player) throw Script::Error("not in game");
    if (LeavingGame()) throw Script::Error("leaving the game");
    return player;
}

uint32_t ArgType(lua_State* L, int index) {
    static const char* names[UnitTypeCount] = { "player", "monster", "object", "missile", "item", "warp" };
    if (lua_type(L, index) == LUA_TSTRING) {
        const std::string name = Script::ArgString(L, index);
        for (uint32_t t = 0; t < UnitTypeCount; ++t) {
            if (name == names[t]) return t;
        }
        throw Script::Error("unknown unit type '" + name + "'");
    }
    const int type = Script::ArgInt(L, index);
    if (type < 0 || type >= UnitTypeCount) throw Script::Error("unit type out of range");
    return (uint32_t)type;
}

// Accepts (type, id) or a unit snapshot table at `index`; returns the next argument index.
int ArgTarget(lua_State* L, int index, uint32_t& type, uint32_t& guid) {
    if (lua_type(L, index) == LUA_TTABLE) {
        Script::RawField(L, index, "type_id");
        Script::RawField(L, index, "id");
        const bool valid = lua_isnumber(L, -2) && lua_isnumber(L, -1);
        type = (uint32_t)lua_tonumber(L, -2);
        guid = (uint32_t)lua_tonumber(L, -1);
        lua_pop(L, 2);
        if (!valid || type >= UnitTypeCount) throw Script::Error("unit table needs type_id and id");
        return index + 1;
    }
    type = ArgType(L, index);
    guid = (uint32_t)Script::ArgNumber(L, index + 1);
    return index + 2;
}

bool IsRight(lua_State* L, int index) {
    const std::string hand = Script::ArgString(L, index);
    if (hand == "left") return false;
    if (hand == "right") return true;
    throw Script::Error("hand must be 'left' or 'right'");
}

int Coord(lua_State* L, int index) {
    const double value = Script::ArgNumber(L, index);
    if (value < 0 || value > 0xFFFF) throw Script::Error("coordinate out of range");
    return (int)value;
}

// act.move(x, y [, walk]) -> bool: path to the nearest reachable point and go.
int ActMove(lua_State* L) {
    Unit* player = RequirePlayer();
    const int x = Coord(L, 1), y = Coord(L, 2);
    const bool walk = Script::OptBool(L, 3, false);
    lua_pushboolean(L, MovePlayerTo(player, x, y, walk ? WalkToLocation : RunToLocation) != FALSE);
    return 1;
}

// act.move_to(type, id | unit [, walk])
int ActMoveTo(lua_State* L) {
    Unit* player = RequirePlayer();
    uint32_t type, guid;
    const int next = ArgTarget(L, 1, type, guid);
    const bool walk = Script::OptBool(L, next, false);
    if (!FindUnit(type, guid)) throw Script::Error("unit not found");
    ExecuteAction(walk ? WalkToUnit : RunToUnit, player, (int)type, (int)guid, 0);
    return 0;
}

// act.select_skill(hand, skillId [, ownerItemId]) -> bool
int ActSelectSkill(lua_State* L) {
    Unit* player = RequirePlayer();
    const bool right = IsRight(L, 1);
    const int skillId = Script::ArgInt(L, 2);
    const int owner = Script::OptInt(L, 3, -1);
    if (!GetSkillByIdAndOwner(player, skillId, owner)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // Same order as the skill hotkey path: request, then the local selection.
    uint8_t packet[9];
    packet[0] = 0x3C;
    *reinterpret_cast<uint32_t*>(packet + 1) = (uint32_t)skillId | (right ? 0 : 0x80000000u);
    *reinterpret_cast<int32_t*>(packet + 5) = owner;
    SendPacket(packet, sizeof(packet));
    (right ? SelectSkillRight : SelectSkillLeft)(player, skillId, owner);
    lua_pushboolean(L, 1);
    return 1;
}

// act.cast(hand, x, y): use the hand's skill at a location.
int ActCast(lua_State* L) {
    Unit* player = RequirePlayer();
    const bool right = IsRight(L, 1);
    const int x = Coord(L, 2), y = Coord(L, 3);
    ExecuteAction(right ? RightOnLocation : LeftOnLocation, player, x, y, 0);
    return 0;
}

// act.cast_at(hand, type, id | unit [, standStill])
int ActCastAt(lua_State* L) {
    Unit* player = RequirePlayer();
    const bool right = IsRight(L, 1);
    uint32_t type, guid;
    const int next = ArgTarget(L, 2, type, guid);
    const bool standStill = Script::OptBool(L, next, false);
    if (!FindUnit(type, guid)) throw Script::Error("unit not found");
    const int action = right ? (standStill ? RightOnUnitShift : RightOnUnit) : (standStill ? LeftOnUnitShift : LeftOnUnit);
    ExecuteAction(action, player, (int)type, (int)guid, 0);
    return 0;
}

// act.interact(unit): what clicking on the unit does: walks up if needed, then
// talks, opens, uses a waypoint, portal or stairs, or picks up a corpse.
int ActInteract(lua_State* L) {
    Unit* player = RequirePlayer();
    uint32_t type, guid;
    ArgTarget(L, 1, type, guid);
    Unit* target = FindUnit(type, guid);
    if (!target) throw Script::Error("unit not found");
    InteractWithUnit(player, target);
    return 0;
}

// act.interact_now(unit): interact immediately without approaching (must be in range).
int ActInteractNow(lua_State* L) {
    Unit* player = RequirePlayer();
    uint32_t type, guid;
    ArgTarget(L, 1, type, guid);
    if (!FindUnit(type, guid)) throw Script::Error("unit not found");
    ExecuteAction(Interact, player, (int)type, (int)guid, 0);
    return 0;
}

// act.pickup(itemId | item [, toCursor])
int ActPickup(lua_State* L) {
    RequirePlayer();
    uint32_t type = UnitType::Item, guid;
    int next = 2;
    if (lua_type(L, 1) == LUA_TTABLE) next = ArgTarget(L, 1, type, guid);
    else guid = (uint32_t)Script::ArgNumber(L, 1);
    const bool toCursor = Script::OptBool(L, next, false);
    if (type != UnitType::Item || !FindUnit(UnitType::Item, guid)) throw Script::Error("item not found");
    uint8_t packet[13] = { 0x16 };
    *reinterpret_cast<uint32_t*>(packet + 1) = UnitType::Item;
    *reinterpret_cast<uint32_t*>(packet + 5) = guid;
    *reinterpret_cast<uint32_t*>(packet + 9) = toCursor ? 1 : 0;
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.use_belt(slot [, mercenary]): slot 0..15; 0..3 is the bottom row.
int ActUseBelt(lua_State* L) {
    Unit* player = RequirePlayer();
    const int slot = Script::ArgInt(L, 1);
    const bool merc = Script::OptBool(L, 2, false);
    if (slot < 0 || slot > 15) throw Script::Error("belt slot must be 0..15");
    if (!player->inventory) throw Script::Error("player has no inventory");
    UseBeltSlot(player, slot, merc);
    return 0;
}

// act.add_stat(stat [, count]): 0 strength, 1 energy, 2 dexterity, 3 vitality.
int ActAddStat(lua_State* L) {
    RequirePlayer();
    const int stat = Script::ArgInt(L, 1);
    int count = Script::OptInt(L, 2, 1);
    if (stat < 0 || stat > 3) throw Script::Error("stat must be 0..3");
    while (count > 0) {
        const int batch = count > 32 ? 32 : count;
        const uint8_t packet[3] = { 0x3A, (uint8_t)stat, (uint8_t)(batch - 1) };
        SendPacket(packet, sizeof(packet));
        count -= batch;
    }
    return 0;
}

// act.add_skill(skillId)
int ActAddSkill(lua_State* L) {
    RequirePlayer();
    const int skill = Script::ArgInt(L, 1);
    if (skill < 0 || skill > 0xFFFF) throw Script::Error("skill id out of range");
    const uint8_t packet[3] = { 0x3B, (uint8_t)(skill & 0xFF), (uint8_t)(skill >> 8) };
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.waypoint(levelId): travel from the open waypoint panel (interact with a
// waypoint first). Mirrors the panel's own click: mark the panel closed, clear
// its selection, send the request and drop the panel flag.
int ActWaypoint(lua_State* L) {
    RequirePlayer();
    const int level = Script::ArgInt(L, 1);
    if (level <= 0 || level > 0xFF) throw Script::Error("level id out of range");
    if (!UIOpen(20)) throw Script::Error("no waypoint panel is open");
    const uint32_t waypoint = *reinterpret_cast<uint32_t*>(0x7BF07D);
    *reinterpret_cast<uint8_t*>(0x7BF085) = 1;
    *reinterpret_cast<uint8_t*>(0x7BF06C) = 0;
    *reinterpret_cast<int32_t*>(0x7BF06D) = -1;
    uint8_t packet[9] = { 0x49 };
    *reinterpret_cast<uint32_t*>(packet + 1) = waypoint;
    *reinterpret_cast<uint32_t*>(packet + 5) = (uint32_t)level;
    SendPacket(packet, sizeof(packet));
    auto setUIFlag = reinterpret_cast<DWORD(__fastcall*)(int id, int operation, int arg)>(0x455F20);
    setUIFlag(20, 1, 0);
    return 0;
}

// act.use_item(item): right-click use of an inventory item (town portal
// scroll or tome, potions).
int ActUseItem(lua_State* L) {
    Unit* player = RequirePlayer();
    uint32_t type = UnitType::Item, guid;
    if (lua_type(L, 1) == LUA_TTABLE) ArgTarget(L, 1, type, guid);
    else guid = (uint32_t)Script::ArgNumber(L, 1);
    if (type != UnitType::Item) throw Script::Error("not an item");
    double x, y;
    Position(player, x, y);
    uint8_t packet[13] = { 0x20 };
    *reinterpret_cast<uint32_t*>(packet + 1) = guid;
    *reinterpret_cast<uint32_t*>(packet + 5) = (uint32_t)x;
    *reinterpret_cast<uint32_t*>(packet + 9) = (uint32_t)y;
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.to_belt(item): move a potion from the inventory to the belt (shift-click).
int ActToBelt(lua_State* L) {
    RequirePlayer();
    uint32_t type = UnitType::Item, guid;
    if (lua_type(L, 1) == LUA_TTABLE) ArgTarget(L, 1, type, guid);
    else guid = (uint32_t)Script::ArgNumber(L, 1);
    uint8_t packet[5] = { 0x63 };
    *reinterpret_cast<uint32_t*>(packet + 1) = guid;
    SendPacket(packet, sizeof(packet));
    return 0;
}

uint32_t ArgItemGuid(lua_State* L, int index) {
    uint32_t type = UnitType::Item, guid;
    if (lua_type(L, index) == LUA_TTABLE) ArgTarget(L, index, type, guid);
    else guid = (uint32_t)Script::ArgNumber(L, index);
    if (type != UnitType::Item) throw Script::Error("not an item");
    return guid;
}

// act.pick_item(item): lift an item from the inventory, cube or open stash onto
// the cursor, as a left click on it does.
int ActPickItem(lua_State* L) {
    Unit* player = RequirePlayer();
    if (player->inventory && player->inventory->cursorItem) throw Script::Error("the cursor already holds an item");
    uint8_t packet[5] = { 0x19 };
    *reinterpret_cast<uint32_t*>(packet + 1) = ArgItemGuid(L, 1);
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.place_item(item, page, x, y): put the cursor item into a grid cell of
// 'inventory', 'cube' or 'stash' (the stash and cube must be open), as a left
// click on that cell does. The server rejects cells that do not fit.
int ActPlaceItem(lua_State* L) {
    Unit* player = RequirePlayer();
    if (!player->inventory || !player->inventory->cursorItem) throw Script::Error("the cursor holds no item");
    const uint32_t guid = ArgItemGuid(L, 1);
    const std::string page = Script::ArgString(L, 2);
    uint32_t buffer;
    if (page == "inventory") buffer = 0;
    else if (page == "cube") buffer = 3;
    else if (page == "stash") buffer = 4;
    else throw Script::Error("page must be 'inventory', 'cube' or 'stash'");
    const int x = Script::ArgInt(L, 3), y = Script::ArgInt(L, 4);
    if (x < 0 || y < 0 || x > 15 || y > 15) throw Script::Error("cell out of range");
    uint8_t packet[17] = { 0x18 };
    *reinterpret_cast<uint32_t*>(packet + 1) = guid;
    *reinterpret_cast<uint32_t*>(packet + 5) = (uint32_t)x;
    *reinterpret_cast<uint32_t*>(packet + 9) = (uint32_t)y;
    *reinterpret_cast<uint32_t*>(packet + 13) = buffer;
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.transfer_gold('deposit' | 'withdraw', amount): move gold between the
// inventory and the open stash, as the stash's gold dialogs do (button 0x14
// deposits the inventory's gold, 0x13 withdraws the stash's). Gold in the stash
// is not dropped on death.
int ActTransferGold(lua_State* L) {
    Unit* player = RequirePlayer();
    if (!UIOpen(25)) throw Script::Error("the stash is not open");
    const std::string way = Script::ArgString(L, 1);
    const int amount = Script::ArgInt(L, 2);
    int16_t button;
    int available;
    if (way == "deposit") {
        button = 0x14;
        available = GetUnitStat(player, 14, 0);
    } else if (way == "withdraw") {
        button = 0x13;
        available = GetUnitStat(player, 15, 0);
    } else {
        throw Script::Error("expected 'deposit' or 'withdraw'");
    }
    if (amount < 1 || amount > available) throw Script::Error("amount out of range");
    uint8_t packet[7] = { 0x4F };
    *reinterpret_cast<int16_t*>(packet + 1) = button;
    *reinterpret_cast<int16_t*>(packet + 3) = (int16_t)(amount >> 16);
    *reinterpret_cast<int16_t*>(packet + 5) = (int16_t)amount;
    SendPacket(packet, sizeof(packet));
    return 0;
}

// act.send(bytes): raw client request, for anything without a helper yet.
int ActSend(lua_State* L) {
    RequirePlayer();
    const std::string bytes = Script::ArgString(L, 1);
    if (bytes.empty() || bytes.size() >= 512) throw Script::Error("packet must be 1..511 bytes");
    SendPacket(bytes.data(), bytes.size());
    return 0;
}

// act.leave_game() -> bool: leave the game the way the escape menu's "Save and
// Exit Game" does, back to the menus. The game starts with "quit the application
// after this game" set; the menu clears it, then closes the game window, whose
// WM_CLOSE handler sends the leave request, and marks the leave as intended.
// A bare leave request would end Game.exe instead. Returns false if a leave is
// already under way.
int ActLeaveGame(lua_State* L) {
    RequirePlayer();
    BOOL* const quitApplication = reinterpret_cast<BOOL*>(0x70EE8C);
    BOOL* const leaveRequested = reinterpret_cast<BOOL*>(0x7A0674);
    BOOL* const stopGameLoop = reinterpret_cast<BOOL*>(0x7A0620);
    if (*leaveRequested || *stopGameLoop) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // The menu's own Save and Exit restores the escape menu's saved panel state;
    // that state is only valid after a normal escape-menu open, so require it closed.
    if (UIOpen(9)) throw Script::Error("close the escape menu before leaving");
    const HWND window = D2::hwndD2GFX_Window;
    if (!window) throw Script::Error("no game window");
    *quitApplication = FALSE;
    SendMessageA(window, WM_CLOSE, 0, 0);
    *leaveRequested = TRUE;
    lua_pushboolean(L, 1);
    return 1;
}

// act.key(vk): press a key in the game window (UI fallback).
int ActKey(lua_State* L) {
    const int key = Script::ArgInt(L, 1);
    const HWND window = D2::hwndD2GFX_Window;
    if (!window) throw Script::Error("no game window");
    const UINT scan = MapVirtualKeyW((UINT)key, MAPVK_VK_TO_VSC);
    PostMessageW(window, WM_KEYDOWN, (WPARAM)key, 1 | (scan << 16));
    PostMessageW(window, WM_KEYUP, (WPARAM)key, 1 | (scan << 16) | 0xC0000000);
    return 0;
}

// act.ready() -> bool: the player can start a new action now.
int ActReady(lua_State* L) {
    Unit* player = Player();
    const bool ready = player && player->skills && !(player->inventory && player->inventory->cursorItem)
        && PlayerCanBeInterrupted(player, player->skills->right) != FALSE;
    lua_pushboolean(L, ready);
    return 1;
}

const bool registered = (Script::RegisterModule(Script::InGame, [](lua_State* L) {
    static const luaL_Reg functions[] = {
        { "move", Script::Bind<ActMove> },
        { "move_to", Script::Bind<ActMoveTo> },
        { "select_skill", Script::Bind<ActSelectSkill> },
        { "cast", Script::Bind<ActCast> },
        { "cast_at", Script::Bind<ActCastAt> },
        { "interact", Script::Bind<ActInteract> },
        { "interact_now", Script::Bind<ActInteractNow> },
        { "pickup", Script::Bind<ActPickup> },
        { "use_belt", Script::Bind<ActUseBelt> },
        { "add_stat", Script::Bind<ActAddStat> },
        { "add_skill", Script::Bind<ActAddSkill> },
        { "waypoint", Script::Bind<ActWaypoint> },
        { "use_item", Script::Bind<ActUseItem> },
        { "to_belt", Script::Bind<ActToBelt> },
        { "pick_item", Script::Bind<ActPickItem> },
        { "place_item", Script::Bind<ActPlaceItem> },
        { "transfer_gold", Script::Bind<ActTransferGold> },
        { "send", Script::Bind<ActSend> },
        { "leave_game", Script::Bind<ActLeaveGame> },
        { "key", Script::Bind<ActKey> },
        { "ready", Script::Bind<ActReady> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "act");
}), true);

}

}
