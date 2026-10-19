#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "core/World.h"

namespace vw {

namespace {

struct Scaling {
    float hp;
    float damage;
    float count;
};

constexpr Scaling kScaling[] = {
    {0.7f, 0.7f, 0.75f},
    {1.0f, 1.0f, 1.0f},
    {1.35f, 1.25f, 1.25f},
    {1.8f, 1.5f, 1.5f},
};

constexpr float kCaptureRate = 0.05f;
constexpr int kCaptureCap = 4;

const Scaling& scaling(Difficulty d) {
    return kScaling[static_cast<std::size_t>(d)];
}

}

const char* difficultyName(Difficulty d) {
    switch (d) {
    case Difficulty::Easy:
        return "easy";
    case Difficulty::Normal:
        return "normal";
    case Difficulty::Hard:
        return "hard";
    case Difficulty::Nightmare:
        return "nightmare";
    }
    return "normal";
}

bool parseDifficulty(std::string_view text, Difficulty& out) {
    for (const auto d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard, Difficulty::Nightmare})
        if (text == difficultyName(d)) {
            out = d;
            return true;
        }
    return false;
}

const char* gameModeName(GameMode m) {
    return m == GameMode::Endless ? "endless" : m == GameMode::Arena ? "arena" : m == GameMode::Skirmish ? "skirmish" : "defense";
}

bool parseGameMode(std::string_view text, GameMode& out) {
    for (const auto m : {GameMode::Defense, GameMode::Endless, GameMode::Arena, GameMode::Skirmish})
        if (text == gameModeName(m)) {
            out = m;
            return true;
        }
    return false;
}

int World::activePlayers() const {
    return static_cast<int>(std::count_if(players_.begin(), players_.end(), [](const PlayerState& p) { return p.active; }));
}

float World::endlessCycle() const {
    const auto waves = map_ ? static_cast<int>(map_->defense().waves.size()) : 0;
    return mode_ == GameMode::Endless && waves > 0 ? static_cast<float>(std::max(0, defense_.wave - 1) / waves) : 0.0f;
}

float World::enemyHpScale() const {
    return scaling(difficulty_).hp * (1.0f + 0.1f * static_cast<float>(std::max(0, activePlayers() - 1))) * (1.0f + 0.4f * endlessCycle());
}

float World::enemyDamageScale() const {
    return scaling(difficulty_).damage * (1.0f + 0.25f * endlessCycle());
}

float World::enemyCountScale() const {
    return scaling(difficulty_).count * (1.0f + 0.25f * static_cast<float>(std::max(0, activePlayers() - 1))) * (1.0f + 0.2f * endlessCycle());
}

void World::grantTeamEnergy(float amount) {
    const int n = activePlayers();
    if (n == 0)
        return;
    const float share = amount / static_cast<float>(n);
    for (auto& p : players_)
        if (p.active)
            p.energy += share;
}

