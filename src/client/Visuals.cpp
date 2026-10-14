#include "Visuals.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include <bx/math.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "render/Pie.h"

namespace vw::client {

namespace {

constexpr const char* kModelDir = "data/wz2100/models/";
constexpr const char* kTextureDir = "data/wz2100/textures/";
constexpr float kDroidYaw = -std::numbers::pi_v<float> * 0.5f;

std::string defaultMask(const std::string& page) {
    if (page.rfind("page-", 0) != 0)
        return {};
    const auto dash = page.find('-', 5);
    return dash == std::string::npos ? std::string{} : page.substr(0, dash) + "_tcmask.png";
}

Mesh parseShape(const std::string& s) {
    if (s == "cylinder")
        return Mesh::Cylinder;
    if (s == "sphere")
        return Mesh::Sphere;
    return Mesh::Cube;
}

void addQuad(std::vector<TexVertex>& v, std::vector<std::uint32_t>& idx, const glm::vec3 (&p)[4], const glm::vec2 (&uv)[4], glm::vec3 n) {
    const auto base = static_cast<std::uint32_t>(v.size());
    for (int i = 0; i < 4; ++i)
        v.push_back({p[i].x, p[i].y, p[i].z, n.x, n.y, n.z, uv[i].x, uv[i].y});
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
        idx.push_back(base + i);
}

}

void makeModelMatrix(float* mtx, glm::vec3 pos, float yaw, float scale, glm::vec3 offset) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const glm::vec3 o = offset * scale;
    const glm::vec3 t{pos.x + o.x * c - o.z * s, pos.y + o.y, pos.z + o.x * s + o.z * c};
    bx::mtxSRT(mtx, scale, scale, scale, 0.0f, yaw, 0.0f, t.x, t.y, t.z);
}

