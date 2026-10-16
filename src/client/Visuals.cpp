#include "Visuals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <numbers>
#include <random>

#include <bx/math.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "City.h"
#include "render/Gltf.h"
#include "render/Pie.h"

namespace vw::client {

namespace {

constexpr const char* kModelDir = "data/wz2100/models/";
constexpr const char* kTextureDir = "data/wz2100/textures/";
constexpr const char* kGlbDir = "data/models/";

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

}

void makeModelMatrix(float* mtx, glm::vec3 pos, float yaw, float scale, glm::vec3 offset) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    const glm::vec3 o = offset * scale;
    const glm::vec3 t{pos.x + o.x * c - o.z * s, pos.y + o.y, pos.z + o.x * s + o.z * c};
    bx::mtxSRT(mtx, scale, scale, scale, 0.0f, yaw, 0.0f, t.x, t.y, t.z);
}

static glm::vec3 rotateY(glm::vec3 v, float yaw) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

glm::vec3 modelPoint(glm::vec3 pos, float yaw, float scale, glm::vec3 p) {
    return pos + rotateY(p * scale, yaw);
}

void makeLegMatrix(float* mtx, glm::vec3 pos, float hullYaw, float scale, glm::vec3 hip, float swing, float lift) {
    float local[16];
    bx::mtxRotateXYZ(local, 0.0f, swing, lift);
    local[12] = hip.x;
    local[13] = hip.y;
    local[14] = hip.z;
    float model[16];
    makeModelMatrix(model, pos, hullYaw, scale, {});
    bx::mtxMul(mtx, local, model);
}

void makeTurretMatrix(float* mtx, glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale) {
    const glm::vec3 t = pos + rotateY(pivot * scale, hullYaw);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float m[16] = {cy, 0.0f, sy, 0.0f, -sp * sy, cp, sp * cy, 0.0f, -cp * sy, -sp, cp * cy, 0.0f, t.x, t.y, t.z, 1.0f};
    for (int i = 0; i < 16; ++i)
        mtx[i] = i < 12 ? m[i] * scale : m[i];
}

glm::vec3 turretPoint(glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale, glm::vec3 local) {
    float m[16];
    makeTurretMatrix(m, pos, hullYaw, pivot, yaw, pitch, scale);
    return {local.x * m[0] + local.y * m[4] + local.z * m[8] + m[12], local.x * m[1] + local.y * m[5] + local.z * m[9] + m[13],
            local.x * m[2] + local.y * m[6] + local.z * m[10] + m[14]};
}

