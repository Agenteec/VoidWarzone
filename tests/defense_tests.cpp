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

TEST_CASE("save and load restore the match") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(kMap);
    World world(data, map);
    world.spawnPlayer(0);
    run(world, 4.0f);
    const std::string file = makeSaveFile({"test", GameMode::Defense, Difficulty::Normal, world.tick()}, world);
    CHECK(readSaveHeader(file).map == "test");

    World loaded(data, map);
    loaded.loadState(file);
    CHECK(loaded.tick() == world.tick());
    CHECK(loaded.defense().wave == world.defense().wave);
    CHECK(countDef(loaded, data, "grunt") == countDef(world, data, "grunt"));
    CHECK(countDef(loaded, data, "soldier") == countDef(world, data, "soldier"));
    CHECK(loaded.player(0).energy == doctest::Approx(world.player(0).energy));
    CHECK(loaded.resumePlayer(0));
    CHECK_FALSE(loaded.resumePlayer(1));
    CHECK(loaded.player(0).active);
    run(loaded, 2.0f);
    CHECK(countDef(loaded, data, "core") == 1);
    CHECK(validSaveName("save_20261017-1200"));
    CHECK_FALSE(validSaveName("../evil"));
}

TEST_CASE("mission objectives and triggers drive the match") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(R"({"width":40,"height":40,
        "playerStart":{"pos":[5,5],"units":["soldier"]},
        "units":[{"unit":"grunt","team":1,"pos":[30,30],"tag":"camp"}],
        "defense":{"startEnergy":10,"baseIncome":0,"waves":[]},
        "mission":{"objectives":[
            {"id":"go","type":"reach","pos":[5,5],"radius":3},
            {"id":"camp","type":"destroy","tag":"camp","hidden":true}],
          "triggers":[
            {"when":"start","do":[{"type":"message","text":"hello"}]},
            {"when":"objective","objective":"go","do":[{"type":"reveal","objective":"camp"},{"type":"energy","amount":50},
              {"type":"spawn","unit":"grunt","count":2,"pos":[20,20],"tag":"camp"}]}]}})");
    World world(data, map);
    world.spawnPlayer(0);
    run(world, 1.0f);
    CHECK(world.takeMessages() == std::vector<std::string>{"hello"});
    REQUIRE(world.objectives().size() == 2);
    CHECK(world.objectives()[0] == 2);
    CHECK(world.objectives()[1] == 1);
    CHECK(world.player(0).energy == doctest::Approx(60.0f));
    CHECK(countDef(world, data, "grunt") == 3);
    CHECK(world.defense().state == MatchState::Playing);
    std::vector<entt::entity> grunts;
    for (auto [e, unit] : world.registry().view<const Unit>().each())
        if (unit.def == data.find("grunt"))
            grunts.push_back(e);
    for (const auto e : grunts)
        world.registry().get<Health>(e).hp = 0.0f;
    run(world, 1.0f);
    CHECK(world.objectives()[1] == 2);
    CHECK(world.defense().state == MatchState::Victory);

    const std::string roster = world.exportRoster(0, 12);
    World next(data, map);
    next.spawnPlayer(0);
    CHECK(next.spawnRoster(0, roster).size() == 1);
    CHECK(countDef(next, data, "soldier") == 2);
}

