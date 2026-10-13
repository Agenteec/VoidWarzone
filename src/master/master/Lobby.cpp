#include "master/Lobby.h"

#include "core/Version.h"
#include "net/Protocol.h"

namespace vw::master {

using namespace vw::net;

Lobby::Lobby(IceServers servers, Send send, Close close) : servers_(std::move(servers)), send_(std::move(send)), close_(std::move(close)) {}

void Lobby::onConnect(std::uint32_t session) {
    auto w = lobbyMessage(LobbyMsg::Welcome);
    w.u32(kMagic);
    w.u32(kProtocolVersion);
    writeIceServers(w, servers_);
    send(session, w);
}

void Lobby::joinError(std::uint32_t session, const std::string& reason) {
    auto w = lobbyMessage(LobbyMsg::JoinError);
    w.str(reason);
    send(session, w);
}

const Lobby::Peer* Lobby::hostPeer(std::uint32_t host, std::uint32_t peer) const {
    const auto room = hostRooms_.find(host);
    const auto it = peers_.find(peer);
    if (room == hostRooms_.end() || it == peers_.end() || it->second.code != room->second)
        return nullptr;
    return &it->second;
}

void Lobby::onMessage(std::uint32_t session, const Bytes& message) {
    Reader r(message.data(), message.size());
    const auto type = static_cast<LobbyMsg>(r.u8());

    switch (type) {
    case LobbyMsg::CreateRoom: {
        const std::uint32_t protocol = r.u32();
        RoomInfo info = readRoom(r);
        if (!r.ok() || protocol != kProtocolVersion) {
            joinError(session, "version mismatch");
            close_(session);
            return;
        }
        if (const auto old = hostRooms_.find(session); old != hostRooms_.end())
            rooms_.erase(old->second);
        do
            info.code = makeRoomCode(rng_);
        while (rooms_.contains(info.code));
        rooms_[info.code] = {info, session};
        hostRooms_[session] = info.code;
        auto w = lobbyMessage(LobbyMsg::RoomCreated);
        w.str(info.code);
        send(session, w);
        break;
    }
    case LobbyMsg::UpdateRoom:
        if (const auto it = hostRooms_.find(session); it != hostRooms_.end())
            rooms_[it->second].info.players = r.u8();
        break;
    case LobbyMsg::JoinRoom: {
        const std::uint32_t protocol = r.u32();
        const std::string code = normalizeRoomCode(r.str(16));
        const std::string offer = r.str(8192);
        if (!r.ok())
            return close_(session);
        if (protocol != kProtocolVersion)
            return joinError(session, "version mismatch");
        const auto room = rooms_.find(code);
        if (room == rooms_.end())
            return joinError(session, "room not found");
        if (room->second.info.players >= room->second.info.maxPlayers)
            return joinError(session, "room is full");
        const std::uint32_t peer = nextPeer_++ & 0x0fffffffu;
        peers_[peer] = {session, code};
        joinerPeers_[session] = peer;
        auto w = lobbyMessage(LobbyMsg::PeerJoin);
        w.u32(peer);
        w.str(offer);
        send(room->second.host, w);
        break;
    }
    case LobbyMsg::PeerAnswer: {
        const std::uint32_t peer = r.u32();
        const std::string answer = r.str(8192);
        if (const Peer* p = hostPeer(session, peer); p && r.ok()) {
            auto w = lobbyMessage(LobbyMsg::Answer);
            w.str(answer);
            send(p->joiner, w);
        }
        break;
    }
    case LobbyMsg::Candidate:
    case LobbyMsg::GatheringDone: {
        const std::uint32_t peer = r.u32();
        const std::string sdp = type == LobbyMsg::Candidate ? r.str(1024) : std::string{};
        if (!r.ok())
            break;
        auto w = lobbyMessage(type);
        if (const Peer* p = hostPeer(session, peer)) {
            w.u32(0);
            if (type == LobbyMsg::Candidate)
                w.str(sdp);
            send(p->joiner, w);
        } else if (const auto j = joinerPeers_.find(session); j != joinerPeers_.end()) {
            const auto& code = peers_[j->second].code;
            const auto room = rooms_.find(code);
            if (room == rooms_.end())
                break;
            w.u32(j->second);
            if (type == LobbyMsg::Candidate)
                w.str(sdp);
            send(room->second.host, w);
        }
        break;
    }
    case LobbyMsg::ListRooms: {
        auto w = lobbyMessage(LobbyMsg::RoomList);
        std::uint16_t count = 0;
        for (const auto& [code, room] : rooms_)
            count += room.info.listed && count < 200;
        w.u16(count);
        std::uint16_t written = 0;
        for (const auto& [code, room] : rooms_)
            if (room.info.listed && written < count) {
                writeRoom(w, room.info);
                ++written;
            }
        send(session, w);
        break;
    }
    default:
        break;
    }
}

void Lobby::onDisconnect(std::uint32_t session) {
    if (const auto it = hostRooms_.find(session); it != hostRooms_.end()) {
        for (auto p = peers_.begin(); p != peers_.end();)
            p = p->second.code == it->second ? peers_.erase(p) : std::next(p);
        rooms_.erase(it->second);
        hostRooms_.erase(it);
    }
    if (const auto j = joinerPeers_.find(session); j != joinerPeers_.end()) {
        const auto peer = peers_.find(j->second);
        if (peer != peers_.end()) {
            if (const auto room = rooms_.find(peer->second.code); room != rooms_.end()) {
                auto w = lobbyMessage(LobbyMsg::PeerLeft);
                w.u32(j->second);
                send(room->second.host, w);
            }
            peers_.erase(peer);
        }
        joinerPeers_.erase(j);
    }
}

}
