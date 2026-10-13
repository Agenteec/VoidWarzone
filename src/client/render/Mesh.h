#pragma once

#include <cstdint>
#include <vector>

namespace vw::client {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

MeshData makeCube();
MeshData makeCylinder(int segments);
MeshData makeSphere(int slices, int stacks);

}
