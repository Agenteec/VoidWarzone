#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <string>

#include "core/World.h"

using namespace vw;

namespace {

constexpr const char* kUnits = R"({"units":[
    {"id":"core","structure":true,"maxHp":1000,"radius":1},
    {"id":"portal","structure":true,"maxHp":1000,"radius":1},
    {"id":"grunt","bounty":10,"maxHp":100,"radius":0.5,"speed":3,"sight":6,"weapon":{"damage":10,"range":0.5,"cooldown":1}},
    {"id":"soldier","cost":50,"maxHp":200,"radius":0.5,"speed":4,"sight":8,"weapon":{"damage":30,"range":4,"cooldown":0.5}},
    {"id":"boss","maxHp":5000,"radius":1,"speed":0,"sight":10,
     "boss":{"abilities":[{"type":"slam","radius":4,"damage":60,"cooldown":2,"windup":0.5}],"enrage":{"hpBelow":0.5,"speed":2}}}
]})";

constexpr const char* kMap = R"({"width":40,"height":40,
    "lanes":[{"name":"mid","points":[[5,5],[35,35]]}],
    "units":[{"unit":"core","role":"core","team":0,"pos":[5,5]},{"unit":"portal","role":"portal","team":1,"pos":[35,35]}],
    "playerStart":{"pos":[10,5],"units":["soldier"]},
    "resourcePoints":[{"name":"A","pos":[20,5],"radius":3,"income":10}],
    "defense":{"firstWaveDelay":1,"waveInterval":2,"baseIncome":0,"startEnergy":60,
      "waves":[{"groups":[{"unit":"grunt","count":2,"lanes":"all"}]},{"groups":[{"unit":"grunt","count":1,"lanes":"raid"}]}]}
})";

void run(World& world, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * kTickRate); ++i)
        world.step();
}

int countDef(const World& world, const GameData& data, const char* id) {
    const auto def = data.find(id);
    int n = 0;
    for (auto [e, unit] : world.registry().view<const Unit>().each())
        n += unit.def == def;
    return n;
}

}

TEST_CASE("waves spawn on schedule") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(kMap);
    World world(data, map);
    CHECK(world.defense().totalWaves == 2);
    CHECK(world.registry().all_of<Invulnerable>(entt::entity{1}));

    run(world, 1.1f);
    CHECK(world.defense().wave == 1);
    CHECK(countDef(world, data, "grunt") == 2);

    run(world, 2.0f);
    CHECK(world.defense().wave == 2);
    CHECK_FALSE(world.registry().all_of<Invulnerable>(entt::entity{1}));
}

TEST_CASE("waves come out of the portal and pylons keep it shielded") {
    std::string units = kUnits;
    units.replace(units.rfind("]}"), 2, R"(,{"id":"pylon","structure":true,"anchor":true,"maxHp":100,"radius":0.5}]})");
    std::string mapText = kMap;
    const std::string portal = R"("pos":[35,35]})";
    mapText.replace(mapText.find(portal), portal.size(), portal + R"(,{"unit":"pylon","team":1,"pos":[30,36]})");
    const auto data = GameData::parse(units);
    const auto map = Map::parse(mapText);
    World world(data, map);
    CHECK(world.anchorsAlive() == 1);
    const auto mouth = world.portalMouth();
    REQUIRE(mouth);

    run(world, 1.1f);
    REQUIRE(world.defense().wave == 1);
    for (auto [e, unit, pos] : world.registry().view<const Unit, const Position>().each())
        if (unit.def == data.find("grunt"))
            CHECK(glm::distance(pos.value, *mouth) < 3.0f);

    run(world, 2.0f);
    CHECK(world.defense().wave == 2);
    CHECK(world.registry().all_of<Invulnerable>(entt::entity{1}));

    for (auto [e, unit] : world.registry().view<const Unit>().each())
        if (unit.def == data.find("pylon"))
            world.damage(e, 1000.0f, -1, entt::null);
    run(world, 0.2f);
    CHECK(world.anchorsAlive() == 0);
    CHECK_FALSE(world.registry().all_of<Invulnerable>(entt::entity{1}));
}

TEST_CASE("recruit spends energy") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(kMap);
    World world(data, map);
    world.spawnPlayer(0);
    CHECK(world.player(0).energy == doctest::Approx(60.0f));

    world.apply(RecruitCommand{0, data.find("soldier")});
    CHECK(world.player(0).energy == doctest::Approx(10.0f));
    CHECK(countDef(world, data, "soldier") == 2);

    world.apply(RecruitCommand{0, data.find("soldier")});
    CHECK(countDef(world, data, "soldier") == 2);
    world.apply(RecruitCommand{0, data.find("grunt")});
    CHECK(countDef(world, data, "grunt") == 0);
}

