#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/vec2.hpp>

#include "core/Commands.h"
#include "core/Components.h"
#include "core/GameData.h"
#include "core/Map.h"
#include "core/Pathfinding.h"
#include "core/SpatialHash.h"

namespace vw {

inline constexpr int kTickRate = 30;
inline constexpr float kTickDt = 1.0f / kTickRate;

class World {
public:
    World();
    World(const GameData& data, const Map& map);

    void step();
    void apply(const Command& command);
    entt::entity spawn(UnitDefId def, std::uint8_t team, glm::vec2 pos, int owner = -1);

    bool alive(entt::entity e) const;
    entt::entity unitAt(glm::vec2 p, float pad = 0.3f) const;

    std::uint64_t tick() const { return tick_; }
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    const std::vector<SimEvent>& events() const { return events_; }
    const GameData* data() const { return data_; }
    const Map* map() const { return map_; }

private:
    void rebuildSpatial();
    void updateSpawners();
    void updateUnits();
    void integrate();
    void resolveCollisions();
    void updateWeapons();
    void updateProjectiles();
    void removeDead();

    entt::entity findEnemy(glm::vec2 pos, std::uint8_t team, float radius) const;
    glm::vec2 navigate(entt::entity e, glm::vec2 pos, glm::vec2 goal, float speed);
    glm::vec2 laneVelocity(LaneFollower& lane, glm::vec2 pos, float speed);
    glm::vec2 lanePoint(const LaneFollower& lane, int i) const;
    const FlowField& flowField(glm::vec2 goal);
    void damage(entt::entity target, float amount, entt::entity source);

    const GameData* data_ = nullptr;
    const Map* map_ = nullptr;
    entt::registry registry_;
    std::uint64_t tick_ = 0;
    SpatialHash spatial_;
    float maxRadius_ = 1.0f;
    std::vector<SimEvent> events_;
    std::unordered_map<int, FlowField> flowFields_;
};

}
