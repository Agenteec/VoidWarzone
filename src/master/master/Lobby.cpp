#include "master/Lobby.h"

#include <algorithm>

#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/Protocol.h"

namespace vw::master {

using namespace vw::net;

Lobby::Lobby(IceServers servers, Send send, Close close) : servers_(std::move(servers)), send_(std::move(send)), close_(std::move(close)) {}

void Lobby::onConnect(std::uint32_t session, std::string address) {
    addresses_[session] = std::move(address);
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

std::string Lobby::who(std::uint32_t session) const {
    const auto it = addresses_.find(session);
    return it != addresses_.end() && !it->second.empty() ? it->second : "session " + std::to_string(session);
}

void Lobby::countCandidate(std::uint32_t peer, bool fromHost, const std::string& sdp) {
    auto& s = stats_[(static_cast<std::uint64_t>(peer) << 1) | (fromHost ? 1u : 0u)];
    if (sdp.find("typ relay") != std::string::npos)
        ++s.relay;
    else if (sdp.find("typ srflx") != std::string::npos || sdp.find("typ prflx") != std::string::npos)
        ++s.srflx;
    else if (sdp.find("typ host") != std::string::npos)
        ++(sdp.find("::") != std::string::npos || std::count(sdp.begin(), sdp.end(), ':') > 2 ? s.host6 : s.host);
    else
        ++s.other;
}

void Lobby::countCandidateLines(std::uint32_t peer, const std::string& sdp) {
    std::size_t pos = 0;
    while ((pos = sdp.find("a=candidate:", pos)) != std::string::npos) {
        const auto end = sdp.find('\n', pos);
        countCandidate(peer, false, sdp.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        pos = end == std::string::npos ? sdp.size() : end;
    }
}

void Lobby::logCandidates(std::uint32_t peer, bool fromHost) {
    const auto key = (static_cast<std::uint64_t>(peer) << 1) | (fromHost ? 1u : 0u);
    const auto& s = stats_[key];
    const auto it = peers_.find(peer);
    spdlog::info("{} peer {} {} candidates: host {} ipv6 {} srflx {} relay {}", it != peers_.end() ? it->second.code : "?", peer,
                 fromHost ? "host" : "joiner", s.host, s.host6, s.srflx, s.relay);
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
        spdlog::info("{} created by {} | '{}' | {} | max {}", info.code, who(session), info.name, info.map, info.maxPlayers);
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
        if (protocol != kProtocolVersion) {
            spdlog::info("{} join from {} rejected: protocol {} != {}", code, who(session), protocol, kProtocolVersion);
            return joinError(session, "version mismatch");
        }
        const auto room = rooms_.find(code);
        if (room == rooms_.end()) {
            spdlog::info("{} join from {} rejected: no such room", code, who(session));
            return joinError(session, "room not found");
        }
        if (room->second.info.players >= room->second.info.maxPlayers)
            return joinError(session, "room is full");
        const std::uint32_t peer = nextPeer_++ & 0x0fffffffu;
        peers_[peer] = {session, code};
        joinerPeers_[session] = peer;
        spdlog::info("{} peer {} joining from {} (host {})", code, peer, who(session), who(room->second.host));
        countCandidateLines(peer, offer);
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
            if (type == LobbyMsg::Candidate)
                countCandidate(peer, true, sdp);
            else
                logCandidates(peer, true);
            w.u32(0);
            if (type == LobbyMsg::Candidate)
                w.str(sdp);
            send(p->joiner, w);
        } else if (const auto j = joinerPeers_.find(session); j != joinerPeers_.end()) {
            const auto& code = peers_[j->second].code;
            const auto room = rooms_.find(code);
            if (room == rooms_.end())
                break;
            if (type == LobbyMsg::Candidate)
                countCandidate(j->second, false, sdp);
            else
                logCandidates(j->second, false);
            w.u32(j->second);
            if (type == LobbyMsg::Candidate)
                w.str(sdp);
            send(room->second.host, w);
        }
        break;
    }
    case LobbyMsg::Report: {
        std::uint32_t peer = r.u32();
        const std::string text = r.str(1024);
        if (!r.ok())
            break;
        const bool fromHost = hostRooms_.contains(session);
        if (!fromHost)
            if (const auto j = joinerPeers_.find(session); j != joinerPeers_.end())
                peer = j->second;
        const auto it = peers_.find(peer);
        spdlog::info("{} peer {} {} {} reports: {}", it != peers_.end() ? it->second.code : "?", peer, fromHost ? "host" : "joiner", who(session),
                     text);
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
        spdlog::info("{} closed (host {} left)", it->second, who(session));
        for (auto p = peers_.begin(); p != peers_.end(); ++p)
            if (p->second.code == it->second) {
                stats_.erase(static_cast<std::uint64_t>(p->first) << 1);
                stats_.erase((static_cast<std::uint64_t>(p->first) << 1) | 1u);
            }
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
            stats_.erase(static_cast<std::uint64_t>(j->second) << 1);
            stats_.erase((static_cast<std::uint64_t>(j->second) << 1) | 1u);
            peers_.erase(peer);
        }
        joinerPeers_.erase(j);
    }
    addresses_.erase(session);
}

}
