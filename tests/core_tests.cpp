#include <chrono>
#include <cmath>
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
    const auto data = vw::GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"), readData("research.json"));
    for (const char* name : {"maps/siege_test.json", "maps/void_siege.json"}) {
        CAPTURE(name);
        const auto map = vw::Map::parse(readData(name));
        CHECK(map.lanes().size() == 4);
        vw::World world(data, map);
        for (int i = 0; i < vw::kTickRate * 60; ++i)
            world.step();
        CHECK(world.registry().view<vw::Unit>().size() > map.units().size());
    }
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

TEST_CASE("turret turns to target before firing") {
    const auto data = vw::GameData::parse(R"({"units":[
        {"id":"gun","structure":true,"maxHp":1000,"radius":1,"weapon":{"damage":1,"range":8,"cooldown":0.5,"turnRate":1.0}},
        {"id":"dummy","maxHp":1000,"radius":0.5}
    ]})");
    vw::Map map(30, 30);
    vw::World world(data, map);
    const auto gun = world.spawn(data.find("gun"), 0, {10.0f, 10.0f});
    world.spawn(data.find("dummy"), 1, {4.0f, 10.0f});
    int firstShot = -1;
    for (int i = 0; i < vw::kTickRate * 5 && firstShot < 0; ++i) {
        world.step();
        for (const auto& ev : world.events())
            if (ev.type == vw::EventType::Shot && firstShot < 0)
                firstShot = i;
    }
    CHECK(firstShot > vw::kTickRate * 2);
    CHECK(std::abs(std::abs(world.registry().get<vw::Turret>(gun).yaw) - 3.14159f) < 0.15f);
}

TEST_CASE("spawner stops with its building") {
    const auto data = vw::GameData::parse(kUnits);
    const auto map = vw::Map::parse(R"({"width":40,"height":20,
        "lanes":[{"name":"mid","points":[[2,10],[38,10]]}],
        "units":[{"unit":"wall","team":0,"pos":[5,5],"tag":"barracks"}],
        "spawners":[{"unit":"soldier","team":0,"lane":"mid","interval":1,"building":"barracks"}]})");
    vw::World world(data, map);
    auto soldiers = [&] {
        int n = 0;
        for (auto [e, unit] : world.registry().view<const vw::Unit>().each())
            n += unit.def == data.find("soldier");
        return n;
    };
    for (int i = 0; i < vw::kTickRate * 2; ++i)
        world.step();
    CHECK(soldiers() >= 2);
    for (auto [e, unit, health] : world.registry().view<const vw::Unit, vw::Health>().each())
        if (unit.def == data.find("wall"))
            health.hp = 0.0f;
    world.step();
    const int before = soldiers();
    for (int i = 0; i < vw::kTickRate * 3; ++i)
        world.step();
    CHECK(soldiers() == before);
    CHECK_THROWS(vw::World(data, vw::Map::parse(R"({"width":10,"height":10,"lanes":[{"name":"m","points":[[1,1],[8,8]]}],
        "spawners":[{"unit":"soldier","lane":"m","building":"none"}]})")));
}

TEST_CASE("lane creeps pass a structure standing on the lane") {
    const auto data = vw::GameData::parse(kUnits);
    const auto map = vw::Map::parse(R"({"width":60,"height":20,
        "walls":[[0,0,60,6],[0,14,60,6]],
        "lanes":[{"name":"mid","points":[[2,10],[58,10]]}],
        "units":[{"unit":"wall","team":0,"pos":[20,10]}],
        "spawners":[{"unit":"soldier","team":0,"lane":"mid","interval":100,"count":5,"offset":2}]})");
    vw::World world(data, map);
    for (int i = 0; i < vw::kTickRate * 20; ++i)
        world.step();
    int passed = 0;
    for (auto [e, pos, unit] : world.registry().view<const vw::Position, const vw::Unit>().each())
        passed += unit.def == data.find("soldier") && pos.value.x > 40.0f;
    CHECK(passed == 5);
}

