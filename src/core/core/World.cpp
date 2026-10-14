#include "core/World.h"

#include "core/Movement.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <variant>

#include <glm/geometric.hpp>

namespace vw {

namespace {

constexpr float kLeashFactor = 1.5f;
constexpr float kWaypointReach = 2.5f;
constexpr float kArriveDistance = 0.3f;
constexpr float kRangeSlack = 0.1f;
constexpr float kAimTolerance = 0.12f;
constexpr float kPitchRate = 1.5f;
constexpr float kLobGravity = 30.0f;

float wrapAngle(float a) {
    constexpr float pi = std::numbers::pi_v<float>;
    a = std::fmod(a + pi, 2.0f * pi);
    return a < 0.0f ? a + pi : a - pi;
}

float approach(float from, float to, float maxStep) {
    const float d = wrapAngle(to - from);
    return wrapAngle(from + std::clamp(d, -maxStep, maxStep));
}
constexpr int kRepathTicks = 12;
constexpr int kScanInterval = 3;
constexpr int kPathsPerTick = 8;
constexpr int kStuckTicks = 20;
constexpr int kDetourTicks = 14;

glm::vec2 seek(glm::vec2 pos, glm::vec2 goal, float speed) {
    return vw::seek(pos, goal, speed, kTickDt);
}

}

World::World() {
    spatial_.reset(128.0f, 128.0f, 3.0f);
    structures_.reset(128.0f, 128.0f, 8.0f);
}

World::World(const GameData& data, const Map& map, Difficulty difficulty) : data_(&data), map_(&map), difficulty_(difficulty) {
    spatial_.reset(static_cast<float>(map.width()), static_cast<float>(map.height()), 3.0f);
    structures_.reset(static_cast<float>(map.width()), static_cast<float>(map.height()), 8.0f);
    for (const auto& def : data.units())
        (def.structure ? maxStructureRadius_ : maxUnitRadius_) = std::max(def.structure ? maxStructureRadius_ : maxUnitRadius_, def.radius);
    nav_ = std::make_unique<Map>(map);

    std::unordered_map<std::string, entt::entity> tagged;
    for (const auto& u : map.units()) {
        const UnitDefId id = data.find(u.unit);
        if (id == kInvalidUnitDef)
            throw std::runtime_error("unknown unit: " + u.unit);
        const auto e = spawn(id, u.team, u.pos, u.owner);
        if (!u.tag.empty())
            tagged[u.tag] = e;
        if (u.role == "core")
            core_ = e;
        else if (u.role == "portal")
            portal_ = e;
        if (u.role == "portal" && map.defense().enabled)
            registry_.emplace<Invulnerable>(e);
    }

    points_.assign(map.resourcePoints().size(), {});
    const auto& def = map.defense();
    defense_.totalWaves = static_cast<int>(def.waves.size());
    defense_.nextWaveIn = def.firstWaveDelay;

    for (const auto& s : map.spawners()) {
        const UnitDefId id = data.find(s.unit);
        if (id == kInvalidUnitDef)
            throw std::runtime_error("unknown unit: " + s.unit);
        const auto e = registry_.create();
        entt::entity building = entt::null;
        if (!s.building.empty()) {
            const auto it = tagged.find(s.building);
            if (it == tagged.end())
                throw std::runtime_error("unknown building: " + s.building);
            building = it->second;
        }
        registry_.emplace<Spawner>(e, Spawner{id, s.team, s.lane, s.interval, s.delay, s.count, s.offset, building});
    }

    rebuildNav();
    for (const auto& lane : map.lanes())
        for (const auto& p : lane.points)
            flowField(p);
}

entt::entity World::spawn(UnitDefId defId, std::uint8_t team, glm::vec2 pos, int owner) {
    const UnitDef& def = data_->unit(defId);
    const auto e = registry_.create();
    registry_.emplace<Position>(e, pos);
    registry_.emplace<PrevPosition>(e, pos);
    registry_.emplace<Velocity>(e);
    registry_.emplace<Facing>(e, team == 0 ? 0.0f : std::numbers::pi_v<float>);
    registry_.emplace<Unit>(e, Unit{defId, def.radius, def.speed, def.sight});
    registry_.emplace<Team>(e, team);
    const bool enemy = team != 0;
    const float maxHp = def.maxHp * (enemy ? enemyHpScale() : 1.0f);
    registry_.emplace<Health>(e, maxHp, maxHp, def.armor);
    registry_.emplace<Target>(e);
    BaseStats base{maxHp, def.armor, def.speed};
    if (def.weapon) {
        WeaponDef w = *def.weapon;
        if (enemy)
            w.damage *= enemyDamageScale();
        registry_.emplace<Weapon>(e, w, 0.0f);
        registry_.emplace<Turret>(e, Turret{registry_.get<Facing>(e).angle, 0.0f});
        base.damage = w.damage;
        base.cooldown = w.cooldown;
        base.range = w.range;
    }
    registry_.emplace<BaseStats>(e, base);
    if (owner >= 0 && !def.structure)
        registry_.emplace<Progression>(e);
    if (def.boss) {
        Boss boss;
        for (const auto& a : def.boss->abilities)
            boss.cooldowns.push_back(a.cooldown * 0.5f);
        registry_.emplace<Boss>(e, std::move(boss));
    }
    if (def.structure) {
        registry_.emplace<Structure>(e);
        navDirty_ = true;
    } else {
        registry_.emplace<Order>(e);
        registry_.emplace<Progress>(e, Progress{pos});
    }
    if (owner >= 0)
        registry_.emplace<Owner>(e, static_cast<std::uint8_t>(owner));
    events_.push_back({EventType::Spawn, e, entt::null, pos, pos});
    return e;
}

glm::vec2 World::playerBase(std::uint8_t player) const {
    const float angle = static_cast<float>(player) * 2.39996f;
    const float radius = player == 0 ? 0.0f : 3.0f + 1.5f * std::sqrt(static_cast<float>(player));
    return map_->playerStart().pos + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
}

std::vector<entt::entity> World::spawnPlayer(std::uint8_t player, std::uint8_t team) {
    std::vector<entt::entity> spawned;
    if (!map_ || player >= kMaxPlayers)
        return spawned;
    players_[player] = {true, map_->defense().startEnergy, 0};
    const auto& start = map_->playerStart();
    const glm::vec2 base = playerBase(player);
    for (std::size_t i = 0; i < start.units.size(); ++i) {
        const UnitDefId id = data_->find(start.units[i]);
        if (id == kInvalidUnitDef)
            throw std::runtime_error("unknown unit: " + start.units[i]);
        const glm::vec2 offset{static_cast<float>(i % 2) * 1.6f, static_cast<float>(i / 2) * 1.6f};
        const glm::vec2 pos = Map::center(nearestFree(*map_, Map::cellOf(base + offset)));
        spawned.push_back(spawn(id, team, pos, player));
    }
    return spawned;
}

void World::releasePlayer(std::uint8_t player) {
    if (player < kMaxPlayers)
        players_[player].active = false;
    std::vector<entt::entity> owned;
    for (auto [e, owner] : registry_.view<const Owner>().each())
        if (owner.player == player)
            owned.push_back(e);
    for (const auto e : owned)
        registry_.remove<Owner>(e);
}

bool World::alive(entt::entity e) const {
    if (e == entt::null || !registry_.valid(e))
        return false;
    const auto* h = registry_.try_get<Health>(e);
    return h && h->hp > 0.0f;
}

entt::entity World::unitAt(glm::vec2 p, float pad) const {
    entt::entity best = entt::null;
    float bestDist = std::numeric_limits<float>::max();
    for (auto [e, pos, unit] : registry_.view<const Position, const Unit>().each()) {
        const float d = glm::distance(pos.value, p) - unit.radius;
        if (d <= pad && d < bestDist) {
            best = e;
            bestDist = d;
        }
    }
    return best;
}

void World::apply(const Command& command) {
    std::visit(
        [this](const auto& cmd) {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (!UnitCommand<T>) {
                handle(cmd);
            } else {
            std::vector<entt::entity> units;
            for (const auto e : cmd.units)
                if (alive(e) && registry_.all_of<Order>(e))
                    units.push_back(e);
            if (units.empty())
                return;

            if constexpr (std::is_same_v<T, MoveCommand>) {
                float spacing = 0.0f;
                std::vector<glm::vec2> positions;
                for (const auto e : units) {
                    spacing = std::max(spacing, registry_.get<Unit>(e).radius * 2.0f + 0.3f);
                    positions.push_back(registry_.get<Position>(e).value);
                }
                const auto goals = assignFormation(map_, positions, spacing, cmd.point);
                for (std::size_t i = 0; i < units.size(); ++i) {
                    auto& order = registry_.get<Order>(units[i]);
                    order = {cmd.attack ? OrderType::AttackMove : OrderType::Move, goals[i], entt::null};
                    registry_.get<Target>(units[i]).value = entt::null;
                    registry_.remove<Navigator>(units[i]);
                    registry_.remove<LaneFollower>(units[i]);
                }
            } else if constexpr (std::is_same_v<T, AttackCommand>) {
                if (!alive(cmd.target))
                    return;
                for (const auto e : units) {
                    registry_.get<Order>(e) = {OrderType::Attack, {}, cmd.target};
                    registry_.remove<LaneFollower>(e);
                }
            } else {
                for (const auto e : units) {
                    registry_.get<Order>(e) = {};
                    registry_.get<Target>(e).value = entt::null;
                    registry_.remove<Navigator>(e);
                }
            }
            }
        },
        command);
}

const char* SystemTimes::name(std::size_t i) {
    static constexpr const char* names[kCount] = {"spatial", "spawners", "heroes",  "units",  "bosses", "integrate", "collisions",
                                                  "turrets", "weapons",  "projectiles", "dead", "points", "defense"};
    return i < kCount ? names[i] : "?";
}

void World::step() {
    using Clock = std::chrono::steady_clock;
    events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(std::min(stepEvents_, events_.size())));
    for (auto [e, p, prev] : registry_.view<const Position, PrevPosition>().each())
        prev.value = p.value;

