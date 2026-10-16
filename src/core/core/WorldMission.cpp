#include "core/World.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace vw {

namespace {

constexpr std::uint8_t kHidden = 0;
constexpr std::uint8_t kActive = 1;
constexpr std::uint8_t kDone = 2;
constexpr std::uint8_t kFailed = 3;

}

bool World::tagAlive(const std::string& tag) const {
    for (auto [e, t] : registry_.view<const Tag>().each())
        if (t.name == tag && alive(e))
            return true;
    return false;
}

bool World::playerNear(glm::vec2 pos, float radius) const {
    for (auto [e, owner, p] : registry_.view<const Owner, const Position>().each())
        if (alive(e) && !registry_.all_of<Carried>(e) && glm::distance(p.value, pos) <= radius)
            return true;
    return false;
}

void World::finishObjective(const std::string& id, std::uint8_t state) {
    const auto& objs = map_->mission().objectives;
    for (std::size_t i = 0; i < objs.size(); ++i)
        if (objs[i].id == id && objectiveState_[i] != kDone && objectiveState_[i] != kFailed) {
            objectiveState_[i] = state;
            events_.push_back({EventType::Objective, entt::null, entt::null, {}, {}, static_cast<float>(i)});
        }
}

void World::runAction(const MissionAction& a) {
    if (a.type == "message") {
        messages_.push_back(a.text);
    } else if (a.type == "spawn") {
        const UnitDefId def = data_->find(a.unit);
        if (def == kInvalidUnitDef)
            return;
        int owner = -1;
        if (a.player)
            for (std::size_t p = 0; p < players_.size(); ++p)
                if (players_[p].active) {
                    owner = static_cast<int>(p);
                    break;
                }
        const float spacing = data_->unit(def).radius * 2.0f + 0.4f;
        const int side = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(a.count)))));
        for (int i = 0; i < a.count; ++i) {
            const glm::vec2 offset{(static_cast<float>(i % side) - static_cast<float>(side - 1) * 0.5f) * spacing,
                                   (static_cast<float>(i / side) - static_cast<float>(side - 1) * 0.5f) * spacing};
            const glm::vec2 pos = Map::center(nearestFree(navMap(), Map::cellOf(a.pos + offset)));
            const auto e = spawn(def, a.player ? std::uint8_t{0} : a.team, pos, owner);
            if (!a.tag.empty())
                registry_.emplace_or_replace<Tag>(e, Tag{a.tag});
            if (a.target && registry_.all_of<Order>(e))
                registry_.get<Order>(e) = {OrderType::AttackMove, *a.target + offset, entt::null, pos};
            events_.push_back({EventType::Summon, e, entt::null, pos, pos, 0.0f});
        }
    } else if (a.type == "energy") {
        grantTeamEnergy(a.amount);
    } else if (a.type == "complete") {
        finishObjective(a.objective, kDone);
    } else if (a.type == "fail") {
        finishObjective(a.objective, kFailed);
    } else if (a.type == "reveal") {
        const auto& objs = map_->mission().objectives;
        for (std::size_t i = 0; i < objs.size(); ++i)
            if (objs[i].id == a.objective && objectiveState_[i] == kHidden) {
                objectiveState_[i] = kActive;
                events_.push_back({EventType::Objective, entt::null, entt::null, {}, {}, static_cast<float>(i)});
            }
    } else if (a.type == "victory" || a.type == "defeat") {
        if (defense_.state == MatchState::Playing) {
            defense_.state = a.type == "victory" ? MatchState::Victory : MatchState::Defeat;
            events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, a.type == "victory" ? 1.0f : 0.0f});
        }
    }
}

void World::updateMission() {
    if (!map_ || !map_->mission().enabled)
        return;
    const auto& mission = map_->mission();
    if (!missionStarted_) {
        missionStarted_ = true;
        if (objectiveState_.size() != mission.objectives.size()) {
            objectiveState_.assign(mission.objectives.size(), kActive);
            for (std::size_t i = 0; i < mission.objectives.size(); ++i)
                if (mission.objectives[i].hidden)
                    objectiveState_[i] = kHidden;
        }
        if (triggerFired_.size() != mission.triggers.size()) {
            triggerFired_.assign(mission.triggers.size(), 0);
            triggerNext_.assign(mission.triggers.size(), 0.0f);
        }
    }
    if (tick_ % 5 != 0 || defense_.state != MatchState::Playing)
        return;
    const float now = missionTime();

    for (std::size_t i = 0; i < mission.objectives.size(); ++i) {
        if (objectiveState_[i] != kActive)
            continue;
        const auto& o = mission.objectives[i];
        bool done = false;
        bool failed = false;
        if (o.type == "reach")
            done = playerNear(o.pos, o.radius);
        else if (o.type == "destroy")
            done = !tagAlive(o.tag);
        else if (o.type == "destroy_team") {
            done = true;
            for (auto [e, team] : registry_.view<const Team>(entt::exclude<Invulnerable>).each())
                if (team.id == o.team && alive(e) && registry_.all_of<Unit>(e)) {
                    done = false;
                    break;
                }
        } else if (o.type == "survive")
            done = now >= o.time;
        else if (o.type == "protect")
            failed = !tagAlive(o.tag);
        if (done || failed)
            finishObjective(o.id, done ? kDone : kFailed);
    }

    for (std::size_t i = 0; i < mission.triggers.size(); ++i) {
        const auto& t = mission.triggers[i];
        if (triggerFired_[i] >= t.times || now < triggerNext_[i])
            continue;
        bool hit = false;
        if (t.when == "start")
            hit = true;
        else if (t.when == "time")
            hit = now >= t.time;
        else if (t.when == "area")
            hit = playerNear(t.pos, t.radius);
        else if (t.when == "destroyed")
            hit = !tagAlive(t.tag);
        else if (t.when == "objective") {
            for (std::size_t k = 0; k < mission.objectives.size(); ++k)
                if (mission.objectives[k].id == t.objective && objectiveState_[k] == kDone)
                    hit = true;
        }
        if (!hit)
            continue;
        ++triggerFired_[i];
        triggerNext_[i] = now + std::max(t.repeat, 0.0f);
        for (const auto& a : t.actions)
            runAction(a);
    }

    bool required = false;
    bool allDone = true;
    for (std::size_t i = 0; i < mission.objectives.size(); ++i) {
        if (mission.objectives[i].optional)
            continue;
        required = true;
        if (objectiveState_[i] == kFailed) {
            runAction({"defeat"});
            return;
        }
        allDone = allDone && (objectiveState_[i] == kDone || mission.objectives[i].type == "protect");
    }
    if (required && allDone) {
        runAction({"victory"});
        return;
    }
    if (core_ == entt::null && now > 5.0f) {
        bool any = false;
        for (auto [e, owner] : registry_.view<const Owner>().each())
            if (alive(e)) {
                any = true;
                break;
            }
        if (!any)
            runAction({"defeat"});
    }
}

}