void World::handle(const StructureCommand& cmd) {
    if (!alive(cmd.target) || !registry_.all_of<Structure, Owner>(cmd.target) || registry_.get<Owner>(cmd.target).player != cmd.player)
        return;
    const auto& def = data_->unit(registry_.get<Unit>(cmd.target).def);
    if (cmd.op == StructureOp::Rally) {
        if (auto* f = registry_.try_get<Factory>(cmd.target)) {
            f->rally = cmd.point;
            f->hasRally = true;
        }
    } else if (cmd.op == StructureOp::Research) {
        auto* lab = registry_.try_get<Lab>(cmd.target);
        if (!lab || lab->topic != kInvalidId || registry_.all_of<Construction>(cmd.target) || !canResearch(cmd.player, cmd.slot))
            return;
        const auto& topic = data_->research(cmd.slot);
        if (players_[cmd.player].energy < topic.cost)
            return;
        players_[cmd.player].energy -= topic.cost;
        lab->topic = cmd.slot;
        lab->progress = 0.0f;
    } else if (cmd.op == StructureOp::Cancel) {
        if (auto* lab = registry_.try_get<Lab>(cmd.target); lab && lab->topic < data_->research().size()) {
            if (cmd.player < kMaxPlayers)
                players_[cmd.player].energy += data_->research(lab->topic).cost;
            lab->topic = kInvalidId;
            lab->progress = 0.0f;
            return;
        }
        auto* f = registry_.try_get<Factory>(cmd.target);
        if (!f || cmd.slot >= f->queue.size())
            return;
        if (cmd.player < kMaxPlayers)
            players_[cmd.player].energy += data_->unit(f->queue[cmd.slot]).cost;
        f->queue.erase(f->queue.begin() + cmd.slot);
        if (cmd.slot == 0)
            f->progress = 0.0f;
    } else if (cmd.op == StructureOp::Demolish) {
        if (def.deployOnly || core_ == cmd.target)
            return;
        if (cmd.player < kMaxPlayers)
            players_[cmd.player].energy += def.cost * 0.5f;
        if (auto* f = registry_.try_get<Factory>(cmd.target); f && cmd.player < kMaxPlayers)
            for (const auto u : f->queue)
                players_[cmd.player].energy += data_->unit(u).cost;
        if (auto* lab = registry_.try_get<Lab>(cmd.target); lab && lab->topic < data_->research().size() && cmd.player < kMaxPlayers)
            players_[cmd.player].energy += data_->research(lab->topic).cost;
        auto& health = registry_.get<Health>(cmd.target);
        health.hp = 0.0f;
        health.lastHitBy = -1;
    }
}

void World::updateFactories() {
    struct Ready {
        entt::entity factory;
        UnitDefId unit;
    };
    std::vector<Ready> ready;
    for (auto [e, factory] : registry_.view<Factory>(entt::exclude<Construction>).each()) {
        if (factory.queue.empty() || !alive(e))
            continue;
        factory.progress += kTickDt;
        if (factory.progress >= data_->unit(factory.queue.front()).produceTime)
            ready.push_back({e, factory.queue.front()});
    }
    for (const auto& r : ready) {
        auto& factory = registry_.get<Factory>(r.factory);
        const glm::vec2 at = registry_.get<Position>(r.factory).value;
        const float radius = registry_.get<Unit>(r.factory).radius;
        const glm::vec2 dir = factory.hasRally && glm::distance(factory.rally, at) > 0.5f ? glm::normalize(factory.rally - at) : glm::vec2(0.0f, 1.0f);
        const glm::vec2 pos = Map::center(nearestFree(navMap(), Map::cellOf(at + dir * (radius + 1.4f))));
        const auto* owner = registry_.try_get<Owner>(r.factory);
        const auto made = spawn(r.unit, registry_.get<Team>(r.factory).id, pos, owner ? static_cast<int>(owner->player) : -1);
        if (factory.hasRally && registry_.all_of<Order>(made))
            registry_.get<Order>(made) = {OrderType::Move, factory.rally, entt::null, pos};
        factory.queue.erase(factory.queue.begin());
        factory.progress = 0.0f;
    }
}

void World::handle(const DeployCommand& cmd) {
    for (const auto e : cmd.targets) {
        if (!alive(e))
            continue;
        const auto* owner = registry_.try_get<Owner>(e);
        if (!owner || owner->player != cmd.player)
            continue;
        const auto& def = data_->unit(registry_.get<Unit>(e).def);
        const UnitDefId into = def.deploy != kInvalidUnitDef ? def.deploy : def.undeploy;
        if (into == kInvalidUnitDef || registry_.all_of<Construction>(e) || registry_.all_of<Carried>(e))
            continue;
        glm::vec2 pos = registry_.get<Position>(e).value;
        const auto& target = data_->unit(into);
        if (target.structure) {
            pos = snapBuild(pos);
            std::vector<SiteBlocker> around;
            for (auto [o, p, unit, t] : registry_.view<const Position, const Unit, const Team>().each())
                if (o != e && alive(o) && glm::distance(p.value, pos) < target.radius + data_->maxRadius() + kEnemyBuildGap + 1.0f)
                    around.push_back({p.value, unit.radius, registry_.all_of<Structure>(o), data_->unit(unit.def).wall, t.id});
            const SiteStatus status = vw::checkSite(*map_, target, pos, registry_.get<Team>(e).id, around);
            if (status == SiteStatus::Blocked || status == SiteStatus::NearEnemy)
                continue;
        }
        const auto& health = registry_.get<Health>(e);
        const float frac = health.max > 0.0f ? health.hp / health.max : 1.0f;
        const std::uint8_t team = registry_.get<Team>(e).id;
        const std::uint8_t player = owner->player;
        std::optional<std::string> tag;
        if (const auto* t = registry_.try_get<Tag>(e))
            tag = t->name;
        const bool wasCore = core_ == e;
        if (registry_.all_of<Structure>(e))
            navDirty_ = true;
        registry_.destroy(e);
        const auto made = spawn(into, team, pos, player);
        if (tag)
            registry_.emplace<Tag>(made, Tag{*tag});
        if (wasCore)
            core_ = made;
        auto& h = registry_.get<Health>(made);
        h.hp = std::max(1.0f, h.max * frac);
        events_.push_back({EventType::Summon, made, entt::null, pos, pos, 0.0f});
    }
}

