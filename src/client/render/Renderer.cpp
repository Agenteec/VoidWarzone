#include "render/Renderer.h"

#include <algorithm>
#include <cstring>

#include <cmath>

#include <SDL3/SDL.h>
#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/math.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Pie.h"
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
constexpr std::uint64_t kShadowState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr std::uint32_t kShadowSamplerFlags = BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr float kShadowDepth = 60.0f;

}

bool Renderer::init(SDL_Window* window, float scale, const std::string& backend) {
    scale_ = std::max(1.0f, scale);
    bgfx::renderFrame();

    bgfx::Init init;
    if (!fillWindowHandle(window, init)) {
        spdlog::error("no native window handle");
        return false;
    }
    int pw = 0;
    int ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    pixelWidth_ = pw;
    pixelHeight_ = ph;
    width_ = std::max(1, static_cast<int>(static_cast<float>(pw) / scale_));
    height_ = std::max(1, static_cast<int>(static_cast<float>(ph) / scale_));
#if defined(SDL_PLATFORM_ANDROID)
    init.type = bgfx::RendererType::OpenGLES;
#endif
    if (backend == "vulkan")
        init.type = bgfx::RendererType::Vulkan;
    else if (backend == "d3d11")
        init.type = bgfx::RendererType::Direct3D11;
    else if (backend == "d3d12")
        init.type = bgfx::RendererType::Direct3D12;
    else if (backend == "gl")
        init.type = bgfx::RendererType::OpenGL;
    else if (backend == "gles")
        init.type = bgfx::RendererType::OpenGLES;
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
    texProgram_ = createTexProgram();
    colorSampler_ = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
    maskSampler_ = bgfx::createUniform("s_mask", bgfx::UniformType::Sampler);
    texLayout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    white_ = solidTexture(0xffffffffu);
    black_ = solidTexture(0xff000000u);

    terrainProgram_ = createTerrainProgram();
    shadowProgram_ = createShadowProgram();
    shadowTexProgram_ = createShadowTexProgram();
    terrainLayout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    const char* terrainNames[] = {"s_ground", "s_dirt", "s_cliff", "s_road"};
    for (std::size_t i = 0; i < terrainSamplers_.size(); ++i)
        terrainSamplers_[i] = bgfx::createUniform(terrainNames[i], bgfx::UniformType::Sampler);
    sunDirUniform_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    sunColorUniform_ = bgfx::createUniform("u_sunColor", bgfx::UniformType::Vec4);
    skyColorUniform_ = bgfx::createUniform("u_skyColor", bgfx::UniformType::Vec4);
    shadowParamsUniform_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
    lightMtxUniform_ = bgfx::createUniform("u_lightMtx", bgfx::UniformType::Mat4);
    shadowSampler_ = bgfx::createUniform("s_shadow", bgfx::UniformType::Sampler);
    const auto* caps = bgfx::getCaps();
    shadowsSupported_ = bgfx::isValid(shadowProgram_) && bgfx::isValid(shadowTexProgram_) &&
                        (caps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    if (!shadowsSupported_)
        spdlog::warn("shadow maps not supported on {}", name());
    setShadowSize(0);

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
    for (auto& t : textures_)
        if (bgfx::isValid(t))
            bgfx::destroy(t);
    for (auto& m : texMeshes_) {
        if (bgfx::isValid(m.vbh))
            bgfx::destroy(m.vbh);
        if (bgfx::isValid(m.ibh))
            bgfx::destroy(m.ibh);
    }
    destroyShadowMap();
    for (auto h : {texProgram_, terrainProgram_, shadowProgram_, shadowTexProgram_})
        if (bgfx::isValid(h))
            bgfx::destroy(h);
    for (auto h : {colorSampler_, maskSampler_, sunDirUniform_, sunColorUniform_, skyColorUniform_, shadowParamsUniform_, lightMtxUniform_,
                   shadowSampler_})
        if (bgfx::isValid(h))
            bgfx::destroy(h);
    for (auto h : terrainSamplers_)
        if (bgfx::isValid(h))
            bgfx::destroy(h);
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
    pixelWidth_ = width;
    pixelHeight_ = height;
    width_ = std::max(1, static_cast<int>(static_cast<float>(width) / scale_));
    height_ = std::max(1, static_cast<int>(static_cast<float>(height) / scale_));
    swapChain_.width = static_cast<std::uint32_t>(width_);
    swapChain_.height = static_cast<std::uint32_t>(height_);
    bgfx::reset(vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &swapChain_);
}

void Renderer::setVideo(bool vsync, int msaa) {
    vsync_ = vsync;
    const std::uint32_t samples = msaa >= 8 ? BGFX_SWAP_CHAIN_MSAA_X8 : msaa >= 4 ? BGFX_SWAP_CHAIN_MSAA_X4 : msaa >= 2 ? BGFX_SWAP_CHAIN_MSAA_X2 : 0;
    swapChain_.flags = (swapChain_.flags & ~BGFX_SWAP_CHAIN_MSAA_MASK) | samples;
    resize(pixelWidth_, pixelHeight_);
}

void Renderer::setScale(float scale) {
    scale_ = std::max(1.0f, scale);
    resize(pixelWidth_, pixelHeight_);
}

TextureId Renderer::createTextureRgba(int width, int height, const void* pixels) {
    const bgfx::Memory* mem = bgfx::copy(pixels, static_cast<std::uint32_t>(width * height * 4));
    textures_.push_back(bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), false, 1, bgfx::TextureFormat::RGBA8,
                                              BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem));
    return static_cast<TextureId>(textures_.size() - 1);
}

