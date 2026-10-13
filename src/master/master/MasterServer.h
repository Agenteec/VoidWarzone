#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <asio.hpp>

#include "master/Lobby.h"

namespace vw::master {

class MasterServer {
public:
    MasterServer(asio::io_context& io, std::uint16_t port, net::IceServers servers);
    ~MasterServer();

    std::uint16_t port() const { return port_; }
    std::size_t sessionCount() const { return sessions_.size(); }
    const Lobby& lobby() const { return lobby_; }
    void stop();

private:
    class Session;

    void accept();
    void send(std::uint32_t id, const net::Bytes& message);
    void close(std::uint32_t id);

    asio::ip::tcp::acceptor acceptor_;
    std::uint16_t port_ = 0;
    Lobby lobby_;
    std::unordered_map<std::uint32_t, std::shared_ptr<Session>> sessions_;
    std::uint32_t nextId_ = 1;
};

}
