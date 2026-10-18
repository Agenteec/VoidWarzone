#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <variant>

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include "net/Messages.h"
#include "net/NetClient.h"
#include "net/NetLibrary.h"
#include "net/Protocol.h"
#include "net/Snapshot.h"
#include "server/Server.h"

using namespace vw;
using namespace vw::net;

namespace {

std::string readData(const std::string& rel) {
    std::ifstream f(std::string(VW_DATA_DIR) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

EntityState makeEntity(std::uint32_t id, float x, float y) {
    EntityState e;
    e.id = id;
    e.def = 3;
    e.team = 1;
    e.x = quantizePos(x);
    e.y = quantizePos(y);
    e.hp = 100;
    return e;
}

bool sameEntities(const Snapshot& a, const Snapshot& b) {
    if (a.entities.size() != b.entities.size())
        return false;
    for (std::size_t i = 0; i < a.entities.size(); ++i) {
        const auto& x = a.entities[i];
        const auto& y = b.entities[i];
        if (x.id != y.id || x.x != y.x || x.y != y.y || x.hp != y.hp || x.def != y.def || x.team != y.team || x.owner != y.owner)
            return false;
    }
    return true;
}

}

TEST_CASE("serialize primitives") {
    Writer w;
    w.u8(7);
    w.u16(0xbeef);
    w.u32(0xdeadbeef);
    w.u64(0x0123456789abcdefull);
    w.i16(-1234);
    w.f32(3.5f);
    w.varu(300);
    w.str("void");
    Reader r(w.data().data(), w.size());
    CHECK(r.u8() == 7);
    CHECK(r.u16() == 0xbeef);
    CHECK(r.u32() == 0xdeadbeef);
    CHECK(r.u64() == 0x0123456789abcdefull);
    CHECK(r.i16() == -1234);
    CHECK(r.f32() == 3.5f);
    CHECK(r.varu() == 300);
    CHECK(r.str() == "void");
    CHECK(r.ok());
    CHECK(r.remaining() == 0);
    r.u8();
    CHECK_FALSE(r.ok());
}

TEST_CASE("snapshot delta roundtrip") {
    Snapshot base;
    base.tick = 10;
    base.entities = {makeEntity(1, 5, 5), makeEntity(2, 6, 6), makeEntity(3, 7, 7)};

    Snapshot cur;
    cur.tick = 11;
    cur.entities = {makeEntity(1, 5, 5), makeEntity(3, 7.5f, 7), makeEntity(4, 9, 9)};
    cur.entities[0].hp = 90;
    cur.events.push_back({1, quantizePos(2), quantizePos(3)});

    Writer full;
    encodeSnapshot(full, cur, nullptr, {});
    Writer delta;
    encodeSnapshot(delta, cur, &base, ClientView{5});
    CHECK(delta.size() < full.size());

    Snapshot decoded;
    Reader r(delta.data().data(), delta.size());
    REQUIRE(decodeSnapshot(r, decoded, [&](std::uint32_t t) { return t == base.tick ? &base : nullptr; }));
    CHECK(decoded.tick == 11);
    CHECK(decoded.view.commandSeq == 5);
    CHECK(sameEntities(decoded, cur));
    CHECK(decoded.events.size() == 1);

    Reader missing(delta.data().data(), delta.size());
    Snapshot ignored;
    CHECK_FALSE(decodeSnapshot(missing, ignored, [](std::uint32_t) { return nullptr; }));
}

TEST_CASE("command roundtrip") {
    MoveCommand move{{static_cast<entt::entity>(5), static_cast<entt::entity>(9)}, {12.5f, 40.25f}, true};
    Writer w;
    writeCommand(w, 42, move);
    Reader r(w.data().data(), w.size());
    CHECK(static_cast<MessageType>(r.u8()) == MessageType::Command);
    std::uint32_t seq = 0;
    Command decoded;
    REQUIRE(readCommand(r, seq, decoded));
    CHECK(seq == 42);
    const auto* m = std::get_if<MoveCommand>(&decoded);
    REQUIRE(m);
    CHECK(m->units.size() == 2);
    CHECK(m->point.y == 40.25f);
    CHECK(m->attack);
}

TEST_CASE("build command roundtrip and arena map") {
    Writer w;
    writeCommand(w, 7, BuildCommand{{static_cast<entt::entity>(4)}, 11, {20.5f, 31.5f}, true});
    Reader r(w.data().data(), w.size());
    r.u8();
    std::uint32_t seq = 0;
    Command decoded;
    REQUIRE(readCommand(r, seq, decoded));
    const auto* b = std::get_if<BuildCommand>(&decoded);
    REQUIRE(b);
    CHECK(b->unit == 11);
    CHECK(b->pos.y == 31.5f);
    CHECK(b->queue);
    CHECK(b->units.size() == 1);

    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"));
    const auto map = Map::parse(readData("maps/void_arena.json"));
    CHECK(data.buildable().size() == 11);
    World world(data, map, Difficulty::Normal, GameMode::Arena);
    world.spawnPlayer(0);
    CHECK(world.canBuild(data.find("wall"), {52.5f, 20.5f}));
    for (int i = 0; i < 70 * kTickRate; ++i)
        world.step();
    CHECK(world.defense().wave == 1);
    CHECK(world.defense().state == MatchState::Playing);
}

TEST_CASE("server and client over loopback") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"));
    const auto map = Map::parse(readData("maps/siege_test.json"));

    server::ServerConfig config;
    config.port = 0;
    server::Server server(data, map, config);
    double now = 0.0;
    REQUIRE(server.start(now));

    NetClient client;
    REQUIRE(client.connect(Address::loopback(server.port()), data.hash(), "tester", now));

    auto pump = [&](double seconds) {
        const double end = now + seconds;
        while (now < end) {
            now += 1.0 / 120.0;
            server.update(now);
            client.update(now);
            std::this_thread::yield();
        }
    };

    pump(0.5);
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.player() == 0);
    CHECK(client.mapHash() == map.hash());
    CHECK(server.playerCount() == 1);
    REQUIRE(client.hasSnapshot());