TEST_CASE("resource point capture gives income") {
    const auto data = GameData::parse(kUnits);
    auto map = Map::parse(kMap);
    World world(data, map);
    const auto units = world.spawnPlayer(0);
    world.apply(MoveCommand{units, {20.0f, 5.0f}});
    run(world, 1.0f);
    const float before = world.player(0).energy;
    run(world, 25.0f);
    CHECK(world.resourcePoints()[0].owner == 0);
    CHECK(world.player(0).energy > before + 10.0f);
}

TEST_CASE("boss slams and enrages") {
    const auto data = GameData::parse(kUnits);
    vw::Map map(30, 30);
    World world(data, map);
    const auto boss = world.spawn(data.find("boss"), 1, {15.0f, 15.0f});
    const auto victim = world.spawn(data.find("grunt"), 0, {17.0f, 15.0f});
    world.registry().get<Weapon>(victim).def.damage = 0.0f;

    bool warned = false;
    bool slammed = false;
    for (int i = 0; i < kTickRate * 3; ++i) {
        world.step();
        for (const auto& ev : world.events()) {
            warned |= ev.type == EventType::SlamWarn;
            slammed |= ev.type == EventType::Slam;
        }
    }
    CHECK(warned);
    CHECK(slammed);
    CHECK(world.registry().get<Health>(victim).hp < 100.0f);

    world.registry().get<Health>(boss).hp = 1000.0f;
    world.step();
    CHECK(world.registry().get<Boss>(boss).enraged);
}

TEST_CASE("losing the core ends the match") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(kMap);
    World world(data, map);
    world.registry().get<Health>(entt::entity{0}).hp = 0.0f;
    world.spawnPlayer(0);
    run(world, 0.2f);
    CHECK(world.defense().state == MatchState::Defeat);

    world.apply(RecruitCommand{0, data.find("soldier")});
    CHECK(countDef(world, data, "soldier") == 2);
    run(world, 1.0f);
    CHECK(world.defense().state == MatchState::Defeat);
}

namespace {

constexpr const char* kArenaUnits = R"({"units":[
    {"id":"core","structure":true,"maxHp":1000,"radius":1},
    {"id":"grunt","bounty":10,"maxHp":100,"radius":0.5,"speed":3,"sight":6,"weapon":{"damage":10,"range":0.5,"cooldown":1}},
    {"id":"soldier","cost":50,"maxHp":200,"radius":0.5,"speed":4,"sight":8,"weapon":{"damage":30,"range":4,"cooldown":0.5}},
    {"id":"engineer","cost":30,"maxHp":100,"radius":0.4,"speed":5,"sight":6,"builder":true},
    {"id":"wall","structure":true,"buildable":true,"wall":true,"priority":0.2,"buildTime":2,"cost":10,"maxHp":60,"radius":0.5},
    {"id":"factory","structure":true,"buildable":true,"buildTime":6,"cost":40,"maxHp":500,"radius":1,"produce":{"unit":"soldier","interval":1,"max":2}}
]})";

constexpr const char* kArenaMap = R"({"width":30,"height":30,
    "walls":[[0,14,13,2],[17,14,13,2]],
    "lanes":[{"name":"gate","points":[[15,25],[15,3]]}],
    "units":[{"unit":"core","role":"core","team":0,"pos":[15,25]}],
    "playerStart":{"pos":[20,25],"units":[]},
    "defense":{"firstWaveDelay":100,"waveInterval":100,"baseIncome":0,"startEnergy":100,
      "waves":[{"groups":[{"unit":"grunt","count":1,"lanes":"all"}]}]}
})";

}

TEST_CASE("engineers build structures") {
    const auto data = GameData::parse(kArenaUnits);
    const auto map = Map::parse(kArenaMap);
    CHECK(data.buildable().size() == 2);
    CHECK(data.recruitable().size() == 2);

    World world(data, map);
    world.spawnPlayer(0);
    const auto eng = world.spawn(data.find("engineer"), 0, {20.5f, 20.5f}, 0);
    const auto soldier = world.spawn(data.find("soldier"), 0, {21.5f, 22.5f}, 0);
    world.apply(BuildCommand{{soldier}, data.find("wall"), {20.2f, 5.7f}});
    CHECK(world.registry().get<Order>(soldier).type == OrderType::None);

    world.apply(BuildCommand{{eng}, data.find("wall"), {20.2f, 5.7f}});
    world.apply(BuildCommand{{eng}, data.find("factory"), {8.5f, 20.5f}, true});
    CHECK(world.registry().get<Order>(eng).type == OrderType::Build);
    CHECK(world.registry().get<OrderQueue>(eng).items.size() == 1);
    run(world, 6.0f);
    CHECK(countDef(world, data, "wall") == 1);
    CHECK(world.player(0).energy == doctest::Approx(90.0f));
    run(world, 10.0f);
    CHECK(countDef(world, data, "factory") == 1);
    CHECK(world.player(0).energy == doctest::Approx(50.0f));
    run(world, 12.0f);
    CHECK(world.registry().get<Order>(eng).type == OrderType::None);
    CHECK(world.registry().view<Construction>().size() == 0);
    run(world, 2.5f);
    CHECK(countDef(world, data, "soldier") >= 2);

    world.apply(BuildCommand{{eng}, data.find("wall"), {5.5f, 14.5f}});
    world.apply(BuildCommand{{eng}, data.find("factory"), {9.5f, 20.5f}});
    CHECK(world.registry().get<Order>(eng).type == OrderType::None);
}

