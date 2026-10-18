#include "server/Server.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <numbers>
#include <variant>

#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "net/Messages.h"
#include "net/Protocol.h"

namespace vw::server {

using namespace vw::net;

Server::Server(const GameData& data, const Map& map, ServerConfig config)
    : data_(data), config_(std::move(config)), map_(std::make_unique<Map>(map)) {
    config_.maxPlayers = std::clamp(config_.maxPlayers, 1, 64);
    if (!config_.lobby)
        world_ = std::make_unique<World>(data_, *map_, config_.difficulty, config_.mode);
}

Server::~Server() {
    stop();
}

bool Server::start(double now) {
    if (!socket_.open(config_.port)) {
        spdlog::error("cannot bind udp port {}", config_.port);
        return false;
    }
    nextTick_ = now;
    now_ = now;
    spdlog::info("server '{}' on port {} | map {} | {} {} | max players {}{}", config_.name, socket_.port(), config_.mapName,
                 gameModeName(config_.mode), difficultyName(config_.difficulty), config_.maxPlayers, config_.lobby ? " | lobby" : "");
    return true;
}

void Server::stop() {
    for (std::size_t i = clients_.size(); i-- > 0;)
        drop(i, true);
    socket_.close();
}

Server::Client* Server::findClient(const Address& address) {
    for (auto& c : clients_)
        if (c->conn->address() == address)
            return c.get();
    return nullptr;
}

PacketSink& Server::sinkFor(const Address& address) {
    return IceHub::isVirtual(address) ? static_cast<PacketSink&>(ice_) : socket_;
}

void Server::sendRaw(const Address& to, const Writer& w) {
    sinkFor(to).send(to, w.data().data(), w.size());
}

void Server::sendAccept(const Client& client) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(PacketType::ConnectAccept));
    w.u64(client.salt);
    w.u8(client.player);
    w.u8(world_ ? 1 : 0);
    w.str(config_.mapName);
    w.u64(map_->hash());
    sendRaw(client.conn->address(), w);
}

void Server::sendDeny(const Address& to, std::uint64_t salt, std::uint8_t reason) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(PacketType::ConnectDeny));
    w.u64(salt);
    w.u8(reason);
    sendRaw(to, w);
}

void Server::handleConnect(const Address& from, Reader& r, double now) {
    if (r.u32() != kMagic)
        return;
    const std::uint32_t protocol = r.u32();
    const std::uint64_t dataHash = r.u64();
    const std::uint64_t salt = r.u64();
    std::string name = r.str(32);
    if (!r.ok())
        return;

    if (Client* existing = findClient(from)) {
        if (existing->salt == salt)
            sendAccept(*existing);
        return;
    }
    if (protocol != kProtocolVersion)
        return sendDeny(from, salt, static_cast<std::uint8_t>(DenyReason::Version));
    if (dataHash != data_.hash())
        return sendDeny(from, salt, static_cast<std::uint8_t>(DenyReason::Data));
    if (playerCount() >= config_.maxPlayers)
        return sendDeny(from, salt, static_cast<std::uint8_t>(DenyReason::Full));

    std::uint8_t player = 0;
    while (std::any_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c->player == player; }))
        ++player;

    auto client = std::make_unique<Client>();
    client->conn = std::make_unique<Connection>(sinkFor(from), from, now, salt);
    client->salt = salt;
    client->player = player;
    client->name = name.empty() ? "Player " + std::to_string(player + 1) : std::move(name);
    while (std::any_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c->color == client->color; }) &&
           client->color + 1u < kPlayerColors.size())
        ++client->color;
    if (world_ && !world_->resumePlayer(player)) {
        world_->spawnPlayer(player);
        if (config_.cheats)
            world_->devBoost(player);
    }
    lobbyDirty_ = true;
    sendAccept(*client);
    spdlog::info("player {} '{}' joined from {}", player, client->name, from.toString());
    clients_.push_back(std::move(client));
}

void Server::handleDiscovery(const Address& from, Reader& r) {
    if (r.u32() != kMagic)
        return;
    Writer w;
    w.u8(static_cast<std::uint8_t>(PacketType::DiscoveryResponse));
    w.u32(kMagic);
    w.u32(kProtocolVersion);
    w.str(config_.name);
    w.str(config_.mapName);
    w.u8(static_cast<std::uint8_t>(playerCount()));
    w.u8(static_cast<std::uint8_t>(config_.maxPlayers));
    w.u16(socket_.port());
    sendRaw(from, w);
}

