#pragma once

#include <array>
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
    int lastHitBy = -1;
    float shield = 0.0f;
    float shieldMax = 0.0f;
    float shieldDelay = 0.0f;
};

struct Weapon {
    WeaponDef def;
    float cooldown = 0.0f;
};

struct Turret {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct Structure {};

struct Producer {
    UnitDefId unit = kInvalidUnitDef;
    float interval = 20.0f;
    float timer = 0.0f;
    int max = 4;
    std::vector<entt::entity> spawned;
};

struct Invulnerable {};

struct Target {
    entt::entity value = entt::null;
};

enum class OrderType : std::uint8_t { None, Move, AttackMove, Attack, Patrol, Hold, Follow, Build, Board, Unload };

struct Order {
    OrderType type = OrderType::None;
    glm::vec2 point{};
    entt::entity target = entt::null;
    glm::vec2 origin{};
    std::uint16_t build = 0xffff;
};

struct Air {};

struct Modules {
    std::array<std::uint16_t, 3> slots{0xffff, 0xffff, 0xffff};
};

struct SecondaryWeapon {
    WeaponDef def;
    float cooldown = 0.0f;
    entt::entity target = entt::null;
};

struct Carried {
    entt::entity transport = entt::null;
};

struct Cargo {
    std::vector<entt::entity> units;
};

struct Construction {
    float progress = 0.0f;
};

struct OrderQueue {
    std::vector<Order> items;
};

struct Motion {
    float speed = 0.0f;
};

struct Navigator {
    glm::vec2 goal{};
    std::vector<glm::vec2> path;
    std::size_t next = 0;
    int repath = 0;
    bool direct = false;
};

struct Progress {
    glm::vec2 anchor{};
    int ticks = 0;
    int detour = 0;
    float side = 1.0f;
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
    entt::entity building = entt::null;
};

struct Projectile {
    entt::entity source = entt::null;
    entt::entity target = entt::null;
    glm::vec2 targetPos{};
    float speed = 10.0f;
    float damage = 0.0f;
    DamageType type = DamageType::Kinetic;
    float splash = 0.0f;
    std::uint8_t team = 0;
    int owner = -1;
    UnitDefId def = kInvalidUnitDef;
};

struct BaseStats {
    float maxHp = 100.0f;
    float armor = 0.0f;
    float speed = 0.0f;
    float damage = 0.0f;
    float cooldown = 1.0f;
    float range = 0.0f;
};

enum class Rank : std::uint8_t { Recruit, Veteran, Elite, Hero };

struct SkillSlot {
    SkillId skill = kInvalidId;
    std::uint8_t level = 1;
    float cooldown = 0.0f;
};

struct Progression {
    Rank rank = Rank::Recruit;
    int level = 0;
    float xp = 0.0f;
    int skillPoints = 0;
    std::vector<SkillSlot> skills;
    std::vector<ItemId> items;
};

struct ActiveBuff {
    StatMods mods;
    float remaining = 0.0f;
    SkillId skill = kInvalidId;
    entt::entity source = entt::null;
};

struct Buffs {
    std::vector<ActiveBuff> list;
};

struct Regen {
    float perSecond = 0.0f;
};

struct Summoned {
    float remaining = 0.0f;
};

struct Boss {
    std::vector<float> cooldowns;
    bool enraged = false;
    int casting = -1;
    float castTimer = 0.0f;
    glm::vec2 castPos{};
};

}
