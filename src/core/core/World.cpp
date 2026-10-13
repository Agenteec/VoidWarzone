#include "core/World.h"

#include <algorithm>
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
constexpr int kRepathTicks = 15;

glm::vec2 seek(glm::vec2 pos, glm::vec2 goal, float speed) {
    const glm::vec2 d = goal - pos;
    const float len = glm::length(d);
    if (len < 1e-4f)
        return {};
    return d / len * std::min(speed, len / kTickDt);
}

void pushOutOfWalls(const Map& map, glm::vec2& p, float r) {
    const int x0 = static_cast<int>(std::floor(p.x - r));
    const int x1 = static_cast<int>(std::floor(p.x + r));
    const int y0 = static_cast<int>(std::floor(p.y - r));
    const int y1 = static_cast<int>(std::floor(p.y + r));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!map.blocked(x, y))
                continue;
            const glm::vec2 lo(static_cast<float>(x), static_cast<float>(y));
            const glm::vec2 closest = glm::clamp(p, lo, lo + 1.0f);
            const glm::vec2 d = p - closest;
            const float d2 = glm::dot(d, d);
            if (d2 >= r * r)
                continue;
            if (d2 > 1e-8f) {
                const float dist = std::sqrt(d2);
                p += d / dist * (r - dist);
                continue;
            }
            const float left = p.x - lo.x;
            const float right = lo.x + 1.0f - p.x;
            const float down = p.y - lo.y;
            const float up = lo.y + 1.0f - p.y;
            const float m = std::min({left, right, down, up});
            if (m == left)
                p.x = lo.x - r;
            else if (m == right)
                p.x = lo.x + 1.0f + r;
            else if (m == down)
                p.y = lo.y - r;
            else
                p.y = lo.y + 1.0f + r;
        }
}

std::vector<glm::vec2> formation(const Map* map, glm::vec2 center, std::size_t count, float spacing) {
    std::vector<glm::vec2> slots;
    for (int ring = 0; slots.size() < count && ring < 64; ++ring) {
        std::vector<glm::vec2> ringSlots;
        for (int y = -ring; y <= ring; ++y)
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != ring)
                    continue;
                const glm::vec2 p = center + glm::vec2(static_cast<float>(x), static_cast<float>(y)) * spacing;
                if (!map || !map->blocked(Map::cellOf(p)))
                    ringSlots.push_back(p);
            }
        std::sort(ringSlots.begin(), ringSlots.end(),
                  [&](glm::vec2 a, glm::vec2 b) { return glm::distance(a, center) < glm::distance(b, center); });
        for (const auto& p : ringSlots)
            if (slots.size() < count)
                slots.push_back(p);
    }
    while (slots.size() < count)
        slots.push_back(center);
    return slots;
}

}

World::World() {
    spatial_.reset(128.0f, 128.0f, 4.0f);
}

