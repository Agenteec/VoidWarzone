#include "core/World.h"

#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace vw {

using nlohmann::json;

namespace {

constexpr int kSaveVersion = 1;

}

std::string makeSaveFile(const SaveHeader& header, const World& world) {
    json out = {{"map", header.map}, {"mode", static_cast<int>(header.mode)}, {"difficulty", static_cast<int>(header.difficulty)},
                {"tick", world.tick()}, {"world", json::parse(world.saveState())}};
    return out.dump(1);
}

SaveHeader readSaveHeader(std::string_view text) {
    const json in = json::parse(text);
    SaveHeader h;
    h.map = in.at("map").get<std::string>();
    h.mode = static_cast<GameMode>(std::clamp(in.value("mode", 0), 0, 2));
    h.difficulty = static_cast<Difficulty>(std::clamp(in.value("difficulty", 1), 0, 3));
    h.tick = in.value("tick", std::uint64_t{0});
    return h;
}

bool validSaveName(std::string_view name) {
    return !name.empty() && name.size() <= 48 &&
           name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string_view::npos;
}

std::string World::saveState() const {
    std::unordered_map<entt::entity, int> index;
    std::vector<entt::entity> units;
    for (auto [e, unit] : registry_.view<const Unit>().each()) {
        if (!alive(e))
            continue;
        index[e] = static_cast<int>(units.size());
        units.push_back(e);
    }
    auto ref = [&](entt::entity e) {
        const auto it = index.find(e);
        return it != index.end() ? it->second : -1;
    };

    json out;
    out["version"] = kSaveVersion;
    out["tick"] = tick_;
    out["recruitCount"] = recruitCount_;
    out["defense"] = {{"wave", defense_.wave},
                      {"totalWaves", defense_.totalWaves},
                      {"nextWaveIn", defense_.nextWaveIn},
                      {"state", static_cast<int>(defense_.state)},
                      {"boss", ref(defense_.boss)}};
    out["core"] = ref(core_);
    out["portal"] = ref(portal_);
    json players = json::array();
    for (const auto& p : players_) {
        json stash = json::array();
        for (const auto item : p.stash)
            stash.push_back(data_->item(item).id);
        json research = json::array();
        for (std::size_t i = 0; i < p.researched.size(); ++i)
            if (p.researched[i])
                research.push_back(data_->research(static_cast<ResearchId>(i)).id);
        players.push_back({{"active", p.active}, {"energy", p.energy}, {"kills", p.kills}, {"shards", p.shards}, {"stash", stash}, {"research", research}});
    }
    out["players"] = players;
    json points = json::array();
    for (const auto& p : points_)
        points.push_back({{"control", p.control}, {"owner", p.owner}});
    out["points"] = points;
    json spawners = json::array();
    for (auto [e, s] : registry_.view<const Spawner>().each())
        spawners.push_back({{"timer", s.timer}, {"building", ref(s.building)}});
    out["spawners"] = spawners;
    out["mission"] = {{"objectives", objectiveState_}, {"fired", triggerFired_}, {"next", triggerNext_}, {"started", missionStarted_}};

    json list = json::array();
    for (const auto e : units) {
        const auto& unit = registry_.get<Unit>(e);
        const auto& health = registry_.get<Health>(e);
        json u = {{"def", data_->unit(unit.def).id},
                  {"team", registry_.get<Team>(e).id},
                  {"pos", {registry_.get<Position>(e).value.x, registry_.get<Position>(e).value.y}},
                  {"facing", registry_.get<Facing>(e).angle},
                  {"hp", health.hp},
                  {"shield", health.shield}};
        if (const auto* owner = registry_.try_get<Owner>(e))
            u["owner"] = owner->player;
        if (registry_.all_of<Invulnerable>(e))
            u["invulnerable"] = true;
        if (const auto* tag = registry_.try_get<Tag>(e))
            u["tag"] = tag->name;
        if (const auto* c = registry_.try_get<Construction>(e))
            u["construction"] = c->progress;
        if (const auto* p = registry_.try_get<Producer>(e))
            u["produceTimer"] = p->timer;
        if (const auto* lab = registry_.try_get<Lab>(e); lab && lab->topic < data_->research().size())
            u["research"] = {data_->research(lab->topic).id, lab->progress};
        if (const auto* f = registry_.try_get<Factory>(e)) {
            json queue = json::array();
            for (const auto q : f->queue)
                queue.push_back(data_->unit(q).id);
            u["factory"] = {{"queue", queue}, {"progress", f->progress}, {"rally", {f->rally.x, f->rally.y}}, {"hasRally", f->hasRally}};
        }
        if (const auto* lane = registry_.try_get<LaneFollower>(e))
            u["lane"] = {lane->lane, lane->waypoint, lane->reverse};
        if (const auto* carried = registry_.try_get<Carried>(e))
            u["carrier"] = ref(carried->transport);
        if (const auto* prog = registry_.try_get<Progression>(e)) {
            json skills = json::array();
            for (const auto& s : prog->skills)
                skills.push_back({data_->skill(s.skill).id, s.level});
            json items = json::array();
            for (const auto item : prog->items)
                items.push_back(data_->item(item).id);
            u["progression"] = {{"rank", static_cast<int>(prog->rank)},
                                {"level", prog->level},
                                {"xp", prog->xp},
                                {"points", prog->skillPoints},
                                {"skills", skills},
                                {"items", items}};
        }
        if (const auto* mods = registry_.try_get<Modules>(e)) {
            json slots = json::array();
            for (const auto m : mods->slots)
                slots.push_back(m == kInvalidId ? json(nullptr) : json(data_->module(m).id));
            u["modules"] = slots;
        }
        list.push_back(std::move(u));
    }
    out["units"] = std::move(list);
    return out.dump();
}

