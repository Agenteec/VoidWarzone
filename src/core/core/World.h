#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>
#include <nlohmann/json_fwd.hpp>
#include <glm/vec2.hpp>

#include "core/Commands.h"
#include "core/Components.h"
#include "core/GameData.h"
#include "core/Map.h"
#include "core/Build.h"
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
inline constexpr SkillId kFrostBuff = 0xfffe;
inline constexpr std::uint8_t kMaxSkillLevel = 3;
inline constexpr std::size_t kStashSize = 12;

int itemSlots(Rank rank);
float xpForNextLevel(int level);

enum class Difficulty : std::uint8_t { Easy, Normal, Hard, Nightmare };
enum class GameMode : std::uint8_t { Defense, Endless, Arena, Skirmish };
enum class MatchState : std::uint8_t { Playing, Victory, Defeat };

struct PlayerState {
    bool active = false;
    float energy = 0.0f;
    int kills = 0;
    int shards = 0;
    std::vector<ItemId> stash;
    std::vector<std::uint8_t> researched;
    bool god = false;
    std::uint8_t team = 0;
    std::uint8_t slot = 0;
    bool ai = false;
};

inline constexpr std::uint8_t kVoidTeam = 1;
inline constexpr std::uint8_t kFirstSkirmishTeam = 2;

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
    int winner = -1;
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
const char* gameModeName(GameMode m);
bool parseGameMode(std::string_view text, GameMode& out);

class World;

struct SaveHeader {
    std::string map;
    GameMode mode = GameMode::Defense;
    Difficulty difficulty = Difficulty::Normal;
    std::uint64_t tick = 0;
};

std::string makeSaveFile(const SaveHeader& header, const World& world);
SaveHeader readSaveHeader(std::string_view text);
bool validSaveName(std::string_view name);

class World {
public:
    World();
    World(const GameData& data, const Map& map, Difficulty difficulty = Difficulty::Normal, GameMode mode = GameMode::Defense);

    void step();
    void apply(const Command& command);
    entt::entity spawn(UnitDefId def, std::uint8_t team, glm::vec2 pos, int owner = -1);
    std::vector<entt::entity> spawnOnLane(UnitDefId def, std::uint8_t team, int lane, int count, float offset,
                                          std::optional<glm::vec2> origin = std::nullopt);
    std::optional<glm::vec2> portalMouth() const;
    int anchorsAlive() const;
    std::vector<entt::entity> spawnPlayer(std::uint8_t player, std::uint8_t team = 0);
    void releasePlayer(std::uint8_t player);
    std::string saveState() const;
    void handle(const DeployCommand& cmd);
    void handle(const StructureCommand& cmd);
    void handle(const CheatCommand& cmd);
    void setCheats(bool on) { cheats_ = on; }
    // Fog of war: per-team grids, bit 0 = visible now, bit 1 = explored. Without fog everything counts as seen.
    void setFog(bool on) { fog_ = on; }
    bool fog() const { return fog_; }
    int sidesLeft() const { return sidesLeft_; }
    bool visibleTo(std::uint8_t team, glm::vec2 pos) const;
    bool exploredBy(std::uint8_t team, glm::vec2 pos) const;
    void updateVisibility();
    bool cheats() const { return cheats_; }
    bool researched(std::uint8_t player, ResearchId topic) const;
    // the unit's own topic plus every designer component topic
    bool unlockedFor(std::uint8_t player, const UnitDef& def) const {
        if (def.research != kInvalidId && !researched(player, def.research))
            return false;
        return std::all_of(def.researchAll.begin(), def.researchAll.end(), [&](ResearchId r) { return researched(player, r); });
    }
    bool canResearch(std::uint8_t player, ResearchId topic) const;
    void grantResearch(std::uint8_t player, ResearchId topic);
    std::string exportResearch(std::uint8_t player) const;
    void importResearch(std::uint8_t player, std::string_view list);
    static constexpr std::size_t kQueueSize = 5;
    bool campaignRules() const { return map_ && (map_->mission().enabled || mode_ == GameMode::Skirmish); }
    void setAi(std::uint8_t player, bool on);
    glm::vec2 startOf(std::uint8_t player) const;
    const std::vector<std::uint8_t>& objectives() const { return objectiveState_; }
    std::vector<std::string> takeMessages() { return std::exchange(messages_, {}); }
    float missionTime() const { return static_cast<float>(tick_) * kTickDt; }
    std::vector<entt::entity> spawnRoster(std::uint8_t player, std::string_view roster);
    std::string exportRoster(std::uint8_t player, std::size_t limit) const;
    // Campaign base: every completed own structure and unit (save format, positions kept); spawnBase replaces the
    // player's map-placed and starting entities with it. Entries without "pos" are placed next to the HQ.
    std::string exportBase(std::uint8_t player) const;
    void setEnergy(std::uint8_t player, float energy) { players_[player].energy = energy; }
    void spawnBase(std::uint8_t player, std::string_view base);
    void loadState(std::string_view text);
    bool resumePlayer(std::uint8_t player);
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
    GameMode mode() const { return mode_; }
    SiteStatus checkSite(UnitDefId def, glm::vec2 pos, std::uint8_t team, bool phantoms = false) const;
    void damage(entt::entity target, float amount, int sourceOwner, entt::entity source, DamageType type = DamageType::Kinetic);
    void clearSite(UnitDefId def, glm::vec2 pos, std::uint8_t team);
    void assistConstruction();
    bool canBuild(UnitDefId def, glm::vec2 pos) const { return checkSite(def, pos, 0) == SiteStatus::Ok; }
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
    void prewarmFields();
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
    void handle(const RefitCommand& cmd);
    void handle(const ModuleCommand& cmd);
    struct Strike {
        glm::vec2 pos;
        float radius;
        float amount;
        DamageType type;
        std::uint8_t team;
        int owner;
        entt::entity source;
        float timer;
    };
    std::vector<Strike> strikes_;
    void updateStrikes();
    void updateRepair();
    glm::vec2 builderStep(entt::entity e, glm::vec2 pos, const Unit& unit, std::uint8_t team, Order& order);
    entt::entity structureAt(glm::vec2 pos, UnitDefId def) const;
    void updateConstruction();
    void finishOrder(entt::entity e, Order& order);
    void applyMotion(entt::entity e, const Unit& unit, glm::vec2& vel, float& facing);
    void updateProducers();
    Progression* ownedProgression(std::uint8_t player, entt::entity e);
    void updateHeroes();
    void onEnemyKilled(const UnitDef& def, glm::vec2 pos, int lastHitBy);
    void applySkillEffect(entt::entity caster, const SkillEffect& fx, std::uint8_t level, glm::vec2 center, SkillId skill);
    std::vector<entt::entity> unitsInRadius(glm::vec2 center, float radius, std::uint8_t team, bool sameTeam) const;
    void grantTeamEnergy(float amount);
    glm::vec2 playerBase(std::uint8_t player) const;
    float endlessCycle() const;
    float enemyHpScale() const;
    float enemyDamageScale() const;
    float enemyCountScale() const;

