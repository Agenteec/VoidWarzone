#include <csignal>

#include <asio.hpp>
#include <juice/juice.h>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/Protocol.h"

using asio::ip::tcp;

namespace {

void accept(tcp::acceptor& acceptor) {
    acceptor.async_accept([&acceptor](std::error_code ec, tcp::socket socket) {
        if (!ec)
            spdlog::info("connection from {}", socket.remote_endpoint().address().to_string());
        if (acceptor.is_open())
            accept(acceptor);
    });
}

}

int main() {
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);

    juice_server_config_t stunConfig{};
    stunConfig.port = vw::net::kStunPort;
    stunConfig.max_allocations = 0;
    juice_server_t* stun = juice_server_create(&stunConfig);
    if (!stun) {
        spdlog::error("failed to start STUN on {}", vw::net::kStunPort);
        return 1;
    }

    asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), vw::net::kMasterPort));
    accept(acceptor);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](std::error_code, int) {
        acceptor.close();
        io.stop();
    });

    spdlog::info("Void Warzone master {} | lobby tcp {} | stun udp {}", vw::kVersion, vw::net::kMasterPort, vw::net::kStunPort);
    io.run();

    juice_server_destroy(stun);
    spdlog::info("stopped");
    return 0;
}