entt::entity World::loadUnit(const json& u) {
    const UnitDefId def = data_->find(u.at("def").get<std::string>());
    if (def == kInvalidUnitDef)
        throw std::runtime_error("unknown unit in save: " + u.at("def").get<std::string>());
    const glm::vec2 pos{u.at("pos").at(0).get<float>(), u.at("pos").at(1).get<float>()};
    const auto e = spawn(def, u.at("team").get<std::uint8_t>(), pos, u.value("owner", -1));
    registry_.get<Facing>(e).angle = u.value("facing", 0.0f);
    if (auto* turret = registry_.try_get<Turret>(e))
        turret->yaw = registry_.get<Facing>(e).angle;
    if (u.value("invulnerable", false))
        registry_.emplace_or_replace<Invulnerable>(e);
    if (u.contains("construction"))
        registry_.emplace_or_replace<Construction>(e, Construction{u.at("construction").get<float>()});
    if (auto* p = registry_.try_get<Producer>(e))
        p->timer = u.value("produceTimer", p->timer);
    if (auto* lab = registry_.try_get<Lab>(e); lab && u.contains("research")) {
        lab->topic = data_->findResearch(u.at("research").at(0).get<std::string>());
        lab->progress = u.at("research").at(1).get<float>();
    }
    if (auto* f = registry_.try_get<Factory>(e); f && u.contains("factory")) {
        const auto& fj = u.at("factory");
        for (const auto& q : fj.value("queue", json::array()))
            if (const UnitDefId id = data_->find(q.get<std::string>()); id != kInvalidUnitDef)
                f->queue.push_back(id);
        f->progress = fj.value("progress", 0.0f);
        f->hasRally = fj.value("hasRally", false);
        if (const auto r = fj.find("rally"); r != fj.end())
            f->rally = {r->at(0).get<float>(), r->at(1).get<float>()};
    }
    if (u.contains("lane")) {
        const auto& l = u.at("lane");
        registry_.emplace_or_replace<LaneFollower>(e, LaneFollower{l.at(0).get<int>(), l.at(1).get<int>(), l.at(2).get<bool>()});
    }
    if (const auto it = u.find("progression"); it != u.end() && registry_.all_of<Progression>(e)) {
        auto& prog = registry_.get<Progression>(e);
        prog.rank = static_cast<Rank>(std::clamp(it->value("rank", 0), 0, 3));
        prog.level = it->value("level", 0);
        prog.xp = it->value("xp", 0.0f);
        prog.skillPoints = it->value("points", 0);
        for (const auto& s : it->value("skills", json::array()))
            if (const SkillId id = data_->findSkill(s.at(0).get<std::string>()); id != kInvalidId)
                prog.skills.push_back({id, s.at(1).get<std::uint8_t>(), 0.0f});
        for (const auto& item : it->value("items", json::array()))
            if (const ItemId id = data_->findItem(item.get<std::string>()); id != kInvalidId)
                prog.items.push_back(id);
    }
    if (const auto it = u.find("modules"); it != u.end() && registry_.all_of<Modules>(e)) {
        auto& mods = registry_.get<Modules>(e);
        for (std::size_t s = 0; s < mods.slots.size() && s < it->size(); ++s) {
            if ((*it)[s].is_null())
                continue;
            const ModuleId m = data_->findModule((*it)[s].get<std::string>());
            if (m == kInvalidId)
                continue;
            mods.slots[s] = m;
            if (data_->module(m).slot == ModuleSlot::Weapon && data_->module(m).weapon)
                registry_.emplace_or_replace<SecondaryWeapon>(e, SecondaryWeapon{*data_->module(m).weapon, 0.0f, entt::null});
        }
    }
    refreshStats(e);
    auto& health = registry_.get<Health>(e);
    health.hp = std::min(u.value("hp", health.max), health.max);
    health.shield = std::min(u.value("shield", health.shieldMax), health.shieldMax);
    if (u.contains("tag"))
        registry_.emplace_or_replace<Tag>(e, Tag{u.at("tag").get<std::string>()});
    return e;
}

