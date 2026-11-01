#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>

#include "core/Pathfinding.h"
#include "core/World.h"

namespace vw {

namespace {

enum Role { Main, AntiAir, Artillery, Aircraft, Support, RoleCount };

constexpr const char* kBalanced[] = {"workshop",   "power_plant", "gun_tower", "tank_shop",  "laboratory", "power_plant", "repair_bay",
                                     "radar_station", "tank_shop", "zu23",      "laboratory", "helipad",    "howitzer",    "power_plant",
                                     "cannon_hardpoint", "sam_site", "power_plant"};
constexpr const char* kRush[] = {"workshop",   "power_plant", "workshop",  "tank_shop",     "laboratory", "power_plant", "repair_bay",
                                 "tank_shop",  "gun_tower",   "radar_station", "power_plant", "helipad",  "zu23",        "power_plant"};
constexpr const char* kTurtle[] = {"workshop",     "gun_tower",  "power_plant",      "laboratory",  "cannon_hardpoint", "gun_tower",
                                   "zu23",         "laboratory", "mortar_pit",       "power_plant", "tank_shop",        "cannon_hardpoint",
                                   "radar_station", "howitzer",  "repair_bay",       "rotary_tower", "power_plant",     "sam_site",
                                   "missile_silo", "howitzer",   "cannon_hardpoint", "power_plant"};
constexpr const char* kArmor[] = {"workshop",  "power_plant",   "laboratory", "tank_shop", "power_plant", "repair_bay",       "tank_shop",
                                  "gun_tower", "radar_station", "laboratory", "power_plant", "zu23",      "tank_shop",        "cannon_hardpoint",
                                  "helipad",   "power_plant"};
constexpr const char* kArtillery[] = {"workshop",   "power_plant", "laboratory",       "gun_tower", "tank_shop", "radar_station",
                                      "mortar_pit", "power_plant", "howitzer",         "laboratory", "repair_bay", "cannon_hardpoint",
                                      "zu23",       "howitzer",    "missile_silo",     "power_plant", "sam_site", "helipad"};
constexpr const char* kAir[] = {"workshop", "power_plant", "laboratory", "gun_tower", "helipad",   "power_plant", "radar_station",
                                "laboratory", "helipad",   "zu23",       "repair_bay", "tank_shop", "sam_site",    "power_plant",
                                "cannon_hardpoint"};

struct Style {
    const char* const* order;
    std::size_t orderSize;
    std::array<float, RoleCount> want; // main, anti-air, artillery, aircraft, support
    int firstAttack;
    float firstTime;
    int grow;
    float researchKeep;
    bool cheap;
    const char* branch;
};

template <std::size_t N>
constexpr Style style(const char* const (&order)[N], std::array<float, RoleCount> want, int first, float time, int grow, float keep, bool cheap,
                      const char* branch) {
    return {order, N, want, first, time, grow, keep, cheap, branch};
}

const Style kStyles[] = {
    style(kBalanced, {0.62f, 0.05f, 0.15f, 0.12f, 0.06f}, 7, 120.0f, 3, 60.0f, false, "economy"),
    style(kRush, {0.86f, 0.03f, 0.04f, 0.02f, 0.05f}, 8, 150.0f, 2, 180.0f, true, "weapons"),
    style(kTurtle, {0.55f, 0.10f, 0.25f, 0.03f, 0.07f}, 10, 300.0f, 4, 0.0f, false, "defences"),
    style(kArmor, {0.76f, 0.06f, 0.08f, 0.03f, 0.07f}, 8, 180.0f, 3, 100.0f, false, "armour"),
    style(kArtillery, {0.42f, 0.07f, 0.41f, 0.03f, 0.07f}, 9, 240.0f, 3, 60.0f, false, "artillery"),
    style(kAir, {0.54f, 0.08f, 0.08f, 0.26f, 0.04f}, 6, 150.0f, 3, 60.0f, false, "air"),
};

// Per AI level (easy, normal, hard, brutal).
constexpr int kThink[kAiLevels] = {75, 45, 30, 20};
constexpr std::size_t kArmyCap[kAiLevels] = {24, 50, 60, 80};
constexpr float kCooldown[kAiLevels] = {80.0f, 45.0f, 40.0f, 35.0f};
constexpr std::size_t kBuilders[kAiLevels] = {1, 2, 2, 3};
constexpr std::size_t kQueue[kAiLevels] = {1, 2, 2, 2};
// smarter AIs only attack with an army this much stronger than the known defence of the target
constexpr float kCaution[kAiLevels] = {0.0f, 0.7f, 1.0f, 1.25f};
constexpr float kFirstDelay[kAiLevels] = {300.0f, 0.0f, 0.0f, 0.0f};

bool hasClass(const UnitDef& def, const char* c) {
    return std::find(def.classes.begin(), def.classes.end(), c) != def.classes.end();
}

Role roleOf(const UnitDef& def) {
    if (!def.weapon)
        return Support;
    if (def.air)
        return Aircraft;
    if (hasClass(def, "aa"))
        return AntiAir;
    if (hasClass(def, "artillery"))
        return Artillery;
    return Main;
}

// rough combat value: sqrt(hp * dps), so cost efficiency = power / cost
float power(const UnitDef& def) {
    if (!def.weapon)
        return std::sqrt(def.maxHp * 4.0f);
    const auto& w = *def.weapon;
    const float dps = w.damage * static_cast<float>(std::max(1, w.burst)) * (1.0f + w.splash * 0.5f) / std::max(w.cooldown, 0.05f);
    return std::sqrt((def.maxHp + def.armor * 60.0f) * dps) * (1.0f + std::min(w.range, 20.0f) * 0.02f);
}

bool fighter(const UnitDef& def) {
    if (def.structure || def.builder || def.deploy != kInvalidUnitDef || def.transport > 0)
        return false;
    if (def.weapon)
        return !def.weapon->suicide;
    return def.repair > 0.0f || !def.auras.empty();
}

}

void World::aiMove(std::vector<entt::entity> units, glm::vec2 point) {
    // re-issuing the same order restarts pathing; skip units already heading there
    std::erase_if(units, [&](entt::entity e) {
        const auto& o = registry_.get<Order>(e);
        if (const auto* t = registry_.try_get<Target>(e); t && t->value != entt::null && alive(t->value) && o.type == OrderType::AttackMove)
            return true; // already fighting
        return (o.type == OrderType::AttackMove || o.type == OrderType::Move) && glm::distance(o.point, point) < 8.0f;
    });
    if (!units.empty())
        apply(MoveCommand{std::move(units), point, true});
}

void World::updateAi() {
    for (std::size_t p = 0; p < players_.size(); ++p) {
        if (!players_[p].active || !players_[p].ai)
            continue;
        const auto interval = static_cast<std::uint64_t>(kThink[players_[p].aiLevel % kAiLevels]);
        if ((tick_ + p * 7) % interval == 0)
            thinkAi(static_cast<std::uint8_t>(p));
    }
}

void World::thinkAi(std::uint8_t p) {
    auto& me = players_[p];
    auto& ai = ai_[p];
    const int level = me.aiLevel % kAiLevels;
    const Style& st = kStyles[static_cast<int>(me.aiStyle) % static_cast<int>(AiStyle::Count)];
    if (!ai.started) {
        ai.started = true;
        ai.attackSize = st.firstAttack + (level == 0 ? 2 : 0);
        ai.nextAttack = st.firstTime + kFirstDelay[level];
    }
    const std::uint8_t team = me.team;
    std::vector<entt::entity> army, builders, factories, labs;
    entt::entity hq = entt::null, kshm = entt::null;
    std::vector<int> counts(data_->units().size(), 0);
    std::array<int, RoleCount> roles{};
    bool building = false;
    std::vector<glm::vec2> repairBays;
    float strength = 0.0f;
    for (auto [e, unit, owner] : registry_.view<const Unit, const Owner>().each()) {
        if (owner.player != p || !alive(e))
            continue;
        const auto& def = data_->unit(unit.def);
        ++counts[unit.def];
        if (def.deploy != kInvalidUnitDef)
            kshm = e;
        else if (def.undeploy != kInvalidUnitDef)
            hq = e;
        if (registry_.all_of<Construction>(e))
            continue;
        if (def.structure && def.repair > 0.0f)
            repairBays.push_back(registry_.get<Position>(e).value);
        if (registry_.all_of<Factory>(e))
            factories.push_back(e);
        if (registry_.all_of<Lab>(e))
            labs.push_back(e);
        if (def.structure || registry_.all_of<Carried>(e))
            continue;
        if (def.builder) {
            builders.push_back(e);
            if (registry_.get<Order>(e).type == OrderType::Build) {
                // a builder that hasn't laid its foundation for a minute is stuck: drop the order
                const bool placed = std::any_of(registry_.view<const Construction>().begin(), registry_.view<const Construction>().end(),
                                                [&](entt::entity c) { return registry_.all_of<Owner>(c) && registry_.get<Owner>(c).player == p; });
                if (!placed && missionTime() - ai.buildStarted > 60.0f) {
                    apply(StopCommand{{e}, false});
                    ++ai.buildTry;
                } else {
                    building = true;
                }
            }
        } else if (fighter(def)) {
            army.push_back(e);
            ++roles[roleOf(def)];
            strength += def.maxHp;
        }
    }
    const glm::vec2 home = alive(hq) ? registry_.get<Position>(hq).value : alive(kshm) ? registry_.get<Position>(kshm).value : startOf(p);

    if (hq == entt::null && kshm != entt::null) {
        const glm::vec2 at = registry_.get<Position>(kshm).value;
        if (glm::distance(at, startOf(p)) < 6.0f || registry_.get<Order>(kshm).type == OrderType::None) {
            apply(DeployCommand{p, {kshm}});
            if (alive(kshm) && registry_.get<Order>(kshm).type == OrderType::None) {
                const float a = static_cast<float>((tick_ / 30 + p) % 8) * 0.785f;
                apply(MoveCommand{{kshm}, at + glm::vec2(std::cos(a), std::sin(a)) * 4.0f});
            }
        }
        return;
    }

    // Enemy picture: armed strength per team and how much of it flies.
    std::array<float, 256> enemyStrength{};
    int enemyArmed = 0, enemyAir = 0;
    for (auto [e, unit, t, pos] : registry_.view<const Unit, const Team, const Position>(entt::exclude<Carried>).each()) {
        if (t.id == team || !alive(e) || !visibleTo(team, pos.value))
            continue;
        const auto& def = data_->unit(unit.def);
        if (!def.weapon || def.structure)
            continue;
        enemyStrength[t.id] += def.maxHp;
        ++enemyArmed;
        enemyAir += registry_.all_of<Air>(e) ? 1 : 0;
    }

    for (std::size_t t = 0; t < enemyStrength.size() && t < ai.seenStrength.size(); ++t) {
        ai.seenStrength[t] = std::max(ai.seenStrength[t] * 0.995f, enemyStrength[t]);
        enemyStrength[t] = ai.seenStrength[t];
    }

    auto unlocked = [&](const UnitDef& def) {
        if (!unlockedFor(p, def))
            return false;
        return def.needs == kInvalidUnitDef || counts[def.needs] > 0;
    };

    float reserve = 0.0f;
    // keep structures and army in proportion: past the core base, every new building needs a few more units
    int structuresOwned = 0;
    for (std::size_t u = 0; u < counts.size(); ++u)
        if (data_->unit(static_cast<UnitDefId>(u)).structure)
            structuresOwned += counts[u];
    const float perBuilding = me.aiStyle == AiStyle::Turtle ? 0.8f : 1.0f;
    const bool armyFirst = structuresOwned >= 6 && static_cast<float>(army.size()) < static_cast<float>(structuresOwned - 5) * perBuilding;
    if (!builders.empty() && !armyFirst) {
        std::vector<int> planned(counts.size(), 0);
        // economy is shared by every style: one more generator every 2.5 minutes
        std::vector<const char*> order;
        const UnitDefId plant = data_->find("power_plant");
        const int wantPlants = std::min(level >= 3 ? 8 : level == 0 ? 4 : 6, 1 + static_cast<int>(missionTime() / 150.0f));
        if (plant != kInvalidUnitDef && counts[plant] < wantPlants && counts[data_->find("workshop")] > 0)
            order.push_back("power_plant");
        order.insert(order.end(), st.order, st.order + st.orderSize);
        for (const char* name : order) {
            const UnitDefId id = data_->find(name);
            if (id == kInvalidUnitDef)
                continue;
            if (counts[id] > planned[id]++)
                continue;
            const auto& def = data_->unit(id);
            if (!unlocked(def))
                continue;
            if (id == data_->find("sam_site") && enemyAir == 0)
                continue;
            reserve = def.cost;
            if (building || me.energy < def.cost)
                break;
            const auto builder = builders[static_cast<std::size_t>(ai.buildTry) % builders.size()];
            for (int attempt = 0; attempt < 32; ++attempt) {
                const float a = static_cast<float>(ai.buildTry * 7 + attempt) * 2.39996f;
                const float r = 6.0f + static_cast<float>((ai.buildTry + attempt) % 6) * 2.5f;
                const glm::vec2 site = snapBuild(home + glm::vec2(std::cos(a), std::sin(a)) * r);
                if (checkSite(id, site, team, true) != SiteStatus::Ok)
                    continue;
                const auto path = findPath(navMap(), registry_.get<Position>(builder).value, site);
                if (path.empty() || glm::distance(path.back(), site) > data_->unit(id).radius + 3.0f)
                    continue;
                apply(BuildCommand{{builder}, id, site, false});
                ai.buildStarted = missionTime();
                break;
            }
            ++ai.buildTry;
            break;
        }
    }

    // Research: unlocks first, then upgrades for classes the army actually fields.
    for (const auto lab : labs) {
        if (registry_.get<Lab>(lab).topic != kInvalidId)
            continue;
        ResearchId best = kInvalidId;
        float bestScore = 1e9f;
        for (std::size_t t = 0; t < data_->research().size(); ++t) {
            const auto topic = static_cast<ResearchId>(t);
            if (!canResearch(p, topic))
                continue;
            const auto& def = data_->research(topic);
            float score = def.cost;
            bool unlocks = false;
            for (const auto& u : data_->units())
                unlocks = unlocks || u.research == topic;
            if (unlocks)
                score *= 0.6f;
            if (!def.classes.empty()) {
                int fielded = 0;
                for (std::size_t u = 0; u < counts.size(); ++u)
                    if (counts[u] > 0 && def.applies(data_->unit(static_cast<UnitDefId>(u))))
                        fielded += counts[u];
                score *= fielded == 0 ? 2.0f : fielded < 4 ? 1.2f : 0.8f;
            }
            if (enemyAir > 0 && std::find(def.classes.begin(), def.classes.end(), "aa") != def.classes.end())
                score *= 0.6f;
            if (def.branch == st.branch)
                score *= 0.55f;
            else if (def.branch == "economy")
                score *= 0.8f;
            if (score < bestScore) {
                bestScore = score;
                best = topic;
            }
        }
        if (best == kInvalidId)
            continue;
        const float cost = data_->research(best).cost;
        if (me.energy > cost + reserve + st.researchKeep)
            apply(StructureCommand{p, lab, StructureOp::Research, static_cast<std::uint8_t>(best), {}});
        else if (!st.cheap || missionTime() > 360.0f)
            reserve += cost * (level >= 2 ? 0.8f : 0.5f); // save up instead of spending everything on units
    }

    // Army mix: anti-air grows with the enemy's air force, artillery and aircraft stay a minority.
    const float total = static_cast<float>(army.size()) + 1.0f;
    std::array<float, RoleCount> want = st.want;
    if (enemyAir > 0)
        want[AntiAir] = std::max(want[AntiAir], std::clamp(static_cast<float>(enemyAir) / static_cast<float>(enemyArmed) * 1.4f, 0.12f, 0.45f));
    want[Main] = std::max(0.1f, 1.0f - want[AntiAir] - want[Artillery] - want[Aircraft] - want[Support]);
    float bestAnywhere = 0.0f, bestAnywhereCost = 0.0f;
    for (const auto f : factories) {
        const UnitDefId kind = registry_.get<Unit>(f).def;
        for (const auto id : data_->recruitable()) {
            const auto& def = data_->unit(id);
            if (!def.weapon || std::find(def.producedAt.begin(), def.producedAt.end(), kind) == def.producedAt.end() || !unlocked(def))
                continue;
            const float score = st.cheap ? power(def) / std::max(def.cost, 1.0f) : power(def) * std::sqrt(power(def) / std::max(def.cost, 1.0f));
            if (score > bestAnywhere) {
                bestAnywhere = score;
                bestAnywhereCost = def.cost;
            }
        }
    }
    for (const auto f : factories) {
        if (registry_.get<Factory>(f).queue.size() >= kQueue[level])
            continue;
        const UnitDefId kind = registry_.get<Unit>(f).def;
        std::vector<UnitDefId> options;
        UnitDefId truck = kInvalidUnitDef;
        for (const auto id : data_->recruitable()) {
            const auto& def = data_->unit(id);
            if (std::find(def.producedAt.begin(), def.producedAt.end(), kind) == def.producedAt.end() || !unlocked(def))
                continue;
            if (def.builder)
                truck = id;
            else if (fighter(def) && army.size() < kArmyCap[level] + (me.energy > 1500.0f ? 20u : 0u) &&
                     (def.weapon || counts[id] < (def.air ? 1 : 3)))
                options.push_back(id);
        }
        if (truck != kInvalidUnitDef && builders.size() < kBuilders[level]) {
            if (me.energy >= data_->unit(truck).cost)
                apply(RecruitCommand{p, truck, f});
            continue;
        }
        int role = -1;
        float deficit = -1e9f;
        for (const auto id : options) {
            const int r = roleOf(data_->unit(id));
            const float d = want[static_cast<std::size_t>(r)] * total - static_cast<float>(roles[static_cast<std::size_t>(r)]);
            if (d > deficit) {
                deficit = d;
                role = r;
            }
        }
        UnitDefId pick = kInvalidUnitDef;
        float pickScore = 0.0f;
        for (const auto id : options) {
            const auto& def = data_->unit(id);
            if (roleOf(def) != role || me.energy < def.cost + reserve)
                continue;
            // rush: best value per energy; others: the strongest unit they can afford, with some variety
            const float jitter = 1.0f + static_cast<float>((tick_ / 90 + id) % 3) * 0.15f;
            const float score = (st.cheap ? power(def) / std::max(def.cost, 1.0f) : power(def) * std::sqrt(power(def) / std::max(def.cost, 1.0f))) * jitter;
            if (pick == kInvalidUnitDef || score > pickScore) {
                pick = id;
                pickScore = score;
            }
        }
        // don't let a workshop drain the energy a tank shop needs for much better units
        if (pick != kInvalidUnitDef && pickScore < bestAnywhere * 0.55f && me.energy < bestAnywhereCost * 2.0f)
            pick = kInvalidUnitDef;
        if (pick != kInvalidUnitDef) {
            apply(RecruitCommand{p, pick, f});
            ++roles[static_cast<std::size_t>(role)];
        }
    }

    // Badly damaged units fall back to the nearest repair bay and stay there until mostly fixed.
    std::vector<entt::entity> idle, air;
    for (const auto e : army) {
        const auto& order = registry_.get<Order>(e);
        const auto& hp = registry_.get<Health>(e);
        const float fraction = hp.hp / std::max(1.0f, hp.max);
        const glm::vec2 pos = registry_.get<Position>(e).value;
        const glm::vec2* bay = nullptr;
        for (const auto& b : repairBays)
            if (!bay || glm::distance(b, pos) < glm::distance(*bay, pos))
                bay = &b;
        if (level > 0 && bay && fraction < 0.3f && glm::distance(pos, *bay) > 6.0f && order.type != OrderType::Move) {
            apply(MoveCommand{{e}, *bay, false});
            continue;
        }
        if (order.type != OrderType::None && order.type != OrderType::Hold)
            continue;
        if (bay && fraction < 0.75f && glm::distance(pos, *bay) < 6.0f)
            continue;
        const auto& def = data_->unit(registry_.get<Unit>(e).def);
        (def.air && def.weapon ? air : idle).push_back(e);
    }

    entt::entity threat = entt::null;
    float threatDist = 22.0f;
    for (auto [e, pos, t, unit] : registry_.view<const Position, const Team, const Unit>(entt::exclude<Carried>).each()) {
        if (t.id == team || !alive(e) || !data_->unit(unit.def).weapon || !visibleTo(team, pos.value))
            continue;
        const float d = glm::distance(pos.value, home);
        if (d < threatDist) {
            threatDist = d;
            threat = e;
        }
    }
    if (threat != entt::null && !army.empty()) {
        // only units near home turn back; an attack already under way keeps going
        std::vector<entt::entity> defenders;
        for (const auto e : army)
            if (glm::distance(registry_.get<Position>(e).value, home) < 40.0f)
                defenders.push_back(e);
        if (!defenders.empty()) {
            aiMove(defenders, registry_.get<Position>(threat).value);
            std::erase_if(idle, [&](entt::entity e) { return std::find(defenders.begin(), defenders.end(), e) != defenders.end(); });
            if (defenders.size() * 2 > army.size()) {
                ai.stageUntil = -1.0f;
                return;
            }
        }
    }

    // Helicopters raid the nearest unarmed enemy structure (economy, factories).
    if (air.size() >= 2) {
        entt::entity raid = entt::null;
        float best = 1e9f;
        for (auto [e, pos, t, unit] : registry_.view<const Position, const Team, const Unit, const Structure>().each()) {
            if (t.id == team || t.id == kVoidTeam || !alive(e) || data_->unit(unit.def).weapon || !exploredBy(team, pos.value))
                continue;
            const float d = glm::distance(pos.value, home);
            if (d < best) {
                best = d;
                raid = e;
            }
        }
        if (raid != entt::null)
            aiMove(air, registry_.get<Position>(raid).value);
        else
            idle.insert(idle.end(), air.begin(), air.end());
    }

    // Attacks: pick the enemy whose army is weakest relative to its distance, gather at a staging point, then go together.
    if (ai.stageUntil >= 0.0f) {
        if (ai.target != entt::null && !alive(ai.target)) {
            ai.stageUntil = -1.0f;
        } else {
            std::vector<entt::entity> ready, late;
            for (const auto e : idle)
                (glm::distance(registry_.get<Position>(e).value, ai.stage) < 10.0f ? ready : late).push_back(e);
            if (static_cast<float>(ready.size()) >= static_cast<float>(ai.attackSize) * 0.75f || missionTime() >= ai.stageUntil) {
                std::vector<entt::entity> group;
                for (const auto e : army)
                    if (registry_.get<Order>(e).type != OrderType::Move && !data_->unit(registry_.get<Unit>(e).def).air)
                        group.push_back(e);
                aiMove(group, ai.target != entt::null ? registry_.get<Position>(ai.target).value : ai.goal);
                ai.stageUntil = -1.0f;
                ai.attackSize = std::min(30, ai.attackSize + st.grow);
                ai.nextAttack = missionTime() + kCooldown[level];
            } else if (!late.empty()) {
                aiMove(late, ai.stage);
            }
            return;
        }
    }
    float strongestEnemy = 0.0f;
    for (const float v : enemyStrength)
        strongestEnemy = std::max(strongestEnemy, v);
    // unscouted bases are assumed to grow defences over time
    const bool strongEnough = strength >= (strongestEnemy * 0.6f + missionTime() * 4.0f) * kCaution[level];
    if (static_cast<int>(idle.size()) >= ai.attackSize && missionTime() >= ai.nextAttack && strongEnough) {
        entt::entity target = entt::null;
        float best = 1e9f;
        auto known = [&](entt::entity e, glm::vec2 at) {
            if (exploredBy(team, at))
                return true;
            const auto* owner = registry_.try_get<Owner>(e);
            return owner && glm::distance(at, startOf(owner->player)) < 30.0f;
        };
        auto defenceAt = [&](glm::vec2 at, std::uint8_t enemy) {
            float sum = enemyStrength[enemy] * 0.6f;
            for (auto [s, sp, stt, su] : registry_.view<const Position, const Team, const Unit, const Structure>().each())
                if (stt.id == enemy && alive(s) && data_->unit(su.def).weapon && glm::distance(sp.value, at) < 16.0f && exploredBy(team, sp.value))
                    sum += data_->unit(su.def).maxHp * 0.8f;
            return sum;
        };
        float targetDefence = 0.0f;
        for (auto [e, pos, t] : registry_.view<const Position, const Team, const Structure>().each()) {
            if (t.id == team || t.id == kVoidTeam || !alive(e) || !known(e, pos.value))
                continue;
            const float defence = defenceAt(pos.value, t.id);
            const float d = glm::distance(pos.value, home) * (0.6f + defence / (strength + 1.0f));
            if (d < best) {
                best = d;
                target = e;
                targetDefence = defence;
            }
        }
        if (target != entt::null && strength < targetDefence * kCaution[level]) {
            ai.nextAttack = missionTime() + 15.0f; // not yet: grow the army, take points meanwhile
            target = entt::null;
            best = -1.0f;
        }
        if (target == entt::null && best >= 0.0f)
            for (auto [e, pos, t] : registry_.view<const Position, const Team>().each()) {
                if (t.id == team || !alive(e) || !visibleTo(team, pos.value))
                    continue;
                const float d = glm::distance(pos.value, home);
                if (d < best) {
                    best = d;
                    target = e;
                }
            }
        glm::vec2 to{};
        if (best < 0.0f) {
            // postponed: fall through to point captures below
        } else if (target != entt::null) {
            to = registry_.get<Position>(target).value;
        } else {
            // nothing scouted yet: head for the start position of the nearest living enemy (start positions are public)
            float bestStart = 1e9f;
            for (std::size_t q = 0; q < players_.size(); ++q) {
                const auto& other = players_[q];
                if (!other.active || other.team == team)
                    continue;
                const glm::vec2 at = startOf(static_cast<std::uint8_t>(q));
                if (glm::distance(at, home) < bestStart) {
                    bestStart = glm::distance(at, home);
                    to = at;
                }
            }
            if (bestStart >= 1e9f)
                return;
        }
        if (best >= 0.0f) {
        const auto path = findPath(navMap(), home, to);
        ai.target = target;
        ai.goal = to;
        ai.stage = home;
        float walked = 0.0f;
        for (std::size_t i = 1; i < path.size() && walked < 28.0f; ++i) {
            walked += glm::distance(path[i - 1], path[i]);
            ai.stage = path[i];
        }
        ai.stageUntil = missionTime() + 25.0f;
        aiMove(idle, ai.stage);
        return;
        }
    }
    // Resource points: take the whole idle group to the nearest point whose known guards it can beat.
    if (idle.size() >= 4 && map_ && missionTime() >= ai.nextCapture) {
        float group = 0.0f;
        for (const auto e : idle)
            group += data_->unit(registry_.get<Unit>(e).def).maxHp;
        const auto& defs = map_->resourcePoints();
        int bestPoint = -1;
        float best = 1e9f;
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (points_[i].owner == team)
                continue;
            float guard = 0.0f;
            for (auto [e, pos, t, unit] : registry_.view<const Position, const Team, const Unit>(entt::exclude<Carried>).each()) {
                if (t.id == team || !alive(e) || glm::distance(pos.value, defs[i].pos) > 11.0f)
                    continue;
                const auto& def = data_->unit(unit.def);
                if (def.weapon && (registry_.all_of<Structure>(e) ? exploredBy(team, pos.value) : visibleTo(team, pos.value)))
                    guard += def.maxHp * (def.structure ? 1.5f : 1.0f);
            }
            if (guard > group * 0.9f)
                continue;
            const float d = glm::distance(defs[i].pos, home) * (1.0f + guard / (group + 1.0f));
            if (d < best) {
                best = d;
                bestPoint = static_cast<int>(i);
            }
        }
        if (bestPoint >= 0 && best < 110.0f) {
            aiMove(idle, defs[static_cast<std::size_t>(bestPoint)].pos);
            ai.nextCapture = missionTime() + 45.0f;
        }
    }
}

}
