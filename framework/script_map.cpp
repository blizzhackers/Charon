/**
 * Lua `map` module: level layout, exits and path finding from client data.
 *
 * A level's rooms are generated on the client from the game seed. Rooms that
 * are not loaded around the player are activated to copy their collision,
 * presets and warps, and stay resident, as MapReveal's are: releasing them
 * propagates reference drops to neighbouring rooms that the game or MapReveal
 * still use, which frees rooms out from under them. Snapshots are cached per
 * level for the current act; paths are planned on the union of the requested
 * levels with a one-cell clearance margin.
 */
#define NOMINMAX
#include "headers/common.h"
#include "headers/pathfinding.h"
#include "headers/script_collision.h"
#include "headers/remote.h"
#include "headers/script_game.h"
#include "headers/script_host.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace ScriptMap {

using namespace Game;

namespace {

const int Clearance = 1;
const int DefaultStep = 15;
const int DoorRadius = 2;
// Collision a closed door adds (0x806, 0x804 or 0x400), within BlockMask.
const uint16_t DoorBits = 0x0C00;
const uint16_t ObjectBit = 0x0400; // the collision an object adds
// Live collision that stops the player: the static mask plus monster occupancy
// (0x0100). Missing or unloaded rooms answer 0x27, which counts as blocked.
const uint32_t LiveMask = BlockMask | 0x0100;
// Live collision that stops missiles (walls); unloaded rooms answer 0x27 and block.
const uint32_t MissileMask = 0x0004;

struct RoomInfo {
    int x, y, width, height; // subtiles
};

struct PresetInfo {
    uint32_t type;
    int classId;
    int x, y;
};

struct DoorInfo {
    int classId;
    int x, y;
};

struct ExitInfo {
    int level;
    int x, y;
    bool warp; // tile warp (stairs, cave entrance) rather than an open border
};

struct LevelInfo {
    int id = 0;
    int x = 0, y = 0, width = 0, height = 0;
    std::vector<RoomInfo> rooms;
    std::vector<PresetInfo> presets;
    std::vector<ExitInfo> exits;
    std::vector<DoorInfo> doors;
    Pathing::Grid raw; // impassable cells before clearance inflation
    std::vector<uint8_t> doorOnly; // cell blocked only by door/object collision
};

struct Cache {
    void* act = nullptr;
    std::map<int, std::unique_ptr<LevelInfo>> levels;
    std::vector<int> gridLevels, tightLevels;
    Pathing::Grid grid;  // with a one-cell margin around obstacles
    Pathing::Grid tight; // exact, for narrow doorways and gates
} cache;

// The player's own level is authoritative; its DRLG (+0x1B4) lists the act's
// levels and points back to the act (+0x46C) that AddRoomData expects.
Level* PlayerLevel(Unit* player) {
    Room1* room = UnitRoom(player);
    return room && room->room2 ? room->room2->level : nullptr;
}

void* LevelDrlg(Level* level) {
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(level) + 0x1B4);
}

void* PlayerAct(Unit* player) {
    Level* level = PlayerLevel(player);
    void* drlg = level ? LevelDrlg(level) : nullptr;
    return drlg ? *reinterpret_cast<void**>(static_cast<uint8_t*>(drlg) + 0x46C) : nullptr;
}

void ResetIfActChanged(Unit* player) {
    if (cache.act != PlayerAct(player)) {
        cache = Cache();
        cache.act = PlayerAct(player);
    }
}

// Rows in the initialized Levels.txt table; generating a level outside it
// dereferences a missing LevelDefs row natively.
int LevelCount() {
    const uint8_t* data = *reinterpret_cast<uint8_t* const*>(0x744304);
    return data ? *reinterpret_cast<const int*>(data + 0xC5C) : 0;
}

Level* FindLevel(Unit* player, int id) {
    if (id <= 0 || id >= LevelCount()) throw Script::Error("level id out of range");
    if (D2::GetActNoFromLevelNumber(id) != player->act) throw Script::Error("level is in another act");
    Level* here = PlayerLevel(player);
    if (!here || !LevelDrlg(here) || !PlayerAct(player)) throw Script::Error("act layout unavailable");
    if (here->id == id) return here;
    Level* level = GetLevelAndAlloc(LevelDrlg(here), id);
    if (!level) throw Script::Error("level unavailable");
    // InitLevel resets the seed and must run only once per level.
    if (!level->firstRoom) InitLevel(level);
    if (!level->firstRoom) throw Script::Error("level has no rooms");
    return level;
}

void CopyCollision(LevelInfo& info, Room1* room, Unit* player) {
    const CollisionGrid* collision = room ? room->collision : nullptr;
    if (!collision || !collision->cells || collision->width <= 0 || collision->height <= 0) return;
    double playerX, playerY;
    Position(player, playerX, playerY);
    const int ownX = (int)std::floor(playerX), ownY = (int)std::floor(playerY);
    const bool ownLevel = info.id == UnitLevelId(player);
    for (int cy = 0; cy < collision->height; ++cy) {
        for (int cx = 0; cx < collision->width; ++cx) {
            const int x = collision->x + cx, y = collision->y + cy;
            if (!info.raw.Contains(x, y)) continue;
            const size_t index = info.raw.Index(x, y);
            const uint16_t bits = SnapshotCollision(collision->cells[size_t(cy) * collision->width + cx],
                ownLevel && x == ownX && y == ownY);
            info.raw.blocked[index] = bits ? 1 : 0;
            info.doorOnly[index] = bits && !(bits & ~DoorBits) ? 1 : 0;
            info.raw.level[index] = (uint16_t)info.id;
        }
    }
}

// Closed doors block in the snapshot; plan through their doorways and let
// scripts open them on the way (map.doors). Only cells blocked by nothing
// but door/object collision are cleared, so walls beside a door stay solid.
void OpenDoorways(LevelInfo& info) {
    for (const DoorInfo& door : info.doors) {
        for (int y = door.y - DoorRadius; y <= door.y + DoorRadius; ++y) {
            for (int x = door.x - DoorRadius; x <= door.x + DoorRadius; ++x) {
                if (info.raw.LevelAt(x, y) != info.id) continue;
                const size_t index = info.raw.Index(x, y);
                if (info.doorOnly[index]) info.raw.blocked[index] = 0;
            }
        }
    }
}