void World::handle(const RecruitCommand& cmd) {
    if (!map_ || cmd.player >= kMaxPlayers || cmd.unit >= data_->units().size())
        return;
    auto& player = players_[cmd.player];
    const auto& def = data_->unit(cmd.unit);
    if (!player.active || def.cost <= 0.0f || player.energy < def.cost)
        return;
    glm::vec2 origin = playerBase(cmd.player);
    float ring = 0.0f;
    if (map_->mission().enabled) {
        auto own = [&](UnitDefId want, glm::vec2* where, float* radius) {
            for (auto [e, unit, owner, pos] : registry_.view<const Unit, const Owner, const Position>(entt::exclude<Construction>).each())
                if (unit.def == want && owner.player == cmd.player && alive(e)) {
                    if (where)
                        *where = pos.value;
                    if (radius)
                        *radius = unit.radius;
                    return true;
                }
            return false;
        };
        if (def.needs != kInvalidUnitDef && !own(def.needs, nullptr, nullptr))
            return;
        if (def.research != kInvalidId && !researched(cmd.player, def.research))
            return;
        if (!def.producedAt.empty()) {
            entt::entity best = entt::null;
            std::size_t shortest = kQueueSize;
            auto consider = [&](entt::entity f) {
                if (!alive(f) || registry_.all_of<Construction>(f) || !registry_.all_of<Factory, Owner, Unit>(f))
                    return;
                if (registry_.get<Owner>(f).player != cmd.player)
                    return;
                const UnitDefId kind = registry_.get<Unit>(f).def;
                if (std::find(def.producedAt.begin(), def.producedAt.end(), kind) == def.producedAt.end())
                    return;
                const auto size = registry_.get<Factory>(f).queue.size();
                if (size < shortest) {
                    shortest = size;
                    best = f;
                }
            };
            if (cmd.factory != entt::null && registry_.valid(cmd.factory))
                consider(cmd.factory);
            if (best == entt::null)
                for (auto [f, factory] : registry_.view<Factory>().each())
                    consider(f);
            if (best == entt::null)
                return;
            player.energy -= def.cost;
            registry_.get<Factory>(best).queue.push_back(cmd.unit);
            return;
        }
    }
    player.energy -= def.cost;
    const int slot = recruitCount_++ % 9;
    const glm::vec2 offset = ring > 0.0f ? glm::vec2(std::cos(static_cast<float>(slot) * 0.7f), std::sin(static_cast<float>(slot) * 0.7f)) * (ring + 1.5f)
                                         : glm::vec2(static_cast<float>(slot % 3 - 1) * 1.5f, static_cast<float>(slot / 3 - 1) * 1.5f);
    const glm::vec2 pos = Map::center(nearestFree(navMap(), Map::cellOf(origin + offset)));
    spawn(cmd.unit, player.team, pos, cmd.player);
}

