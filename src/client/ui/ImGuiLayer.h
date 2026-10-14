#pragma once

#include <functional>
#include <string>

#include <SDL3/SDL_events.h>
#include <bgfx/bgfx.h>

struct ImFont;
struct ImDrawData;

namespace vw::client {

class ImGuiLayer {
public:
    using Loader = std::function<std::string(const std::string&)>;

    bool init(SDL_Window* window, const Loader& load);
    void shutdown();

    void processEvent(const SDL_Event& e, float pixelDensity, int width, int height);
    void beginFrame(float dt, int width, int height, float scale);
    void endFrame(bgfx::ViewId view);

    bool wantsMouse() const;
    bool wantsText() const;

    ImFont* regular() const { return regular_; }
    ImFont* bold() const { return bold_; }

private:
    void render(ImDrawData* data, bgfx::ViewId view);
    void applyStyle(float scale);

    SDL_Window* window_ = nullptr;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    ImFont* regular_ = nullptr;
    ImFont* bold_ = nullptr;
    std::string boldData_;
    bool textInput_ = false;
    float styleScale_ = 0.0f;
    bool initialized_ = false;
};

}