    if (navDirty_)
        rebuildNav();
    pathBudget_ = kPathsPerTick;
    const auto begin = Clock::now();
    auto last = begin;
    std::size_t index = 0;
    auto run = [&](auto&& system) {
        system();
        const auto now = Clock::now();
        times_.seconds[index++] += std::chrono::duration<double>(now - last).count();
        last = now;
    };
    run([&] { rebuildSpatial(); });
    run([&] { updateSpawners(); });
    run([&] { updateHeroes(); });
    run([&] { updateUnits(); });
    run([&] { updateBosses(); });
    run([&] { integrate(); });
    run([&] { resolveCollisions(); });
    run([&] { updateTurrets(); });
    run([&] { updateWeapons(); });
    run([&] { updateProjectiles(); });
    run([&] { removeDead(); });
    run([&] { updateResourcePoints(); });
    run([&] { updateDefense(); });
    times_.peak = std::max(times_.peak, std::chrono::duration<double>(last - begin).count());
    ++times_.steps;
    stepEvents_ = events_.size();
    ++tick_;
}

void World::rebuildSpatial() {
    spatial_.clear();
    structures_.clear();
    for (auto [e, pos, unit, team, vel] : registry_.view<const Position, const Unit, const Team, const Velocity>().each()) {
        const bool structure = registry_.all_of<Structure>(e);
        (structure ? structures_ : spatial_).insert({e, pos.value, vel.value, unit.radius, team.id, structure});
    }
}