SiteStatus World::checkSite(UnitDefId id, glm::vec2 pos, std::uint8_t team, bool phantoms) const {
    if (!map_ || id >= data_->units().size() || !data_->unit(id).buildable)
        return SiteStatus::Blocked;
    const auto& def = data_->unit(id);
    std::vector<SiteBlocker> around;
    const float reach = def.radius + data_->maxRadius() + kEnemyBuildGap + 1.0f;
    for (auto [e, p, unit, t] : registry_.view<const Position, const Unit, const Team>().each()) {
        if (glm::distance(p.value, pos) > reach || !alive(e))
            continue;
        const auto& other = data_->unit(unit.def);
        around.push_back({p.value, unit.radius, registry_.all_of<Structure>(e), other.wall, t.id});
    }
    if (phantoms) {
        auto addPhantom = [&](const Order& o, std::uint8_t t) {
            if (o.type != OrderType::Build || o.build >= data_->units().size() || glm::distance(o.point, pos) > reach)
                return;
            if (o.build == id && glm::distance(o.point, pos) < 0.3f)
                return;
            const auto& other = data_->unit(o.build);
            around.push_back({o.point, other.radius, true, other.wall, t});
        };
        for (auto [e, order, t] : registry_.view<const Order, const Team>().each()) {
            if (t.id != team)
                continue;
            addPhantom(order, t.id);
            if (const auto* q = registry_.try_get<OrderQueue>(e))
                for (const auto& o : q->items)
                    addPhantom(o, t.id);
        }
    }
    return vw::checkSite(*map_, def, pos, team, around);
}

void World::clearSite(UnitDefId id, glm::vec2 pos, std::uint8_t team) {
    const auto& def = data_->unit(id);
    for (auto [e, p, unit, t, order] : registry_.view<const Position, const Unit, const Team, Order>().each()) {
        if (t.id != team || (order.type != OrderType::None && order.type != OrderType::Hold) || data_->unit(unit.def).builder)
            continue;
        const glm::vec2 d = p.value - pos;
        const float dist = glm::length(d);
        const float need = def.radius + unit.radius + 0.8f;
        if (dist >= need)
            continue;
        const glm::vec2 dir = dist > 1e-3f ? d / dist : glm::vec2(1.0f, 0.0f);
        glm::vec2 to = pos + dir * (need + 0.5f);
        if (map_)
            to = Map::center(nearestFree(navMap(), Map::cellOf(to)));
        order = {OrderType::Move, to, entt::null, to};
        registry_.remove<Navigator>(e);
    }
}

void World::assistConstruction() {
    if (tick_ % 15 != 0)
        return;
    for (auto [b, order, bp, bu, bt] : registry_.view<Order, const Position, const Unit, const Team>().each()) {
        if (order.type != OrderType::None || !data_->unit(bu.def).builder || registry_.all_of<OrderQueue>(b))
            continue;
        for (auto [s, construction, sp, su, st] : registry_.view<const Construction, const Position, const Unit, const Team>().each())
            if (st.id == bt.id && alive(s) && glm::distance(sp.value, bp.value) < 8.0f + su.radius) {
                order = {OrderType::Build, sp.value, entt::null, sp.value, static_cast<std::uint16_t>(su.def)};
                break;
            }
    }
}

entt::entity World::structureAt(glm::vec2 pos, UnitDefId def) const {
    for (auto [e, p, unit] : registry_.view<const Position, const Unit, const Structure>().each())
        if (unit.def == def && glm::distance(p.value, pos) < 0.3f && alive(e))
            return e;
    return entt::null;
}

glm::vec2 World::builderStep(entt::entity e, glm::vec2 pos, const Unit& unit, std::uint8_t team, Order& order) {
    if (order.build >= data_->units().size()) {
        finishOrder(e, order);
        return {};
    }
    const auto& def = data_->unit(order.build);
    entt::entity site = structureAt(order.point, order.build);
    if (site != entt::null && !registry_.all_of<Construction>(site)) {
        finishOrder(e, order);
        return {};
    }
    if (glm::distance(pos, order.point) > def.radius + unit.radius + 1.0f)
        return navigate(e, pos, order.point, unit.speed);
    registry_.remove<Navigator>(e);
    if (site != entt::null)
        return {};
    const auto* owner = registry_.try_get<Owner>(e);
    const SiteStatus status = checkSite(order.build, order.point, team);
    if (!owner || status == SiteStatus::Blocked || status == SiteStatus::NearEnemy) {
        finishOrder(e, order);
        return {};
    }
    if (status == SiteStatus::Occupied) {
        clearSite(order.build, order.point, team);
        return {};
    }
    auto& player = players_[owner->player];
    if (player.energy < def.cost) {
        finishOrder(e, order);
        return {};
    }
    player.energy -= def.cost;
    site = spawn(order.build, team, order.point, owner->player);
    registry_.emplace<Construction>(site);
    auto& health = registry_.get<Health>(site);
    health.hp = health.max * 0.1f;
    return {};
}