World::World(const GameData& data, const Map& map) : data_(&data), map_(&map) {
    spatial_.reset(static_cast<float>(map.width()), static_cast<float>(map.height()), 4.0f);
    maxRadius_ = std::max(data.maxRadius(), 0.5f);

    for (const auto& u : map.units()) {
        const UnitDefId id = data.find(u.unit);
        if (id == kInvalidUnitDef)
            throw std::runtime_error("unknown unit: " + u.unit);
        spawn(id, u.team, u.pos, u.owner);
    }

    for (const auto& s : map.spawners()) {
        const UnitDefId id = data.find(s.unit);
        if (id == kInvalidUnitDef)
            throw std::runtime_error("unknown unit: " + s.unit);
        const auto e = registry_.create();
        registry_.emplace<Spawner>(e, Spawner{id, s.team, s.lane, s.interval, s.delay, s.count, s.offset});
    }
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
    registry_.emplace<Health>(e, def.maxHp, def.maxHp, def.armor);
    registry_.emplace<Target>(e);
    if (def.weapon)
        registry_.emplace<Weapon>(e, *def.weapon, 0.0f);
    if (def.structure)
        registry_.emplace<Structure>(e);
    else
        registry_.emplace<Order>(e);
    if (owner >= 0)
        registry_.emplace<Owner>(e, static_cast<std::uint8_t>(owner));
    events_.push_back({EventType::Spawn, e, entt::null, pos, pos});
    return e;
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
            std::vector<entt::entity> units;
            for (const auto e : cmd.units)
                if (alive(e) && registry_.all_of<Order>(e))
                    units.push_back(e);
            if (units.empty())
                return;

            if constexpr (std::is_same_v<T, MoveCommand>) {
                float spacing = 0.0f;
                for (const auto e : units)
                    spacing = std::max(spacing, registry_.get<Unit>(e).radius * 2.0f + 0.3f);
                std::sort(units.begin(), units.end(), [&](auto a, auto b) {
                    return glm::distance(registry_.get<Position>(a).value, cmd.point) <
                           glm::distance(registry_.get<Position>(b).value, cmd.point);
                });
                const auto slots = formation(map_, cmd.point, units.size(), spacing);
                for (std::size_t i = 0; i < units.size(); ++i) {
                    auto& order = registry_.get<Order>(units[i]);
                    order = {cmd.attack ? OrderType::AttackMove : OrderType::Move, slots[i], entt::null};
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
        },
        command);
}

void World::step() {
    events_.clear();
    for (auto [e, p, prev] : registry_.view<const Position, PrevPosition>().each())
        prev.value = p.value;

    rebuildSpatial();
    updateSpawners();
    updateUnits();
    integrate();
    resolveCollisions();
    updateWeapons();
    updateProjectiles();
    removeDead();
    ++tick_;
}

void World::rebuildSpatial() {
    spatial_.clear();
    const auto view = registry_.view<const Position, const Unit>();
    for (const auto e : view)
        spatial_.insert(e, view.get<const Position>(e).value);
}

void World::updateSpawners() {
    if (!map_)
        return;
    for (auto [e, s] : registry_.view<Spawner>().each()) {
        s.timer -= kTickDt;
        if (s.timer > 0.0f)
            continue;
        s.timer += s.interval;

        const auto& points = map_->lanes()[static_cast<std::size_t>(s.lane)].points;
        const bool reverse = s.team != 0;
        const glm::vec2 a = reverse ? points.back() : points.front();
        const glm::vec2 b = reverse ? points[points.size() - 2] : points[1];
        const glm::vec2 dir = glm::normalize(b - a);
        const glm::vec2 side(-dir.y, dir.x);
        const float spacing = data_->unit(s.unit).radius * 2.0f + 0.4f;
        for (int i = 0; i < s.count; ++i) {
            const float offset = (static_cast<float>(i) - static_cast<float>(s.count - 1) * 0.5f) * spacing;
            const auto u = spawn(s.unit, s.team, a + dir * s.offset + side * offset);
            registry_.emplace<LaneFollower>(u, LaneFollower{s.lane, 1, reverse});
        }
    }
}

entt::entity World::findEnemy(glm::vec2 pos, std::uint8_t team, float radius) const {
    entt::entity best = entt::null;
    float bestDist = std::numeric_limits<float>::max();
    spatial_.query(pos, radius + maxRadius_, [&](const SpatialHash::Item& item) {
        if (registry_.get<Team>(item.entity).id == team || !alive(item.entity))
            return;
        const float d = glm::distance(item.pos, pos) - registry_.get<Unit>(item.entity).radius;
        if (d <= radius && d < bestDist) {
            best = item.entity;
            bestDist = d;
        }
    });
    return best;
}

