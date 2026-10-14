#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

namespace vw::client {

struct TexVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct PieModel {
    std::vector<TexVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string texture;
    std::string tcmask;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    std::vector<glm::vec3> connectors;
};

PieModel parsePie(std::string_view text);

}
