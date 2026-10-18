#include <algorithm>

#include <nlohmann/json.hpp>

#include "core/World.h"

namespace vw {

bool World::researched(std::uint8_t player, ResearchId topic) const {
    if (player >= kMaxPlayers)
        return false;
    const auto& r = players_[player].researched;
    return topic < r.size() && r[topic] != 0;
}

bool World::canResearch(std::uint8_t player, ResearchId topic) const {
    if (player >= kMaxPlayers || topic >= data_->research().size() || researched(player, topic))
        return false;
    for (const auto a : data_->research(topic).after)
        if (!researched(player, a))
            return false;
    for (auto [e, lab, owner] : registry_.view<const Lab, const Owner>().each())
        if (owner.player == player && lab.topic == topic && alive(e))
            return false;
    return true;
}

void World::grantResearch(std::uint8_t player, ResearchId topic) {
    if (player >= kMaxPlayers || topic >= data_->research().size())
        return;
    auto& r = players_[player].researched;
    r.resize(data_->research().size(), 0);
    r[topic] = 1;
}

void World::refreshOwned(std::uint8_t player) {
    std::vector<entt::entity> list;
    for (auto [e, owner] : registry_.view<const Owner>().each())
        if (owner.player == player)
            list.push_back(e);
    for (const auto e : list)
        refreshStats(e);
}

void World::updateResearch() {
    std::vector<std::pair<std::uint8_t, ResearchId>> done;
    for (auto [e, lab, owner] : registry_.view<Lab, const Owner>(entt::exclude<Construction>).each()) {
        if (lab.topic >= data_->research().size() || !alive(e))
            continue;
        lab.progress += kTickDt;
        if (lab.progress >= data_->research(lab.topic).time) {
            done.emplace_back(owner.player, lab.topic);
            lab.topic = kInvalidId;
            lab.progress = 0.0f;
        }
    }
    for (const auto& [player, topic] : done) {
        grantResearch(player, topic);
        refreshOwned(player);
    }
}

std::string World::exportResearch(std::uint8_t player) const {
    nlohmann::json list = nlohmann::json::array();
    for (std::size_t i = 0; i < data_->research().size(); ++i)
        if (researched(player, static_cast<ResearchId>(i)))
            list.push_back(data_->research(static_cast<ResearchId>(i)).id);
    return list.dump();
}

void World::importResearch(std::uint8_t player, std::string_view text) {
    if (text.empty())
        return;
    for (const auto& id : nlohmann::json::parse(text))
        if (const ResearchId t = data_->findResearch(id.get<std::string>()); t != kInvalidId)
            grantResearch(player, t);
    refreshOwned(player);
}

void World::handle(const CheatCommand& cmd) {
    if (!cheats_ || cmd.player >= kMaxPlayers || !players_[cmd.player].active)
        return;
    auto& player = players_[cmd.player];
    auto owned = [&](entt::entity e) {
        const auto* o = registry_.try_get<Owner>(e);
        return o && o->player == cmd.player;
    };
    switch (cmd.op) {
    case CheatOp::Energy:
        player.energy += 1000.0f;
        player.shards += 5;
        break;
    case CheatOp::Spawn:
        if (cmd.unit < data_->units().size())
            for (int i = 0; i < std::clamp<int>(cmd.count, 1, 20); ++i) {
                const glm::vec2 offset{static_cast<float>(i % 5) * 1.4f, static_cast<float>(i / 5) * 1.4f};
                const glm::vec2 pos = data_->unit(cmd.unit).structure ? snapBuild(cmd.point)
                                                                      : Map::center(nearestFree(navMap(), Map::cellOf(cmd.point + offset)));
                spawn(cmd.unit, cmd.team, pos, cmd.team == 0 ? static_cast<int>(cmd.player) : -1);
                if (data_->unit(cmd.unit).structure)
                    break;
            }
        break;
    case CheatOp::Kill:
        for (const auto e : cmd.targets)
            if (alive(e) && registry_.all_of<Health>(e)) {
                registry_.remove<Invulnerable>(e);
                auto& h = registry_.get<Health>(e);
                h.hp = 0.0f;
                h.lastHitBy = static_cast<int>(cmd.player);
            }
        break;
    case CheatOp::Heal:
        for (auto [e, h] : registry_.view<Health>().each())
            if (owned(e) && h.hp > 0.0f) {
                h.hp = h.max;
                h.shield = h.shieldMax;
            }
        break;
    case CheatOp::Finish:
        for (auto [e, c] : registry_.view<Construction>().each())
            if (owned(e))
                c.progress = 1.0f;
        for (auto [e, f] : registry_.view<Factory>().each())
            if (owned(e) && !f.queue.empty())
                f.progress = data_->unit(f.queue.front()).produceTime;
        for (auto [e, lab] : registry_.view<Lab>().each())
            if (owned(e) && lab.topic < data_->research().size())
                lab.progress = data_->research(lab.topic).time;
        break;
    case CheatOp::Research:
        for (std::size_t i = 0; i < data_->research().size(); ++i)
            grantResearch(cmd.player, static_cast<ResearchId>(i));
        refreshOwned(cmd.player);
        break;
    case CheatOp::Xp:
        for (const auto e : cmd.targets)
            if (alive(e) && owned(e))
                if (auto* prog = registry_.try_get<Progression>(e))
                    prog->xp = std::max(prog->xp, kHeroXp);
        break;
    case CheatOp::God:
        player.god = !player.god;
        break;
    case CheatOp::Win:
    case CheatOp::Lose:
        if (defense_.state == MatchState::Playing) {
            const bool win = cmd.op == CheatOp::Win;
            defense_.state = win ? MatchState::Victory : MatchState::Defeat;
            events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, win ? 1.0f : 0.0f});
        }
        break;
    case CheatOp::Wave:
        defense_.nextWaveIn = std::min(defense_.nextWaveIn, kTickDt);
        break;
    default:
        break;
    }
}

}
