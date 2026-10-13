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

int simulate(const vw::GameData& data, const vw::Map& map, vw::Difficulty difficulty, int players) {
    vw::World world(data, map, difficulty);
    for (int p = 0; p < players; ++p)
        world.spawnPlayer(static_cast<std::uint8_t>(p));
    spdlog::info("simulating {} | {} | {} idle players", map.name(), vw::difficultyName(difficulty), players);

    const std::uint64_t limit = static_cast<std::uint64_t>(vw::kTickRate) * 60 * 20;
    while (world.defense().state == vw::MatchState::Playing && world.tick() < limit) {
        world.step();
        for (const auto& ev : world.events()) {
            if (ev.type != vw::EventType::WaveStart && ev.type != vw::EventType::PointCaptured && ev.type != vw::EventType::Enrage)
                continue;
            int teams[2] = {0, 0};
            for (auto [e, team] : world.registry().view<const vw::Team>(entt::exclude<vw::Structure>).each())
                ++teams[team.id & 1];
            const char* what = ev.type == vw::EventType::WaveStart ? "wave" : ev.type == vw::EventType::Enrage ? "boss enraged" : "point captured";
            spdlog::info("{:6.1f}s {} {} | units {}/{} | energy {:.0f}", static_cast<double>(world.tick()) / vw::kTickRate, what,
                         static_cast<int>(ev.value), teams[0], teams[1], world.player(0).energy);
        }
    }

    const auto& d = world.defense();
    const char* result = d.state == vw::MatchState::Victory ? "VICTORY" : d.state == vw::MatchState::Defeat ? "DEFEAT" : "TIMEOUT";
    int structures[2] = {0, 0};
    for (auto [e, team] : world.registry().view<const vw::Team, const vw::Structure>().each())
        ++structures[team.id & 1];
    spdlog::info("{} at {:.1f}s, wave {}/{} | structures {}/{}", result, static_cast<double>(world.tick()) / vw::kTickRate, d.wave, d.totalWaves,
                 structures[0], structures[1]);
    return d.state == vw::MatchState::Defeat ? 2 : 0;
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
    int simPlayers = -1;
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
        else if (arg == "--difficulty" && hasValue) {
            if (!vw::parseDifficulty(argv[++i], config.difficulty)) {
                spdlog::error("unknown difficulty {}", argv[i]);
                return 1;
            }
        } else if (arg == "--simulate")
            simPlayers = hasValue && argv[i + 1][0] != '-' ? std::atoi(argv[++i]) : 0;
    }

    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    vw::net::NetLibrary net;
    if (!net.ok())
        return 1;

    try {
        const auto data = vw::GameData::parse(readFile(dataDir / "units.json"));
        const auto map = vw::Map::parse(readFile(dataDir / "maps" / (config.mapName + ".json")));
        if (simPlayers >= 0)
            return simulate(data, map, config.difficulty, simPlayers);
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
