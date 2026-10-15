#include <fstream>
#include <sstream>
#include <string>

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include "core/World.h"

using namespace vw;

namespace {

constexpr const char* kUnits = R"({"units":[
    {"id":"soldier","maxHp":200,"armor":1,"radius":0.5,"speed":4,"sight":8,"weapon":{"damage":20,"range":4,"cooldown":1},
     "skills":["blast","haste","mend","aura","drones"]},
    {"id":"grunt","bounty":10,"shards":1,"maxHp":100,"radius":0.5,"speed":0,"sight":6,"weapon":{"damage":5,"range":0.5,"cooldown":1}},
    {"id":"chief","bounty":50,"shards":3,"loot":["blade"],"maxHp":50,"radius":0.5,"speed":0,"sight":6},
    {"id":"drone","maxHp":50,"radius":0.3,"speed":5,"sight":6}
]})";

constexpr const char* kSkills = R"({"skills":[
    {"id":"blast","target":"point","range":6,"cooldown":5,"effects":[{"type":"damage","radius":2,"amount":80}]},
    {"id":"haste","target":"self","cooldown":10,"effects":[{"type":"buff","duration":2,"mods":{"speedMul":2}}]},
    {"id":"mend","target":"self","cooldown":10,"effects":[{"type":"heal","radius":5,"amount":50}]},
    {"id":"aura","passive":true,"effects":[{"type":"aura","radius":5,"who":"allies","mods":{"armor":3}}]},
    {"id":"drones","target":"self","cooldown":20,"effects":[{"type":"summon","unit":"drone","count":2,"duration":1}]}
]})";

constexpr const char* kItems = R"({"items":[
    {"id":"plate","cost":50,"mods":{"hp":100}},
    {"id":"blade","mods":{"damageMul":2}}
]})";

constexpr const char* kMap = R"({"width":40,"height":40,"playerStart":{"pos":[10,10],"units":["soldier","soldier"]},
    "defense":{"startEnergy":500,"waves":[]}})";

void run(World& world, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * kTickRate); ++i)
        world.step();
}

struct Fixture {
    GameData data = GameData::parse(kUnits, kSkills, kItems);
    Map map = Map::parse(kMap);
    World world{data, map};
    std::vector<entt::entity> units = world.spawnPlayer(0);

    Progression& prog(entt::entity e) { return world.registry().get<Progression>(e); }

    entt::entity makeHero() {
        const auto e = units[0];
        prog(e).xp = kHeroXp;
        world.apply(PromoteCommand{0, e});
        const_cast<PlayerState&>(world.player(0)).shards = kHeroShards;
        world.apply(PromoteCommand{0, e});
        return e;
    }
};

}

TEST_CASE_FIXTURE(Fixture, "xp from kills promotes to veteran") {
    const auto e = units[0];
    CHECK(prog(e).rank == Rank::Recruit);
    for (int i = 0; i < 6; ++i) {
        const auto g = world.spawn(data.find("grunt"), 1, {12.0f, 12.0f});
        world.registry().get<Health>(g).hp = 0.0f;
        world.registry().get<Health>(g).lastHitBy = 0;
        world.step();
    }
    CHECK(prog(e).rank == Rank::Veteran);
    CHECK(world.player(0).shards == 6);
    CHECK(world.registry().get<Health>(e).max == doctest::Approx(200.0f * 1.15f));
}

TEST_CASE_FIXTURE(Fixture, "promotion requires xp, energy and shards") {
    const auto e = units[0];
    world.apply(PromoteCommand{0, e});
    CHECK(prog(e).rank == Rank::Recruit);

    prog(e).xp = kEliteXp;
    world.apply(PromoteCommand{1, e});
    CHECK(prog(e).rank == Rank::Recruit);
    world.apply(PromoteCommand{0, e});
    CHECK(prog(e).rank == Rank::Elite);
    CHECK(world.player(0).energy == doctest::Approx(500.0f - kEliteCost));

    prog(e).xp = kHeroXp;
    world.apply(PromoteCommand{0, e});
    CHECK(prog(e).rank == Rank::Elite);
    const_cast<PlayerState&>(world.player(0)).shards = kHeroShards;
    world.apply(PromoteCommand{0, e});
    CHECK(prog(e).rank == Rank::Hero);
    CHECK(prog(e).level == 1);
    CHECK(prog(e).skillPoints == 1);
}

