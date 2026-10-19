#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

#include "core/World.h"

namespace vw {

namespace {

constexpr const char* kBuildOrder[] = {"workshop",   "power_plant", "gun_tower",   "laboratory",       "power_plant", "tank_shop",
                                       "cannon_hardpoint", "gun_tower", "power_plant", "rotary_tower", "howitzer",    "zu23",
                                       "power_plant", "missile_silo"};

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
    bool building = false;
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
        if (registry_.all_of<Factory>(e))
            factories.push_back(e);
        if (registry_.all_of<Lab>(e))
            labs.push_back(e);
        if (def.structure || registry_.all_of<Carried>(e))
            continue;
        if (def.builder) {
            builders.push_back(e);
            building = building || registry_.get<Order>(e).type == OrderType::Build;
        } else if (def.weapon && def.deploy == kInvalidUnitDef) {
            army.push_back(e);
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
            reserve = def.cost;
            if (building || me.energy < def.cost)
                break;
            const auto builder = builders[static_cast<std::size_t>(ai.buildTry) % builders.size()];
            for (int attempt = 0; attempt < 24; ++attempt) {
                const float a = static_cast<float>(ai.buildTry * 7 + attempt) * 2.39996f;
                const float r = 6.0f + static_cast<float>((ai.buildTry + attempt) % 5) * 2.5f;
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

    for (const auto lab : labs) {
        if (registry_.get<Lab>(lab).topic != kInvalidId)
            continue;
        ResearchId best = kInvalidId;
        for (std::size_t t = 0; t < data_->research().size(); ++t)
            if (canResearch(p, static_cast<ResearchId>(t)) &&
                (best == kInvalidId || data_->research(static_cast<ResearchId>(t)).cost < data_->research(best).cost))
                best = static_cast<ResearchId>(t);
        if (best != kInvalidId && me.energy > data_->research(best).cost + reserve + 60.0f)
            apply(StructureCommand{p, lab, StructureOp::Research, static_cast<std::uint8_t>(best), {}});
    }

    for (const auto f : factories) {
        if (registry_.get<Factory>(f).queue.size() >= 2)
            continue;
        const UnitDefId kind = registry_.get<Unit>(f).def;
        UnitDefId pick = kInvalidUnitDef;
        for (const auto id : data_->recruitable()) {
            const auto& def = data_->unit(id);
            if (std::find(def.producedAt.begin(), def.producedAt.end(), kind) == def.producedAt.end() || !unlocked(def) ||
                me.energy < def.cost + (def.builder ? 0.0f : reserve) || (!def.builder && army.size() >= 45))
                continue;
            if (def.builder) {
                if (builders.size() < 2)
                    pick = id;
                continue;
            }
            if (!def.weapon || def.weapon->suicide)
                continue;
            if (pick == kInvalidUnitDef || data_->unit(pick).builder ||
                def.cost * (1.0f + static_cast<float>((tick_ / 90 + id) % 3) * 0.3f) > data_->unit(pick).cost)
                pick = id;
        }
        if (pick != kInvalidUnitDef)
            apply(RecruitCommand{p, pick, f});
    }

    std::vector<entt::entity> idle;
    for (const auto e : army) {
        const auto type = registry_.get<Order>(e).type;
        if (type == OrderType::None || type == OrderType::Hold)
            idle.push_back(e);
    }
    entt::entity threat = entt::null;
    float threatDist = 18.0f;
    for (auto [e, pos, t] : registry_.view<const Position, const Team>(entt::exclude<Carried>).each()) {
        if (t.id == team || !alive(e))
            continue;
        const float d = glm::distance(pos.value, home);
        if (d < threatDist) {
            threatDist = d;
            threat = e;
        }
    }
    if (threat != entt::null && !army.empty()) {
        apply(MoveCommand{army, registry_.get<Position>(threat).value, true});
        return;
    }
    if (static_cast<int>(idle.size()) >= ai.attackSize && missionTime() >= ai.nextAttack) {
        entt::entity target = entt::null;
        float best = 1e9f;
        for (auto [e, pos, t] : registry_.view<const Position, const Team, const Structure>().each()) {
            if (t.id == team || t.id == kVoidTeam || !alive(e))
                continue;
            const float d = glm::distance(pos.value, home);
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
            apply(MoveCommand{idle, registry_.get<Position>(target).value, true});
            ai.attackSize = std::min(28, ai.attackSize + 3);
            ai.nextAttack = missionTime() + 40.0f;
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
        if (bestPoint >= 0 && best < 70.0f)
            apply(MoveCommand{{idle.begin(), idle.begin() + 3}, defs[static_cast<std::size_t>(bestPoint)].pos, true});
    }
}

}
