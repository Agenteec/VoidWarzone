#include "net/NetClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>

#include <glm/common.hpp>

#include "core/Version.h"
#include "core/World.h"
#include "net/Messages.h"
#include "net/Protocol.h"

namespace vw::net {

namespace {

constexpr double kSendInterval = 1.0 / kTickRate;
constexpr double kDt = 1.0 / kTickRate;

float lerpAngle(float a, float b, float t) {
    constexpr float pi = std::numbers::pi_v<float>;
    float d = std::fmod(b - a, 2.0f * pi);
    if (d > pi)
        d -= 2.0f * pi;
    else if (d < -pi)
        d += 2.0f * pi;
    return a + d * t;
}

InterpolatedEntity toEntity(const EntityState& s) {
    InterpolatedEntity e;
    e.id = s.id;
    e.kind = s.kind;
    e.def = s.def;
    e.team = s.team;
    e.owner = s.owner;
    e.pos = {dequantizePos(s.x), dequantizePos(s.y)};
    e.vel = {dequantizeVel(s.vx), dequantizeVel(s.vy)};
    e.facing = dequantizeAngle(s.facing);
    e.aim = dequantizeAngle(s.aim);
    e.pitch = static_cast<float>(static_cast<std::int8_t>(s.pitch)) / 128.0f * std::numbers::pi_v<float>;
    e.target = {dequantizePos(s.tx), dequantizePos(s.ty)};
    e.hp = s.hp;
    e.maxHp = std::max<std::uint16_t>(s.maxHp, 1);
    e.shield = s.shield;
    e.flags = s.flags;
    e.modules = s.modules;
    e.rank = s.rank;
    return e;
}

std::uint64_t randomSalt() {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
           static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

}

NetClient::~NetClient() {
    disconnect();
}

bool NetClient::connect(const Address& server, std::uint64_t dataHash, std::string name, double now) {
    disconnect();
    if (!socket_.open(0)) {
        fail("cannot open socket");
        return false;
    }
    server_ = server;
    start(dataHash, std::move(name), now);
    return true;
}

bool NetClient::connectVia(std::shared_ptr<IceAgent> agent, std::uint64_t dataHash, std::string name, double now) {
    disconnect();
    if (!agent || agent->state() != IceAgent::State::Connected) {
        fail("ice link is not ready");
        return false;
    }
    ice_ = std::move(agent);
    server_ = IceHub::addressOf(0);
    start(dataHash, std::move(name), now);
    return true;
}

void NetClient::start(std::uint64_t dataHash, std::string name, double now) {
    dataHash_ = dataHash;
    name_ = std::move(name);
    salt_ = randomSalt();
    state_ = ClientState::Connecting;
    error_.clear();
    connectStart_ = now;
    lastRequest_ = -1.0;
    now_ = now;
}

void NetClient::disconnect() {
    if (state_ != ClientState::Disconnected && (socket_.isOpen() || ice_)) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(PacketType::Disconnect));
        w.u64(salt_);
        for (int i = 0; i < 3; ++i)
            sendRaw(w);
    }
    conn_.reset();
    socket_.close();
    ice_.reset();
    state_ = ClientState::Disconnected;
    latestTick_ = 0;
    synced_ = false;
    snapshots_ = {};
    commandSeq_ = 0;
    lobby_ = {};
    chat_.clear();
    events_.clear();
    deaths_.clear();
}

void NetClient::fail(std::string error) {
    disconnect();
    error_ = std::move(error);
}

void NetClient::sendRaw(const Writer& w) {
    sink().send(server_, w.data().data(), w.size());
}

void NetClient::sendConnectRequest() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(PacketType::ConnectRequest));
    w.u32(kMagic);
    w.u32(kProtocolVersion);
    w.u64(dataHash_);
    w.u64(salt_);
    w.str(name_);
    sendRaw(w);
}

void NetClient::handleAccept(Reader& r, double now) {
    if (r.u64() != salt_ || state_ != ClientState::Connecting)
        return;
    player_ = r.u8();
    lobby_ = {};
    lobby_.phase = r.u8() == 1 ? LobbyPhase::Playing : LobbyPhase::Lobby;
    mapName_ = r.str();
    mapHash_ = r.u64();
    if (!r.ok())
        return;
    lobby_.map = mapName_;
    lobby_.mapHash = mapHash_;
    conn_ = std::make_unique<Connection>(sink(), server_, now, salt_);
    conn_->setUnreliableHandler([this](Reader& ur) { return onSnapshot(ur); });
    state_ = ClientState::Connected;
    lastSend_ = now;
}

