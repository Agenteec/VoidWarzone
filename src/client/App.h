#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_events.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/World.h"
#include "render/Camera.h"
#include "render/Renderer.h"

namespace vw::client {

struct UnitVisual {
    Mesh shape = Mesh::Cube;
    float height = 1.0f;
};

class App {
public:
    bool init(SDL_Window* window);
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void frame(double dt);
    bool running() const { return running_; }

private:
    struct Effect {
        glm::vec3 pos;
        glm::vec4 color;
        float size;
        float t;
        float life;
    };

    void loadContent();
    void step();
    void updateCamera(float dt);
    void render(float alpha);
    void renderHud();
    void clickSelect(glm::vec2 screen, bool add);
    void boxSelect(glm::vec2 a, glm::vec2 b, bool add);
    void issueCommand(glm::vec2 screen, bool attackMove);
    void pruneSelection();
    bool isOwn(entt::entity e) const;
    glm::vec4 unitColor(entt::entity e) const;

    SDL_Window* window_ = nullptr;
    Renderer renderer_;
    Camera camera_;
    std::unique_ptr<GameData> data_;
    std::unique_ptr<Map> map_;
    std::unique_ptr<World> world_;
    std::vector<UnitVisual> visuals_;
    std::vector<entt::entity> selected_;
    std::vector<Effect> effects_;

    bool running_ = true;
    bool initialized_ = false;
    double accumulator_ = 0.0;
    float fps_ = 0.0f;
    float pixelDensity_ = 1.0f;
    glm::vec2 mouse_{};
    glm::vec2 dragStart_{};
    bool dragging_ = false;
    bool attackMoveArmed_ = false;
    std::uint8_t localPlayer_ = 0;
    std::uint8_t localTeam_ = 0;
};

}
