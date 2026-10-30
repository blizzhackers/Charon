/**
 * Grid path finding for scripts, independent of the game so it can be tested.
 * Coordinates are world subtiles. Cells are passable or not; unknown space
 * (outside every known room) is impassable.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Pathing {

struct Point {
    int x = 0, y = 0;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

struct Grid {
    int originX = 0, originY = 0, width = 0, height = 0;
    std::vector<uint8_t> blocked;   // 1 = impassable, after clearance inflation
    std::vector<uint16_t> level;    // level id per cell, 0 = unknown

    void Reset(int x, int y, int w, int h);
    bool Contains(int x, int y) const { return x >= originX && y >= originY && x < originX + width && y < originY + height; }
    size_t Index(int x, int y) const { return size_t(y - originY) * width + size_t(x - originX); }
    bool Passable(int x, int y) const { return Contains(x, y) && !blocked[Index(x, y)]; }
    uint16_t LevelAt(int x, int y) const { return Contains(x, y) ? level[Index(x, y)] : 0; }
    // Marks every cell within `radius` of an impassable cell impassable, so paths keep a margin.
    void Inflate(int radius);
};

using GoalTest = std::function<bool(int x, int y)>;

// 8-connected A* without corner cutting. `goalHint` steers the search (it is the
// goal itself for point searches). Returns the cell path including start and
// goal, or empty when unreachable within `maxExpansions`.
std::vector<Point> FindPath(const Grid& grid, Point start, const GoalTest& isGoal, Point goalHint, size_t maxExpansions = 2000000);
std::vector<Point> FindPath(const Grid& grid, Point start, Point goal, size_t maxExpansions = 2000000);

// True when every cell on the line from a to b is passable.
bool LineClear(const Grid& grid, Point a, Point b);

// Reduces a cell path to waypoints with clear straight lines, each at most `maxStep` apart.
std::vector<Point> Simplify(const Grid& grid, const std::vector<Point>& path, int maxStep);

// Closest passable cell within `maxRadius` (Chebyshev rings), or false.
bool NearestPassable(const Grid& grid, Point near, int maxRadius, Point& found);

struct Threat {
    double x = 0, y = 0;
    double weight = 1;
};

struct SafeSpotOptions {
    int radius = 14;            // search window around the player
    double occupied = 2.5;      // cells closer than this to a threat are unusable
    double reach = 9;           // a threat's danger fades to zero at this distance
    double lead = 2;            // threats are also assumed this much closer: gaps about to close count as closed
    int clearance = 2;          // routes and spots keep this many subtiles from anything blocked
    double travelWeight = 0.04; // score cost per subtile walked
    bool hasTarget = false;     // keep a target in range and in line of fire
    double targetX = 0, targetY = 0;
    double range = 7;
    // Optional live check (for example the game's current collision, which
    // includes monster footprints); true means the cell cannot be entered now.
    std::function<bool(int x, int y)> liveBlocked;
};

struct SafeSpot {
    bool found = false;
    Point spot;
    double dangerHere = 0, dangerThere = 0;
    int openSectors = 0;        // of 8 compass sectors, how many still offer a wide, safer way out
    std::vector<Point> path;    // cells from the player to the spot, inclusive
};

// Danger at a point: each threat contributes its weight, fading linearly to zero at `reach`.
double Danger(const std::vector<Threat>& threats, double x, double y, double reach);

// Best nearby cell to stand on: reachable along routes at least `clearance`
// wide that cross no occupied cells (Dijkstra where dangerous cells cost
// more), lowest danger, short trip, and when a target is given, within range
// and with a clear line to it. Threats count both where they are and `lead`
// closer to the player. Also counts the open escape sectors around the player.
SafeSpot FindSafeSpot(const Grid& grid, Point player, const std::vector<Threat>& threats, const SafeSpotOptions& options);

// Sweep plan for exploring: blocks of `cell` subtiles of ground reachable from
// `start` (only cells of `level` when nonzero), each represented by its
// reachable cell nearest the block centre, ordered depth-first (dead ends
// first, heading kept on ties). Walking the points in order passes within
// about `cell` of all reachable ground. Linear in the grid size: one flood
// fill plus one pass over the blocks, well within a game frame.
std::vector<Point> PlanCoverage(const Grid& grid, Point start, int cell, uint16_t level = 0);

}
