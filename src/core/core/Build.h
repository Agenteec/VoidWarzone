#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>

#include "core/GameData.h"
#include "core/Map.h"

namespace vw {

inline constexpr float kEnemyBuildGap = 8.0f;
inline constexpr float kStructureGap = 1.5f;
inline constexpr float kBaseBuildRadius = 12.0f;

struct SiteBlocker {
    glm::vec2 pos{};
    float radius = 0.5f;
    bool structure = false;
    bool wall = false;
    std::uint8_t team = 0;
};

enum class SiteStatus : std::uint8_t { Ok, Blocked, Occupied, NearEnemy, FarFromBase };

inline glm::vec2 snapBuild(glm::vec2 pos) {
    return {std::floor(pos.x) + 0.5f, std::floor(pos.y) + 0.5f};
}

SiteStatus checkSite(const Map& terrain, const UnitDef& def, glm::vec2 pos, std::uint8_t team, const std::vector<SiteBlocker>& around, bool needBase = false);

}
