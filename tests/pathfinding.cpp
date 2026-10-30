#include "headers/pathfinding.h"
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
    for (const Point& p : spot.path) assert(!(p.x == 5 && p.y == 10));

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

    std::puts("pathfinding checks passed");
    return 0;
}
