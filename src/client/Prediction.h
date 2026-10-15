#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

#include "core/GameData.h"
#include "core/Map.h"
#include "net/NetClient.h"

namespace vw::client {

class Prediction {
public:
    void clear() { units_.clear(); }
    void onMove(const std::vector<net::InterpolatedEntity>& units, glm::vec2 point, std::uint32_t seq, const GameData& data, const Map& map);
    void cancel(const std::vector<std::uint32_t>& ids);
    void update(float dt, double now, const net::NetClient& net, const GameData& data, const Map& map, std::vector<net::InterpolatedEntity>& entities);
    std::size_t active() const { return units_.size(); }

private:
    struct Unit {
        std::uint32_t seq = 0;
        glm::vec2 pos{};
        glm::vec2 goal{};
        float facing = 0.0f;
        std::vector<glm::vec2> path;
        std::size_t next = 0;
        float speed = 0.0f;
        float radius = 0.5f;
        float current = 0.0f;
        float accel = 10.0f;
        float turnRate = 8.0f;
        bool air = false;
    };

    std::unordered_map<std::uint32_t, Unit> units_;
};

}