void World::updateConstruction() {
    updateStrikes();
    assistConstruction();
    updateRepair();
    for (auto [s, construction, pos, unit] : registry_.view<Construction, const Position, const Unit>().each()) {
        if (!alive(s))
            continue;
        int builders = 0;
        for (auto [b, order, bp, bu] : registry_.view<const Order, const Position, const Unit>().each())
            if (order.type == OrderType::Build && order.build == unit.def && glm::distance(order.point, pos.value) < 0.3f &&
                glm::distance(bp.value, pos.value) <= unit.radius + bu.radius + 1.6f)
                ++builders;
        if (builders == 0)
            continue;
        const float step = kTickDt * std::pow(static_cast<float>(builders), 0.75f) / std::max(data_->unit(unit.def).buildTime, 0.1f);
        construction.progress += step;
        auto& health = registry_.get<Health>(s);
        health.hp = std::min(health.max, health.hp + health.max * 0.9f * step);
    }
    std::vector<entt::entity> done;
    for (auto [s, construction] : registry_.view<const Construction>().each())
        if (construction.progress >= 1.0f)
            done.push_back(s);
    for (const auto s : done)
        registry_.remove<Construction>(s);
}

void World::updateProducers() {
    struct Pending {
        entt::entity producer;
        UnitDefId unit;
        glm::vec2 pos;
        int owner;
        std::uint8_t team;
    };
    std::vector<Pending> pending;
    for (auto [e, producer, pos, unit] : registry_.view<Producer, const Position, const Unit>(entt::exclude<Construction>).each()) {
        if (!alive(e))
            continue;
        std::erase_if(producer.spawned, [&](entt::entity s) { return !alive(s); });
        producer.timer -= kTickDt;
        if (producer.timer > 0.0f || static_cast<int>(producer.spawned.size()) >= producer.max)
            continue;
        producer.timer = producer.interval;
        const auto* owner = registry_.try_get<Owner>(e);
        const float angle = static_cast<float>(producer.spawned.size()) * 1.3f;
        const glm::vec2 at = pos.value + glm::vec2(std::cos(angle), std::sin(angle)) * (unit.radius + 1.2f);
        pending.push_back({e, producer.unit, at, owner ? static_cast<int>(owner->player) : -1, registry_.get<Team>(e).id});
    }
    for (const auto& p : pending) {
        const glm::vec2 at = map_ ? Map::center(nearestFree(navMap(), Map::cellOf(p.pos))) : p.pos;
        const auto spawned = spawn(p.unit, p.team, at, p.owner);
        registry_.get<Producer>(p.producer).spawned.push_back(spawned);
    }
}

