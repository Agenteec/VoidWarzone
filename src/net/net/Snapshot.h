#pragma once

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <functional>
#include <numbers>
#include <vector>

#include "net/Serialize.h"

namespace vw::net {

enum class EntityKind : std::uint8_t { Unit = 0, Projectile = 1 };

struct EntityState {
    std::uint32_t id = 0;
    EntityKind kind = EntityKind::Unit;
    std::uint16_t def = 0;
    std::uint8_t team = 0;
    std::uint8_t owner = 0xff;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::int16_t vx = 0;
    std::int16_t vy = 0;
    std::uint8_t facing = 0;
    std::uint16_t hp = 0;
    std::uint16_t maxHp = 0;
    std::uint16_t shield = 0;
    std::uint8_t flags = 0;
    std::uint8_t rank = 0;
    std::array<std::uint8_t, 3> modules{0xff, 0xff, 0xff};
    std::uint8_t aim = 0;
    std::uint8_t pitch = 0;
    std::uint16_t tx = 0;
    std::uint16_t ty = 0;
};

struct NetEvent {
    std::uint8_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t value = 0;
};

inline constexpr std::uint32_t kNoEntity = 0xffffffffu;
inline constexpr std::uint8_t kStateBuilding = 1;
inline constexpr std::uint8_t kStateCarried = 2;
inline constexpr std::uint8_t kStateFrozen = 4;

struct PointInfo {
    std::int8_t control = 0;
    std::uint8_t owner = 0xff;
};

struct GameInfo {
    std::uint8_t state = 0;
    std::uint8_t wave = 0;
    std::uint8_t totalWaves = 0;
    std::uint8_t difficulty = 1;
    std::uint16_t nextWaveIn = 0;
    std::uint32_t boss = kNoEntity;
    std::vector<PointInfo> points;
};

struct OwnSkill {
    std::uint16_t skill = 0;
    std::uint8_t level = 0;
    std::uint16_t cooldown = 0;
};

struct OwnOrder {
    std::uint8_t type = 0;
    std::uint16_t unit = 0xffff;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct OwnUnit {
    std::uint32_t id = 0;
    std::uint8_t cargo = 0;
    std::vector<OwnOrder> orders;
    std::uint8_t rank = 0;
    std::uint8_t level = 0;
    std::uint16_t xp = 0;
    std::uint8_t skillPoints = 0;
    std::vector<OwnSkill> skills;
    std::vector<std::uint16_t> items;
};

struct ClientView {
    std::uint32_t commandSeq = 0;
    std::uint32_t energy = 0;
    std::uint16_t shards = 0;
    std::vector<std::uint16_t> stash;
    std::vector<OwnUnit> units;

    const OwnUnit* unit(std::uint32_t id) const;
};

struct Snapshot {
    std::uint32_t tick = 0;
    ClientView view;
    GameInfo game;
    std::vector<EntityState> entities;
    std::vector<NetEvent> events;

    const EntityState* find(std::uint32_t id) const;
};

using BaselineLookup = std::function<const Snapshot*(std::uint32_t tick)>;

void encodeSnapshot(Writer& w, const Snapshot& current, const Snapshot* baseline, const ClientView& view);
bool decodeSnapshot(Reader& r, Snapshot& out, const BaselineLookup& baseline);

inline std::uint16_t quantizePos(float v) {
    return static_cast<std::uint16_t>(std::clamp(std::lround(v * 64.0f), 0L, 65535L));
}
inline float dequantizePos(std::uint16_t q) {
    return static_cast<float>(q) / 64.0f;
}
inline std::int16_t quantizeVel(float v) {
    return static_cast<std::int16_t>(std::clamp(std::lround(v * 256.0f), -32768L, 32767L));
}
inline float dequantizeVel(std::int16_t q) {
    return static_cast<float>(q) / 256.0f;
}
inline std::uint8_t quantizeAngle(float a) {
    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    float t = std::fmod(a, twoPi);
    if (t < 0.0f)
        t += twoPi;
    return static_cast<std::uint8_t>(std::lround(t / twoPi * 256.0f) & 0xff);
}
inline float dequantizeAngle(std::uint8_t q) {
    return static_cast<float>(q) / 256.0f * 2.0f * std::numbers::pi_v<float>;
}

}
