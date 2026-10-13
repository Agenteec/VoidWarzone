#include <doctest/doctest.h>
#include <glm/geometric.hpp>

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
    run(world, 0.2f);
    CHECK(world.defense().state == MatchState::Defeat);
}
