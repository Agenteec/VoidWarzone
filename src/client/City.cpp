#include "City.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace vw::client {

namespace {

constexpr float kBay = 0.5f;
constexpr float kTile = 8.0f;

float cross(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - a.y * b.x;
}

float signedArea(const std::vector<glm::vec2>& poly) {
    float a = 0.0f;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        a += cross(poly[j], poly[i]);
    return a * 0.5f;
}

void addQuad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec2 ta, glm::vec2 tb, glm::vec2 tc, glm::vec2 td) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (const auto& [p, t] : {std::pair{a, ta}, std::pair{b, tb}, std::pair{c, tc}, std::pair{d, td}})
        m.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y});
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
        m.indices.push_back(base + i);
}

void addTriangle(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec2 ta, glm::vec2 tb, glm::vec2 tc) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    if (n.y < 0.0f && std::abs(n.y) > 0.1f)
        n = -n;
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (const auto& [p, t] : {std::pair{a, ta}, std::pair{b, tb}, std::pair{c, tc}})
        m.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y});
    for (const std::uint32_t i : {0u, 1u, 2u})
        m.indices.push_back(base + i);
}

Facade wallFacade(const std::string& kind) {
    if (kind == "panel")
        return Facade::Panel;
    if (kind == "house")
        return Facade::House;
    if (kind == "garage")
        return Facade::Garage;
    if (kind == "industrial" || kind == "shed")
        return Facade::Industrial;
    return Facade::Brick;
}

}

const char* facadeName(Facade f) {
    static constexpr const char* kNames[] = {"panel", "brick", "industrial", "house", "roof", "slate", "ruin", "garage", "rubble"};
    return kNames[static_cast<std::size_t>(f)];
}

std::vector<std::uint32_t> triangulate(const std::vector<glm::vec2>& poly) {
    std::vector<std::uint32_t> out;
    const std::size_t n = poly.size();
    if (n < 3)
        return out;
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i)
        idx[i] = static_cast<std::uint32_t>(i);
    const float orient = signedArea(poly) >= 0.0f ? 1.0f : -1.0f;
    auto inside = [&](glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        const float d1 = cross(b - a, p - a) * orient;
        const float d2 = cross(c - b, p - b) * orient;
        const float d3 = cross(a - c, p - c) * orient;
        return d1 > 0.0f && d2 > 0.0f && d3 > 0.0f;
    };
    std::size_t guard = n * n + 8;
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const auto ia = idx[(i + idx.size() - 1) % idx.size()];
            const auto ib = idx[i];
            const auto ic = idx[(i + 1) % idx.size()];
            const glm::vec2 a = poly[ia], b = poly[ib], c = poly[ic];
            if (cross(b - a, c - b) * orient <= 1e-6f)
                continue;
            bool ear = true;
            for (const auto k : idx)
                if (k != ia && k != ib && k != ic && inside(poly[k], a, b, c)) {
                    ear = false;
                    break;
                }
            if (!ear)
                continue;
            out.insert(out.end(), {ia, ib, ic});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
            break;
    }
    if (idx.size() == 3) {
        out.insert(out.end(), {idx[0], idx[1], idx[2]});
    } else {
        for (std::size_t i = 1; i + 1 < idx.size(); ++i)
            out.insert(out.end(), {idx[0], idx[i], idx[i + 1]});
    }
    return out;
}

float buildingHeight(const BuildingDef& b) {
    const float levels = static_cast<float>(std::max(b.levels, 1));
    if (b.kind == "industrial")
        return levels * kStoreyHeight * 1.5f + 0.2f;
    if (b.kind == "garage")
        return 0.75f;
    if (b.kind == "house" || b.kind == "shed")
        return levels * kStoreyHeight * 1.5f;
    return levels * kStoreyHeight;
}