    entt::entity findEnemy(glm::vec2 pos, std::uint8_t team, float radius, std::uint8_t canHit = 3) const;
    bool canHit(entt::entity shooter, entt::entity target) const;
    void updateTransport();
    void unloadAll(entt::entity transport, glm::vec2 at);
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
    void splashDamage(glm::vec2 at, float radius, float amount, std::uint8_t team, int owner, entt::entity source, DamageType type,
                      entt::entity skip);
    void updateShields();
    void applySlow(entt::entity target, float move, float attack, float time);

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
    int builtVersion_ = 0;
    int fieldsVersion_ = 0;
    int pathBudget_ = 0;
    std::vector<SimEvent> events_;
    std::size_t stepEvents_ = 0;
    std::unordered_map<int, FlowField> flowFields_;

    Difficulty difficulty_ = Difficulty::Normal;
    GameMode mode_ = GameMode::Defense;
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
    std::array<bool, kMaxPlayers> saved_{};
    std::vector<std::uint8_t> objectiveState_;
    std::vector<int> triggerFired_;
    std::vector<float> triggerNext_;
    std::vector<std::string> messages_;
    bool missionStarted_ = false;
    void updateMission();
    void updateFactories();
    void updateResearch();
    void updateAi();
    void thinkAi(std::uint8_t player);
    void updateSkirmish();
    void grantEnergyTo(std::uint8_t team, float amount);
    struct AiState {
        int attackSize = 6;
        float nextAttack = 0.0f;
        int buildTry = 0;
        float stageUntil = -1.0f;
        glm::vec2 stage{};
        glm::vec2 goal{};
        std::array<float, 32> seenStrength{};
        entt::entity target = entt::null;
    };
    std::array<AiState, kMaxPlayers> ai_{};
    int skirmishSlots_ = 0;
    int skirmishTeams_ = 0;
    int sidesLeft_ = 0;
    void refreshOwned(std::uint8_t player);
    bool cheats_ = false;
    bool fog_ = false;
    std::unordered_map<std::uint8_t, std::vector<std::uint8_t>> vision_;
    std::uint8_t visionBits(std::uint8_t team, glm::vec2 pos) const;
    void runAction(const MissionAction& action);
    bool tagAlive(const std::string& tag) const;
    bool playerNear(glm::vec2 pos, float radius) const;
    void finishObjective(const std::string& id, std::uint8_t state);
    entt::entity loadUnit(const nlohmann::json& u);
};

}