void Renderer::destroyShadowMap() {
    if (bgfx::isValid(shadowBuffer_))
        bgfx::destroy(shadowBuffer_);
    shadowBuffer_ = BGFX_INVALID_HANDLE;
    shadowTexture_ = BGFX_INVALID_HANDLE;
    shadowMapSize_ = 0;
}

void Renderer::setShadowSize(int size) {
    shadowSize_ = shadowsSupported_ ? std::clamp(size, 0, 4096) : 0;
    if (!shadowsSupported_)
        return;
    const int mapSize = shadowSize_ > 0 ? shadowSize_ : 16;
    if (mapSize == shadowMapSize_)
        return;
    destroyShadowMap();
    shadowTexture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(mapSize), static_cast<std::uint16_t>(mapSize), false, 1,
                                           bgfx::TextureFormat::D16, BGFX_TEXTURE_RT | kShadowSamplerFlags);
    shadowBuffer_ = bgfx::createFrameBuffer(1, &shadowTexture_, true);
    shadowMapSize_ = mapSize;
}

void Renderer::setLighting(const Lighting& lighting) {
    lighting_ = lighting;
    lighting_.sunDir = glm::normalize(lighting.sunDir);
    bgfx::setViewClear(kWorldView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, lighting_.clear, 1.0f, 0);
}

void Renderer::updateLight(const Camera& camera) {
    const glm::vec3 d = lighting_.sunDir;
    const glm::vec3 up = std::abs(d.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    float view[16];
    bx::mtxLookAt(view, {d.x, d.y, d.z}, {0.0f, 0.0f, 0.0f}, {up.x, up.y, up.z});

    const float radius = std::max(20.0f, camera.distance * 1.25f);
    const int size = std::max(shadowMapSize_, 1);
    const float texel = radius * 2.0f / static_cast<float>(size);
    const bx::Vec3 c = bx::mul({camera.target.x, 0.0f, camera.target.y}, view);
    const float cx = std::floor(c.x / texel) * texel;
    const float cy = std::floor(c.y / texel) * texel;
    const bool hd = homogeneousDepth();
    float proj[16];
    bx::mtxOrtho(proj, cx - radius, cx + radius, cy - radius, cy + radius, c.z - kShadowDepth, c.z + kShadowDepth, 0.0f, hd);

    bgfx::setViewFrameBuffer(kShadowView, shadowBuffer_);
    bgfx::setViewRect(kShadowView, 0, 0, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size));
    bgfx::setViewTransform(kShadowView, view, proj);
    bgfx::setViewClear(kShadowView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::touch(kShadowView);

    const auto* caps = bgfx::getCaps();
    const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
    const float sz = hd ? 0.5f : 1.0f;
    const float tz = hd ? 0.5f : 0.0f;
    const float crop[16] = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, sy, 0.0f, 0.0f, 0.0f, 0.0f, sz, 0.0f, 0.5f, 0.5f, tz, 1.0f};
    float viewProj[16];
    bx::mtxMul(viewProj, view, proj);
    bx::mtxMul(lightMtx_, viewProj, crop);
    shadowTexel_ = 1.0f / static_cast<float>(size);
}

