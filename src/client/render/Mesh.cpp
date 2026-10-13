#include "render/Mesh.h"

#include <cmath>
#include <numbers>

namespace vw::client {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

void quad(MeshData& m, const float n[3], const float c[4][3]) {
    const auto base = static_cast<std::uint16_t>(m.vertices.size());
    for (int i = 0; i < 4; ++i)
        m.vertices.push_back({c[i][0], c[i][1], c[i][2], n[0], n[1], n[2]});
    constexpr std::uint16_t order[] = {0, 1, 2, 0, 2, 3};
    for (const std::uint16_t i : order)
        m.indices.push_back(static_cast<std::uint16_t>(base + i));
}

}

MeshData makeCube() {
    MeshData m;
    const float h = 0.5f;
    const float faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& n : faces) {
        const float u[3] = {n[1] != 0 ? 1.0f : (n[2] != 0 ? 1.0f : 0.0f), 0.0f, n[0] != 0 ? 1.0f : 0.0f};
        const float v[3] = {n[1] != 0 ? 0.0f : 0.0f, n[1] != 0 ? 0.0f : 1.0f, n[1] != 0 ? 1.0f : 0.0f};
        float c[4][3];
        const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 3; ++k)
                c[i][k] = (n[k] + u[k] * s[i][0] + v[k] * s[i][1]) * h;
        quad(m, n, c);
    }
    return m;
}

MeshData makeCylinder(int segments) {
    MeshData m;
    const float r = 0.5f;
    const float h = 0.5f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float am = (a0 + a1) * 0.5f;
        const float n[3] = {std::cos(am), 0.0f, std::sin(am)};
        const float side[4][3] = {
            {std::cos(a0) * r, -h, std::sin(a0) * r},
            {std::cos(a1) * r, -h, std::sin(a1) * r},
            {std::cos(a1) * r, h, std::sin(a1) * r},
            {std::cos(a0) * r, h, std::sin(a0) * r},
        };
        quad(m, n, side);
    }
    for (const float y : {h, -h}) {
        const auto center = static_cast<std::uint16_t>(m.vertices.size());
        const float ny = y > 0 ? 1.0f : -1.0f;
        m.vertices.push_back({0.0f, y, 0.0f, 0.0f, ny, 0.0f});
        for (int i = 0; i <= segments; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            m.vertices.push_back({std::cos(a) * r, y, std::sin(a) * r, 0.0f, ny, 0.0f});
        }
        for (int i = 0; i < segments; ++i) {
            m.indices.push_back(center);
            m.indices.push_back(static_cast<std::uint16_t>(center + 1 + i));
            m.indices.push_back(static_cast<std::uint16_t>(center + 2 + i));
        }
    }
    return m;
}

MeshData makeSphere(int slices, int stacks) {
    MeshData m;
    for (int j = 0; j <= stacks; ++j) {
        const float v = kPi * static_cast<float>(j) / static_cast<float>(stacks);
        for (int i = 0; i <= slices; ++i) {
            const float u = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(slices);
            const float nx = std::sin(v) * std::cos(u);
            const float ny = std::cos(v);
            const float nz = std::sin(v) * std::sin(u);
            m.vertices.push_back({nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz});
        }
    }
    for (int j = 0; j < stacks; ++j)
        for (int i = 0; i < slices; ++i) {
            const auto a = static_cast<std::uint16_t>(j * (slices + 1) + i);
            const auto b = static_cast<std::uint16_t>(a + slices + 1);
            for (const std::uint16_t idx : {a, b, static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(a + 1), b,
                                            static_cast<std::uint16_t>(b + 1)})
                m.indices.push_back(idx);
        }
    return m;
}

}
