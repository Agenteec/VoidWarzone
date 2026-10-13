#pragma once

#include <vector>

#include <glm/vec2.hpp>

#include "core/Map.h"

namespace vw {

glm::vec2 seek(glm::vec2 pos, glm::vec2 goal, float speed, float dt);
void pushOutOfWalls(const Map& map, glm::vec2& p, float radius);
std::vector<glm::vec2> formationSlots(const Map* map, glm::vec2 center, std::size_t count, float spacing);
std::vector<glm::vec2> assignFormation(const Map* map, const std::vector<glm::vec2>& positions, float spacing, glm::vec2 point);

}