void Renderer::bindLighting() {
    const float sunDir[4] = {lighting_.sunDir.x, lighting_.sunDir.y, lighting_.sunDir.z, shadowSize_ > 0 ? 1.0f : 0.0f};
    const float sun[4] = {lighting_.sun.x, lighting_.sun.y, lighting_.sun.z, 0.0f};
    const float sky[4] = {lighting_.sky.x, lighting_.sky.y, lighting_.sky.z, 0.0f};
    const float shadow[4] = {shadowTexel_, 0.0015f + 1.5f / static_cast<float>(std::max(shadowMapSize_, 1)), 0.0f, 0.0f};
    bgfx::setUniform(sunDirUniform_, sunDir);
    bgfx::setUniform(sunColorUniform_, sun);
    bgfx::setUniform(skyColorUniform_, sky);
    bgfx::setUniform(shadowParamsUniform_, shadow);
    bgfx::setUniform(lightMtxUniform_, lightMtx_);
    if (bgfx::isValid(shadowTexture_))
        bgfx::setTexture(4, shadowSampler_, shadowTexture_, kShadowSamplerFlags);
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
    if (shadowsSupported_)
        updateLight(camera);
    terrain_.clear();

    for (auto& b : batches_)
        b.clear();
    for (auto& [key, list] : texBatches_)
        list.clear();
    overlay_.clear();
}

TextureId Renderer::solidTexture(std::uint32_t abgr) {
    const bgfx::Memory* mem = bgfx::copy(&abgr, sizeof(abgr));
    textures_.push_back(bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, mem));
    return static_cast<TextureId>(textures_.size() - 1);
}

TextureId Renderer::createTexture(const void* data, std::uint32_t size) {
    bimg::ImageContainer* image = bimg::imageParse(&allocator_, data, size, bimg::TextureFormat::RGBA8);
    if (!image)
        return white_;
    const std::uint32_t w = image->m_width;
    const std::uint32_t h = image->m_height;
    const bool mips = w == h && w > 1 && (w & (w - 1)) == 0;
    std::uint32_t total = 0;
    for (std::uint32_t lw = w, lh = h;;) {
        total += lw * lh * 4;
        if (!mips || (lw == 1 && lh == 1))
            break;
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
    const bgfx::Memory* mem = bgfx::alloc(total);
    std::memcpy(mem->data, image->m_data, w * h * 4);
    std::uint8_t* src = mem->data;
    for (std::uint32_t lw = w, lh = h; mips && (lw > 1 || lh > 1);) {
        std::uint8_t* dst = src + lw * lh * 4;
        bimg::imageRgba8Downsample2x2(dst, lw, lh, 1, lw * 4, std::max(1u, lw / 2) * 4, src);
        src = dst;
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
    }
    bimg::imageFree(image);
    textures_.push_back(bgfx::createTexture2D(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), mips, 1, bgfx::TextureFormat::RGBA8,
                                              BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC, mem));
    return static_cast<TextureId>(textures_.size() - 1);
}

MeshId Renderer::createTerrain(const std::vector<TerrainVertex>& vertices, const std::vector<std::uint32_t>& indices) {
    GpuMesh m;
    m.vbh = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(TerrainVertex))),
                                     terrainLayout_);
    m.ibh = bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t))),
                                    BGFX_BUFFER_INDEX32);
    texMeshes_.push_back(m);
    return static_cast<MeshId>(texMeshes_.size() - 1);
}

void Renderer::drawTerrain(MeshId mesh, const std::array<TextureId, 4>& textures) {
    terrain_.push_back({mesh, textures});
}

MeshId Renderer::createMesh(const std::vector<TexVertex>& vertices, const std::vector<std::uint32_t>& indices) {
    GpuMesh m;
    m.vbh = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(TexVertex))), texLayout_);
    m.ibh = bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t))),
                                    BGFX_BUFFER_INDEX32);
    texMeshes_.push_back(m);
    return static_cast<MeshId>(texMeshes_.size() - 1);
}

