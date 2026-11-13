/**
 * Grid path finding for scripts. See headers/pathfinding.h.
 */
#include "headers/pathfinding.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace Pathing {

namespace {

const int StraightCost = 10, DiagonalCost = 14;
const int Directions[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

int Octile(Point a, Point b) {
    const int dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
    return StraightCost * (dx + dy) + (DiagonalCost - 2 * StraightCost) * std::min(dx, dy);
}

bool GroundCellOverlap(int size, int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    // AddCollision_Width (0x64EA00): point, cross, or 3x3, expanded
    // by the player's cross (GetUnitSizeX 0x620510).
    if (size == 1) return dx + dy <= 1;
    if (size == 2) return dx + dy <= 2;
    return size == 3 && std::max(dx, dy) <= 2 && dx + dy <= 3;
}

}

void Grid::Reset(int x, int y, int w, int h) {
    originX = x;
    originY = y;
    width = w;
    height = h;
    blocked.assign(size_t(w) * size_t(h), 1);
    level.assign(size_t(w) * size_t(h), 0);
}

void Grid::Inflate(int radius) {
    if (radius <= 0 || blocked.empty()) return;
    // Separable dilation: rows, then columns.
    std::vector<uint8_t> rows(blocked.size(), 0);
    for (int y = 0; y < height; ++y) {
        int last = -radius - 1;
        for (int x = 0; x < width; ++x) {
            if (blocked[size_t(y) * width + x]) last = x;
            if (x - last <= radius) rows[size_t(y) * width + x] = 1;
        }
        last = width + radius + 1;
        for (int x = width - 1; x >= 0; --x) {
            if (blocked[size_t(y) * width + x]) last = x;
            if (last - x <= radius) rows[size_t(y) * width + x] = 1;
        }
    }
    for (int x = 0; x < width; ++x) {
        int last = -radius - 1;
        for (int y = 0; y < height; ++y) {
            if (rows[size_t(y) * width + x]) last = y;
            if (y - last <= radius) blocked[size_t(y) * width + x] = 1;
        }
        last = height + radius + 1;
        for (int y = height - 1; y >= 0; --y) {
            if (rows[size_t(y) * width + x]) last = y;
            if (last - y <= radius) blocked[size_t(y) * width + x] = 1;
        }
    }
}

namespace {
std::vector<Point> Search(const Grid& grid, const Grid* padded, int marginCost, Point start, const GoalTest& isGoal,
    Point goalHint, size_t maxExpansions);
}

std::vector<Point> FindPath(const Grid& grid, Point start, const GoalTest& isGoal, Point goalHint, size_t maxExpansions) {
    return Search(grid, nullptr, 1, start, isGoal, goalHint, maxExpansions);
}

std::vector<Point> FindPathSqueezing(const Grid& exact, const Grid& padded, Point start, const GoalTest& isGoal,
    Point goalHint, int marginCost, size_t maxExpansions) {
    return Search(exact, &padded, std::max(1, marginCost), start, isGoal, goalHint, maxExpansions);
}

namespace {
std::vector<Point> Search(const Grid& grid, const Grid* padded, int marginCost, Point start, const GoalTest& isGoal,
    Point goalHint, size_t maxExpansions) {
    if (!grid.Passable(start.x, start.y)) return {};
    const size_t cells = grid.blocked.size();
    std::vector<int32_t> cost(cells, INT32_MAX);
    std::vector<int32_t> parent(cells, -1);
    std::vector<uint8_t> closed(cells, 0);
    using Entry = std::pair<int32_t, int32_t>; // (estimate, cell index)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const int32_t startIndex = (int32_t)grid.Index(start.x, start.y);
    cost[startIndex] = 0;
    open.push({ Octile(start, goalHint), startIndex });
    size_t expansions = 0;
    while (!open.empty() && expansions++ < maxExpansions) {
        const int32_t current = open.top().second;
        open.pop();
        if (closed[current]) continue;
        closed[current] = 1;
        const Point p = { grid.originX + current % grid.width, grid.originY + current / grid.width };
        if (isGoal(p.x, p.y)) {
            std::vector<Point> path;
            for (int32_t i = current; i >= 0; i = parent[i]) {
                path.push_back({ grid.originX + i % grid.width, grid.originY + i / grid.width });
            }
            std::reverse(path.begin(), path.end());
            return path;
        }
        for (int d = 0; d < 8; ++d) {
            const int nx = p.x + Directions[d][0], ny = p.y + Directions[d][1];
            if (!grid.Passable(nx, ny)) continue;
            // No corner cutting: both orthogonal neighbours must be open for a diagonal step.
            if (d >= 4 && (!grid.Passable(nx, p.y) || !grid.Passable(p.x, ny))) continue;
            const int32_t next = (int32_t)grid.Index(nx, ny);
            if (closed[next]) continue;
            const int32_t step = d >= 4 ? DiagonalCost : StraightCost;
            const int32_t nextCost = cost[current] + (padded && !padded->Passable(nx, ny) ? step * marginCost : step);
            if (nextCost >= cost[next]) continue;
            cost[next] = nextCost;
            parent[next] = current;
            open.push({ nextCost + Octile({ nx, ny }, goalHint), next });
        }
    }
    return {};
}
}

std::vector<Point> FindPath(const Grid& grid, Point start, Point goal, size_t maxExpansions) {
    if (!grid.Passable(goal.x, goal.y)) return {};
    return FindPath(grid, start, [goal](int x, int y) { return x == goal.x && y == goal.y; }, goal, maxExpansions);
}

bool LineClear(const Grid& grid, Point a, Point b) {
    // Supercover-style walk: checks every cell the segment passes through.
    int x = a.x, y = a.y;
    const int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const int sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
    int error = dx - dy;
    if (!grid.Passable(x, y)) return false;
    while (x != b.x || y != b.y) {
        const int e2 = 2 * error;
        const bool stepX = e2 > -dy, stepY = e2 < dx;
        if (stepX && stepY) {
            // Diagonal move: both side cells must be open, as for A*.
            if (!grid.Passable(x + sx, y) || !grid.Passable(x, y + sy)) return false;
        }
        if (stepX) {
            error -= dy;
            x += sx;
        }
        if (stepY) {
            error += dx;
            y += sy;
        }
        if (!grid.Passable(x, y)) return false;
    }
    return true;
}

std::vector<Point> Simplify(const Grid& grid, const std::vector<Point>& path, int maxStep) {
    if (path.size() <= 2) return path;
    std::vector<Point> waypoints = { path.front() };
    size_t anchor = 0;
    while (anchor + 1 < path.size()) {
        size_t best = anchor + 1;
        for (size_t candidate = anchor + 2; candidate < path.size(); ++candidate) {
            const Point a = path[anchor], b = path[candidate];
            const double length = std::hypot(double(b.x - a.x), double(b.y - a.y));
            if (length > maxStep) break;
            if (LineClear(grid, a, b)) best = candidate;
        }
        waypoints.push_back(path[best]);
        anchor = best;
    }
    return waypoints;
}

std::vector<Point> SimplifySqueezing(const Grid& exact, const Grid& padded, const std::vector<Point>& path,
    int maxStep, int squeezeStep) {
    if (path.size() <= 2) return path;
    std::vector<Point> waypoints = { path.front() };
    size_t anchor = 0;
    while (anchor + 1 < path.size()) {
        const Point a = path[anchor];
        size_t best = anchor + 1;
        for (size_t candidate = anchor + 2; candidate < path.size(); ++candidate) {
            const Point b = path[candidate];
            if (std::hypot(double(b.x - a.x), double(b.y - a.y)) > maxStep) break;
            if (LineClear(padded, a, b)) best = candidate;
        }
        if (best == anchor + 1) {
            // In or beside the margin: follow the planned cells a short way at a time.
            for (size_t candidate = anchor + 2; candidate < path.size() && candidate - anchor <= size_t(squeezeStep); ++candidate) {
                if (!LineClear(exact, a, path[candidate])) break;
                best = candidate;
            }
        }
        waypoints.push_back(path[best]);
        anchor = best;
    }
    return waypoints;
}

size_t BlockZones(Grid& grid, const std::vector<Zone>& zones, Point start, const Point* goal) {
    size_t marked = 0;
    auto inside = [](const Zone& zone, Point p) {
        const double dx = p.x + 0.5 - zone.x, dy = p.y + 0.5 - zone.y;
        return dx * dx + dy * dy <= zone.radius * zone.radius;
    };
    for (const Zone& zone : zones) {
        if (zone.radius <= 0 || inside(zone, start) || (goal && inside(zone, *goal))) continue;
        const int reach = (int)std::ceil(zone.radius);
        const int cx = (int)std::floor(zone.x), cy = (int)std::floor(zone.y);
        for (int y = cy - reach; y <= cy + reach; ++y) {
            for (int x = cx - reach; x <= cx + reach; ++x) {
                if (grid.Contains(x, y) && inside(zone, { x, y })) grid.blocked[grid.Index(x, y)] = 1;
            }
        }
        ++marked;
    }
    return marked;
}

bool NearestPassable(const Grid& grid, Point near, int maxRadius, Point& found) {
    for (int radius = 0; radius <= maxRadius; ++radius) {
        bool hit = false;
        double bestDistance = 0;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                if (!grid.Passable(near.x + dx, near.y + dy)) continue;
                const double distance = std::hypot(double(dx), double(dy));
                if (!hit || distance < bestDistance) {
                    hit = true;
                    bestDistance = distance;
                    found = { near.x + dx, near.y + dy };
                }
            }
        }
        if (hit) return true;
    }
    return false;
}

