#include "core/Pathfinding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

#include <glm/geometric.hpp>

namespace vw {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kDiag = 1.41421356f;

struct Step {
    glm::ivec2 d;
    float cost;
};

constexpr std::array<Step, 8> kSteps{{
    {{1, 0}, 1.0f},
    {{-1, 0}, 1.0f},
    {{0, 1}, 1.0f},
    {{0, -1}, 1.0f},
    {{1, 1}, kDiag},
    {{1, -1}, kDiag},
    {{-1, 1}, kDiag},
    {{-1, -1}, kDiag},
}};

bool canStep(const Map& map, glm::ivec2 c, glm::ivec2 d) {
    const glm::ivec2 n = c + d;
    if (map.blocked(n))
        return false;
    if (d.x != 0 && d.y != 0)
        return !map.blocked(c.x + d.x, c.y) && !map.blocked(c.x, c.y + d.y);
    return true;
}

float octile(glm::ivec2 a, glm::ivec2 b) {
    const float dx = static_cast<float>(std::abs(a.x - b.x));
    const float dy = static_cast<float>(std::abs(a.y - b.y));
    return std::max(dx, dy) + (kDiag - 1.0f) * std::min(dx, dy);
}

using QueueItem = std::pair<float, int>;
using MinQueue = std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<>>;

}

bool lineOfSight(const Map& map, glm::vec2 a, glm::vec2 b) {
    glm::ivec2 c = Map::cellOf(a);
    const glm::ivec2 end = Map::cellOf(b);
    if (map.blocked(c))
        return false;

    const glm::vec2 d = b - a;
    const int sx = d.x > 0 ? 1 : -1;
    const int sy = d.y > 0 ? 1 : -1;
    const float dtx = d.x != 0.0f ? std::abs(1.0f / d.x) : kInf;
    const float dty = d.y != 0.0f ? std::abs(1.0f / d.y) : kInf;
    float tx = d.x != 0.0f ? (sx > 0 ? (static_cast<float>(c.x) + 1.0f - a.x) : (a.x - static_cast<float>(c.x))) * dtx : kInf;
    float ty = d.y != 0.0f ? (sy > 0 ? (static_cast<float>(c.y) + 1.0f - a.y) : (a.y - static_cast<float>(c.y))) * dty : kInf;

    while (c != end && (tx <= 1.0f || ty <= 1.0f)) {
        if (std::abs(tx - ty) < 1e-6f) {
            if (map.blocked(c.x + sx, c.y) || map.blocked(c.x, c.y + sy))
                return false;
            c += glm::ivec2(sx, sy);
            tx += dtx;
            ty += dty;
        } else if (tx < ty) {
            c.x += sx;
            tx += dtx;
        } else {
            c.y += sy;
            ty += dty;
        }
        if (map.blocked(c))
            return false;
    }
    return true;
}

glm::ivec2 nearestFree(const Map& map, glm::ivec2 cell, int maxRadius) {
    if (!map.blocked(cell))
        return cell;
    for (int r = 1; r <= maxRadius; ++r) {
        glm::ivec2 best = cell;
        int bestDist = std::numeric_limits<int>::max();
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (std::abs(x) != r && std::abs(y) != r)
                    continue;
                const glm::ivec2 c = cell + glm::ivec2(x, y);
                const int dist = x * x + y * y;
                if (!map.blocked(c) && dist < bestDist) {
                    best = c;
                    bestDist = dist;
                }
            }
        if (bestDist != std::numeric_limits<int>::max())
            return best;
    }
    return cell;
}

std::vector<glm::vec2> findPath(const Map& map, glm::vec2 from, glm::vec2 to) {
    if (lineOfSight(map, from, to))
        return {to};

    const glm::ivec2 start = nearestFree(map, Map::cellOf(from));
    const glm::ivec2 goal = nearestFree(map, Map::cellOf(to));
    if (map.blocked(start) || map.blocked(goal))
        return {};

    const auto size = static_cast<std::size_t>(map.width() * map.height());
    std::vector<float> g(size, kInf);
    std::vector<int> parent(size, -1);
    MinQueue open;

    g[map.index(start)] = 0.0f;
    open.emplace(octile(start, goal), map.index(start));

    bool found = false;
    while (!open.empty()) {
        const auto [f, idx] = open.top();
        open.pop();
        const glm::ivec2 c{idx % map.width(), idx / map.width()};
        if (c == goal) {
            found = true;
            break;
        }
        if (f - octile(c, goal) > g[idx] + 1e-4f)
            continue;
        for (const auto& s : kSteps) {
            if (!canStep(map, c, s.d))
                continue;
            const glm::ivec2 n = c + s.d;
            const int ni = map.index(n);
            const float ng = g[idx] + s.cost;
            if (ng < g[ni]) {
                g[ni] = ng;
                parent[ni] = idx;
                open.emplace(ng + octile(n, goal), ni);
            }
        }
    }
    if (!found)
        return {};

    std::vector<glm::vec2> cells;
    for (int i = map.index(goal); i != -1; i = parent[i])
        cells.push_back(Map::center({i % map.width(), i / map.width()}));
    std::reverse(cells.begin(), cells.end());
    if (!map.blocked(Map::cellOf(to)))
        cells.back() = to;

    std::vector<glm::vec2> path;
    glm::vec2 cur = from;
    std::size_t i = 0;
    while (i < cells.size()) {
        std::size_t j = cells.size() - 1;
        while (j > i && !lineOfSight(map, cur, cells[j]))
            --j;
        path.push_back(cells[j]);
        cur = cells[j];
        i = j + 1;
    }
    return path;
}

FlowField::FlowField(const Map& map, glm::vec2 goal) : map_(&map), goal_(goal), goalCell_(nearestFree(map, Map::cellOf(goal))) {
    cost_.assign(static_cast<std::size_t>(map.width() * map.height()), kInf);
    if (map.blocked(goalCell_))
        return;

    MinQueue open;
    cost_[map.index(goalCell_)] = 0.0f;
    open.emplace(0.0f, map.index(goalCell_));
    while (!open.empty()) {
        const auto [d, idx] = open.top();
        open.pop();
        if (d > cost_[idx])
            continue;
        const glm::ivec2 c{idx % map.width(), idx / map.width()};
        for (const auto& s : kSteps) {
            if (!canStep(map, c, s.d))
                continue;
            const int ni = map.index(c + s.d);
            const float nd = d + s.cost;
            if (nd < cost_[ni]) {
                cost_[ni] = nd;
                open.emplace(nd, ni);
            }
        }
    }
}

float FlowField::cost(glm::ivec2 cell) const {
    return map_->blocked(cell) ? kInf : cost_[map_->index(cell)];
}

glm::vec2 FlowField::direction(glm::vec2 pos) const {
    const glm::ivec2 c = Map::cellOf(pos);
    const float here = cost(c);
    if (c == goalCell_ || here == kInf) {
        const glm::vec2 d = goal_ - pos;
        const float len = glm::length(d);
        return len > 1e-4f ? d / len : glm::vec2{};
    }

    glm::ivec2 best = c;
    float bestCost = here;
    for (const auto& s : kSteps) {
        if (!canStep(*map_, c, s.d))
            continue;
        const float nc = cost(c + s.d);
        if (nc < bestCost) {
            bestCost = nc;
            best = c + s.d;
        }
    }
    if (best == c)
        return {};
    return glm::normalize(Map::center(best) - pos);
}

}
