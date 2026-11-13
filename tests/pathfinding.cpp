#include "headers/pathfinding.h"
#include "headers/script_collision.h"
#include "headers/script_missiles.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

using namespace Pathing;

namespace {

// '#' blocked, '.' open; origin offset proves world coordinates are honoured.
Grid FromRows(const char* const* rows, int height, int originX = 100, int originY = 200) {
    Grid grid;
    const int width = (int)strlen(rows[0]);
    grid.Reset(originX, originY, width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            grid.blocked[grid.Index(originX + x, originY + y)] = rows[y][x] == '#';
            grid.level[grid.Index(originX + x, originY + y)] = rows[y][x] == '2' ? 2 : 1;
        }
    }
    return grid;
}

bool Adjacent(Point a, Point b) {
    return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1 && !(a == b);
}

void CheckGroundHazards() {
    // Exact installed 1.14d handler/type/size fixtures: blood, impact/death FX
    // must stay harmless even when they have a stationary client position.
    assert(Game::GroundFireSize(18, 2, 0, 0, 1) == 0); // blood18/19
    assert(Game::GroundFireSize(29, 1, 0, 0, 1) == 0); // fireexplode29
    assert(Game::GroundFireSize(309, 5, 1, 0, 1) == 0); // andyfirewall309
    assert(Game::GroundFireSize(653, 6, 6, 8, 1) == 0); // maker653
    for (int id : {67,69,131,230,331}) assert(Game::GroundFireSize(id, 5, 5, 3, 1) == 1);
    for (int id : {240,423,681}) assert(Game::GroundFireSize(id, 5, 5, 3, 2) == 2);
    for (int id : {104,105}) {
        assert(Game::GroundFireSize(id, 5, 1, 3, 1) == 1); // real fire's client substitutes
        assert(Game::GroundFireSize(id, 5, 1, 0, 1) == 0);
        assert(Game::GroundFireSize(id, 5, 1, 3, 2) == 0);
    }
    for (int id : {180,457,521}) assert(Game::GroundFireSize(id, 5, 1, 3, 1) == 0);
    assert(Game::GroundFireSize(69, 5, 5, 3, 0) == 0);
    assert(Game::GroundFireSize(69, 5, 5, 3, 4) == 0);
    for (int size = 1; size <= 3; ++size) {
        const std::vector<GroundHazard> hazards = {{15.8, 15.2, size}};
        for (int y = -3; y <= 3; ++y) {
            for (int x = -3; x <= 3; ++x) {
                const bool overlap = size == 1 ? std::abs(x) + std::abs(y) <= 1
                    : size == 2 ? std::abs(x) + std::abs(y) <= 2
                    : std::max(std::abs(x), std::abs(y)) <= 2 && std::abs(x) + std::abs(y) <= 3;
                assert((GroundDanger(hazards, 15.5+x, 15.5+y) > 0) == overlap);
                assert(GroundHazardClear(hazards, 15.5+x, 15.5+y, 15.5+x, 15.5+y) == !overlap);
            }
        }
    }
    assert(GroundDanger({{-0.1, -0.1, 1}}, -1.5, -0.5) > 0); // floor, not truncation
    const std::vector<GroundHazard> patch = {{15.5,15.5,1}};
    assert(!GroundHazardClear(patch, 10.5,15.5,20.5,15.5)); // safe endpoints, fire between
    assert(!GroundHazardClear(patch, 20.5,15.5,10.5,15.5));
    assert(!GroundHazardClear(patch, 14.5,14.5,15.5,13.5)); // corner touch
    assert(GroundHazardClear(patch, 14.4,14.4,15.4,13.4)); // just outside that corner
    assert(!GroundHazardClear(patch, 15.5,15.5,20.5,15.5)); // start overlap
    assert(!GroundHazardClear(patch, 10.5,15.5,15.5,15.5)); // endpoint overlap
    assert(GroundHazardClear(patch, -65535,10.5,65535,10.5)); // bounded work for long queries
    assert(!GroundHazardClear(patch, -65535,15.5,65535,15.5));
    Grid field;
    field.Reset(0, 0, 32, 32);
    std::fill(field.blocked.begin(), field.blocked.end(), uint8_t(0));
    std::fill(field.level.begin(), field.level.end(), uint16_t(1));
    SafeSpotOptions options;
    options.radius = 10;
    options.clearance = 1;
    options.hazards = {{15.5, 15.5, 2}};
    const auto escape = FindSafeSpot(field, {15,15}, {}, options);
    assert(escape.found && escape.hazardHere == 100 && escape.hazardThere == 0);
    bool clear = false;
    for (const Point& point : escape.path) {
        const bool burning = GroundDanger(options.hazards, point.x+0.5, point.y+0.5) > 0;
        assert(!clear || !burning);
        clear = !burning;
    }
    options.hazards = {{13.5,15.5,1}};
    options.hasTarget = true;
    options.targetX = 20.5;
    options.targetY = 15.5;
    options.range = 0;
    options.rangeWeight = 4;
    options.occupied = 1;
    options.lead = 0;
    const auto detour = FindSafeSpot(field, {10,15}, {{9.5,15.5,20}}, options);
    assert(detour.found && detour.spot.x > 14 && detour.hazardThere == 0);
    for (size_t i = 1; i < detour.path.size(); ++i) {
        const Point a = detour.path[i-1], b = detour.path[i];
        assert(Adjacent(a,b) && GroundDanger(options.hazards,b.x+0.5,b.y+0.5) == 0);
        if (a.x != b.x && a.y != b.y) {
            assert(GroundDanger(options.hazards,a.x+0.5,b.y+0.5) == 0);
            assert(GroundDanger(options.hazards,b.x+0.5,a.y+0.5) == 0);
        }
    }
    // Fire cannot turn a wall or a blocked diagonal into an escape route.
    std::fill(field.blocked.begin(), field.blocked.end(), uint8_t(1));
    field.blocked[field.Index(15,15)] = field.blocked[field.Index(16,16)] = 0;
    options.hasTarget = false;
    options.hazards = {{15.5,15.5,1}};
    assert(!FindSafeSpot(field,{15,15},{},options).found);
}