void World::loadState(std::string_view text) {
    json in = json::parse(text);
    if (in.contains("world"))
        in = in.at("world");
    if (in.value("version", 0) != kSaveVersion)
        throw std::runtime_error("unsupported save version");

    std::vector<entt::entity> old;
    for (auto e : registry_.view<Unit>())
        old.push_back(e);
    for (auto e : registry_.view<Projectile>())
        old.push_back(e);
    for (const auto e : old)
        registry_.destroy(e);
    strikes_.clear();
    respawns_.clear();

    std::vector<entt::entity> made;
    const json units = in.at("units");
    for (const auto& u : units)
        made.push_back(loadUnit(u));
    auto at = [&](int i) { return i >= 0 && i < static_cast<int>(made.size()) ? made[static_cast<std::size_t>(i)] : entt::null; };
    for (std::size_t i = 0; i < made.size(); ++i) {
        const int carrier = units[i].value("carrier", -1);
        const auto transport = at(carrier);
        if (transport == entt::null || !registry_.all_of<Cargo>(transport))
            continue;
        registry_.emplace_or_replace<Carried>(made[i], Carried{transport});
        registry_.get<Cargo>(transport).units.push_back(made[i]);
    }

    tick_ = in.value("tick", std::uint64_t{0});
    recruitCount_ = in.value("recruitCount", 0);
    core_ = at(in.value("core", -1));
    portal_ = at(in.value("portal", -1));
    const auto& d = in.at("defense");
    defense_.wave = d.value("wave", 0);
    defense_.totalWaves = d.value("totalWaves", defense_.totalWaves);
    defense_.nextWaveIn = d.value("nextWaveIn", 0.0f);
    defense_.state = static_cast<MatchState>(std::clamp(d.value("state", 0), 0, 2));
    defense_.boss = at(d.value("boss", -1));
    const json players = in.value("players", json::array());
    for (std::size_t i = 0; i < players.size() && i < players_.size(); ++i) {
        auto& p = players_[i];
        p.active = false;
        p.energy = players[i].value("energy", 0.0f);
        p.kills = players[i].value("kills", 0);
        p.shards = players[i].value("shards", 0);
        p.stash.clear();
        for (const auto& item : players[i].value("stash", json::array()))
            if (const ItemId id = data_->findItem(item.get<std::string>()); id != kInvalidId)
                p.stash.push_back(id);
        saved_[i] = players[i].value("active", false);
        p.researched.clear();
        for (const auto& t : players[i].value("research", json::array()))
            if (const ResearchId id = data_->findResearch(t.get<std::string>()); id != kInvalidId)
                grantResearch(static_cast<std::uint8_t>(i), id);
        if (!p.researched.empty())
            refreshOwned(static_cast<std::uint8_t>(i));
    }
    if (const auto it = in.find("mission"); it != in.end()) {
        objectiveState_ = it->value("objectives", std::vector<std::uint8_t>{});
        triggerFired_ = it->value("fired", std::vector<int>{});
        triggerNext_ = it->value("next", std::vector<float>{});
        missionStarted_ = false;
    }
    const json points = in.value("points", json::array());
    for (std::size_t i = 0; i < points.size() && i < points_.size(); ++i)
        points_[i] = {points[i].value("control", 0.0f), points[i].value("owner", -1)};
    const json spawners = in.value("spawners", json::array());
    std::size_t k = 0;
    for (auto [e, s] : registry_.view<Spawner>().each()) {
        if (k < spawners.size()) {
            s.timer = spawners[k].value("timer", s.timer);
            s.building = at(spawners[k].value("building", -1));
        }
        ++k;
    }
    navDirty_ = true;
    ++builtVersion_;
    events_.clear();
}

