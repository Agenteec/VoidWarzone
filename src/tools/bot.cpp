#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <juice/juice.h>
#include <spdlog/spdlog.h>

#include "core/GameData.h"
#include "net/MasterClient.h"
#include "net/NetClient.h"
#include "net/NetLibrary.h"
#include "net/Protocol.h"

namespace {

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
    std::string master = "127.0.0.1:27400";
    std::string code;
    std::string address;
    std::string name = "bot";
    double seconds = 10.0;
    bool list = false;
    bool relayOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--master" && hasValue)
            master = argv[++i];
        else if (arg == "--join" && hasValue)
            code = argv[++i];
        else if (arg == "--connect" && hasValue)
            address = argv[++i];
        else if (arg == "--name" && hasValue)
            name = argv[++i];
        else if (arg == "--seconds" && hasValue)
            seconds = std::atof(argv[++i]);
        else if (arg == "--data" && hasValue)
            dataDir = argv[++i];
        else if (arg == "--list")
            list = true;
        else if (arg == "--relay-only")
            relayOnly = true;
        else if (arg == "--verbose")
            juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);
    }

    const auto logLevel = std::find(argv, argv + argc, std::string_view("--verbose")) != argv + argc ? JUICE_LOG_LEVEL_DEBUG : JUICE_LOG_LEVEL_WARN;
    vw::net::NetLibrary lib;
    if (!lib.ok())
        return 1;
    juice_set_log_level(logLevel);
    const auto data = vw::GameData::parse(readFile(dataDir / "units.json"), readFile(dataDir / "skills.json"), readFile(dataDir / "items.json"));

    vw::net::MasterClient lobby;
    lobby.setRelayOnly(relayOnly);
    vw::net::NetClient client;
    const double start = nowSeconds();

    if (list) {
        lobby.browse(master, start);
        while (lobby.state() == vw::net::MasterClient::State::Connecting && nowSeconds() - start < 10.0) {
            lobby.update(nowSeconds());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (lobby.state() != vw::net::MasterClient::State::Ready) {
            spdlog::error("list failed: {}", lobby.error());
            return 1;
        }
        for (const auto& r : lobby.rooms())
            spdlog::info("{} | {} | {} | {}/{}", r.code, r.name, r.map, r.players, r.maxPlayers);
        return 0;
    }

    if (!code.empty()) {
        if (!lobby.join(master, code, start)) {
            spdlog::error("{}", lobby.error());
            return 1;
        }
    } else if (const auto addr = vw::net::Address::parse(address, vw::net::kGamePort)) {
        client.connect(*addr, data.hash(), name, start);
    } else {
        spdlog::error("use --join CODE, --connect HOST[:PORT] or --list");
        return 1;
    }

    std::size_t snapshots = 0;
    std::uint32_t lastTick = 0;
    double connectedAt = 0.0;
    while (nowSeconds() - start < seconds + 20.0) {
        const double now = nowSeconds();
        lobby.update(now);
        if (!code.empty() && client.state() == vw::net::ClientState::Disconnected && lobby.state() == vw::net::MasterClient::State::Ready) {
            spdlog::info("ice connected in {:.2f}s via {}", now - start, lobby.agent()->selectedPath());
            spdlog::info("pair: {}", lobby.agent()->selectedPair());
            client.connectVia(lobby.agent(), data.hash(), name, now);
        }
        if (lobby.state() == vw::net::MasterClient::State::Failed) {
            spdlog::error("lobby: {}", lobby.error());
            return 1;
        }
        const bool wasConnected = client.state() == vw::net::ClientState::Connected;
        client.update(now);
        if (!wasConnected && client.state() == vw::net::ClientState::Connected) {
            connectedAt = now;
            spdlog::info("joined as player {} on map {} ({})", client.player() + 1, client.mapName(), client.path());
        }
        if (wasConnected && client.state() != vw::net::ClientState::Connected) {
            spdlog::error("disconnected: {}", client.error());
            return 1;
        }
        if (client.state() == vw::net::ClientState::Disconnected && !client.error().empty()) {
            spdlog::error("connect failed: {}", client.error());
            return 1;
        }
        if (const auto* s = client.latest(); s && s->tick != lastTick) {
            lastTick = s->tick;
            ++snapshots;
        }
        if (connectedAt > 0.0 && now - connectedAt >= 3.0 && lobby.state() != vw::net::MasterClient::State::Idle && !code.empty()) {
            spdlog::info("turn relay candidates gathered: {}", lobby.relayCandidates());
            lobby.close();
        }
        if (connectedAt > 0.0 && now - connectedAt >= seconds)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (connectedAt <= 0.0) {
        spdlog::error("timed out");
        return 1;
    }
    const auto stats = client.stats();
    spdlog::info("snapshots {} | rtt {:.0f} ms | loss {:.1f}% | interp {:.0f} ms | last snapshot {} B", snapshots, stats.rtt, stats.packetLoss,
                 client.interpDelay() * 1000.0, client.lastSnapshotBytes());
    client.disconnect();
    return snapshots > 0 ? 0 : 1;
}
