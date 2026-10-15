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
        const auto point = glb.points.find(part == "hull" ? "pivot" : part == "barrel" ? "muzzle" : "");
        if (point != glb.points.end())
            m->connectors.push_back(point->second);
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
            if (!map.blocked(x, y)) {
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
            if (l == 0)
                continue;
            const float n = fbm(static_cast<float>(i) * 0.15f, static_cast<float>(j) * 0.15f, seed);
            const float rise = std::min(static_cast<float>(l), 5.0f);
            out.heights[idx] = 1.7f + rise * 0.45f + n * 1.4f * std::min(rise / 3.0f, 1.0f);
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

            float road = 0.0f;
            for (const auto& lane : map.lanes())
                for (std::size_t k = 0; k + 1 < lane.points.size(); ++k)
                    road = std::max(road, 1.0f - smooth(2.6f, 4.2f, segmentDistance(p, lane.points[k], lane.points[k + 1])));
            for (std::size_t k = 0; k < structures.size(); ++k)
                road = std::max(road, 1.0f - smooth(pads[k] - 0.6f, pads[k] + 0.6f, glm::distance(p, structures[k])));
            const float patch = fbm(p.x * 0.07f + 13.0f, p.y * 0.07f, seed + 101u);
            float dirt = smooth(0.52f, 0.7f, patch);
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
    out.terrainTextures = {models.texture("data/terrain/ground.png"), models.texture("data/terrain/soil.png"), models.texture("data/terrain/cliff.png"),
                           models.texture("data/terrain/road.png")};

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
    };
    std::map<std::tuple<std::size_t, TextureId, TextureId>, Geometry> merged;
    auto place = [&](const Model* model, glm::vec2 at, float size, bool byHeight) {
        if (!model || model->vertices.empty())
            return;
        const float extent = byHeight ? std::max(model->max.y, 1.0f) : std::max({model->max.x - model->min.x, model->max.z - model->min.z, 1.0f});
        float m[16];
        makeModelMatrix(m, {at.x, out.heightAt(at.x, at.y) - 0.1f, at.y}, unit(rng) * 6.283f, size / extent, {});
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
    for (int y = 0; y < h; ++y)
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
        out.chunks[std::get<0>(key)].props.push_back({renderer.createMesh(g.vertices, g.indices), g.texture, g.mask, g.glow});
    return out;
}

}