TextureId ModelLibrary::texture(const std::string& file) {
    if (const auto it = textures_.find(file); it != textures_.end())
        return it->second;
    TextureId id = renderer_.whiteTexture();
    try {
        const std::string bytes = load_(file.rfind("data/", 0) == 0 ? file : kTextureDir + file);
        id = renderer_.createTexture(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    } catch (const std::exception& e) {
        spdlog::warn("texture {}: {}", file, e.what());
    }
    textures_[file] = id;
    return id;
}

void ModelLibrary::loadGlb(const std::string& file) {
    GltfModel glb;
    try {
        glb = parseGlb(load_(kGlbDir + file + ".glb"));
    } catch (const std::exception& e) {
        spdlog::warn("model {}: {}", file, e.what());
    }
    auto image = [&](const std::string& bytes, const std::string& uri, TextureId fallback) {
        if (!bytes.empty())
            return renderer_.createTexture(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        if (uri.empty())
            return fallback;
        const std::string key = kGlbDir + uri;
        if (const auto it = textures_.find(key); it != textures_.end())
            return it->second;
        TextureId id = fallback;
        try {
            const std::string data = load_(key);
            id = renderer_.createTexture(data.data(), static_cast<std::uint32_t>(data.size()));
        } catch (const std::exception& e) {
            spdlog::warn("texture {}: {}", key, e.what());
        }
        textures_[key] = id;
        return id;
    };
    const TextureId color = image(glb.color, glb.colorUri, renderer_.whiteTexture());
    const TextureId mask = image(glb.mask, glb.maskUri, renderer_.blackTexture());
    for (auto& [part, data] : glb.parts) {
        auto m = std::make_unique<Model>();
        m->mesh = renderer_.createMesh(data.vertices, data.indices);
        m->texture = color;
        m->mask = mask;
        m->glow = true;
        m->min = data.min;
        m->max = data.max;
        std::string pointName = part == "hull" ? "pivot" : part == "barrel" ? "muzzle" : "";
        if (part.rfind("leg", 0) == 0)
            pointName = "hip" + part.substr(3);
        const auto point = glb.points.find(pointName);
        if (point != glb.points.end())
            m->connectors.push_back(point->second);
        if (part == "hull") {
            for (int t = 0;; ++t) {
                const auto tip = glb.points.find("tip" + std::to_string(t));
                if (tip == glb.points.end())
                    break;
                m->tips.push_back(tip->second);
            }
            const auto gate = glb.points.find("gate");
            const auto edge = glb.points.find("gate_edge");
            if (gate != glb.points.end() && edge != glb.points.end())
                m->gate = {gate->second, glm::distance(gate->second, edge->second)};
        }
        m->vertices = std::move(data.vertices);
        m->indices = std::move(data.indices);
        models_[file + ":" + part] = std::move(m);
    }
    models_.try_emplace(file + ":", nullptr);
}

const Model* ModelLibrary::model(const std::string& name) {
    if (name.empty())
        return nullptr;
    if (const auto it = models_.find(name); it != models_.end())
        return it->second.get();
    if (const auto colon = name.find(':'); colon != std::string::npos) {
        const std::string file = name.substr(0, colon);
        if (!models_.contains(file + ":"))
            loadGlb(file);
        const auto it = models_.find(name);
        return it != models_.end() ? it->second.get() : nullptr;
    }
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
        result->vertices = pie.vertices;
        result->indices = pie.indices;
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
        v.crystalShot = it->value("shot", "") == "crystal";
        v.frostShot = it->value("shot", "") == "frost";
        if (const auto t = it->find("tint"); t != it->end() && t->is_array() && t->size() == 3)
            v.tint = glm::vec3((*t)[0].get<float>(), (*t)[1].get<float>(), (*t)[2].get<float>());
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
        const Model* hull = nullptr;
        if (const auto glb = m->find("glb"); glb != m->end()) {
            const std::string file = glb->get<std::string>();
            hull = add(file + ":hull", {}, true);
            if (hull) {
                v.tips = hull->tips;
                v.gate = hull->gate;
            }
            for (int k = 0;; ++k) {
                const Model* leg = models.model(file + ":leg" + std::to_string(k));
                if (!leg)
                    break;
                const glm::vec3 hip = leg->connectors.empty() ? glm::vec3(0.0f) : leg->connectors[0];
                v.legs.push_back(leg);
                v.hips.push_back(hip);
                lo = glm::min(lo, leg->min + hip);
                hi = glm::max(hi, leg->max + hip);
            }
            v.yaw = kModelYaw;
            v.pivot = hull && !hull->connectors.empty() ? hull->connectors[0] : glm::vec3(0.0f, hi.y, 0.0f);
            const std::string turretFile = m->value("turret", file);
            if (const Model* turret = models.model(turretFile + ":turret")) {
                v.parts.push_back({turret, {}, true, false});
                hi.y = std::max(hi.y, v.pivot.y + turret->max.y);
                v.turret = true;
            }
            if (const Model* barrel = models.model(turretFile + ":barrel")) {
                v.parts.push_back({barrel, {}, true, true});
                hi.y = std::max(hi.y, v.pivot.y + barrel->max.y);
                v.muzzle = barrel->connectors.empty() ? glm::vec3(0.0f, (barrel->min.y + barrel->max.y) * 0.5f, barrel->min.z) : barrel->connectors[0];
                v.turret = true;
            }
        } else if (const auto pie = m->find("pie"); pie != m->end()) {
            hull = add(pie->get<std::string>(), {}, true);
        } else {
            add(m->value("prop", ""), {}, true);
            hull = add(m->value("body", ""), {}, true);
            v.yaw = kModelYaw;
        }
        v.pivot = hull && !hull->connectors.empty() ? hull->connectors[0] : glm::vec3(0.0f, hi.y, 0.0f);
        if (const Model* mount = models.model(m->value("mount", ""))) {
            v.parts.push_back({mount, {}, true, false});
            hi.y = std::max(hi.y, v.pivot.y + mount->max.y);
            v.turret = true;
        }
        if (const Model* weapon = models.model(m->value("weapon", ""))) {
            v.parts.push_back({weapon, {}, true, true});
            hi.y = std::max(hi.y, v.pivot.y + weapon->max.y);
            v.muzzle = weapon->connectors.empty() ? glm::vec3(0.0f, (weapon->min.y + weapon->max.y) * 0.5f, weapon->min.z) : weapon->connectors[0];
            v.turret = true;
        }
        if (v.parts.empty())
            continue;
        const float extent = std::max({hi.x - lo.x, hi.z - lo.z, 1.0f});
        const float fit = m->value("fit", def.structure ? 1.0f : 1.3f);
        v.scale = def.radius * 2.0f * fit / extent;
        v.height = std::max(0.3f, hi.y * v.scale);
        v.lo = lo;
        v.hi = hi;
        v.yaw += m->value("yaw", 0.0f) * std::numbers::pi_v<float> / 180.0f;
    }
    return visuals;
}

namespace {

std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x8da6b343u) ^ (static_cast<std::uint32_t>(y) * 0xd8163841u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float valueNoise(float x, float y, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    auto v = [&](int dx, int dy) { return static_cast<float>(hash2(ix + dx, iy + dy, seed) & 0xffff) / 65535.0f; };
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = v(0, 0) + (v(1, 0) - v(0, 0)) * sx;
    const float b = v(0, 1) + (v(1, 1) - v(0, 1)) * sx;
    return a + (b - a) * sy;
}

float fbm(float x, float y, std::uint32_t seed) {
    return valueNoise(x, y, seed) * 0.6f + valueNoise(x * 2.3f, y * 2.3f, seed + 17u) * 0.3f + valueNoise(x * 5.1f, y * 5.1f, seed + 31u) * 0.1f;
}

float segmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    return glm::distance(p, a + ab * t);
}

float smooth(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::uint8_t toByte(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

}

float MapVisuals::heightAt(float x, float z) const {
    if (columns < 2 || rows < 2)
        return 0.0f;
    const float cx = std::clamp(x, 0.0f, static_cast<float>(columns - 1));
    const float cz = std::clamp(z, 0.0f, static_cast<float>(rows - 1));
    const int ix = std::min(static_cast<int>(cx), columns - 2);
    const int iz = std::min(static_cast<int>(cz), rows - 2);
    const float fx = cx - static_cast<float>(ix);
    const float fz = cz - static_cast<float>(iz);
    auto h = [&](int i, int j) { return heights[static_cast<std::size_t>(j * columns + i)]; };
    const float a = h(ix, iz) + (h(ix + 1, iz) - h(ix, iz)) * fx;
    const float b = h(ix, iz + 1) + (h(ix + 1, iz + 1) - h(ix, iz + 1)) * fx;
    return a + (b - a) * fz;
}

MapVisuals buildMapVisuals(const Map& map, const GameData& data, Renderer& renderer, ModelLibrary& models) {
    constexpr int kChunk = 32;
    MapVisuals out;
    const int w = map.width();
    const int h = map.height();
    const auto seed = static_cast<std::uint32_t>(map.hash());

    std::vector<int> dist(static_cast<std::size_t>(w * h), -1);
    std::vector<int> queue;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (map.cellKind(x, y) != CellKind::Rock) {
                dist[static_cast<std::size_t>(map.index(x, y))] = 0;
                queue.push_back(map.index(x, y));
            }
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const int c = queue[q];
        const int cx = c % w;
        const int cy = c / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                if (!map.inside(nx, ny) || dist[static_cast<std::size_t>(map.index(nx, ny))] >= 0)
                    continue;
                dist[static_cast<std::size_t>(map.index(nx, ny))] = dist[static_cast<std::size_t>(c)] + 1;
                queue.push_back(map.index(nx, ny));
            }
    }
    auto cellDist = [&](int x, int y) {
        if (!map.inside(x, y))
            return 99;
        const int d = dist[static_cast<std::size_t>(map.index(x, y))];
        return d < 0 ? 99 : d;
    };

    out.columns = w + 1;
    out.rows = h + 1;
    out.heights.assign(static_cast<std::size_t>(out.columns * out.rows), 0.0f);
    std::vector<int> level(out.heights.size(), 0);
    for (int j = 0; j < out.rows; ++j)
        for (int i = 0; i < out.columns; ++i) {
            const int l = std::min({cellDist(i - 1, j - 1), cellDist(i, j - 1), cellDist(i - 1, j), cellDist(i, j)});
            const auto idx = static_cast<std::size_t>(j * out.columns + i);
            level[idx] = l;
            const float ground = map.elevation({static_cast<float>(i), static_cast<float>(j)}) * kMetre;
            out.heights[idx] = ground;
            if (l == 0)
                continue;
            const float n = fbm(static_cast<float>(i) * 0.15f, static_cast<float>(j) * 0.15f, seed);
            const float rise = std::min(static_cast<float>(l), 5.0f);
            out.heights[idx] = ground + 1.7f + rise * 0.45f + n * 1.4f * std::min(rise / 3.0f, 1.0f);
        }

    std::vector<glm::vec2> structures;
    std::vector<float> pads;
    for (const auto& u : map.units()) {
        const UnitDefId id = data.find(u.unit);
        if (id == kInvalidUnitDef)
            continue;
        structures.push_back(u.pos);
        pads.push_back(data.unit(id).radius + 1.6f);
    }

    const bool winter = map.theme() == "winter";
    const bool city = !map.paths().empty();
    std::vector<float> roadPaint(out.heights.size(), 0.0f);
    std::vector<float> dirtPaint(out.heights.size(), 0.0f);
    auto stamp = [&](std::vector<float>& grid, glm::vec2 a, glm::vec2 b, float half, float strength) {
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - half - 1.0f)));
        const int x1 = std::min(out.columns - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + half + 1.0f)));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - half - 1.0f)));
        const int y1 = std::min(out.rows - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + half + 1.0f)));
        for (int j = y0; j <= y1; ++j)
            for (int i = x0; i <= x1; ++i) {
                const float d = segmentDistance({static_cast<float>(i), static_cast<float>(j)}, a, b);
                auto& v = grid[static_cast<std::size_t>(j * out.columns + i)];
                v = std::max(v, strength * (1.0f - smooth(half - 0.3f, half + 0.7f, d)));
            }
    };
    for (const auto& path : map.paths()) {
        const float half = std::max(path.width, 0.6f) * 0.5f;
        for (std::size_t k = 0; k + 1 < path.points.size(); ++k) {
            if (path.kind == "road" && winter)
                stamp(dirtPaint, path.points[k], path.points[k + 1], half + 0.6f, 0.55f);
            else if (path.kind == "road")
                stamp(roadPaint, path.points[k], path.points[k + 1], half, 1.0f);
            else if (path.kind == "track" || path.kind == "rail")
                stamp(dirtPaint, path.points[k], path.points[k + 1], half, 0.9f);
            else if (path.kind == "path")
                stamp(dirtPaint, path.points[k], path.points[k + 1], 0.5f, 0.6f);
        }
    }
    for (const auto& area : map.areas()) {
        const float strength = area.kind == "industrial" ? 0.75f : area.kind == "yard" ? 0.5f : 0.0f;
        if (strength <= 0.0f)
            continue;
        glm::vec2 lo = area.poly[0], hi = area.poly[0];
        for (const auto p : area.poly) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        for (int j = std::max(0, static_cast<int>(lo.y)); j <= std::min(out.rows - 1, static_cast<int>(hi.y) + 1); ++j)
            for (int i = std::max(0, static_cast<int>(lo.x)); i <= std::min(out.columns - 1, static_cast<int>(hi.x) + 1); ++i)
                if (pointInPolygon({static_cast<float>(i), static_cast<float>(j)}, area.poly)) {
                    const float n = fbm(static_cast<float>(i) * 0.2f, static_cast<float>(j) * 0.2f, seed + 31u);
                    auto& v = dirtPaint[static_cast<std::size_t>(j * out.columns + i)];
                    v = std::max(v, strength * (0.6f + 0.4f * n));
                }
    }

    std::vector<TerrainVertex> vertices;
    vertices.reserve(out.heights.size());
    auto heightOf = [&](int i, int j) {
        i = std::clamp(i, 0, out.columns - 1);
        j = std::clamp(j, 0, out.rows - 1);
        return out.heights[static_cast<std::size_t>(j * out.columns + i)];
    };
    for (int j = 0; j < out.rows; ++j)
        for (int i = 0; i < out.columns; ++i) {
            const float y = heightOf(i, j);
            const glm::vec3 n = glm::normalize(glm::vec3(heightOf(i - 1, j) - heightOf(i + 1, j), 2.0f, heightOf(i, j - 1) - heightOf(i, j + 1)));
            const glm::vec2 p{static_cast<float>(i), static_cast<float>(j)};
            const int l = level[static_cast<std::size_t>(j * out.columns + i)];

            const auto vi = static_cast<std::size_t>(j * out.columns + i);
            float road = roadPaint[vi];
            if (!city)
                for (const auto& lane : map.lanes())
                    for (std::size_t k = 0; k + 1 < lane.points.size(); ++k)
                        road = std::max(road, 1.0f - smooth(2.6f, 4.2f, segmentDistance(p, lane.points[k], lane.points[k + 1])));
            for (std::size_t k = 0; k < structures.size(); ++k)
                road = std::max(road, 1.0f - smooth(pads[k] - 0.6f, pads[k] + 0.6f, glm::distance(p, structures[k])));
            const float patch = fbm(p.x * 0.07f + 13.0f, p.y * 0.07f, seed + 101u);
            float dirt = std::max(winter ? 0.0f : smooth(0.52f, 0.7f, patch), dirtPaint[vi]);
            float cliff = std::clamp((1.0f - n.y) * 3.2f, 0.0f, 1.0f);
            if (l > 0) {
                dirt = std::max(dirt, 0.55f + 0.45f * smooth(0.3f, 0.7f, patch));
                road = 0.0f;
            }
            road *= 1.0f - cliff;
            dirt *= 1.0f - cliff;
            const float ground = std::max(0.0f, 1.0f - road - dirt - cliff);
            const std::uint32_t weights = static_cast<std::uint32_t>(toByte(ground)) | static_cast<std::uint32_t>(toByte(dirt)) << 8 |
                                          static_cast<std::uint32_t>(toByte(cliff)) << 16 | static_cast<std::uint32_t>(toByte(road)) << 24;
            vertices.push_back({p.x, y, p.y, n.x, n.y, n.z, weights});
        }

    const int chunksX = (w + kChunk - 1) / kChunk;
    const int chunksY = (h + kChunk - 1) / kChunk;
    out.chunks.resize(static_cast<std::size_t>(chunksX * chunksY));
    for (int cy = 0; cy < chunksY; ++cy)
        for (int cx = 0; cx < chunksX; ++cx) {
            const int x0 = cx * kChunk;
            const int y0 = cy * kChunk;
            const int x1 = std::min(w, x0 + kChunk);
            const int y1 = std::min(h, y0 + kChunk);
            const int cols = x1 - x0 + 1;
            std::vector<TerrainVertex> local;
            local.reserve(static_cast<std::size_t>(cols * (y1 - y0 + 1)));
            for (int j = y0; j <= y1; ++j)
                for (int i = x0; i <= x1; ++i)
                    local.push_back(vertices[static_cast<std::size_t>(j * out.columns + i)]);
            std::vector<std::uint32_t> indices;
            indices.reserve(static_cast<std::size_t>((x1 - x0) * (y1 - y0) * 6));
            for (int j = y0; j < y1; ++j)
                for (int i = x0; i < x1; ++i) {
                    const auto a = static_cast<std::uint32_t>((j - y0) * cols + (i - x0));
                    const auto b = a + 1;
                    const auto c = a + static_cast<std::uint32_t>(cols);
                    const auto d = c + 1;
                    if (((i + j) & 1) == 0) {
                        for (const auto v : {a, c, b, b, c, d})
                            indices.push_back(v);
                    } else {
                        for (const auto v : {a, c, d, a, d, b})
                            indices.push_back(v);
                    }
                }
            auto& chunk = out.chunks[static_cast<std::size_t>(cy * chunksX + cx)];
            chunk.min = {static_cast<float>(x0), static_cast<float>(y0)};
            chunk.max = {static_cast<float>(x1), static_cast<float>(y1)};
            chunk.terrain = renderer.createTerrain(local, indices);
        }
    const std::string terrainDir = winter ? "data/terrain/winter/" : "data/terrain/";
    out.terrainTextures = {models.texture(terrainDir + "ground.png"), models.texture(terrainDir + "soil.png"), models.texture(terrainDir + "cliff.png"),
                           models.texture(terrainDir + "road.png")};

    out.derrick = models.model("colony_extractor:hull");

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const Model* trees[] = {models.model("prop_tree1:hull"), models.model("prop_tree2:hull"), models.model("prop_tree3:hull")};
    const Model* bushes[] = {models.model("prop_crystal1:hull"), models.model("prop_crystal2:hull")};
    const Model* boulders[] = {models.model("prop_boulder1:hull"), models.model("prop_boulder2:hull"), models.model("prop_boulder3:hull")};
    const Model* wreck = models.model("prop_wreck:hull");
    struct Geometry {
        std::vector<TexVertex> vertices;
        std::vector<std::uint32_t> indices;
        TextureId texture;
        TextureId mask;
        bool glow = false;
        bool cutaway = false;
    };
    std::map<std::tuple<std::size_t, TextureId, TextureId>, Geometry> merged;
    auto place = [&](const Model* model, glm::vec2 at, float size, bool byHeight, float yaw = -1.0f) {
        if (!model || model->vertices.empty())
            return;
        const float extent = byHeight ? std::max(model->max.y, 1.0f) : std::max({model->max.x - model->min.x, model->max.z - model->min.z, 1.0f});
        float m[16];
        makeModelMatrix(m, {at.x, out.heightAt(at.x, at.y) - 0.1f, at.y}, yaw < 0.0f ? unit(rng) * 6.283f : yaw, size / extent, {});
        const int cx = std::clamp(static_cast<int>(at.x) / kChunk, 0, chunksX - 1);
        const int cy = std::clamp(static_cast<int>(at.y) / kChunk, 0, chunksY - 1);
        auto& g = merged[{static_cast<std::size_t>(cy * chunksX + cx), model->texture, model->mask}];
        g.texture = model->texture;
        g.mask = model->mask;
        g.glow = model->glow;
        const auto base = static_cast<std::uint32_t>(g.vertices.size());
        for (const auto& v : model->vertices) {
            TexVertex t = v;
            t.x = v.x * m[0] + v.y * m[4] + v.z * m[8] + m[12];
            t.y = v.x * m[1] + v.y * m[5] + v.z * m[9] + m[13];
            t.z = v.x * m[2] + v.y * m[6] + v.z * m[10] + m[14];
            const glm::vec3 n = glm::normalize(glm::vec3(v.nx * m[0] + v.ny * m[4] + v.nz * m[8], v.nx * m[1] + v.ny * m[5] + v.nz * m[9],
                                                         v.nx * m[2] + v.ny * m[6] + v.nz * m[10]));
            t.nx = n.x;
            t.ny = n.y;
            t.nz = n.z;
            g.vertices.push_back(t);
        }
        for (const auto i : model->indices)
            g.indices.push_back(base + i);
    };
    auto chunkOf = [&](glm::vec2 p) {
        const int cx = std::clamp(static_cast<int>(p.x) / kChunk, 0, chunksX - 1);
        const int cy = std::clamp(static_cast<int>(p.y) / kChunk, 0, chunksY - 1);
        return static_cast<std::size_t>(cy * chunksX + cx);
    };
    std::array<std::pair<TextureId, TextureId>, static_cast<std::size_t>(Facade::Count)> facades{};
    if (!map.buildings().empty())
        for (std::size_t f = 0; f < facades.size(); ++f) {
            const std::string name = std::string("data/city/") + facadeName(static_cast<Facade>(f));
            facades[f] = {models.texture(name + ".png"), models.texture(name + "_mask.png")};
        }
    for (std::size_t k = 0; k < map.buildings().size(); ++k) {
        const auto& b = map.buildings()[k];
        glm::vec2 c{0.0f};
        for (const auto p : b.poly)
            c += p;
        c /= static_cast<float>(b.poly.size());
        std::array<MeshData, static_cast<std::size_t>(Facade::Count)> parts;
        extrudeBuilding(b, seed ^ static_cast<std::uint32_t>(k * 2654435761u), parts);
        float base = out.heightAt(c.x, c.y);
        for (const auto p : b.poly)
            base = std::min(base, out.heightAt(p.x, p.y));
        for (auto& part : parts)
            for (auto& v : part.vertices)
                v.y += base;
        if (out.roofs.empty())
            out.roofs.assign(static_cast<std::size_t>(w * h), 0.0f);
        glm::vec2 lo = b.poly[0], hi = b.poly[0];
        for (const auto p : b.poly) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        for (int y = std::max(0, static_cast<int>(lo.y)); y <= std::min(h - 1, static_cast<int>(hi.y)); ++y)
            for (int x = std::max(0, static_cast<int>(lo.x)); x <= std::min(w - 1, static_cast<int>(hi.x)); ++x)
                if (map.cellKind(x, y) == CellKind::Building)
                    out.roofs[static_cast<std::size_t>(y * w + x)] = std::max(out.roofs[static_cast<std::size_t>(y * w + x)], base + buildingHeight(b) + 0.6f);
        const auto chunk = chunkOf(c);
        for (std::size_t f = 0; f < parts.size(); ++f) {
            if (parts[f].indices.empty())
                continue;
            auto& g = merged[{chunk, facades[f].first, facades[f].second}];
            g.texture = facades[f].first;
            g.mask = facades[f].second;
            g.glow = true;
            g.cutaway = true;
            const auto first = static_cast<std::uint32_t>(g.vertices.size());
            g.vertices.insert(g.vertices.end(), parts[f].vertices.begin(), parts[f].vertices.end());
            for (const auto i : parts[f].indices)
                g.indices.push_back(first + i);
        }
    }

    auto freeCell = [&](glm::vec2 p) { return map.cellKind(static_cast<int>(p.x), static_cast<int>(p.y)) == CellKind::Open; };
    struct PropLook {
        const char* name;
        float size;
        bool byHeight;
    };
    static constexpr PropLook kPropLooks[] = {{"prop_gas_station", 6.0f, false}, {"prop_substation", 4.0f, false}, {"prop_water_tower", 5.5f, true},
                                              {"prop_cell_tower", 9.0f, true},   {"prop_bus_stop", 1.8f, false},  {"prop_kiosk", 1.3f, false},
                                              {"prop_crane", 13.0f, true},       {"prop_trash1", 1.2f, false},    {"prop_trash2", 1.2f, false},
                                              {"prop_barrels", 0.9f, false},     {"prop_tires", 0.8f, false},     {"prop_blocks", 1.6f, false}};
    auto placeProp = [&](const std::string& name, glm::vec2 at, float yaw) {
        for (const auto& look : kPropLooks)
            if (name == look.name) {
                place(models.model(name + ":hull"), at, look.size, look.byHeight, yaw);
                return;
            }
        place(models.model(name + ":hull"), at, 1.5f, false, yaw);
    };
    for (const auto& p : map.props())
        placeProp(p.model, p.pos, p.yaw * 0.0174533f);
    static constexpr const char* kClutter[] = {"prop_trash1", "prop_trash2", "prop_barrels", "prop_tires", "prop_blocks"};
    for (std::size_t k = 0; k < map.buildings().size(); ++k) {
        const auto& b = map.buildings()[k];
        if (b.kind == "house" || b.poly.size() < 3)
            continue;
        glm::vec2 c{0.0f};
        for (const auto p : b.poly)
            c += p;
        c /= static_cast<float>(b.poly.size());
        std::size_t longest = 0;
        for (std::size_t i = 0; i < b.poly.size(); ++i)
            if (glm::distance(b.poly[i], b.poly[(i + 1) % b.poly.size()]) >
                glm::distance(b.poly[longest], b.poly[(longest + 1) % b.poly.size()]))
                longest = i;
        const glm::vec2 mid = (b.poly[longest] + b.poly[(longest + 1) % b.poly.size()]) * 0.5f;
        const glm::vec2 outward = glm::length(mid - c) > 1e-3f ? glm::normalize(mid - c) : glm::vec2(0.0f, 1.0f);
        if (b.unfinished) {
            const glm::vec2 at = mid + outward * 3.5f;
            if (freeCell(at))
                placeProp("prop_crane", at, unit(rng) * 6.283f);
            for (int i = 0; i < 2; ++i) {
                const glm::vec2 q = mid + outward * (1.6f + unit(rng)) + glm::vec2(unit(rng) - 0.5f, unit(rng) - 0.5f) * 3.0f;
                if (freeCell(q))
                    placeProp("prop_blocks", q, unit(rng) * 6.283f);
            }
            continue;
        }
        const int count = b.ruin ? 3 : unit(rng) < 0.45f ? 1 : 0;
        for (int i = 0; i < count; ++i) {
            const std::size_t e = static_cast<std::size_t>(unit(rng) * static_cast<float>(b.poly.size())) % b.poly.size();
            const glm::vec2 m2 = glm::mix(b.poly[e], b.poly[(e + 1) % b.poly.size()], 0.2f + unit(rng) * 0.6f);
            const glm::vec2 q = m2 + (glm::length(m2 - c) > 1e-3f ? glm::normalize(m2 - c) : outward) * (1.0f + unit(rng) * 0.8f);
            if (freeCell(q))
                placeProp(b.ruin ? (unit(rng) < 0.6f ? "prop_blocks" : "prop_trash2") : kClutter[static_cast<std::size_t>(unit(rng) * 4.99f)], q,
                          unit(rng) * 6.283f);
        }
    }
    if (winter) {
        const TextureId asphalt = models.texture("data/terrain/winter/road.png");
        const TextureId black = models.texture("data/city/flat_mask.png");
        int order = 0;
        for (const auto& path : map.paths()) {
            if (path.kind != "road")
                continue;
            const float half = std::max(path.width, 0.7f) * 0.5f;
            const float lift = 0.05f + 0.001f * static_cast<float>(order++ % 16);
            for (std::size_t k = 0; k + 1 < path.points.size(); ++k) {
                const glm::vec2 a = path.points[k];
                const glm::vec2 b = path.points[k + 1];
                const float len = glm::distance(a, b);
                if (len < 1e-3f)
                    continue;
                const glm::vec2 dir = (b - a) / len;
                const glm::vec2 side = glm::vec2(-dir.y, dir.x) * half;
                const glm::vec2 a0 = a - dir * half * 0.9f;
                const glm::vec2 b0 = b + dir * half * 0.9f;
                const int steps = std::max(1, static_cast<int>(std::ceil(glm::distance(a0, b0))));
                auto& g = merged[{chunkOf((a + b) * 0.5f), asphalt, black}];
                g.texture = asphalt;
                g.mask = black;
                for (int s = 0; s < steps; ++s) {
                    const glm::vec2 p0 = glm::mix(a0, b0, static_cast<float>(s) / static_cast<float>(steps));
                    const glm::vec2 p1 = glm::mix(a0, b0, static_cast<float>(s + 1) / static_cast<float>(steps));
                    const auto base = static_cast<std::uint32_t>(g.vertices.size());
                    for (const glm::vec2 p : {p0 - side, p1 - side, p1 + side, p0 + side})
                        g.vertices.push_back({p.x, out.heightAt(p.x, p.y) + lift, p.y, 0.0f, 1.0f, 0.0f, p.x / 4.0f, p.y / 4.0f});
                    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                        g.indices.push_back(base + i);
                }
            }
        }
    }
    if (winter) {
        const Model* firs[] = {models.model("prop_fir1:hull"), models.model("prop_fir2:hull"), models.model("prop_fir3:hull")};
        const Model* birches[] = {models.model("prop_birch1:hull"), models.model("prop_birch2:hull")};
        const Model* cars[] = {models.model("prop_car1:hull"), models.model("prop_car2:hull")};
        const Model* lamps[] = {models.model("prop_lamp:hull"), models.model("prop_lamp_off:hull")};
        const Model* pylon = models.model("prop_pylon:hull");
        for (const auto& path : map.paths()) {
            if (path.kind == "road" && path.width >= 1.2f) {
                float carry = unit(rng) * 9.0f;
                for (std::size_t k = 0; k + 1 < path.points.size(); ++k) {
                    const glm::vec2 a = path.points[k];
                    const glm::vec2 b = path.points[k + 1];
                    const float len = glm::distance(a, b);
                    if (len < 1e-3f)
                        continue;
                    const glm::vec2 dir = (b - a) / len;
                    const glm::vec2 side{-dir.y, dir.x};
                    const float yaw = std::atan2(dir.y, dir.x);
                    float t = carry;
                    for (; t < len; t += 9.0f) {
                        const float s = (static_cast<int>(t) / 9) % 2 ? 1.0f : -1.0f;
                        const glm::vec2 at = a + dir * t + side * s * (path.width * 0.5f + 0.5f);
                        if (freeCell(at))
                            place(lamps[unit(rng) < 0.6f ? 0 : 1], at, 3.0f, true, yaw + (s > 0.0f ? 3.1416f : 0.0f) + kModelYaw);
                        if (unit(rng) < 0.18f) {
                            const glm::vec2 car = a + dir * (t + 4.0f) + side * (unit(rng) - 0.5f) * path.width * 0.8f;
                            if (freeCell(car))
                                place(cars[unit(rng) < 0.5f ? 0 : 1], car, 1.25f, false, yaw + kModelYaw + (unit(rng) - 0.5f) * 0.8f);
                        }
                    }
                    carry = t - len;
                }
            } else if (path.kind == "power") {
                std::vector<glm::vec3> towers;
                for (std::size_t k = 0; k + 1 < path.points.size(); ++k) {
                    const glm::vec2 a = path.points[k];
                    const glm::vec2 b = path.points[k + 1];
                    const float len = glm::distance(a, b);
                    for (float t = 0.0f; t < len; t += 14.0f) {
                        const glm::vec2 at = a + (b - a) * (t / std::max(len, 1e-3f));
                        if (!freeCell(at))
                            continue;
                        const float yaw = std::atan2(b.y - a.y, b.x - a.x) + 1.5708f;
                        place(pylon, at, 4.2f, true, yaw);
                        towers.push_back({at.x, at.y, yaw});
                    }
                }
                if (!pylon || pylon->vertices.empty() || towers.size() < 2)
                    continue;
                const float scale = 4.2f / std::max(pylon->max.y, 1.0f);
                auto& g = merged[{chunkOf(path.points.front()), pylon->texture, pylon->mask}];
                g.texture = pylon->texture;
                g.mask = pylon->mask;
                g.glow = pylon->glow;
                for (std::size_t k = 0; k + 1 < towers.size(); ++k) {
                    if (glm::distance(glm::vec2(towers[k]), glm::vec2(towers[k + 1])) > 20.0f)
                        continue;
                    for (const float off : {-2.4f, 2.4f, -1.6f, 1.6f}) {
                        const float hgt = (std::abs(off) > 2.0f ? 5.25f : 6.45f) * scale - 0.1f;
                        auto end = [&](const glm::vec3& t) {
                            return glm::vec3(t.x + std::cos(t.z) * off * scale, hgt + out.heightAt(t.x, t.y), t.y + std::sin(t.z) * off * scale);
                        };
                        const glm::vec3 p0 = end(towers[k]);
                        const glm::vec3 p1 = end(towers[k + 1]);
                        constexpr int kSeg = 6;
                        auto at = [&](int i) {
                            const float t = static_cast<float>(i) / kSeg;
                            glm::vec3 q = glm::mix(p0, p1, t);
                            q.y -= 4.0f * t * (1.0f - t) * 0.6f;
                            return q;
                        };
                        for (int s = 0; s < kSeg; ++s) {
                            const glm::vec3 q0 = at(s);
                            const glm::vec3 q1 = at(s + 1);
                            const auto base = static_cast<std::uint32_t>(g.vertices.size());
                            const glm::vec3 up{0.0f, 0.03f, 0.0f};
                            for (const glm::vec3 v : {q0 - up, q1 - up, q1 + up, q0 + up})
                                g.vertices.push_back({v.x, v.y, v.z, 0.0f, 1.0f, 0.0f, 0.06f, 0.12f});
                            for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                                g.indices.push_back(base + i);
                        }
                    }
                }
            }
        }
        std::vector<std::uint8_t> sparse(static_cast<std::size_t>(w * h), 0);
        for (const auto& area : map.areas()) {
            if (area.kind != "scrub" && area.kind != "park" && area.kind != "cemetery")
                continue;
            glm::vec2 lo = area.poly[0], hi = area.poly[0];
            for (const auto p : area.poly) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            for (int y = std::max(0, static_cast<int>(lo.y)); y <= std::min(h - 1, static_cast<int>(hi.y)); ++y)
                for (int x = std::max(0, static_cast<int>(lo.x)); x <= std::min(w - 1, static_cast<int>(hi.x)); ++x)
                    if (pointInPolygon(Map::center({x, y}), area.poly))
                        sparse[static_cast<std::size_t>(y * w + x)] = 1;
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto kind = map.cellKind(x, y);
                const glm::vec2 at{static_cast<float>(x) + 0.2f + unit(rng) * 0.6f, static_cast<float>(y) + 0.2f + unit(rng) * 0.6f};
                const float roll = unit(rng);
                const auto vi = static_cast<std::size_t>(y * out.columns + x);
                if (kind == CellKind::Forest || (kind == CellKind::Rock && cellDist(x, y) >= 1)) {
                    if (roll < 0.5f)
                        place(firs[static_cast<std::size_t>(unit(rng) * 2.99f)], at, 3.0f + unit(rng) * 2.2f, true);
                    else if (roll < 0.7f)
                        place(birches[static_cast<std::size_t>(unit(rng) * 1.99f)], at, 2.8f + unit(rng) * 1.6f, true);
                } else if (kind == CellKind::Open && roadPaint[vi] < 0.1f) {
                    const bool near = sparse[static_cast<std::size_t>(y * w + x)] != 0;
                    const float forest = fbm(static_cast<float>(x) * 0.06f, static_cast<float>(y) * 0.06f, seed + 7u);
                    const float chance = near ? 0.06f : 0.002f + std::max(0.0f, forest - 0.6f) * 0.08f;
                    if (roll < chance)
                        place(unit(rng) < 0.6f ? birches[static_cast<std::size_t>(unit(rng) * 1.99f)] : firs[static_cast<std::size_t>(unit(rng) * 2.99f)], at,
                              2.4f + unit(rng) * 2.0f, true);
                    else if (roll > 0.9993f)
                        place(wreck, at, 1.6f + unit(rng) * 0.6f, false);
                }
            }
    }
    for (int y = 0; y < h && !winter; ++y)
        for (int x = 0; x < w; ++x) {
            const int d = cellDist(x, y);
            if (d < 2)
                continue;
            const glm::vec2 at{static_cast<float>(x) + 0.2f + unit(rng) * 0.6f, static_cast<float>(y) + 0.2f + unit(rng) * 0.6f};
            const float roll = unit(rng);
            const float forest = fbm(static_cast<float>(x) * 0.09f, static_cast<float>(y) * 0.09f, seed + 7u);
            if (d >= 3 && roll < 0.002f)
                place(wreck, at, 1.6f + unit(rng) * 0.6f, false);
            else if (roll < 0.02f + forest * 0.12f)
                place(trees[static_cast<std::size_t>(unit(rng) * 2.99f)], at, 2.4f + unit(rng) * 1.8f, true);
            else if (roll < 0.2f && roll > 0.17f)
                place(bushes[static_cast<std::size_t>(unit(rng) * 1.99f)], at, 1.0f + unit(rng) * 0.8f, false);
            else if (roll > 0.985f)
                place(boulders[static_cast<std::size_t>(unit(rng) * 2.99f)], at, 0.9f + unit(rng) * 1.4f, false);
        }
    for (const auto& [key, g] : merged)
        out.chunks[std::get<0>(key)].props.push_back({renderer.createMesh(g.vertices, g.indices), g.texture, g.mask, g.glow, g.cutaway});
    return out;
}

}
