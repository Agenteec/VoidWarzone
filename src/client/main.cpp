#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include "App.h"

int main(int argc, char** argv) {
    vw::client::LaunchOptions options;
    if (const char* env = std::getenv("VW_MASTER"); env && *env)
        options.master = env;
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
        else if (arg == "--master" && hasValue)
            options.master = argv[++i];
        else if (arg == "--ui-scale" && hasValue)
            options.uiScale = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--camera" && hasValue) {
            float x = 0.0f;
            float y = 0.0f;
            if (std::sscanf(argv[++i], "%f,%f", &x, &y) == 2)
                options.camera = glm::vec2{x, y};
        } else if (arg == "--zoom" && hasValue)
            options.zoom = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--touch")
            options.touch = true;
        else if (arg == "--cheats")
            options.cheats = true;
        else if (arg == "--difficulty" && hasValue)
            vw::parseDifficulty(argv[++i], options.difficulty);
    }

#if defined(SDL_PLATFORM_ANDROID)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
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