bool MovementStart(const Grid& grid, Point actual, Point& start) {
    if (!grid.Passable(actual.x, actual.y)) return false;
    start = actual;
    return true;
}

bool OpenMovementStart(Grid& grid, Point actual) {
    if (!grid.Contains(actual.x, actual.y) || !grid.LevelAt(actual.x, actual.y)) return false;
    if (grid.Passable(actual.x, actual.y)) return true;
    for (int d = 0; d < 4; ++d) {
        if (grid.Passable(actual.x + Directions[d][0], actual.y + Directions[d][1])) {
            grid.blocked[grid.Index(actual.x, actual.y)] = 0;
            return true;
        }
    }
    return false;
}

double Danger(const std::vector<Threat>& threats, double x, double y, double reach) {
    double danger = 0;
    for (const Threat& t : threats) {
        const double d = std::hypot(t.x - x, t.y - y);
        if (d < reach) danger += t.weight * (1 - std::max(0.0, d - 1) / std::max(1.0, reach - 1));
    }
    return danger;
}

double MissileDanger(const std::vector<Missile>& missiles, double x, double y) {
    double danger = 0;
    for (const Missile& m : missiles) {
        const double dx = m.toX - m.x, dy = m.toY - m.y;
        const double length2 = dx * dx + dy * dy;
        if (length2 < 0.01) continue; // stationary effects have no observed flight
        const double projection = ((x - m.x) * dx + (y - m.y) * dy) / length2;
        if (projection < 0) continue; // the missile has already passed this point
        const double along = std::min(projection, 1.0);
        const double distance = std::hypot(x - (m.x + along * dx), y - (m.y + along * dy));
        // A short sidestep clears the lane; running directly away does not.
        danger += 2 * std::max(0.0, 1 - distance / 1.5);
    }
    return danger;
}

