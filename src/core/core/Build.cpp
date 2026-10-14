#include "core/Build.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace vw {

SiteStatus checkSite(const Map& terrain, const UnitDef& def, glm::vec2 pos, std::uint8_t team, const std::vector<SiteBlocker>& around) {
    const float r = std::max(def.radius - 0.25f, 0.5f);
    const glm::ivec2 c = Map::cellOf(pos);
    const int span = static_cast<int>(std::ceil(r)) + 1;
    for (int y = c.y - span; y <= c.y + span; ++y)
        for (int x = c.x - span; x <= c.x + span; ++x)
            if ((glm::ivec2(x, y) == c || glm::distance(Map::center({x, y}), pos) < r) && terrain.blocked(x, y))
                return SiteStatus::Blocked;
    SiteStatus status = SiteStatus::Ok;
    for (const auto& b : around) {
        const float d = glm::distance(b.pos, pos);
        if (b.structure) {
            if (b.team != team && d < def.radius + b.radius + kEnemyBuildGap)
                return SiteStatus::NearEnemy;
            const float gap = def.wall && b.wall ? -0.1f : kStructureGap;
            if (d < def.radius + b.radius + gap)
                return SiteStatus::Blocked;
        } else if (d < def.radius + b.radius - 0.1f) {
            status = SiteStatus::Occupied;
        }
    }
    return status;
}

}