void Server::drop(std::size_t index, bool notify) {
    auto& c = *clients_[index];
    if (notify) {
        Writer w;
        w.u8(static_cast<std::uint8_t>(PacketType::Disconnect));
        w.u64(c.salt);
        for (int i = 0; i < 3; ++i)
            sendRaw(c.conn->address(), w);
    }
    spdlog::info("player {} '{}' left", c.player, c.name);
    if (world_)
        world_->releasePlayer(c.player);
    ice_.remove(c.conn->address());
    clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(index));
    lobbyDirty_ = true;
    if (clients_.empty() && config_.lobby && world_)
        endMatch();
}

std::uint8_t Server::leader() const {
    std::uint8_t best = 0xff;
    for (const auto& c : clients_)
        best = std::min(best, c->player);
    return best;
}

void Server::broadcastLobby() {
    lobbyDirty_ = false;
    LobbyInfo info;
    info.phase = world_ ? LobbyPhase::Playing : LobbyPhase::Lobby;
    info.serverName = config_.name;
    info.map = config_.mapName;
    info.mapHash = map_->hash();
    info.mode = static_cast<std::uint8_t>(config_.mode);
    info.difficulty = static_cast<std::uint8_t>(config_.difficulty);
    info.maxPlayers = static_cast<std::uint8_t>(config_.maxPlayers);
    info.leader = leader();
    for (const auto& c : clients_)
        info.players.push_back({c->player, c->name, c->ready, c->color, c->team});
    std::sort(info.players.begin(), info.players.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Lobby));
    writeLobby(w, info);
    for (auto& c : clients_)
        c->conn->sendReliable(w.data());
}

void Server::broadcastChat(const Client& from, const std::string& text) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Chat));
    w.u8(from.player);
    w.str(from.name);
    w.str(text);
    for (auto& c : clients_)
        c->conn->sendReliable(w.data());
}

void Server::startMatch(const std::string* save) {
    const auto t0 = std::chrono::steady_clock::now();
    world_ = std::make_unique<World>(data_, *map_, config_.difficulty, config_.mode);
    spdlog::info("world ready in {:.2f}s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    if (save)
        world_->loadState(*save);
    for (auto& c : clients_) {
        if (save && world_->resumePlayer(c->player))
            continue;
        world_->spawnPlayer(c->player);
        if (c->player == leader() && !config_.roster.empty()) {
            try {
                world_->spawnRoster(c->player, config_.roster);
            } catch (const std::exception& e) {
                spdlog::warn("roster: {}", e.what());
            }
        }
        if (config_.cheats)
            world_->devBoost(c->player);
        c->ready = false;
        c->ackedTick = 0;
        c->commandSeq = 0;
        c->sentTicks = {};
    }
    history_ = {};
    nextTick_ = now_;
    lobbyDirty_ = true;
    spdlog::info("match started | map {} | {} {} | {} players", config_.mapName, gameModeName(config_.mode), difficultyName(config_.difficulty),
                 playerCount());
}

void Server::notice(const std::string& text, const std::string& from, std::uint8_t player) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(MessageType::Chat));
    w.u8(player);
    w.str(from);
    w.str(text);
    for (auto& c : clients_)
        c->conn->sendReliable(w.data());
}

void Server::saveGame(Client& client, const std::string& name) {
    if (!world_ || client.player != leader() || !validSaveName(name))
        return;
    try {
        std::filesystem::create_directories(config_.saveDir);
        const auto path = std::filesystem::path(config_.saveDir) / (name + ".json");
        std::ofstream f(path, std::ios::binary);
        f << makeSaveFile({config_.mapName, config_.mode, config_.difficulty, world_->tick()}, *world_);
        if (!f)
            throw std::runtime_error("write failed");
        spdlog::info("saved '{}' to {}", name, path.string());
        notice("Game saved: " + name);
    } catch (const std::exception& e) {
        spdlog::warn("save '{}': {}", name, e.what());
        notice("Save failed: " + std::string(e.what()));
    }
}

