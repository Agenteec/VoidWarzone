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

struct PromoteCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
};

struct CastCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint8_t slot = 0;
    glm::vec2 point{};
};

struct LearnCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint16_t skill = 0;
};

struct BuyCommand {
    std::uint8_t player = 0;
    std::uint16_t item = 0;
};

struct EquipCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint8_t stashIndex = 0;
};

struct UnequipCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint8_t slot = 0;
};

struct BuildCommand {
    std::uint8_t player = 0;
    std::uint16_t unit = 0;
    glm::vec2 pos{};
};

using Command = std::variant<MoveCommand, AttackCommand, StopCommand, RecruitCommand, PromoteCommand, CastCommand, LearnCommand, BuyCommand,
                             EquipCommand, UnequipCommand, BuildCommand>;

template <class T>
concept UnitCommand = requires(T t) { t.units; };

enum class EventType : std::uint8_t {
    Spawn,
    Shot,
    Hit,
    Death,
    SlamWarn,
    Slam,
    Summon,
    Enrage,
    WaveStart,
    PointCaptured,
    MatchEnd,
    SkillCast,
    Promote,
    LevelUp,
    Loot,
};

struct SimEvent {
    EventType type = EventType::Spawn;
    entt::entity entity = entt::null;
    entt::entity other = entt::null;
    glm::vec2 from{};
    glm::vec2 to{};
    float value = 0.0f;
};

}
