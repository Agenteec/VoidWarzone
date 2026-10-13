#pragma once

#include <cstdint>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/vec2.hpp>

#include "core/GameData.h"

namespace vw {

struct Position {
    glm::vec2 value{};
};

struct PrevPosition {
    glm::vec2 value{};
};

struct Velocity {
    glm::vec2 value{};
};

struct Facing {
    float angle = 0.0f;
};

struct Unit {
    UnitDefId def = kInvalidUnitDef;
    float radius = 0.5f;
    float speed = 0.0f;
    float sight = 8.0f;
};

struct Team {
    std::uint8_t id = 0;
};

struct Owner {
    std::uint8_t player = 0;
};

struct Health {
    float hp = 100.0f;
    float max = 100.0f;
    float armor = 0.0f;
};

struct Weapon {
    WeaponDef def;
    float cooldown = 0.0f;
};

struct Structure {};

struct Target {
    entt::entity value = entt::null;
};

enum class OrderType : std::uint8_t { None, Move, AttackMove, Attack };

struct Order {
    OrderType type = OrderType::None;
    glm::vec2 point{};
    entt::entity target = entt::null;
};

struct Navigator {
    glm::vec2 goal{};
    std::vector<glm::vec2> path;
    std::size_t next = 0;
    int repath = 0;
};

struct LaneFollower {
    int lane = 0;
    int waypoint = 1;
    bool reverse = false;
};

struct Spawner {
    UnitDefId unit = kInvalidUnitDef;
    std::uint8_t team = 0;
    int lane = 0;
    float interval = 30.0f;
    float timer = 0.0f;
    int count = 1;
    float offset = 4.0f;
};

struct Projectile {
    entt::entity source = entt::null;
    entt::entity target = entt::null;
    glm::vec2 targetPos{};
    float speed = 10.0f;
    float damage = 0.0f;
    std::uint8_t team = 0;
};

}
