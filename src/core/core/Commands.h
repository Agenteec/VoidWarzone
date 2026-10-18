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
    bool queue = false;
    bool patrol = false;
};

struct AttackCommand {
    std::vector<entt::entity> units;
    entt::entity target = entt::null;
    bool queue = false;
};

struct StopCommand {
    std::vector<entt::entity> units;
    bool hold = false;
};

struct RecruitCommand {
    std::uint8_t player = 0;
    std::uint16_t unit = 0;
    entt::entity factory = entt::null;
};

enum class StructureOp : std::uint8_t { Rally, Cancel, Demolish, Research };

struct StructureCommand {
    std::uint8_t player = 0;
    entt::entity target = entt::null;
    StructureOp op = StructureOp::Rally;
    std::uint8_t slot = 0;
    glm::vec2 point{};
};

enum class CheatOp : std::uint8_t { Energy, Spawn, Kill, Heal, Finish, Research, Xp, God, Win, Lose, Wave, Count };

struct CheatCommand {
    std::uint8_t player = 0;
    CheatOp op = CheatOp::Energy;
    std::uint16_t unit = 0;
    std::uint8_t team = 0;
    std::uint8_t count = 1;
    glm::vec2 point{};
    std::vector<entt::entity> targets;
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
    std::vector<entt::entity> units;
    std::uint16_t unit = 0;
    glm::vec2 pos{};
    bool queue = false;
};

struct RefitCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint16_t variant = 0;
};

struct UnloadCommand {
    std::vector<entt::entity> units;
    glm::vec2 point{};
    bool queue = false;
};

struct ModuleCommand {
    std::uint8_t player = 0;
    entt::entity unit = entt::null;
    std::uint16_t module = 0;
};

struct DeployCommand {
    std::uint8_t player = 0;
    std::vector<entt::entity> targets;
};

using Command = std::variant<MoveCommand, AttackCommand, StopCommand, RecruitCommand, PromoteCommand, CastCommand, LearnCommand, BuyCommand,
                             EquipCommand, UnequipCommand, BuildCommand, RefitCommand, UnloadCommand, ModuleCommand, DeployCommand, StructureCommand, CheatCommand>;

template <class T>
concept UnitCommand = requires(T t) { t.units; };

enum class EventType : std::uint8_t {
    Objective,
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