// Object collision as the room has it now, into a snapshot taken earlier:
// cells an object has come to block, and cells that only an object blocked
// and no longer does. Walls and the rest are left as copied. True on a change.
bool CopyObjects(LevelInfo& info, Room1* room) {
    const CollisionGrid* collision = room ? room->collision : nullptr;
    if (!collision || !collision->cells || collision->width <= 0 || collision->height <= 0) return false;
    bool changed = false;
    for (int cy = 0; cy < collision->height; ++cy) {
        for (int cx = 0; cx < collision->width; ++cx) {
            const int x = collision->x + cx, y = collision->y + cy;
            if (info.raw.LevelAt(x, y) != info.id) continue;
            const size_t index = info.raw.Index(x, y);
            const uint16_t bits = collision->cells[size_t(cy) * collision->width + cx] & BlockMask;
            if ((bits & ObjectBit) && !info.raw.blocked[index]) {
                info.raw.blocked[index] = 1;
                info.doorOnly[index] = !(bits & ~DoorBits) ? 1 : 0;
                changed = true;
            }
            else if (!bits && info.raw.blocked[index] && info.doorOnly[index]) {
                info.raw.blocked[index] = 0;
                info.doorOnly[index] = 0;
                changed = true;
            }
        }
    }
    return changed;
}

void CopyRoomData(LevelInfo& info, Room2* room) {
    const int originX = room->tileX * 5, originY = room->tileY * 5;
    for (PresetUnit* preset = room->presets; preset; preset = preset->next) {
        info.presets.push_back({ preset->type, preset->classId, originX + preset->localX, originY + preset->localY });
        if (preset->type == UnitType::Object && preset->classId >= 0 && preset->classId < 0x23D) {
            const uint8_t* txt = GetObjectsTxt(preset->classId);
            if (txt && txt[0x13A]) info.doors.push_back({ preset->classId, originX + preset->localX, originY + preset->localY });
        }
        if (preset->type != UnitType::Warp) continue;
        for (RoomTile* tile = room->tiles; tile; tile = tile->next) {
            if (!tile->lvlWarp || *tile->lvlWarp != preset->classId) continue;
            if (!tile->destination || !tile->destination->level) continue;
            info.exits.push_back({ tile->destination->level->id, originX + preset->localX, originY + preset->localY, true });
        }
    }
}

LevelInfo& LoadLevel(Unit* player, int id) {
    ResetIfActChanged(player);
    auto found = cache.levels.find(id);
    if (found != cache.levels.end()) return *found->second;

    Level* level = FindLevel(player, id);
    auto info = std::make_unique<LevelInfo>();
    info->id = id;
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (Room2* room = level->firstRoom; room; room = room->next) {
        minX = std::min(minX, room->tileX * 5);
        minY = std::min(minY, room->tileY * 5);
        maxX = std::max(maxX, (room->tileX + room->tileWidth) * 5);
        maxY = std::max(maxY, (room->tileY + room->tileHeight) * 5);
        info->rooms.push_back({ room->tileX * 5, room->tileY * 5, room->tileWidth * 5, room->tileHeight * 5 });
    }
    if (minX >= maxX || minY >= maxY || (int64_t)(maxX - minX) * (maxY - minY) > 16 * 1024 * 1024) {
        throw Script::Error("level bounds are invalid");
    }
    info->x = minX;
    info->y = minY;
    info->width = maxX - minX;
    info->height = maxY - minY;
    info->raw.Reset(minX, minY, info->width, info->height);
    info->doorOnly.assign(info->raw.blocked.size(), 0);

    void* act = PlayerAct(player);
    std::vector<int> adjacent;
    for (Room2* room = level->firstRoom; room; room = room->next) {
        if (!room->room1) AddRoomData(act, id, room->tileX, room->tileY, nullptr);
        CopyCollision(*info, room->room1, player);
        CopyRoomData(*info, room);
        for (int i = 0; i < room->nearCount && room->nearRooms; ++i) {
            Room2* neighbour = room->nearRooms[i];
            if (neighbour && neighbour->level && neighbour->level->id != id) adjacent.push_back(neighbour->level->id);
        }
    }
    OpenDoorways(*info);
    std::sort(adjacent.begin(), adjacent.end());
    adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
    for (int other : adjacent) {
        const bool known = std::any_of(info->exits.begin(), info->exits.end(), [other](const ExitInfo& e) { return e.level == other; });
        if (!known) info->exits.push_back({ other, 0, 0, false });
    }
    LevelInfo& result = *info;
    cache.levels[id] = std::move(info);
    return result;
}

// Union of the given levels' snapshots, cached by level set. Normally inflated
// by a one-cell margin; `exact` skips the margin, for passages (gates, narrow
// doorways) that the margin would close.
const Pathing::Grid& GridFor(Unit* player, std::vector<int> ids, bool exact = false) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    ResetIfActChanged(player);
    Pathing::Grid& cached = exact ? cache.tight : cache.grid;
    std::vector<int>& cachedLevels = exact ? cache.tightLevels : cache.gridLevels;
    if (ids == cachedLevels && !cached.blocked.empty()) return cached;
    std::vector<LevelInfo*> infos;
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (int id : ids) {
        LevelInfo& info = LoadLevel(player, id);
        infos.push_back(&info);
        minX = std::min(minX, info.x);
        minY = std::min(minY, info.y);
        maxX = std::max(maxX, info.x + info.width);
        maxY = std::max(maxY, info.y + info.height);
    }
    if ((int64_t)(maxX - minX) * (maxY - minY) > 32 * 1024 * 1024) throw Script::Error("combined levels are too large");
    Pathing::Grid grid;
    grid.Reset(minX, minY, maxX - minX, maxY - minY);
    for (LevelInfo* info : infos) {
        for (int y = info->y; y < info->y + info->height; ++y) {
            for (int x = info->x; x < info->x + info->width; ++x) {
                const size_t from = info->raw.Index(x, y);
                if (!info->raw.level[from]) continue;
                const size_t to = grid.Index(x, y);
                grid.blocked[to] = info->raw.blocked[from];
                grid.level[to] = info->raw.level[from];
            }
        }
    }
    if (!exact) grid.Inflate(Clearance);
    cached = std::move(grid);
    cachedLevels = ids;
    return cached;
}

// A level is copied with the rooms the map module activated itself, and the
// game puts an object's collision (a torch stand, a sarcophagus, a barrel)
// into a room only when it spawns the object there, as the player comes
// near. So the rooms around her are read again as she goes, and the merged
// grids dropped when an object has appeared or gone. Planned over the first
// copy, steps led into torch stands in the Den: accepted, and not walked.
const uint32_t RefreshFrames = 8;
void RefreshNear(Unit* player) {
    static uint32_t last = 0;
    const uint32_t frame = *ClientFrame;
    if (frame - last < RefreshFrames) return;
    last = frame;
    ResetIfActChanged(player);
    const auto found = cache.levels.find(UnitLevelId(player));
    Room1* here = UnitRoom(player);
    if (found == cache.levels.end() || !here) return;
    LevelInfo& info = *found->second;
    bool changed = CopyObjects(info, here);
    for (int i = 0; i < here->nearCount && here->nearRooms; ++i) {
        Room1* beside = here->nearRooms[i];
        if (beside && beside != here) changed = CopyObjects(info, beside) || changed;
    }
    if (!changed) return;
    OpenDoorways(info);
    cache.gridLevels.clear();
    cache.tightLevels.clear();
}