void extrudeBuilding(const BuildingDef& b, std::uint32_t seed, std::array<MeshData, static_cast<std::size_t>(Facade::Count)>& out) {
    const auto& poly = b.poly;
    if (poly.size() < 3)
        return;
    const float top = buildingHeight(b);
    const float bottom = -0.3f;
    const float storey = top / static_cast<float>(std::max(b.levels, 1));
    const float ou = static_cast<float>(seed % 8u) / kTile;
    const float ov = static_cast<float>((seed / 8u) % 8u) / kTile;
    const Facade wall = b.ruin ? Facade::Ruin : wallFacade(b.kind);
    auto& walls = out[static_cast<std::size_t>(wall)];
    const float orient = signedArea(poly) >= 0.0f ? 1.0f : -1.0f;
    const float phase = static_cast<float>(seed % 997u) * 0.37f;
    auto ruinHeight = [&](float s) {
        const float wave = 0.5f + 0.4f * std::sin(s * 0.35f + phase) + 0.25f * std::sin(s * 1.9f + phase * 2.3f);
        const float step = std::floor(wave * 6.0f) / 6.0f;
        return std::max(storey * 0.6f, top * std::clamp(step, 0.05f, 1.0f));
    };
    if (b.ruin) {
        float along = 0.0f;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const glm::vec2 a = poly[i];
            const glm::vec2 c = poly[(i + 1) % poly.size()];
            const float len = glm::distance(a, c);
            if (len < 1e-4f)
                continue;
            const glm::vec2 d = (c - a) / len;
            const glm::vec3 n = glm::vec3(d.y, 0.0f, -d.x) * orient;
            const int steps = std::max(1, static_cast<int>(std::ceil(len / kBay)));
            for (int k = 0; k < steps; ++k) {
                const float s0 = along + len * static_cast<float>(k) / static_cast<float>(steps);
                const float s1 = along + len * static_cast<float>(k + 1) / static_cast<float>(steps);
                const glm::vec2 p0 = glm::mix(a, c, static_cast<float>(k) / static_cast<float>(steps));
                const glm::vec2 p1 = glm::mix(a, c, static_cast<float>(k + 1) / static_cast<float>(steps));
                const float h0 = ruinHeight(s0);
                const float h1 = ruinHeight(s1);
                auto v = [&](float y) { return (top - y) / (storey * kTile) + ov; };
                const float u0 = s0 / (kBay * kTile) + ou;
                const float u1 = s1 / (kBay * kTile) + ou;
                addQuad(walls, {p0.x, bottom, p0.y}, {p1.x, bottom, p1.y}, {p1.x, h1, p1.y}, {p0.x, h0, p0.y}, n, {u0, v(bottom)}, {u1, v(bottom)},
                        {u1, v(h1)}, {u0, v(h0)});
            }
            along += len;
        }
        auto& heap = out[static_cast<std::size_t>(Facade::Rubble)];
        glm::vec2 c{0.0f};
        for (const auto p : poly)
            c += p;
        c /= static_cast<float>(poly.size());
        const float peak = std::min(top * 0.45f, 2.2f) + storey;
        const glm::vec3 apex{c.x, peak, c.y};
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const glm::vec2 a = poly[i];
            const glm::vec2 e = poly[(i + 1) % poly.size()];
            const glm::vec2 m = (a + e) * 0.5f;
            const glm::vec3 pa{a.x, storey * 0.5f, a.y};
            const glm::vec3 pe{e.x, storey * 0.5f, e.y};
            const glm::vec3 pm{glm::mix(m, c, 0.45f).x, peak * 0.7f, glm::mix(m, c, 0.45f).y};
            addTriangle(heap, pa, pe, pm, {a.x / 3.0f, a.y / 3.0f}, {e.x / 3.0f, e.y / 3.0f}, {pm.x / 3.0f, pm.z / 3.0f});
            addTriangle(heap, pa, pm, apex, {a.x / 3.0f, a.y / 3.0f}, {pm.x / 3.0f, pm.z / 3.0f}, {c.x / 3.0f, c.y / 3.0f});
            addTriangle(heap, pm, pe, apex, {pm.x / 3.0f, pm.z / 3.0f}, {e.x / 3.0f, e.y / 3.0f}, {c.x / 3.0f, c.y / 3.0f});
        }
        return;
    }

    float along = 0.0f;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const glm::vec2 a = poly[i];
        const glm::vec2 c = poly[(i + 1) % poly.size()];
        const float len = glm::distance(a, c);
        if (len < 1e-4f)
            continue;
        const glm::vec2 d = (c - a) / len;
        glm::vec2 n2{d.y, -d.x};
        n2 *= orient;
        const glm::vec3 n{n2.x, 0.0f, n2.y};
        const float u0 = along / (kBay * kTile) + ou;
        const float u1 = (along + len) / (kBay * kTile) + ou;
        const float vb = (top - bottom) / (storey * kTile) + ov;
        addQuad(walls, {a.x, bottom, a.y}, {c.x, bottom, c.y}, {c.x, top, c.y}, {a.x, top, a.y}, n, {u0, vb}, {u1, vb}, {u1, ov}, {u0, ov});
        if (wall != Facade::House)
            addQuad(walls, {a.x, top, a.y}, {c.x, top, c.y}, {c.x, top + 0.12f, c.y}, {a.x, top + 0.12f, a.y}, n, {u0, ov + 0.012f}, {u1, ov + 0.012f},
                    {u1, ov + 0.002f}, {u0, ov + 0.002f});
        along += len;
    }

    const bool gable = poly.size() == 4 && (b.kind == "house" || b.kind == "shed");
    if (gable) {
        int s = glm::distance(poly[0], poly[1]) >= glm::distance(poly[1], poly[2]) ? 0 : 1;
        const glm::vec2 p0 = poly[static_cast<std::size_t>(s)];
        const glm::vec2 p1 = poly[static_cast<std::size_t>(s + 1)];
        const glm::vec2 p2 = poly[static_cast<std::size_t>((s + 2) % 4)];
        const glm::vec2 p3 = poly[static_cast<std::size_t>((s + 3) % 4)];
        const float rise = std::min(0.55f, glm::distance(p1, p2) * 0.35f);
        const glm::vec2 m03 = (p0 + p3) * 0.5f;
        const glm::vec2 m12 = (p1 + p2) * 0.5f;
        const glm::vec3 r0{m03.x, top + rise, m03.y};
        const glm::vec3 r1{m12.x, top + rise, m12.y};
        auto& roof = out[static_cast<std::size_t>(Facade::Slate)];
        const float l = glm::distance(p0, p1) / 2.0f;
        const float w = glm::distance(p0, p3) / 2.0f;
        const glm::vec3 a{p0.x, top, p0.y}, bb{p1.x, top, p1.y}, c{p2.x, top, p2.y}, d{p3.x, top, p3.y};
        addTriangle(roof, a, bb, r1, {0, w}, {l, w}, {l, 0});
        addTriangle(roof, a, r1, r0, {0, w}, {l, 0}, {0, 0});
        addTriangle(roof, d, c, r1, {0, w}, {l, w}, {l, 0});
        addTriangle(roof, d, r1, r0, {0, w}, {l, w}, {0, 0});
        const float vt = ov;
        const float vr = ov - rise / (storey * kTile);
        addTriangle(walls, a, d, r0, {ou, vt}, {ou + w * 2.0f / (kBay * kTile), vt}, {ou + w / (kBay * kTile), vr});
        addTriangle(walls, bb, c, r1, {ou, vt}, {ou + w * 2.0f / (kBay * kTile), vt}, {ou + w / (kBay * kTile), vr});
        return;
    }

    auto& roof = out[static_cast<std::size_t>(Facade::Roof)];
    const auto base = static_cast<std::uint32_t>(roof.vertices.size());
    for (const auto p : poly)
        roof.vertices.push_back({p.x, top, p.y, 0.0f, 1.0f, 0.0f, p.x / 6.0f, p.y / 6.0f});
    for (const auto i : triangulate(poly))
        roof.indices.push_back(base + i);
    if (b.kind == "panel" && b.levels >= 5) {
        glm::vec2 lo = poly[0], hi = poly[0];
        for (const auto p : poly) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const glm::vec2 c = (lo + hi) * 0.5f;
        auto& box = out[static_cast<std::size_t>(Facade::Brick)];
        const float h = 0.45f;
        const float r = 0.35f;
        const glm::vec3 corners[] = {{c.x - r, top, c.y - r}, {c.x + r, top, c.y - r}, {c.x + r, top, c.y + r}, {c.x - r, top, c.y + r}};
        const glm::vec3 normals[] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
        for (int k = 0; k < 4; ++k) {
            const glm::vec3 p = corners[k];
            const glm::vec3 q = corners[(k + 1) % 4];
            addQuad(box, p, q, q + glm::vec3(0, h, 0), p + glm::vec3(0, h, 0), normals[k], {0.0f, 0.1f}, {0.1f, 0.1f}, {0.1f, 0.0f}, {0.0f, 0.0f});
        }
        auto& cap = out[static_cast<std::size_t>(Facade::Roof)];
        addQuad(cap, corners[0] + glm::vec3(0, h, 0), corners[1] + glm::vec3(0, h, 0), corners[2] + glm::vec3(0, h, 0),
                corners[3] + glm::vec3(0, h, 0), {0, 1, 0}, {0, 0}, {0.1f, 0}, {0.1f, 0.1f}, {0, 0.1f});
    }
}

}
