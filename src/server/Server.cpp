#include "server/Server.h"

#include <algorithm>
#include <optional>
#include <random>

#include "core/MapGen.h"
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
    if (world_) {
        world_->setCheats(config_.cheats);
        world_->setFog(config_.fog);
    }
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
    while (idTaken(player))
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
        world_->spawnPlayer(player, matchTeam(player, 0));
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

bool Server::idTaken(std::uint8_t player) const {
    return std::any_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c->player == player; }) ||
           std::any_of(bots_.begin(), bots_.end(), [&](const Bot& b) { return b.player == player; });
}

std::uint8_t Server::matchTeam(std::uint8_t player, std::uint8_t lobbyTeam) const {
    if (config_.mode != GameMode::Skirmish)
        return 0;
    bool same = true;
    std::optional<std::uint8_t> first;
    auto check = [&](std::uint8_t t) {
        if (!first)
            first = t;
        same = same && *first == t;
    };
    for (const auto& c : clients_)
        check(c->team);
    for (const auto& b : bots_)
        check(b.team);
    return static_cast<std::uint8_t>(kFirstSkirmishTeam + (same ? player : lobbyTeam));
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
    info.cheats = config_.cheats;
    info.fog = config_.fog;
    for (const auto& c : clients_)
        info.players.push_back({c->player, c->name, c->ready, c->color, c->team});
    for (const auto& b : bots_)
        info.players.push_back({b.player, "AI " + std::to_string(b.player + 1), true, b.color, b.team, true});
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
    world_->setCheats(config_.cheats);
    world_->setFog(config_.fog);
    spdlog::info("world ready in {:.2f}s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    if (save)
        world_->loadState(*save);
    if (config_.mode == GameMode::Skirmish)
        for (const auto& b : bots_) {
            world_->spawnPlayer(b.player, matchTeam(b.player, b.team));
            world_->setAi(b.player, true);
        }
    for (auto& c : clients_) {
        if (save && world_->resumePlayer(c->player))
            continue;
        world_->spawnPlayer(c->player, matchTeam(c->player, c->team));
        if (c->player == leader() && !config_.roster.empty()) {
            try {
                world_->spawnRoster(c->player, config_.roster);
                world_->importResearch(c->player, config_.research);
            } catch (const std::exception& e) {
                spdlog::warn("roster: {}", e.what());
            }
        }
        if (config_.cheats)
            world_->devBoost(c->player);
        c->ready = false;
        c->ackedTick = 0;
        c->commandSeq = 0;
        c->history.reset();
        c->vanish.clear();
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
            map_ = std::make_unique<Map>(Map::parse(generatedMap(header.map).value_or(config_.loadMap(header.map))));
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
    if (mapName == "random")
        mapName = std::string(kRandomMapPrefix) + std::to_string(std::random_device{}() % 1000000u);
    if (mapName != config_.mapName && config_.loadMap) {
        try {
            const auto generated = generatedMap(mapName);
            auto map = std::make_unique<Map>(Map::parse(generated ? *generated : config_.loadMap(mapName)));
            map_ = std::move(map);
            config_.mapName = std::move(mapName);
        } catch (const std::exception& e) {
            spdlog::warn("map '{}': {}", mapName, e.what());
        }
    }
    config_.mode = static_cast<GameMode>(std::min<int>(mode, 3));
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
    const bool taken = std::any_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c->player != target && c->color == color; }) ||
                       std::any_of(bots_.begin(), bots_.end(), [&](const auto& b) { return b.player != target && b.color == color; });
    const auto it = std::find_if(clients_.begin(), clients_.end(), [&](const auto& c) { return c->player == target; });
    if (it == clients_.end()) {
        for (auto& b : bots_)
            if (b.player == target && client.player == leader() && !world_) {
                b.team = team;
                if (!taken)
                    b.color = color;
                lobbyDirty_ = true;
            }
        return;
    }
    Client& who = **it;
    if (!taken)
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
        case MessageType::Bot: {
            const bool add = r.u8() != 0;
            const std::uint8_t target = r.u8();
            if (!r.ok() || world_ || client.player != leader())
                break;
            if (add && playerCount() < config_.maxPlayers) {
                Bot b;
                while (idTaken(b.player))
                    ++b.player;
                while ((std::any_of(clients_.begin(), clients_.end(), [&](const auto& c) { return c->color == b.color; }) ||
                        std::any_of(bots_.begin(), bots_.end(), [&](const Bot& o) { return o.color == b.color; })) &&
                       b.color + 1u < kPlayerColors.size())
                    ++b.color;
                b.team = client.team;
                bots_.push_back(b);
            } else if (!add) {
                std::erase_if(bots_, [&](const Bot& b) { return b.player == target; });
            }
            lobbyDirty_ = true;
            break;
        }
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
        case MessageType::Fog: {
            const bool on = r.u8() != 0;
            if (r.ok() && client.player == leader() && !world_ && on != config_.fog) {
                config_.fog = on;
                lobbyDirty_ = true;
            }
            break;
        }
        case MessageType::Cheats: {
            const bool on = r.u8() != 0;
            if (r.ok() && client.player == leader() && on != config_.cheats) {
                config_.cheats = on;
                if (world_)
                    world_->setCheats(on);
                notice(on ? "Cheats enabled by the host" : "Cheats disabled");
                lobbyDirty_ = true;
            }
            break;
        }
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
    for (auto [e, owner, lab] : world_->registry().view<const Owner, const Lab>().each()) {
        if (owner.player != client.player)
            continue;
        OwnLab l;
        l.id = entt::to_integral(e);
        l.topic = lab.topic;
        if (lab.topic < data_.research().size())
            l.progress = static_cast<std::uint8_t>(std::clamp(lab.progress / std::max(0.1f, data_.research(lab.topic).time), 0.0f, 1.0f) * 255.0f);
        v.labs.push_back(l);
    }
    v.research.resize(data_.research().size());
    for (std::size_t i = 0; i < v.research.size(); ++i)
        v.research[i] = world_->researched(client.player, static_cast<ResearchId>(i)) ? 1 : 0;
    v.flags = static_cast<std::uint8_t>((world_->cheats() ? kViewCheats : 0) | (player.god ? kViewGod : 0));
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

Snapshot Server::fogView(const Snapshot& full, Client& client) const {
    const std::uint8_t team = world_->player(client.player).team;
    Snapshot s;
    s.tick = full.tick;
    s.game = full.game;
    auto pos = [](std::uint16_t x, std::uint16_t y) { return glm::vec2(dequantizePos(x), dequantizePos(y)); };
    const auto& pointDefs = map_->resourcePoints();
    for (std::size_t i = 0; i < s.game.points.size() && i < pointDefs.size(); ++i)
        if (s.game.points[i].owner != team && !world_->visibleTo(team, pointDefs[i].pos))
            s.game.points[i] = {};
    for (const auto& e : full.entities) {
        bool keep = e.team == team;
        if (!keep && e.kind == EntityKind::Unit)
            keep = data_.unit(e.def).structure ? world_->exploredBy(team, pos(e.x, e.y)) : world_->visibleTo(team, pos(e.x, e.y));
        else if (!keep)
            keep = world_->visibleTo(team, pos(e.x, e.y));
        if (keep)
            s.entities.push_back(e);
    }
    for (const auto& ev : full.events) {
        const auto type = static_cast<NetEventType>(ev.type);
        const bool local = type == NetEventType::Hit || type == NetEventType::Fire || type == NetEventType::SlamWarn || type == NetEventType::Slam ||
                           type == NetEventType::Summon || type == NetEventType::Enrage || type == NetEventType::SkillCast;
        if (!local || world_->visibleTo(team, pos(ev.x, ev.y)))
            s.events.push_back(ev);
    }
    // entities that are still alive but just went out of sight, so the client doesn't play their death
    const auto& history = *client.history;
    const Snapshot& prev = history[(full.tick - 1) % history.size()];
    if (prev.tick + 1 == full.tick)
        for (const auto& e : prev.entities)
            if (!s.find(e.id) && full.find(e.id))
                client.vanish[e.id] = full.tick;
    std::erase_if(client.vanish, [&](const auto& kv) { return full.tick - kv.second > 15; });
    for (const auto& [id, t] : client.vanish)
        s.game.vanished.push_back(id);
    return s;
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
        if (proj.guided > 0.0f) {
            st.vx = quantizeVel(proj.dir.x * proj.speed);
            st.vy = quantizeVel(proj.dir.y * proj.speed);
        } else if (len > 1e-4f) {
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
    s.game.mode = static_cast<std::uint8_t>(world_->mode());
    s.game.fog = world_->fog() ? 1 : 0;
    s.game.sides = static_cast<std::uint8_t>(std::clamp(world_->sidesLeft(), 0, 255));
    s.game.winner = defense.winner < 0 ? std::uint8_t{0xff} : static_cast<std::uint8_t>(defense.winner);
    s.game.teams.assign(16, 0xff);
    for (std::size_t i = 0; i < s.game.teams.size(); ++i)
        if (world_->player(static_cast<std::uint8_t>(i)).active)
            s.game.teams[i] = world_->player(static_cast<std::uint8_t>(i)).team;
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
        const bool fogged = world_->fog() && world_->player(c->player).active;
        if (fogged && !c->history)
            c->history = std::make_unique<std::array<Snapshot, 64>>();
        auto& history = fogged ? *c->history : history_;
        if (fogged)
            history[tick % history.size()] = fogView(current, *c);
        const Snapshot& sent = history[tick % history.size()];
        const Snapshot* base = nullptr;
        if (c->ackedTick != 0 && tick - c->ackedTick < history.size() / 2) {
            const Snapshot& candidate = history[c->ackedTick % history.size()];
            if (candidate.tick == c->ackedTick)
                base = &candidate;
        }
        Writer w;
        encodeSnapshot(w, sent, base, buildView(*c));
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