TEST_CASE("ordered squads drive around towers on the real map") {
    const auto data = vw::GameData::parse(readData("units.json"), readData("skills.json"), readData("items.json"), readData("research.json"));
    const auto map = vw::Map::parse(readData("maps/void_siege.json"));
    vw::World world(data, map);
    std::vector<entt::entity> squad;
    for (int i = 0; i < 6; ++i)
        squad.push_back(world.spawn(data.find(i % 2 ? "scout" : "tank"), 0, {66.0f + static_cast<float>(i % 3), 69.0f + static_cast<float>(i / 3)}, 0));
    auto& reg = world.registry();
    auto run = [&](glm::vec2 goal, float seconds) {
        world.apply(vw::MoveCommand{squad, goal, false});
        for (int t = 0; t < static_cast<int>(seconds * vw::kTickRate); ++t)
            world.step();
        int done = 0;
        for (const auto e : squad)
            done += reg.get<vw::Order>(e).type == vw::OrderType::None && glm::distance(reg.get<vw::Position>(e).value, goal) < 4.0f;
        return done;
    };
    CHECK(run({96.0f, 70.0f}, 15.0f) == 6);
    CHECK(run({70.0f, 71.0f}, 15.0f) == 6);
    CHECK(run({82.0f, 70.0f}, 15.0f) == 6);
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

TEST_CASE("bench pathing" * doctest::skip()) {
    const auto map = vw::Map::parse(readData("maps/void_siege.json"));
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        vw::FlowField field(map, {12.0f + static_cast<float>(i), 88.0f});
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        CHECK_FALSE(vw::findPath(map, {30.0f, 88.0f}, {300.0f, 31.0f + static_cast<float>(i)}).empty());
    auto t2 = std::chrono::steady_clock::now();
    MESSAGE("field ms " << std::chrono::duration<double, std::milli>(t1 - t0).count() / 20 << " path ms "
                        << std::chrono::duration<double, std::milli>(t2 - t1).count() / 20);
}

TEST_CASE("queued orders, patrol, hold and inertia") {
    const auto data = vw::GameData::parse(kUnits);
    vw::Map map(40, 40);
    vw::World world(data, map);
    auto& reg = world.registry();
    const auto u = world.spawn(data.find("soldier"), 0, {5.5f, 5.5f}, 0);
    auto pos = [&] { return reg.get<vw::Position>(u).value; };
    auto run = [&](float s) {
        for (int i = 0; i < static_cast<int>(s * vw::kTickRate); ++i)
            world.step();
    };

    world.apply(vw::MoveCommand{{u}, {15.5f, 5.5f}});
    world.step();
    CHECK(reg.get<vw::Motion>(u).speed < 1.0f);
    world.apply(vw::MoveCommand{{u}, {15.5f, 15.5f}, false, true});
    world.apply(vw::MoveCommand{{u}, {5.5f, 15.5f}, false, true});
    CHECK(reg.get<vw::OrderQueue>(u).items.size() == 2);
    run(12.0f);
    CHECK(glm::distance(pos(), glm::vec2{5.5f, 15.5f}) < 0.6f);
    CHECK(reg.get<vw::Order>(u).type == vw::OrderType::None);

    world.apply(vw::MoveCommand{{u}, {15.5f, 15.5f}, false, false, true});
    run(4.0f);
    CHECK(reg.get<vw::Order>(u).type == vw::OrderType::Patrol);
    CHECK(glm::distance(reg.get<vw::Order>(u).point, glm::vec2{5.5f, 15.5f}) < 0.6f);

    world.apply(vw::StopCommand{{u}, true});
    const auto enemy = world.spawn(data.find("soldier"), 1, {pos().x + 6.0f, pos().y}, -1);
    run(2.0f);
    CHECK(reg.get<vw::Order>(u).type == vw::OrderType::Hold);
    CHECK(glm::distance(reg.get<vw::Position>(enemy).value, pos()) < 3.0f);
}

TEST_CASE("damage types, shields and splash") {
    const auto data = vw::GameData::parse(R"({"units":[
        {"id":"mg","maxHp":100,"radius":0.5,"speed":4,"sight":8,"weapon":{"type":"kinetic","damage":10,"range":5,"cooldown":0.5}},
        {"id":"rocket","maxHp":100,"radius":0.5,"speed":4,"sight":8,"weapon":{"type":"explosive","damage":20,"range":6,"cooldown":1,"splash":2}},
        {"id":"light","maxHp":1000,"radius":0.4,"armorType":"light"},
        {"id":"heavy","maxHp":1000,"radius":0.7,"armorType":"heavy"},
        {"id":"shielded","maxHp":1000,"radius":0.5,"armorType":"crystal","shield":50,"shieldRegen":25}
    ]})");
    vw::Map map(40, 40);
    vw::World world(data, map);
    auto& reg = world.registry();
    const auto light = world.spawn(data.find("light"), 1, {10.5f, 10.5f});
    const auto heavy = world.spawn(data.find("heavy"), 1, {20.5f, 20.5f});
    const auto shielded = world.spawn(data.find("shielded"), 1, {30.5f, 30.5f});
    world.damage(light, 100.0f, -1, entt::null, vw::DamageType::Kinetic);
    world.damage(heavy, 100.0f, -1, entt::null, vw::DamageType::Kinetic);
    CHECK(reg.get<vw::Health>(light).hp == doctest::Approx(875.0f));
    CHECK(reg.get<vw::Health>(heavy).hp == doctest::Approx(940.0f));

    world.damage(shielded, 30.0f, -1, entt::null, vw::DamageType::Energy);
    CHECK(reg.get<vw::Health>(shielded).shield == doctest::Approx(8.0f));
    CHECK(reg.get<vw::Health>(shielded).hp == doctest::Approx(1000.0f));
    world.damage(shielded, 100.0f, -1, entt::null, vw::DamageType::Emp);
    CHECK(reg.get<vw::Health>(shielded).shield == doctest::Approx(0.0f));
    for (int i = 0; i < vw::kTickRate * 6; ++i)
        world.step();
    CHECK(reg.get<vw::Health>(shielded).shield == doctest::Approx(50.0f));

    const auto a = world.spawn(data.find("light"), 1, {5.0f, 30.0f});
    const auto b = world.spawn(data.find("light"), 1, {5.0f, 31.2f});
    world.spawn(data.find("rocket"), 0, {5.0f, 26.0f});
    for (int i = 0; i < vw::kTickRate * 2; ++i)
        world.step();
    CHECK(reg.get<vw::Health>(a).hp < 1000.0f);
    CHECK(reg.get<vw::Health>(b).hp < 1000.0f);
}

