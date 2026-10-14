#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <unordered_map>

#include "net/Lobby.h"

namespace vw::master {

class Lobby {
public:
    using Send = std::function<void(std::uint32_t session, const net::Bytes& message)>;
    using Close = std::function<void(std::uint32_t session)>;

    Lobby(net::IceServers servers, Send send, Close close);

    void onConnect(std::uint32_t session, std::string address = {});
    void onMessage(std::uint32_t session, const net::Bytes& message);
    void onDisconnect(std::uint32_t session);

    std::size_t roomCount() const { return rooms_.size(); }

private:
    struct Room {
        net::RoomInfo info;
        std::uint32_t host = 0;
    };
    struct Peer {
        std::uint32_t joiner = 0;
        std::string code;
    };

    void send(std::uint32_t session, const net::Writer& w) { send_(session, w.data()); }
    void joinError(std::uint32_t session, const std::string& reason);
    const Peer* hostPeer(std::uint32_t host, std::uint32_t peer) const;
    std::string who(std::uint32_t session) const;
    void countCandidate(std::uint32_t peer, bool fromHost, const std::string& sdp);
    void logCandidates(std::uint32_t peer, bool fromHost);
    void countCandidateLines(std::uint32_t peer, const std::string& sdp);

    struct CandidateStats {
        int host = 0;
        int host6 = 0;
        int srflx = 0;
        int relay = 0;
        int other = 0;
    };

    net::IceServers servers_;
    Send send_;
    Close close_;
    std::map<std::string, Room> rooms_;
    std::unordered_map<std::uint32_t, std::string> hostRooms_;
    std::unordered_map<std::uint32_t, Peer> peers_;
    std::unordered_map<std::uint32_t, std::uint32_t> joinerPeers_;
    std::uint32_t nextPeer_ = 1;
    std::unordered_map<std::uint32_t, std::string> addresses_;
    std::unordered_map<std::uint64_t, CandidateStats> stats_;
    std::mt19937 rng_{std::random_device{}()};
};

}