Unit* RequirePlayer() {
    Unit* player = Player();
    if (!player || !UnitLevelId(player)) throw Script::Error("not in game");
    RefreshNear(player);
    return player;
}

int OptLevel(lua_State* L, int index, Unit* player) {
    return lua_isnoneornil(L, index) ? UnitLevelId(player) : Script::ArgInt(L, index);
}

int OptionInt(lua_State* L, int index, const char* key, int fallback) {
    if (lua_type(L, index) != LUA_TTABLE) return fallback;
    Script::RawField(L, index, key);
    const int value = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

std::vector<int> OptionLevels(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TTABLE) return {};
    Script::RawField(L, index, "levels");
    std::vector<int> levels;
    if (lua_type(L, -1) == LUA_TTABLE) {
        for (int i = 1; i <= (int)lua_objlen(L, -1); ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_isnumber(L, -1)) levels.push_back((int)lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return levels;
}

Pathing::Point PlayerCell(Unit* player) {
    double x, y;
    Position(player, x, y);
    return { (int)std::floor(x), (int)std::floor(y) };
}

// Pushes waypoints (excluding the start) or nil + reason.
// {avoid = {{x=, y=, radius=}, ...}}: ground to keep off when another way exists.
std::vector<Pathing::Zone> OptionAvoid(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TTABLE) return {};
    Script::RawField(L, index, "avoid");
    std::vector<Pathing::Zone> zones;
    if (lua_type(L, -1) == LUA_TTABLE) {
        const int count = std::min(64, (int)lua_objlen(L, -1));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TTABLE) {
                Pathing::Zone zone;
                Script::RawField(L, -1, "x"); zone.x = lua_tonumber(L, -1); lua_pop(L, 1);
                Script::RawField(L, -1, "y"); zone.y = lua_tonumber(L, -1); lua_pop(L, 1);
                Script::RawField(L, -1, "radius"); zone.radius = std::min(40.0, lua_tonumber(L, -1)); lua_pop(L, 1);
                if (zone.radius > 0) zones.push_back(zone);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return zones;
}

// A way around the zones is taken when it is not much longer than the way
// through: a pack is not worth a walk around the whole level. The search is
// bounded as well, because a zone that closes the only passage would
// otherwise flood every cell on this side of it within the tick.
const size_t DetourExpansions = 150000;
bool WorthDetour(size_t detour, size_t direct) { return detour <= direct * 2 + 60; }

// `padded` is given for a path found by FindPathSqueezing on the exact `grid`.
int PushPath(lua_State* L, const Pathing::Grid& grid, Pathing::Point start, std::vector<Pathing::Point> cells, int step,
    const Pathing::Grid* padded = nullptr) {
    if (cells.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "no path");
        return 2;
    }
    const std::vector<Pathing::Point> waypoints = padded ? Pathing::SimplifySqueezing(grid, *padded, cells, step)
        : Pathing::Simplify(grid, cells, step);
    lua_createtable(L, (int)waypoints.size(), 0);
    int index = 0;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        lua_createtable(L, 0, 2);
        Script::SetField(L, "x", waypoints[i].x);
        Script::SetField(L, "y", waypoints[i].y);
        lua_rawseti(L, -2, ++index);
    }
    Script::SetField(L, "length", (int)cells.size());
    (void)start;
    return 1;
}

const Pathing::Grid* DepartureGrid(lua_State* L, Unit* player, const Pathing::Grid& exact, Pathing::Point actual, Pathing::Grid& departure) {
    if (exact.Passable(actual.x, actual.y)) return &exact;
    Room1* room = UnitRoom(player);
    const uint32_t collision = room ? CheckCollision(room, actual.x, actual.y, 0xFFFF) : 0xFFFF;
    // Native PATH_RemoveUnitCollision (0x649560 -> 0x64EC10) clears the player's
    // 0x80 footprint and 0x1000 center before searching; retain every other blocker.
    if (collision & (BlockMask & ~0x1000)) return nullptr;
    departure = exact;
    if (!Pathing::OpenMovementStart(departure, actual)) return nullptr;
    if (auto host = Script::HostFor(L)) host->Log("map: departing blocked cell (" + std::to_string(actual.x)
        + "," + std::to_string(actual.y) + "), live collision="
        + std::to_string(collision));
    return &departure;
}

// The margin can cover the cell we stand in (a stair landing beside a wall)
// while open ground lies next to it. Then only that cell is opened and the
// margin kept for the rest of the way. Planned on the exact grid instead, the
// whole route hugged the walls: from the stairs of one Tower cellar its first
// step led into a corner the game would not walk her into, the request was
// accepted and nothing moved, and the descent was given up in every game.
bool MarginDeparture(const Pathing::Grid& padded, Pathing::Point actual, Pathing::Grid& opened) {
    if (padded.Passable(actual.x, actual.y)) return false;
    opened = padded;
    return Pathing::OpenMovementStart(opened, actual);
}

int BlockedMovementStart(lua_State* L, Unit* player, Pathing::Point actual) {
    Room1* room = UnitRoom(player);
    lua_pushnil(L);
    lua_pushfstring(L, "player cannot leave cell (%d,%d), live collision=%d", actual.x, actual.y,
        room ? (int)CheckCollision(room, actual.x, actual.y, 0xFFFF) : -1);
    return 2;
}

