/**
 * Lua `game` module: read-only inspection of the client's world.
 *
 * Units are returned as snapshot tables keyed by (type, id); scripts never hold
 * pointers, so a unit that disappears simply stops being returned. Only client
 * memory is read (see headers/script_game.h).
 */
#include "headers/common.h"
#include "headers/script_game.h"
#include "headers/script_host.h"
#include "headers/utilities.h"
#include <cmath>

namespace Game {

BOOL MovePlayerTo(Unit* player, int x, int y, int action) {
    // CLIENT_MovePlayerToPosition: EDI=player, CX=x, AX=y, stack(action), RET 4.
    const DWORD function = 0x461840;
    BOOL result;
    __asm {
        push edi
        mov edi, player
        mov ecx, x
        mov eax, y
        push action
        call function
        mov result, eax
        pop edi
    }
    return result;
}

void UseBeltSlot(Unit* player, int slot, bool toMercenary) {
    // ClickBeltRight: ECX=inventory, EDX=player, EAX=to mercenary, stack(slot), RET 4.
    const DWORD function = 0x498A90;
    Inventory* inventory = player->inventory;
    const DWORD merc = toMercenary ? 1 : 0;
    __asm {
        mov ecx, inventory
        mov edx, player
        mov eax, merc
        push slot
        call function
    }
}

void InteractWithUnit(Unit* player, Unit* target) {
    // PLAYER_InteractWithUnitByType: EAX=input context, stack(type, guid), RET 8.
    // The click-on-a-unit path: approaches when out of range, applies the
    // client's range and line-of-sight rules, then interacts.
    const DWORD function = 0x461DC0;
    InputContext context = {};
    context.flags = 0x01 | 0x04 | 0x40; // left button, initial press, run
    context.player = player;
    context.target = target;
    double x = 0, y = 0;
    Position(target, x, y);
    context.x = (int32_t)x;
    context.y = (int32_t)y;
    context.moveAction = 3;     // run to location
    context.unitMoveAction = 4; // run to unit
    context.skill = player->skills ? player->skills->left : nullptr;
    InputContext* pContext = &context;
    const uint32_t targetType = target->type, targetGuid = target->guid;
    __asm {
        mov eax, pContext
        push targetGuid
        push targetType
        call function
    }
}

const wchar_t* LocaleString(uint16_t id) {
    // GetLocaleString reads the id from the full ECX; pass it zero-extended.
    auto getter = reinterpret_cast<const wchar_t*(__fastcall*)(uint32_t)>(0x524A30);
    return getter(id);
}

void SendPacket(const void* bytes, size_t size) {
    clientSendPacket(reinterpret_cast<char*>(const_cast<void*>(bytes)), size);
}

Unit* Player() {
    Unit* player = *PlayerUnit;
    return player && player->path ? player : nullptr;
}

Unit* FindUnit(uint32_t type, uint32_t guid) {
    if (type >= UnitTypeCount) return nullptr;
    Unit* player = Player();
    if (type == UnitType::Player && player && player->guid == guid) return player;
    for (Unit* unit = SyncedUnits[type][guid & 127]; unit; unit = unit->hashNext) {
        if (unit->guid == guid && unit->type == type) return unit;
    }
    return nullptr;
}

static bool HasDynamicPath(const Unit* unit) {
    return unit->type == UnitType::Player || unit->type == UnitType::Monster || unit->type == UnitType::Missile;
}

bool Position(Unit* unit, double& x, double& y) {
    if (!unit || !unit->path) return false;
    if (HasDynamicPath(unit)) {
        const DynamicPath* path = static_cast<DynamicPath*>(unit->path);
        x = path->x / 65536.0;
        y = path->y / 65536.0;
    }
    else {
        const StaticPath* path = static_cast<StaticPath*>(unit->path);
        x = path->x;
        y = path->y;
    }
    return true;
}

Room1* UnitRoom(Unit* unit) {
    if (!unit || !unit->path) return nullptr;
    return HasDynamicPath(unit) ? static_cast<DynamicPath*>(unit->path)->room : static_cast<StaticPath*>(unit->path)->room;
}

int UnitLevelId(Unit* unit) {
    Room1* room = UnitRoom(unit);
    if (!room || !room->room2 || !room->room2->level) return 0;
    return room->room2->level->id;
}

}