void CheckMovementStart() {
    // Exclude only our captured center before inflation: opening it afterwards
    // leaves its artificial one-cell halo and forces the whole route to exact.
    Grid ownSnapshot;
    ownSnapshot.Reset(0, 0, 12, 9);
    std::fill(ownSnapshot.blocked.begin(), ownSnapshot.blocked.end(), uint8_t(0));
    std::fill(ownSnapshot.level.begin(), ownSnapshot.level.end(), uint16_t(1));
    ownSnapshot.blocked[ownSnapshot.Index(4, 4)] = ScriptMap::SnapshotCollision(0x1080, true) != 0;
    ownSnapshot.blocked[ownSnapshot.Index(9, 4)] = ScriptMap::SnapshotCollision(0x1080, false) != 0;
    ownSnapshot.blocked[ownSnapshot.Index(4, 1)] = ScriptMap::SnapshotCollision(0x1081, true) != 0;
    ownSnapshot.Inflate(1);
    assert(ownSnapshot.Passable(4, 4) && ownSnapshot.Passable(5, 4));
    assert(!FindPath(ownSnapshot, {4, 4}, {6, 4}).empty());
    assert(!ownSnapshot.Passable(9, 4) && !ownSnapshot.Passable(8, 4));
    assert(!ownSnapshot.Passable(4, 1) && !ownSnapshot.Passable(4, 2));
    for (uint16_t blocker : {uint16_t(1), uint16_t(8), uint16_t(0x400), uint16_t(0x800)}) {
        assert(ScriptMap::SnapshotCollision(0x1080 | blocker, true) == (0x1000 | blocker));
        assert(ScriptMap::SnapshotCollision(blocker, false) == blocker);
    }

    const char* rows[] = {
        "############",
        "#.#........#",
        "#.#........#",
        "#.#........#",
        "#.#........#",
        "#.#........#",
        "############",
    };
    const Grid exact = FromRows(rows, 7, 0, 0);
    Grid padded = exact;
    padded.Inflate(1);
    const Point actual{1, 3}, otherSide{8, 3};
    Point snapped, start;
    // The old nearest-cell policy selects the disconnected room across the
    // wall and produces a plausible route there, skipping the crossing.
    assert(NearestPassable(padded, actual, 4, snapped) && snapped.x > 2);
    assert(!FindPath(padded, snapped, otherSide).empty());
    assert(!MovementStart(padded, actual, start));
    assert(MovementStart(exact, actual, start) && start == actual);
    assert(FindPath(exact, start, otherSide).empty());

    // The wall-adjacent one-cell corridor still permits real movement after
    // falling back to exact collision. This covers point and goal-set routes.
    const Point goal{1, 5};
    const auto pointPath = FindPath(exact, start, goal);
    const auto levelPath = FindPath(exact, start, [goal](int x, int y) {
        return x == goal.x && y == goal.y;
    }, goal);
    for (const auto& cells : {pointPath, levelPath}) {
        assert(!cells.empty() && cells.front() == actual);
        const auto waypoints = Simplify(exact, cells, 15);
        assert(waypoints.size() > 1 && LineClear(exact, actual, waypoints[1]));
    }
    const Point open{8, 3};
    assert(MovementStart(padded, open, start) && start == open);
    assert(!MovementStart(exact, {2, 3}, start));
    assert(!MovementStart(exact, {-1, 3}, start));

    Grid occupied = exact;
    occupied.blocked[occupied.Index(actual.x, actual.y)] = 1;
    const Grid snapshot = occupied;
    assert(!MovementStart(occupied, actual, start));
    assert(OpenMovementStart(occupied, actual));
    assert(MovementStart(occupied, actual, start) && start == actual);
    for (size_t i = 0; i < occupied.blocked.size(); ++i) {
        if (i != occupied.Index(actual.x, actual.y)) assert(occupied.blocked[i] == snapshot.blocked[i]);
    }
    const auto departure = Simplify(occupied, FindPath(occupied, actual, goal), 15);
    assert(departure.size() > 1 && departure.front() == actual);
    assert(LineClear(occupied, actual, departure[1]));
    assert(FindPath(occupied, actual, otherSide).empty());
    assert(!snapshot.Passable(actual.x, actual.y));

    Grid enclosed;
    enclosed.Reset(0, 0, 3, 3);
    std::fill(enclosed.level.begin(), enclosed.level.end(), uint16_t(1));
    enclosed.blocked[enclosed.Index(0, 0)] = 0; // diagonal-only exit cannot cut its two blocked corners
    const auto blocked = enclosed.blocked;
    assert(!OpenMovementStart(enclosed, {1, 1}) && enclosed.blocked == blocked);
    enclosed.blocked[enclosed.Index(1, 0)] = 0;
    enclosed.level[enclosed.Index(1, 1)] = 0;
    assert(!OpenMovementStart(enclosed, {1, 1}));
    assert(!OpenMovementStart(enclosed, {-1, 1}));
}