double GroundDanger(const std::vector<GroundHazard>& hazards, double x, double y) {
    double danger = 0;
    const int px = (int)std::floor(x), py = (int)std::floor(y);
    for (const GroundHazard& hazard : hazards) {
        const int hx = (int)std::floor(hazard.x), hy = (int)std::floor(hazard.y);
        if (GroundCellOverlap(hazard.size, hx - px, hy - py)) danger += 100;
    }
    return danger;
}

bool GroundHazardClear(const std::vector<GroundHazard>& hazards, double x1, double y1, double x2, double y2) {
    if (x1 == x2 && y1 == y2) return GroundDanger(hazards, x1, y1) == 0;
    for (const GroundHazard& hazard : hazards) {
        const int hx = (int)std::floor(hazard.x), hy = (int)std::floor(hazard.y);
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (!GroundCellOverlap(hazard.size, dx, dy)) continue;
                // Clip the segment against each expanded cell. Closed edges
                // include corner-touching cells, with work bounded by hazards.
                double begin = 0, end = 1;
                auto clip = [&](double from, double delta, int cell) {
                    if (delta == 0) return from >= cell && from <= cell + 1;
                    const double a = (cell - from) / delta, b = (cell + 1 - from) / delta;
                    begin = std::max(begin, std::min(a, b));
                    end = std::min(end, std::max(a, b));
                    return begin <= end;
                };
                if (clip(x1, x2-x1, hx+dx) && clip(y1, y2-y1, hy+dy)) return false;
            }
        }
    }
    return true;
}

