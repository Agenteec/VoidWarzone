#pragma once

#include <cmath>
#include <cstdint>
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
    std::uint8_t team = 0;
    int owner = -1;
    glm::vec2 pos{};
};

struct SpawnerDef {
    std::string unit;
    std::uint8_t team = 0;
    int lane = 0;
    float interval = 30.0f;
    float delay = 0.0f;
    int count = 1;
    float offset = 4.0f;
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

    static glm::ivec2 cellOf(glm::vec2 p) { return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))}; }
    static glm::vec2 center(glm::ivec2 c) { return glm::vec2(c) + 0.5f; }

    const std::vector<Rect>& walls() const { return walls_; }
    const std::vector<Lane>& lanes() const { return lanes_; }
    const std::vector<PlacedUnit>& units() const { return units_; }
    const std::vector<SpawnerDef>& spawners() const { return spawners_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::string name_;
    std::vector<std::uint8_t> cells_;
    std::vector<Rect> walls_;
    std::vector<Lane> lanes_;
    std::vector<PlacedUnit> units_;
    std::vector<SpawnerDef> spawners_;
    std::uint64_t hash_ = 0;
};

}
