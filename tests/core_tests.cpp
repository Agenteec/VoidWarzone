#include <fstream>
#include <sstream>
#include <string>

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include "core/Pathfinding.h"
#include "core/World.h"
#include "net/NetLibrary.h"

namespace {

std::string readData(const std::string& rel) {
    std::ifstream f(std::string(VW_DATA_DIR) + "/" + rel, std::ios::binary);
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

constexpr const char* kUnits = R"({"units":[
    {"id":"soldier","maxHp":100,"radius":0.5,"speed":4,"sight":8,"weapon":{"damage":20,"range":1,"cooldown":0.5}},
    {"id":"archer","maxHp":60,"radius":0.4,"speed":4,"sight":8,"weapon":{"damage":10,"range":5,"cooldown":0.5,"projectileSpeed":15}},
    {"id":"wall","structure":true,"maxHp":1000,"radius":1}
]})";

}

TEST_CASE("world step moves raw entities") {
    vw::World world;
    auto& reg = world.registry();
    const auto e = reg.create();
    reg.emplace<vw::Position>(e);
    reg.emplace<vw::Velocity>(e, glm::vec2{3.0f, 0.0f});

    for (int i = 0; i < vw::kTickRate; ++i)
        world.step();

    CHECK(world.tick() == vw::kTickRate);
    CHECK(reg.get<vw::Position>(e).value.x == doctest::Approx(3.0f));
}

TEST_CASE("game data parses") {
    const auto data = vw::GameData::parse(kUnits);
    REQUIRE(data.units().size() == 3);
    CHECK(data.find("archer") == 1);
    CHECK(data.find("nope") == vw::kInvalidUnitDef);
    CHECK(data.unit(1).weapon->projectileSpeed == doctest::Approx(15.0f));
    CHECK(data.unit(2).structure);
    CHECK_FALSE(data.unit(2).weapon.has_value());
    CHECK_THROWS(vw::GameData::parse(R"({"units":[{"id":"a"},{"id":"a"}]})"));
}

TEST_CASE("shipped data is valid") {
    const auto data = vw::GameData::parse(readData("units.json"));
    const auto map = vw::Map::parse(readData("maps/siege_test.json"));
    CHECK(map.lanes().size() == 4);
    vw::World world(data, map);
    for (int i = 0; i < vw::kTickRate * 60; ++i)
        world.step();
    CHECK(world.registry().view<vw::Unit>().size() > map.units().size());
}

TEST_CASE("line of sight and path around wall") {
    vw::Map map(20, 20);
    map.addWall({10, 0, 1, 15});

    CHECK(vw::lineOfSight(map, {2.5f, 18.5f}, {17.5f, 18.5f}));
    CHECK_FALSE(vw::lineOfSight(map, {2.5f, 2.5f}, {17.5f, 2.5f}));

    const auto path = vw::findPath(map, {2.5f, 2.5f}, {17.5f, 2.5f});
    REQUIRE_FALSE(path.empty());
    CHECK(glm::distance(path.back(), glm::vec2{17.5f, 2.5f}) < 0.01f);
    glm::vec2 prev{2.5f, 2.5f};
    for (const auto& p : path) {
        CHECK(vw::lineOfSight(map, prev, p));
        prev = p;
    }
}

TEST_CASE("flow field leads to goal") {
    vw::Map map(20, 20);
    map.addWall({10, 0, 1, 15});
    vw::FlowField field(map, {17.5f, 2.5f});

    glm::vec2 p{2.5f, 2.5f};
    for (int i = 0; i < 400 && glm::distance(p, field.goal()) > 0.5f; ++i)
        p += field.direction(p) * 0.25f;
    CHECK(glm::distance(p, field.goal()) <= 0.5f);
}

TEST_CASE("move order reaches point and avoids walls") {
    const auto data = vw::GameData::parse(kUnits);
    vw::Map map(30, 30);
    map.addWall({15, 0, 1, 20});
    vw::World world(data, map);
    const auto u = world.spawn(data.find("soldier"), 0, {5.5f, 5.5f}, 0);

    world.apply(vw::MoveCommand{{u}, {25.5f, 5.5f}});
    for (int i = 0; i < vw::kTickRate * 20; ++i) {
        world.step();
        CHECK_FALSE(map.blocked(vw::Map::cellOf(world.registry().get<vw::Position>(u).value)));
    }
    CHECK(glm::distance(world.registry().get<vw::Position>(u).value, glm::vec2{25.5f, 5.5f}) < 0.5f);
    CHECK(world.registry().get<vw::Order>(u).type == vw::OrderType::None);
}

TEST_CASE("units fight and die") {
    const auto data = vw::GameData::parse(kUnits);
    vw::Map map(30, 30);
    vw::World world(data, map);
    const auto a = world.spawn(data.find("soldier"), 0, {10.0f, 10.0f});
    const auto b = world.spawn(data.find("archer"), 1, {14.0f, 10.0f});

    bool shot = false;
    for (int i = 0; i < vw::kTickRate * 20 && world.alive(a) && world.alive(b); ++i) {
        world.step();
        for (const auto& ev : world.events())
            shot |= ev.type == vw::EventType::Shot;
    }
    CHECK(shot);
    CHECK(world.alive(a) != world.alive(b));
}

TEST_CASE("units separate") {
    const auto data = vw::GameData::parse(kUnits);
    vw::Map map(30, 30);
    vw::World world(data, map);
    const auto a = world.spawn(data.find("soldier"), 0, {10.0f, 10.0f});
    const auto b = world.spawn(data.find("soldier"), 0, {10.0f, 10.0f});
    for (int i = 0; i < 10; ++i)
        world.step();
    const auto& reg = world.registry();
    CHECK(glm::distance(reg.get<vw::Position>(a).value, reg.get<vw::Position>(b).value) > 0.9f);
}

TEST_CASE("net library init") {
    vw::net::NetLibrary net;
    CHECK(net.ok());
}