void CheckExploration() {
    Grid grid;
    grid.Reset(100, 200, 48, 24);
    std::fill(grid.blocked.begin(), grid.blocked.end(), uint8_t(0));
    std::fill(grid.level.begin(), grid.level.end(), uint16_t(1));
    // A one-cell door remains reachable; a solid enclosure and an adjoining
    // level do not contribute monsters or coverage.
    for (int y = 200; y < 224; ++y) if (y != 220) grid.blocked[grid.Index(112, y)] = 1;
    for (int y = 202; y <= 206; ++y) {
        for (int x = 102; x <= 106; ++x)
            if (x == 102 || x == 106 || y == 202 || y == 206) grid.blocked[grid.Index(x, y)] = 1;
    }
    for (int y = 200; y < 224; ++y) grid.level[grid.Index(147, y)] = 2;
    std::vector<uint8_t> seen(grid.blocked.size(), 0);
    ExploreOptions options;
    const Point start{110, 204};
    options.targets = {{1, {104, 204}}, {2, {147, 204}}, {3, {114, 204}}, {5, {-1, -1}}};
    options.exclude = {{114, 204}}; // failed exploration blocks do not hide monsters
    auto choice = ChooseExploration(grid, start, 1, seen, options);
    assert(choice.found && choice.monster && choice.id == 3 && choice.steps > 30);
    assert(choice.point == Point({114, 204}) && choice.gain == 0);
    options.exclude.clear();
    const int reachable = choice.total;
    assert(choice.unseen == reachable && reachable < 48 * 24 - 24 - 9);
    // The target two cells west wins over the target just across the wall:
    // distance is walking distance, and input order cannot change the result.
    options.targets.push_back({4, {108, 204}});
    choice = ChooseExploration(grid, start, 1, seen, options);
    assert(choice.id == 4 && choice.steps == 2);
    std::reverse(options.targets.begin(), options.targets.end());
    assert(ChooseExploration(grid, start, 1, seen, options).id == 4);
    options.targets = {{1, {104, 204}}, {2, {147, 204}}};
    choice = ChooseExploration(grid, start, 1, seen, options);
    assert(choice.found && !choice.monster && !seen[grid.Index(choice.point.x, choice.point.y)]);
    assert(choice.total == reachable);

    const char* cornerRows[] = {".#", "#."};
    Grid corner = FromRows(cornerRows, 2, 0, 0);
    options.minimum = 1;
    options.targets = {{1, {1, 1}}};
    choice = ChooseExploration(corner, {0, 0}, 1, std::vector<uint8_t>(4, 0), options);
    assert(choice.found && !choice.monster && choice.total == 1 && choice.gain == 1);

    Grid open;
    open.Reset(0, 0, 72, 24);
    std::fill(open.blocked.begin(), open.blocked.end(), uint8_t(0));
    std::fill(open.level.begin(), open.level.end(), uint16_t(1));
    seen.assign(open.blocked.size(), 1);
    seen[open.Index(60, 8)] = 0;
    options = ExploreOptions();
    assert(!ChooseExploration(open, {2, 8}, 1, seen, options).found);
    options.minimum = 1;
    choice = ChooseExploration(open, {2, 8}, 1, seen, options);
    assert(choice.found && choice.point == Point({60, 8}) && choice.gain == 1 && choice.steps == 58);
    // A substantial nearby patch beats a long trip for a crumb, even when
    // exhaustive coverage is requested. Its representative is still unseen.
    for (int y = 1; y <= 10; ++y) for (int x = 12; x <= 21; ++x) seen[open.Index(x, y)] = 0;
    choice = ChooseExploration(open, {2, 8}, 1, seen, options);
    assert(choice.gain == 100 && choice.point.x >= 12 && choice.point.x <= 21);
    const auto savedSeen = seen;
    options.exclude = {choice.point};
    choice = ChooseExploration(open, {2, 8}, 1, seen, options);
    assert(choice.point == Point({60, 8}) && choice.gain == 1 && seen == savedSeen);
    options.exclude.push_back(choice.point);
    assert(!ChooseExploration(open, {2, 8}, 1, seen, options).found && seen == savedSeen);

    // Two unseen pockets in one block are separated by a wall but connected
    // outside that block. Their gains must never be summed to pass minimum.
    Grid pockets;
    pockets.Reset(0, 0, 12, 16);
    std::fill(pockets.blocked.begin(), pockets.blocked.end(), uint8_t(0));
    std::fill(pockets.level.begin(), pockets.level.end(), uint16_t(1));
    for (int y = 0; y < 12; ++y) pockets.blocked[pockets.Index(6, y)] = 1;
    seen.assign(pockets.blocked.size(), 1);
    for (int y = 2; y < 6; ++y) {
        for (int x = 2; x < 5; ++x) seen[pockets.Index(x, y)] = 0;
        for (int x = 8; x < 11; ++x) seen[pockets.Index(x, y)] = 0;
    }
    options = ExploreOptions();
    choice = ChooseExploration(pockets, {3, 8}, 1, seen, options);
    assert(!choice.found && choice.unseen == 24 && choice.total == 180);
    options.minimum = 1;
    choice = ChooseExploration(pockets, {3, 8}, 1, seen, options);
    assert(choice.found && choice.gain == 12 && choice.point.x < 6);

    // Symmetric ties are stable; a small heading bias can break them.
    Grid symmetric;
    symmetric.Reset(0, 0, 36, 12);
    std::fill(symmetric.blocked.begin(), symmetric.blocked.end(), uint8_t(0));
    std::fill(symmetric.level.begin(), symmetric.level.end(), uint16_t(1));
    seen.assign(symmetric.blocked.size(), 1);
    seen[symmetric.Index(5, 6)] = seen[symmetric.Index(29, 6)] = 0;
    choice = ChooseExploration(symmetric, {17, 6}, 1, seen, options);
    assert(choice.point.x == 5);
    options.headingX = 1;
    assert(ChooseExploration(symmetric, {17, 6}, 1, seen, options).point.x == 29);
    options.targets = {{12, {29, 6}}, {10, {5, 6}}};
    assert(ChooseExploration(symmetric, {17, 6}, 1, seen, options).id == 10);
    options.minimum = 0;
    assert(!ChooseExploration(symmetric, {17, 6}, 1, seen, options).found);
    options.minimum = 1;
    options.headingX = NAN;
    assert(!ChooseExploration(symmetric, {17, 6}, 1, seen, options).found);
    options.headingX = 0;
    options.cell = 3;
    assert(!ChooseExploration(symmetric, {17, 6}, 1, seen, options).found);
    options.cell = 12;
    options.targets.resize(513);
    assert(!ChooseExploration(symmetric, {17, 6}, 1, seen, options).found);
    options.targets.clear();
    options.exclude.resize(65);
    assert(!ChooseExploration(symmetric, {17, 6}, 1, seen, options).found);
    options.exclude.clear();
    assert(!ChooseExploration(symmetric, {17, 6}, 1, {}, options).found);

    Grid large;
    large.Reset(0, 0, 480, 280);
    std::fill(large.blocked.begin(), large.blocked.end(), uint8_t(0));
    std::fill(large.level.begin(), large.level.end(), uint16_t(1));
    seen.assign(large.blocked.size(), 0);
    options = ExploreOptions();
    const auto began = std::chrono::steady_clock::now();
    choice = ChooseExploration(large, {5, 5}, 1, seen, options);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    std::printf("adaptive exploration for 480x280: %d cells in %.1f ms\n", choice.total, ms);
    assert(choice.found && choice.gain == 144 && choice.steps == 0 && choice.total == 480 * 280);
    const auto again = ChooseExploration(large, {5, 5}, 1, seen, options);
    assert(again.point == choice.point && again.gain == choice.gain);
    assert(ms < 40);
}

}