SafeSpot FindSafeSpot(const Grid& grid, Point player, const std::vector<Threat>& threats, const SafeSpotOptions& options) {
    SafeSpot result;
    const int r = std::max(2, options.radius);
    const int side = 2 * r + 1;
    const int originX = player.x - r, originY = player.y - r;
    const size_t cells = size_t(side) * side;
    auto inside = [&](int x, int y) { return x >= originX && y >= originY && x < originX + side && y < originY + side; };
    auto index = [&](int x, int y) { return size_t(y - originY) * side + size_t(x - originX); };
    const double px = player.x + 0.5, py = player.y + 0.5;
    const double headingScale = std::max(std::abs(options.headingX), std::abs(options.headingY));
    const double headingLength = headingScale > 0 && std::isfinite(headingScale)
        ? std::hypot(options.headingX / headingScale, options.headingY / headingScale) : 0;
    const double headingX = headingLength > 0 ? (options.headingX / headingScale) / headingLength : 0;
    const double headingY = headingLength > 0 ? (options.headingY / headingScale) / headingLength : 0;

    // Threats where they are and where they will be shortly (moving toward us).
    std::vector<Threat> predicted = threats;
    for (Threat& t : predicted) {
        const double d = std::hypot(px - t.x, py - t.y);
        // Never projected closer than the occupied radius: a monster already at
        // our side has not cut off the other side.
        const double step = std::min(options.lead, d - options.occupied);
        if (step > 0) {
            t.x += (px - t.x) / d * step;
            t.y += (py - t.y) / d * step;
        }
    }

    // Per-cell danger (from predicted positions) and usability.
    std::vector<double> danger(cells, 0), ground(cells, 0);
    std::vector<uint8_t> usable(cells, 0);
    for (int y = originY; y < originY + side; ++y) {
        for (int x = originX; x < originX + side; ++x) {
            const size_t i = index(x, y);
            ground[i] = GroundDanger(options.hazards, x + 0.5, y + 0.5);
            danger[i] = Danger(predicted, x + 0.5, y + 0.5, options.reach)
                + MissileDanger(options.missiles, x + 0.5, y + 0.5) + ground[i];
            bool occupied = false;
            for (size_t t = 0; t < threats.size() && !occupied; ++t) {
                occupied = std::hypot(threats[t].x - (x + 0.5), threats[t].y - (y + 0.5)) < options.occupied
                    || std::hypot(predicted[t].x - (x + 0.5), predicted[t].y - (y + 0.5)) < options.occupied;
            }
            usable[i] = grid.Passable(x, y) && !occupied && !(options.liveBlocked && options.liveBlocked(x, y)) ? 1 : 0;
        }
    }
    result.missileHere = MissileDanger(options.missiles, px, py);
    result.hazardHere = GroundDanger(options.hazards, px, py);
    result.dangerHere = Danger(predicted, px, py, options.reach) + result.missileHere + result.hazardHere;
    // The player's own cell is always a start, even when crowded.
    const size_t start = index(player.x, player.y);
    usable[start] = 1;

    // Clearance: Chebyshev distance to the nearest unusable cell (capped), so a
    // route needs a corridor, not a one-subtile gap that closes as monsters move.
    const int wanted = std::max(1, options.clearance);
    const uint8_t cap = uint8_t(wanted + 1);
    std::vector<uint8_t> clearance(cells, cap);
    std::vector<int32_t> frontier;
    for (size_t i = 0; i < cells; ++i) {
        if (!usable[i]) {
            clearance[i] = 0;
            frontier.push_back((int32_t)i);
        }
    }
    for (uint8_t level = 1; level < cap && !frontier.empty(); ++level) {
        std::vector<int32_t> next;
        for (int32_t i : frontier) {
            const int cx = originX + i % side, cy = originY + i / side;
            for (int d = 0; d < 8; ++d) {
                const int nx = cx + Directions[d][0], ny = cy + Directions[d][1];
                if (!inside(nx, ny)) continue;
                const size_t n = index(nx, ny);
                if (clearance[n] > level) {
                    clearance[n] = level;
                    next.push_back((int32_t)n);
                }
            }
        }
        frontier.swap(next);
    }

    // Dijkstra over usable cells. `cost` (danger-weighted) picks the route;
    // `length` (plain steps) prices the trip. Beyond the first two steps out of
    // where we stand, a route needs the full clearance.
    std::vector<double> cost(cells, 1e30);
    std::vector<double> length(cells, 0);
    std::vector<int32_t> parent(cells, -1);
    using Entry = std::pair<double, int32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    cost[start] = 0;
    open.push({ 0, (int32_t)start });
    while (!open.empty()) {
        const auto [c, current] = open.top();
        open.pop();
        if (c > cost[current]) continue;
        const int cx = originX + current % side, cy = originY + current / side;
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + Directions[d][0], ny = cy + Directions[d][1];
            if (!inside(nx, ny) || !usable[index(nx, ny)]) continue;
            if (d >= 4 && (!usable[index(nx, cy)] || !usable[index(cx, ny)])) continue;
            const size_t next = index(nx, ny);
            // Escape an existing overlap, but never enter fire once clear.
            // A diagonal also sweeps its two orthogonal side cells.
            if (ground[current] == 0 && (ground[next] > 0 || (d >= 4
                && (ground[index(nx, cy)] > 0 || ground[index(cx, ny)] > 0)))) continue;
            if (clearance[next] < wanted && length[current] >= 2) continue;
            const double distance = d >= 4 ? 1.414 : 1.0;
            const double step = distance * (1 + 6 * danger[next]);
            if (c + step < cost[next]) {
                cost[next] = c + step;
                length[next] = length[current] + distance;
                parent[next] = current;
                open.push({ c + step, (int32_t)next });
            }
        }
    }

    // Score reachable, roomy cells: low danger, short trip, target in range and in sight.
    auto targetPenalty = [&](int x, int y) {
        if (!options.hasTarget) return 0.0;
        const double d = std::hypot(options.targetX - (x + 0.5), options.targetY - (y + 0.5));
        double penalty = 0;
        if (d > options.range) penalty += (d - options.range) * options.rangeWeight;
        const Point target = { (int)std::floor(options.targetX), (int)std::floor(options.targetY) };
        if (!LineClear(grid, { x, y }, target)) penalty += 0.25;
        return penalty;
    };
    const double pi = 3.14159265358979;
    const double stayScore = -(result.dangerHere + targetPenalty(player.x, player.y));
    double bestScore = result.hazardHere > 0 ? -1e30 : stayScore;
    double bestForward = 0;
    int32_t best = -1;
    bool sectorOpen[8] = { false };
    for (size_t i = 0; i < cells; ++i) {
        if (cost[i] >= 1e30 || !usable[i] || clearance[i] < wanted || ground[i] > 0) continue;
        const int x = originX + int(i % side), y = originY + int(i / side);
        // An open sector: a roomy cell at least 6 away that is clearly safer than here.
        const double away = std::hypot(double(x - player.x), double(y - player.y));
        if (away >= 6 && danger[i] < result.dangerHere * 0.6) {
            const double angle = std::atan2(double(y - player.y), double(x - player.x));
            sectorOpen[int(std::floor((angle + pi) / (2 * pi) * 8)) & 7] = true;
        }
        const double forward = (x - player.x) * headingX + (y - player.y) * headingY;
        const double score = -(danger[i] + options.travelWeight * length[i] + targetPenalty(x, y))
            + options.headingWeight * forward;
        const bool better = score > bestScore + 1e-9;
        if (better || (best >= 0 && score > stayScore + 1e-9
            && std::abs(score - bestScore) <= 1e-9 && forward > bestForward + 1e-9)) {
            if (better) bestScore = score; // heading ties never lower the primary score anchor
            bestForward = forward;
            best = (int32_t)i;
        }
    }
    for (bool sector : sectorOpen) result.openSectors += sector ? 1 : 0;
    if (best < 0 || best == (int32_t)start) return result;
    result.found = true;
    result.spot = { originX + best % side, originY + best / side };
    result.dangerThere = danger[best];
    result.missileThere = MissileDanger(options.missiles, result.spot.x + 0.5, result.spot.y + 0.5);
    result.hazardThere = ground[best];
    for (int32_t i = best; i >= 0; i = parent[i]) result.path.push_back({ originX + i % side, originY + i / side });
    std::reverse(result.path.begin(), result.path.end());
    return result;
}