// map.path(x, y [, {step=, levels={...}, avoid={{x=,y=,radius=},...}}]) -> waypoints [, true when it goes around every zone] | nil, reason
int MapPath(lua_State* L) {
    Unit* player = RequirePlayer();
    const Pathing::Point target = { (int)std::floor(Script::ArgNumber(L, 1)), (int)std::floor(Script::ArgNumber(L, 2)) };
    const int step = std::max(2, OptionInt(L, 3, "step", DefaultStep));
    std::vector<int> levels = OptionLevels(L, 3);
    levels.push_back(UnitLevelId(player));
    const Pathing::Grid& grid = GridFor(player, levels);
    if (grid.LevelAt(target.x, target.y) == 0) {
        // Include the target's level when it is a known neighbour.
        const LevelInfo& here = LoadLevel(player, UnitLevelId(player));
        for (const ExitInfo& exit : here.exits) {
            const LevelInfo& other = LoadLevel(player, exit.level);
            if (other.raw.LevelAt(target.x, target.y)) {
                levels.push_back(exit.level);
                break;
            }
        }
    }
    const Pathing::Grid& full = GridFor(player, levels);
    const Pathing::Point actual = PlayerCell(player);
    Pathing::Point start, goal;
    std::vector<Pathing::Point> cells;
    if (Pathing::MovementStart(full, actual, start) && Pathing::NearestPassable(full, target, 6, goal))
        cells = Pathing::FindPath(full, start, goal);
    const std::vector<Pathing::Zone> avoid = OptionAvoid(L, 3);
    if (!cells.empty() && !avoid.empty()) {
        Pathing::Grid around = full;
        if (Pathing::BlockZones(around, avoid, start, &goal)) {
            const std::vector<Pathing::Point> detour = Pathing::FindPath(around, start, goal, DetourExpansions);
            if (!detour.empty() && WorthDetour(detour.size(), cells.size())) {
                PushPath(L, around, start, detour, step);
                lua_pushboolean(L, 1);
                return 2;
            }
        }
    }
    if (cells.empty()) {
        Pathing::Grid opened;
        if (MarginDeparture(full, actual, opened) && Pathing::NearestPassable(opened, target, 6, goal)) {
            cells = Pathing::FindPath(opened, actual, goal);
            if (!cells.empty()) return PushPath(L, opened, actual, cells, step);
        }
        // The margin can close a narrow passage.
        const Pathing::Grid& exact = GridFor(player, levels, true);
        Pathing::Grid departure;
        const Pathing::Grid* movement = DepartureGrid(L, player, exact, actual, departure);
        if (!movement) return BlockedMovementStart(L, player, actual);
        Pathing::Point tightGoal;
        if (!Pathing::NearestPassable(*movement, target, 6, tightGoal)) {
            lua_pushnil(L);
            lua_pushstring(L, "target is not walkable");
            return 2;
        }
        cells = Pathing::FindPathSqueezing(*movement, full, actual,
            [tightGoal](int x, int y) { return x == tightGoal.x && y == tightGoal.y; }, tightGoal);
        return PushPath(L, *movement, actual, cells, step, &full);
    }
    return PushPath(L, full, start, cells, step);
}

// map.path_to_level(levelId [, {step=, avoid=}]) -> waypoints ending inside the level [, true] | nil, reason
int MapPathToLevel(lua_State* L) {
    Unit* player = RequirePlayer();
    const int target = Script::ArgInt(L, 1);
    const int step = std::max(2, OptionInt(L, 2, "step", DefaultStep));
    const int here = UnitLevelId(player);
    const Pathing::Grid& grid = GridFor(player, { here, target });
    const LevelInfo& info = LoadLevel(player, target);
    const Pathing::Point actual = PlayerCell(player);
    Pathing::Point start;
    // Aim a few cells past the border so the player is clearly inside.
    const Pathing::Point hint = { info.x + info.width / 2, info.y + info.height / 2 };
    auto inside = [&grid, target](int x, int y) {
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (grid.LevelAt(x + dx, y + dy) != target) return false;
            }
        }
        return true;
    };
    std::vector<Pathing::Point> cells;
    if (Pathing::MovementStart(grid, actual, start)) cells = Pathing::FindPath(grid, start, inside, hint);
    const std::vector<Pathing::Zone> avoid = OptionAvoid(L, 2);
    if (!cells.empty() && !avoid.empty()) {
        Pathing::Grid around = grid;
        if (Pathing::BlockZones(around, avoid, start, &cells.back())) {
            const std::vector<Pathing::Point> detour = Pathing::FindPath(around, start, inside, hint, DetourExpansions);
            if (!detour.empty() && WorthDetour(detour.size(), cells.size())) {
                PushPath(L, around, start, detour, step);
                lua_pushboolean(L, 1);
                return 2;
            }
        }
    }
    if (cells.empty()) {
        Pathing::Grid opened;
        if (MarginDeparture(grid, actual, opened)) {
            cells = Pathing::FindPath(opened, actual, inside, hint);
            if (!cells.empty()) return PushPath(L, opened, actual, cells, step);
        }
        // The margin can close narrow passages (gates): try the exact grid.
        const Pathing::Grid& exact = GridFor(player, { here, target }, true);
        Pathing::Grid departure;
        const Pathing::Grid* movement = DepartureGrid(L, player, exact, actual, departure);
        if (movement) {
            auto insideExact = [movement, target](int x, int y) {
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (movement->LevelAt(x + dx, y + dy) != target) return false;
                    }
                }
                return movement->Passable(x, y);
            };
            cells = Pathing::FindPathSqueezing(*movement, grid, actual, insideExact, hint);
            return PushPath(L, *movement, actual, cells, step, &grid);
        }
        return BlockedMovementStart(L, player, actual);
    }
    return PushPath(L, grid, start, cells, step);
}

int MapWalkable(lua_State* L) {
    Unit* player = RequirePlayer();
    const int x = (int)std::floor(Script::ArgNumber(L, 1)), y = (int)std::floor(Script::ArgNumber(L, 2));
    const Pathing::Grid& grid = GridFor(player, { UnitLevelId(player) });
    lua_pushboolean(L, grid.Passable(x, y));
    return 1;
}

int MapLineClear(lua_State* L) {
    Unit* player = RequirePlayer();
    const Pathing::Point a = { (int)std::floor(Script::ArgNumber(L, 1)), (int)std::floor(Script::ArgNumber(L, 2)) };
    const Pathing::Point b = { (int)std::floor(Script::ArgNumber(L, 3)), (int)std::floor(Script::ArgNumber(L, 4)) };
    const Pathing::Grid& grid = GridFor(player, { UnitLevelId(player) });
    lua_pushboolean(L, Pathing::LineClear(grid, a, b));
    return 1;
}

double RawNumber(lua_State* L, int index, const char* key, double fallback) {
    Script::RawField(L, index, key);
    const double value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

std::vector<Pathing::GroundHazard> ReadGroundHazards(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TTABLE) throw Script::Error("hazards must be a table");
    const size_t count = lua_objlen(L, index);
    if (count > 128) throw Script::Error("at most 128 ground hazards are supported");
    std::vector<Pathing::GroundHazard> hazards;
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, index, (int)i);
        if (lua_type(L, -1) != LUA_TTABLE) throw Script::Error("ground hazard must be a table");
        const double x = RawNumber(L, -1, "x", NAN), y = RawNumber(L, -1, "y", NAN);
        const double size = RawNumber(L, -1, "size", NAN);
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 65535 || std::abs(y) > 65535)
            throw Script::Error("ground hazard coordinates must be finite world coordinates");
        if (!std::isfinite(size) || size < 1 || size > 3 || size != std::floor(size))
            throw Script::Error("ground hazard size must be 1, 2 or 3");
        hazards.push_back({ x, y, (int)size });
        lua_pop(L, 1);
    }
    return hazards;
}