void Server::loadGame(Client& client, const std::string& name) {
    if (world_ || client.player != leader() || !validSaveName(name))
        return;
    try {
        std::ifstream f(std::filesystem::path(config_.saveDir) / (name + ".json"), std::ios::binary);
        if (!f)
            throw std::runtime_error("not found");
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const SaveHeader header = readSaveHeader(text);
        if (header.map != config_.mapName) {
            if (!config_.loadMap)
                throw std::runtime_error("map " + header.map + " unavailable");
            map_ = std::make_unique<Map>(Map::parse(config_.loadMap(header.map)));
            config_.mapName = header.map;
        }
        config_.mode = header.mode;
        config_.difficulty = header.difficulty;
        startMatch(&text);
        spdlog::info("loaded '{}' (map {}, tick {})", name, header.map, header.tick);
        notice("Game loaded: " + name);
    } catch (const std::exception& e) {
        world_.reset();
        lobbyDirty_ = true;
        spdlog::warn("load '{}': {}", name, e.what());
        notice("Load failed: " + std::string(e.what()));
    }
}

void Server::endMatch() {
    world_.reset();
    for (auto& c : clients_)
        c->ready = false;
    lobbyDirty_ = true;
    spdlog::info("back to lobby");
}

void Server::handleSettings(Client& client, Reader& r) {
    std::string mapName = r.str(64);
    const std::uint8_t mode = r.u8();
    const std::uint8_t difficulty = r.u8();
    const std::uint8_t maxPlayers = r.u8();
    if (!r.ok() || world_ || client.player != leader())
        return;
    if (mapName != config_.mapName && config_.loadMap) {
        try {
            auto map = std::make_unique<Map>(Map::parse(config_.loadMap(mapName)));
            map_ = std::move(map);
            config_.mapName = std::move(mapName);
        } catch (const std::exception& e) {
            spdlog::warn("map '{}': {}", mapName, e.what());
        }
    }
    config_.mode = static_cast<GameMode>(std::min<int>(mode, 2));
    config_.difficulty = static_cast<Difficulty>(std::min<int>(difficulty, 3));
    config_.maxPlayers = std::clamp(static_cast<int>(maxPlayers), std::max(1, playerCount()), 64);
    lobbyDirty_ = true;
}

void Server::handlePlayerSetup(Client& client, Reader& r) {
    const std::uint8_t target = r.u8();
    const std::uint8_t color = r.u8();
    const std::uint8_t team = r.u8();
    if (!r.ok() || color >= kPlayerColors.size() || team >= kLobbyTeams)
        return;
    if (target != client.player && client.player != leader())
        return;
    const auto it = std::find_if(clients_.begin(), clients_.end(), [&](const auto& c) { return c->player == target; });
    if (it == clients_.end())
        return;
    Client& who = **it;
    if (std::none_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c.get() != &who && c->color == color; }))
        who.color = color;
    if (!world_)
        who.team = team;
    lobbyDirty_ = true;
}

void Server::handleMessages(Client& client) {
    Bytes msg;
    while (client.conn->popMessage(msg)) {
        Reader r(msg.data(), msg.size());
        switch (static_cast<MessageType>(r.u8())) {
        case MessageType::Command:
            handleCommand(client, r);
            break;
        case MessageType::Ready:
            client.ready = r.u8() != 0;
            lobbyDirty_ = true;
            break;
        case MessageType::Settings:
            handleSettings(client, r);
            break;
        case MessageType::PlayerSetup:
            handlePlayerSetup(client, r);
            break;
        case MessageType::Kick: {
            const std::uint8_t target = r.u8();
            if (r.ok() && client.player == leader() && target != client.player)
                for (auto& c : clients_)
                    if (c->player == target)
                        c->kicked = true;
            break;
        }
        case MessageType::Start:
            if (!world_ && client.player == leader())
                startMatch();
            break;
        case MessageType::SaveGame: {
            const std::string name = r.str(64);
            if (r.ok())
                saveGame(client, name);
            break;
        }
        case MessageType::LoadGame: {
            const std::string name = r.str(64);
            if (r.ok())
                loadGame(client, name);
            break;
        }
        case MessageType::ReturnToLobby:
            if (world_ && config_.lobby && client.player == leader())
                endMatch();
            break;
        case MessageType::Chat: {
            std::string text = r.str(kMaxChatLength);
            if (r.ok() && !text.empty())
                broadcastChat(client, text);
            break;
        }
        default:
            break;
        }
    }
}