TEST_CASE_FIXTURE(Fixture, "hero levels up and learns skills") {
    const auto hero = makeHero();
    REQUIRE(prog(hero).rank == Rank::Hero);
    world.grantXp(hero, xpForNextLevel(1) + xpForNextLevel(2));
    CHECK(prog(hero).level == 3);
    CHECK(prog(hero).skillPoints == 3);

    const auto blast = data.findSkill("blast");
    world.apply(LearnCommand{0, hero, blast});
    world.apply(LearnCommand{0, hero, blast});
    CHECK(prog(hero).skills.size() == 1);
    CHECK(prog(hero).skills[0].level == 2);
    world.apply(LearnCommand{0, hero, data.findSkill("aura")});
    CHECK(prog(hero).skills.size() == 2);
    CHECK(prog(hero).skillPoints == 0);
    world.apply(LearnCommand{0, hero, data.findSkill("haste")});
    CHECK(prog(hero).skills.size() == 2);
}

TEST_CASE_FIXTURE(Fixture, "skills damage, buff, heal and summon") {
    const auto hero = makeHero();
    prog(hero).skillPoints = 4;
    for (const char* s : {"blast", "haste", "mend", "drones"})
        world.apply(LearnCommand{0, hero, data.findSkill(s)});
    REQUIRE(prog(hero).skills.size() == 4);

    const glm::vec2 heroPos = world.registry().get<Position>(hero).value;
    const auto grunt = world.spawn(data.find("grunt"), 1, heroPos + glm::vec2{5.0f, 0.0f});
    world.registry().get<Weapon>(grunt).def.damage = 0.0f;
    world.apply(CastCommand{0, hero, 0, heroPos + glm::vec2{5.0f, 0.0f}});
    CHECK(world.registry().get<Health>(grunt).hp < 30.0f);
    world.step();
    bool cast = false;
    for (const auto& ev : world.events())
        cast |= ev.type == EventType::SkillCast;
    CHECK(cast);
    CHECK(prog(hero).skills[0].cooldown > 0.0f);

    const float before = world.registry().get<Unit>(hero).speed;
    world.apply(CastCommand{0, hero, 1, heroPos});
    CHECK(world.registry().get<Unit>(hero).speed == doctest::Approx(before * 2.0f));
    run(world, 2.5f);
    CHECK(world.registry().get<Unit>(hero).speed == doctest::Approx(before));

    world.registry().get<Health>(units[1]).hp = 10.0f;
    world.apply(CastCommand{0, hero, 2, heroPos});
    CHECK(world.registry().get<Health>(units[1]).hp > 50.0f);

    world.apply(CastCommand{0, hero, 3, heroPos});
    int drones = 0;
    for (auto [e, unit] : world.registry().view<const Unit>().each())
        drones += unit.def == data.find("drone");
    CHECK(drones == 2);
    run(world, 1.5f);
    drones = 0;
    for (auto [e, unit] : world.registry().view<const Unit>().each())
        drones += unit.def == data.find("drone");
    CHECK(drones == 0);
}

TEST_CASE_FIXTURE(Fixture, "aura buffs nearby allies") {
    const auto hero = makeHero();
    world.apply(LearnCommand{0, hero, data.findSkill("aura")});
    const float armor = world.registry().get<Health>(units[1]).armor;
    run(world, 0.6f);
    CHECK(world.registry().get<Health>(units[1]).armor == doctest::Approx(armor + 3.0f));
}

TEST_CASE_FIXTURE(Fixture, "shop, stash and equipment") {
    const auto hero = makeHero();
    world.apply(BuyCommand{0, data.findItem("plate")});
    world.apply(BuyCommand{0, data.findItem("blade")});
    REQUIRE(world.player(0).stash.size() == 1);

    const float hp = world.registry().get<Health>(hero).max;
    world.apply(EquipCommand{0, hero, 0});
    CHECK(world.player(0).stash.empty());
    CHECK(world.registry().get<Health>(hero).max > hp + 90.0f);

    world.apply(EquipCommand{0, units[1], 0});
    world.apply(UnequipCommand{0, hero, 0});
    CHECK(world.player(0).stash.size() == 1);
    CHECK(world.registry().get<Health>(hero).max == doctest::Approx(hp));
}