void Renderer::drawMesh(MeshId mesh, TextureId texture, TextureId mask, const float* mtx, const glm::vec4& color, std::uint8_t flags) {
    Instance inst;
    std::memcpy(inst.mtx, mtx, sizeof(inst.mtx));
    std::memcpy(inst.color, &color.x, sizeof(inst.color));
    texBatches_[TexBatchKey{mesh, texture, mask, flags}].push_back(inst);
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
    if (shadowSize_ > 0)
        submitShadows();
    if (bgfx::isValid(terrainProgram_)) {
        Instance identity;
        bx::mtxIdentity(identity.mtx);
        std::fill(std::begin(identity.color), std::end(identity.color), 1.0f);
        const std::vector<Instance> one{identity};
        for (const auto& t : terrain_) {
            if (t.mesh >= texMeshes_.size())
                continue;
            const auto& gpu = texMeshes_[t.mesh];
            submitInstances(kWorldView, terrainProgram_, kOpaqueState, one, [&] {
                bgfx::setVertexBuffer(0, gpu.vbh);
                bgfx::setIndexBuffer(gpu.ibh);
                for (std::size_t i = 0; i < t.textures.size(); ++i)
                    bgfx::setTexture(static_cast<std::uint8_t>(i), terrainSamplers_[i], textures_[t.textures[i]]);
                bindLighting();
            });
        }
    }
    submitTextured(false);
    for (const std::uint8_t flags : {std::uint8_t{Lit}, std::uint8_t{Unlit}, std::uint8_t{Alpha}, std::uint8_t{Alpha | Unlit}})
        for (std::size_t m = 0; m < meshes_.size(); ++m)
            submit(kWorldView, static_cast<Mesh>(m), flags, batches_[m * kFlagVariants + flags]);
    submitTextured(true);
    submit(kOverlayView, Mesh::Cube, Alpha | Unlit, overlay_);
    bgfx::frame();
}

void Renderer::submitShadows() {
    Instance identity;
    bx::mtxIdentity(identity.mtx);
    const std::vector<Instance> one{identity};
    for (const auto& t : terrain_) {
        if (t.mesh >= texMeshes_.size())
            continue;
        const auto& gpu = texMeshes_[t.mesh];
        submitInstances(kShadowView, shadowProgram_, kShadowState, one, [&] {
            bgfx::setVertexBuffer(0, gpu.vbh);
            bgfx::setIndexBuffer(gpu.ibh);
        });
    }
    for (const auto& [key, instances] : texBatches_) {
        if (instances.empty() || (key.flags & (Alpha | Unlit | NoShadow)) || key.mesh >= texMeshes_.size())
            continue;
        const auto& gpu = texMeshes_[key.mesh];
        const auto texture = textures_[key.texture];
        submitInstances(kShadowView, shadowTexProgram_, kShadowState, instances, [&] {
            bgfx::setVertexBuffer(0, gpu.vbh);
            bgfx::setIndexBuffer(gpu.ibh);
            bgfx::setTexture(0, colorSampler_, texture);
        });
    }
    for (std::size_t m = 0; m < meshes_.size(); ++m) {
        const auto& gpu = meshes_[m];
        submitInstances(kShadowView, shadowProgram_, kShadowState, batches_[m * kFlagVariants + Lit], [&] {
            bgfx::setVertexBuffer(0, gpu.vbh);
            bgfx::setIndexBuffer(gpu.ibh);
        });
    }
}

void Renderer::submitTextured(bool alpha) {
    for (const auto& [key, instances] : texBatches_) {
        if (instances.empty() || ((key.flags & Alpha) != 0) != alpha || key.mesh >= texMeshes_.size())
            continue;
        const auto& gpu = texMeshes_[key.mesh];
        const float params[4] = {(key.flags & Unlit) ? 1.0f : 0.0f, (key.flags & Tint) ? 0.35f : 0.0f, 0.0f, 0.0f};
        const auto texture = textures_[key.texture];
        const auto mask = textures_[key.mask];
        submitInstances(kWorldView, texProgram_, alpha ? kAlphaState : kOpaqueState, instances, [&] {
            bgfx::setVertexBuffer(0, gpu.vbh);
            bgfx::setIndexBuffer(gpu.ibh);
            bgfx::setTexture(0, colorSampler_, texture);
            bgfx::setTexture(1, maskSampler_, mask);
            bgfx::setUniform(params_, params);
            bindLighting();
        });
    }
}

void Renderer::submitInstances(bgfx::ViewId view, bgfx::ProgramHandle program, std::uint64_t state, const std::vector<Instance>& instances,
                               const std::function<void()>& bind) {
    std::size_t offset = 0;
    while (offset < instances.size()) {
        const auto wanted = static_cast<std::uint32_t>(instances.size() - offset);
        const std::uint32_t count = bgfx::getAvailInstanceDataBuffer(wanted, sizeof(Instance));
        if (count == 0)
            break;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, count, sizeof(Instance));
        std::memcpy(idb.data, instances.data() + offset, count * sizeof(Instance));
        bind();
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::setState(state);
        bgfx::submit(view, program);
        offset += count;
    }
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
        if (view == kWorldView)
            bindLighting();
        bgfx::setState(state);
        bgfx::submit(view, program_);
        offset += count;
    }
}

}