void World::rebuildNav() {
    navDirty_ = false;
    if (!map_ || !nav_)
        return;
    *nav_ = *map_;
    ++times_.navRebuilds;
    for (auto [e, pos, unit] : registry_.view<const Position, const Unit, const Structure>().each()) {
        if (registry_.get<Health>(e).hp <= 0.0f)
            continue;
        const float r = std::max(unit.radius - 0.25f, 0.5f);
        const glm::ivec2 c = Map::cellOf(pos.value);
        const int span = static_cast<int>(std::ceil(r)) + 1;
        for (int y = c.y - span; y <= c.y + span; ++y)
            for (int x = c.x - span; x <= c.x + span; ++x)
                if (nav_->inside(x, y) && (glm::ivec2(x, y) == c || glm::distance(Map::center({x, y}), pos.value) < r))
                    nav_->setBlocked(x, y, true);
    }
    for (auto [e, nav] : registry_.view<Navigator>().each()) {
        nav.repath = 0;
        nav.path.clear();
    }
}

std::vector<entt::entity> World::spawnOnLane(UnitDefId def, std::uint8_t team, int lane, int count, float offset) {
    std::vector<entt::entity> spawned;
    const auto& points = map_->lanes()[static_cast<std::size_t>(lane)].points;
    const bool reverse = team != 0;
    const glm::vec2 a = reverse ? points.back() : points.front();
    const glm::vec2 b = reverse ? points[points.size() - 2] : points[1];
    const glm::vec2 dir = glm::normalize(b - a);
    const glm::vec2 side(-dir.y, dir.x);
    const float spacing = data_->unit(def).radius * 2.0f + 0.4f;
    constexpr int perRow = 5;
    for (int i = 0; i < count; ++i) {
        const int row = i / perRow;
        const int inRow = std::min(perRow, count - row * perRow);
        const float across = (static_cast<float>(i % perRow) - static_cast<float>(inRow - 1) * 0.5f) * spacing;
        const glm::vec2 pos = a + dir * (offset + static_cast<float>(row) * spacing) + side * across;
        const auto u = spawn(def, team, pos);
        registry_.emplace<LaneFollower>(u, LaneFollower{lane, 1, reverse});
        spawned.push_back(u);
    }
    return spawned;
}

