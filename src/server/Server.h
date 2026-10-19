#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/World.h"
#include "net/Connection.h"
#include "net/Ice.h"
#include "net/GameLobby.h"
#include "net/Snapshot.h"
#include "net/Socket.h"

namespace vw::server {

struct ServerConfig {
    std::uint16_t port = 27450;
    std::string name = "Void Warzone";
    std::string mapName = "void_siege";
    int maxPlayers = 8;
    Difficulty difficulty = Difficulty::Normal;
    GameMode mode = GameMode::Defense;
    bool cheats = false;
    bool lobby = false;
    std::function<std::string(const std::string&)> loadMap;
    std::string saveDir = "saves";
    std::string roster;
    std::string research;
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
    int playerCount() const { return static_cast<int>(clients_.size() + bots_.size()); }
    const World* world() const { return world_.get(); }
    bool playing() const { return world_ != nullptr; }
    const ServerConfig& config() const { return config_; }
    net::IceHub& iceHub() { return ice_; }

private:
    struct Client {
        std::unique_ptr<net::Connection> conn;
        std::uint64_t salt = 0;
        std::uint8_t player = 0;
        std::string name;
        bool ready = false;
        std::uint8_t color = 0;
        std::uint8_t team = 0;
        bool kicked = false;
        std::uint32_t commandSeq = 0;
        std::uint32_t ackedTick = 0;
        std::array<std::pair<std::uint16_t, std::uint32_t>, 256> sentTicks{};
    };

    void receive(double now);
    void handlePacket(const net::Address& from, const std::uint8_t* buf, int n, double now);
    net::PacketSink& sinkFor(const net::Address& address);
    void handleConnect(const net::Address& from, net::Reader& r, double now);
    void handleDiscovery(const net::Address& from, net::Reader& r);
    void handleMessages(Client& client);
    void handleCommand(Client& client, net::Reader& r);
    void handleSettings(Client& client, net::Reader& r);
    void sendRaw(const net::Address& to, const net::Writer& w);
    void sendAccept(const Client& client);
    void sendDeny(const net::Address& to, std::uint64_t salt, std::uint8_t reason);
    void broadcastLobby();
    void handlePlayerSetup(Client& client, net::Reader& r);
    void broadcastChat(const Client& from, const std::string& text);
    void drop(std::size_t index, bool notify);
    void startMatch(const std::string* save = nullptr);
    void saveGame(Client& client, const std::string& name);
    void loadGame(Client& client, const std::string& name);
    void notice(const std::string& text, const std::string& from = "Server", std::uint8_t player = 0xff);
    void endMatch();
    std::uint8_t leader() const;
    void tick();
    net::Snapshot buildSnapshot() const;
    net::ClientView buildView(const Client& client) const;
    Client* findClient(const net::Address& address);

    const GameData& data_;
    ServerConfig config_;
    std::unique_ptr<Map> map_;
    std::unique_ptr<World> world_;
    net::UdpSocket socket_;
    net::IceHub ice_;
    std::vector<std::unique_ptr<Client>> clients_;
    struct Bot {
        std::uint8_t player = 0;
        std::uint8_t color = 0;
        std::uint8_t team = 0;
    };
    std::vector<Bot> bots_;
    bool idTaken(std::uint8_t player) const;
    std::uint8_t matchTeam(std::uint8_t player, std::uint8_t lobbyTeam) const;
    std::array<net::Snapshot, 64> history_{};
    double nextTick_ = 0.0;
    double now_ = 0.0;
    bool lobbyDirty_ = true;
};

}
