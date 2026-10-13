#include <csignal>
#include <cstdlib>
#include <string>
#include <string_view>

#include <asio.hpp>
#include <juice/juice.h>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "master/MasterServer.h"
#include "net/Protocol.h"

int main(int argc, char** argv) {
    std::uint16_t lobbyPort = vw::net::kMasterPort;
    std::uint16_t stunPort = vw::net::kStunPort;
    std::uint16_t relayBegin = 49152;
    std::uint16_t relayEnd = 49352;
    std::string externalIp;
    std::string turnUser;
    std::string turnPass;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--port" && hasValue)
            lobbyPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--stun-port" && hasValue)
            stunPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (arg == "--relay-ports" && hasValue) {
            const std::string range = argv[++i];
            const auto dash = range.find('-');
            relayBegin = static_cast<std::uint16_t>(std::atoi(range.substr(0, dash).c_str()));
            relayEnd = dash == std::string::npos ? relayBegin : static_cast<std::uint16_t>(std::atoi(range.substr(dash + 1).c_str()));
        } else if (arg == "--external-ip" && hasValue)
            externalIp = argv[++i];
        else if (arg == "--turn-user" && hasValue)
            turnUser = argv[++i];
        else if (arg == "--turn-pass" && hasValue)
            turnPass = argv[++i];
    }
    if (const char* env = std::getenv("VW_TURN_PASS"); env && turnPass.empty())
        turnPass = env;

    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    juice_server_credentials_t credentials{};
    credentials.username = turnUser.c_str();
    credentials.password = turnPass.c_str();
    credentials.allocations_quota = 0;
    const bool turn = !turnUser.empty() && !turnPass.empty();

    juice_server_config_t stunConfig{};
    stunConfig.port = stunPort;
    stunConfig.relay_port_range_begin = relayBegin;
    stunConfig.relay_port_range_end = relayEnd;
    stunConfig.external_address = externalIp.empty() ? nullptr : externalIp.c_str();
    stunConfig.realm = "voidwarzone";
    stunConfig.max_allocations = turn ? 1000 : 0;
    if (turn) {
        stunConfig.credentials = &credentials;
        stunConfig.credentials_count = 1;
    }
    juice_server_t* stun = juice_server_create(&stunConfig);
    if (!stun) {
        spdlog::error("failed to start STUN/TURN on udp {}", stunPort);
        return 1;
    }

    vw::net::IceServers servers;
    servers.stunPort = stunPort;
    servers.turnPort = stunPort;
    if (turn) {
        servers.turnUser = turnUser;
        servers.turnPass = turnPass;
    }

    asio::io_context io;
    try {
        vw::master::MasterServer master(io, lobbyPort, servers);
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](std::error_code, int) {
            master.stop();
            io.stop();
        });
        spdlog::info("Void Warzone master {} | lobby tcp {} | stun{} udp {} | relay {}-{}", vw::kVersion, master.port(), turn ? "/turn" : "", stunPort,
                     relayBegin, relayEnd);
        io.run();
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        juice_server_destroy(stun);
        return 1;
    }

    juice_server_destroy(stun);
    spdlog::info("stopped");
    return 0;
}