// map.hazard_clear(x1, y1, x2, y2, hazards) -> bool, including both endpoints.
int MapHazardClear(lua_State* L) {
    double points[4];
    for (int i = 0; i < 4; ++i) {
        points[i] = Script::ArgNumber(L, i+1);
        if (!std::isfinite(points[i]) || std::abs(points[i]) > 65535)
            throw Script::Error("ground hazard coordinates must be finite world coordinates");
    }
    const auto hazards = ReadGroundHazards(L, 5);
    lua_pushboolean(L, Pathing::GroundHazardClear(hazards, points[0], points[1], points[2], points[3]));
    return 1;
}

// map.safe_spot{ threats = { {x=, y=, weight=}, ... } [, target = {x=, y=}] [, range=7]
//                [, range_weight=0.15] [, radius=14] [, occupied=2.5] [, reach=9] [, heading={x=,y=}]
//                [, heading_weight=0] }
// Optional missiles = { {x, y, to_x, to_y}, ... } adds imminent flight lanes.
// Optional hazards = { {x, y, size}, ... } adds confirmed ground-fire footprints.
// -> { found, x, y, danger_here, danger_there, missile_here, missile_there,
//      path = { {x, y}, ... } }; found is false when no safer spot is reachable.
// Danger fades from each threat; cells beside a threat count as occupied and
// routes avoid passing near threats. See Pathing::FindSafeSpot.
int MapSafeSpot(lua_State* L) {
    Unit* player = RequirePlayer();
    if (lua_type(L, 1) != LUA_TTABLE) throw Script::Error("bad argument #1 (table expected)");
    Pathing::SafeSpotOptions options;
    options.radius = (int)RawNumber(L, 1, "radius", options.radius);
    options.occupied = RawNumber(L, 1, "occupied", options.occupied);
    options.reach = RawNumber(L, 1, "reach", options.reach);
    options.range = RawNumber(L, 1, "range", options.range);
    options.rangeWeight = RawNumber(L, 1, "range_weight", options.rangeWeight);
    options.lead = RawNumber(L, 1, "lead", options.lead);
    options.clearance = (int)RawNumber(L, 1, "clearance", options.clearance);
    if (options.radius < 2 || options.radius > 40) throw Script::Error("radius must be 2..40");
    if (!std::isfinite(options.rangeWeight) || options.rangeWeight < 0 || options.rangeWeight > 4)
        throw Script::Error("range_weight must be finite and 0..4");
    Script::RawField(L, 1, "heading");
    if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) != LUA_TTABLE) throw Script::Error("heading must be a table with finite numeric x and y");
        auto component = [&](const char* key) {
            Script::RawField(L, -1, key);
            if (lua_type(L, -1) != LUA_TNUMBER || !std::isfinite(lua_tonumber(L, -1)))
                throw Script::Error("heading must be a table with finite numeric x and y");
            const double value = lua_tonumber(L, -1);
            lua_pop(L, 1);
            return value;
        };
        options.headingX = component("x");
        options.headingY = component("y");
    }
    lua_pop(L, 1);
    options.headingWeight = RawNumber(L, 1, "heading_weight", options.headingWeight);
    if (!std::isfinite(options.headingWeight) || options.headingWeight < 0 || options.headingWeight > 1)
        throw Script::Error("heading_weight must be finite and 0..1");
    Script::RawField(L, 1, "target");
    if (lua_type(L, -1) == LUA_TTABLE) {
        options.hasTarget = true;
        options.targetX = RawNumber(L, -1, "x", 0);
        options.targetY = RawNumber(L, -1, "y", 0);
    }
    lua_pop(L, 1);
    std::vector<Pathing::Threat> threats;
    Script::RawField(L, 1, "threats");
    if (lua_type(L, -1) == LUA_TTABLE) {
        const int count = (int)lua_objlen(L, -1);
        for (int i = 1; i <= count && i <= 256; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TTABLE) {
                threats.push_back({ RawNumber(L, -1, "x", 0), RawNumber(L, -1, "y", 0), RawNumber(L, -1, "weight", 1) });
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    Script::RawField(L, 1, "missiles");
    if (lua_type(L, -1) == LUA_TTABLE) {
        const int count = (int)lua_objlen(L, -1);
        if (count > 64) throw Script::Error("at most 64 missile lanes are supported");
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) != LUA_TTABLE) throw Script::Error("missile lane must be a table");
            const Pathing::Missile missile = { RawNumber(L, -1, "x", NAN), RawNumber(L, -1, "y", NAN),
                RawNumber(L, -1, "to_x", NAN), RawNumber(L, -1, "to_y", NAN) };
            if (!std::isfinite(missile.x) || !std::isfinite(missile.y) || !std::isfinite(missile.toX) || !std::isfinite(missile.toY)
                || std::abs(missile.x) > 65535 || std::abs(missile.y) > 65535
                || std::abs(missile.toX) > 65535 || std::abs(missile.toY) > 65535)
                throw Script::Error("missile coordinates must be finite world coordinates");
            options.missiles.push_back(missile);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    Script::RawField(L, 1, "hazards");
    if (!lua_isnil(L, -1)) options.hazards = ReadGroundHazards(L, -1);
    lua_pop(L, 1);
    // The snapshot holds walls and objects as they were; the game's live
    // collision also has monster footprints and doors as they are now.
    Room1* room = UnitRoom(player);
    if (room) {
        options.liveBlocked = [room](int x, int y) { return CheckCollision(room, x, y, LiveMask) != 0; };
    }
    const Pathing::Grid& grid = GridFor(player, { UnitLevelId(player) });
    const Pathing::SafeSpot spot = Pathing::FindSafeSpot(grid, PlayerCell(player), threats, options);
    lua_createtable(L, 0, 7);
    Script::SetField(L, "found", spot.found);
    Script::SetField(L, "danger_here", spot.dangerHere);
    Script::SetField(L, "missile_here", spot.missileHere);
    Script::SetField(L, "hazard_here", spot.hazardHere);
    Script::SetField(L, "open_sectors", spot.openSectors);
    if (spot.found) {
        Script::SetField(L, "x", spot.spot.x);
        Script::SetField(L, "y", spot.spot.y);
        Script::SetField(L, "danger_there", spot.dangerThere);
        Script::SetField(L, "missile_there", spot.missileThere);
        Script::SetField(L, "hazard_there", spot.hazardThere);
    }
    lua_createtable(L, (int)spot.path.size(), 0);
    for (size_t i = 1; i < spot.path.size(); ++i) {
        lua_createtable(L, 0, 2);
        Script::SetField(L, "x", spot.path[i].x);
        Script::SetField(L, "y", spot.path[i].y);
        lua_rawseti(L, -2, (int)i);
    }
    lua_setfield(L, -2, "path");
    return 1;
}

