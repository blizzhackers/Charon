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

// PLAYER_COLLISION_DEFAULT: walls, no-player areas, objects, doors and no-path cells.
const uint16_t BlockMask = 0x1C09;
const int Clearance = 1;
const int DefaultStep = 15;
const int DoorRadius = 2;
// Collision a closed door adds (0x806, 0x804 or 0x400), within BlockMask.
const uint16_t DoorBits = 0x0C00;
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

void CopyCollision(LevelInfo& info, Room1* room) {
    const CollisionGrid* collision = room ? room->collision : nullptr;
    if (!collision || !collision->cells || collision->width <= 0 || collision->height <= 0) return;
    for (int cy = 0; cy < collision->height; ++cy) {
        for (int cx = 0; cx < collision->width; ++cx) {
            const int x = collision->x + cx, y = collision->y + cy;
            if (!info.raw.Contains(x, y)) continue;
            const size_t index = info.raw.Index(x, y);
            const uint16_t bits = collision->cells[size_t(cy) * collision->width + cx] & BlockMask;
            info.raw.blocked[index] = bits ? 1 : 0;
            info.doorOnly[index] = bits && !(bits & ~DoorBits) ? 1 : 0;
            info.raw.level[index] = (uint16_t)info.id;
        }
    }
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
        CopyCollision(*info, room->room1);
        CopyRoomData(*info, room);
        for (int i = 0; i < room->nearCount && room->nearRooms; ++i) {
            Room2* neighbour = room->nearRooms[i];
            if (neighbour && neighbour->level && neighbour->level->id != id) adjacent.push_back(neighbour->level->id);
        }
    }
    // Closed doors block in the snapshot; plan through their doorways and let
    // scripts open them on the way (map.doors). Only cells blocked by nothing
    // but door/object collision are cleared, so walls beside a door stay solid.
    for (const DoorInfo& door : info->doors) {
        for (int y = door.y - DoorRadius; y <= door.y + DoorRadius; ++y) {
            for (int x = door.x - DoorRadius; x <= door.x + DoorRadius; ++x) {
                if (info->raw.LevelAt(x, y) != id) continue;
                const size_t index = info->raw.Index(x, y);
                if (info->doorOnly[index]) info->raw.blocked[index] = 0;
            }
        }
    }
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

Unit* RequirePlayer() {
    Unit* player = Player();
    if (!player || !UnitLevelId(player)) throw Script::Error("not in game");
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
int PushPath(lua_State* L, const Pathing::Grid& grid, Pathing::Point start, std::vector<Pathing::Point> cells, int step) {
    if (cells.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "no path");
        return 2;
    }
    const std::vector<Pathing::Point> waypoints = Pathing::Simplify(grid, cells, step);
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

bool StartCell(const Pathing::Grid& grid, Unit* player, Pathing::Point& start) {
    // The clearance margin can cover the player's own cell near walls.
    return Pathing::NearestPassable(grid, PlayerCell(player), 4, start);
}

// map.path(x, y [, {step=, levels={...}}]) -> waypoints | nil, reason
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
    Pathing::Point start, goal;
    if (!StartCell(full, player, start)) {
        lua_pushnil(L);
        lua_pushstring(L, "player is not on a walkable cell");
        return 2;
    }
    if (!Pathing::NearestPassable(full, target, 6, goal)) {
        lua_pushnil(L);
        lua_pushstring(L, "target is not walkable");
        return 2;
    }
    std::vector<Pathing::Point> cells = Pathing::FindPath(full, start, goal);
    if (cells.empty()) {
        // The margin can close narrow passages: try the exact grid before giving up.
        const Pathing::Grid& exact = GridFor(player, levels, true);
        Pathing::Point tightStart, tightGoal;
        if (StartCell(exact, player, tightStart) && Pathing::NearestPassable(exact, target, 6, tightGoal)) {
            cells = Pathing::FindPath(exact, tightStart, tightGoal);
            if (!cells.empty()) return PushPath(L, exact, tightStart, cells, step);
        }
    }
    return PushPath(L, full, start, cells, step);
}

