#pragma once

#include <vector>

#include <glm/vec2.hpp>

#include "core/Map.h"

namespace vw {

bool lineOfSight(const Map& map, glm::vec2 a, glm::vec2 b);
glm::ivec2 nearestFree(const Map& map, glm::ivec2 cell, int maxRadius = 8);
std::vector<glm::vec2> findPath(const Map& map, glm::vec2 from, glm::vec2 to);
std::size_t lastPathWork();

class FlowField {
public:
    FlowField(const Map& map, glm::vec2 goal);

    glm::vec2 direction(glm::vec2 pos) const;
    float cost(glm::ivec2 cell) const;
    glm::vec2 goal() const { return goal_; }

private:
    const Map* map_;
    glm::vec2 goal_;
    glm::ivec2 goalCell_;
    std::vector<float> cost_;
    std::vector<int> next_;
};

}