// map.line_of_sight(x1, y1, x2, y2) -> bool: whether a missile can fly between
// two units, from the game's live collision (missile barrier bit 0x0004; walls
// and unloaded space block). The cells the two units stand on are not checked,
// unlike map.line_clear, which tests walkability with a safety margin.
int MapLineOfSight(lua_State* L) {
    Unit* player = RequirePlayer();
    const Pathing::Point a = { (int)std::floor(Script::ArgNumber(L, 1)), (int)std::floor(Script::ArgNumber(L, 2)) };
    const Pathing::Point b = { (int)std::floor(Script::ArgNumber(L, 3)), (int)std::floor(Script::ArgNumber(L, 4)) };
    Room1* room = UnitRoom(player);
    if (!room) throw Script::Error("player has no room");
    int x = a.x, y = a.y;
    const int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const int sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
    int error = dx - dy;
    bool clear = true;
    for (int steps = 0; (x != b.x || y != b.y) && steps < 256; ++steps) {
        const int e2 = 2 * error;
        if (e2 > -dy) {
            error -= dy;
            x += sx;
        }
        if (e2 < dx) {
            error += dx;
            y += sy;
        }
        if (x == b.x && y == b.y) break;
        if (CheckCollision(room, x, y, MissileMask)) {
            clear = false;
            break;
        }
    }
    lua_pushboolean(L, clear);
    return 1;
}

