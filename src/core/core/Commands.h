#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/vec2.hpp>

namespace vw {

struct MoveCommand {
    std::vector<entt::entity> units;
    glm::vec2 point{};
    bool attack = false;
};

struct AttackCommand {
    std::vector<entt::entity> units;
    entt::entity target = entt::null;
};

struct StopCommand {
    std::vector<entt::entity> units;
};

struct RecruitCommand {
    std::uint8_t player = 0;
    std::uint16_t unit = 0;
};

using Command = std::variant<MoveCommand, AttackCommand, StopCommand, RecruitCommand>;

enum class EventType : std::uint8_t { Spawn, Shot, Hit, Death, SlamWarn, Slam, Summon, Enrage, WaveStart, PointCaptured, MatchEnd };

struct SimEvent {
    EventType type = EventType::Spawn;
    entt::entity entity = entt::null;
    entt::entity other = entt::null;
    glm::vec2 from{};
    glm::vec2 to{};
    float value = 0.0f;
};

}
