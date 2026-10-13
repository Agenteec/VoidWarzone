#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_events.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "Prediction.h"
#include "core/GameData.h"
#include "core/Map.h"
#include "net/NetClient.h"
#include "net/NetLibrary.h"
#include "net/Protocol.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "server/Server.h"

namespace vw::client {

struct LaunchOptions {
    bool host = false;
    std::string connect;
    std::string name = "Player";
    std::string map = "siege_test";
    std::uint16_t port = net::kGamePort;
    Difficulty difficulty = Difficulty::Normal;
};

struct UnitVisual {
    Mesh shape = Mesh::Cube;
    float height = 1.0f;
};

class App {
public:
    bool init(SDL_Window* window, LaunchOptions options);
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void frame(double dt);
    bool running() const { return running_; }

private:
    enum class Screen { Menu, Lan, Address, Connecting, Game };

    struct Effect {
        Mesh mesh;
        glm::vec3 pos;
        glm::vec4 color;
        float from;
        float to;
        float life;
        float t = 0.0f;
    };

    void loadContent();
    void setScreen(Screen screen);
    void startHost();
    void startConnect(const net::Address& address);
    void leaveGame(std::string status);
    bool onConnected();

    void handleMenuKey(SDL_Keycode key);
    void handleGameEvent(const SDL_Event& e);
    void updateGame(float dt);
    void updateCamera(float dt);

    void clickSelect(glm::vec2 screen, bool add);
    void boxSelect(glm::vec2 a, glm::vec2 b, bool add);
    void issueCommand(glm::vec2 screen, bool attackMove);
    void pruneSelection();
    const net::InterpolatedEntity* entityAt(glm::vec2 p, float pad) const;
    const net::InterpolatedEntity* findEntity(std::uint32_t id) const;
    std::vector<entt::entity> selectedEntities() const;
    bool isOwn(const net::InterpolatedEntity& e) const { return e.owner == net_.player(); }
    glm::vec4 unitColor(const net::InterpolatedEntity& e) const;

    void addEffect(Mesh mesh, glm::vec3 pos, glm::vec4 color, float from, float to, float life);
    void handleNetEvent(const net::NetEvent& ev);
    void showBanner(std::string text, float seconds);
    void renderResourcePoints();
    void renderGameHud();

    void render();
    void renderWorld();
    void renderHud();
    void renderMenu();

    SDL_Window* window_ = nullptr;
    LaunchOptions options_;
    net::NetLibrary netLibrary_;
    Renderer renderer_;
    Camera camera_;
    std::unique_ptr<GameData> data_;
    std::vector<UnitVisual> visuals_;

    std::unique_ptr<Map> hostMap_;
    std::unique_ptr<server::Server> host_;
    std::unique_ptr<Map> map_;
    net::NetClient net_;
    net::LanBrowser lan_;
    Prediction prediction_;

    std::vector<net::InterpolatedEntity> entities_;
    std::vector<std::uint32_t> selected_;
    std::vector<Effect> effects_;
    std::string banner_;
    float bannerTime_ = 0.0f;

    Screen screen_ = Screen::Menu;
    std::string status_;
    std::string addressInput_;
    bool running_ = true;
    bool initialized_ = false;
    bool cameraPlaced_ = false;
    double now_ = 0.0;
    float fps_ = 0.0f;
    float pixelDensity_ = 1.0f;
    glm::vec2 mouse_{};
    glm::vec2 dragStart_{};
    bool dragging_ = false;
    bool attackMoveArmed_ = false;
};

}
