#include "core/Pathfinding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

struct SearchBuffers {
    std::vector<float> g;
    std::vector<int> parent;
    std::vector<std::uint32_t> stamp;
    std::vector<QueueItem> heap;
    std::uint32_t generation = 0;

    void begin(std::size_t size) {
        if (stamp.size() != size) {
            g.assign(size, kInf);
            parent.assign(size, -1);
            stamp.assign(size, 0);
            generation = 0;
        }
        if (++generation == 0) {
            std::fill(stamp.begin(), stamp.end(), 0u);
            generation = 1;
        }
        heap.clear();
    }
    float cost(int i) const { return stamp[static_cast<std::size_t>(i)] == generation ? g[static_cast<std::size_t>(i)] : kInf; }
    void set(int i, float c, int p) {
        stamp[static_cast<std::size_t>(i)] = generation;
        g[static_cast<std::size_t>(i)] = c;
        parent[static_cast<std::size_t>(i)] = p;
    }
    void push(float f, int i) {
        heap.emplace_back(f, i);
        std::push_heap(heap.begin(), heap.end(), std::greater<>{});
    }
    QueueItem pop() {
        std::pop_heap(heap.begin(), heap.end(), std::greater<>{});
        const QueueItem top = heap.back();
        heap.pop_back();
        return top;
    }
};

thread_local SearchBuffers g_search;
thread_local std::size_t g_work = 0;

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

std::size_t lastPathWork() {
    return g_work;
}

std::vector<glm::vec2> findPath(const Map& map, glm::vec2 from, glm::vec2 to) {
    g_work = 0;
    if (lineOfSight(map, from, to))
        return {to};

    const glm::ivec2 start = nearestFree(map, Map::cellOf(from));
    const glm::ivec2 goal = nearestFree(map, Map::cellOf(to));
    if (map.blocked(start) || map.blocked(goal))
        return {};

    auto& s = g_search;
    s.begin(static_cast<std::size_t>(map.width() * map.height()));
    s.set(map.index(start), 0.0f, -1);
    s.push(octile(start, goal), map.index(start));

    bool found = false;
    while (!s.heap.empty()) {
        const auto [f, idx] = s.pop();
        ++g_work;
        const glm::ivec2 c{idx % map.width(), idx / map.width()};
        if (c == goal) {
            found = true;
            break;
        }
        const float gc = s.cost(idx);
        if (f - octile(c, goal) > gc + 1e-4f)
            continue;
        for (const auto& step : kSteps) {
            if (!canStep(map, c, step.d))
                continue;
            const glm::ivec2 n = c + step.d;
            const int ni = map.index(n);
            const float ng = gc + step.cost;
            if (ng < s.cost(ni)) {
                s.set(ni, ng, idx);
                s.push(ng + octile(n, goal), ni);
            }
        }
    }
    if (!found)
        return {};

    std::vector<glm::vec2> cells;
    for (int i = map.index(goal); i != -1; i = s.parent[static_cast<std::size_t>(i)])
        cells.push_back(Map::center({i % map.width(), i / map.width()}));
    std::reverse(cells.begin(), cells.end());
    if (!map.blocked(Map::cellOf(to)))
        cells.back() = to;

    std::vector<glm::vec2> path;
    glm::vec2 cur = from;
    std::size_t i = 0;
    while (i < cells.size()) {
        std::size_t j = i;
        while (j + 1 < cells.size() && lineOfSight(map, cur, cells[j + 1]))
            ++j, ++g_work;
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

    constexpr int kBuckets = 15;
    std::vector<int> dist(cost_.size(), std::numeric_limits<int>::max());
    std::array<std::vector<int>, kBuckets> buckets;
    dist[static_cast<std::size_t>(map.index(goalCell_))] = 0;
    buckets[0].push_back(map.index(goalCell_));
    std::size_t pending = 1;
    for (int current = 0; pending > 0; ++current) {
        auto& bucket = buckets[static_cast<std::size_t>(current % kBuckets)];
        while (!bucket.empty()) {
            const int idx = bucket.back();
            bucket.pop_back();
            --pending;
            if (dist[static_cast<std::size_t>(idx)] != current)
                continue;
            const glm::ivec2 c{idx % map.width(), idx / map.width()};
            for (const auto& s : kSteps) {
                if (!canStep(map, c, s.d))
                    continue;
                const int ni = map.index(c + s.d);
                const int nd = current + (s.d.x != 0 && s.d.y != 0 ? 14 : 10);
                if (nd < dist[static_cast<std::size_t>(ni)]) {
                    dist[static_cast<std::size_t>(ni)] = nd;
                    buckets[static_cast<std::size_t>(nd % kBuckets)].push_back(ni);
                    ++pending;
                }
            }
        }
    }
    for (std::size_t i = 0; i < cost_.size(); ++i)
        if (dist[i] != std::numeric_limits<int>::max())
            cost_[i] = static_cast<float>(dist[i]) * 0.1f;

    next_.assign(cost_.size(), -1);
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x) {
            const int idx = map.index(x, y);
            float best = cost_[static_cast<std::size_t>(idx)];
            if (best == kInf)
                continue;
            for (const auto& s : kSteps) {
                if (!canStep(map, {x, y}, s.d))
                    continue;
                const int ni = map.index(x + s.d.x, y + s.d.y);
                if (cost_[static_cast<std::size_t>(ni)] < best) {
                    best = cost_[static_cast<std::size_t>(ni)];
                    next_[static_cast<std::size_t>(idx)] = ni;
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

    const int next = next_[static_cast<std::size_t>(map_->index(c))];
    if (next < 0)
        return {};
    return glm::normalize(Map::center({next % map_->width(), next / map_->width()}) - pos);
}

}