void Server::handleCommand(Client& client, Reader& r) {
    {
        if (!world_)
            return;
        std::uint32_t seq = 0;
        Command command;
        if (!readCommand(r, seq, command))
            return;
        std::visit(
            [&](auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (!UnitCommand<T>) {
                    c.player = client.player;
                } else {
                    std::erase_if(c.units, [&](entt::entity e) {
                        const auto* owner = world_->registry().valid(e) ? world_->registry().try_get<Owner>(e) : nullptr;
                        return !owner || owner->player != client.player;
                    });
                }
            },
            command);
        world_->apply(command);
        client.commandSeq = std::max(client.commandSeq, seq);
    }
}

void Server::receive(double now) {
    std::uint8_t buf[2048];
    Address from;
    int n = 0;
    while ((n = socket_.receive(from, buf, sizeof(buf))) >= 0)
        handlePacket(from, buf, n, now);
    ice_.poll([&](const Address& peer, const Bytes& packet) { handlePacket(peer, packet.data(), static_cast<int>(packet.size()), now); });
}

void Server::handlePacket(const Address& from, const std::uint8_t* buf, int n, double now) {
    if (n < 1)
        return;
    Reader r(buf + 1, static_cast<std::size_t>(n - 1));
    switch (static_cast<PacketType>(buf[0])) {
    case PacketType::ConnectRequest:
        handleConnect(from, r, now);
        break;
    case PacketType::DiscoveryRequest:
        handleDiscovery(from, r);
        break;
    case PacketType::Payload:
        if (Client* c = findClient(from)) {
            c->conn->receive(buf + 1, n - 1, now);
            for (const auto seq : c->conn->takeAckedPackets()) {
                const auto& [sentSeq, tick] = c->sentTicks[seq % c->sentTicks.size()];
                if (sentSeq == seq && tick > c->ackedTick)
                    c->ackedTick = tick;
            }
            handleMessages(*c);
        }
        break;
    case PacketType::Disconnect: {
        const std::uint64_t salt = r.u64();
        for (std::size_t i = 0; i < clients_.size(); ++i)
            if (clients_[i]->conn->address() == from && clients_[i]->salt == salt) {
                drop(i, false);
                break;
            }
        break;
    }
    default:
        break;
    }
}

ClientView Server::buildView(const Client& client) const {
    const auto& player = world_->player(client.player);
    ClientView v;
    v.commandSeq = client.commandSeq;
    v.energy = static_cast<std::uint32_t>(std::max(0.0f, player.energy));
    v.shards = static_cast<std::uint16_t>(std::clamp(player.shards, 0, 65535));
    v.stash.assign(player.stash.begin(), player.stash.end());
    for (auto [e, owner, factory] : world_->registry().view<const Owner, const Factory>().each()) {
        if (owner.player != client.player)
            continue;
        OwnFactory f;
        f.id = entt::to_integral(e);
        f.queue = factory.queue;
        const float total = factory.queue.empty() ? 1.0f : std::max(0.1f, data_.unit(factory.queue.front()).produceTime);
        f.progress = static_cast<std::uint8_t>(std::clamp(factory.progress / total, 0.0f, 1.0f) * 255.0f);
        f.hasRally = factory.hasRally;
        f.rallyX = quantizePos(factory.rally.x);
        f.rallyY = quantizePos(factory.rally.y);
        v.factories.push_back(std::move(f));
    }
    for (auto [e, owner, prog] : world_->registry().view<const Owner, const Progression>().each()) {
        if (owner.player != client.player)
            continue;
        OwnUnit u;
        u.id = entt::to_integral(e);
        u.rank = static_cast<std::uint8_t>(prog.rank);
        u.level = static_cast<std::uint8_t>(prog.level);
        u.xp = static_cast<std::uint16_t>(std::clamp(prog.xp, 0.0f, 65535.0f));
        u.skillPoints = static_cast<std::uint8_t>(std::clamp(prog.skillPoints, 0, 255));
        for (const auto& s : prog.skills)
            u.skills.push_back({s.skill, s.level, static_cast<std::uint16_t>(std::ceil(s.cooldown * 10.0f))});
        u.items.assign(prog.items.begin(), prog.items.end());
        const auto& reg = world_->registry();
        auto addOrder = [&](const Order& o) {
            if (o.type == OrderType::None || o.type == OrderType::Hold || u.orders.size() >= 8)
                return;
            glm::vec2 p = o.point;
            if ((o.type == OrderType::Attack || o.type == OrderType::Follow) && reg.valid(o.target) && reg.all_of<Position>(o.target))
                p = reg.get<Position>(o.target).value;
            u.orders.push_back({static_cast<std::uint8_t>(o.type), o.build, quantizePos(p.x), quantizePos(p.y)});
            if (o.type == OrderType::Patrol)
                u.orders.push_back({static_cast<std::uint8_t>(o.type), o.build, quantizePos(o.origin.x), quantizePos(o.origin.y)});
        };
        if (const auto* cargo = reg.try_get<Cargo>(e))
            u.cargo = static_cast<std::uint8_t>(cargo->units.size());
        if (const auto* order = reg.try_get<Order>(e))
            addOrder(*order);
        if (const auto* queue = reg.try_get<OrderQueue>(e))
            for (const auto& o : queue->items)
                addOrder(o);
        v.units.push_back(std::move(u));
    }
    return v;
}

