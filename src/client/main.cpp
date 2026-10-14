#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <juice/juice.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#if defined(SDL_PLATFORM_ANDROID)
#include <spdlog/sinks/android_sink.h>
#endif

#include "App.h"

namespace {

void setupLogging() {
    std::vector<spdlog::sink_ptr> sinks;
#if defined(SDL_PLATFORM_ANDROID)
    sinks.push_back(std::make_shared<spdlog::sinks::android_sink_mt>("VoidWarzone"));
#else
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif
    if (char* pref = SDL_GetPrefPath("Agenteec", "VoidWarzone")) {
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(std::string(pref) + "client.log", 2 * 1024 * 1024, 3, true));
        } catch (const std::exception&) {
        }
        SDL_free(pref);
    }
    auto logger = std::make_shared<spdlog::logger>("vw", sinks.begin(), sinks.end());
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
}

}

int main(int argc, char** argv) {
    setupLogging();
    juice_set_log_handler([](juice_log_level_t level, const char* message) {
        if (level >= JUICE_LOG_LEVEL_WARN)
            spdlog::warn("ice: {}", message);
        else
            spdlog::info("ice: {}", message);
    });
    juice_set_log_level(JUICE_LOG_LEVEL_INFO);
    vw::client::LaunchOptions options;
    for (int i = 1; i < argc; ++i)
        spdlog::info("arg {}: {}", i, argv[i]);
    if (const char* env = std::getenv("VW_MASTER"); env && *env)
        options.master = env;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--host")
            options.host = true;
        else if (arg == "--connect" && hasValue)
            options.connect = argv[++i];
        else if (arg == "--join" && hasValue)
            options.join = argv[++i];
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
        else if (arg == "--renderer" && hasValue)
            options.renderer = argv[++i];
        else if (arg == "--touch")
            options.touch = true;
        else if (arg == "--cheats")
            options.cheats = true;
        else if (arg == "--difficulty" && hasValue)
            vw::parseDifficulty(argv[++i], options.difficulty);
        else if (arg == "--windowed")
            options.windowMode = 0;
        else if (arg == "--borderless")
            options.windowMode = 1;
        else if (arg == "--fullscreen")
            options.windowMode = 2;
        else if (arg == "--mode" && hasValue)
            vw::parseGameMode(argv[++i], options.mode);
        else if (arg == "--shadows" && hasValue)
            options.shadows = vw::client::parseShadowQuality(argv[++i]);
        else if (arg == "--light" && hasValue)
            options.light = vw::client::parseLightPreset(argv[++i]);
    }

#if defined(SDL_PLATFORM_ANDROID)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        spdlog::error("SDL_Init: {}", SDL_GetError());
        return 1;
    }

#if defined(SDL_PLATFORM_ANDROID)
    constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    SDL_Window* window = SDL_CreateWindow("Void Warzone", 1280, 720, kWindowFlags);
    if (!window) {
        spdlog::error("SDL_CreateWindow: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int code = 0;
    {
        vw::client::App app;
        juice_set_log_level(JUICE_LOG_LEVEL_INFO);
        if (app.init(window, options)) {
            std::uint64_t last = SDL_GetTicksNS();
            while (app.running()) {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    app.handleEvent(e);
                const std::uint64_t now = SDL_GetTicksNS();
                app.frame(static_cast<double>(now - last) * 1e-9);
                last = now;
                if (const double budget = app.frameBudget(); budget > 0.0) {
                    const auto target = now + static_cast<std::uint64_t>(budget * 1e9);
                    const auto after = SDL_GetTicksNS();
                    if (after < target)
                        SDL_DelayPrecise(target - after);
                }
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
