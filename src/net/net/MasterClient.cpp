#include "net/MasterClient.h"

#include <algorithm>

#include "core/Version.h"
#include "net/Protocol.h"

namespace vw::net {

namespace {

constexpr double kMasterTimeout = 6.0;
constexpr double kJoinTimeout = 20.0;

}

bool MasterClient::open(const std::string& master, Mode mode, double now) {
    close();
    const auto address = Address::parse(master, kMasterPort);
    if (!address) {
        fail("bad master address: " + master);
        return false;
    }
    masterHost_ = master.substr(0, master.rfind(':') == std::string::npos ? master.size() : master.rfind(':'));
    if (!link_.connect(*address)) {
        fail("cannot reach master server");
        return false;
    }
    mode_ = mode;
    state_ = State::Connecting;
    error_.clear();
    started_ = now;
    return true;
}

bool MasterClient::host(const std::string& master, RoomInfo room, IceHub& hub, double now) {
    room_ = std::move(room);
    room_.code.clear();
    hub_ = &hub;
    return open(master, Mode::Host, now);
}

bool MasterClient::join(const std::string& master, const std::string& code, double now) {
    joinCode_ = normalizeRoomCode(code);
    if (joinCode_.empty()) {
        fail("bad room code");
        return false;
    }
    return open(master, Mode::Join, now);
}

bool MasterClient::browse(const std::string& master, double now) {
    rooms_.clear();
    return open(master, Mode::Browse, now);
}

void MasterClient::close() {
    link_.close();
    mode_ = Mode::None;
    if (state_ != State::Failed)
        state_ = State::Idle;
    peers_.clear();
    agent_.reset();
}

void MasterClient::fail(std::string error) {
    close();
    state_ = State::Failed;
    error_ = std::move(error);
}

void MasterClient::setPlayers(int players) {
    if (mode_ != Mode::Host || state_ != State::Hosting || room_.players == players)
        return;
    room_.players = static_cast<std::uint8_t>(std::clamp(players, 0, 255));
    auto w = lobbyMessage(LobbyMsg::UpdateRoom);
    w.u8(room_.players);
    send(w);
}

void MasterClient::flushCandidates(std::uint32_t peer, IceAgent& agent) {
    for (const auto& c : agent.takeCandidates()) {
        relayCandidates_ += c.find("typ relay") != std::string::npos;
        if (relayOnly_ && c.find("typ relay") == std::string::npos)
            continue;
        auto w = lobbyMessage(LobbyMsg::Candidate);
        w.u32(peer);
        w.str(c);
        send(w);
    }
    if (agent.takeGatheringDone()) {
        auto w = lobbyMessage(LobbyMsg::GatheringDone);
        w.u32(peer);
        send(w);
    }
}

void MasterClient::handle(const Bytes& message) {
    Reader r(message.data(), message.size());
    const auto type = static_cast<LobbyMsg>(r.u8());

    if (type == LobbyMsg::Welcome) {
        if (r.u32() != kMagic || r.u32() != kProtocolVersion)
            return fail("master protocol mismatch");
        servers_ = readIceServers(r);
        if (servers_.stunHost.empty())
            servers_.stunHost = masterHost_;
        if (servers_.turnHost.empty() && !servers_.turnUser.empty())
            servers_.turnHost = masterHost_;
        if (mode_ == Mode::Host) {
            auto w = lobbyMessage(LobbyMsg::CreateRoom);
            w.u32(kProtocolVersion);
            writeRoom(w, room_);
            send(w);
        } else if (mode_ == Mode::Join) {
            agent_ = std::make_shared<IceAgent>(servers_);
            if (!agent_->valid())
                return fail("cannot create ICE agent");
            auto w = lobbyMessage(LobbyMsg::JoinRoom);
            w.u32(kProtocolVersion);
            w.str(joinCode_);
            w.str(agent_->localDescription());
            send(w);
            state_ = State::Joining;
        } else if (mode_ == Mode::Browse) {
            send(lobbyMessage(LobbyMsg::ListRooms));
        }
        return;
    }

    switch (type) {
    case LobbyMsg::RoomCreated:
        room_.code = r.str(16);
        state_ = State::Hosting;
        break;
    case LobbyMsg::JoinError:
        fail(r.str(128));
        break;
    case LobbyMsg::PeerJoin: {
        const std::uint32_t peer = r.u32();
        const std::string offer = r.str(8192);
        if (!r.ok() || !hub_)
            break;
        IceAgent& agent = hub_->create(peer, servers_);
        agent.setRemoteDescription(offer);
        auto w = lobbyMessage(LobbyMsg::PeerAnswer);
        w.u32(peer);
        w.str(agent.localDescription());
        send(w);
        agent.gather();
        peers_.push_back(peer);
        break;
    }
    case LobbyMsg::PeerLeft: {
        const std::uint32_t peer = r.u32();
        if (hub_)
            if (IceAgent* agent = hub_->find(peer); agent && agent->state() != IceAgent::State::Connected)
                hub_->remove(peer);
        std::erase(peers_, peer);
        break;
    }
    case LobbyMsg::Answer:
        if (agent_) {
            agent_->setRemoteDescription(r.str(8192));
            agent_->gather();
        }
        break;
    case LobbyMsg::Candidate: {
        const std::uint32_t peer = r.u32();
        const std::string sdp = r.str(1024);
        if (relayOnly_ && sdp.find("typ relay") == std::string::npos)
            break;
        if (mode_ == Mode::Join && agent_)
            agent_->addRemoteCandidate(sdp);
        else if (hub_)
            if (IceAgent* agent = hub_->find(peer))
                agent->addRemoteCandidate(sdp);
        break;
    }
    case LobbyMsg::GatheringDone: {
        const std::uint32_t peer = r.u32();
        if (mode_ == Mode::Join && agent_)
            agent_->remoteGatheringDone();
        else if (hub_)
            if (IceAgent* agent = hub_->find(peer))
                agent->remoteGatheringDone();
        break;
    }
    case LobbyMsg::RoomList: {
        rooms_.clear();
        const auto count = r.u16();
        for (std::uint16_t i = 0; i < count && r.ok(); ++i)
            rooms_.push_back(readRoom(r));
        state_ = State::Ready;
        link_.close();
        mode_ = Mode::None;
        break;
    }
    default:
        break;
    }
}

void MasterClient::update(double now) {
    if (mode_ == Mode::None)
        return;
    link_.update();
    Bytes message;
    while (mode_ != Mode::None && link_.pop(message))
        handle(message);
    if (mode_ == Mode::None)
        return;

    if (link_.state() == TcpLink::State::Failed) {
        fail(state_ == State::Connecting ? "cannot reach master server" : "master connection lost");
        return;
    }
    if (state_ == State::Connecting && now - started_ > kMasterTimeout) {
        fail("master server timed out");
        return;
    }

    if (mode_ == Mode::Host && hub_) {
        std::erase_if(peers_, [&](std::uint32_t p) { return hub_->find(p) == nullptr; });
        for (const auto p : peers_)
            flushCandidates(p, *hub_->find(p));
    } else if (mode_ == Mode::Join && agent_) {
        flushCandidates(0, *agent_);
        if (agent_->state() == IceAgent::State::Connected)
            state_ = State::Ready;
        else if (agent_->state() == IceAgent::State::Failed)
            fail("could not reach host (NAT traversal failed)");
        else if (now - started_ > kJoinTimeout)
            fail("joining timed out");
    }
    link_.update();
}

}