void World::updateSpawners() {
    if (!map_)
        return;
    struct Pending {
        UnitDefId unit;
        std::uint8_t team;
        int lane;
        int count;
        float offset;
    };
    std::vector<Pending> pending;
    for (auto [e, s] : registry_.view<Spawner>().each()) {
        if (s.building != entt::null && !alive(s.building))
            continue;
        s.timer -= kTickDt;
        if (s.timer > 0.0f)
            continue;
        s.timer += s.interval;
        pending.push_back({s.unit, s.team, s.lane, s.count, s.offset});
    }
    for (const auto& p : pending)
        spawnOnLane(p.unit, p.team, p.lane, p.count, p.offset);
}

entt::entity World::findEnemy(glm::vec2 pos, std::uint8_t team, float radius) const {
    entt::entity best = entt::null;
    float bestDist = std::numeric_limits<float>::max();
    queryAll(pos, radius, [&](const SpatialHash::Item& item) {
        if (item.team == team)
            return;
        const float d = glm::distance(item.pos, pos) - item.radius;
        if (d <= radius && d < bestDist && alive(item.entity) && !registry_.all_of<Invulnerable>(item.entity)) {
            best = item.entity;
            bestDist = d;
        }
    });
    return best;
}

glm::vec2 World::avoid(entt::entity e, glm::vec2 pos, glm::vec2 vel, float radius, std::uint8_t team) const {
    const float speed = glm::length(vel);
    if (speed < 1e-3f)
        return vel;
    const glm::vec2 dir = vel / speed;
    const glm::vec2 left{-dir.y, dir.x};
    const float look = radius + 1.2f;
    const float tie = (entt::to_integral(e) & 1u) ? 1.0f : -1.0f;
    glm::vec2 steer{};
    queryAll(pos, look, [&](const SpatialHash::Item& o) {
        if (o.entity == e || (o.team != team && !o.structure))
            return;
        const glm::vec2 rel = o.pos - pos;
        const float ahead = glm::dot(rel, dir);
        const float reach = radius + o.radius + 0.15f;
        if (ahead <= 0.0f || ahead > reach + look)
            return;
        const float lateral = dir.x * rel.y - dir.y * rel.x;
        if (std::abs(lateral) >= reach)
            return;
        if (!o.structure && glm::dot(o.vel, dir) > speed * 0.6f)
            return;
        const float side = lateral > 0.05f ? -1.0f : lateral < -0.05f ? 1.0f : tie;
        steer += left * side * (1.0f - ahead / (reach + look)) * (1.0f - std::abs(lateral) / reach);
    });
    if (glm::dot(steer, steer) < 1e-6f)
        return vel;
    const glm::vec2 turned = glm::normalize(dir + steer * 2.0f);
    if (map_ && navMap().blocked(Map::cellOf(pos + turned * (radius + 0.6f))))
        return vel;
    return turned * speed;
}

