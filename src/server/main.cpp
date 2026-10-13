#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/NetLibrary.h"
#include "server/Server.h"

namespace {

std::atomic_bool g_running{true};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path.string());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}

int main(int argc, char** argv) {
    std::filesystem::path dataDir = std::filesystem::path(argv[0]).parent_path() / "data";
    vw::server::ServerConfig config;
    double runFor = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--data" && hasValue)
            dataDir = argv[++i];
        else if (arg == "--map" && hasValue)
            config.mapName = argv[++i];
        else if (arg == "--port" && hasValue)
            config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--name" && hasValue)
            config.name = argv[++i];
        else if (arg == "--max-players" && hasValue)
            config.maxPlayers = std::atoi(argv[++i]);
        else if (arg == "--seconds" && hasValue)
            runFor = std::atof(argv[++i]);
    }

    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    vw::net::NetLibrary net;
    if (!net.ok())
        return 1;

    try {
        const auto data = vw::GameData::parse(readFile(dataDir / "units.json"));
        const auto map = vw::Map::parse(readFile(dataDir / "maps" / (config.mapName + ".json")));
        vw::server::Server server(data, map, config);
        const double start = nowSeconds();
        if (!server.start(start))
            return 1;
        spdlog::info("Void Warzone dedicated server {}", vw::kVersion);

        while (g_running && (runFor <= 0.0 || nowSeconds() - start < runFor)) {
            server.update(nowSeconds());
            const double wait = server.nextTickTime() - nowSeconds();
            if (wait > 0.0)
                std::this_thread::sleep_for(std::chrono::duration<double>(std::min(wait, 0.005)));
        }
        server.stop();
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return 1;
    }
    return 0;
}
