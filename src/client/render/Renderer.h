#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include <bx/allocator.h>

#include <functional>

#include <bgfx/bgfx.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct SDL_Window;

namespace vw::client {

class Camera;
struct TexVertex;

using TextureId = std::uint16_t;
using MeshId = std::uint16_t;

enum class Mesh : std::uint8_t { Cube, Cylinder, Sphere, Count };

enum DrawFlags : std::uint8_t {
    Lit = 0,
    Unlit = 1,
    Alpha = 2,
    Tint = 4,
};

class Renderer {
public:
    bool init(SDL_Window* window, float scale = 1.0f);
    void shutdown();
    void resize(int width, int height);

    float scale() const { return scale_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool homogeneousDepth() const;
    const char* name() const;

    void begin(const Camera& camera);
    void draw(Mesh mesh, glm::vec3 center, glm::vec3 size, float yaw, const glm::vec4& color, std::uint8_t flags = Lit);
    void drawScreenRect(glm::vec2 a, glm::vec2 b, const glm::vec4& color);
    void drawMesh(MeshId mesh, TextureId texture, TextureId mask, const float* mtx, const glm::vec4& color, std::uint8_t flags = Lit);
    void end();

    TextureId createTexture(const void* data, std::uint32_t size);
    MeshId createMesh(const std::vector<TexVertex>& vertices, const std::vector<std::uint32_t>& indices);
    TextureId whiteTexture() const { return white_; }
    TextureId blackTexture() const { return black_; }

private:
    struct Instance {
        float mtx[16];
        float color[4];
    };
    static_assert(sizeof(Instance) == 80);

    struct GpuMesh {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    };

    static constexpr int kFlagVariants = 4;
    static constexpr bgfx::ViewId kWorldView = 0;
    static constexpr bgfx::ViewId kOverlayView = 1;

    struct TexBatchKey {
        MeshId mesh;
        TextureId texture;
        TextureId mask;
        std::uint8_t flags;
        auto operator<=>(const TexBatchKey&) const = default;
    };

    void submit(bgfx::ViewId view, Mesh mesh, std::uint8_t flags, const std::vector<Instance>& instances);
    void submitTextured(bool alpha);
    void submitInstances(bgfx::ViewId view, bgfx::ProgramHandle program, std::uint64_t state, const std::vector<Instance>& instances,
                         const std::function<void()>& bind);
    TextureId solidTexture(std::uint32_t rgba);

    int width_ = 0;
    int height_ = 0;
    float scale_ = 1.0f;
    bgfx::SwapChain swapChain_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_ = BGFX_INVALID_HANDLE;
    std::array<GpuMesh, static_cast<std::size_t>(Mesh::Count)> meshes_;
    std::array<std::vector<Instance>, static_cast<std::size_t>(Mesh::Count) * kFlagVariants> batches_;
    std::vector<Instance> overlay_;

    bx::DefaultAllocator allocator_;
    bgfx::ProgramHandle texProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle maskSampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout texLayout_;
    std::vector<bgfx::TextureHandle> textures_;
    std::vector<GpuMesh> texMeshes_;
    std::map<TexBatchKey, std::vector<Instance>> texBatches_;
    TextureId white_ = 0;
    TextureId black_ = 0;
};

}
