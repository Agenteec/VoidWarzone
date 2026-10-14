#include "render/Pie.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <glm/geometric.hpp>

namespace vw::client {

namespace {

bool isCountedSection(const std::string& key) {
    return key == "NORMALS" || key == "SHADOWPOINTS" || key == "SHADOWPOLYGONS" || key == "ANIMOBJECT";
}

}

PieModel parsePie(std::string_view text) {
    PieModel model;
    std::istringstream in{std::string(text)};
    std::string line;
    int version = 2;
    float texW = 256.0f;
    float texH = 256.0f;
    std::vector<glm::vec3> points;
    bool first = true;

    auto addTriangle = [&](const glm::vec3 (&p)[3], const float (&uv)[6]) {
        glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
        const float len = glm::length(n);
        n = len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
        const auto base = static_cast<std::uint32_t>(model.vertices.size());
        for (int i = 0; i < 3; ++i) {
            model.vertices.push_back({p[i].x, p[i].y, p[i].z, n.x, n.y, n.z, uv[i * 2], uv[i * 2 + 1]});
            model.indices.push_back(base + static_cast<std::uint32_t>(i));
            if (first) {
                model.min = model.max = p[i];
                first = false;
            }
            model.min = glm::min(model.min, p[i]);
            model.max = glm::max(model.max, p[i]);
        }
    };

    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key))
            continue;

        if (key == "PIE") {
            ls >> version;
        } else if (key == "TEXTURE") {
            int page = 0;
            ls >> page >> model.texture;
            float w = 0.0f;
            float h = 0.0f;
            if (ls >> w >> h && w > 0.0f && h > 0.0f) {
                texW = w;
                texH = h;
            }
        } else if (key == "TCMASK") {
            int page = 0;
            ls >> page >> model.tcmask;
        } else if (key == "POINTS") {
            int count = 0;
            ls >> count;
            points.clear();
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ps(line);
                glm::vec3 p{};
                ps >> p.x >> p.y >> p.z;
                points.push_back(p);
            }
        } else if (key == "POLYGONS") {
            int count = 0;
            ls >> count;
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ps(line);
                std::uint32_t flags = 0;
                int n = 0;
                ps >> std::hex >> flags >> std::dec >> n;
                std::vector<int> idx(static_cast<std::size_t>(std::max(0, n)));
                for (auto& v : idx)
                    ps >> v;
                if (flags & 0x4000) {
                    float skip = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        ps >> skip;
                }
                std::vector<float> uv(static_cast<std::size_t>(n) * 2, 0.0f);
                for (auto& t : uv)
                    ps >> t;
                if (version < 3)
                    for (std::size_t k = 0; k < uv.size(); k += 2) {
                        uv[k] /= texW;
                        uv[k + 1] /= texH;
                    }
                for (int k = 1; k + 1 < n; ++k) {
                    const int ids[3] = {idx[0], idx[static_cast<std::size_t>(k)], idx[static_cast<std::size_t>(k + 1)]};
                    if (ids[0] < 0 || ids[1] < 0 || ids[2] < 0 || std::max({ids[0], ids[1], ids[2]}) >= static_cast<int>(points.size()))
                        continue;
                    const glm::vec3 p[3] = {points[static_cast<std::size_t>(ids[0])], points[static_cast<std::size_t>(ids[1])],
                                            points[static_cast<std::size_t>(ids[2])]};
                    const float t[6] = {uv[0], uv[1], uv[static_cast<std::size_t>(k) * 2], uv[static_cast<std::size_t>(k) * 2 + 1],
                                        uv[static_cast<std::size_t>(k + 1) * 2], uv[static_cast<std::size_t>(k + 1) * 2 + 1]};
                    addTriangle(p, t);
                }
            }
        } else if (key == "CONNECTORS") {
            int count = 0;
            ls >> count;
            for (int i = 0; i < count && std::getline(in, line); ++i) {
                std::istringstream ps(line);
                float a = 0.0f;
                float b = 0.0f;
                float c = 0.0f;
                ps >> a >> b >> c;
                if (model.connectors.size() < 16)
                    model.connectors.emplace_back(a, c, b);
            }
        } else if (isCountedSection(key)) {
            int count = 0;
            ls >> count;
            for (int i = 0; i < count && std::getline(in, line); ++i) {
            }
        }
    }

    if (model.vertices.empty())
        throw std::runtime_error("pie has no geometry");
    return model;
}

}
