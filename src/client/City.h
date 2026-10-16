#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>

#include "core/Map.h"
#include "render/Pie.h"

namespace vw::client {

enum class Facade : std::uint8_t { Panel, Brick, Industrial, House, Roof, Slate, Count };

struct MeshData {
    std::vector<TexVertex> vertices;
    std::vector<std::uint32_t> indices;
};

inline constexpr float kStoreyHeight = 0.34f;

const char* facadeName(Facade f);
std::vector<std::uint32_t> triangulate(const std::vector<glm::vec2>& poly);
float buildingHeight(const BuildingDef& b);
void extrudeBuilding(const BuildingDef& b, std::uint32_t seed, std::array<MeshData, static_cast<std::size_t>(Facade::Count)>& out);

}