TEST_CASE("follow an ally and target priority") {
    const auto data = GameData::parse(kArenaUnits);
    const auto map = Map::parse(kArenaMap);
    World world(data, map);
    const auto leader = world.spawn(data.find("soldier"), 0, {20.5f, 20.5f}, 1);
    const auto escort = world.spawn(data.find("engineer"), 0, {25.5f, 25.5f}, 0);
    world.apply(AttackCommand{{escort}, leader});
    CHECK(world.registry().get<Order>(escort).type == OrderType::Follow);
    world.apply(MoveCommand{{leader}, {20.5f, 5.5f}});
    run(world, 8.0f);
    CHECK(glm::distance(world.registry().get<Position>(escort).value, world.registry().get<Position>(leader).value) < 3.5f);

    const auto wall = world.spawn(data.find("wall"), 0, {5.5f, 5.5f}, 0);
    const auto grunt = world.spawn(data.find("soldier"), 0, {7.0f, 5.5f}, 0);
    const auto enemy = world.spawn(data.find("soldier"), 1, {6.0f, 7.5f}, -1);
    run(world, 0.3f);
    CHECK(world.registry().get<Target>(enemy).value == grunt);
    static_cast<void>(wall);
}

TEST_CASE("arena creeps break through a walled gate") {
    const auto data = GameData::parse(kArenaUnits);
    const auto map = Map::parse(kArenaMap);
    World world(data, map, Difficulty::Normal, GameMode::Arena);
    world.spawnPlayer(0);
    for (int x = 13; x < 17; ++x)
        world.spawn(data.find("wall"), 0, {static_cast<float>(x) + 0.5f, 14.5f}, 0);
    CHECK(countDef(world, data, "wall") == 4);
    world.spawnOnLane(data.find("grunt"), 1, 0, 1, 0.0f);
    run(world, 20.0f);
    CHECK(countDef(world, data, "wall") < 4);
}

TEST_CASE("queued builds respect phantoms and resume after interruption") {
    const auto data = GameData::parse(kArenaUnits);
    const auto map = Map::parse(kArenaMap);
    World world(data, map);
    world.spawnPlayer(0);
    world.devBoost(0);
    const auto eng = world.spawn(data.find("engineer"), 0, {20.5f, 20.5f}, 0);
    for (int x = 0; x < 3; ++x)
        world.apply(BuildCommand{{eng}, data.find("wall"), {18.5f + static_cast<float>(x), 8.5f}, true});
    world.apply(BuildCommand{{eng}, data.find("factory"), {22.5f, 10.5f}, true});
    world.apply(BuildCommand{{eng}, data.find("factory"), {23.5f, 10.5f}, true});
    CHECK(world.registry().get<OrderQueue>(eng).items.size() == 3);
    run(world, 20.0f);
    CHECK(countDef(world, data, "wall") == 3);
    CHECK(countDef(world, data, "factory") == 1);
    CHECK(world.registry().view<Construction>().size() == 0);

    world.apply(BuildCommand{{eng}, data.find("factory"), {8.5f, 20.5f}});
    run(world, 7.0f);
    REQUIRE(countDef(world, data, "factory") == 2);
    CHECK(world.registry().view<Construction>().size() == 1);
    world.apply(MoveCommand{{eng}, {25.5f, 25.5f}});
    run(world, 4.0f);
    const auto site = *world.registry().view<Construction>().begin();
    world.apply(AttackCommand{{eng}, site});
    CHECK(world.registry().get<Order>(eng).type == OrderType::Build);
    run(world, 12.0f);
    CHECK(world.registry().view<Construction>().size() == 0);

    const auto blocker = world.spawn(data.find("soldier"), 0, {14.5f, 20.5f}, 0);
    world.apply(BuildCommand{{eng}, data.find("wall"), {14.5f, 20.5f}});
    run(world, 8.0f);
    CHECK(glm::distance(world.registry().get<Position>(blocker).value, glm::vec2{14.5f, 20.5f}) > 1.0f);
    CHECK(countDef(world, data, "wall") == 4);
}