TextureId ModelLibrary::texture(const std::string& file) {
    if (const auto it = textures_.find(file); it != textures_.end())
        return it->second;
    TextureId id = renderer_.whiteTexture();
    try {
        const std::string bytes = load_(kTextureDir + file);
        id = renderer_.createTexture(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    } catch (const std::exception& e) {
        spdlog::warn("texture {}: {}", file, e.what());
    }
    textures_[file] = id;
    return id;
}

const Model* ModelLibrary::model(const std::string& name) {
    if (const auto it = models_.find(name); it != models_.end())
        return it->second.get();
    std::unique_ptr<Model> result;
    try {
        const PieModel pie = parsePie(load_(kModelDir + name + ".pie"));
        result = std::make_unique<Model>();
        result->mesh = renderer_.createMesh(pie.vertices, pie.indices);
        result->texture = texture(pie.texture);
        std::string mask = pie.tcmask.empty() ? defaultMask(pie.texture) : pie.tcmask;
        result->mask = renderer_.blackTexture();
        if (!mask.empty()) {
            try {
                load_(kTextureDir + mask);
                result->mask = texture(mask);
            } catch (const std::exception&) {
            }
        }
        result->min = pie.min;
        result->max = pie.max;
        result->connectors = pie.connectors;
    } catch (const std::exception& e) {
        spdlog::warn("model {}: {}", name, e.what());
    }
    const Model* ptr = result.get();
    models_[name] = std::move(result);
    return ptr;
}

std::vector<UnitVisual> loadUnitVisuals(const std::string& unitsJson, const GameData& data, ModelLibrary& models) {
    std::vector<UnitVisual> visuals(data.units().size());
    const auto units = nlohmann::json::parse(unitsJson);
    for (const auto& u : units.at("units")) {
        const auto it = u.find("visual");
        if (it == u.end())
            continue;
        const UnitDefId id = data.find(u.at("id").get<std::string>());
        auto& v = visuals[id];
        const auto& def = data.unit(id);
        v.shape = parseShape(it->value("shape", "cube"));
        v.height = it->value("height", v.height);

        const auto m = it->find("model");
        if (m == it->end())
            continue;
        glm::vec3 lo{1e9f};
        glm::vec3 hi{-1e9f};
        auto add = [&](const std::string& name, glm::vec3 offset, bool bounds) -> const Model* {
            const Model* model = models.model(name);
            if (!model)
                return nullptr;
            v.parts.push_back({model, offset});
            if (bounds) {
                lo = glm::min(lo, model->min + offset);
                hi = glm::max(hi, model->max + offset);
            }
            return model;
        };
        if (const auto pie = m->find("pie"); pie != m->end()) {
            add(pie->get<std::string>(), {}, true);
        } else {
            add(m->value("prop", ""), {}, true);
            const Model* body = add(m->value("body", ""), {}, true);
            const glm::vec3 mount = body && !body->connectors.empty() ? body->connectors[0] : glm::vec3(0.0f, hi.y, 0.0f);
            if (const Model* weapon = models.model(m->value("weapon", ""))) {
                v.parts.push_back({weapon, mount});
                hi.y = std::max(hi.y, mount.y + weapon->max.y);
            }
            v.yaw = kDroidYaw;
        }
        if (v.parts.empty())
            continue;
        const float extent = std::max({hi.x - lo.x, hi.z - lo.z, 1.0f});
        const float fit = m->value("fit", def.structure ? 1.0f : 1.3f);
        v.scale = def.radius * 2.0f * fit / extent;
        v.height = std::max(0.3f, hi.y * v.scale);
        v.yaw += m->value("yaw", 0.0f) * std::numbers::pi_v<float> / 180.0f;
    }
    return visuals;
}

MapVisuals buildMapVisuals(const Map& map, Renderer& renderer, ModelLibrary& models) {
    MapVisuals out;
    const auto w = static_cast<float>(map.width());
    const auto h = static_cast<float>(map.height());
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    {
        std::vector<TexVertex> v;
        std::vector<std::uint32_t> idx;
        constexpr float tile = 8.0f;
        addQuad(v, idx, {{0, 0, 0}, {w, 0, 0}, {w, 0, h}, {0, 0, h}}, {{0, 0}, {w / tile, 0}, {w / tile, h / tile}, {0, h / tile}}, up);
        out.ground = renderer.createMesh(v, idx);
        out.groundTexture = models.texture("page-82-yellow-sand-arizona.png");
    }

    {
        std::vector<TexVertex> v;
        std::vector<std::uint32_t> idx;
        constexpr float half = 4.0f;
        constexpr float tile = 4.0f;
        for (const auto& lane : map.lanes())
            for (std::size_t i = 0; i + 1 < lane.points.size(); ++i) {
                const glm::vec2 a = lane.points[i];
                const glm::vec2 b = lane.points[i + 1];
                const glm::vec2 dir = glm::normalize(b - a);
                const glm::vec2 side{-dir.y * half, dir.x * half};
                const glm::vec2 a0 = a - dir * half;
                const glm::vec2 b0 = b + dir * half;
                const glm::vec2 c[4] = {a0 - side, b0 - side, b0 + side, a0 + side};
                const float y = 0.02f;
                addQuad(v, idx, {{c[0].x, y, c[0].y}, {c[1].x, y, c[1].y}, {c[2].x, y, c[2].y}, {c[3].x, y, c[3].y}},
                        {{c[0].x / tile, c[0].y / tile}, {c[1].x / tile, c[1].y / tile}, {c[2].x / tile, c[2].y / tile}, {c[3].x / tile, c[3].y / tile}},
                        up);
            }
        out.lanes = renderer.createMesh(v, idx);
        out.laneTexture = models.texture("page-84-tiles-arizona.png");
    }

    {
        std::vector<TexVertex> v;
        std::vector<std::uint32_t> idx;
        constexpr float tile = 4.0f;
        constexpr float height = 2.5f;
        for (const auto& r : map.walls()) {
            const float x0 = static_cast<float>(r.x);
            const float z0 = static_cast<float>(r.y);
            const float x1 = x0 + static_cast<float>(r.w);
            const float z1 = z0 + static_cast<float>(r.h);
            addQuad(v, idx, {{x0, height, z0}, {x1, height, z0}, {x1, height, z1}, {x0, height, z1}},
                    {{x0 / tile, z0 / tile}, {x1 / tile, z0 / tile}, {x1 / tile, z1 / tile}, {x0 / tile, z1 / tile}}, up);
            const float ty = height / tile;
            addQuad(v, idx, {{x0, 0, z0}, {x1, 0, z0}, {x1, height, z0}, {x0, height, z0}}, {{x0 / tile, ty}, {x1 / tile, ty}, {x1 / tile, 0}, {x0 / tile, 0}},
                    {0, 0, -1});
            addQuad(v, idx, {{x1, 0, z1}, {x0, 0, z1}, {x0, height, z1}, {x1, height, z1}}, {{x1 / tile, ty}, {x0 / tile, ty}, {x0 / tile, 0}, {x1 / tile, 0}},
                    {0, 0, 1});
            addQuad(v, idx, {{x0, 0, z1}, {x0, 0, z0}, {x0, height, z0}, {x0, height, z1}}, {{z1 / tile, ty}, {z0 / tile, ty}, {z0 / tile, 0}, {z1 / tile, 0}},
                    {-1, 0, 0});
            addQuad(v, idx, {{x1, 0, z0}, {x1, 0, z1}, {x1, height, z1}, {x1, height, z0}}, {{z0 / tile, ty}, {z1 / tile, ty}, {z1 / tile, 0}, {z0 / tile, 0}},
                    {1, 0, 0});
        }
        out.walls = renderer.createMesh(v, idx);
        out.wallTexture = models.texture("page-85-cliff-arizona.png");
    }

    out.derrick = models.model("blderik");

    std::mt19937 rng(static_cast<std::uint32_t>(map.hash()));
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const Model* boulders[] = {models.model("mibould1"), models.model("mibould2")};
    for (const auto& r : map.walls()) {
        if (r.w < 3 || r.h < 3)
            continue;
        const int count = r.w * r.h / 90;
        for (int i = 0; i < count; ++i) {
            const Model* model = boulders[i % 2];
            if (!model)
                continue;
            const glm::vec3 pos{static_cast<float>(r.x) + 1.0f + unit(rng) * static_cast<float>(r.w - 2), 2.4f,
                                static_cast<float>(r.y) + 1.0f + unit(rng) * static_cast<float>(r.h - 2)};
            const float extent = std::max({model->max.x - model->min.x, model->max.z - model->min.z, 1.0f});
            Placement p;
            p.model = model;
            makeModelMatrix(p.mtx, pos, unit(rng) * 6.283f, (0.8f + unit(rng) * 1.4f) / extent, {});
            out.props.push_back(p);
        }
    }
    return out;
}

}