    std::vector<entt::entity> own;
    glm::vec2 before{};
    for (const auto& e : client.latest()->entities)
        if (e.owner == client.player() && e.kind == EntityKind::Unit) {
            own.push_back(static_cast<entt::entity>(e.id));
            before = {dequantizePos(e.x), dequantizePos(e.y)};
        }
    REQUIRE(own.size() == map.playerStart().units.size());

    const glm::vec2 target{30.0f, 18.0f};
    const auto seq = client.sendCommand(MoveCommand{own, target});
    pump(3.0);
    CHECK(client.ackedCommandSeq() == seq);

    const auto* moved = client.latest()->find(entt::to_integral(own.back()));
    REQUIRE(moved);
    const glm::vec2 after{dequantizePos(moved->x), dequantizePos(moved->y)};
    CHECK(glm::distance(after, target) < glm::distance(before, target) - 5.0f);

    std::vector<InterpolatedEntity> interpolated;
    client.interpolate(now, interpolated);
    CHECK(interpolated.size() > own.size());

    client.disconnect();
    pump(0.2);
    CHECK(server.playerCount() == 0);
}

TEST_CASE("lobby picks map and mode, starts and returns") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"));
    const auto map = Map::parse(readData("maps/siege_test.json"));

    server::ServerConfig config;
    config.port = 0;
    config.lobby = true;
    config.mapName = "siege_test";
    config.loadMap = [](const std::string& name) { return readData("maps/" + name + ".json"); };
    server::Server server(data, map, config);
    double now = 0.0;
    REQUIRE(server.start(now));

    NetClient a;
    NetClient b;
    REQUIRE(a.connect(Address::loopback(server.port()), data.hash(), "alpha", now));
    REQUIRE(b.connect(Address::loopback(server.port()), data.hash(), "beta", now));
    auto pump = [&](double seconds) {
        const double end = now + seconds;
        while (now < end) {
            now += 1.0 / 120.0;
            server.update(now);
            a.update(now);
            b.update(now);
            std::this_thread::yield();
        }
    };
    pump(0.5);
    REQUIRE(a.state() == ClientState::Connected);
    REQUIRE(b.state() == ClientState::Connected);
    CHECK(a.inLobby());
    CHECK_FALSE(server.playing());
    REQUIRE(a.lobby().players.size() == 2);
    CHECK(a.lobby().leader == a.player());
    CHECK_FALSE(a.hasSnapshot());

    b.sendSettings("void_siege", 1, 3, 4);
    b.sendStart();
    pump(0.3);
    CHECK_FALSE(server.playing());
    CHECK(a.lobby().map == "siege_test");

    b.sendReady(true);
    a.sendChat("hello");
    a.sendSettings("void_siege", 1, 2, 6);
    pump(0.3);
    CHECK(a.lobby().map == "void_siege");
    CHECK(a.lobby().mode == 1);
    CHECK(a.lobby().difficulty == 2);
    CHECK(a.lobby().maxPlayers == 6);
    CHECK(a.lobby().players[1].ready);
    const auto chat = b.takeChat();
    REQUIRE(chat.size() == 1);
    CHECK(chat[0].text == "hello");
    CHECK(chat[0].name == "alpha");

    a.sendStart();
    pump(0.5);
    REQUIRE(server.playing());
    CHECK(server.world()->mode() == GameMode::Endless);
    CHECK(server.world()->difficulty() == Difficulty::Hard);
    CHECK_FALSE(b.inLobby());
    CHECK(b.mapName() == "void_siege");
    REQUIRE(b.hasSnapshot());
    CHECK(b.latest()->game.totalWaves == 0);

    a.sendReturnToLobby();
    pump(0.3);
    CHECK_FALSE(server.playing());
    CHECK(b.inLobby());
    CHECK_FALSE(b.hasSnapshot());

    a.sendStart();
    pump(0.5);
    CHECK(server.playing());
    CHECK(b.hasSnapshot());
}

TEST_CASE("server denies data mismatch") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"));
    const auto map = Map::parse(readData("maps/siege_test.json"));
    server::ServerConfig config;
    config.port = 0;
    server::Server server(data, map, config);
    double now = 0.0;
    REQUIRE(server.start(now));

    NetClient client;
    REQUIRE(client.connect(Address::loopback(server.port()), data.hash() + 1, "bad", now));
    for (int i = 0; i < 60 && client.state() == ClientState::Connecting; ++i) {
        now += 1.0 / 60.0;
        server.update(now);
        client.update(now);
        std::this_thread::yield();
    }
    CHECK(client.state() == ClientState::Disconnected);
    CHECK(client.error() == denyReasonText(DenyReason::Data));
}

TEST_CASE("lan browser finds server") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"));
    const auto map = Map::parse(readData("maps/siege_test.json"));
    server::ServerConfig config;
    config.port = 0;
    config.name = "lan test";
    server::Server server(data, map, config);
    double now = 0.0;
    REQUIRE(server.start(now));

    LanBrowser browser;
    REQUIRE(browser.start(server.port()));
    for (int i = 0; i < 60 && browser.servers().empty(); ++i) {
        now += 1.0 / 60.0;
        browser.update(now);
        server.update(now);
        std::this_thread::yield();
        browser.update(now);
    }
    REQUIRE(browser.servers().size() == 1);
    CHECK(browser.servers()[0].name == "lan test");
    CHECK(browser.servers()[0].address.port == server.port());
}