TEST_CASE("air units, anti-air and transports") {
    const auto data = vw::GameData::parse(R"({"units":[
        {"id":"tank","maxHp":300,"radius":0.6,"speed":4,"sight":8,"weapon":{"damage":20,"range":5,"cooldown":0.5}},
        {"id":"aa","maxHp":300,"radius":0.5,"speed":0,"sight":8,"weapon":{"damage":20,"range":6,"cooldown":0.5,"targets":"both"}},
        {"id":"heli","air":true,"maxHp":200,"radius":0.6,"speed":6,"sight":8},
        {"id":"lifter","air":true,"transport":2,"maxHp":200,"radius":1,"speed":6,"sight":8}
    ]})");
    vw::Map map(40, 40);
    map.addWall({18, 0, 2, 40});
    vw::World world(data, map);
    auto& reg = world.registry();
    auto run = [&](float s) {
        for (int i = 0; i < static_cast<int>(s * vw::kTickRate); ++i)
            world.step();
    };

    const auto heli = world.spawn(data.find("heli"), 0, {10.5f, 10.5f}, 0);
    world.apply(vw::MoveCommand{{heli}, {30.5f, 10.5f}});
    run(6.0f);
    CHECK(glm::distance(reg.get<vw::Position>(heli).value, glm::vec2{30.5f, 10.5f}) < 1.0f);

    const auto tank = world.spawn(data.find("tank"), 1, {33.5f, 10.5f});
    const auto aa = world.spawn(data.find("aa"), 1, {30.5f, 14.5f});
    run(2.0f);
    CHECK(reg.get<vw::Target>(tank).value != heli);
    CHECK(reg.get<vw::Target>(aa).value == heli);
    CHECK(reg.get<vw::Health>(heli).hp < 200.0f);
    reg.destroy(aa);
    reg.destroy(tank);

    const auto lifter = world.spawn(data.find("lifter"), 0, {5.5f, 30.5f}, 0);
    const auto a = world.spawn(data.find("tank"), 0, {8.5f, 30.5f}, 0);
    const auto b = world.spawn(data.find("tank"), 0, {8.5f, 33.5f}, 0);
    const auto c = world.spawn(data.find("tank"), 0, {8.5f, 27.5f}, 0);
    world.apply(vw::AttackCommand{{a, b, c}, lifter});
    run(3.0f);
    CHECK(reg.get<vw::Cargo>(lifter).units.size() == 2);
    CHECK(reg.all_of<vw::Carried>(a));
    CHECK(reg.all_of<vw::Carried>(b) + reg.all_of<vw::Carried>(c) == 1);
    world.apply(vw::UnloadCommand{{lifter}, {30.5f, 30.5f}});
    run(6.0f);
    CHECK(reg.get<vw::Cargo>(lifter).units.empty());
    CHECK_FALSE(reg.all_of<vw::Carried>(a));
    CHECK(glm::distance(reg.get<vw::Position>(a).value, glm::vec2{30.5f, 30.5f}) < 3.0f);
    CHECK_FALSE(map.blocked(vw::Map::cellOf(reg.get<vw::Position>(a).value)));

    world.apply(vw::AttackCommand{{a}, lifter});
    run(3.0f);
    REQUIRE(reg.all_of<vw::Carried>(a));
    reg.get<vw::Health>(lifter).hp = 0.0f;
    run(0.2f);
    CHECK_FALSE(reg.valid(a));
}

