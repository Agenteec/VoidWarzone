#include "Prediction.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <glm/geometric.hpp>

#include "core/Movement.h"
#include "core/Pathfinding.h"
#include "core/World.h"

namespace vw::client {

namespace {

constexpr float kBlendRate = 6.0f;
constexpr float kSettleDistance = 0.15f;
constexpr float kDivergeDistance = 4.0f;

}

void Prediction::onMove(const std::vector<net::InterpolatedEntity>& units, glm::vec2 point, std::uint32_t seq, const GameData& data,
                        const Map& map) {
    if (units.empty())
        return;
    float spacing = 0.0f;
    std::vector<glm::vec2> positions;
    for (const auto& u : units) {
        spacing = std::max(spacing, data.unit(u.def).radius * 2.0f + 0.3f);
        const auto it = units_.find(u.id);
        positions.push_back(it != units_.end() ? it->second.pos : u.pos);
    }
    const auto goals = assignFormation(&map, positions, spacing, point);
    for (std::size_t i = 0; i < units.size(); ++i) {
        const auto& def = data.unit(units[i].def);
        Unit p;
        p.seq = seq;
        p.pos = positions[i];
        p.goal = goals[i];
        p.facing = units[i].facing;
        p.path = findPath(map, p.pos, p.goal);
        p.speed = def.speed;
        p.radius = def.radius;
        units_[units[i].id] = std::move(p);
    }
}

void Prediction::cancel(const std::vector<std::uint32_t>& ids) {
    for (const auto id : ids)
        units_.erase(id);
}

void Prediction::update(float dt, double now, const net::NetClient& net, const GameData&, const Map& map,
                        std::vector<net::InterpolatedEntity>& entities) {
    const net::Snapshot* latest = net.latest();
    if (!latest) {
        units_.clear();
        return;
    }
    const std::uint32_t acked = net.ackedCommandSeq();
    const float lag = static_cast<float>(net.serverTime(now) - latest->tick * static_cast<double>(kTickDt) + net.stats().rtt * 0.0005);

    for (auto it = units_.begin(); it != units_.end();) {
        auto& p = it->second;
        const net::EntityState* server = latest->find(it->first);
        auto entity = std::find_if(entities.begin(), entities.end(), [&](const auto& e) { return e.id == it->first; });
        if (!server || entity == entities.end()) {
            it = units_.erase(it);
            continue;
        }

        while (p.next < p.path.size() && glm::distance(p.pos, p.path[p.next]) < 0.4f)
            ++p.next;
        const glm::vec2 waypoint = p.next < p.path.size() ? p.path[p.next] : p.goal;
        const glm::vec2 vel = seek(p.pos, waypoint, p.speed, dt);
        p.pos += vel * dt;
        pushOutOfWalls(map, p.pos, p.radius);
        if (glm::dot(vel, vel) > 1e-6f)
            p.facing = std::atan2(vel.y, vel.x);

        bool done = false;
        if (acked >= p.seq) {
            const glm::vec2 serverVel{net::dequantizeVel(server->vx), net::dequantizeVel(server->vy)};
            const glm::vec2 serverNow = glm::vec2{net::dequantizePos(server->x), net::dequantizePos(server->y)} + serverVel * lag;
            const float error = glm::distance(serverNow, p.pos);
            p.pos += (serverNow - p.pos) * (1.0f - std::exp(-dt * kBlendRate));
            const bool settled = glm::dot(serverVel, serverVel) < 1e-4f && error < kSettleDistance;
            done = settled || error > kDivergeDistance;
        }

        entity->pos = p.pos;
        entity->facing = p.facing;
        it = done ? units_.erase(it) : std::next(it);
    }
}

}
