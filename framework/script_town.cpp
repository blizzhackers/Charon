/**
 * Lua `town` module: NPC trade, vendor stock, prices and mercenaries.
 *
 * Talking to an NPC (act.interact) runs the client's own handshake; this
 * module then drives the NPC menu through the client's menu callbacks and
 * sends the same transaction requests the shop UI sends. Vendor stock is read
 * from the NPC's client-side inventory. Results arrive as ordinary item and
 * gold updates; scripts confirm by watching `game.items()` and `game.me().gold`.
 */
#include "headers/common.h"
#include "headers/script_game.h"
#include "headers/script_host.h"

namespace ScriptTown {

using namespace Game;

namespace {

const int NpcMenuFlag = 8, ShopFlag = 12;

// NPC menu callbacks (no arguments; they act on the current interaction).
auto const TradeOption = reinterpret_cast<int(*)()>(0x4B42B0);
auto const CloseAllNpcDialogs = reinterpret_cast<void(*)()>(0x4B2110);
auto const CloseNpcMenu = reinterpret_cast<void(*)()>(0x4B3DE0);
auto const CloseShop = reinterpret_cast<void(*)()>(0x4B3ED0);
auto const TransactionPrice = reinterpret_cast<int(__stdcall*)(Unit* buyer, Unit* item, int difficulty, void* quests, int npcClass, int mode)>(0x62FDC0);
auto const RepairAllPrice = reinterpret_cast<int(__stdcall*)(void* game, Unit* player, int npcClass, int difficulty, void* quests, void* callback)>(0x62FE60);
auto const GetMercAct = reinterpret_cast<int(__fastcall*)(BOOL expansion, int classId, uint32_t nameId)>(0x663750);
auto const RollMercStats = reinterpret_cast<BOOL(__fastcall*)(int expansion, Unit* player, int seed, int act, int difficulty, void* out)>(0x6637F0);

void* QuestRecord() {
    return *reinterpret_cast<void**>(0x7C0D43);
}

int Difficulty() {
    return *Game::Difficulty > 2 ? 2 : *Game::Difficulty;
}

Unit* RequirePlayer() {
    Unit* player = Player();
    if (!player) throw Script::Error("not in game");
    return player;
}

uint32_t ArgGuid(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TTABLE) {
        Script::RawField(L, index, "id");
        const double id = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return (uint32_t)id;
    }
    return (uint32_t)Script::ArgNumber(L, index);
}

Unit* ArgNpc(lua_State* L, int index) {
    Unit* npc = FindUnit(UnitType::Monster, ArgGuid(L, index));
    if (!npc) throw Script::Error("NPC not found");
    return npc;
}

// An item in the NPC's (vendor) or player's client inventory.
Unit* FindInInventory(Unit* owner, uint32_t guid) {
    if (!owner || !owner->inventory) return nullptr;
    int guard = 0;
    for (Unit* item = owner->inventory->firstItem; item && guard++ < 2048;
        item = item->data ? static_cast<ItemData*>(item->data)->nextInInventory : nullptr) {
        if (item->guid == guid) return item;
    }
    return nullptr;
}

void SendTransaction(uint8_t opcode, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint8_t packet[17];
    packet[0] = opcode;
    memcpy(packet + 1, &a, 4);
    memcpy(packet + 5, &b, 4);
    memcpy(packet + 9, &c, 4);
    memcpy(packet + 13, &d, 4);
    SendPacket(packet, sizeof(packet));
}

// NPCs whose menu has a Trade (or Trade/Repair) row. Act 1 only so far:
// Akara (148) and Charsi (154) per the client menu table, Gheed (147) as
// observed in game. Extend per act as vendors are confirmed.
bool Trades(int classId) {
    return classId == 148 || classId == 154 || classId == 147;
}

// town.trade(npc) -> bool: choose Trade (or Trade/Repair) in the open menu of a vendor.
int TownTrade(lua_State* L) {
    RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    if (!UIOpen(NpcMenuFlag)) throw Script::Error("no NPC menu is open");
    if (!Trades(npc->classId)) throw Script::Error("this NPC does not trade");
    TradeOption();
    lua_pushboolean(L, UIOpen(ShopFlag));
    return 1;
}

// town.close(): leave the shop or NPC menu the way the UI does. The hire
// list's own teardown is not called: the hireables UI flag is also set outside
// a real hire list, and tearing down a list that does not exist corrupts
// memory (the sound thread crashed on it). After hiring, scripts leave the game.
int TownClose(lua_State* L) {
    RequirePlayer();
    if (UIOpen(ShopFlag)) {
        CloseAllNpcDialogs();
        CloseShop();
    }
    else if (UIOpen(NpcMenuFlag)) {
        CloseAllNpcDialogs();
        CloseNpcMenu();
    }
    (void)L;
    return 0;
}

// town.respec(akara): Akara's one-time skill and stat reset (granted for the
// Den of Evil), as her menu's confirm button does it: close the menu, then
// C->S 0x38 (action 0, Akara's GUID, 0). The server decides whether a reset
// is still available; scripts see the points come back in game.me().
int TownRespec(lua_State* L) {
    RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    if (npc->classId != 148) throw Script::Error("only Akara resets points");
    if (!UIOpen(NpcMenuFlag)) throw Script::Error("talk to Akara first");
    CloseAllNpcDialogs();
    CloseNpcMenu();
    uint8_t packet[13] = { 0x38 };
    *reinterpret_cast<uint32_t*>(packet + 1) = 0;
    *reinterpret_cast<uint32_t*>(packet + 5) = npc->guid;
    *reinterpret_cast<uint32_t*>(packet + 9) = 0;
    SendPacket(packet, sizeof(packet));
    return 0;
}

// town.price(item, npc, mode) -> gold: mode 'buy', 'sell' or 'repair'.
int TownPrice(lua_State* L) {
    Unit* player = RequirePlayer();
    const uint32_t itemGuid = ArgGuid(L, 1);
    Unit* npc = ArgNpc(L, 2);
    const std::string mode = Script::ArgString(L, 3);
    const int modeId = mode == "buy" ? 0 : mode == "sell" ? 1 : mode == "repair" ? 3 : -1;
    if (modeId < 0) throw Script::Error("mode must be 'buy', 'sell' or 'repair'");
    Unit* item = FindInInventory(modeId == 0 ? npc : player, itemGuid);
    if (!item) throw Script::Error("item not found");
    if (!QuestRecord()) throw Script::Error("quest record not received yet");
    lua_pushinteger(L, TransactionPrice(player, item, Difficulty(), QuestRecord(), npc->classId, modeId));
    return 1;
}

// town.buy(npc, item [, fill]) -> price: fill uses shift-buy (fills belt or stack when allowed).
int TownBuy(lua_State* L) {
    Unit* player = RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    const uint32_t itemGuid = ArgGuid(L, 2);
    const bool fill = Script::OptBool(L, 3, false);
    if (!UIOpen(ShopFlag)) throw Script::Error("no shop is open");
    Unit* item = FindInInventory(npc, itemGuid);
    if (!item) throw Script::Error("item is not in the vendor's stock");
    if (!QuestRecord()) throw Script::Error("quest record not received yet");
    const int price = TransactionPrice(player, item, Difficulty(), QuestRecord(), npc->classId, 0);
    SendTransaction(0x32, npc->guid, item->guid, (item->mode << 16) | (fill ? 0x80000000u : 0), (uint32_t)price);
    lua_pushinteger(L, price);
    return 1;
}

// town.sell(npc, item) -> price
int TownSell(lua_State* L) {
    Unit* player = RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    const uint32_t itemGuid = ArgGuid(L, 2);
    if (!UIOpen(ShopFlag)) throw Script::Error("no shop is open");
    Unit* item = FindInInventory(player, itemGuid);
    if (!item) throw Script::Error("item is not in the player's inventory");
    if (!QuestRecord()) throw Script::Error("quest record not received yet");
    const int price = TransactionPrice(player, item, Difficulty(), QuestRecord(), npc->classId, 1);
    SendTransaction(0x33, npc->guid, item->guid, item->mode, (uint32_t)price);
    lua_pushinteger(L, price);
    return 1;
}

// town.repair_all(npc) -> price
int TownRepairAll(lua_State* L) {
    Unit* player = RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    if (!UIOpen(ShopFlag)) throw Script::Error("no shop is open");
    if (!QuestRecord()) throw Script::Error("quest record not received yet");
    const int price = RepairAllPrice(nullptr, player, npc->classId, Difficulty(), QuestRecord(), nullptr);
    SendTransaction(0x35, npc->guid, 0, 0, 0x80000000u);
    lua_pushinteger(L, price);
    return 1;
}

#pragma pack(push, 1)
struct HireOffer {
    uint16_t nameId;
    uint16_t pad;
    int32_t seed;
    BOOL active;
    uint32_t unknown;
};
struct HireData {
    int32_t id, level, life, strength, dexterity, cost, experience, defense, minDamage, maxDamage, share;
    uint8_t rest[0x3A - 0x2C];
};
struct PetRecord {
    int32_t classId;      // 00
    int32_t type;         // 04 7 = mercenary
    uint32_t guid;        // 08
    uint32_t owner;       // 0C
    int32_t level;        // 10
    uint8_t pad14[0x1C - 0x14];
    int32_t lifePercent;  // 1C
    BOOL removed;         // 20
    uint8_t pad24[0x30 - 0x24];
    PetRecord* next;      // 30
};
#pragma pack(pop)
static_assert(sizeof(HireOffer) == 0x10 && sizeof(HireData) == 0x3A, "hire layouts");
static_assert(offsetof(PetRecord, next) == 0x30, "pet roster layout");

// town.hire_offers() -> { {index, name, cost, level, life}, ... } from the last hire list received.
int TownHireOffers(lua_State* L) {
    Unit* player = RequirePlayer();
    const HireOffer* offers = reinterpret_cast<const HireOffer*>(0x7C0C85);
    const BOOL expansion = *ExpansionGame;
    lua_newtable(L);
    int index = 0;
    for (int i = 0; i < 10; ++i) {
        if (!offers[i].active) continue;
        HireData data = {};
        const int act = GetMercAct(expansion, 0, offers[i].nameId);
        if (!RollMercStats(expansion, player, offers[i].seed, act, Difficulty(), &data)) continue;
        lua_newtable(L);
        Script::SetField(L, "index", i);
        const wchar_t* name = LocaleString(offers[i].nameId);
        Script::SetField(L, "name", name ? Script::Narrow(std::wstring(name, wcsnlen(name, 64))) : std::string());
        Script::SetField(L, "cost", data.cost);
        Script::SetField(L, "level", data.level);
        Script::SetField(L, "life", data.life);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

// town.hire(npc, offerIndex): hire an offer from town.hire_offers().
int TownHire(lua_State* L) {
    RequirePlayer();
    Unit* npc = ArgNpc(L, 1);
    const int index = Script::ArgInt(L, 2);
    const HireOffer* offers = reinterpret_cast<const HireOffer*>(0x7C0C85);
    if (index < 0 || index >= 10 || !offers[index].active) throw Script::Error("no such hire offer");
    uint8_t packet[9] = { 0x36 };
    const uint32_t nameId = offers[index].nameId;
    memcpy(packet + 1, &npc->guid, 4);
    memcpy(packet + 5, &nameId, 4);
    SendPacket(packet, sizeof(packet));
    return 0;
}

// town.mercenary() -> { id, class_id, level, life_percent, dead } for our hireling, or nil.
int TownMercenary(lua_State* L) {
    Unit* player = RequirePlayer();
    int guard = 0;
    for (const PetRecord* pet = *reinterpret_cast<PetRecord* const*>(0x7BB5BC); pet && guard++ < 64; pet = pet->next) {
        if (pet->type != 7 || pet->owner != player->guid) continue;
        lua_newtable(L);
        Script::SetField(L, "id", double(pet->guid));
        Script::SetField(L, "class_id", pet->classId);
        Script::SetField(L, "level", pet->level);
        Script::SetField(L, "life_percent", pet->lifePercent < 0 ? 0 : pet->lifePercent > 100 ? 100 : pet->lifePercent);
        Script::SetField(L, "dead", pet->removed != FALSE);
        return 1;
    }
    return 0;
}

const bool registered = (Script::RegisterModule(Script::InGame, [](lua_State* L) {
    static const luaL_Reg functions[] = {
        { "trade", Script::Bind<TownTrade> },
        { "close", Script::Bind<TownClose> },
        { "respec", Script::Bind<TownRespec> },
        { "price", Script::Bind<TownPrice> },
        { "buy", Script::Bind<TownBuy> },
        { "sell", Script::Bind<TownSell> },
        { "repair_all", Script::Bind<TownRepairAll> },
        { "hire_offers", Script::Bind<TownHireOffers> },
        { "hire", Script::Bind<TownHire> },
        { "mercenary", Script::Bind<TownMercenary> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "town");
}), true);

}

}