void World::updateBosses() {
    struct Summon {
        UnitDefId unit;
        int count;
        glm::vec2 pos;
        std::uint8_t team;
        LaneFollower lane;
        bool hasLane;
    };
    std::vector<Summon> summons;
    std::vector<entt::entity> enraged;

    for (auto [e, pos, boss, health, unit, team, vel] : registry_.view<const Position, Boss, const Health, const Unit, const Team, Velocity>().each()) {
        const auto& def = *data_->unit(unit.def).boss;

        if (!boss.enraged && def.enrage && health.hp < health.max * def.enrage->hpBelow) {
            boss.enraged = true;
            enraged.push_back(e);
            events_.push_back({EventType::Enrage, e, entt::null, pos.value, pos.value});
        }

        if (boss.casting >= 0) {
            vel.value = {};
            boss.castTimer -= kTickDt;
            if (boss.castTimer > 0.0f)
                continue;
            const auto& ability = def.abilities[static_cast<std::size_t>(boss.casting)];
            const float dmg = ability.damage * enemyDamageScale() * (boss.enraged && def.enrage ? def.enrage->damage : 1.0f);
            std::vector<entt::entity> hits;
            queryAll(boss.castPos, ability.radius, [&](const SpatialHash::Item& item) {
                if (item.team == team.id || !alive(item.entity))
                    return;
                if (glm::distance(item.pos, boss.castPos) - item.radius <= ability.radius)
                    hits.push_back(item.entity);
            });
            for (const auto h : hits)
                damage(h, dmg, -1, e, DamageType::Explosive);
            events_.push_back({EventType::Slam, e, entt::null, boss.castPos, boss.castPos, ability.radius});
            boss.casting = -1;
            continue;
        }

        for (std::size_t i = 0; i < def.abilities.size(); ++i) {
            auto& cd = boss.cooldowns[i];
            cd -= kTickDt;
            if (cd > 0.0f)
                continue;
            const auto& ability = def.abilities[i];
            if (ability.type == AbilityType::Slam) {
                if (findEnemy(pos.value, team.id, ability.radius * 0.8f) == entt::null)
                    continue;
                boss.casting = static_cast<int>(i);
                boss.castTimer = ability.windup;
                boss.castPos = pos.value;
                vel.value = {};
                cd = ability.cooldown;
                events_.push_back({EventType::SlamWarn, e, entt::null, pos.value, pos.value, ability.radius});
                break;
            }
            if (findEnemy(pos.value, team.id, unit.sight) == entt::null)
                continue;
            const auto* lane = registry_.try_get<LaneFollower>(e);
            summons.push_back({ability.unitId, ability.count, pos.value, team.id, lane ? *lane : LaneFollower{}, lane != nullptr});
            cd = ability.cooldown;
            events_.push_back({EventType::Summon, e, entt::null, pos.value, pos.value});
        }
    }

    for (const auto e : enraged)
        refreshStats(e);

    for (const auto& s : summons)
        for (int i = 0; i < s.count; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(std::max(1, s.count)) * 6.2831853f;
            const glm::vec2 p = s.pos + glm::vec2(std::cos(a), std::sin(a)) * 2.5f;
            const auto u = spawn(s.unit, s.team, map_ ? Map::center(nearestFree(*map_, Map::cellOf(p))) : p);
            if (s.hasLane)
                registry_.emplace<LaneFollower>(u, s.lane);
        }
}

void World::updateResourcePoints() {
    if (!map_)
        return;
    const auto& defs = map_->resourcePoints();
    if (mode_ == GameMode::Skirmish) {
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const auto& def = defs[i];
            auto& st = points_[i];
            std::array<int, 16> counts{};
            spatial_.query(def.pos, def.radius, [&](const SpatialHash::Item& item) {
                if (glm::distance(item.pos, def.pos) <= def.radius && alive(item.entity) && item.team < counts.size())
                    ++counts[item.team];
            });
            int present = -1;
            int teams = 0;
            for (std::size_t t = 0; t < counts.size(); ++t)
                if (counts[t] > 0) {
                    ++teams;
                    present = static_cast<int>(t);
                }
            if (teams == 1 && present != st.owner) {
                st.control += kCaptureRate * static_cast<float>(std::min(counts[static_cast<std::size_t>(present)], kCaptureCap)) * kTickDt;
                if (st.control >= 1.0f) {
                    st.owner = present;
                    st.control = 0.0f;
                    events_.push_back({EventType::PointCaptured, entt::null, entt::null, def.pos, def.pos, static_cast<float>(std::min(present, 2))});
                }
            } else if (teams == 0 || present == st.owner) {
                st.control = std::max(0.0f, st.control - kCaptureRate * kTickDt);
            }
            if (st.owner >= kFirstSkirmishTeam)
                grantEnergyTo(static_cast<std::uint8_t>(st.owner), def.income * kTickDt);
        }
        return;
    }
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const auto& def = defs[i];
        auto& st = points_[i];
        int counts[2] = {0, 0};
        spatial_.query(def.pos, def.radius, [&](const SpatialHash::Item& item) {
            if (glm::distance(item.pos, def.pos) <= def.radius && alive(item.entity))
                ++counts[item.team & 1];
        });

        if (counts[0] > 0 && counts[1] == 0)
            st.control += kCaptureRate * static_cast<float>(std::min(counts[0], kCaptureCap)) * kTickDt;
        else if (counts[1] > 0 && counts[0] == 0)
            st.control -= kCaptureRate * static_cast<float>(std::min(counts[1], kCaptureCap)) * kTickDt;
        st.control = std::clamp(st.control, -1.0f, 1.0f);

        int owner = st.owner;
        if (st.control >= 1.0f)
            owner = 0;
        else if (st.control <= -1.0f)
            owner = 1;
        else if ((st.owner == 0 && st.control <= 0.0f) || (st.owner == 1 && st.control >= 0.0f))
            owner = -1;
        if (owner != st.owner) {
            st.owner = owner;
            events_.push_back({EventType::PointCaptured, entt::null, entt::null, def.pos, def.pos, static_cast<float>(owner)});
        }

        if (st.owner == 0)
            grantTeamEnergy(def.income * kTickDt);
    }
}

