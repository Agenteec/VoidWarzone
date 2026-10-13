#include "core/Movement.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace vw {

glm::vec2 seek(glm::vec2 pos, glm::vec2 goal, float speed, float dt) {
    const glm::vec2 d = goal - pos;
    const float len = glm::length(d);
    if (len < 1e-4f)
        return {};
    return d / len * std::min(speed, len / dt);
}

void pushOutOfWalls(const Map& map, glm::vec2& p, float r) {
    const int x0 = static_cast<int>(std::floor(p.x - r));
    const int x1 = static_cast<int>(std::floor(p.x + r));
    const int y0 = static_cast<int>(std::floor(p.y - r));
    const int y1 = static_cast<int>(std::floor(p.y + r));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!map.blocked(x, y))
                continue;
            const glm::vec2 lo(static_cast<float>(x), static_cast<float>(y));
            const glm::vec2 closest = glm::clamp(p, lo, lo + 1.0f);
            const glm::vec2 d = p - closest;
            const float d2 = glm::dot(d, d);
            if (d2 >= r * r)
                continue;
            if (d2 > 1e-8f) {
                const float dist = std::sqrt(d2);
                p += d / dist * (r - dist);
                continue;
            }
            const float left = p.x - lo.x;
            const float right = lo.x + 1.0f - p.x;
            const float down = p.y - lo.y;
            const float up = lo.y + 1.0f - p.y;
            const float m = std::min({left, right, down, up});
            if (m == left)
                p.x = lo.x - r;
            else if (m == right)
                p.x = lo.x + 1.0f + r;
            else if (m == down)
                p.y = lo.y - r;
            else
                p.y = lo.y + 1.0f + r;
        }
}

std::vector<glm::vec2> formationSlots(const Map* map, glm::vec2 center, std::size_t count, float spacing) {
    std::vector<glm::vec2> slots;
    for (int ring = 0; slots.size() < count && ring < 64; ++ring) {
        std::vector<glm::vec2> ringSlots;
        for (int y = -ring; y <= ring; ++y)
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != ring)
                    continue;
                const glm::vec2 p = center + glm::vec2(static_cast<float>(x), static_cast<float>(y)) * spacing;
                if (!map || !map->blocked(Map::cellOf(p)))
                    ringSlots.push_back(p);
            }
        std::stable_sort(ringSlots.begin(), ringSlots.end(),
                         [&](glm::vec2 a, glm::vec2 b) { return glm::distance(a, center) < glm::distance(b, center); });
        for (const auto& p : ringSlots)
            if (slots.size() < count)
                slots.push_back(p);
    }
    while (slots.size() < count)
        slots.push_back(center);
    return slots;
}

std::vector<glm::vec2> assignFormation(const Map* map, const std::vector<glm::vec2>& positions, float spacing, glm::vec2 point) {
    std::vector<std::size_t> order(positions.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return glm::distance(positions[a], point) < glm::distance(positions[b], point); });
    const auto slots = formationSlots(map, point, positions.size(), spacing);
    std::vector<glm::vec2> goals(positions.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        goals[order[i]] = slots[i];
    return goals;
}

}