// map.path_to_level(levelId [, {step=}]) -> waypoints ending inside the level | nil, reason
int MapPathToLevel(lua_State* L) {
    Unit* player = RequirePlayer();
    const int target = Script::ArgInt(L, 1);
    const int step = std::max(2, OptionInt(L, 2, "step", DefaultStep));
    const int here = UnitLevelId(player);
    const Pathing::Grid& grid = GridFor(player, { here, target });
    const LevelInfo& info = LoadLevel(player, target);
    Pathing::Point start;
    if (!StartCell(grid, player, start)) {
        lua_pushnil(L);
        lua_pushstring(L, "player is not on a walkable cell");
        return 2;
    }
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
    std::vector<Pathing::Point> cells = Pathing::FindPath(grid, start, inside, hint);
    if (cells.empty()) {
        // The margin can close narrow passages (gates): try the exact grid.
        const Pathing::Grid& exact = GridFor(player, { here, target }, true);
        Pathing::Point tightStart;
        if (StartCell(exact, player, tightStart)) {
            auto insideExact = [&exact, target](int x, int y) {
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (exact.LevelAt(x + dx, y + dy) != target) return false;
                    }
                }
                return exact.Passable(x, y);
            };
            cells = Pathing::FindPath(exact, tightStart, insideExact, hint);
            if (!cells.empty()) return PushPath(L, exact, tightStart, cells, step);
        }
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

// map.safe_spot{ threats = { {x=, y=, weight=}, ... } [, target = {x=, y=}] [, range=7]
//                [, radius=14] [, occupied=2.5] [, reach=9] }
// -> { x, y, danger_here, danger_there, path = { {x, y}, ... } } or nil when no
// reachable spot nearby is safer than standing still (trapped: fight).
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
    options.lead = RawNumber(L, 1, "lead", options.lead);
    options.clearance = (int)RawNumber(L, 1, "clearance", options.clearance);
    if (options.radius < 2 || options.radius > 40) throw Script::Error("radius must be 2..40");
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
    Script::SetField(L, "open_sectors", spot.openSectors);
    if (spot.found) {
        Script::SetField(L, "x", spot.spot.x);
        Script::SetField(L, "y", spot.spot.y);
        Script::SetField(L, "danger_there", spot.dangerThere);
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
    const Pathing::Grid& grid = GridFor(player, { id });
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
    Pathing::Point start;
    if (!Pathing::NearestPassable(grid, PlayerCell(player), 4, start)) return 0;
    // Breadth-first over walkable cells of this level, nearest first.
    std::vector<int32_t> steps(grid.blocked.size(), -1);
    std::vector<int32_t> queue;
    queue.reserve(4096);
    const size_t first = grid.Index(start.x, start.y);
    steps[first] = 0;
    queue.push_back((int32_t)first);
    static const int directions[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
    for (size_t head = 0; head < queue.size(); ++head) {
        const int32_t current = queue[head];
        const int x = grid.originX + current % grid.width, y = grid.originY + current / grid.width;
        if (unseenAt(x, y) && patch(x, y) >= minimum) {
            lua_createtable(L, 0, 5);
            Script::SetField(L, "x", x);
            Script::SetField(L, "y", y);
            Script::SetField(L, "steps", steps[current]);
            Script::SetField(L, "unseen", unseen);
            Script::SetField(L, "total", total);
            return 1;
        }
        for (const auto& d : directions) {
            const int nx = x + d[0], ny = y + d[1];
            if (!grid.Passable(nx, ny) || grid.LevelAt(nx, ny) != id) continue;
            const size_t next = grid.Index(nx, ny);
            if (steps[next] >= 0) continue;
            steps[next] = steps[current] + 1;
            queue.push_back((int32_t)next);
        }
    }
    return 0;
}

// map.explore_plan([cell]) -> { {x, y}, ... }, milliseconds: a sweep of the
// player's level from where the player stands (Pathing::PlanCoverage with
// blocks of `cell` subtiles, default 12), in walking order.
int MapExplorePlan(lua_State* L) {
    Unit* player = RequirePlayer();
    const int cell = Script::OptInt(L, 1, 12);
    if (cell < 4 || cell > 40) throw Script::Error("cell must be 4..40");
    const int id = UnitLevelId(player);
    const Pathing::Grid& grid = GridFor(player, { id });
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
        { "explore_next", Script::Bind<MapExploreNext> },
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
