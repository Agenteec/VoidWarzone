#include <cstdint>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include "App.h"

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

    int code = 0;
    {
        vw::client::App app;
        if (app.init(window)) {
            std::uint64_t last = SDL_GetTicksNS();
            while (app.running()) {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    app.handleEvent(e);
                const std::uint64_t now = SDL_GetTicksNS();
                app.frame(static_cast<double>(now - last) * 1e-9);
                last = now;
            }
        } else {
            code = 1;
        }
        app.shutdown();
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
