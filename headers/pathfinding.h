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

// For when the margin closes the only way (a narrow doorway): A* on `exact`
// where each cell the margin of `padded` covers costs `marginCost` steps. The
// route keeps to open ground and squeezes only where it must, for as few
// cells as it can. The plain shortest path on `exact` hugged the walls the
// whole way instead, and the game accepts a step along a wall's foot or into
// a corner without walking it.
std::vector<Point> FindPathSqueezing(const Grid& exact, const Grid& padded, Point start, const GoalTest& isGoal,
    Point goalHint, int marginCost = 12, size_t maxExpansions = 2000000);

// Waypoints for such a path: straight lines only over open ground (clear in
// `padded`); through the margin the path's own cells are kept, at most
// `squeezeStep` apart, so the squeeze is walked where it was planned.
std::vector<Point> SimplifySqueezing(const Grid& exact, const Grid& padded, const std::vector<Point>& path,
    int maxStep, int squeezeStep = 3);

// Ground to keep off when another way exists (a dangerous pack): a disc.
struct Zone {
    double x = 0, y = 0, radius = 0;
};

// Marks the cells of every zone impassable, except zones that hold `start` or
// `goal`: what we stand in, or must reach, cannot be gone around. Returns how
// many zones were marked.
size_t BlockZones(Grid& grid, const std::vector<Zone>& zones, Point start, const Point* goal);

// True when every cell on the line from a to b is passable.
bool LineClear(const Grid& grid, Point a, Point b);

// Reduces a cell path to waypoints with clear straight lines, each at most `maxStep` apart.
std::vector<Point> Simplify(const Grid& grid, const std::vector<Point>& path, int maxStep);

// Closest passable cell within `maxRadius` (Chebyshev rings), or false.
bool NearestPassable(const Grid& grid, Point near, int maxRadius, Point& found);

// Movement must start at the actual cell; snapping could cross a wall before
// the first waypoint. False lets the caller retry without clearance inflation.
bool MovementStart(const Grid& grid, Point actual, Point& start);

// Permit departure from the occupied actual cell only. All neighboring
// collision stays intact; an unknown or enclosed start cannot be opened.
bool OpenMovementStart(Grid& grid, Point actual);

struct Threat {
    double x = 0, y = 0;
    double weight = 1;
};

// A client-observed missile's short projected flight, in world subtiles.
struct Missile {
    double x = 0, y = 0, toX = 0, toY = 0;
};

struct GroundHazard {
    double x = 0, y = 0;
    int size = 1; // game footprint: 1 point, 2 cross, 3 square
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
    double rangeWeight = 0.15;  // score cost per subtile beyond the target range
    double headingX = 0, headingY = 0; // normalized internally; prefer onward progress only on score ties
    double headingWeight = 0;   // score gained per subtile of progress along the heading: with it a dodge on
                                // the way somewhere goes onward out of a lane, not back or aside (0: ties only)
    std::vector<Missile> missiles; // avoid crossing imminent flight lanes
    std::vector<GroundHazard> hazards; // confirmed harmful client ground effects
    // Optional live check (for example the game's current collision, which
    // includes monster footprints); true means the cell cannot be entered now.
    std::function<bool(int x, int y)> liveBlocked;
};

struct SafeSpot {
    bool found = false;
    Point spot;
    double dangerHere = 0, dangerThere = 0;
    double missileHere = 0, missileThere = 0;
    double hazardHere = 0, hazardThere = 0;
    int openSectors = 0;        // of 8 compass sectors, how many still offer a wide, safer way out
    std::vector<Point> path;    // cells from the player to the spot, inclusive
};

// Danger at a point: each threat contributes its weight, fading linearly to zero at `reach`.
double Danger(const std::vector<Threat>& threats, double x, double y, double reach);

double MissileDanger(const std::vector<Missile>& missiles, double x, double y);
// Ground footprints overlap the player's size-2 cross; positions name floor cells.
double GroundDanger(const std::vector<GroundHazard>& hazards, double x, double y);
// Includes the endpoints and every touched cell, including diagonal corners.
bool GroundHazardClear(const std::vector<GroundHazard>& hazards, double x1, double y1, double x2, double y2);

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

// Nearest reachable goal by 8-way walking distance, without corner cutting.
bool FindNearest(const Grid& grid, Point start, uint16_t level, const GoalTest& isGoal, Point& result, int& distance);

struct ExploreTarget {
    uint32_t id = 0;
    Point point;
};

struct ExploreOptions {
    int minimum = 24, cell = 12;
    double headingX = 0, headingY = 0;
    std::vector<ExploreTarget> targets; // caller supplies alive hostiles of this level only
    std::vector<Point> exclude;        // temporarily suppress entire failed destination blocks
    bool organized = true;             // false: the earlier ranking, kept for comparison in the tests
};

struct ExploreChoice {
    bool found = false, monster = false;
    uint32_t id = 0;
    int steps = 0, gain = 0, unseen = 0, total = 0;
    Point point;
};

// One same-level flood measures 8-way walking steps, without corner cutting.
// Monsters take priority by shortest distance. Otherwise rank connected unseen
// components within each block by gain/(steps+12), with at most 5% heading bias.
// Coverage counts reachable ground; seen must have the same indexing as grid.
ExploreChoice ChooseExploration(const Grid& grid, Point start, uint16_t level,
    const std::vector<uint8_t>& seen, const ExploreOptions& options);
}