Snapshot Server::buildSnapshot() const {
    Snapshot s;
    s.tick = static_cast<std::uint32_t>(world_->tick());
    const auto& reg = world_->registry();

    for (auto [e, pos, unit, team, health] : reg.view<const Position, const Unit, const Team, const Health>().each()) {
        EntityState st;
        st.id = entt::to_integral(e);
        st.kind = EntityKind::Unit;
        st.def = unit.def;
        st.team = team.id;
        const auto* owner = reg.try_get<Owner>(e);
        st.owner = owner ? owner->player : kNoOwner;
        if (const auto* prog = reg.try_get<Progression>(e))
            st.rank = static_cast<std::uint8_t>(static_cast<int>(prog->rank) + 1);
        st.x = quantizePos(pos.value.x);
        st.y = quantizePos(pos.value.y);
        if (const auto* vel = reg.try_get<Velocity>(e); vel && !reg.all_of<Structure>(e)) {
            st.vx = quantizeVel(vel->value.x);
            st.vy = quantizeVel(vel->value.y);
        }
        if (const auto* f = reg.try_get<Facing>(e))
            st.facing = quantizeAngle(f->angle);
        if (const auto* t = reg.try_get<Turret>(e)) {
            st.aim = quantizeAngle(t->yaw);
            st.pitch = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(std::lround(t->pitch / std::numbers::pi_v<float> * 128.0f), -127L, 127L)));
        }
        st.hp = static_cast<std::uint16_t>(std::clamp(std::ceil(health.hp), 0.0f, 65535.0f));
        st.maxHp = static_cast<std::uint16_t>(std::clamp(std::ceil(health.max), 1.0f, 65535.0f));
        st.shield = static_cast<std::uint16_t>(std::clamp(std::ceil(health.shield), 0.0f, 65535.0f));
        if (const auto* modules = reg.try_get<Modules>(e))
            for (std::size_t i = 0; i < 3; ++i)
                st.modules[i] = modules->slots[i] < 0xff ? static_cast<std::uint8_t>(modules->slots[i]) : 0xff;
        bool frozen = false;
        if (const auto* buffs = reg.try_get<Buffs>(e))
            frozen = std::any_of(buffs->list.begin(), buffs->list.end(), [](const ActiveBuff& b) { return b.skill == kFrostBuff; });
        st.flags = static_cast<std::uint8_t>((reg.all_of<Construction>(e) ? kStateBuilding : 0) | (reg.all_of<Carried>(e) ? kStateCarried : 0) |
                                             (frozen ? kStateFrozen : 0));
        s.entities.push_back(st);
    }

    for (auto [e, pos, proj] : reg.view<const Position, const Projectile>().each()) {
        EntityState st;
        st.id = entt::to_integral(e);
        st.kind = EntityKind::Projectile;
        st.team = proj.team;
        st.def = proj.def;
        st.tx = quantizePos(proj.targetPos.x);
        st.ty = quantizePos(proj.targetPos.y);
        st.x = quantizePos(pos.value.x);
        st.y = quantizePos(pos.value.y);
        const glm::vec2 d = proj.targetPos - pos.value;
        const float len = glm::length(d);
        if (len > 1e-4f) {
            st.vx = quantizeVel(d.x / len * proj.speed);
            st.vy = quantizeVel(d.y / len * proj.speed);
        }
        s.entities.push_back(st);
    }
    std::sort(s.entities.begin(), s.entities.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    const auto& defense = world_->defense();
    s.game.state = static_cast<std::uint8_t>(defense.state);
    s.game.wave = static_cast<std::uint8_t>(defense.wave);
    s.game.totalWaves = static_cast<std::uint8_t>(defense.totalWaves);
    s.game.difficulty = static_cast<std::uint8_t>(world_->difficulty());
    s.game.nextWaveIn = static_cast<std::uint16_t>(std::clamp(defense.nextWaveIn * 10.0f, 0.0f, 65535.0f));
    s.game.boss = defense.boss == entt::null ? kNoEntity : entt::to_integral(defense.boss);
    s.game.objectives = world_->objectives();
    for (const auto& p : world_->resourcePoints())
        s.game.points.push_back({static_cast<std::int8_t>(std::lround(p.control * 127.0f)),
                                 p.owner < 0 ? std::uint8_t{0xff} : static_cast<std::uint8_t>(p.owner)});

    for (const auto& ev : world_->events()) {
        const auto value = static_cast<std::uint16_t>(std::clamp(ev.value * 64.0f + 64.0f, 0.0f, 65535.0f));
        auto push = [&](NetEventType type) {
            s.events.push_back({static_cast<std::uint8_t>(type), quantizePos(ev.to.x), quantizePos(ev.to.y), value});
        };
        switch (ev.type) {
        case EventType::Shot:
            s.events.push_back({static_cast<std::uint8_t>(NetEventType::Fire), quantizePos(ev.from.x), quantizePos(ev.from.y), value});
            if (reg.valid(ev.entity))
                if (const auto* w = reg.try_get<Weapon>(ev.entity); w && w->def.projectileSpeed <= 0.0f)
                    push(NetEventType::Hit);
            break;
        case EventType::SlamWarn:
            push(NetEventType::SlamWarn);
            break;
        case EventType::Slam:
            push(NetEventType::Slam);
            break;
        case EventType::Summon:
            push(NetEventType::Summon);
            break;
        case EventType::Enrage:
            push(NetEventType::Enrage);
            break;
        case EventType::WaveStart:
            push(NetEventType::WaveStart);
            break;
        case EventType::PointCaptured:
            push(NetEventType::PointCaptured);
            break;
        case EventType::MatchEnd:
            push(NetEventType::MatchEnd);
            break;
        case EventType::SkillCast:
            push(NetEventType::SkillCast);
            break;
        case EventType::Promote:
            push(NetEventType::Promote);
            break;
        case EventType::LevelUp:
            push(NetEventType::LevelUp);
            break;
        case EventType::Loot:
            push(NetEventType::Loot);
            break;
        default:
            break;
        }
    }
    return s;
}

