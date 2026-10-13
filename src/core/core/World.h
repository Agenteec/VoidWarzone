#pragma once

#include <array>
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
inline constexpr int kMaxPlayers = 64;

enum class Difficulty : std::uint8_t { Easy, Normal, Hard, Nightmare };
enum class MatchState : std::uint8_t { Playing, Victory, Defeat };

struct PlayerState {
    bool active = false;
    float energy = 0.0f;
    int kills = 0;
};

struct ResourcePointState {
    float control = 0.0f;
    int owner = -1;
};

struct DefenseStatus {
    int wave = 0;
    int totalWaves = 0;
    float nextWaveIn = 0.0f;
    MatchState state = MatchState::Playing;
    entt::entity boss = entt::null;
};

const char* difficultyName(Difficulty d);
bool parseDifficulty(std::string_view text, Difficulty& out);

class World {
public:
    World();
    World(const GameData& data, const Map& map, Difficulty difficulty = Difficulty::Normal);

    void step();
    void apply(const Command& command);
    entt::entity spawn(UnitDefId def, std::uint8_t team, glm::vec2 pos, int owner = -1);
    std::vector<entt::entity> spawnOnLane(UnitDefId def, std::uint8_t team, int lane, int count, float offset);
    std::vector<entt::entity> spawnPlayer(std::uint8_t player, std::uint8_t team = 0);
    void releasePlayer(std::uint8_t player);

    bool alive(entt::entity e) const;
    entt::entity unitAt(glm::vec2 p, float pad = 0.3f) const;

    std::uint64_t tick() const { return tick_; }
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    const std::vector<SimEvent>& events() const { return events_; }
    const GameData* data() const { return data_; }
    const Map* map() const { return map_; }

    Difficulty difficulty() const { return difficulty_; }
    const PlayerState& player(std::uint8_t p) const { return players_[p]; }
    int activePlayers() const;
    const std::vector<ResourcePointState>& resourcePoints() const { return points_; }
    const DefenseStatus& defense() const { return defense_; }

private:
    void rebuildSpatial();
    void updateSpawners();
    void updateUnits();
    void updateBosses();
    void integrate();
    void resolveCollisions();
    void updateWeapons();
    void updateProjectiles();
    void removeDead();
    void updateResourcePoints();
    void updateDefense();
    void spawnWave(const WaveDef& wave);
    void recruit(const RecruitCommand& cmd);
    void grantTeamEnergy(float amount);
    glm::vec2 playerBase(std::uint8_t player) const;
    float enemyHpScale() const;
    float enemyDamageScale() const;
    float enemyCountScale() const;

    entt::entity findEnemy(glm::vec2 pos, std::uint8_t team, float radius) const;
    glm::vec2 navigate(entt::entity e, glm::vec2 pos, glm::vec2 goal, float speed);
    glm::vec2 laneVelocity(LaneFollower& lane, glm::vec2 pos, float speed);
    glm::vec2 lanePoint(const LaneFollower& lane, int i) const;
    const FlowField& flowField(glm::vec2 goal);
    void damage(entt::entity target, float amount, int sourceOwner, entt::entity source);

    const GameData* data_ = nullptr;
    const Map* map_ = nullptr;
    entt::registry registry_;
    std::uint64_t tick_ = 0;
    SpatialHash spatial_;
    float maxRadius_ = 1.0f;
    std::vector<SimEvent> events_;
    std::unordered_map<int, FlowField> flowFields_;

    Difficulty difficulty_ = Difficulty::Normal;
    std::array<PlayerState, kMaxPlayers> players_{};
    std::vector<ResourcePointState> points_;
    DefenseStatus defense_;
    entt::entity core_ = entt::null;
    entt::entity portal_ = entt::null;
    int recruitCount_ = 0;
};

}
