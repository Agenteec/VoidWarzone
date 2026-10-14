#pragma once

#include <array>
#include <cstdint>
#include <memory>
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
inline constexpr float kVeteranXp = 60.0f;
inline constexpr float kEliteXp = 200.0f;
inline constexpr float kEliteCost = 100.0f;
inline constexpr float kHeroXp = 400.0f;
inline constexpr int kHeroShards = 2;
inline constexpr int kMaxHeroes = 3;
inline constexpr int kMaxHeroLevel = 10;
inline constexpr std::size_t kMaxSkills = 4;
inline constexpr std::uint8_t kMaxSkillLevel = 3;
inline constexpr std::size_t kStashSize = 12;

int itemSlots(Rank rank);
float xpForNextLevel(int level);

enum class Difficulty : std::uint8_t { Easy, Normal, Hard, Nightmare };
enum class MatchState : std::uint8_t { Playing, Victory, Defeat };

struct PlayerState {
    bool active = false;
    float energy = 0.0f;
    int kills = 0;
    int shards = 0;
    std::vector<ItemId> stash;
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

struct SystemTimes {
    static constexpr std::size_t kCount = 13;
    static const char* name(std::size_t i);
    std::array<double, kCount> seconds{};
    double peak = 0.0;
    std::uint64_t steps = 0;
    std::uint64_t paths = 0;
    std::uint64_t fields = 0;
    std::uint64_t navRebuilds = 0;
    double worstPath = 0.0;
    double worstField = 0.0;
    std::size_t worstWork = 0;
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
    void devBoost(std::uint8_t player);

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
    int heroCount(std::uint8_t player) const;
    void refreshStats(entt::entity e);
    void grantXp(entt::entity e, float amount);
    const std::vector<ResourcePointState>& resourcePoints() const { return points_; }
    const DefenseStatus& defense() const { return defense_; }
    const SystemTimes& systemTimes() const { return times_; }
    const Map& navMap() const { return nav_ ? *nav_ : *map_; }
    void resetSystemTimes() { times_ = {}; }

private:
    void rebuildSpatial();
    void updateSpawners();
    void updateUnits();
    void updateBosses();
    void integrate();
    void resolveCollisions();
    void updateTurrets();
    void updateWeapons();
    void updateProjectiles();
    void removeDead();
    void updateResourcePoints();
    void updateDefense();
    void spawnWave(const WaveDef& wave);
    void handle(const RecruitCommand& cmd);
    void handle(const PromoteCommand& cmd);
    void handle(const CastCommand& cmd);
    void handle(const LearnCommand& cmd);
    void handle(const BuyCommand& cmd);
    void handle(const EquipCommand& cmd);
    void handle(const UnequipCommand& cmd);
    Progression* ownedProgression(std::uint8_t player, entt::entity e);
    void updateHeroes();
    void onEnemyKilled(const UnitDef& def, glm::vec2 pos, int lastHitBy);
    void applySkillEffect(entt::entity caster, const SkillEffect& fx, std::uint8_t level, glm::vec2 center, SkillId skill);
    std::vector<entt::entity> unitsInRadius(glm::vec2 center, float radius, std::uint8_t team, bool sameTeam) const;
    void grantTeamEnergy(float amount);
    glm::vec2 playerBase(std::uint8_t player) const;
    float enemyHpScale() const;
    float enemyDamageScale() const;
    float enemyCountScale() const;

    entt::entity findEnemy(glm::vec2 pos, std::uint8_t team, float radius) const;
    glm::vec2 avoid(entt::entity e, glm::vec2 pos, glm::vec2 vel, float radius, std::uint8_t team) const;
    void rebuildNav();
    template <class F>
    void queryAll(glm::vec2 pos, float radius, F&& f) const {
        spatial_.query(pos, radius + maxUnitRadius_, f);
        structures_.query(pos, radius + maxStructureRadius_, f);
    }
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
    SpatialHash structures_;
    float maxUnitRadius_ = 0.5f;
    float maxStructureRadius_ = 0.5f;
    std::unique_ptr<Map> nav_;
    bool navDirty_ = true;
    int pathBudget_ = 0;
    std::vector<SimEvent> events_;
    std::size_t stepEvents_ = 0;
    std::unordered_map<int, FlowField> flowFields_;

    Difficulty difficulty_ = Difficulty::Normal;
    std::array<PlayerState, kMaxPlayers> players_{};
    std::vector<ResourcePointState> points_;
    DefenseStatus defense_;
    entt::entity core_ = entt::null;
    entt::entity portal_ = entt::null;
    int recruitCount_ = 0;
    SystemTimes times_;

    struct PendingRespawn {
        std::uint8_t player;
        UnitDefId def;
        Progression progression;
        float timer;
    };
    std::vector<PendingRespawn> respawns_;
};

}
