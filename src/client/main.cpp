#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include "App.h"

int main(int argc, char** argv) {
    vw::client::LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--host")
            options.host = true;
        else if (arg == "--connect" && hasValue)
            options.connect = argv[++i];
        else if (arg == "--name" && hasValue)
            options.name = argv[++i];
        else if (arg == "--map" && hasValue)
            options.map = argv[++i];
        else if (arg == "--port" && hasValue)
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    }

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
        if (app.init(window, options)) {
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