// map.rooms([levelId]) -> { {x, y, width, height, center_x, center_y}, ... }
// center_x/y is the passable cell nearest the room centre, if any.
int MapRooms(lua_State* L) {
    Unit* player = RequirePlayer();
    const int id = OptLevel(L, 1, player);
    const LevelInfo& info = LoadLevel(player, id);
    const Pathing::Grid& grid = GridFor(player, { id });
    lua_createtable(L, (int)info.rooms.size(), 0);
    int index = 0;
    for (const RoomInfo& room : info.rooms) {
        lua_createtable(L, 0, 6);
        Script::SetField(L, "x", room.x);
        Script::SetField(L, "y", room.y);
        Script::SetField(L, "width", room.width);
        Script::SetField(L, "height", room.height);
        Pathing::Point center;
        if (Pathing::NearestPassable(grid, { room.x + room.width / 2, room.y + room.height / 2 }, std::max(room.width, room.height) / 2, center)) {
            Script::SetField(L, "center_x", center.x);
            Script::SetField(L, "center_y", center.y);
        }
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int MapPresets(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, OptLevel(L, 1, player));
    static const char* types[UnitTypeCount] = { "player", "monster", "object", "missile", "item", "warp" };
    lua_createtable(L, (int)info.presets.size(), 0);
    int index = 0;
    for (const PresetInfo& preset : info.presets) {
        lua_createtable(L, 0, 4);
        Script::SetField(L, "type", preset.type < UnitTypeCount ? types[preset.type] : "unknown");
        Script::SetField(L, "class_id", preset.classId);
        Script::SetField(L, "x", preset.x);
        Script::SetField(L, "y", preset.y);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

// map.exits([levelId]) -> { {level=, kind='warp'|'border', x=, y=}, ... }
int MapExits(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, OptLevel(L, 1, player));
    lua_createtable(L, (int)info.exits.size(), 0);
    int index = 0;
    for (const ExitInfo& exit : info.exits) {
        lua_createtable(L, 0, 4);
        Script::SetField(L, "level", exit.level);
        Script::SetField(L, "kind", exit.warp ? "warp" : "border");
        if (exit.warp) {
            Script::SetField(L, "x", exit.x);
            Script::SetField(L, "y", exit.y);
        }
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

// map.doors([levelId]) -> { {class_id, x, y}, ... } door spawns; find the live
// door object near one to open it.
int MapDoors(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, OptLevel(L, 1, player));
    lua_createtable(L, (int)info.doors.size(), 0);
    int index = 0;
    for (const DoorInfo& door : info.doors) {
        lua_createtable(L, 0, 3);
        Script::SetField(L, "class_id", door.classId);
        Script::SetField(L, "x", door.x);
        Script::SetField(L, "y", door.y);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int MapBounds(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, OptLevel(L, 1, player));
    lua_pushinteger(L, info.x);
    lua_pushinteger(L, info.y);
    lua_pushinteger(L, info.width);
    lua_pushinteger(L, info.height);
    return 4;
}

// map.current_room() -> the player's room: subtile rectangle, tile rectangle and collision origin.
int MapCurrentRoom(lua_State* L) {
    Unit* player = RequirePlayer();
    Room1* room = UnitRoom(player);
    if (!room) return 0;
    lua_newtable(L);
    Script::SetField(L, "x", room->x);
    Script::SetField(L, "y", room->y);
    Script::SetField(L, "width", room->width);
    Script::SetField(L, "height", room->height);
    if (room->room2) {
        Script::SetField(L, "tile_x", room->room2->tileX);
        Script::SetField(L, "tile_y", room->room2->tileY);
        Script::SetField(L, "tile_width", room->room2->tileWidth);
        Script::SetField(L, "tile_height", room->room2->tileHeight);
        if (room->room2->level) Script::SetField(L, "level", room->room2->level->id);
    }
    if (room->collision) {
        Script::SetField(L, "collision_x", room->collision->x);
        Script::SetField(L, "collision_y", room->collision->y);
        Script::SetField(L, "collision_width", room->collision->width);
        Script::SetField(L, "collision_height", room->collision->height);
    }
    return 1;
}

// Exploration coverage per level: cells the player has had in sight. Kept apart
// from the layout cache so map.invalidate() (doors opened) does not forget it.
struct Coverage {
    void* act = nullptr;
    std::map<int, std::vector<uint8_t>> seen; // level id -> cells of LevelInfo::raw
} coverage;

std::vector<uint8_t>& SeenFor(Unit* player, const LevelInfo& info) {
    if (coverage.act != PlayerAct(player)) coverage = Coverage{ PlayerAct(player), {} };
    std::vector<uint8_t>& seen = coverage.seen[info.id];
    if (seen.size() != info.raw.blocked.size()) seen.assign(info.raw.blocked.size(), 0);
    return seen;
}

// map.explore_reset([levelId]): forget what has been seen in a level.
int MapExploreReset(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, OptLevel(L, 1, player));
    std::vector<uint8_t>& seen = SeenFor(player, info);
    std::fill(seen.begin(), seen.end(), uint8_t(0));
    return 0;
}

// map.explore_mark(x, y, radius): mark the cells of the player's level within
// `radius` that are in sight from (x, y) (walls block sight).
int MapExploreMark(lua_State* L) {
    Unit* player = RequirePlayer();
    const Pathing::Point from = { (int)std::floor(Script::ArgNumber(L, 1)), (int)std::floor(Script::ArgNumber(L, 2)) };
    const int radius = Script::ArgInt(L, 3);
    if (radius < 1 || radius > 40) throw Script::Error("radius must be 1..40");
    const LevelInfo& info = LoadLevel(player, UnitLevelId(player));
    std::vector<uint8_t>& seen = SeenFor(player, info);
    for (int y = from.y - radius; y <= from.y + radius; ++y) {
        for (int x = from.x - radius; x <= from.x + radius; ++x) {
            if (!info.raw.Contains(x, y) || (x - from.x) * (x - from.x) + (y - from.y) * (y - from.y) > radius * radius) continue;
            const size_t index = info.raw.Index(x, y);
            if (seen[index]) continue;
            // Walls stop sight; a blocked cell itself counts as seen when visible.
            if (Pathing::LineClear(info.raw, from, { x, y }) || info.raw.blocked[index]) seen[index] = 1;
        }
    }
    return 0;
}

// map.explore_mark_rooms() -> cells newly marked: the ground of the room the
// player stands in and of the rooms the game keeps beside it (Room1's near
// list). Those are the rooms whose units the client holds, so what stands in
// them is known without walking up to look: a search for monsters has
// nothing more to find there, whatever sight alone has covered.
int MapExploreMarkRooms(lua_State* L) {
    Unit* player = RequirePlayer();
    const LevelInfo& info = LoadLevel(player, UnitLevelId(player));
    std::vector<uint8_t>& seen = SeenFor(player, info);
    int marked = 0;
    auto mark = [&](const Room1* room) {
        if (!room || room->width <= 0 || room->height <= 0 || room->width > 400 || room->height > 400) return;
        for (int y = room->y; y < room->y + room->height; ++y) {
            for (int x = room->x; x < room->x + room->width; ++x) {
                if (info.raw.LevelAt(x, y) != info.id) continue;
                const size_t index = info.raw.Index(x, y);
                if (!seen[index]) { seen[index] = 1; ++marked; }
            }
        }
    };
    if (Room1* here = UnitRoom(player)) {
        mark(here);
        for (int i = 0; i < here->nearCount && here->nearRooms; ++i) mark(here->nearRooms[i]);
    }
    lua_pushinteger(L, marked);
    return 1;
}

// map.explore_next([minimum]) -> { x, y, steps, unseen, total } or nil when
// nothing reachable is left unseen. The target is the reachable walkable cell
// nearest by walking distance that is unseen and sits in a patch of at least
// `minimum` (default 6) unseen cells, so tiny corners do not cause detours.
// `unseen` / `total` count walkable cells of the level.
int MapExploreNext(lua_State* L) {
    Unit* player = RequirePlayer();
    const int minimum = Script::OptInt(L, 1, 6);
    const int id = UnitLevelId(player);
    const LevelInfo& info = LoadLevel(player, id);
    const std::vector<uint8_t>& seen = SeenFor(player, info);
    const Pathing::Grid& grid = GridFor(player, { id }, true);
    int total = 0, unseen = 0;
    for (size_t i = 0; i < info.raw.blocked.size(); ++i) {
        if (info.raw.blocked[i] || info.raw.level[i] != id) continue;
        ++total;
        if (!seen[i]) ++unseen;
    }
    auto unseenAt = [&](int x, int y) {
        return info.raw.Contains(x, y) && !info.raw.blocked[info.raw.Index(x, y)] && !seen[info.raw.Index(x, y)];
    };
    auto patch = [&](int x, int y) {
        int count = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) count += unseenAt(x + dx, y + dy) ? 1 : 0;
        }
        return count;
    };
    Pathing::Point start, target;
    int distance = 0;
    if (Pathing::NearestPassable(grid, PlayerCell(player), 4, start) &&
        Pathing::FindNearest(grid, start, (uint16_t)id,
            [&](int x, int y) { return unseenAt(x, y) && patch(x, y) >= minimum; }, target, distance)) {
        lua_createtable(L, 0, 5);
        Script::SetField(L, "x", target.x);
        Script::SetField(L, "y", target.y);
        Script::SetField(L, "steps", distance);
        Script::SetField(L, "unseen", unseen);
        Script::SetField(L, "total", total);
        return 1;
    }
    return 0;
}

// map.explore_choose{minimum=24, cell=12, targets={{id,x,y},...},
//                    exclude={{x,y},...}, heading_x=0, heading_y=0}
// Always returns coverage and found; one flood chooses a monster or unseen patch.
int MapExploreChoose(lua_State* L) {
    Unit* player = RequirePlayer();
    if (lua_type(L, 1) != LUA_TTABLE) throw Script::Error("bad argument #1 (table expected)");
    auto number = [&](int table, const char* key, double fallback, double low, double high, bool integer = false) {
        Script::RawField(L, table, key);
        const double value = lua_isnil(L, -1) ? fallback : Script::ArgNumber(L, -1);
        lua_pop(L, 1);
        if (!std::isfinite(value) || value < low || value > high || (integer && value != std::floor(value)))
            throw Script::Error(std::string(key) + " is outside the supported range");
        return value;
    };
    Pathing::ExploreOptions options;
    options.minimum = (int)number(1, "minimum", 24, 1, 1600, true);
    options.cell = (int)number(1, "cell", 12, 4, 40, true);
    options.headingX = number(1, "heading_x", 0, -65535, 65535);
    options.headingY = number(1, "heading_y", 0, -65535, 65535);
    auto readPoints = [&](const char* key, int limit, bool targets) {
        Script::RawField(L, 1, key);
        if (!lua_isnil(L, -1)) {
            if (lua_type(L, -1) != LUA_TTABLE) throw Script::Error(std::string(key) + " must be a table");
            const size_t count = lua_objlen(L, -1);
            if (count > size_t(limit)) throw Script::Error(std::string(key) + " has too many entries");
            for (size_t i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, (int)i);
                if (lua_type(L, -1) != LUA_TTABLE) throw Script::Error("exploration point must be a table");
                const Pathing::Point point = {
                    (int)std::floor(number(-1, "x", NAN, -65535, 65535)),
                    (int)std::floor(number(-1, "y", NAN, -65535, 65535)) };
                if (targets) options.targets.push_back({ (uint32_t)number(-1, "id", NAN, 0, UINT32_MAX, true), point });
                else options.exclude.push_back(point);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    };
    readPoints("targets", 512, true);
    readPoints("exclude", 64, false);
    LARGE_INTEGER before, after, frequency;
    QueryPerformanceCounter(&before);
    const int id = UnitLevelId(player);
    const LevelInfo& info = LoadLevel(player, id);
    const std::vector<uint8_t>& seen = SeenFor(player, info);
    const Pathing::Grid& grid = GridFor(player, { id }, true);
    // GridFor with one level preserves its raw grid bounds and indexing.
    Pathing::Point start;
    Pathing::ExploreChoice choice;
    if (Pathing::NearestPassable(grid, PlayerCell(player), 4, start))
        choice = Pathing::ChooseExploration(grid, start, (uint16_t)id, seen, options);
    QueryPerformanceCounter(&after);
    QueryPerformanceFrequency(&frequency);
    lua_createtable(L, 0, 10);
    Script::SetField(L, "found", choice.found);
    Script::SetField(L, "unseen", choice.unseen);
    Script::SetField(L, "total", choice.total);
    Script::SetField(L, "gain", choice.gain);
    Script::SetField(L, "steps", choice.steps);
    Script::SetField(L, "elapsed_ms", double(after.QuadPart - before.QuadPart) * 1000.0 / double(frequency.QuadPart));
    if (choice.found) {
        Script::SetField(L, "kind", choice.monster ? "monster" : "patch");
        if (choice.monster) Script::SetField(L, "id", double(choice.id));
        Script::SetField(L, "x", choice.point.x);
        Script::SetField(L, "y", choice.point.y);
    }
    return 1;
}

// map.explore_plan([cell]) -> { {x, y}, ... }, milliseconds: a sweep of the
// player's level from where the player stands (Pathing::PlanCoverage with
// blocks of `cell` subtiles, default 12), in walking order.
int MapExplorePlan(lua_State* L) {
    Unit* player = RequirePlayer();
    const int cell = Script::OptInt(L, 1, 12);
    if (cell < 4 || cell > 40) throw Script::Error("cell must be 4..40");
    const int id = UnitLevelId(player);
    const Pathing::Grid& grid = GridFor(player, { id }, true);
    Pathing::Point start;
    if (!Pathing::NearestPassable(grid, PlayerCell(player), 4, start)) return 0;
    LARGE_INTEGER before, after, frequency;
    QueryPerformanceCounter(&before);
    const std::vector<Pathing::Point> tour = Pathing::PlanCoverage(grid, start, cell, (uint16_t)id);
    QueryPerformanceCounter(&after);
    QueryPerformanceFrequency(&frequency);
    lua_createtable(L, (int)tour.size(), 0);
    for (size_t i = 0; i < tour.size(); ++i) {
        lua_createtable(L, 0, 2);
        Script::SetField(L, "x", tour[i].x);
        Script::SetField(L, "y", tour[i].y);
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_pushnumber(L, double(after.QuadPart - before.QuadPart) * 1000.0 / double(frequency.QuadPart));
    return 2;
}

// map.explore_seen(x, y) -> bool
int MapExploreSeen(lua_State* L) {
    Unit* player = RequirePlayer();
    const int x = (int)std::floor(Script::ArgNumber(L, 1)), y = (int)std::floor(Script::ArgNumber(L, 2));
    const LevelInfo& info = LoadLevel(player, UnitLevelId(player));
    const std::vector<uint8_t>& seen = SeenFor(player, info);
    lua_pushboolean(L, info.raw.Contains(x, y) && seen[info.raw.Index(x, y)] != 0);
    return 1;
}

int MapInvalidate(lua_State* L) {
    cache = Cache();
    (void)L;
    return 0;
}

template <typename T> size_t VectorBytes(const std::vector<T>& v) { return v.capacity() * sizeof(T); }

size_t GridBytes(const Pathing::Grid& grid) { return VectorBytes(grid.blocked) + VectorBytes(grid.level); }

// Heap held by the layout cache and coverage (rooms the game activated for them are game memory).
size_t CacheBytes() {
    size_t total = GridBytes(cache.grid) + GridBytes(cache.tight);
    for (const auto& [id, info] : cache.levels) {
        total += sizeof(LevelInfo) + GridBytes(info->raw) + VectorBytes(info->doorOnly) + VectorBytes(info->rooms)
            + VectorBytes(info->presets) + VectorBytes(info->exits) + VectorBytes(info->doors);
    }
    for (const auto& [id, seen] : coverage.seen) total += VectorBytes(seen);
    return total;
}

const bool reported = (Script::RegisterMemoryUse("map", CacheBytes), true);

const bool registered = (Script::RegisterModule(Script::InGame, [](lua_State* L) {
    // A fresh game script starts from fresh layouts and coverage.
    cache = Cache();
    coverage = Coverage();
    static const luaL_Reg functions[] = {
        { "path", Script::Bind<MapPath> },
        { "path_to_level", Script::Bind<MapPathToLevel> },
        { "walkable", Script::Bind<MapWalkable> },
        { "line_clear", Script::Bind<MapLineClear> },
        { "safe_spot", Script::Bind<MapSafeSpot> },
        { "hazard_clear", Script::Bind<MapHazardClear> },
        { "line_of_sight", Script::Bind<MapLineOfSight> },
        { "rooms", Script::Bind<MapRooms> },
        { "presets", Script::Bind<MapPresets> },
        { "exits", Script::Bind<MapExits> },
        { "doors", Script::Bind<MapDoors> },
        { "bounds", Script::Bind<MapBounds> },
        { "current_room", Script::Bind<MapCurrentRoom> },
        { "invalidate", Script::Bind<MapInvalidate> },
        { "explore_reset", Script::Bind<MapExploreReset> },
        { "explore_mark", Script::Bind<MapExploreMark> },
        { "explore_mark_rooms", Script::Bind<MapExploreMarkRooms> },
        { "explore_next", Script::Bind<MapExploreNext> },
        { "explore_choose", Script::Bind<MapExploreChoose> },
        { "explore_seen", Script::Bind<MapExploreSeen> },
        { "explore_plan", Script::Bind<MapExplorePlan> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "map");
}), true);

}

}
