#include <cstdint>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/NetLibrary.h"

namespace {

bool fillWindowHandle(SDL_Window* window, bgfx::Init& init) {
    bgfx::SwapChain& sc = init.swapChain;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
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
        sc.nwh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
            SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    }
#endif
    return sc.nwh != nullptr;
}

}

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        spdlog::error("SDL_Init: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Void Warzone", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        spdlog::error("SDL_CreateWindow: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    vw::net::NetLibrary net;

    bgfx::renderFrame();

    bgfx::Init init;
    if (!fillWindowHandle(window, init)) {
        spdlog::error("no native window handle");
        return 1;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    init.swapChain.width = static_cast<std::uint32_t>(width);
    init.swapChain.height = static_cast<std::uint32_t>(height);
    init.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) {
        spdlog::error("bgfx::init failed");
        return 1;
    }

    spdlog::info("Void Warzone {} | {}", vw::kVersion, bgfx::getRendererName(bgfx::getRendererType()));

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x0b0b14ff, 1.0f, 0);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                width = e.window.data1;
                height = e.window.data2;
                init.swapChain.width = static_cast<std::uint32_t>(width);
                init.swapChain.height = static_cast<std::uint32_t>(height);
                bgfx::reset(BGFX_RESET_VSYNC, &init.swapChain);
                break;
            default:
                break;
            }
        }

        bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
        bgfx::touch(0);
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(1, 1, 0x0f, "Void Warzone %.*s", static_cast<int>(vw::kVersion.size()), vw::kVersion.data());
        bgfx::frame();
    }

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