glm::vec2 World::navigate(entt::entity e, glm::vec2 pos, glm::vec2 goal, float speed) {
    if (!map_)
        return seek(pos, goal, speed);
    const Map& nav = navMap();
    auto& n = registry_.get_or_emplace<Navigator>(e);
    if ((--n.repath <= 0 || glm::distance(n.goal, goal) > 1.0f) && pathBudget_ > 0) {
        n.goal = goal;
        n.repath = kRepathTicks + static_cast<int>(entt::to_integral(e) % 5u);
        n.direct = lineOfSight(nav, pos, goal);
        if (n.direct)
            n.path.clear();
        else {
            const auto t0 = std::chrono::steady_clock::now();
            n.path = findPath(nav, pos, goal);
            times_.worstPath = std::max(times_.worstPath, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            ++times_.paths;
            --pathBudget_;
            times_.worstWork = std::max(times_.worstWork, lastPathWork());
        }
        n.next = 0;
    }
    if (n.direct)
        return seek(pos, goal, speed);
    while (n.next < n.path.size() && glm::distance(pos, n.path[n.next]) < 0.6f)
        ++n.next;
    if (n.next >= n.path.size())
        return seek(pos, goal, speed);
    return seek(pos, n.path[n.next], speed);
}

glm::vec2 World::lanePoint(const LaneFollower& lane, int i) const {
    const auto& points = map_->lanes()[static_cast<std::size_t>(lane.lane)].points;
    const auto n = static_cast<int>(points.size());
    return points[static_cast<std::size_t>(lane.reverse ? n - 1 - i : i)];
}

const FlowField& World::flowField(glm::vec2 goal) {
    const int key = map_->index(nearestFree(*map_, Map::cellOf(goal)));
    auto it = flowFields_.find(key);
    if (it == flowFields_.end()) {
        const auto t0 = std::chrono::steady_clock::now();
        it = flowFields_.emplace(key, FlowField(*map_, goal)).first;
        times_.worstField = std::max(times_.worstField, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        ++times_.fields;
    }
    return it->second;
}

glm::vec2 World::laneVelocity(LaneFollower& lane, glm::vec2 pos, float speed) {
    const auto n = static_cast<int>(map_->lanes()[static_cast<std::size_t>(lane.lane)].points.size());
    while (lane.waypoint < n - 1 && glm::distance(pos, lanePoint(lane, lane.waypoint)) < kWaypointReach)
        ++lane.waypoint;
    return flowField(lanePoint(lane, lane.waypoint)).direction(pos) * speed;
}

void World::updateUnits() {
    auto view = registry_.view<const Position, const Unit, const Team, Velocity, Target, Facing>();
    for (auto [e, pos, unit, team, vel, target, facing] : view.each()) {
        vel.value = {};
        auto* order = registry_.try_get<Order>(e);
        const auto* weapon = registry_.try_get<Weapon>(e);
        if (!alive(target.value))
            target.value = entt::null;

        if (order && order->type == OrderType::Attack) {
            if (alive(order->target)) {
                target.value = order->target;
            } else {
                *order = {};
                target.value = entt::null;
            }
        } else if (order && order->type == OrderType::Move) {
            target.value = entt::null;
        } else if (weapon) {
            const float acquire = order ? unit.sight : weapon->def.range + unit.radius;
            if (target.value != entt::null) {
                const auto& tp = registry_.get<Position>(target.value).value;
                if (glm::distance(tp, pos.value) - registry_.get<Unit>(target.value).radius > acquire * kLeashFactor)
                    target.value = entt::null;
            }
            if (target.value == entt::null && (tick_ + entt::to_integral(e)) % kScanInterval == 0)
                target.value = findEnemy(pos.value, team.id, acquire);
        }

        if (target.value != entt::null && weapon) {
            const glm::vec2 tp = registry_.get<Position>(target.value).value;
            const float reach = weapon->def.range + unit.radius + registry_.get<Unit>(target.value).radius;
            const glm::vec2 d = tp - pos.value;
            if (glm::dot(d, d) > reach * reach && unit.speed > 0.0f)
                vel.value = navigate(e, pos.value, tp, unit.speed);
        } else if (order && (order->type == OrderType::Move || order->type == OrderType::AttackMove)) {
            if (glm::distance(pos.value, order->point) < kArriveDistance) {
                *order = {};
                registry_.remove<Navigator>(e);
            } else {
                vel.value = navigate(e, pos.value, order->point, unit.speed);
            }
        } else if (auto* lane = registry_.try_get<LaneFollower>(e); lane && map_) {
            vel.value = laneVelocity(*lane, pos.value, unit.speed);
        }

        if (auto* progress = registry_.try_get<Progress>(e)) {
            if (glm::dot(vel.value, vel.value) > 1e-6f) {
                vel.value = avoid(e, pos.value, vel.value, unit.radius, team.id);
                if (progress->detour > 0) {
                    --progress->detour;
                    const float a = progress->side * 1.1f;
                    const glm::vec2 v = vel.value;
                    const glm::vec2 turned{v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)};
                    if (!map_ || !navMap().blocked(Map::cellOf(pos.value + glm::normalize(turned) * (unit.radius + 0.6f))))
                        vel.value = turned;
                } else if (++progress->ticks >= kStuckTicks) {
                    const float expected = unit.speed * static_cast<float>(kStuckTicks) * kTickDt;
                    if (glm::distance(pos.value, progress->anchor) < expected * 0.2f) {
                        progress->detour = kDetourTicks;
                        progress->side = -progress->side;
                        if (auto* nav = registry_.try_get<Navigator>(e))
                            nav->repath = 0;
                    }
                    progress->anchor = pos.value;
                    progress->ticks = 0;
                }
            } else {
                progress->anchor = pos.value;
                progress->ticks = 0;
                progress->detour = 0;
            }
        }

        if (glm::dot(vel.value, vel.value) > 1e-6f)
            facing.angle = std::atan2(vel.value.y, vel.value.x);
    }
}

void World::integrate() {
    for (auto [e, pos, vel] : registry_.view<Position, const Velocity>(entt::exclude<Structure>).each())
        pos.value += vel.value * kTickDt;
}

void World::resolveCollisions() {
    for (auto [e, pos, unit] : registry_.view<Position, const Unit>(entt::exclude<Structure>).each()) {
        glm::vec2 push{};
        queryAll(pos.value, unit.radius, [&](const SpatialHash::Item& item) {
            if (item.entity == e)
                return;
            const float minDist = unit.radius + item.radius;
            glm::vec2 d = pos.value - item.pos;
            float dist2 = glm::dot(d, d);
            if (dist2 >= minDist * minDist)
                return;
            if (dist2 < 1e-8f) {
                const float a = static_cast<float>(entt::to_integral(e) % 360u) * std::numbers::pi_v<float> / 180.0f;
                d = {std::cos(a), std::sin(a)};
                dist2 = 0.0f;
            }
            const float dist = std::sqrt(dist2);
            const glm::vec2 n = dist > 1e-4f ? d / dist : d;
            const float share = item.structure ? 1.0f : 0.5f;
            push += n * (minDist - dist) * share;
        });
        pos.value += push;
        if (map_)
            pushOutOfWalls(*map_, pos.value, unit.radius);
    }
}

void World::damage(entt::entity target, float amount, int sourceOwner, entt::entity source) {
    if (registry_.all_of<Invulnerable>(target))
        return;
    auto& h = registry_.get<Health>(target);
    h.hp -= std::max(1.0f, amount - h.armor);
    if (sourceOwner >= 0)
        h.lastHitBy = sourceOwner;
    events_.push_back({EventType::Hit, target, source, registry_.get<Position>(target).value, registry_.get<Position>(target).value});
}

void World::updateTurrets() {
    for (auto [e, pos, weapon, target, facing, turret] : registry_.view<const Position, const Weapon, const Target, const Facing, Turret>().each()) {
        float yaw = registry_.all_of<Structure>(e) ? turret.yaw : facing.angle;
        float pitch = 0.0f;
        if (alive(target.value)) {
            const glm::vec2 d = registry_.get<Position>(target.value).value - pos.value;
            yaw = std::atan2(d.y, d.x);
            if (weapon.def.projectileSpeed > 0.0f) {
                const float v = weapon.def.projectileSpeed;
                pitch = 0.5f * std::asin(std::min(1.0f, kLobGravity * glm::length(d) / (v * v)));
            }
        }
        turret.yaw = approach(turret.yaw, yaw, weapon.def.turnRate * kTickDt);
        turret.pitch += std::clamp(pitch - turret.pitch, -kPitchRate * kTickDt, kPitchRate * kTickDt);
    }
}

void World::updateWeapons() {
    struct PendingShot {
        entt::entity source;
        entt::entity target;
        glm::vec2 from;
        glm::vec2 to;
        WeaponDef def;
        std::uint8_t team;
        int owner;
        UnitDefId unit;
    };
    std::vector<PendingShot> shots;

    for (auto [e, pos, unit, team, weapon, target] : registry_.view<const Position, const Unit, const Team, Weapon, const Target>().each()) {
        weapon.cooldown = std::max(0.0f, weapon.cooldown - kTickDt);
        if (weapon.cooldown > 0.0f || !alive(target.value))
            continue;
        const glm::vec2 tp = registry_.get<Position>(target.value).value;
        const float reach = weapon.def.range + unit.radius + registry_.get<Unit>(target.value).radius + kRangeSlack;
        if (glm::distance(tp, pos.value) > reach)
            continue;
        if (const auto* turret = registry_.try_get<Turret>(e);
            turret && std::abs(wrapAngle(std::atan2(tp.y - pos.value.y, tp.x - pos.value.x) - turret->yaw)) > kAimTolerance)
            continue;
        weapon.cooldown = weapon.def.cooldown;
        const auto* owner = registry_.try_get<Owner>(e);
        shots.push_back({e, target.value, pos.value, tp, weapon.def, team.id, owner ? static_cast<int>(owner->player) : -1, unit.def});
    }

    for (const auto& s : shots) {
        events_.push_back({EventType::Shot, s.source, s.target, s.from, s.to});
        if (s.def.projectileSpeed > 0.0f) {
            const auto p = registry_.create();
            registry_.emplace<Position>(p, s.from);
            registry_.emplace<PrevPosition>(p, s.from);
            registry_.emplace<Projectile>(p, Projectile{s.source, s.target, s.to, s.def.projectileSpeed, s.def.damage, s.team, s.owner, s.unit});
        } else {
            damage(s.target, s.def.damage, s.owner, s.source);
        }
    }
}

void World::updateProjectiles() {
    std::vector<entt::entity> done;
    for (auto [e, pos, proj] : registry_.view<Position, Projectile>().each()) {
        const bool targetAlive = alive(proj.target);
        if (targetAlive)
            proj.targetPos = registry_.get<Position>(proj.target).value;
        const glm::vec2 d = proj.targetPos - pos.value;
        const float dist = glm::length(d);
        const float step = proj.speed * kTickDt;
        if (dist <= step) {
            pos.value = proj.targetPos;
            if (targetAlive)
                damage(proj.target, proj.damage, proj.owner, proj.source);
            done.push_back(e);
        } else {
            pos.value += d / dist * step;
        }
    }
    registry_.destroy(done.begin(), done.end());
}

void World::removeDead() {
    std::vector<entt::entity> dead;
    struct Kill {
        UnitDefId def;
        glm::vec2 pos;
        int lastHitBy;
    };
    std::vector<Kill> kills;
    for (auto [e, h, pos, unit, team] : registry_.view<const Health, const Position, const Unit, const Team>().each()) {
        if (h.hp > 0.0f)
            continue;
        dead.push_back(e);
        if (registry_.all_of<Structure>(e))
            navDirty_ = true;
        events_.push_back({EventType::Death, e, entt::null, pos.value, pos.value});
        if (team.id != 0) {
            kills.push_back({unit.def, pos.value, h.lastHitBy});
            continue;
        }
        const auto* prog = registry_.try_get<Progression>(e);
        const auto* owner = registry_.try_get<Owner>(e);
        if (prog && owner && prog->rank == Rank::Hero && players_[owner->player].active)
            respawns_.push_back({owner->player, unit.def, *prog, 10.0f + 2.0f * static_cast<float>(prog->level)});
    }
    for (const auto& k : kills)
        onEnemyKilled(data_->unit(k.def), k.pos, k.lastHitBy);
    registry_.destroy(dead.begin(), dead.end());
}

}
