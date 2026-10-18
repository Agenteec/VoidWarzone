#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <doctest/doctest.h>

#include "core/Version.h"
#include "master/Lobby.h"
#include "master/MasterServer.h"
#include "net/MasterClient.h"
#include "net/NetClient.h"
#include "net/NetLibrary.h"
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

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct MasterThread {
    asio::io_context io;
    master::MasterServer server{io, 0, IceServers{}};
    std::thread thread{[this] { io.run(); }};

    ~MasterThread() {
        asio::post(io, [this] {
            server.stop();
            io.stop();
        });
        thread.join();
    }

    std::string address() const { return "127.0.0.1:" + std::to_string(server.port()); }
};

}

TEST_CASE("room codes") {
    std::mt19937 rng(1);
    const auto code = makeRoomCode(rng);
    CHECK(code.size() == 9);
    CHECK(normalizeRoomCode(code) == code);
    CHECK(normalizeRoomCode(" void-" + code.substr(5)) == code);
    CHECK(normalizeRoomCode(code.substr(5)) == code);
    CHECK(normalizeRoomCode("VOID-O0I1").empty());
    CHECK(normalizeRoomCode("ABC").empty());
}

TEST_CASE("lobby routes signaling") {
    std::vector<std::pair<std::uint32_t, Bytes>> sent;
    master::Lobby lobby({}, [&](std::uint32_t s, const Bytes& m) { sent.emplace_back(s, m); }, [](std::uint32_t) {});
    auto typeOf = [](const Bytes& m) { return static_cast<LobbyMsg>(m[0]); };

    lobby.onConnect(1);
    lobby.onConnect(2);
    CHECK(typeOf(sent[0].second) == LobbyMsg::Welcome);

    auto create = lobbyMessage(LobbyMsg::CreateRoom);
    create.u32(kProtocolVersion);
    writeRoom(create, RoomInfo{"", "room", "siege_test", 1, 4, true});
    lobby.onMessage(1, create.data());
    REQUIRE(typeOf(sent.back().second) == LobbyMsg::RoomCreated);
    Reader created(sent.back().second.data() + 1, sent.back().second.size() - 1);
    const std::string code = created.str();
    CHECK(lobby.roomCount() == 1);

    auto join = lobbyMessage(LobbyMsg::JoinRoom);
    join.u32(kProtocolVersion);
    join.str("VOID-ZZZZ");
    join.str("offer");
    lobby.onMessage(2, join.data());
    CHECK(typeOf(sent.back().second) == LobbyMsg::JoinError);

    auto join2 = lobbyMessage(LobbyMsg::JoinRoom);
    join2.u32(kProtocolVersion);
    join2.str(code);
    join2.str("offer");
    lobby.onMessage(2, join2.data());
    REQUIRE(sent.back().first == 1);
    REQUIRE(typeOf(sent.back().second) == LobbyMsg::PeerJoin);
    Reader peerJoin(sent.back().second.data() + 1, sent.back().second.size() - 1);
    const std::uint32_t peer = peerJoin.u32();
    CHECK(peerJoin.str() == "offer");

    auto cand = lobbyMessage(LobbyMsg::Candidate);
    cand.u32(0);
    cand.str("a=candidate");
    lobby.onMessage(2, cand.data());
    CHECK(sent.back().first == 1);

    auto answer = lobbyMessage(LobbyMsg::PeerAnswer);
    answer.u32(peer);
    answer.str("answer");
    lobby.onMessage(1, answer.data());
    CHECK(sent.back().first == 2);
    CHECK(typeOf(sent.back().second) == LobbyMsg::Answer);

    auto badAnswer = lobbyMessage(LobbyMsg::PeerAnswer);
    badAnswer.u32(peer + 5);
    badAnswer.str("x");
    const auto before = sent.size();
    lobby.onMessage(1, badAnswer.data());
    CHECK(sent.size() == before);

    lobby.onDisconnect(2);
    CHECK(typeOf(sent.back().second) == LobbyMsg::PeerLeft);
    lobby.onDisconnect(1);
    CHECK(lobby.roomCount() == 0);
}

TEST_CASE("join a hosted game by room code over ice") {
    NetLibrary lib;
    const auto data = GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"), readData("research.json"));
    const auto map = Map::parse(readData("maps/siege_test.json"));
    MasterThread master;

    server::ServerConfig config;
    config.port = 0;
    server::Server server(data, map, config);
    REQUIRE(server.start(now()));

    MasterClient hostLobby;
    REQUIRE(hostLobby.host(master.address(), RoomInfo{"", "test", "siege_test", 0, 8, true}, server.iceHub(), now()));

    MasterClient joinLobby;
    NetClient client;
    bool joined = false;
    const double deadline = now() + 20.0;
    while (now() < deadline && client.state() != ClientState::Connected) {
        const double t = now();
        server.update(t);
        hostLobby.update(t);
        if (!joined && hostLobby.state() == MasterClient::State::Hosting) {
            REQUIRE(joinLobby.join(master.address(), hostLobby.code(), t));
            joined = true;
        }
        if (joined) {
            joinLobby.update(t);
            REQUIRE(joinLobby.state() != MasterClient::State::Failed);
            if (joinLobby.state() == MasterClient::State::Ready && client.state() == ClientState::Disconnected)
                REQUIRE(client.connectVia(joinLobby.agent(), data.hash(), "remote", t));
        }
        client.update(t);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(client.state() == ClientState::Connected);
    joinLobby.close();
    CHECK(server.playerCount() == 1);
    CHECK(client.path().rfind("p2p", 0) == 0);

    const double until = now() + 1.0;
    while (now() < until) {
        server.update(now());
        client.update(now());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(client.hasSnapshot());

    client.disconnect();
    for (int i = 0; i < 20; ++i) {
        server.update(now());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(server.playerCount() == 0);
}

TEST_CASE("browse lists rooms") {
    NetLibrary lib;
    MasterThread master;
    IceHub hub;
    MasterClient host;
    REQUIRE(host.host(master.address(), RoomInfo{"", "listed", "siege_test", 1, 4, true}, hub, now()));
    MasterClient browser;
    bool asked = false;
    const double deadline = now() + 5.0;
    while (now() < deadline && browser.state() != MasterClient::State::Ready) {
        host.update(now());
        if (!asked && host.state() == MasterClient::State::Hosting) {
            REQUIRE(browser.browse(master.address(), now()));
            asked = true;
        }
        browser.update(now());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(browser.state() == MasterClient::State::Ready);
    REQUIRE(browser.rooms().size() == 1);
    CHECK(browser.rooms()[0].name == "listed");
    CHECK(browser.rooms()[0].code == host.code());
}