void NetClient::update(double now) {
    if (conn_ && now_ > 0.0 && now - now_ > 1.0)
        conn_->touch(now);
    now_ = now;
    if (state_ == ClientState::Disconnected)
        return;

    if (ice_) {
        if (ice_->state() == IceAgent::State::Failed) {
            fail("connection lost (ice)");
            return;
        }
        Bytes packet;
        while (state_ != ClientState::Disconnected && ice_ && ice_->receive(packet))
            handlePacket(packet.data(), static_cast<int>(packet.size()), now);
    } else {
        std::uint8_t buf[2048];
        Address from;
        int n = 0;
        while (state_ != ClientState::Disconnected && (n = socket_.receive(from, buf, sizeof(buf))) >= 0)
            if (from == server_)
                handlePacket(buf, n, now);
    }
    if (state_ == ClientState::Disconnected)
        return;

    if (state_ == ClientState::Connecting) {
        if (now - connectStart_ > kConnectTimeout) {
            fail("connection timed out");
            return;
        }
        if (lastRequest_ < 0.0 || now - lastRequest_ >= kConnectRetry) {
            sendConnectRequest();
            lastRequest_ = now;
        }
        return;
    }

    if (state_ == ClientState::Connected) {
        if (now - conn_->lastReceiveTime() > kTimeout) {
            fail("connection lost");
            return;
        }
        conn_->update(now);
        handleMessages();
        if (now - lastSend_ >= kSendInterval) {
            conn_->flush();
            lastSend_ = now;
        }
    }
}

void NetClient::handlePacket(const std::uint8_t* buf, int n, double now) {
    if (n < 1)
        return;
    Reader r(buf + 1, static_cast<std::size_t>(n - 1));
    switch (static_cast<PacketType>(buf[0])) {
    case PacketType::ConnectAccept:
        handleAccept(r, now);
        break;
    case PacketType::ConnectDeny:
        if (r.u64() == salt_ && state_ == ClientState::Connecting)
            fail(denyReasonText(static_cast<DenyReason>(r.u8())));
        break;
    case PacketType::Payload:
        if (conn_)
            conn_->receive(buf + 1, n - 1, now);
        break;
    case PacketType::Disconnect:
        if (state_ == ClientState::Connected)
            fail("server closed connection");
        break;
    default:
        break;
    }
}

void NetClient::resetMatch() {
    snapshots_ = {};
    latestTick_ = 0;
    synced_ = false;
    commandSeq_ = 0;
    events_.clear();
    deaths_.clear();
}

void NetClient::handleMessages() {
    Bytes msg;
    while (conn_ && conn_->popMessage(msg)) {
        Reader r(msg.data(), msg.size());
        const auto type = static_cast<MessageType>(r.u8());
        if (type == MessageType::Lobby) {
            LobbyInfo info;
            if (!readLobby(r, info))
                continue;
            if (info.phase == LobbyPhase::Lobby && lobby_.phase == LobbyPhase::Playing)
                resetMatch();
            mapName_ = info.map;
            mapHash_ = info.mapHash;
            lobby_ = std::move(info);
        } else if (type == MessageType::Chat) {
            ChatLine line;
            line.player = r.u8();
            line.name = r.str(32);
            line.text = r.str(kMaxChatLength);
            if (r.ok())
                chat_.push_back(std::move(line));
        }
    }
}

void NetClient::sendMessage(const Writer& w) {
    if (!conn_)
        return;
    conn_->sendReliable(w.data());
    conn_->flush();
    lastSend_ = now_;
}

void NetClient::sendReady(bool ready) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Ready));
    w.u8(ready ? 1 : 0);
    sendMessage(w);
}

void NetClient::sendCheats(bool on) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Cheats));
    w.u8(on ? 1 : 0);
    sendMessage(w);
}

void NetClient::sendKick(std::uint8_t target) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Kick));
    w.u8(target);
    sendMessage(w);
}

void NetClient::sendPlayerSetup(std::uint8_t target, std::uint8_t color, std::uint8_t team) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::PlayerSetup));
    w.u8(target);
    w.u8(color);
    w.u8(team);
    sendMessage(w);
}

void NetClient::sendSettings(const std::string& map, std::uint8_t mode, std::uint8_t difficulty, std::uint8_t maxPlayers) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Settings));
    w.str(map);
    w.u8(mode);
    w.u8(difficulty);
    w.u8(maxPlayers);
    sendMessage(w);
}

void NetClient::sendStart() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Start));
    sendMessage(w);
}

void NetClient::sendSaveGame(const std::string& name) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::SaveGame));
    w.str(name);
    sendMessage(w);
}

void NetClient::sendLoadGame(const std::string& name) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::LoadGame));
    w.str(name);
    sendMessage(w);
}

void NetClient::sendReturnToLobby() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::ReturnToLobby));
    sendMessage(w);
}

void NetClient::sendChat(const std::string& text) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Chat));
    w.str(text.substr(0, kMaxChatLength));
    sendMessage(w);
}

std::uint32_t NetClient::sendCommand(const Command& command) {
    if (!conn_)
        return 0;
    Writer w;
    writeCommand(w, ++commandSeq_, command);
    conn_->sendReliable(std::move(w.data()));
    conn_->flush();
    lastSend_ = now_;
    return commandSeq_;
}

std::uint32_t NetClient::ackedCommandSeq() const {
    const Snapshot* s = latest();
    return s ? s->view.commandSeq : 0;
}

const Snapshot* NetClient::snapshot(std::uint32_t tick) const {
    if (tick == 0)
        return nullptr;
    const Snapshot& s = snapshots_[tick % snapshots_.size()];
    return s.tick == tick ? &s : nullptr;
}