TEST_CASE("mobile hq deploys, packs up and gates production") {
    const auto data = GameData::parse(R"({"units":[
        {"id":"kshm","maxHp":1000,"radius":0.8,"speed":3,"deploy":"hq"},
        {"id":"hq","structure":true,"buildable":true,"deployOnly":true,"maxHp":4000,"radius":2,"undeploy":"kshm"},
        {"id":"truck","cost":50,"maxHp":200,"radius":0.5,"speed":4,"builder":true,"producedAt":["workshop"]},
        {"id":"workshop","structure":true,"buildable":true,"cost":100,"maxHp":2000,"radius":2,"buildTime":2},
        {"id":"lab","structure":true,"buildable":true,"cost":100,"maxHp":2000,"radius":1.5,"buildTime":2},
        {"id":"shop","structure":true,"buildable":true,"cost":100,"maxHp":2000,"radius":2,"buildTime":2,"requires":"lab"}
    ]})");
    const auto map = Map::parse(R"({"width":60,"height":60,"playerStart":{"pos":[10,10],"units":["kshm","truck"]},
        "defense":{"startEnergy":1000,"baseIncome":0,"waves":[]},
        "mission":{"objectives":[{"id":"x","type":"survive","time":9999}]}})");
    World world(data, map);
    world.spawnPlayer(0);
    run(world, 0.2f);
    auto find = [&](const char* id) {
        for (auto [e, unit] : world.registry().view<const Unit>().each())
            if (unit.def == data.find(id))
                return e;
        return entt::entity{entt::null};
    };
    auto exists = [&](const char* id) { return find(id) != entt::null; };
    CHECK(data.buildable().size() == 3);
    const auto truck = find("truck");
    world.apply(RecruitCommand{0, data.find("truck")});
    CHECK(countDef(world, data, "truck") == 1);
    CHECK(world.checkSite(data.find("workshop"), {45.5f, 45.5f}, 0, false) == SiteStatus::Ok);

    world.apply(DeployCommand{0, {find("kshm")}});
    run(world, 0.2f);
    REQUIRE(exists("hq"));
    CHECK_FALSE(exists("kshm"));
    CHECK(world.checkSite(data.find("workshop"), {18.5f, 10.5f}, 0, false) == SiteStatus::Ok);

    world.apply(BuildCommand{{truck}, data.find("shop"), {18.5f, 16.5f}, false});
    run(world, 0.2f);
    CHECK(world.registry().get<Order>(truck).type != OrderType::Build);
    world.apply(BuildCommand{{truck}, data.find("workshop"), {18.5f, 10.5f}, false});
    run(world, 12.0f);
    REQUIRE(exists("workshop"));
    CHECK_FALSE(world.registry().all_of<Construction>(find("workshop")));
    const auto shop = find("workshop");
    world.apply(StructureCommand{0, shop, StructureOp::Rally, 0, {30.5f, 20.5f}});
    const float before = world.player(0).energy;
    world.apply(RecruitCommand{0, data.find("truck"), shop});
    world.apply(RecruitCommand{0, data.find("truck"), shop});
    CHECK(world.registry().get<Factory>(shop).queue.size() == 2);
    world.apply(StructureCommand{0, shop, StructureOp::Cancel, 1, {}});
    CHECK(world.player(0).energy == doctest::Approx(before - 50.0f));
    CHECK(countDef(world, data, "truck") == 1);
    run(world, 6.0f);
    CHECK(countDef(world, data, "truck") == 2);
    CHECK(world.registry().get<Factory>(shop).queue.empty());
    world.apply(StructureCommand{0, shop, StructureOp::Demolish, 0, {}});
    run(world, 0.5f);
    CHECK_FALSE(exists("workshop"));

    world.apply(DeployCommand{0, {find("hq")}});
    run(world, 0.2f);
    CHECK_FALSE(exists("hq"));
    CHECK(exists("kshm"));
}

TEST_CASE("labs research upgrades and unlock units") {
    const auto data = GameData::parse(R"({"units":[
        {"id":"lab","structure":true,"lab":true,"maxHp":1000,"radius":1.5},
        {"id":"tank","cost":50,"maxHp":200,"radius":0.5,"speed":3,"weapon":{"type":"piercing","damage":20,"range":4,"cooldown":1}},
        {"id":"jeep","cost":30,"maxHp":100,"radius":0.5,"speed":5,"weapon":{"damage":10,"range":4,"cooldown":1}}
    ]})", {}, {}, R"({"topics":[
        {"id":"guns","cost":100,"time":2,"weapon":"piercing","mods":{"damageMul":1.5}},
        {"id":"armor","cost":100,"time":2,"applies":"vehicles","after":["guns"],"mods":{"hpMul":2},"unlocks":["tank"]}
    ]})");
    REQUIRE(data.research().size() == 2);
    CHECK(data.research(1).tier == 1);
    CHECK(data.unit(data.find("tank")).research == 1);
    const auto map = Map::parse(R"({"width":40,"height":40,"playerStart":{"pos":[10,10],"units":["jeep"]},
        "defense":{"startEnergy":1000,"baseIncome":0,"waves":[]},
        "mission":{"objectives":[{"id":"x","type":"survive","time":9999}]},
        "units":[{"unit":"lab","team":0,"owner":0,"pos":[20,20]}]})");
    World world(data, map);
    world.spawnPlayer(0);
    entt::entity lab = entt::null, jeep = entt::null;
    for (auto [e, unit] : world.registry().view<const Unit>().each())
        (unit.def == data.find("lab") ? lab : jeep) = e;
    REQUIRE(world.registry().all_of<Lab>(lab));
    world.registry().emplace_or_replace<Owner>(lab, std::uint8_t{0});

    world.apply(RecruitCommand{0, data.find("tank")});
    CHECK(countDef(world, data, "tank") == 0);
    world.apply(StructureCommand{0, lab, StructureOp::Research, 1, {}});
    CHECK(world.registry().get<Lab>(lab).topic == kInvalidId);
    world.apply(StructureCommand{0, lab, StructureOp::Research, 0, {}});
    CHECK(world.registry().get<Lab>(lab).topic == 0);
    CHECK(world.player(0).energy == doctest::Approx(900.0f));
    world.apply(StructureCommand{0, lab, StructureOp::Cancel, 0, {}});
    CHECK(world.player(0).energy == doctest::Approx(1000.0f));
    world.apply(StructureCommand{0, lab, StructureOp::Research, 0, {}});
    run(world, 2.2f);
    CHECK(world.researched(0, 0));
    world.apply(StructureCommand{0, lab, StructureOp::Research, 1, {}});
    run(world, 2.2f);
    CHECK(world.researched(0, 1));
    CHECK(world.registry().get<Health>(jeep).max == doctest::Approx(200.0f));
    world.apply(RecruitCommand{0, data.find("tank")});
    REQUIRE(countDef(world, data, "tank") == 1);
    for (auto [e, unit, w] : world.registry().view<const Unit, const Weapon>().each())
        if (unit.def == data.find("tank"))
            CHECK(w.def.damage == doctest::Approx(30.0f));
    CHECK(world.exportResearch(0) == R"(["guns","armor"])");

    World loaded(data, map);
    loaded.loadState(world.saveState());
    CHECK(loaded.researched(0, 1));
}

