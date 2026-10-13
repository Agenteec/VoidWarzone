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
#include "core/World.h"
#include "net/NetLibrary.h"
#include "net/Protocol.h"

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

}

int main(int argc, char** argv) {
    std::uint64_t maxTicks = 0;
    bool fast = false;
    std::filesystem::path dataDir = std::filesystem::path(argv[0]).parent_path() / "data";
    std::string mapName = "siege_test";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--ticks" && i + 1 < argc)
            maxTicks = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--data" && i + 1 < argc)
            dataDir = argv[++i];
        else if (arg == "--map" && i + 1 < argc)
            mapName = argv[++i];
        else if (arg == "--fast")
            fast = true;
    }

    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    vw::net::NetLibrary net;
    if (!net.ok())
        return 1;

    try {
        const auto data = vw::GameData::parse(readFile(dataDir / "units.json"));
        const auto map = vw::Map::parse(readFile(dataDir / "maps" / (mapName + ".json")));
        vw::World world(data, map);

        spdlog::info("Void Warzone server {} | port {} | {} Hz | map {}", vw::kVersion, vw::net::kGamePort, vw::kTickRate, map.name());

        using Clock = std::chrono::steady_clock;
        constexpr auto tickDuration = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(vw::kTickDt));
        auto next = Clock::now();
        while (g_running && (maxTicks == 0 || world.tick() < maxTicks)) {
            world.step();
            if (world.tick() % (vw::kTickRate * 10) == 0) {
                int teams[2] = {0, 0};
                for (auto [e, team] : world.registry().view<const vw::Team>().each())
                    ++teams[team.id & 1];
                spdlog::info("tick {} | units {}/{} | projectiles {}", world.tick(), teams[0], teams[1],
                             world.registry().view<vw::Projectile>().size());
            }
            if (!fast) {
                next += tickDuration;
                std::this_thread::sleep_until(next);
            }
        }
        spdlog::info("stopped at tick {}", world.tick());
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return 1;
    }
    return 0;
}