bool NetClient::onSnapshot(Reader& r) {
    const std::size_t bytes = r.remaining();
    Snapshot snap;
    if (!decodeSnapshot(r, snap, [this](std::uint32_t t) { return snapshot(t); }))
        return false;
    if (latestTick_ != 0 && snap.tick + snapshots_.size() <= latestTick_)
        return true;
    lastSnapshotBytes_ = bytes;

    if (snap.tick > latestTick_) {
        const double sample = static_cast<double>(snap.tick) * kDt - now_;
        if (!synced_ || std::abs(sample - offset_) > 0.5) {
            offset_ = sample;
            jitter_ = 0.0;
            synced_ = true;
        } else {
            jitter_ += (std::abs(sample - offset_) - jitter_) * 0.1;
            offset_ += (sample - offset_) * 0.05;
        }
        const double target = std::clamp(kDt + 2.5 * jitter_ + 0.004, kDt * 0.8, 0.3);
        delay_ += (target - delay_) * 0.05;

        if (const Snapshot* prev = latest())
            for (const auto& e : prev->entities)
                if (e.kind == EntityKind::Unit && !snap.find(e.id))
                    deaths_.push_back({{dequantizePos(e.x), dequantizePos(e.y)}, e.team, e.def, e.flags});
        events_.insert(events_.end(), snap.events.begin(), snap.events.end());
        latestTick_ = snap.tick;
    }
    snapshots_[snap.tick % snapshots_.size()] = std::move(snap);
    return true;
}

void NetClient::interpolate(double now, std::vector<InterpolatedEntity>& out) const {
    out.clear();
    const Snapshot* newest = latest();
    if (!newest)
        return;

    const double renderTick = (now + offset_ - delay_) / kDt;
    const Snapshot* a = nullptr;
    const Snapshot* b = nullptr;
    for (std::uint32_t t = latestTick_; t > 0 && t + snapshots_.size() > latestTick_; --t) {
        const Snapshot* s = snapshot(t);
        if (!s)
            continue;
        if (static_cast<double>(t) <= renderTick) {
            a = s;
            break;
        }
        b = s;
    }

    if (!a) {
        for (const auto& s : b ? b->entities : newest->entities)
            out.push_back(toEntity(s));
        return;
    }
    if (!b) {
        const float ahead = static_cast<float>(std::min((renderTick - a->tick) * kDt, 0.1));
        for (const auto& s : a->entities) {
            auto e = toEntity(s);
            e.pos += e.vel * ahead;
            out.push_back(e);
        }
        return;
    }

    const float t = static_cast<float>((renderTick - a->tick) / static_cast<double>(b->tick - a->tick));
    for (const auto& sb : b->entities) {
        auto e = toEntity(sb);
        if (const EntityState* sa = a->find(sb.id)) {
            const auto ea = toEntity(*sa);
            e.pos = glm::mix(ea.pos, e.pos, t);
            e.facing = lerpAngle(ea.facing, e.facing, t);
            e.aim = lerpAngle(ea.aim, e.aim, t);
            e.pitch = ea.pitch + (e.pitch - ea.pitch) * t;
        }
        out.push_back(e);
    }
}

bool LanBrowser::start(std::uint16_t port) {
    port_ = port;
    servers_.clear();
    lastQuery_ = -1.0;
    return socket_.open(0, true);
}

void LanBrowser::stop() {
    socket_.close();
}

void LanBrowser::update(double now) {
    if (!socket_.isOpen())
        return;
    if (lastQuery_ < 0.0 || now - lastQuery_ > 1.0) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(PacketType::DiscoveryRequest));
        w.u32(kMagic);
        socket_.send(Address::broadcast(port_), w.data().data(), w.size());
        socket_.send(Address::loopback(port_), w.data().data(), w.size());
        lastQuery_ = now;
    }

    std::uint8_t buf[1024];
    Address from;
    int n = 0;
    while ((n = socket_.receive(from, buf, sizeof(buf))) > 0) {
        if (static_cast<PacketType>(buf[0]) != PacketType::DiscoveryResponse)
            continue;
        Reader r(buf + 1, static_cast<std::size_t>(n - 1));
        if (r.u32() != kMagic || r.u32() != kProtocolVersion)
            continue;
        ServerInfo info;
        info.name = r.str(64);
        info.map = r.str(64);
        info.players = r.u8();
        info.maxPlayers = r.u8();
        info.address = {from.ip, r.u16()};
        info.seen = now;
        if (!r.ok())
            continue;
        constexpr std::uint32_t loopback = 0x7f000001u;
        const auto it = std::find_if(servers_.begin(), servers_.end(), [&](const ServerInfo& s) {
            const bool sameHost = s.address.ip == info.address.ip || s.address.ip == loopback || info.address.ip == loopback;
            return sameHost && s.address.port == info.address.port && s.name == info.name && s.map == info.map;
        });
        if (it == servers_.end()) {
            servers_.push_back(info);
            continue;
        }
        if (it->address.ip == loopback)
            info.address.ip = loopback;
        *it = info;
    }
    std::erase_if(servers_, [now](const ServerInfo& s) { return now - s.seen > 3.5; });
}

}