bool FindNearest(const Grid& grid, Point start, uint16_t level, const GoalTest& isGoal, Point& result, int& distance) {
    if (!grid.Passable(start.x, start.y)) return false;

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
        if (isGoal(x, y)) { result = { x, y }; distance = steps[current]; return true; }
        for (const auto& d : directions) {
            const int nx = x + d[0], ny = y + d[1];
            if (!grid.Passable(nx, ny) || grid.LevelAt(nx, ny) != level) continue;
            if (d[0] && d[1] && (!grid.Passable(nx, y) || !grid.Passable(x, ny))) continue;
            const size_t next = grid.Index(nx, ny);
            if (steps[next] >= 0) continue;
            steps[next] = steps[current] + 1;
            queue.push_back((int32_t)next);
        }
    }
    return false;
}

// An unsearched region up to this many blocks counts as a pocket, in
// proportion; a region that size or larger counts 1 + PocketWeight times as far.
const double PocketBlocks = 64, PocketWeight = 4;

ExploreChoice ChooseExploration(const Grid& grid, Point start, uint16_t level,
    const std::vector<uint8_t>& seen, const ExploreOptions& options) {
    ExploreChoice result;
    if (options.cell < 4 || options.cell > 40 || options.minimum < 1 || options.minimum > 1600
        || !std::isfinite(options.headingX) || !std::isfinite(options.headingY)
        || std::abs(options.headingX) > 65535 || std::abs(options.headingY) > 65535
        || options.targets.size() > 512 || options.exclude.size() > 64
        || seen.size() != grid.blocked.size() || !grid.Passable(start.x, start.y)
        || grid.LevelAt(start.x, start.y) != level) return result;
    const int w = grid.width, h = grid.height, cell = options.cell;
    const size_t cells = grid.blocked.size();
    std::vector<uint8_t> walkable(cells);
    std::vector<int32_t> steps(cells, -1), queue;
    queue.reserve(cells);
    for (size_t i = 0; i < cells; ++i) walkable[i] = !grid.blocked[i] && grid.level[i] == level;
    const int32_t first = (int32_t)grid.Index(start.x, start.y);
    steps[first] = 0;
    queue.push_back(first);
    for (size_t head = 0; head < queue.size(); ++head) {
        const int32_t i = queue[head];
        result.unseen += !seen[i];
        const int x = i % w, y = i / w;
        for (const auto& d : Directions) {
            const int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            const int32_t next = ny * w + nx;
            if (!walkable[next] || steps[next] >= 0) continue;
            if (d[0] && d[1] && (!walkable[y * w + nx] || !walkable[ny * w + x])) continue;
            steps[next] = steps[i] + 1;
            queue.push_back(next);
        }
    }
    result.total = (int)queue.size();
    for (const auto& target : options.targets) {
        if (!grid.Contains(target.point.x, target.point.y)) continue;
        const int distance = steps[grid.Index(target.point.x, target.point.y)];
        if (distance < 0 || (result.found && (distance > result.steps
            || (distance == result.steps && target.id >= result.id)))) continue;
        result.found = result.monster = true;
        result.id = target.id;
        result.point = target.point;
        result.steps = distance;
    }
    if (result.found) return result;

    std::vector<uint8_t> visited(cells, 0);
    const int columns = (w + cell - 1) / cell, rows = (h + cell - 1) / cell;
    std::vector<uint8_t> excluded(size_t(columns) * rows, 0);
    for (Point p : options.exclude) {
        if (grid.Contains(p.x, p.y)) excluded[size_t((p.y - grid.originY) / cell) * columns + (p.x - grid.originX) / cell] = 1;
    }
    // How much unsearched ground hangs together with each unsearched cell
    // (4-connected through unsearched cells, the whole level): a small figure
    // is a pocket, a side room or the end of a passage.
    std::vector<int32_t> region(cells, -1);
    std::vector<int32_t> regionSize;
    if (options.organized) {
        std::vector<int32_t> fill;
        for (int32_t seed : queue) {
            if (seen[seed] || region[seed] >= 0) continue;
            const int32_t id = (int32_t)regionSize.size();
            fill.clear();
            fill.push_back(seed);
            region[seed] = id;
            for (size_t head = 0; head < fill.size(); ++head) {
                const int32_t i = fill[head];
                const int x = i % w, y = i / w;
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + Directions[d][0], ny = y + Directions[d][1];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    const int32_t next = ny * w + nx;
                    if (steps[next] < 0 || seen[next] || region[next] >= 0) continue;
                    region[next] = id;
                    fill.push_back(next);
                }
            }
            regionSize.push_back((int32_t)fill.size());
        }
    }
    // Local components keep two pockets on opposite sides of a wall separate,
    // even when they connect by a long route outside this block.
    std::vector<int32_t> component;
    component.reserve(size_t(cell) * cell);
    double bestScore = -1;
    const double headingLength = std::hypot(options.headingX, options.headingY);
    for (int32_t seed : queue) {
        if (seen[seed] || visited[seed]) continue;
        const int bx = seed % w / cell, by = seed / w / cell;
        if (excluded[size_t(by) * columns + bx]) continue;
        const int left = bx * cell, top = by * cell;
        const int right = std::min(w, left + cell), bottom = std::min(h, top + cell);
        component.clear();
        component.push_back(seed);
        visited[seed] = 1;
        double sumX = 0, sumY = 0;
        for (size_t head = 0; head < component.size(); ++head) {
            const int32_t i = component[head];
            const int x = i % w, y = i / w;
            sumX += x;
            sumY += y;
            for (int d = 0; d < 4; ++d) {
                const int nx = x + Directions[d][0], ny = y + Directions[d][1];
                if (nx < left || nx >= right || ny < top || ny >= bottom) continue;
                const int32_t next = ny * w + nx;
                if (steps[next] < 0 || seen[next] || visited[next]) continue;
                visited[next] = 1;
                component.push_back(next);
            }
        }
        const int gain = (int)component.size();
        if (gain < options.minimum) continue;
        const double cx = sumX / gain, cy = sumY / gain;
        int32_t representative = seed;
        double nearest = HUGE_VAL;
        for (int32_t i : component) {
            const double dx = i % w - cx, dy = i / w - cy, distance = dx * dx + dy * dy;
            if (distance < nearest || (distance == nearest && (steps[i] < steps[representative]
                || (steps[i] == steps[representative] && i < representative)))) {
                nearest = distance;
                representative = i;
            }
        }
        const Point p = {grid.originX + representative % w, grid.originY + representative / w};
        const double dx = double(p.x) - start.x, dy = double(p.y) - start.y, length = std::hypot(dx, dy);
        const double alignment = headingLength > 0 && length > 0
            ? (dx * options.headingX + dy * options.headingY) / (length * headingLength) : 0;
        double score = gain / (steps[representative] + 12.0) * (1.0 + 0.05 * alignment);
        if (options.organized) {
            // Nearest first, and what is at hand before what is ahead: ground
            // that belongs to a large unsearched region counts as up to five
            // times as far, so a pocket beside the way is taken while it is
            // close and not walked back to from the far end of the level.
            // (Ranked by ground gained per step, the big patch ahead won and
            // the side passage was left for later.) The two figures are the
            // best of a dozen pairs on the three maps of tests/pathfinding.cpp.
            const double pocket = PocketBlocks * cell * cell;
            const double share = std::min(double(regionSize[region[seed]]), pocket) / pocket;
            score = 1.0 / ((steps[representative] + 12.0) * (1.0 + PocketWeight * share)) * (1.0 + 0.05 * alignment);
        }
        if (score < bestScore || (score == bestScore && result.found
            && (steps[representative] > result.steps || (steps[representative] == result.steps
                && representative >= (int32_t)grid.Index(result.point.x, result.point.y))))) continue;
        bestScore = score;
        result.found = true;
        result.point = p;
        result.steps = steps[representative];
        result.gain = gain;
    }
    return result;
}

