#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

#include "core/Commands.h"
#include "net/Connection.h"
#include "net/Snapshot.h"
#include "net/Socket.h"

namespace vw::net {

enum class ClientState { Disconnected, Connecting, Connected };

struct InterpolatedEntity {
    std::uint32_t id = 0;
    EntityKind kind = EntityKind::Unit;
    std::uint16_t def = 0;
    std::uint8_t team = 0;
    std::uint8_t owner = 0xff;
    glm::vec2 pos{};
    glm::vec2 vel{};
    float facing = 0.0f;
    std::uint16_t hp = 0;
    std::uint16_t maxHp = 1;
    std::uint8_t rank = 0;
};

struct UnitDeath {
    glm::vec2 pos{};
    std::uint8_t team = 0;
};

class NetClient {
public:
    NetClient() = default;
    ~NetClient();

    bool connect(const Address& server, std::uint64_t dataHash, std::string name, double now);
    void disconnect();
    void update(double now);
    std::uint32_t sendCommand(const Command& command);

    ClientState state() const { return state_; }
    const std::string& error() const { return error_; }
    std::uint8_t player() const { return player_; }
    const std::string& mapName() const { return mapName_; }
    std::uint64_t mapHash() const { return mapHash_; }

    bool hasSnapshot() const { return latestTick_ != 0; }
    const Snapshot* latest() const { return snapshot(latestTick_); }
    void interpolate(double now, std::vector<InterpolatedEntity>& out) const;
    double serverTime(double now) const { return now + offset_; }
    double interpDelay() const { return delay_; }
    double jitter() const { return jitter_; }
    std::uint32_t ackedCommandSeq() const;
    std::uint32_t lastCommandSeq() const { return commandSeq_; }
    std::size_t lastSnapshotBytes() const { return lastSnapshotBytes_; }
    ConnectionStats stats() const { return conn_ ? conn_->stats() : ConnectionStats{}; }

    std::vector<NetEvent> takeEvents() { return std::exchange(events_, {}); }
    std::vector<UnitDeath> takeDeaths() { return std::exchange(deaths_, {}); }

private:
    void sendRaw(const Writer& w);
    void sendConnectRequest();
    void handleAccept(Reader& r, double now);
    bool onSnapshot(Reader& r);
    const Snapshot* snapshot(std::uint32_t tick) const;
    void fail(std::string error);

    UdpSocket socket_;
    Address server_;
    std::unique_ptr<Connection> conn_;
    ClientState state_ = ClientState::Disconnected;
    std::string error_;
    std::string name_;
    std::uint64_t dataHash_ = 0;
    std::uint64_t salt_ = 0;
    double connectStart_ = 0.0;
    double lastRequest_ = 0.0;
    double lastSend_ = 0.0;
    double now_ = 0.0;

    std::uint8_t player_ = 0;
    std::string mapName_;
    std::uint64_t mapHash_ = 0;

    std::array<Snapshot, 64> snapshots_{};
    std::uint32_t latestTick_ = 0;
    std::size_t lastSnapshotBytes_ = 0;
    double offset_ = 0.0;
    double jitter_ = 0.0;
    double delay_ = 0.1;
    bool synced_ = false;

    std::uint32_t commandSeq_ = 0;
    std::vector<NetEvent> events_;
    std::vector<UnitDeath> deaths_;
};

struct ServerInfo {
    Address address;
    std::string name;
    std::string map;
    int players = 0;
    int maxPlayers = 0;
    double seen = 0.0;
};

class LanBrowser {
public:
    bool start(std::uint16_t port = 27450);
    void stop();
    void update(double now);
    const std::vector<ServerInfo>& servers() const { return servers_; }

private:
    UdpSocket socket_;
    std::uint16_t port_ = 27450;
    double lastQuery_ = -1.0;
    std::vector<ServerInfo> servers_;
};

}
