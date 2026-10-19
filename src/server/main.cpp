#include <map>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <juice/juice.h>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/NetLibrary.h"
#include "net/MasterClient.h"
#include "core/MapGen.h"
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

int simulate(const vw::GameData& data, const vw::Map& map, vw::Difficulty difficulty, vw::GameMode mode, int players, bool fog) {
    vw::World world(data, map, difficulty, mode);
    world.setFog(fog);
    const bool skirmish = mode == vw::GameMode::Skirmish;
    for (int p = 0; p < players; ++p) {
        world.spawnPlayer(static_cast<std::uint8_t>(p), skirmish ? static_cast<std::uint8_t>(vw::kFirstSkirmishTeam + p) : std::uint8_t{0});
        if (skirmish)
            world.setAi(static_cast<std::uint8_t>(p), true);
    }
    spdlog::info("simulating {} | {} | {} {} players", map.name(), vw::difficultyName(difficulty), players, skirmish ? "AI" : "idle");

    const std::uint64_t limit = static_cast<std::uint64_t>(vw::kTickRate) * 60 * 20;
    while (world.defense().state == vw::MatchState::Playing && world.tick() < limit) {
        world.step();
        if (skirmish && world.tick() % (vw::kTickRate * 120) == 0) {
            std::string line;
            for (int p = 0; p < players; ++p) {
                int units = 0, structures = 0, building = 0;
                for (auto [e, owner, unit] : world.registry().view<const vw::Owner, const vw::Unit>().each())
                    if (owner.player == p) {
                        (world.registry().all_of<vw::Structure>(e) ? structures : units)++;
                        if (const auto* o = world.registry().try_get<vw::Order>(e); o && o->type == vw::OrderType::Build)
                            ++building;
                    }
                line += fmt::format(" p{} {:.0f}e {}s {}u {}b |", p, world.player(static_cast<std::uint8_t>(p)).energy, structures, units, building);
            }
            spdlog::info("{:6.0f}s{}", static_cast<double>(world.tick()) / vw::kTickRate, line);
        }
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

    const auto& t = world.systemTimes();
    double total = 0.0;
    for (std::size_t i = 0; i < vw::SystemTimes::kCount; ++i)
        total += t.seconds[i];
    std::string line;
    for (std::size_t i = 0; i < vw::SystemTimes::kCount; ++i)
        if (t.seconds[i] / total > 0.01)
            line += fmt::format(" {} {:.0f}%", vw::SystemTimes::name(i), t.seconds[i] / total * 100.0);
    spdlog::info("tick avg {:.3f} ms, peak {:.2f} ms |{} | paths {} (worst {:.2f} ms) fields {} (worst {:.2f} ms) nav {} work {}",
                 total / static_cast<double>(t.steps) * 1000.0, t.peak * 1000.0, line, t.paths, t.worstPath * 1000.0, t.fields,
                 t.worstField * 1000.0, t.navRebuilds, t.worstWork);

    const auto& d = world.defense();
    const char* result = d.state == vw::MatchState::Victory ? "VICTORY" : d.state == vw::MatchState::Defeat ? "DEFEAT" : "TIMEOUT";
    int structures[2] = {0, 0};
    for (auto [e, team] : world.registry().view<const vw::Team, const vw::Structure>().each())
        ++structures[team.id & 1];
    spdlog::info("{} at {:.1f}s, wave {}/{} | structures {}/{}", result, static_cast<double>(world.tick()) / vw::kTickRate, d.wave, d.totalWaves,
                 structures[0], structures[1]);
    if (skirmish) {
        std::map<int, std::pair<int, int>> perPlayer;
        std::map<int, std::string> kinds;
        for (auto [e, unit, owner] : world.registry().view<const vw::Unit, const vw::Owner, const vw::Structure>().each())
            kinds[owner.player] += data.unit(unit.def).id + " ";
        for (auto [e, unit, owner] : world.registry().view<const vw::Unit, const vw::Owner>().each())
            (world.registry().all_of<vw::Structure>(e) ? perPlayer[owner.player].first : perPlayer[owner.player].second)++;
        for (const auto& [p, c] : perPlayer)
            spdlog::info("  player {} (team {}): {} structures, {} units, energy {:.0f}", p, world.player(static_cast<std::uint8_t>(p)).team, c.first, c.second,
                         world.player(static_cast<std::uint8_t>(p)).energy);
        for (const auto& [p, k] : kinds)
            spdlog::info("  player {}: {}", p, k);
        spdlog::info("  winner team {}", d.winner);
    }
    return d.state == vw::MatchState::Defeat ? 2 : 0;
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}

int main(int argc, char** argv) {
    std::string exportPath;
    std::filesystem::path dataDir = std::filesystem::path(argv[0]).parent_path() / "data";
    vw::server::ServerConfig config;
    bool fog = true;
    config.lobby = true;
    double runFor = 0.0;
    std::string master;
    int simPlayers = -1;
    bool iceLog = false;
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
        else if (arg == "--master" && hasValue)
            master = argv[++i];
        else if (arg == "--cheats")
            config.cheats = true;
        else if (arg == "--no-fog")
            fog = false;
        else if (arg == "--seconds" && hasValue)
            runFor = std::atof(argv[++i]);
        else if (arg == "--difficulty" && hasValue) {
            if (!vw::parseDifficulty(argv[++i], config.difficulty)) {
                spdlog::error("unknown difficulty {}", argv[i]);
                return 1;
            }
        } else if (arg == "--mode" && hasValue) {
            if (!vw::parseGameMode(argv[++i], config.mode)) {
                spdlog::error("unknown mode {}", argv[i]);
                return 1;
            }
        } else if (arg == "--ice-log") {
            iceLog = true;
        } else if (arg == "--no-lobby") {
            config.lobby = false;
        } else if (arg == "--export-map" && hasValue) {
            exportPath = argv[++i];
        } else if (arg == "--simulate")
            simPlayers = hasValue && argv[i + 1][0] != '-' ? std::atoi(argv[++i]) : 0;
    }

    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    vw::net::NetLibrary net;
    if (!net.ok())
        return 1;
    if (iceLog) {
        juice_set_log_handler([](juice_log_level_t level, const char* message) {
            if (level >= JUICE_LOG_LEVEL_WARN)
                spdlog::warn("ice: {}", message);
            else
                spdlog::info("ice: {}", message);
        });
        juice_set_log_level(JUICE_LOG_LEVEL_INFO);
    }

    try {
        const auto data = vw::GameData::parse(readFile(dataDir / "units.json"), readFile(dataDir / "skills.json"), readFile(dataDir / "items.json"), readFile(dataDir / "research.json"));
        const auto generated = vw::generatedMap(config.mapName);
        if (generated && !exportPath.empty()) {
            std::ofstream(exportPath, std::ios::binary) << *generated;
            return 0;
        }
        const auto map = vw::Map::parse(generated ? *generated : readFile(dataDir / "maps" / (config.mapName + ".json")));
        if (simPlayers >= 0)
            return simulate(data, map, config.difficulty, config.mode, simPlayers, fog);
        config.loadMap = [dataDir](const std::string& name) {
            if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
                throw std::runtime_error("bad map name");
            return readFile(dataDir / "maps" / (name + ".json"));
        };
        config.fog = fog;
        vw::server::Server server(data, map, config);
        const double start = nowSeconds();
        if (!server.start(start))
            return 1;
        spdlog::info("Void Warzone dedicated server {}", vw::kVersion);

        vw::net::MasterClient lobby;
        const vw::net::RoomInfo room{"", config.name, config.mapName, 0, static_cast<std::uint8_t>(config.maxPlayers), true};
        double lobbyRetry = 0.0;
        std::string announced;

        while (g_running && (runFor <= 0.0 || nowSeconds() - start < runFor)) {
            const double now = nowSeconds();
            server.update(now);
            if (!master.empty()) {
                const auto state = lobby.state();
                if ((state == vw::net::MasterClient::State::Idle || state == vw::net::MasterClient::State::Failed) && now >= lobbyRetry) {
                    if (state == vw::net::MasterClient::State::Failed)
                        spdlog::warn("master: {}", lobby.error());
                    lobby.host(master, room, server.iceHub(), now);
                    lobbyRetry = now + 10.0;
                }
                lobby.update(now);
                lobby.setPlayers(server.playerCount());
                if (lobby.state() == vw::net::MasterClient::State::Hosting && lobby.code() != announced) {
                    announced = lobby.code();
                    spdlog::info("room code {}", announced);
                }
            }
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
