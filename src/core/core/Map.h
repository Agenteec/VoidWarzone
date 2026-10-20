#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

namespace vw {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct Lane {
    std::string name;
    std::vector<glm::vec2> points;
};

struct PlacedUnit {
    std::string unit;
    std::string role;
    std::string tag;
    std::uint8_t team = 0;
    int owner = -1;
    glm::vec2 pos{};
    std::vector<std::string> modules;
};

struct SpawnerDef {
    std::string unit;
    std::uint8_t team = 0;
    int lane = 0;
    float interval = 30.0f;
    float delay = 0.0f;
    int count = 1;
    float offset = 4.0f;
    std::string building;
};

enum class CellKind : std::uint8_t { Open, Rock, Building, Forest, Water };

struct BuildingDef {
    std::vector<glm::vec2> poly;
    int levels = 5;
    std::string kind;
    bool ruin = false;
    bool unfinished = false;
};

struct PropDef {
    std::string model;
    glm::vec2 pos{};
    float yaw = 0.0f;
    float radius = 0.0f;
};

struct AreaDef {
    std::string kind;
    std::vector<glm::vec2> poly;
};

struct PathDef {
    std::string kind;
    float width = 1.0f;
    std::vector<glm::vec2> points;
};

bool pointInPolygon(glm::vec2 p, const std::vector<glm::vec2>& poly);

struct MissionObjective {
    std::string id;
    std::string text;
    std::string type;
    std::string tag;
    glm::vec2 pos{};
    float radius = 6.0f;
    float time = 0.0f;
    std::uint8_t team = 1;
    bool hidden = false;
    bool optional = false;
};

struct MissionAction {
    std::string type;
    std::string text;
    std::string unit;
    std::string tag;
    std::string objective;
    int count = 1;
    std::uint8_t team = 1;
    bool player = false;
    glm::vec2 pos{};
    std::optional<glm::vec2> target;
    std::string targetTag;
    std::string topic;
    float amount = 0.0f;
};

struct MissionTrigger {
    std::string when;
    float time = 0.0f;
    glm::vec2 pos{};
    float radius = 6.0f;
    std::string tag;
    std::string objective;
    float repeat = 0.0f;
    int times = 1;
    std::vector<MissionAction> actions;
};

struct MissionDef {
    bool enabled = false;
    std::string title;
    std::string briefing;
    std::vector<MissionObjective> objectives;
    std::vector<MissionTrigger> triggers;
};

struct PlayerStart {
    glm::vec2 pos{};
    std::vector<std::string> units;
    std::vector<glm::vec2> slots;
};

struct ResourcePointDef {
    std::string name;
    glm::vec2 pos{};
    float radius = 4.0f;
    float income = 2.0f;
};

struct WaveGroup {
    std::string unit;
    int count = 1;
    std::vector<int> lanes;
    bool raid = false;
};

struct WaveDef {
    std::vector<WaveGroup> groups;
    std::string boss;
    int bossLane = 0;
    float delay = -1.0f;
};

struct DefenseDef {
    bool enabled = false;
    float firstWaveDelay = 20.0f;
    float waveInterval = 40.0f;
    float baseIncome = 1.0f;
    float startEnergy = 100.0f;
    std::vector<WaveDef> waves;
};

class Map {
public:
    Map(int width, int height);

    static Map parse(std::string_view json);

    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& name() const { return name_; }
    std::uint64_t hash() const { return hash_; }

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    bool blocked(int x, int y) const { return !inside(x, y) || cells_[index(x, y)] != 0; }
    bool blocked(glm::ivec2 c) const { return blocked(c.x, c.y); }
    int index(int x, int y) const { return y * width_ + x; }
    int index(glm::ivec2 c) const { return index(c.x, c.y); }

    void addWall(const Rect& r);
    void setBlocked(int x, int y, bool blocked) { cells_[static_cast<std::size_t>(index(x, y))] = blocked ? 1 : 0; }
    CellKind cellKind(int x, int y) const { return inside(x, y) ? static_cast<CellKind>(cells_[index(x, y)]) : CellKind::Rock; }
    void fill(const std::vector<glm::vec2>& poly, CellKind kind);
    void setCell(int x, int y, CellKind kind) { cells_[static_cast<std::size_t>(index(x, y))] = static_cast<std::uint8_t>(kind); }
    std::string encodeCells() const;

    static glm::ivec2 cellOf(glm::vec2 p) { return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))}; }
    static glm::vec2 center(glm::ivec2 c) { return glm::vec2(c) + 0.5f; }

    const std::vector<Rect>& walls() const { return walls_; }
    const std::vector<Lane>& lanes() const { return lanes_; }
    const std::vector<PlacedUnit>& units() const { return units_; }
    const std::vector<SpawnerDef>& spawners() const { return spawners_; }
    const PlayerStart& playerStart() const { return playerStart_; }
    const std::vector<ResourcePointDef>& resourcePoints() const { return resourcePoints_; }
    const DefenseDef& defense() const { return defense_; }
    const std::string& theme() const { return theme_; }
    const std::vector<BuildingDef>& buildings() const { return buildings_; }
    const std::vector<AreaDef>& areas() const { return areas_; }
    const std::vector<PathDef>& paths() const { return paths_; }
    const std::vector<PropDef>& props() const { return props_; }
    const MissionDef& mission() const { return mission_; }
    bool hasElevation() const { return !elevation_.empty(); }
    float elevation(glm::vec2 p) const;
    int laneIndex(std::string_view name) const;

private:
    int requireLane(std::string_view name) const;

    int width_ = 0;
    int height_ = 0;
    std::string name_;
    std::vector<std::uint8_t> cells_;
    std::vector<Rect> walls_;
    std::vector<Lane> lanes_;
    std::vector<PlacedUnit> units_;
    std::vector<SpawnerDef> spawners_;
    PlayerStart playerStart_;
    std::vector<ResourcePointDef> resourcePoints_;
    DefenseDef defense_;
    std::string theme_;
    std::vector<BuildingDef> buildings_;
    std::vector<AreaDef> areas_;
    std::vector<PathDef> paths_;
    std::vector<PropDef> props_;
    MissionDef mission_;
    std::vector<float> elevation_;
    int elevationStep_ = 1;
    int elevationColumns_ = 0;
    int elevationRows_ = 0;
    std::uint64_t hash_ = 0;
};

}
