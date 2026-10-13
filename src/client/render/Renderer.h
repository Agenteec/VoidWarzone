#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct SDL_Window;

namespace vw::client {

class Camera;

enum class Mesh : std::uint8_t { Cube, Cylinder, Sphere, Count };

enum DrawFlags : std::uint8_t {
    Lit = 0,
    Unlit = 1,
    Alpha = 2,
};

class Renderer {
public:
    bool init(SDL_Window* window);
    void shutdown();
    void resize(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    bool homogeneousDepth() const;
    const char* name() const;

    void begin(const Camera& camera);
    void draw(Mesh mesh, glm::vec3 center, glm::vec3 size, float yaw, const glm::vec4& color, std::uint8_t flags = Lit);
    void drawScreenRect(glm::vec2 a, glm::vec2 b, const glm::vec4& color);
    void end();

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

    void submit(bgfx::ViewId view, Mesh mesh, std::uint8_t flags, const std::vector<Instance>& instances);

    int width_ = 0;
    int height_ = 0;
    bgfx::SwapChain swapChain_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_ = BGFX_INVALID_HANDLE;
    std::array<GpuMesh, static_cast<std::size_t>(Mesh::Count)> meshes_;
    std::array<std::vector<Instance>, static_cast<std::size_t>(Mesh::Count) * kFlagVariants> batches_;
    std::vector<Instance> overlay_;
};

}