int main() {
    const char* maze[] = {
        "..........",
        ".########.",
        ".#......#.",
        ".#.#..#.#.",
        ".#.#..#.#.",
        "...#..#...",
        "####..####",
    };
    Grid grid = FromRows(maze, 7);

    // Shortest route around the wall, every step adjacent and passable, no corner cuts.
    const std::vector<Point> path = FindPath(grid, { 100, 200 }, { 104, 205 });
    assert(!path.empty());
    assert(path.front() == Point({ 100, 200 }) && path.back() == Point({ 104, 205 }));
    for (size_t i = 1; i < path.size(); ++i) {
        assert(Adjacent(path[i - 1], path[i]));
        assert(grid.Passable(path[i].x, path[i].y));
        const int dx = path[i].x - path[i - 1].x, dy = path[i].y - path[i - 1].y;
        if (dx && dy) assert(grid.Passable(path[i - 1].x + dx, path[i - 1].y) && grid.Passable(path[i - 1].x, path[i - 1].y + dy));
    }

    // Unreachable and blocked goals give empty paths.
    assert(FindPath(grid, { 100, 200 }, { 101, 201 }).empty());
    assert(FindPath(grid, { 104, 204 }, { 120, 220 }).empty());

    // Simplified waypoints keep clear lines and the step limit.
    const std::vector<Point> waypoints = Simplify(grid, path, 4);
    assert(waypoints.front() == path.front() && waypoints.back() == path.back());
    assert(waypoints.size() < path.size());
    for (size_t i = 1; i < waypoints.size(); ++i) {
        assert(LineClear(grid, waypoints[i - 1], waypoints[i]));
        assert(std::hypot(waypoints[i].x - waypoints[i - 1].x, waypoints[i].y - waypoints[i - 1].y) <= 4.0001);
    }
    assert(!LineClear(grid, { 100, 200 }, { 104, 205 }));

    // Goal sets: reach the first cell of level 2.
    const char* border[] = {
        "1111#2222",
        "1111.2222",
        "1111#2222",
    };
    Grid levels = FromRows(border, 3, 0, 0);
    const std::vector<Point> toLevel = FindPath(levels, { 0, 0 },
        [&](int x, int y) { return levels.LevelAt(x, y) == 2; }, { 6, 1 });
    assert(!toLevel.empty() && levels.LevelAt(toLevel.back().x, toLevel.back().y) == 2);
    assert(toLevel.back() == Point({ 5, 1 }));

    // Inflation closes the one-cell gap; nearest passable search finds open cells.
    Grid inflated = levels;
    inflated.Inflate(1);
    assert(!inflated.Passable(4, 1) && !inflated.Passable(3, 0) && inflated.Passable(2, 1));
    Point found;
    assert(NearestPassable(inflated, { 4, 1 }, 3, found) && inflated.Passable(found.x, found.y));
    assert(std::abs(found.x - 4) <= 2);

    // Safe spot, open ground: step away from a threat to the east.
    Grid field;
    field.Reset(0, 0, 40, 40);
    std::fill(field.blocked.begin(), field.blocked.end(), uint8_t(0));
    std::fill(field.level.begin(), field.level.end(), uint16_t(1));
    std::vector<Threat> east = { { 22.5, 20.5, 1 } };
    SafeSpotOptions options;
    SafeSpot spot = FindSafeSpot(field, { 20, 20 }, east, options);
    assert(spot.found && spot.spot.x < 20 && spot.dangerThere < spot.dangerHere);
    assert(spot.path.front() == Point({ 20, 20 }) && spot.path.back() == spot.spot);
    for (size_t i = 1; i < spot.path.size(); ++i) {
        assert(Adjacent(spot.path[i - 1], spot.path[i]));
        assert(std::hypot(spot.path[i].x + 0.5 - 22.5, spot.path[i].y + 0.5 - 20.5) >= options.occupied);
    }

    // An adjacent monster's safety margin can trap us despite open ground behind.
    std::vector<Threat> adjacent = { { 21.5, 20.5, 1 } };
    SafeSpotOptions retreat;
    retreat.lead = 1.5;
    spot = FindSafeSpot(field, { 20, 20 }, adjacent, retreat);
    assert(!spot.found && spot.path.empty());

    // Emergency retreat reduces margins, while still respecting real collision.
    retreat.occupied = 1;
    retreat.clearance = 1;
    retreat.lead = 0.5;
    retreat.liveBlocked = [](int x, int y) { return x == 21 && y == 20; };
    spot = FindSafeSpot(field, { 20, 20 }, adjacent, retreat);
    assert(spot.found && spot.spot.x < 20 && spot.dangerThere < spot.dangerHere);
    assert(spot.path.front() == Point({ 20, 20 }) && spot.path.back() == spot.spot);

    Grid retreatWalls = field;
    for (int x = 0; x < retreatWalls.width; ++x)
        retreatWalls.blocked[retreatWalls.Index(x, 18)] = 1;
    retreat.liveBlocked = [](int x, int y) { return x == 18 || (x == 21 && y == 20); };
    spot = FindSafeSpot(retreatWalls, { 20, 20 }, adjacent, retreat);
    assert(spot.found && spot.dangerThere < spot.dangerHere);
    for (size_t i = 1; i < spot.path.size(); ++i) {
        const Point a = spot.path[i - 1], b = spot.path[i];
        assert(Adjacent(a, b));
        assert(retreatWalls.Passable(b.x, b.y) && !retreat.liveBlocked(b.x, b.y));
        assert(b.x > 18 && b.y > 18);
        assert(std::hypot(b.x + 0.5 - adjacent[0].x, b.y + 0.5 - adjacent[0].y) >= retreat.occupied);
        if (a.x != b.x && a.y != b.y) {
            assert(retreatWalls.Passable(b.x, a.y) && retreatWalls.Passable(a.x, b.y));
            assert(!retreat.liveBlocked(b.x, a.y) && !retreat.liveBlocked(a.x, b.y));
        }
    }

    // Open diagonal cells do not permit squeezing between blocked cardinal cells.
    Grid retreatCorner = field;
    retreatCorner.blocked[retreatCorner.Index(19, 20)] = 1;
    retreatCorner.blocked[retreatCorner.Index(20, 19)] = 1;
    retreat.liveBlocked = [](int x, int y) {
        return (x == 21 && y == 20) || (x == 20 && y == 21);
    };
    spot = FindSafeSpot(retreatCorner, { 20, 20 }, adjacent, retreat);
    assert(!spot.found && spot.path.empty());

    // Live collision (monster footprints, closed doors) is never entered.
    options.liveBlocked = [](int x, int) { return x < 14; };
    spot = FindSafeSpot(field, { 20, 20 }, east, options);
    assert(spot.found);
    for (const Point& p : spot.path) assert(p.x >= 14);
    options.liveBlocked = nullptr;

    // With a target to keep in range, the spot stays within range of it.
    options.hasTarget = true;
    options.targetX = 20.5;
    options.targetY = 12.5;
    options.range = 7;
    spot = FindSafeSpot(field, { 20, 20 }, east, options);
    assert(spot.found && std::hypot(spot.spot.x + 0.5 - 20.5, spot.spot.y + 0.5 - 12.5) <= 9);
    options.hasTarget = false;

    // Melee flanks a crowd while staying close enough to resume striking after
    // a short approach. Range is a soft preference, including diagonal cells.
    std::vector<Threat> crowd = {
        { 22.5, 20.5, 1 }, { 17.5, 20.5, 1 }, { 20.5, 17.5, 1 }, { 18.5, 23.5, 1 }
    };
    SafeSpotOptions melee;
    melee.radius = 8;
    melee.occupied = 1.5;
    melee.clearance = 1;
    melee.lead = 0.5;
    melee.hasTarget = true;
    melee.targetX = crowd[0].x;
    melee.targetY = crowd[0].y;
    melee.range = 2.8;
    melee.rangeWeight = 1.5;
    spot = FindSafeSpot(field, { 20, 20 }, crowd, melee);
    assert(spot.found && spot.dangerThere < spot.dangerHere);
    const double meleeDistance = std::hypot(spot.spot.x + 0.5 - melee.targetX, spot.spot.y + 0.5 - melee.targetY);
    assert(meleeDistance <= 3);
    assert(spot.spot.x > 20); // flank toward the selected enemy, away from the crowd
    for (size_t i = 1; i < spot.path.size(); ++i) {
        const Point& p = spot.path[i];
        assert(Adjacent(spot.path[i - 1], p) && field.Passable(p.x, p.y));
        for (const Threat& threat : crowd)
            assert(std::hypot(p.x + 0.5 - threat.x, p.y + 0.5 - threat.y) >= melee.occupied);
    }

    // The original range weight still gives ranged combat room to retreat.
    SafeSpotOptions ranged = melee;
    ranged.rangeWeight = SafeSpotOptions().rangeWeight;
    spot = FindSafeSpot(field, { 20, 20 }, crowd, ranged);
    assert(spot.found && spot.dangerThere < spot.dangerHere);
    assert(std::hypot(spot.spot.x + 0.5 - ranged.targetX, spot.spot.y + 0.5 - ranged.targetY) > meleeDistance + 1);

    // Fully occupied neighbours leave no safe route, even with melee clearance.
    std::vector<Threat> surround = {
        { 22.5, 20.5, 1 }, { 18.5, 20.5, 1 }, { 20.5, 18.5, 1 }, { 20.5, 22.5, 1 }
    };
    spot = FindSafeSpot(field, { 20, 20 }, surround, melee);
    assert(!spot.found && spot.path.empty());

    // Dead-end pocket with the monster at its mouth: no way out but through it.
    const char* pocket[] = {
        "#######",
        "#.....#",
        "###.###",
        "###.###",
        "###.###",
    };
    Grid trap = FromRows(pocket, 5, 0, 0);
    std::vector<Threat> mouth = { { 3.5, 3.5, 1 } };
    SafeSpotOptions near;
    near.radius = 6;
    spot = FindSafeSpot(trap, { 3, 1 }, mouth, near);
    assert(!spot.found || spot.spot.y <= 1);
    for (const Point& p : spot.path) assert(p.y <= 2);

    // Open ground leaves many ways out.
    spot = FindSafeSpot(field, { 20, 20 }, east, SafeSpotOptions());
    assert(spot.openSectors >= 4);

    // A wall with a one-subtile gap beside the player and a wide gap further
    // away: the escape must use the wide gap, never the slim one.
    Grid wall;
    wall.Reset(0, 0, 30, 24);
    std::fill(wall.blocked.begin(), wall.blocked.end(), uint8_t(0));
    std::fill(wall.level.begin(), wall.level.end(), uint16_t(1));
    for (int x = 0; x < 30; ++x) {
        if (x != 5 && (x < 14 || x > 19)) wall.blocked[wall.Index(x, 10)] = 1;
    }
    std::vector<Threat> south = { { 5.5, 16.5, 1 }, { 3.5, 15.5, 1 }, { 7.5, 15.5, 1 } };
    SafeSpotOptions narrow;
    narrow.radius = 16;
    spot = FindSafeSpot(wall, { 5, 13 }, south, narrow);
    for (const Point& p : spot.path) {
        assert(wall.Passable(p.x, p.y));
        assert(!(p.x == 5 && p.y == 10));
    }

    // Incoming arrows need a lateral sidestep, not a retreat down their lane.
    SafeSpotOptions dodge;
    dodge.radius = 4;
    dodge.clearance = 1;
    dodge.missiles = { { 28.5, 20.5, 12.5, 20.5 } };
    assert(MissileDanger(dodge.missiles, 20.5, 20.5) == 2);
    assert(MissileDanger(dodge.missiles, 20.5, 22.5) == 0);
    spot = FindSafeSpot(field, {20, 20}, {}, dodge);
    assert(spot.found && spot.missileHere == 2 && spot.missileThere == 0);
    assert(std::abs(spot.spot.y - 20) >= 2 && spot.path.size() <= 4);
    // A vertical arrow offers symmetric east/west escapes. Heading only breaks ties.
    SafeSpotOptions passage = dodge;
    passage.missiles = {{20.5, 28.5, 20.5, 12.5}};
    const SafeSpot unbiased = FindSafeSpot(field, {20, 20}, {}, passage);
    passage.headingX = 1;
    const SafeSpot eastward = FindSafeSpot(field, {20, 20}, {}, passage);
    passage.headingX = -1;
    const SafeSpot westward = FindSafeSpot(field, {20, 20}, {}, passage);
    assert(eastward.found && westward.found && eastward.spot.x > 20 && westward.spot.x < 20);
    assert(eastward.missileThere == 0 && westward.missileThere == 0);
    assert(eastward.dangerThere == westward.dangerThere && eastward.path.size() == westward.path.size());
    passage.headingX = 0;
    const SafeSpot zeroHeading = FindSafeSpot(field, {20, 20}, {}, passage);
    assert(zeroHeading.spot == unbiased.spot && zeroHeading.path == unbiased.path
        && zeroHeading.dangerThere == unbiased.dangerThere);
    passage.headingX = 1e300;
    assert(FindSafeSpot(field, {20, 20}, {}, passage).spot == eastward.spot);
    // Even a small superior safety score wins over the preferred direction.
    passage.lead = 0;
    const std::vector<Threat> slightEastDanger = {{30.5, 20.5, 0.01}};
    const SafeSpot saferWest = FindSafeSpot(field, {20, 20}, slightEastDanger, passage);
    assert(saferWest.found && saferWest.spot.x < 20);
    // On the way somewhere, onward counts for more than that: the dodge goes
    // on out of the lane, and no farther back than it must.
    passage.headingWeight = 0.08;
    const SafeSpot onward = FindSafeSpot(field, {20, 20}, slightEastDanger, passage);
    assert(onward.found && onward.spot.x > 20 && onward.missileThere == 0);
    assert(onward.spot.x >= eastward.spot.x);
    passage.headingX = 0; passage.headingY = 1; // along the arrow's own line there is no way out of it: aside still
    const SafeSpot along = FindSafeSpot(field, {20, 20}, {}, passage);
    assert(along.found && along.missileThere == 0 && along.spot.x != 20);
    passage.headingX = 1e300; passage.headingY = 0;
    passage.headingWeight = 0;
    passage.liveBlocked = [](int x, int) { return x > 20; };
    const SafeSpot blockedEast = FindSafeSpot(field, {20, 20}, {}, passage);
    assert(blockedEast.found && blockedEast.spot.x < 20);
    for (size_t i = 1; i < blockedEast.path.size(); ++i) {
        const Point a = blockedEast.path[i-1], b = blockedEast.path[i];
        assert(field.Passable(b.x,b.y) && !passage.liveBlocked(b.x,b.y) && Adjacent(a,b));
        if (a.x != b.x && a.y != b.y)
            assert(!passage.liveBlocked(a.x,b.y) && !passage.liveBlocked(b.x,a.y));
    }
    SafeSpotOptions headedRetreat = retreat;
    headedRetreat.headingX = -1;
    const SafeSpot occupiedEscape = FindSafeSpot(retreatWalls, {20,20}, adjacent, headedRetreat);
    assert(occupiedEscape.found);
    for (const Point& p : occupiedEscape.path) {
        assert(retreatWalls.Passable(p.x,p.y) && !headedRetreat.liveBlocked(p.x,p.y));
        assert(std::hypot(p.x+0.5-adjacent[0].x,p.y+0.5-adjacent[0].y) >= headedRetreat.occupied);
    }
    assert(!FindSafeSpot(retreatCorner, {20,20}, adjacent, headedRetreat).found);
    passage.liveBlocked = nullptr;
    passage.missiles.clear();
    passage.travelWeight = 0;
    assert(!FindSafeSpot(field, {20,20}, {}, passage).found); // equal to staying cannot initiate movement
    // Footprints and walls still constrain the side chosen for the dodge.
    dodge.liveBlocked = [](int, int y) { return y < 20; };
    spot = FindSafeSpot(field, {20, 20}, {{23.5, 20.5, 1}}, dodge);
    assert(spot.found && spot.spot.y >= 22 && spot.missileThere == 0);
    for (size_t i = 1; i < spot.path.size(); ++i) {
        const auto p = spot.path[i];
        assert(p.y >= 20 && field.Passable(p.x, p.y) && Adjacent(spot.path[i-1], p));
        assert(std::hypot(p.x + 0.5 - 23.5, p.y + 0.5 - 20.5) >= dodge.occupied);
    }
    dodge.liveBlocked = nullptr;
    dodge.missiles = {{23.5, 20.5, 30.5, 20.5}};
    assert(!FindSafeSpot(field, {20, 20}, {}, dodge).found); // receding arrow
    dodge.missiles = {{21, 20.5, 30, 20.5}};
    assert(!FindSafeSpot(field, {20, 20}, {}, dodge).found); // just passed, still within one subtile
    dodge.missiles = {{20.5, 20.5, 20.5, 20.5}};
    assert(!FindSafeSpot(field, {20, 20}, {}, dodge).found); // stationary effect

    // Coverage plan: every reachable block appears once; walls split nothing reachable.
    Grid rooms;
    rooms.Reset(0, 0, 60, 36);
    std::fill(rooms.blocked.begin(), rooms.blocked.end(), uint8_t(0));
    std::fill(rooms.level.begin(), rooms.level.end(), uint16_t(1));
    for (int y = 0; y < 36; ++y) {
        if (y != 18) rooms.blocked[rooms.Index(30, y)] = 1; // wall with one door
    }
    for (int y = 0; y < 36; ++y) {
        for (int x = 50; x < 60; ++x) rooms.blocked[rooms.Index(x, y)] = (x == 50 && y < 30) ? 1 : rooms.blocked[rooms.Index(x, y)];
    }
    const std::vector<Point> tour = PlanCoverage(rooms, { 2, 2 }, 12);
    assert(!tour.empty() && tour.size() <= size_t(5 * 3));
    for (const Point& p : tour) assert(rooms.Passable(p.x, p.y));
    for (size_t i = 0; i < tour.size(); ++i) {
        for (size_t j = i + 1; j < tour.size(); ++j) assert(!(tour[i] == tour[j]));
    }
    // Every reachable cell is within reach of some tour point (block size bound).
    for (int y = 0; y < 36; ++y) {
        for (int x = 0; x < 60; ++x) {
            if (!rooms.Passable(x, y)) continue;
            double nearest = 1e9;
            for (const Point& p : tour) nearest = std::min(nearest, std::hypot(double(p.x - x), double(p.y - y)));
            assert(nearest <= 12 * 1.5);
        }
    }
    // The tour starts in the start block and does not jump back and forth across
    // the wall more than needed: crossings of x = 30 are at most 2.
    assert(tour.front().x < 12 && tour.front().y < 12);
    int crossings = 0;
    for (size_t i = 1; i < tour.size(); ++i) crossings += (tour[i - 1].x < 30) != (tour[i].x < 30) ? 1 : 0;
    assert(crossings <= 2);

    {
        Pathing::Grid raw;
        raw.Reset(0, 0, 60, 30);
        std::fill(raw.blocked.begin(), raw.blocked.end(), uint8_t(0));
        std::fill(raw.level.begin(), raw.level.end(), uint16_t(1));
        // Two rooms with a two-subtile doorway, closed by the one-cell margin.
        for (int y = 0; y < 30; ++y)
            if (y != 14 && y != 15) raw.blocked[raw.Index(30, y)] = 1;
        auto doorInflated = raw;
        doorInflated.Inflate(1);
        const Pathing::Point start{5, 15}, goal{50, 15};
        const auto exactPath = Pathing::FindPath(raw, start, goal);
        const auto paddedPath = Pathing::FindPath(doorInflated, start, goal);
        const auto doorTour = Pathing::PlanCoverage(raw, start, 12, 1);
        const auto rightPoints = std::count_if(doorTour.begin(), doorTour.end(), [](auto p){return p.x > 30;});
        assert(!exactPath.empty() && paddedPath.empty() && rightPoints > 0);
        Point frontier;
        int steps = 0;
        assert(FindNearest(raw, start, 1, [](int x, int) { return x > 30; }, frontier, steps));
        assert(frontier.x > 30 && steps > 0);
        Grid corner;
        corner.Reset(0, 0, 3, 3);
        std::fill(corner.level.begin(), corner.level.end(), uint16_t(1));
        corner.blocked[corner.Index(0, 0)] = corner.blocked[corner.Index(1, 1)] = 0;
        assert(!FindNearest(corner, {0, 0}, 1, [](int x, int y) { return x == 1 && y == 1; }, frontier, steps));
        assert(FindPath(corner, {0, 0}, {1, 1}).empty());
        assert(PlanCoverage(corner, {0, 0}, 2, 1).size() == 1);

    }

    // Compute budget: planning a large open level (Blood Moor is 480 x 280) must
    // fit comfortably in a game frame (40 ms); allow 20 ms.
    Grid large;
    large.Reset(0, 0, 480, 280);
    std::fill(large.blocked.begin(), large.blocked.end(), uint8_t(0));
    std::fill(large.level.begin(), large.level.end(), uint16_t(1));
    const auto began = std::chrono::steady_clock::now();
    const std::vector<Point> sweep = PlanCoverage(large, { 5, 5 }, 12);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    std::printf("coverage plan for 480x280: %zu points in %.1f ms\n", sweep.size(), ms);
    std::fflush(stdout);
    assert(sweep.size() == size_t(40 * 24));
    assert(ms < 20);

    CheckMovementStart();
    CheckExploration();
    CheckGroundHazards();
    {
        // Ground to keep off: the way goes around a zone, and a zone that
        // holds the start or the goal is not marked at all.
        Grid open;
        open.Reset(0, 0, 60, 30);
        for (auto& cell : open.level) cell = 1;
        for (auto& cell : open.blocked) cell = 0;
        const Point from{ 5, 15 }, to{ 55, 15 };
        const std::vector<Zone> pack{ { 30.0, 15.0, 8.0 } };
        Grid around = open;
        assert(BlockZones(around, pack, from, &to) == 1);
        const auto direct = FindPath(open, from, to), detour = FindPath(around, from, to);
        assert(!direct.empty() && !detour.empty()); // diagonal steps make the way round no longer in cells
        for (const Point& p : detour) {
            const double dx = p.x + 0.5 - 30.0, dy = p.y + 0.5 - 15.0;
            assert(dx * dx + dy * dy > 8.0 * 8.0);
        }
        for (const Point& p : Simplify(around, detour, 15)) assert(around.Passable(p.x, p.y));
        Grid here = open;
        assert(BlockZones(here, { { 6.0, 15.0, 8.0 } }, from, &to) == 0 && here.Passable(8, 15));
        Grid there = open;
        assert(BlockZones(there, { { 54.0, 15.0, 8.0 } }, from, &to) == 0 && there.Passable(52, 15));
        // A zone across the whole passage leaves no way round within the search bound.
        Grid closed = open;
        assert(BlockZones(closed, { { 30.0, 15.0, 20.0 } }, from, &to) == 1);
        assert(FindPath(closed, from, to, 150000).empty());
    }

    {
        // A doorway the margin closes: the route keeps to open ground and
        // squeezes only at the door, where the plain shortest path on the
        // exact grid runs along the wall's foot on both sides.
        Grid exact;
        exact.Reset(0, 0, 40, 16);
        for (auto& cell : exact.level) cell = 1;
        for (auto& cell : exact.blocked) cell = 0;
        for (int x = 0; x < 40; ++x) if (x != 30) exact.blocked[exact.Index(x, 8)] = 1;
        Grid padded = exact;
        padded.Inflate(1);
        const Point from{ 3, 12 }, to{ 3, 4 };
        const GoalTest there = [to](int x, int y) { return x == to.x && y == to.y; };
        assert(FindPath(padded, from, to).empty());
        auto inMargin = [&padded](const std::vector<Point>& cells) {
            int count = 0;
            for (const Point& p : cells) if (!padded.Passable(p.x, p.y)) ++count;
            return count;
        };
        const auto hugging = FindPath(exact, from, to);
        const auto squeezing = FindPathSqueezing(exact, padded, from, there, to);
        assert(!hugging.empty() && !squeezing.empty());
        assert(inMargin(hugging) > 20 && inMargin(squeezing) == 3);
        const auto legs = SimplifySqueezing(exact, padded, squeezing, 15);
        assert(legs.front() == from && legs.back() == to);
        for (size_t i = 1; i < legs.size(); ++i) {
            const Point a = legs[i - 1], b = legs[i];
            const int cells = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
            assert(LineClear(padded, a, b) || (cells <= 3 && LineClear(exact, a, b)));
        }
        // With open ground everywhere it is the ordinary shortest path.
        Grid open = exact;
        for (auto& cell : open.blocked) cell = 0;
        assert(FindPathSqueezing(open, open, from, there, to).size() == FindPath(open, from, to).size());
    }

    {
        // The sweep of a level, walked on paper: how many steps until nothing
        // worth searching is left, with sight as the game marks it.
        auto mark = [](const Grid& grid, std::vector<uint8_t>& seen, Point from, int radius) {
            for (int y = from.y - radius; y <= from.y + radius; ++y) {
                for (int x = from.x - radius; x <= from.x + radius; ++x) {
                    if (!grid.Contains(x, y) || (x - from.x) * (x - from.x) + (y - from.y) * (y - from.y) > radius * radius) continue;
                    const size_t index = grid.Index(x, y);
                    if (!seen[index] && (grid.blocked[index] || LineClear(grid, from, { x, y }))) seen[index] = 1;
                }
            }
        };
        auto walk = [&mark](const Grid& grid, Point start, bool organized) {
            std::vector<uint8_t> seen(grid.blocked.size(), 0);
            ExploreOptions options;
            options.organized = organized;
            Point at = start;
            int walked = 0;
            mark(grid, seen, at, 24);
            for (int legs = 0; legs < 2000; ++legs) {
                const ExploreChoice choice = ChooseExploration(grid, at, 1, seen, options);
                if (!choice.found) return walked;
                const auto path = FindPath(grid, at, choice.point);
                assert(!path.empty());
                options.headingX = choice.point.x - at.x; options.headingY = choice.point.y - at.y;
                for (size_t i = 1; i < path.size(); ++i) {
                    at = path[i]; ++walked;
                    if (i % 2 == 0 || i + 1 == path.size()) mark(grid, seen, at, 24);
                    const int dx = choice.point.x - at.x, dy = choice.point.y - at.y;
                    if (seen[grid.Index(choice.point.x, choice.point.y)] && dx * dx + dy * dy <= 144) break;
                }
            }
            assert(false && "sweep did not finish");
            return walked;
        };
        auto carve = [](Grid& grid, int left, int top, int right, int bottom) {
            for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) grid.blocked[grid.Index(x, y)] = 0;
        };
        // A long passage with rooms off both sides, entered at one end.
        Grid hall;
        hall.Reset(0, 0, 420, 120);
        for (auto& cell : hall.level) cell = 1;
        carve(hall, 2, 54, 417, 65);
        for (int i = 0; i < 8; ++i) {
            const int x = 30 + i * 48;
            if (i % 2 == 0) { carve(hall, x, 8, x + 34, 44); carve(hall, x + 14, 45, x + 19, 53); }
            else { carve(hall, x, 75, x + 34, 111); carve(hall, x + 14, 66, x + 19, 74); }
        }
        // A cave: a trunk with branches of different lengths, some forked.
        Grid cave;
        cave.Reset(0, 0, 360, 260);
        for (auto& cell : cave.level) cell = 1;
        carve(cave, 4, 124, 355, 135);
        for (int i = 0; i < 6; ++i) {
            const int x = 40 + i * 52, reach = 40 + (i * 37) % 70;
            if (i % 2 == 0) {
                carve(cave, x, 124 - reach, x + 9, 123);
                if (i % 4 == 0) carve(cave, x - 30, 124 - reach, x + 40, 133 - reach);
            } else {
                carve(cave, x, 136, x + 9, 135 + reach);
                carve(cave, x, 126 + reach, x + 45, 135 + reach);
            }
        }
        // Open ground with a few walls.
        Grid meadow;
        meadow.Reset(0, 0, 300, 220);
        for (auto& cell : meadow.level) cell = 1;
        carve(meadow, 2, 2, 297, 217);
        for (int i = 0; i < 5; ++i) for (int y = 30 + i * 30; y < 60 + i * 30; ++y) meadow.blocked[meadow.Index(50 + i * 45, y)] = 1;
        const struct { const char* name; const Grid* grid; Point start; double bound; } maps[] = {
            { "hall with side rooms", &hall, { 6, 60 }, 1.05 }, { "branching cave", &cave, { 8, 130 }, 0.8 },
            { "open field", &meadow, { 10, 110 }, 0.85 } };
        for (const auto& map : maps) {
            const int before = walk(*map.grid, map.start, false), after = walk(*map.grid, map.start, true);
            // Never much worse than the earlier ranking, and a quarter better where there are branches to miss.
            assert(after <= before * map.bound);
            std::printf("sweep of %s: %d steps organized, %d by ground gained per step\n", map.name, after, before);
        }
    }

    std::puts("pathfinding checks passed");
    return 0;
}