TEST_CASE_FIXTURE(Fixture, "boss loot goes to the killer") {
    const auto chief = world.spawn(data.find("chief"), 1, {20.0f, 20.0f});
    world.registry().get<Health>(chief).hp = 0.0f;
    world.registry().get<Health>(chief).lastHitBy = 0;
    world.step();
    REQUIRE(world.player(0).stash.size() == 1);
    CHECK(world.player(0).stash[0] == data.findItem("blade"));
    CHECK(world.player(0).shards == 3);
}

TEST_CASE_FIXTURE(Fixture, "dead hero respawns with progression") {
    const auto hero = makeHero();
    world.grantXp(hero, xpForNextLevel(1));
    world.registry().get<Health>(hero).hp = 0.0f;
    world.step();
    CHECK_FALSE(world.registry().valid(hero));
    CHECK(world.heroCount(0) == 1);
    run(world, 15.0f);
    int heroes = 0;
    for (auto [e, p] : world.registry().view<const Progression>().each())
        if (p.rank == Rank::Hero) {
            ++heroes;
            CHECK(p.level == 2);
        }
    CHECK(heroes == 1);
}

TEST_CASE("refit elite vehicles into classes") {
    auto read = [](const std::string& rel) {
        std::ifstream f(std::string(VW_DATA_DIR) + "/" + rel, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const auto data = GameData::parse(read("units.json"), read("skills.json"), read("items.json"));
    Map map(60, 60);
    World world(data, map);
    world.spawnPlayer(0);
    auto& reg = world.registry();
    const auto tank = world.spawn(data.find("tank"), 0, {20.5f, 20.5f}, 0);
    world.devBoost(0);
    CHECK(data.refits(data.find("tank")).size() == 5);

    world.apply(RefitCommand{0, tank, data.find("tank_rocket")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank"));
    world.apply(PromoteCommand{0, tank});
    REQUIRE(reg.get<Progression>(tank).rank == Rank::Elite);
    const float energy = world.player(0).energy;
    world.apply(RefitCommand{0, tank, data.find("scout_emp")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank"));
    world.apply(RefitCommand{0, tank, data.find("tank_rocket")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank_rocket"));
    CHECK(reg.get<Weapon>(tank).def.type == DamageType::Explosive);
    CHECK(reg.get<Weapon>(tank).def.splash > 0.0f);
    CHECK(world.player(0).energy == doctest::Approx(energy - data.unit(data.find("tank_rocket")).refitCost));
    CHECK(reg.get<Progression>(tank).rank == Rank::Elite);

    world.apply(RefitCommand{0, tank, data.find("tank_repair")});
    REQUIRE(reg.get<Unit>(tank).def == data.find("tank_repair"));
    const auto tower = world.spawn(data.find("gun_tower"), 0, {23.5f, 20.5f}, 0);
    reg.get<Health>(tower).hp = 100.0f;
    for (int i = 0; i < kTickRate * 3; ++i)
        world.step();
    CHECK(reg.get<Health>(tower).hp > 130.0f);

    world.apply(RefitCommand{0, tank, data.find("tank_mechanic")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank_repair"));
    reg.get<Progression>(tank).rank = Rank::Hero;
    world.apply(RefitCommand{0, tank, data.find("tank_siege")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank_repair"));
    world.apply(RefitCommand{0, tank, data.find("tank_mechanic")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank_mechanic"));
    CHECK(data.refitTier(data.find("tank_mechanic")) == 2);
    world.apply(RefitCommand{0, tank, data.find("tank_radar")});
    CHECK(reg.get<Unit>(tank).def == data.find("tank_mechanic"));
    reg.get<Unit>(tank).def = data.find("tank_repair");
    world.apply(RefitCommand{0, tank, data.find("tank_radar")});
    const auto ally = world.spawn(data.find("tank"), 0, {22.5f, 22.5f}, 0);
    for (int i = 0; i < kTickRate; ++i)
        world.step();
    CHECK(reg.get<Weapon>(ally).def.range > data.unit(data.find("tank")).weapon->range);
}

TEST_CASE("vehicle modules") {
    auto read = [](const std::string& rel) {
        std::ifstream f(std::string(VW_DATA_DIR) + "/" + rel, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const auto data = GameData::parse(read("units.json"), read("skills.json"), read("items.json"));
    REQUIRE(data.modules().size() >= 6);
    Map map(60, 60);
    World world(data, map);
    world.spawnPlayer(0);
    auto& reg = world.registry();
    const auto tank = world.spawn(data.find("tank"), 0, {20.5f, 20.5f}, 0);
    world.devBoost(0);
    const float hp = reg.get<Health>(tank).max;

    world.apply(ModuleCommand{0, tank, data.findModule("armor_plates")});
    CHECK(reg.get<Modules>(tank).slots[0] == 0xffff);
    reg.get<Progression>(tank).rank = Rank::Veteran;
    world.apply(ModuleCommand{0, tank, data.findModule("armor_plates")});
    CHECK(reg.get<Modules>(tank).slots[0] == data.findModule("armor_plates"));
    CHECK(reg.get<Health>(tank).max > hp + 100.0f);
    world.apply(ModuleCommand{0, tank, data.findModule("shield_gen")});
    CHECK(reg.get<Health>(tank).shieldMax == doctest::Approx(0.0f));

    world.apply(PromoteCommand{0, tank});
    REQUIRE(reg.get<Progression>(tank).rank == Rank::Elite);
    world.apply(ModuleCommand{0, tank, data.findModule("shield_gen")});
    CHECK(reg.get<Health>(tank).shieldMax == doctest::Approx(160.0f));
    world.apply(ModuleCommand{0, tank, data.findModule("aa_missiles")});
    REQUIRE(reg.all_of<SecondaryWeapon>(tank));

    const auto heli = world.spawn(data.find("gunship"), 1, {25.5f, 20.5f});
    for (int i = 0; i < kTickRate * 3; ++i)
        world.step();
    CHECK(reg.get<Health>(heli).hp < reg.get<Health>(heli).max);
}

TEST_CASE("hero ultimate strikes after a telegraph") {
    auto read = [](const std::string& rel) {
        std::ifstream f(std::string(VW_DATA_DIR) + "/" + rel, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const auto data = GameData::parse(read("units.json"), read("skills.json"), read("items.json"));
    Map map(60, 60);
    World world(data, map);
    world.spawnPlayer(0);
    auto& reg = world.registry();
    const auto tank = world.spawn(data.find("tank"), 0, {10.5f, 10.5f}, 0);
    world.devBoost(0);
    world.apply(PromoteCommand{0, tank});
    world.apply(PromoteCommand{0, tank});
    REQUIRE(reg.get<Progression>(tank).rank == Rank::Hero);
    const auto ult = data.findSkill("artillery_strike");
    reg.get<Progression>(tank).skillPoints = 3;
    world.apply(LearnCommand{0, tank, ult});
    CHECK(reg.get<Progression>(tank).skills.empty());
    reg.get<Progression>(tank).level = 3;
    world.apply(LearnCommand{0, tank, ult});
    REQUIRE(reg.get<Progression>(tank).skills.size() == 1);

    const auto target = world.spawn(data.find("void_brute"), 1, {20.5f, 10.5f});
    reg.remove<Weapon>(target);
    const float hp0 = reg.get<Health>(target).hp + reg.get<Health>(target).shield;
    world.apply(CastCommand{0, tank, 0, {20.5f, 10.5f}});
    for (int i = 0; i < kTickRate; ++i)
        world.step();
    CHECK(reg.get<Health>(target).hp + reg.get<Health>(target).shield == doctest::Approx(hp0));
    for (int i = 0; i < kTickRate; ++i)
        world.step();
    CHECK(reg.get<Health>(target).hp + reg.get<Health>(target).shield < hp0 - 150.0f);
}