namespace ScriptGame {

using namespace Game;

namespace {

const char* TypeNames[UnitTypeCount] = { "player", "monster", "object", "missile", "item", "warp" };
const char* ClassNames[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
const char* ItemLocations[7] = { "container", "equipped", "belt", "ground", "cursor", "dropping", "socketed" };
const char* UINames[UIFlagCount] = {
    "game", "inventory", "character", "skill_select", "skill_tree", "chat", "new_stats", "new_skills",
    "npc_menu", "escape_menu", "automap", "config", "npc_shop", "hold_alt", "repair", "quests",
    "quest_scroll", "quest_log", "no_draw", "hireables", "waypoint", "mini_panel", "party", "trade",
    "message_log", "stash", "cube", "steeg_stone", "guild_vault", "ui_29", "ui_30", "belt_rows",
    "ui_32", "help", "help_button", "hire_icons", "merc_inventory", "recipe_scroll",
};

enum Stat { StrengthStat = 0, EnergyStat = 1, DexterityStat = 2, VitalityStat = 3, StatPoints = 4, SkillPoints = 5,
    LifeStat = 6, MaxLifeStat = 7, ManaStat = 8, MaxManaStat = 9, StaminaStat = 10, MaxStaminaStat = 11,
    LevelStat = 12, ExperienceStat = 13, GoldStat = 14, StashGoldStat = 15 };

Unit* RequirePlayer() {
    Unit* player = Player();
    if (!player) throw Script::Error("not in game");
    return player;
}

uint32_t ArgType(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TSTRING) {
        const std::string name = Script::ArgString(L, index);
        for (uint32_t t = 0; t < UnitTypeCount; ++t) {
            if (name == TypeNames[t]) return t;
        }
        throw Script::Error("unknown unit type '" + name + "'");
    }
    const int type = Script::ArgInt(L, index);
    if (type < 0 || type >= UnitTypeCount) throw Script::Error("unit type out of range");
    return (uint32_t)type;
}

Unit* ArgUnit(lua_State* L, int index) {
    const uint32_t type = ArgType(L, index);
    const uint32_t guid = (uint32_t)Script::ArgNumber(L, index + 1);
    return FindUnit(type, guid);
}

std::string WideToUtf8(const wchar_t* text, size_t limit = 256) {
    return text ? Script::Narrow(std::wstring(text, wcsnlen(text, limit))) : std::string();
}

bool IsDead(const Unit* unit) {
    if (unit->flags & 0x10000) return true;
    if (unit->type == UnitType::Monster) return unit->mode == 0 || unit->mode == 12;
    if (unit->type == UnitType::Player) return unit->mode == 0 || unit->mode == 17;
    return false;
}

void PushMonster(lua_State* L, Unit* unit) {
    const MonsterData* data = static_cast<MonsterData*>(unit->data);
    const bool dead = IsDead(unit);
    Script::SetField(L, "alive", !dead);
    const int life = GetUnitStat(unit, LifeStat, 0), maxLife = GetUnitStat(unit, MaxLifeStat, 0);
    Script::SetField(L, "life", life);
    Script::SetField(L, "max_life", maxLife);
    Script::SetField(L, "life_percent", maxLife > 0 ? 100.0 * life / maxLife : 0.0);
    Script::SetField(L, "mercenary", (unit->flags & 0x200) != 0);
    Script::SetField(L, "attackable", !dead && (unit->flags & 4) != 0);
    if (!data) return;
    const uint8_t type = (uint8_t)data->typeFlags;
    Script::SetField(L, "superunique", (type & 0x02) != 0);
    Script::SetField(L, "champion", (type & 0x04) != 0);
    Script::SetField(L, "unique", (type & 0x08) != 0);
    Script::SetField(L, "minion", (type & 0x10) != 0);
    Script::SetField(L, "owner", double(data->ownerGuid));
    if (data->monStats) {
        const uint8_t flags0 = data->monStats[0x0C], flags1 = data->monStats[0x0D];
        Script::SetField(L, "boss", (flags0 & 0x40) != 0);
        Script::SetField(L, "npc", (flags1 & 0x01) != 0);
        Script::SetField(L, "interactable", (flags1 & 0x02) != 0);
        Script::SetField(L, "undead", (flags1 & 0x18) != 0);
        Script::SetField(L, "demon", (flags1 & 0x20) != 0);
        Script::SetField(L, "killable", (flags1 & 0x80) != 0);
        Script::SetField(L, "ranged", (data->monStats[0x0F] & 0x10) != 0);
    }
    lua_newtable(L);
    int count = 0;
    for (uint8_t enchant : data->enchants) {
        if (enchant) {
            lua_pushinteger(L, enchant);
            lua_rawseti(L, -2, ++count);
        }
    }
    lua_setfield(L, -2, "enchants");
}

// Objects.txt row facts. The game bounds class ids below 0x23D and marks waypoints with subclass 0x40.
void PushObjectTxt(lua_State* L, const uint8_t* txt) {
    Script::SetField(L, "code", std::string(reinterpret_cast<const char*>(txt), strnlen(reinterpret_cast<const char*>(txt), 64)));
    Script::SetField(L, "door", txt[0x13A] != 0);
    Script::SetField(L, "shrine", (txt[0x167] & 1) != 0);
    Script::SetField(L, "waypoint", (txt[0x167] & 0x40) != 0);
    Script::SetField(L, "subclass", int(txt[0x167]));
    Script::SetField(L, "operate", int(txt[0x1B3]));
    Script::SetField(L, "open_warp", txt[0x1B8] != 0);
}

void PushObject(lua_State* L, Unit* unit) {
    const uint8_t* data = static_cast<const uint8_t*>(unit->data);
    const uint8_t* txt = data ? *reinterpret_cast<uint8_t* const*>(data) : nullptr;
    if (txt) PushObjectTxt(L, txt);
    // Town portals (class 59 blue, 60 red): destination level and owner name.
    if (data && (unit->classId == 59 || unit->classId == 60)) {
        Script::SetField(L, "portal", true);
        Script::SetField(L, "destination", int(data[0x04]));
        const char* owner = reinterpret_cast<const char*>(data + 0x28);
        Script::SetField(L, "owner_name", std::string(owner, strnlen(owner, 16)));
    }
}

void PushItem(lua_State* L, Unit* unit) {
    const ItemData* data = static_cast<ItemData*>(unit->data);
    const uint8_t* txt = GetItemsTxt((uint32_t)unit->classId);
    if (txt) {
        char code[5] = { 0 };
        memcpy(code, txt + 0x80, 4);
        for (int i = 3; i >= 0 && code[i] == ' '; --i) code[i] = 0;
        Script::SetField(L, "code", code);
        Script::SetField(L, "width", int(txt[0x10F]));
        Script::SetField(L, "height", int(txt[0x110]));
        // ItemTypes.txt (0xE4-byte rows at data table +0xBF8, count +0xBFC):
        // staffmods (+0x1F) is the class whose skills the type can roll and
        // class (+0x21) restricts it to one class; 7 or more means none.
        const uint8_t* tables = *reinterpret_cast<uint8_t* const*>(0x744304);
        const uint8_t* types = tables ? *reinterpret_cast<uint8_t* const*>(tables + 0xBF8) : nullptr;
        const int typeCount = tables ? *reinterpret_cast<const int*>(tables + 0xBFC) : 0;
        for (int offset : { 0x11E, 0x120 }) {
            const int16_t type = *reinterpret_cast<const int16_t*>(txt + offset);
            if (!types || type < 0 || type >= typeCount) continue;
            const uint8_t* row = types + size_t(type) * 0xE4;
            if (row[0x1F] < 7) Script::SetField(L, "staff_mods", ClassNames[row[0x1F]]);
            if (row[0x21] < 7) Script::SetField(L, "class_only", ClassNames[row[0x21]]);
        }
    }
    Script::SetField(L, "location", unit->mode < 7 ? ItemLocations[unit->mode] : "unknown");
    if (!data) return;
    Script::SetField(L, "quality", int(data->quality));
    Script::SetField(L, "item_level", int(data->itemLevel));
    Script::SetField(L, "identified", (data->flags & 0x10) != 0);
    Script::SetField(L, "socketed", (data->flags & 0x800) != 0);
    Script::SetField(L, "ethereal", (data->flags & 0x400000) != 0);
    Script::SetField(L, "runeword", (data->flags & 0x4000000) != 0);
    Script::SetField(L, "page", int(data->page));
    Script::SetField(L, "body_location", int(data->bodyLocation));
    if (unit->mode != 3 && unit->path) {
        const StaticPath* path = static_cast<StaticPath*>(unit->path);
        Script::SetField(L, "grid_x", int(path->x));
        Script::SetField(L, "grid_y", int(path->y));
    }
}

void PushUnit(lua_State* L, Unit* unit) {
    lua_newtable(L);
    Script::SetField(L, "type", TypeNames[unit->type]);
    Script::SetField(L, "type_id", int(unit->type));
    Script::SetField(L, "class_id", int(unit->classId));
    Script::SetField(L, "id", double(unit->guid));
    Script::SetField(L, "mode", int(unit->mode));
    Script::SetField(L, "flags", double(unit->flags));
    double x, y;
    const bool carried = unit->type == UnitType::Item && unit->mode != 3;
    if (!carried && Position(unit, x, y)) {
        Script::SetField(L, "x", x);
        Script::SetField(L, "y", y);
    }
    switch (unit->type) {
    case UnitType::Player:
        if (unit->data) Script::SetField(L, "name", std::string(static_cast<char*>(unit->data), strnlen(static_cast<char*>(unit->data), 16)));
        if (unit->classId >= 0 && unit->classId < 7) Script::SetField(L, "class", ClassNames[unit->classId]);
        Script::SetField(L, "level", GetUnitStat(unit, LevelStat, 0));
        Script::SetField(L, "alive", !IsDead(unit));
        break;
    case UnitType::Monster:
        Script::SetField(L, "name", WideToUtf8(GetUnitName(unit)));
        PushMonster(L, unit);
        break;
    case UnitType::Object:
        Script::SetField(L, "name", WideToUtf8(GetUnitName(unit)));
        PushObject(L, unit);
        break;
    case UnitType::Item:
        Script::SetField(L, "name", WideToUtf8(GetUnitName(unit)));
        PushItem(L, unit);
        break;
    case UnitType::Missile:
        if (unit->flagsEx & 0x400) {
            Script::SetField(L, "owner_type", int(unit->ownerType));
            Script::SetField(L, "owner", double(unit->ownerGuid));
        }
        break;
    default:
        break;
    }
}

int GameReady(lua_State* L) {
    Unit* player = Player();
    lua_pushboolean(L, player && UnitLevelId(player) != 0);
    return 1;
}

int GameMe(lua_State* L) {
    Unit* player = RequirePlayer();
    PushUnit(L, player);
    Script::SetField(L, "area", UnitLevelId(player));
    Script::SetField(L, "act", int(player->act));
    Script::SetField(L, "in_town", IsTown(UnitLevelId(player)));
    Script::SetField(L, "difficulty", int(*Difficulty));
    Script::SetField(L, "expansion", *ExpansionGame != FALSE);
    Script::SetField(L, "life", GetUnitStat(player, LifeStat, 0) / 256.0);
    Script::SetField(L, "max_life", GetUnitStat(player, MaxLifeStat, 0) / 256.0);
    Script::SetField(L, "mana", GetUnitStat(player, ManaStat, 0) / 256.0);
    Script::SetField(L, "max_mana", GetUnitStat(player, MaxManaStat, 0) / 256.0);
    Script::SetField(L, "stamina", GetUnitStat(player, StaminaStat, 0) / 256.0);
    Script::SetField(L, "max_stamina", GetUnitStat(player, MaxStaminaStat, 0) / 256.0);
    Script::SetField(L, "experience", double((uint32_t)GetUnitStat(player, ExperienceStat, 0)));
    Script::SetField(L, "gold", GetUnitStat(player, GoldStat, 0));
    Script::SetField(L, "stash_gold", GetUnitStat(player, StashGoldStat, 0));
    Script::SetField(L, "stat_points", GetUnitStat(player, StatPoints, 0));
    Script::SetField(L, "skill_points", GetUnitStat(player, SkillPoints, 0));
    Script::SetField(L, "strength", GetUnitStat(player, StrengthStat, 0));
    Script::SetField(L, "dexterity", GetUnitStat(player, DexterityStat, 0));
    Script::SetField(L, "vitality", GetUnitStat(player, VitalityStat, 0));
    Script::SetField(L, "energy", GetUnitStat(player, EnergyStat, 0));
    if (player->skills) {
        Script::SetField(L, "left_skill", int(SkillId(player->skills->left)));
        Script::SetField(L, "right_skill", int(SkillId(player->skills->right)));
    }
    const Skill* current = player->skills ? player->skills->right : nullptr;
    Script::SetField(L, "can_act", PlayerCanBeInterrupted(player, const_cast<Skill*>(current)) != FALSE);
    Script::SetField(L, "cursor_item", player->inventory && player->inventory->cursorItem != nullptr);
    if (player->path) {
        const DynamicPath* path = static_cast<DynamicPath*>(player->path);
        Script::SetField(L, "target_x", int(path->targetX));
        Script::SetField(L, "target_y", int(path->targetY));
    }
    return 1;
}

int GameUnits(lua_State* L) {
    Unit* player = RequirePlayer();
    int first = 0, last = UnitTypeCount - 1;
    if (!lua_isnoneornil(L, 1)) first = last = (int)ArgType(L, 1);
    lua_newtable(L);
    int count = 0;
    for (int type = first; type <= last; ++type) {
        for (int bucket = 0; bucket < 128; ++bucket) {
            for (Unit* unit = SyncedUnits[type][bucket]; unit && count < 4096; unit = unit->hashNext) {
                if (unit == player || unit->type != (uint32_t)type) continue;
                PushUnit(L, unit);
                lua_rawseti(L, -2, ++count);
            }
        }
    }
    return 1;
}

int GameUnit(lua_State* L) {
    RequirePlayer();
    Unit* unit = ArgUnit(L, 1);
    if (!unit) return 0;
    PushUnit(L, unit);
    return 1;
}

int GameStat(lua_State* L) {
    RequirePlayer();
    Unit* unit = ArgUnit(L, 1);
    const int stat = Script::ArgInt(L, 3);
    const int layer = Script::OptInt(L, 4, 0);
    if (!unit) return 0;
    lua_pushinteger(L, GetUnitStat(unit, stat, (uint32_t)layer));
    return 1;
}

int GameBaseStat(lua_State* L) {
    RequirePlayer();
    Unit* unit = ArgUnit(L, 1);
    const int stat = Script::ArgInt(L, 3);
    const int layer = Script::OptInt(L, 4, 0);
    if (!unit || !unit->stats) return 0;
    lua_pushinteger(L, GetBaseStat(unit->stats, stat, (uint32_t)layer));
    return 1;
}

int GameState(lua_State* L) {
    RequirePlayer();
    Unit* unit = ArgUnit(L, 1);
    const int state = Script::ArgInt(L, 3);
    if (!unit || state < 0 || state > 0x1FF) return 0;
    if (unit->type != UnitType::Player && unit->type != UnitType::Monster && unit->type != UnitType::Missile) return 0;
    lua_pushboolean(L, GetUnitState(unit, state) != 0);
    return 1;
}

const uint8_t* SkillsTable(int& count) {
    const uint8_t* data = *reinterpret_cast<uint8_t* const*>(0x744304);
    count = data ? *reinterpret_cast<const int*>(data + 0xBA0) : 0;
    return data ? *reinterpret_cast<uint8_t* const*>(data + 0xB98) : nullptr;
}

std::string SkillName(int id) {
    auto getDescription = reinterpret_cast<const uint8_t*(__fastcall*)(int)>(0x4A89D0);
    const uint8_t* txt = reinterpret_cast<const uint8_t*>(GetSkillsTxtById(id));
    if (!txt || *reinterpret_cast<const int16_t*>(txt + 0x194) < 0) return "";
    const uint8_t* description = getDescription(id);
    return description ? WideToUtf8(LocaleString(*reinterpret_cast<const uint16_t*>(description + 0x08))) : "";
}

// Static Skills.txt facts about a skill; pushes nothing for unknown ids.
bool PushSkillInfo(lua_State* L, int id) {
    int count;
    const uint8_t* table = SkillsTable(count);
    if (!table || id < 0 || id >= count) return false;
    const uint8_t* txt = table + size_t(id) * 0x23C;
    lua_newtable(L);
    Script::SetField(L, "id", id);
    Script::SetField(L, "name", SkillName(id));
    Script::SetField(L, "class_id", int(static_cast<int8_t>(txt[0x0C])));
    Script::SetField(L, "required_level", int(*reinterpret_cast<const int16_t*>(txt + 0x174)));
    Script::SetField(L, "max_level", int(*reinterpret_cast<const int16_t*>(txt + 0x12C)));
    Script::SetField(L, "passive", (txt[0x04] & 0x10) != 0);
    Script::SetField(L, "aura", (txt[0x04] & 0x20) != 0);
    Script::SetField(L, "element", int(txt[0x1DC]));
    // aurastate: the state the skill puts on its caster (armors, shouts); 0 for none.
    Script::SetField(L, "state", int(*reinterpret_cast<const int16_t*>(txt + 0x80)));
    lua_newtable(L);
    int prerequisites = 0;
    for (int i = 0; i < 3; ++i) {
        const int16_t required = *reinterpret_cast<const int16_t*>(txt + 0x17E + i * 2);
        if (required >= 0) {
            lua_pushinteger(L, required);
            lua_rawseti(L, -2, ++prerequisites);
        }
    }
    lua_setfield(L, -2, "prerequisites");
    return true;
}

int GameSkillInfo(lua_State* L) {
    return PushSkillInfo(L, Script::ArgInt(L, 1)) ? 1 : 0;
}

// game.class_skills([classId]) -> skill_info tables for a class (default: the player's).
int GameClassSkills(lua_State* L) {
    const int classId = lua_isnoneornil(L, 1) ? RequirePlayer()->classId : Script::ArgInt(L, 1);
    int count;
    const uint8_t* table = SkillsTable(count);
    lua_newtable(L);
    int index = 0;
    for (int id = 0; table && id < count; ++id) {
        if (static_cast<int8_t>(table[size_t(id) * 0x23C + 0x0C]) != classId) continue;
        if (PushSkillInfo(L, id)) lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int GameSkills(lua_State* L) {
    Unit* player = RequirePlayer();
    lua_newtable(L);
    if (!player->skills) return 1;
    int count = 0;
    for (Skill* skill = player->skills->first; skill && count < 512; skill = skill->next) {
        const int id = SkillId(skill);
        const int level = GetSkillLevel(player, skill, TRUE);
        lua_newtable(L);
        Script::SetField(L, "id", id);
        Script::SetField(L, "name", SkillName(id));
        Script::SetField(L, "level", level);
        Script::SetField(L, "base_level", int(skill->baseLevel));
        Script::SetField(L, "owner", double(skill->ownerGuid));
        Script::SetField(L, "charges", int(skill->charges));
        Script::SetField(L, "quantity", int(skill->quantity));
        Script::SetField(L, "left", skill == player->skills->left);
        Script::SetField(L, "right", skill == player->skills->right);
        Script::SetField(L, "usable", CanUseSkill(player, skill) == 0);
        Script::SetField(L, "mana", level > 0 ? SkillManaCost(id, level) : 0);
        lua_rawseti(L, -2, ++count);
    }
    return 1;
}

int GameSkillDamage(lua_State* L) {
    Unit* player = RequirePlayer();
    const int id = Script::ArgInt(L, 1);
    Skill* skill = GetSkillByIdAndOwner(player, id, Script::OptInt(L, 3, -1));
    int level = Script::OptInt(L, 2, 0);
    if (level <= 0) level = skill ? GetSkillLevel(player, skill, TRUE) : 0;
    const uint8_t* txt = reinterpret_cast<const uint8_t*>(GetSkillsTxtById(id));
    if (!txt) throw Script::Error("unknown skill id");
    lua_newtable(L);
    Script::SetField(L, "level", level);
    if (level <= 0) return 1;
    Script::SetField(L, "physical_min", SkillPhysMin(player, id, level, TRUE) / 256.0);
    Script::SetField(L, "physical_max", SkillPhysMax(player, id, level, TRUE) / 256.0);
    Script::SetField(L, "elemental_min", SkillElemMin(player, id, level, TRUE) / 256.0);
    Script::SetField(L, "elemental_max", SkillElemMax(player, id, level, TRUE) / 256.0);
    Script::SetField(L, "element", int(txt[0x1DC]));
    Script::SetField(L, "to_hit_bonus", int(SkillToHit(player, id, level)));
    Script::SetField(L, "mana", SkillManaCost(id, level));
    return 1;
}

int GameAttackDamage(lua_State* L) {
    // Character-screen damage for the equipped weapon (SKILLDESC_CalcWeaponDamageRange
    // then SKILLDESC_CalcElemDamageRange), for the innate Attack skill.
    Unit* player = RequirePlayer();
    Skill* attack = GetSkillByIdAndOwner(player, 0, -1);
    int minDamage = 0, maxDamage = 0, color = 0;
    const DWORD weaponRange = 0x4E86F0;
    int* pMin = &minDamage;
    int* pMax = &maxDamage;
    int* pColor = &color;
    __asm {
        push 0          // separate weapon stats
        push 0          // weapon: equipped
        push 0x80       // full weapon damage
        push attack
        push 0          // flat bonus
        push pColor
        push pMax
        push pMin
        mov ecx, player
        xor eax, eax    // bonus percent
        call weaponRange
    }
    auto elemRange = reinterpret_cast<void(__stdcall*)(Unit*, int*, int*, int*, Skill*)>(0x4E89A0);
    elemRange(player, &minDamage, &maxDamage, &color, attack);
    lua_newtable(L);
    Script::SetField(L, "min", minDamage);
    Script::SetField(L, "max", maxDamage);
    return 1;
}

// game.items([unit]) -> items in the player's inventory (equipped, inventory,
// belt, stash, cube, cursor), or in another unit's, e.g. a vendor's stock.
int GameItems(lua_State* L) {
    Unit* player = RequirePlayer();
    Unit* owner = player;
    if (lua_type(L, 1) == LUA_TTABLE) {
        Script::RawField(L, 1, "type_id");
        Script::RawField(L, 1, "id");
        owner = FindUnit((uint32_t)lua_tonumber(L, -2), (uint32_t)lua_tonumber(L, -1));
        lua_pop(L, 2);
        if (!owner) throw Script::Error("unit not found");
    }
    lua_newtable(L);
    if (!owner->inventory) return 1;
    int count = 0;
    for (Unit* item = owner->inventory->firstItem; item && count < 1024;
        item = item->data ? static_cast<ItemData*>(item->data)->nextInInventory : nullptr) {
        PushUnit(L, item);
        lua_rawseti(L, -2, ++count);
    }
    if (Unit* cursor = owner == player ? player->inventory->cursorItem : nullptr) {
        PushUnit(L, cursor);
        lua_rawseti(L, -2, ++count);
    }
    return 1;
}

int GameUI(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        lua_newtable(L);
        for (int id = 1; id < UIFlagCount; ++id) {
            if (UIOpen(id)) Script::SetField(L, UINames[id], true);
        }
        return 1;
    }
    int id = -1;
    if (lua_type(L, 1) == LUA_TSTRING) {
        const std::string name = Script::ArgString(L, 1);
        for (int i = 0; i < UIFlagCount; ++i) {
            if (name == UINames[i]) id = i;
        }
        if (id < 0) throw Script::Error("unknown UI panel '" + name + "'");
    }
    else id = Script::ArgInt(L, 1);
    lua_pushboolean(L, UIOpen(id));
    return 1;
}

int GameArea(lua_State* L) {
    lua_pushinteger(L, UnitLevelId(RequirePlayer()));
    return 1;
}

const uint8_t* DataTable() {
    return *reinterpret_cast<uint8_t* const*>(0x744304);
}

template <class T> const T* TxtTable(int pointerOffset, int countOffset, int& count) {
    const uint8_t* data = DataTable();
    count = data ? *reinterpret_cast<const int*>(data + countOffset) : 0;
    return data ? *reinterpret_cast<T* const*>(data + pointerOffset) : nullptr;
}

const uint8_t* MonStatsRow(int classId) {
    int count;
    const uint8_t* table = TxtTable<uint8_t>(0xA78, 0xA80, count);
    return table && classId >= 0 && classId < count ? table + size_t(classId) * 0x1A8 : nullptr;
}

int CurrentDifficulty() {
    return *Game::Difficulty > 2 ? 2 : *Game::Difficulty;
}

// Monster life and experience at a monster level, as D2Common scales them with
// MonLvl.txt: the expansion columns follow the classic ones; noRatio rows are raw.
void ScaledMonsterStats(const uint8_t* row, int monsterLevel, int& minLife, int& maxLife, int& experience) {
    const int difficulty = CurrentDifficulty();
    const int16_t* minHp = reinterpret_cast<const int16_t*>(row + 0x0B0);
    const int16_t* maxHp = reinterpret_cast<const int16_t*>(row + 0x0B6);
    const int16_t* exp = reinterpret_cast<const int16_t*>(row + 0x0D4);
    minLife = minHp[difficulty];
    maxLife = maxHp[difficulty];
    experience = exp[difficulty];
    if (row[0x0C] & 0x04) return;
    int count;
    const int32_t* monLvl = TxtTable<int32_t>(0xB70, 0xB74, count);
    if (!monLvl || count <= 0) return;
    const int level = monsterLevel < 0 ? 0 : monsterLevel >= count ? count - 1 : monsterLevel;
    const int32_t* entry = monLvl + size_t(level) * (0x78 / 4);
    const int column = (*Game::ExpansionGame ? 3 : 0) + difficulty;
    minLife = int(int64_t(entry[0x30 / 4 + column]) * minLife / 100);
    maxLife = int(int64_t(entry[0x30 / 4 + column]) * maxLife / 100);
    experience = int(int64_t(entry[0x60 / 4 + column]) * experience / 100);
}

// game.monster_info(classId [, monsterLevel]) -> MonStats facts for the current
// difficulty, with life and experience scaled to the level (default: the
// monster's own Normal level, or `monsterLevel`).
int GameMonsterInfo(lua_State* L) {
    const int classId = Script::ArgInt(L, 1);
    const uint8_t* row = MonStatsRow(classId);
    if (!row) return 0;
    const int difficulty = CurrentDifficulty();
    const int ownLevel = reinterpret_cast<const int16_t*>(row + 0x0AA)[difficulty];
    const int level = Script::OptInt(L, 2, ownLevel);
    int minLife, maxLife, experience;
    ScaledMonsterStats(row, level, minLife, maxLife, experience);
    lua_newtable(L);
    Script::SetField(L, "class_id", classId);
    Script::SetField(L, "name", WideToUtf8(LocaleString(*reinterpret_cast<const uint16_t*>(row + 0x06))));
    Script::SetField(L, "level", level);
    Script::SetField(L, "own_level", ownLevel);
    Script::SetField(L, "min_life", minLife);
    Script::SetField(L, "max_life", maxLife);
    Script::SetField(L, "experience", experience);
    static const char* resistNames[6] = { "physical", "magic", "fire", "lightning", "cold", "poison" };
    lua_newtable(L);
    for (int i = 0; i < 6; ++i) {
        Script::SetField(L, resistNames[i], int(reinterpret_cast<const int16_t*>(row + 0x144 + i * 6)[difficulty]));
    }
    lua_setfield(L, -2, "resist");
    Script::SetField(L, "boss", (row[0x0C] & 0x40) != 0);
    Script::SetField(L, "npc", (row[0x0D] & 0x01) != 0);
    Script::SetField(L, "undead", (row[0x0D] & 0x18) != 0);
    Script::SetField(L, "demon", (row[0x0D] & 0x20) != 0);
    Script::SetField(L, "killable", (row[0x0D] & 0x80) != 0);
    Script::SetField(L, "speed", int(*reinterpret_cast<const int16_t*>(row + 0x032)));
    return 1;
}

void PushClassList(lua_State* L, const int16_t* ids, int length, const char* field) {
    int count;
    TxtTable<uint8_t>(0xA78, 0xA80, count);
    lua_newtable(L);
    int index = 0;
    for (int i = 0; i < length; ++i) {
        if (ids[i] >= 0 && ids[i] < count) {
            lua_pushinteger(L, ids[i]);
            lua_rawseti(L, -2, ++index);
        }
    }
    lua_setfield(L, -2, field);
}

// game.level_info(levelId) -> Levels.txt facts for the current difficulty.
int GameLevelInfo(lua_State* L) {
    const int id = Script::ArgInt(L, 1);
    int count;
    const uint8_t* table = TxtTable<uint8_t>(0xC58, 0xC5C, count);
    if (!table || id <= 0 || id >= count) return 0;
    const uint8_t* row = table + size_t(id) * 0x220;
    const int difficulty = CurrentDifficulty();
    lua_newtable(L);
    Script::SetField(L, "id", id);
    Script::SetField(L, "name", WideToUtf8(reinterpret_cast<const wchar_t*>(row + 0x16E), 40));
    Script::SetField(L, "act", int(row[0x03]));
    const int16_t* levels = reinterpret_cast<const int16_t*>(row + (*Game::ExpansionGame ? 0x16 : 0x10));
    Script::SetField(L, "monster_level", int(levels[difficulty]));
    Script::SetField(L, "density", reinterpret_cast<const int32_t*>(row + 0x1C)[difficulty]);
    Script::SetField(L, "unique_packs_min", int(static_cast<int8_t>(row[0x28 + difficulty])));
    Script::SetField(L, "unique_packs_max", int(static_cast<int8_t>(row[0x2B + difficulty])));
    Script::SetField(L, "waypoint", row[0xE4] != 0xFF);
    Script::SetField(L, "inside", row[0x08] != 0);
    // Normal spawns from mon1..25; Nightmare and Hell from nmon1..25.
    PushClassList(L, reinterpret_cast<const int16_t*>(row + (difficulty ? 0x68 : 0x36)), 25, "monsters");
    PushClassList(L, reinterpret_cast<const int16_t*>(row + 0x9A), 25, "unique_monsters");
    return 1;
}

// The client's copy of the local player's quest record: 48 little-endian WORDs,
// one per quest. Bit 0 completed (reward collected), bit 1 reward waiting,
// bit 13 completed in this game, bit 14 cannot complete in this game.
const uint16_t* QuestWords() {
    const uint8_t* buffer = *reinterpret_cast<uint8_t* const*>(0x7C0D43);
    return buffer ? *reinterpret_cast<uint16_t* const*>(buffer) : nullptr;
}

void PushQuest(lua_State* L, uint16_t bits) {
    lua_newtable(L);
    Script::SetField(L, "bits", int(bits));
    Script::SetField(L, "completed", (bits & 0x0001) != 0);
    Script::SetField(L, "reward_pending", (bits & 0x0002) != 0);
    Script::SetField(L, "completed_this_game", (bits & 0x2000) != 0);
    Script::SetField(L, "blocked_this_game", (bits & 0x4000) != 0);
    Script::SetField(L, "started", (bits & 0x0FFF) != 0);
}

// game.quest(id) -> quest state table, or nil before the record arrives.
// Act 1: 1 Den of Evil, 2 Sisters' Burial Grounds, 3 Tools of the Trade,
// 4 Search for Cain, 5 Forgotten Tower, 6 Sisters to the Slaughter.
int GameQuest(lua_State* L) {
    const int id = Script::ArgInt(L, 1);
    const uint16_t* words = QuestWords();
    if (!words || id < 0 || id >= 48) return 0;
    PushQuest(L, words[id]);
    return 1;
}

// game.waypoints() -> { [levelId] = true, ... } waypoints activated on this
// difficulty, as of the last waypoint panel opened (nil before any).
int GameWaypoints(lua_State* L) {
    const uint8_t* history = *reinterpret_cast<uint8_t* const*>(0x7BF081);
    if (!history) return 0;
    auto levelToBit = reinterpret_cast<BOOL(__stdcall*)(int levelId, int16_t* out)>(0x660E00);
    auto checkBit = reinterpret_cast<uint32_t(__stdcall*)(const void* history, uint16_t bit)>(0x660E50);
    int count;
    TxtTable<uint8_t>(0xC58, 0xC5C, count);
    lua_newtable(L);
    for (int level = 1; level < count && level < 0x100; ++level) {
        int16_t bit = -1;
        if (levelToBit(level, &bit) && bit >= 0 && bit < 112 && checkBit(history, (uint16_t)bit)) {
            lua_pushboolean(L, 1);
            lua_rawseti(L, -2, level);
        }
    }
    return 1;
}

// game.difficulty_info() -> facts from DifficultyLevels.txt for the current difficulty.
int GameDifficultyInfo(lua_State* L) {
    auto getLevels = reinterpret_cast<const uint8_t*(__stdcall*)(int)>(0x611D30);
    const uint8_t* row = getLevels(CurrentDifficulty());
    lua_newtable(L);
    Script::SetField(L, "difficulty", CurrentDifficulty());
    if (row) {
        // Classic games ignore the table and let Static Field drain to 0.
        Script::SetField(L, "static_field_min", *Game::ExpansionGame ? *reinterpret_cast<const int32_t*>(row + 0x40) : 0);
    }
    return 1;
}

// game.object_info(classId) -> Objects.txt facts for a class id (for presets), or nil.
int GameObjectInfo(lua_State* L) {
    const int classId = Script::ArgInt(L, 1);
    if (classId < 0 || classId >= 0x23D) return 0;
    const uint8_t* txt = GetObjectsTxt(classId);
    if (!txt) return 0;
    lua_newtable(L);
    Script::SetField(L, "class_id", classId);
    PushObjectTxt(L, txt);
    return 1;
}

int GameLevelName(lua_State* L) {
    const int id = Script::ArgInt(L, 1);
    const uint8_t* txt = id > 0 && id < 0x400 ? GetLevelsTxt(id) : nullptr;
    if (!txt) return 0;
    lua_pushstring(L, WideToUtf8(reinterpret_cast<const wchar_t*>(txt + 0x16E), 40).c_str());
    return 1;
}

int GameLocale(lua_State* L) {
    const int id = Script::ArgInt(L, 1);
    if (id < 0 || id > 0xFFFF) throw Script::Error("string id out of range");
    lua_pushstring(L, WideToUtf8(LocaleString((uint16_t)id), 1024).c_str());
    return 1;
}

int GameScreen(lua_State* L) {
    // World subtile -> screen pixel, for drawing or click fallbacks.
    int x = (int)std::floor(Script::ArgNumber(L, 1));
    int y = (int)std::floor(Script::ArgNumber(L, 2));
    CoordsMapToScreen(&x, &y);
    const int cameraX = reinterpret_cast<int(*)()>(0x45AFC0)();
    const int cameraY = reinterpret_cast<int(*)()>(0x45AFB0)();
    lua_pushinteger(L, x - cameraX);
    lua_pushinteger(L, y - cameraY);
    return 2;
}

const bool registered = (Script::RegisterModule(Script::InGame, [](lua_State* L) {
    static const luaL_Reg functions[] = {
        { "ready", Script::Bind<GameReady> },
        { "me", Script::Bind<GameMe> },
        { "units", Script::Bind<GameUnits> },
        { "unit", Script::Bind<GameUnit> },
        { "stat", Script::Bind<GameStat> },
        { "base_stat", Script::Bind<GameBaseStat> },
        { "state", Script::Bind<GameState> },
        { "skills", Script::Bind<GameSkills> },
        { "skill_damage", Script::Bind<GameSkillDamage> },
        { "skill_info", Script::Bind<GameSkillInfo> },
        { "class_skills", Script::Bind<GameClassSkills> },
        { "attack_damage", Script::Bind<GameAttackDamage> },
        { "items", Script::Bind<GameItems> },
        { "ui", Script::Bind<GameUI> },
        { "area", Script::Bind<GameArea> },
        { "level_name", Script::Bind<GameLevelName> },
        { "object_info", Script::Bind<GameObjectInfo> },
        { "monster_info", Script::Bind<GameMonsterInfo> },
        { "level_info", Script::Bind<GameLevelInfo> },
        { "quest", Script::Bind<GameQuest> },
        { "waypoints", Script::Bind<GameWaypoints> },
        { "difficulty_info", Script::Bind<GameDifficultyInfo> },
        { "locale", Script::Bind<GameLocale> },
        { "to_screen", Script::Bind<GameScreen> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "game");
}), true);

}

}