std::vector<Point> PlanCoverage(const Grid& grid, Point start, int cell, uint16_t level) {
    std::vector<Point> tour;
    if (cell < 2 || !grid.Passable(start.x, start.y)) return tour;
    const int w = grid.width, h = grid.height;
    const size_t cells = size_t(w) * h;

    // 1. Flood fill (4-way; diagonal steps need both side cells open anyway)
    // over a flat walkable mask: plain array lookups, linear in the grid.
    std::vector<uint8_t> reached(cells, 0);
    for (size_t i = 0; i < cells; ++i) {
        reached[i] = (!grid.blocked[i] && (!level || grid.level[i] == level)) ? 1 : 0; // 1 = walkable
    }
    std::vector<int32_t> queue;
    queue.reserve(cells / 4);
    const int32_t origin = (int32_t)grid.Index(start.x, start.y);
    queue.push_back(origin);
    reached[origin] = 2; // 2 = reached
    for (size_t head = 0; head < queue.size(); ++head) {
        const int32_t i = queue[head];
        const int x = i % w, y = i / w;
        if (x > 0 && reached[i - 1] == 1) { reached[i - 1] = 2; queue.push_back(i - 1); }
        if (x + 1 < w && reached[i + 1] == 1) { reached[i + 1] = 2; queue.push_back(i + 1); }
        if (y > 0 && reached[i - w] == 1) { reached[i - w] = 2; queue.push_back(i - w); }
        if (y + 1 < h && reached[i + w] == 1) { reached[i + w] = 2; queue.push_back(i + w); }
    }

    // 2. Coverage blocks of `cell` subtiles, each represented by its reachable
    // cell nearest the block centre (integer squared distances).
    const int blocksX = (w + cell - 1) / cell, blocksY = (h + cell - 1) / cell;
    std::vector<int32_t> nodeOf(size_t(blocksX) * blocksY, -1);
    std::vector<Point> represent;
    std::vector<int> representDistance;
    for (int32_t i : queue) {
        const int x = i % w, y = i / w;
        const int bx = x / cell, by = y / cell;
        const size_t block = size_t(by) * blocksX + bx;
        const int dx = 2 * (x - bx * cell) + 1 - cell, dy = 2 * (y - by * cell) + 1 - cell;
        const int d = dx * dx + dy * dy;
        if (nodeOf[block] < 0) {
            nodeOf[block] = (int32_t)represent.size();
            represent.push_back({ grid.originX + x, grid.originY + y });
            representDistance.push_back(d);
        }
        else if (d < representDistance[nodeOf[block]]) {
            represent[nodeOf[block]] = { grid.originX + x, grid.originY + y };
            representDistance[nodeOf[block]] = d;
        }
    }
    const size_t nodes = represent.size();
    if (nodes == 0) return tour;

    // Blocks are linked when reachable ground crosses between them: only cells
    // on a block's right or bottom edge can cross, so only those are checked.
    std::vector<std::vector<int32_t>> links(nodes);
    auto link = [&](int32_t a, int32_t b) {
        if (std::find(links[a].begin(), links[a].end(), b) == links[a].end()) {
            links[a].push_back(b);
            links[b].push_back(a);
        }
    };
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const int32_t a = nodeOf[size_t(by) * blocksX + bx];
            if (a < 0) continue;
            const int right = std::min(w - 1, (bx + 1) * cell - 1), bottom = std::min(h - 1, (by + 1) * cell - 1);
            if (right + 1 < w) {
                const int32_t b = nodeOf[size_t(by) * blocksX + bx + 1];
                for (int y = by * cell; b >= 0 && y <= bottom; ++y) {
                    const size_t i = size_t(y) * w + right;
                    if (reached[i] == 2 && reached[i + 1] == 2) { link(a, b); break; }
                }
            }
            if (bottom + 1 < h) {
                const int32_t b = nodeOf[size_t(by + 1) * blocksX + bx];
                for (int x = bx * cell; b >= 0 && x <= right; ++x) {
                    const size_t i = size_t(bottom) * w + x;
                    if (reached[i] == 2 && reached[i + w] == 2) { link(a, b); break; }
                }
            }
        }
    }

    // 3. Order: depth-first over the block graph from the start block. At each
    // block go next to the unvisited neighbour with the fewest unvisited
    // neighbours of its own (dead ends first, so less backtracking), keeping the
    // current heading on ties. Linear in the number of blocks.
    const size_t first = (size_t)nodeOf[size_t((start.y - grid.originY) / cell) * blocksX + (start.x - grid.originX) / cell];
    std::vector<uint8_t> visited(nodes, 0);
    std::vector<size_t> order, stack = { first };
    visited[first] = 1;
    order.push_back(first);
    auto unvisitedLinks = [&](size_t node) {
        int count = 0;
        for (int32_t next : links[node]) count += visited[next] ? 0 : 1;
        return count;
    };
    while (!stack.empty()) {
        const size_t current = stack.back();
        const int headingX = stack.size() > 1 ? represent[current].x - represent[stack[stack.size() - 2]].x : 0;
        const int headingY = stack.size() > 1 ? represent[current].y - represent[stack[stack.size() - 2]].y : 0;
        size_t best = nodes;
        int bestFree = 0;
        double bestAlign = 0;
        for (int32_t next : links[current]) {
            if (visited[next]) continue;
            const int free = unvisitedLinks((size_t)next);
            const double dx = represent[next].x - represent[current].x, dy = represent[next].y - represent[current].y;
            const double align = (dx * headingX + dy * headingY) / (std::hypot(dx, dy) * std::hypot(double(headingX), double(headingY)) + 1e-9);
            if (best == nodes || free < bestFree || (free == bestFree && align > bestAlign)) {
                best = (size_t)next;
                bestFree = free;
                bestAlign = align;
            }
        }
        if (best == nodes) {
            stack.pop_back();
            continue;
        }
        visited[best] = 1;
        order.push_back(best);
        stack.push_back(best);
    }
    for (size_t node : order) tour.push_back(represent[node]);
    return tour;
}

}
