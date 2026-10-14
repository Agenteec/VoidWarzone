#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/Ice.h"
#include "net/Lobby.h"
#include "net/TcpLink.h"

namespace vw::net {

class MasterClient {
public:
    enum class State { Idle, Connecting, Hosting, Joining, Ready, Failed };

    bool host(const std::string& master, RoomInfo room, IceHub& hub, double now);
    bool join(const std::string& master, const std::string& code, double now);
    bool browse(const std::string& master, double now);
    void update(double now);
    void setPlayers(int players);
    void setRelayOnly(bool relayOnly) { relayOnly_ = relayOnly; }
    void setNoHostCandidates(bool noHost) { noHost_ = noHost; }
    void setNoRelay(bool noRelay) { noRelay_ = noRelay; }
    void setPortMapper(const PortMapper* mapper) { mapper_ = mapper; }
    int relayCandidates() const { return relayCandidates_; }
    void close();

    State state() const { return state_; }
    const std::string& error() const { return error_; }
    const std::string& code() const { return room_.code; }
    const std::vector<RoomInfo>& rooms() const { return rooms_; }
    std::shared_ptr<IceAgent> agent() const { return agent_; }

private:
    enum class Mode { None, Host, Join, Browse };

    bool open(const std::string& master, Mode mode, double now);
    void handle(const Bytes& message);
    void fail(std::string error);
    void send(const Writer& w) { link_.send(w.data()); }
    void flushCandidates(std::uint32_t peer, IceAgent& agent);

    TcpLink link_;
    Mode mode_ = Mode::None;
    State state_ = State::Idle;
    std::string error_;
    std::string masterHost_;
    std::string joinCode_;
    RoomInfo room_;
    IceServers servers_;
    IceHub* hub_ = nullptr;
    std::vector<std::uint32_t> peers_;
    std::shared_ptr<IceAgent> agent_;
    std::vector<RoomInfo> rooms_;
    double started_ = 0.0;
    bool relayOnly_ = false;
    bool noHost_ = false;
    bool noRelay_ = false;
    const PortMapper* mapper_ = nullptr;
    bool keep(const std::string& candidate) const;
    int relayCandidates_ = 0;
};

}
