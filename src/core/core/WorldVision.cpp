#include <algorithm>
#include <cmath>

#include "core/World.h"

namespace vw {

void World::updateVisibility() {
    const int w = map_->width();
    const int h = map_->height();
    for (auto& [team, grid] : vision_)
        for (auto& c : grid)
            c &= 2;
    for (const auto& p : players_)
        if (p.active && !vision_.contains(p.team))
            vision_[p.team].assign(static_cast<std::size_t>(w * h), 0);
    for (auto [e, pos, unit, team] : registry_.view<const Position, const Unit, const Team>(entt::exclude<Carried>).each()) {
        const auto it = vision_.find(team.id);
        if (it == vision_.end() || !alive(e))
            continue;
        auto& grid = it->second;
        const float r = data_->unit(unit.def).vision();
        const int cx = static_cast<int>(std::floor(pos.value.x));
        const int cy = static_cast<int>(std::floor(pos.value.y));
        const int ir = static_cast<int>(std::ceil(r));
        const float r2 = r * r;
        for (int y = std::max(0, cy - ir); y <= std::min(h - 1, cy + ir); ++y) {
            const float dy = static_cast<float>(y) + 0.5f - pos.value.y;
            for (int x = std::max(0, cx - ir); x <= std::min(w - 1, cx + ir); ++x) {
                const float dx = static_cast<float>(x) + 0.5f - pos.value.x;
                if (dx * dx + dy * dy <= r2)
                    grid[static_cast<std::size_t>(y * w + x)] = 3;
            }
        }
    }
}

std::uint8_t World::visionBits(std::uint8_t team, glm::vec2 pos) const {
    if (!fog_)
        return 3;
    const auto it = vision_.find(team);
    if (it == vision_.end())
        return 3;
    const int x = static_cast<int>(std::floor(pos.x));
    const int y = static_cast<int>(std::floor(pos.y));
    if (x < 0 || y < 0 || x >= map_->width() || y >= map_->height())
        return 0;
    return it->second[static_cast<std::size_t>(y * map_->width() + x)];
}

bool World::visibleTo(std::uint8_t team, glm::vec2 pos) const {
    return (visionBits(team, pos) & 1) != 0;
}

bool World::exploredBy(std::uint8_t team, glm::vec2 pos) const {
    return (visionBits(team, pos) & 2) != 0;
}

}