glm::vec2 World::navigate(entt::entity e, glm::vec2 pos, glm::vec2 goal, float speed) {
    if (!map_ || lineOfSight(*map_, pos, goal)) {
        registry_.remove<Navigator>(e);
        return seek(pos, goal, speed);
    }
    auto& nav = registry_.get_or_emplace<Navigator>(e);
    if (--nav.repath <= 0 && (nav.path.empty() || glm::distance(nav.goal, goal) > 1.0f)) {
        nav.goal = goal;
        nav.path = findPath(*map_, pos, goal);
        nav.next = 0;
        nav.repath = kRepathTicks;
    }
    while (nav.next < nav.path.size() && glm::distance(pos, nav.path[nav.next]) < 0.6f)
        ++nav.next;
    if (nav.next >= nav.path.size())
        return seek(pos, goal, speed);
    return seek(pos, nav.path[nav.next], speed);
}

glm::vec2 World::lanePoint(const LaneFollower& lane, int i) const {
    const auto& points = map_->lanes()[static_cast<std::size_t>(lane.lane)].points;
    const auto n = static_cast<int>(points.size());
    return points[static_cast<std::size_t>(lane.reverse ? n - 1 - i : i)];
}

const FlowField& World::flowField(glm::vec2 goal) {
    const int key = map_->index(nearestFree(*map_, Map::cellOf(goal)));
    auto it = flowFields_.find(key);
    if (it == flowFields_.end())
        it = flowFields_.emplace(key, FlowField(*map_, goal)).first;
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
            if (target.value == entt::null)
                target.value = findEnemy(pos.value, team.id, acquire);
        }

        if (target.value != entt::null && weapon) {
            const glm::vec2 tp = registry_.get<Position>(target.value).value;
            const float reach = weapon->def.range + unit.radius + registry_.get<Unit>(target.value).radius;
            const glm::vec2 d = tp - pos.value;
            if (glm::dot(d, d) > reach * reach) {
                if (unit.speed > 0.0f)
                    vel.value = navigate(e, pos.value, tp, unit.speed);
            } else {
                facing.angle = std::atan2(d.y, d.x);
            }
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
        spatial_.query(pos.value, unit.radius + maxRadius_, [&](const SpatialHash::Item& item) {
            if (item.entity == e)
                return;
            const float minDist = unit.radius + registry_.get<Unit>(item.entity).radius;
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
            const float share = registry_.all_of<Structure>(item.entity) ? 1.0f : 0.5f;
            push += n * (minDist - dist) * share;
        });
        pos.value += push;
        if (map_)
            pushOutOfWalls(*map_, pos.value, unit.radius);
    }
}

void World::damage(entt::entity target, float amount, entt::entity source) {
    auto& h = registry_.get<Health>(target);
    h.hp -= std::max(1.0f, amount - h.armor);
    events_.push_back({EventType::Hit, target, source, registry_.get<Position>(target).value, registry_.get<Position>(target).value});
}

void World::updateWeapons() {
    struct PendingShot {
        entt::entity source;
        entt::entity target;
        glm::vec2 from;
        glm::vec2 to;
        WeaponDef def;
        std::uint8_t team;
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
        weapon.cooldown = weapon.def.cooldown;
        shots.push_back({e, target.value, pos.value, tp, weapon.def, team.id});
    }

    for (const auto& s : shots) {
        events_.push_back({EventType::Shot, s.source, s.target, s.from, s.to});
        if (s.def.projectileSpeed > 0.0f) {
            const auto p = registry_.create();
            registry_.emplace<Position>(p, s.from);
            registry_.emplace<PrevPosition>(p, s.from);
            registry_.emplace<Projectile>(p, Projectile{s.source, s.target, s.to, s.def.projectileSpeed, s.def.damage, s.team});
        } else {
            damage(s.target, s.def.damage, s.source);
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
                damage(proj.target, proj.damage, proj.source);
            done.push_back(e);
        } else {
            pos.value += d / dist * step;
        }
    }
    registry_.destroy(done.begin(), done.end());
}

void World::removeDead() {
    std::vector<entt::entity> dead;
    for (auto [e, h, pos] : registry_.view<const Health, const Position>().each())
        if (h.hp <= 0.0f) {
            dead.push_back(e);
            events_.push_back({EventType::Death, e, entt::null, pos.value, pos.value});
        }
    registry_.destroy(dead.begin(), dead.end());
}

}