TEST_CASE("cheats work only when enabled") {
    const auto data = GameData::parse(kUnits);
    const auto map = Map::parse(kMap);
    World world(data, map);
    world.spawnPlayer(0);
    const float start = world.player(0).energy;
    world.apply(CheatCommand{0, CheatOp::Energy});
    CHECK(world.player(0).energy == doctest::Approx(start));
    world.setCheats(true);
    world.apply(CheatCommand{0, CheatOp::Energy});
    CHECK(world.player(0).energy == doctest::Approx(start + 1000.0f));
    world.apply(CheatCommand{0, CheatOp::Spawn, data.find("grunt"), 1, 3, {20.0f, 20.0f}});
    CHECK(countDef(world, data, "grunt") == 3);
    world.apply(CheatCommand{0, CheatOp::God});
    CHECK(world.player(0).god);
    world.apply(CheatCommand{0, CheatOp::Win});
    CHECK(world.defense().state == MatchState::Victory);
}

TEST_CASE("rocket salvos, guided missiles and kamikaze drones") {
    const auto data = GameData::parse(R"({"units":[
        {"id":"dummy","maxHp":5000,"radius":0.5,"speed":0},
        {"id":"grad","maxHp":300,"radius":0.6,"speed":0,"sight":20,"weapon":{"type":"explosive","damage":10,"range":15,"cooldown":30,"projectileSpeed":20,
          "splash":2,"burst":6,"burstGap":0.1,"spread":1.0,"turnRate":20}},
        {"id":"atgm","maxHp":300,"radius":0.5,"speed":0,"sight":20,"weapon":{"type":"piercing","damage":80,"range":12,"cooldown":30,"projectileSpeed":12,"guided":3,"turnRate":20}},
        {"id":"drone","maxHp":20,"radius":0.3,"speed":9,"air":true,"weapon":{"type":"explosive","damage":100,"range":0.6,"cooldown":1,"splash":1,"suicide":true}}
    ]})");
    const auto map = Map::parse(R"({"width":40,"height":40,"defense":{"startEnergy":0,"baseIncome":0,"waves":[]}})");
    auto count = [](const World& w, const GameData& d, const char* id) { return countDef(w, d, id); };

    World salvo(data, map);
    salvo.spawn(data.find("grad"), 0, {5.5f, 20.5f});
    const auto t1 = salvo.spawn(data.find("dummy"), 1, {17.5f, 20.5f});
    int maxShells = 0;
    for (int i = 0; i < 60; ++i) {
        salvo.step();
        maxShells = std::max(maxShells, static_cast<int>(salvo.registry().storage<Projectile>().size()));
    }
    CHECK(maxShells >= 4);
    run(salvo, 2.0f);
    CHECK(salvo.registry().get<Health>(t1).hp < 5000.0f - 30.0f);

    World guided(data, map);
    guided.spawn(data.find("atgm"), 0, {5.5f, 20.5f});
    const auto t2 = guided.spawn(data.find("dummy"), 1, {15.5f, 20.5f});
    bool curved = false;
    for (int i = 0; i < 90; ++i) {
        guided.step();
        for (auto [e, pos, proj] : guided.registry().view<const Position, const Projectile>().each())
            curved = curved || std::abs(pos.value.y - 20.5f) > 0.3f;
    }
    CHECK(curved);
    CHECK(guided.registry().get<Health>(t2).hp < 5000.0f - 50.0f);

    World kamikaze(data, map);
    kamikaze.spawn(data.find("drone"), 0, {10.5f, 20.5f});
    const auto t3 = kamikaze.spawn(data.find("dummy"), 1, {12.5f, 20.5f});
    run(kamikaze, 3.0f);
    CHECK(count(kamikaze, data, "drone") == 0);
    CHECK(kamikaze.registry().get<Health>(t3).hp < 5000.0f - 50.0f);
}