void World::spawnWave(const WaveDef& wave) {
    const float countScale = enemyCountScale();
    const auto mouth = portalMouth();
    if (mouth)
        events_.push_back({EventType::Summon, portal_, entt::null, *mouth, *mouth, 4.0f});
    for (const auto& group : wave.groups) {
        const UnitDefId id = data_->find(group.unit);
        if (id == kInvalidUnitDef)
            continue;
        const int count = std::max(1, static_cast<int>(std::lround(static_cast<float>(group.count) * countScale)));
        if (!group.raid) {
            for (const int lane : group.lanes)
                spawnOnLane(id, 1, lane, count, 5.0f, mouth);
            continue;
        }

        const auto& defs = map_->resourcePoints();
        if (defs.empty() || !alive(portal_))
            continue;
        const glm::vec2 from = registry_.get<Position>(portal_).value;
        std::size_t target = static_cast<std::size_t>(defense_.wave) % defs.size();
        float best = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < defs.size(); ++i)
            if (points_[i].owner == 0 && glm::distance(defs[i].pos, from) < best) {
                best = glm::distance(defs[i].pos, from);
                target = i;
            }
        const glm::vec2 dir = glm::normalize(defs[target].pos - from);
        const float startOffset = registry_.get<Unit>(portal_).radius + 2.0f;
        std::vector<entt::entity> raiders;
        for (int i = 0; i < count; ++i) {
            const glm::vec2 side(-dir.y, dir.x);
            const glm::vec2 p = from + dir * (startOffset + static_cast<float>(i / 4)) + side * (static_cast<float>(i % 4) - 1.5f);
            raiders.push_back(spawn(id, 1, Map::center(nearestFree(*map_, Map::cellOf(p)))));
        }
        apply(MoveCommand{raiders, defs[target].pos, true});
    }

    if (!wave.boss.empty()) {
        const UnitDefId id = data_->find(wave.boss);
        if (id != kInvalidUnitDef) {
            const auto spawned = spawnOnLane(id, 1, wave.bossLane, 1, 6.0f, mouth);
            defense_.boss = spawned.front();
        }
    }
}

void World::grantEnergyTo(std::uint8_t team, float amount) {
    int n = 0;
    for (const auto& p : players_)
        n += p.active && p.team == team;
    if (n == 0)
        return;
    for (auto& p : players_)
        if (p.active && p.team == team)
            p.energy += amount / static_cast<float>(n);
}