TEST_CASE("frost shots slow their target") {
    const auto data = vw::GameData::parse(R"({"units":[
        {"id":"frost","structure":true,"maxHp":500,"radius":1,"weapon":{"damage":5,"range":8,"cooldown":0.5,"projectileSpeed":20,"slow":{"move":0.5,"attack":0.75,"duration":3}}},
        {"id":"runner","maxHp":5000,"radius":0.5,"speed":4,"sight":2}
    ]})");
    vw::Map map(40, 40);
    vw::World world(data, map);
    auto& reg = world.registry();
    world.spawn(data.find("frost"), 0, {10.5f, 10.5f});
    const auto runner = world.spawn(data.find("runner"), 1, {15.5f, 10.5f});
    for (int i = 0; i < vw::kTickRate * 2; ++i)
        world.step();
    CHECK(reg.get<vw::Unit>(runner).speed == doctest::Approx(2.0f));
    reg.destroy(*reg.view<vw::Structure>().begin());
    for (int i = 0; i < vw::kTickRate * 4; ++i)
        world.step();
    CHECK(reg.get<vw::Unit>(runner).speed == doctest::Approx(4.0f));
}

TEST_CASE("map buildings and forests block their cells") {
    const auto map = vw::Map::parse(R"({"width":20,"height":20,"theme":"winter",
        "buildings":[{"poly":[[2,2],[8,2],[8,5],[2,5]],"levels":9,"kind":"panel"},{"poly":[[15.1,15.1],[15.4,15.1],[15.4,15.4]],"levels":1}],
        "areas":[{"kind":"forest","poly":[[10,10],[14,10],[14,14],[10,14]]},{"kind":"park","poly":[[0,10],[5,10],[5,15]]}],
        "paths":[{"kind":"road","width":1.5,"points":[[0,8],[19,8]]}]})");
    CHECK(map.theme() == "winter");
    CHECK(map.cellKind(4, 3) == vw::CellKind::Building);
    CHECK(map.blocked(4, 3));
    CHECK(map.cellKind(9, 3) == vw::CellKind::Open);
    CHECK(map.cellKind(15, 15) == vw::CellKind::Building);
    CHECK(map.cellKind(12, 12) == vw::CellKind::Forest);
    CHECK(map.cellKind(2, 12) == vw::CellKind::Open);
    CHECK(map.buildings().size() == 2);
    CHECK(map.paths().size() == 1);
}

TEST_CASE("voronezh map loads with lanes on open ground") {
    const auto map = vw::Map::parse(readData("maps/voronezh_shilovo.json"));
    CHECK(map.buildings().size() > 100);
    for (const auto& lane : map.lanes())
        for (const auto p : lane.points)
            CHECK_FALSE(map.blocked(vw::Map::cellOf(p)));
}

TEST_CASE("map cells round-trip through the rle string") {
    auto map = vw::Map::parse(R"({"width":6,"height":3,"buildings":[{"poly":[[0,0],[2,0],[2,1],[0,1]]}]})");
    map.setCell(4, 1, vw::CellKind::Rock);
    map.setCell(5, 2, vw::CellKind::Forest);
    const auto copy = vw::Map::parse(std::string(R"({"width":6,"height":3,"cells":")") + map.encodeCells() + "\"}");
    CHECK(copy.cellKind(4, 1) == vw::CellKind::Rock);
    CHECK(copy.cellKind(5, 2) == vw::CellKind::Forest);
    CHECK(copy.cellKind(0, 0) == vw::CellKind::Open);
}
