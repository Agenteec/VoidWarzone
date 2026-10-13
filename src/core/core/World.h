#pragma once

#include <cstdint>

#include <entt/entity/registry.hpp>
#include <glm/vec2.hpp>

namespace vw {

inline constexpr int kTickRate = 30;
inline constexpr float kTickDt = 1.0f / kTickRate;

struct Position {
    glm::vec2 value{};
};

struct Velocity {
    glm::vec2 value{};
};

class World {
public:
    void step();

    std::uint64_t tick() const { return tick_; }
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

private:
    entt::registry registry_;
    std::uint64_t tick_ = 0;
};

}
