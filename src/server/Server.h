#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/World.h"
#include "net/Connection.h"
#include "net/Snapshot.h"
#include "net/Socket.h"

namespace vw::server {

struct ServerConfig {
    std::uint16_t port = 27450;
    std::string name = "Void Warzone";
    std::string mapName = "siege_test";
    int maxPlayers = 8;
    Difficulty difficulty = Difficulty::Normal;
    bool cheats = false;
};

class Server {
public:
    Server(const GameData& data, const Map& map, ServerConfig config);
    ~Server();

    bool start(double now);
    void stop();
    void update(double now);

    double nextTickTime() const { return nextTick_; }
    std::uint16_t port() const { return socket_.port(); }
    int playerCount() const { return static_cast<int>(clients_.size()); }
    const World& world() const { return world_; }

private:
    struct Client {
        std::unique_ptr<net::Connection> conn;
        std::uint64_t salt = 0;
        std::uint8_t player = 0;
        std::string name;
        std::uint32_t commandSeq = 0;
        std::uint32_t ackedTick = 0;
        std::array<std::pair<std::uint16_t, std::uint32_t>, 256> sentTicks{};
    };

    void receive(double now);
    void handleConnect(const net::Address& from, net::Reader& r, double now);
    void handleDiscovery(const net::Address& from, net::Reader& r);
    void handleMessages(Client& client);
    void sendRaw(const net::Address& to, const net::Writer& w);
    void sendAccept(const Client& client);
    void sendDeny(const net::Address& to, std::uint64_t salt, std::uint8_t reason);
    void drop(std::size_t index, bool notify);
    void tick();
    net::Snapshot buildSnapshot() const;
    net::ClientView buildView(const Client& client) const;
    Client* findClient(const net::Address& address);

    const GameData& data_;
    const Map& map_;
    ServerConfig config_;
    World world_;
    net::UdpSocket socket_;
    std::vector<std::unique_ptr<Client>> clients_;
    std::array<net::Snapshot, 64> history_{};
    double nextTick_ = 0.0;
    double now_ = 0.0;
};

}
