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
    encodeSnapshot(delta, cur, &base, {5, 0});
    CHECK(delta.size() < full.size());

    Snapshot decoded;
    Reader r(delta.data().data(), delta.size());
    REQUIRE(decodeSnapshot(r, decoded, [&](std::uint32_t t) { return t == base.tick ? &base : nullptr; }));
    CHECK(decoded.tick == 11);
    CHECK(decoded.commandSeq == 5);
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

TEST_CASE("server and client over loopback") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"));
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

TEST_CASE("server denies data mismatch") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"));
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
    const auto data = GameData::parse(readData("units.json"));
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