void Server::tick() {
    if (!world_) {
        for (auto& c : clients_)
            c->conn->flush();
        return;
    }
    world_->step();
    for (const auto& text : world_->takeMessages())
        notice(text, "Command", 0xfe);
    Snapshot snap = buildSnapshot();
    const std::uint32_t tick = snap.tick;
    history_[tick % history_.size()] = std::move(snap);
    const Snapshot& current = history_[tick % history_.size()];

    for (auto& c : clients_) {
        const Snapshot* base = nullptr;
        if (c->ackedTick != 0 && tick - c->ackedTick < history_.size() / 2) {
            const Snapshot& candidate = history_[c->ackedTick % history_.size()];
            if (candidate.tick == c->ackedTick)
                base = &candidate;
        }
        Writer w;
        encodeSnapshot(w, current, base, buildView(*c));
        c->conn->setUnreliable(std::move(w.data()));
        const std::uint16_t seq = c->conn->flush();
        c->sentTicks[seq % c->sentTicks.size()] = {seq, tick};
    }
}

void Server::update(double now) {
    if (now_ > 0.0 && now - now_ > 1.0)
        for (auto& c : clients_)
            c->conn->touch(now);
    now_ = now;
    receive(now);
    if (lobbyDirty_)
        broadcastLobby();

    int steps = 0;
    while (now >= nextTick_ && steps < 5) {
        tick();
        nextTick_ += kTickDt;
        ++steps;
    }
    if (now - nextTick_ > 1.0)
        nextTick_ = now;

    for (std::size_t i = clients_.size(); i-- > 0;) {
        clients_[i]->conn->update(now);
        if (clients_[i]->kicked)
            drop(i, true);
        else if (now - clients_[i]->conn->lastReceiveTime() > kTimeout)
            drop(i, false);
    }
}

}