void World::updateSkirmish() {
    const auto& def = map_->defense();
    std::array<float, kMaxPlayers> income{};
    for (std::size_t i = 0; i < players_.size(); ++i)
        if (players_[i].active)
            income[i] = def.baseIncome * (players_[i].ai && difficulty_ >= Difficulty::Hard ? 1.25f : 1.0f);
    std::array<bool, 16> alive_{};
    for (auto [e, unit, owner, team] : registry_.view<const Unit, const Owner, const Team>().each()) {
        if (!alive(e) || owner.player >= kMaxPlayers)
            continue;
        if (team.id < alive_.size())
            alive_[team.id] = true;
        if (registry_.all_of<Structure>(e) && !registry_.all_of<Construction>(e))
            income[owner.player] += data_->unit(unit.def).income;
    }
    for (std::size_t i = 0; i < players_.size(); ++i)
        if (players_[i].active)
            players_[i].energy += income[i] * kTickDt;
    if (defense_.state != MatchState::Playing)
        return;
    int teams = 0;
    int last = -1;
    for (std::size_t t = kFirstSkirmishTeam; t < alive_.size(); ++t)
        if (alive_[t]) {
            ++teams;
            last = static_cast<int>(t);
        }
    skirmishTeams_ = std::max(skirmishTeams_, teams);
    if (skirmishTeams_ >= 2 && teams <= 1 && tick_ > kTickRate * 5) {
        defense_.state = MatchState::Victory;
        defense_.winner = last;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 1.0f});
    }
}

void World::updateDefense() {
    if (map_ && mode_ == GameMode::Skirmish) {
        updateSkirmish();
        return;
    }
    if (!map_ || !map_->defense().enabled)
        return;
    const auto& def = map_->defense();
    auto grantIncome = [&] {
        float income = def.baseIncome;
        for (auto [e, unit, team] : registry_.view<const Unit, const Team, const Structure>(entt::exclude<Construction>).each())
            if (team.id == 0 && alive(e))
                income += data_->unit(unit.def).income;
        grantTeamEnergy(income * kTickDt);
    };
    if (defense_.state != MatchState::Playing) {
        grantIncome();
        return;
    }

    if (defense_.boss != entt::null && !alive(defense_.boss))
        defense_.boss = entt::null;
    if (portal_ != entt::null && mode_ != GameMode::Endless && defense_.totalWaves > 0 && defense_.wave >= defense_.totalWaves &&
        registry_.all_of<Invulnerable>(portal_) && anchorsAlive() == 0)
        registry_.remove<Invulnerable>(portal_);

    if (core_ != entt::null && !alive(core_)) {
        defense_.state = MatchState::Defeat;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 0.0f});
        return;
    }

    if (portal_ != entt::null && !alive(portal_)) {
        defense_.state = MatchState::Victory;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 1.0f});
        return;
    }

    grantIncome();

    if (mode_ == GameMode::Endless && !def.waves.empty()) {
        defense_.nextWaveIn -= kTickDt;
        if (defense_.nextWaveIn <= 0.0f) {
            const auto index = static_cast<std::size_t>(defense_.wave) % def.waves.size();
            ++defense_.wave;
            spawnWave(def.waves[index]);
            events_.push_back({EventType::WaveStart, entt::null, entt::null, {}, {}, static_cast<float>(defense_.wave)});
            const float delay = def.waves[(index + 1) % def.waves.size()].delay;
            defense_.nextWaveIn = delay > 0.0f ? delay : def.waveInterval;
        }
        return;
    }

    if (defense_.wave < defense_.totalWaves) {
        defense_.nextWaveIn -= kTickDt;
        if (defense_.nextWaveIn <= 0.0f) {
            spawnWave(def.waves[static_cast<std::size_t>(defense_.wave)]);
            ++defense_.wave;
            events_.push_back({EventType::WaveStart, entt::null, entt::null, {}, {}, static_cast<float>(defense_.wave)});
            if (defense_.wave < defense_.totalWaves) {
                const float delay = def.waves[static_cast<std::size_t>(defense_.wave)].delay;
                defense_.nextWaveIn = delay > 0.0f ? delay : def.waveInterval;
            } else {
                defense_.nextWaveIn = 0.0f;
            }
        }
        return;
    }

    if (map_->mission().enabled)
        return;
    bool enemiesLeft = false;
    for (auto [e, team] : registry_.view<const Team>(entt::exclude<Structure>).each())
        if (team.id == 1 && alive(e)) {
            enemiesLeft = true;
            break;
        }
    if (!enemiesLeft) {
        defense_.state = MatchState::Victory;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 1.0f});
    }
}

}
