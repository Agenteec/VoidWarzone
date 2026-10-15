#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

#include "render/Pie.h"

namespace vw::client {

struct GltfPart {
    std::vector<TexVertex> vertices;
    std::vector<std::uint32_t> indices;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct GltfModel {
    std::map<std::string, GltfPart> parts;
    std::map<std::string, glm::vec3> points;
    std::string color;
    std::string mask;
    std::string colorUri;
    std::string maskUri;
};

GltfModel parseGlb(std::string_view bytes);

}
