#include <algorithm>
#include <array>
#include <cmath>

#include <glm/geometric.hpp>

#include "core/Pathfinding.h"
#include "core/World.h"

namespace vw {

namespace {

constexpr const char* kBuildOrder[] = {"workshop",    "power_plant",  "gun_tower",    "laboratory",       "power_plant", "tank_shop",
                                       "cannon_hardpoint", "radar_station", "repair_bay", "power_plant",   "laboratory",  "zu23",
                                       "helipad",     "rotary_tower", "tank_shop",    "howitzer",         "sam_site",    "power_plant",
                                       "missile_silo", "gun_tower",   "cannon_hardpoint", "power_plant"};

enum Role { Main, AntiAir, Artillery, Aircraft, Support, RoleCount };

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

bool fighter(const UnitDef& def) {
    if (def.structure || def.builder || def.deploy != kInvalidUnitDef || def.transport > 0)
        return false;
    if (def.weapon)
        return !def.weapon->suicide;
    return def.repair > 0.0f || !def.auras.empty();
}

}

void World::updateAi() {
    const int interval = difficulty_ == Difficulty::Easy ? 60 : difficulty_ == Difficulty::Normal ? 30 : 15;
    if (tick_ % static_cast<std::uint64_t>(interval) != 0)
        return;
    for (std::size_t p = 0; p < players_.size(); ++p)
        if (players_[p].active && players_[p].ai)
            thinkAi(static_cast<std::uint8_t>(p));
}

void World::thinkAi(std::uint8_t p) {
    auto& me = players_[p];
    auto& ai = ai_[p];
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
            building = building || registry_.get<Order>(e).type == OrderType::Build;
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
    for (auto [e, unit, t] : registry_.view<const Unit, const Team>(entt::exclude<Carried>).each()) {
        if (t.id == team || !alive(e))
            continue;
        const auto& def = data_->unit(unit.def);
        if (!def.weapon || def.structure)
            continue;
        enemyStrength[t.id] += def.maxHp;
        ++enemyArmed;
        enemyAir += registry_.all_of<Air>(e) ? 1 : 0;
    }

    auto unlocked = [&](const UnitDef& def) {
        if (def.research != kInvalidId && !researched(p, def.research))
            return false;
        return def.needs == kInvalidUnitDef || counts[def.needs] > 0;
    };

    float reserve = 0.0f;
    if (!builders.empty()) {
        std::vector<int> planned(counts.size(), 0);
        for (const char* name : kBuildOrder) {
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
                if (checkSite(id, site, team, true) == SiteStatus::Ok) {
                    apply(BuildCommand{{builder}, id, site, false});
                    break;
                }
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
            if (score < bestScore) {
                bestScore = score;
                best = topic;
            }
        }
        if (best != kInvalidId && me.energy > data_->research(best).cost + reserve + 60.0f)
            apply(StructureCommand{p, lab, StructureOp::Research, static_cast<std::uint8_t>(best), {}});
    }

    // Army mix: anti-air grows with the enemy's air force, artillery and aircraft stay a minority.
    const float total = static_cast<float>(army.size()) + 1.0f;
    std::array<float, RoleCount> want{};
    want[AntiAir] = enemyAir > 0 ? std::clamp(static_cast<float>(enemyAir) / static_cast<float>(enemyArmed) * 1.4f, 0.12f, 0.45f) : 0.04f;
    want[Artillery] = 0.18f;
    want[Aircraft] = 0.14f;
    want[Support] = 0.07f;
    want[Main] = 1.0f - want[AntiAir] - want[Artillery] - want[Aircraft] - want[Support];
    for (const auto f : factories) {
        if (registry_.get<Factory>(f).queue.size() >= 2)
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
            else if (fighter(def) && army.size() < (me.energy > 1500.0f ? 70u : 50u))
                options.push_back(id);
        }
        if (truck != kInvalidUnitDef && builders.size() < 2) {
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
        for (const auto id : options) {
            const auto& def = data_->unit(id);
            if (roleOf(def) != role || me.energy < def.cost + reserve)
                continue;
            if (pick == kInvalidUnitDef || def.cost * (1.0f + static_cast<float>((tick_ / 90 + id) % 3) * 0.3f) > data_->unit(pick).cost)
                pick = id;
        }
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
        if (bay && fraction < 0.3f && glm::distance(pos, *bay) > 6.0f && order.type != OrderType::Move) {
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
        if (t.id == team || !alive(e) || !data_->unit(unit.def).weapon)
            continue;
        const float d = glm::distance(pos.value, home);
        if (d < threatDist) {
            threatDist = d;
            threat = e;
        }
    }
    if (threat != entt::null && !army.empty()) {
        apply(MoveCommand{army, registry_.get<Position>(threat).value, true});
        ai.stageUntil = -1.0f;
        return;
    }

    // Helicopters raid the nearest unarmed enemy structure (economy, factories).
    if (air.size() >= 2) {
        entt::entity raid = entt::null;
        float best = 1e9f;
        for (auto [e, pos, t, unit] : registry_.view<const Position, const Team, const Unit, const Structure>().each()) {
            if (t.id == team || t.id == kVoidTeam || !alive(e) || data_->unit(unit.def).weapon)
                continue;
            const float d = glm::distance(pos.value, home);
            if (d < best) {
                best = d;
                raid = e;
            }
        }
        if (raid != entt::null)
            apply(MoveCommand{air, registry_.get<Position>(raid).value, true});
        else
            idle.insert(idle.end(), air.begin(), air.end());
    }

    // Attacks: pick the enemy whose army is weakest relative to its distance, gather at a staging point, then go together.
    if (ai.stageUntil >= 0.0f) {
        if (!alive(ai.target)) {
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
                apply(MoveCommand{group, registry_.get<Position>(ai.target).value, true});
                ai.stageUntil = -1.0f;
                ai.attackSize = std::min(30, ai.attackSize + 3);
                ai.nextAttack = missionTime() + 40.0f;
            } else if (!late.empty()) {
                apply(MoveCommand{late, ai.stage, true});
            }
            return;
        }
    }
    if (static_cast<int>(idle.size()) >= ai.attackSize && missionTime() >= ai.nextAttack) {
        entt::entity target = entt::null;
        float best = 1e9f;
        for (auto [e, pos, t] : registry_.view<const Position, const Team, const Structure>().each()) {
            if (t.id == team || t.id == kVoidTeam || !alive(e))
                continue;
            const float d = glm::distance(pos.value, home) * (0.6f + enemyStrength[t.id] / (strength + 1.0f));
            if (d < best) {
                best = d;
                target = e;
            }
        }
        if (target == entt::null)
            for (auto [e, pos, t] : registry_.view<const Position, const Team>().each()) {
                if (t.id == team || !alive(e))
                    continue;
                const float d = glm::distance(pos.value, home);
                if (d < best) {
                    best = d;
                    target = e;
                }
            }
        if (target != entt::null) {
            const glm::vec2 to = registry_.get<Position>(target).value;
            const auto path = findPath(navMap(), home, to);
            ai.target = target;
            ai.stage = home;
            float walked = 0.0f;
            for (std::size_t i = 1; i < path.size() && walked < 28.0f; ++i) {
                walked += glm::distance(path[i - 1], path[i]);
                ai.stage = path[i];
            }
            ai.stageUntil = missionTime() + 25.0f;
            apply(MoveCommand{idle, ai.stage, true});
        }
        return;
    }
    if (idle.size() >= 3 && map_) {
        const auto& defs = map_->resourcePoints();
        int bestPoint = -1;
        float best = 1e9f;
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (points_[i].owner == team)
                continue;
            const float d = glm::distance(defs[i].pos, home);
            if (d < best) {
                best = d;
                bestPoint = static_cast<int>(i);
            }
        }
        if (bestPoint >= 0 && best < 90.0f)
            apply(MoveCommand{{idle.begin(), idle.begin() + 3}, defs[static_cast<std::size_t>(bestPoint)].pos, true});
    }
}

}
