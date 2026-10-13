#include "render/Renderer.h"

#include <algorithm>
#include <cstring>

#include <SDL3/SDL.h>
#include <bx/math.h>
#include <glm/common.hpp>
#include <spdlog/spdlog.h>

#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Shaders.h"

namespace vw::client {

namespace {

bool fillWindowHandle(SDL_Window* window, bgfx::Init& init) {
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    bgfx::SwapChain& sc = init.swapChain;
#if defined(SDL_PLATFORM_WINDOWS)
    sc.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    sc.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        sc.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        sc.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        sc.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        sc.nwh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    }
#endif
    return sc.nwh != nullptr;
}

constexpr std::uint64_t kOpaqueState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
constexpr std::uint64_t kAlphaState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | BGFX_STATE_BLEND_ALPHA;
constexpr std::uint64_t kOverlayState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

}

bool Renderer::init(SDL_Window* window) {
    bgfx::renderFrame();

    bgfx::Init init;
    if (!fillWindowHandle(window, init)) {
        spdlog::error("no native window handle");
        return false;
    }
    SDL_GetWindowSizeInPixels(window, &width_, &height_);
    init.swapChain.width = static_cast<std::uint32_t>(width_);
    init.swapChain.height = static_cast<std::uint32_t>(height_);
    init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
    init.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        spdlog::error("bgfx::init failed");
        return false;
    }
    swapChain_ = init.swapChain;

    program_ = createMeshProgram();
    if (!bgfx::isValid(program_)) {
        spdlog::error("no shaders for {}", name());
        return false;
    }
    params_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();

    const MeshData data[] = {makeCube(), makeCylinder(20), makeSphere(16, 10)};
    for (std::size_t i = 0; i < meshes_.size(); ++i) {
        const auto& d = data[i];
        meshes_[i].vbh = bgfx::createVertexBuffer(
            bgfx::copy(d.vertices.data(), static_cast<std::uint32_t>(d.vertices.size() * sizeof(Vertex))), layout);
        meshes_[i].ibh = bgfx::createIndexBuffer(
            bgfx::copy(d.indices.data(), static_cast<std::uint32_t>(d.indices.size() * sizeof(std::uint16_t))));
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(kWorldView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x07070dff, 1.0f, 0);
    bgfx::setViewMode(kWorldView, bgfx::ViewMode::Sequential);
    bgfx::setViewMode(kOverlayView, bgfx::ViewMode::Sequential);
    return true;
}

void Renderer::shutdown() {
    for (auto& m : meshes_) {
        if (bgfx::isValid(m.vbh))
            bgfx::destroy(m.vbh);
        if (bgfx::isValid(m.ibh))
            bgfx::destroy(m.ibh);
    }
    if (bgfx::isValid(params_))
        bgfx::destroy(params_);
    if (bgfx::isValid(program_))
        bgfx::destroy(program_);
    bgfx::shutdown();
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    swapChain_.width = static_cast<std::uint32_t>(width);
    swapChain_.height = static_cast<std::uint32_t>(height);
    bgfx::reset(BGFX_RESET_VSYNC, &swapChain_);
}

bool Renderer::homogeneousDepth() const {
    return bgfx::getCaps()->homogeneousDepth;
}

const char* Renderer::name() const {
    return bgfx::getRendererName(bgfx::getRendererType());
}

void Renderer::begin(const Camera& camera) {
    const auto w = static_cast<std::uint16_t>(width_);
    const auto h = static_cast<std::uint16_t>(height_);
    bgfx::setViewRect(kWorldView, 0, 0, w, h);
    bgfx::setViewRect(kOverlayView, 0, 0, w, h);
    bgfx::setViewTransform(kWorldView, camera.view(), camera.proj());

    float ortho[16];
    bx::mtxOrtho(ortho, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, -1.0f, 1.0f, 0.0f, homogeneousDepth());
    bgfx::setViewTransform(kOverlayView, nullptr, ortho);
    bgfx::touch(kWorldView);

    for (auto& b : batches_)
        b.clear();
    overlay_.clear();
}

void Renderer::draw(Mesh mesh, glm::vec3 center, glm::vec3 size, float yaw, const glm::vec4& color, std::uint8_t flags) {
    Instance inst;
    bx::mtxSRT(inst.mtx, size.x, size.y, size.z, 0.0f, yaw, 0.0f, center.x, center.y, center.z);
    std::memcpy(inst.color, &color.x, sizeof(inst.color));
    batches_[static_cast<std::size_t>(mesh) * kFlagVariants + (flags & 3)].push_back(inst);
}

void Renderer::drawScreenRect(glm::vec2 a, glm::vec2 b, const glm::vec4& color) {
    const glm::vec2 lo = glm::min(a, b);
    const glm::vec2 hi = glm::max(a, b);
    Instance inst;
    bx::mtxSRT(inst.mtx, hi.x - lo.x, hi.y - lo.y, 0.01f, 0.0f, 0.0f, 0.0f, (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f);
    std::memcpy(inst.color, &color.x, sizeof(inst.color));
    overlay_.push_back(inst);
}

void Renderer::end() {
    for (const std::uint8_t flags : {std::uint8_t{Lit}, std::uint8_t{Unlit}, std::uint8_t{Alpha}, std::uint8_t{Alpha | Unlit}})
        for (std::size_t m = 0; m < meshes_.size(); ++m)
            submit(kWorldView, static_cast<Mesh>(m), flags, batches_[m * kFlagVariants + flags]);
    submit(kOverlayView, Mesh::Cube, Alpha | Unlit, overlay_);
    bgfx::frame();
}

void Renderer::submit(bgfx::ViewId view, Mesh mesh, std::uint8_t flags, const std::vector<Instance>& instances) {
    const auto& gpu = meshes_[static_cast<std::size_t>(mesh)];
    const std::uint64_t state = view == kOverlayView ? kOverlayState : (flags & Alpha) ? kAlphaState : kOpaqueState;
    const float params[4] = {(flags & Unlit) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

    std::size_t offset = 0;
    while (offset < instances.size()) {
        const auto wanted = static_cast<std::uint32_t>(instances.size() - offset);
        const std::uint32_t count = bgfx::getAvailInstanceDataBuffer(wanted, sizeof(Instance));
        if (count == 0)
            break;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, count, sizeof(Instance));
        std::memcpy(idb.data, instances.data() + offset, count * sizeof(Instance));
        bgfx::setVertexBuffer(0, gpu.vbh);
        bgfx::setIndexBuffer(gpu.ibh);
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::setUniform(params_, params);
        bgfx::setState(state);
        bgfx::submit(view, program_);
        offset += count;
    }
}

}