std::string World::exportRoster(std::uint8_t player, std::size_t limit) const {
    const json all = json::parse(saveState());
    std::vector<json> units;
    for (const auto& u : all.at("units")) {
        if (u.value("owner", -1) != player || data_->unit(data_->find(u.at("def").get<std::string>())).structure)
            continue;
        json copy = u;
        copy.erase("lane");
        copy.erase("carrier");
        copy.erase("construction");
        copy.erase("tag");
        units.push_back(std::move(copy));
    }
    auto score = [](const json& u) {
        const auto it = u.find("progression");
        return it == u.end() ? 0.0f : static_cast<float>(it->value("rank", 0)) * 10000.0f + it->value("xp", 0.0f);
    };
    std::stable_sort(units.begin(), units.end(), [&](const json& a, const json& b) { return score(a) > score(b); });
    if (units.size() > limit)
        units.resize(limit);
    return json(units).dump();
}

std::vector<entt::entity> World::spawnRoster(std::uint8_t player, std::string_view roster) {
    std::vector<entt::entity> out;
    if (roster.empty() || !map_)
        return out;
    const json list = json::parse(roster);
    const glm::vec2 base = playerBase(player);
    int i = 0;
    for (auto u : list) {
        if (data_->find(u.value("def", "")) == kInvalidUnitDef)
            continue;
        const glm::vec2 offset{static_cast<float>(i % 4) * 1.7f - 2.5f, static_cast<float>(i / 4) * 1.7f + 3.0f};
        const glm::vec2 pos = Map::center(nearestFree(*map_, Map::cellOf(base + offset)));
        u["pos"] = {pos.x, pos.y};
        u["owner"] = player;
        u["team"] = 0;
        u.erase("hp");
        out.push_back(loadUnit(u));
        ++i;
    }
    return out;
}

bool World::resumePlayer(std::uint8_t player) {
    if (player >= kMaxPlayers || !saved_[player])
        return false;
    saved_[player] = false;
    players_[player].active = true;
    return true;
}

}
