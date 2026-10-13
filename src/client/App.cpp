#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "core/World.h"

namespace vw::client {

namespace {

constexpr glm::vec4 kGround{0.06f, 0.06f, 0.10f, 1.0f};
constexpr glm::vec4 kLane{0.10f, 0.10f, 0.17f, 1.0f};
constexpr glm::vec4 kWall{0.22f, 0.20f, 0.32f, 1.0f};
constexpr glm::vec4 kTeamColors[] = {{0.25f, 0.75f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.55f, 1.0f}};
constexpr glm::vec4 kOwnColor{0.35f, 1.0f, 0.55f, 1.0f};
constexpr glm::vec4 kAllyColor{0.95f, 0.85f, 0.3f, 1.0f};
constexpr glm::vec4 kSelectColor{0.35f, 1.0f, 0.55f, 0.55f};
constexpr float kMinZoom = 12.0f;
constexpr float kMaxZoom = 90.0f;
constexpr float kDragThreshold = 5.0f;

std::string loadText(const std::string& rel) {
#if defined(SDL_PLATFORM_ANDROID)
    const std::string path = rel;
#else
    const std::string path = std::string(SDL_GetBasePath()) + rel;
#endif
    std::size_t size = 0;
    void* buf = SDL_LoadFile(path.c_str(), &size);
    if (!buf)
        throw std::runtime_error("cannot load " + path + ": " + SDL_GetError());
    std::string text(static_cast<const char*>(buf), size);
    SDL_free(buf);
    return text;
}

Mesh parseShape(const std::string& s) {
    if (s == "cylinder")
        return Mesh::Cylinder;
    if (s == "sphere")
        return Mesh::Sphere;
    return Mesh::Cube;
}

template <class... Args>
void text(int x, int y, std::uint8_t attr, const char* format, Args... args) {
    bgfx::dbgTextPrintf(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), attr, format, args...);
}

double nowSeconds() {
    return static_cast<double>(SDL_GetTicksNS()) * 1e-9;
}

}

bool App::init(SDL_Window* window, LaunchOptions options) {
    window_ = window;
    options_ = std::move(options);
    now_ = nowSeconds();
    pixelDensity_ = SDL_GetWindowPixelDensity(window);
    if (!netLibrary_.ok() || !renderer_.init(window))
        return false;
    initialized_ = true;

    try {
        loadContent();
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return false;
    }
    spdlog::info("Void Warzone {} | {}", kVersion, renderer_.name());

    if (options_.host) {
        startHost();
    } else if (!options_.connect.empty()) {
        if (const auto addr = net::Address::parse(options_.connect, net::kGamePort))
            startConnect(*addr);
        else
            status_ = "bad address: " + options_.connect;
    }
    return true;
}

void App::loadContent() {
    const std::string unitsText = loadText("data/units.json");
    data_ = std::make_unique<GameData>(GameData::parse(unitsText));

    visuals_.assign(data_->units().size(), {});
    const auto units = nlohmann::json::parse(unitsText);
    for (const auto& u : units.at("units")) {
        const auto it = u.find("visual");
        if (it == u.end())
            continue;
        auto& v = visuals_[data_->find(u.at("id").get<std::string>())];
        v.shape = parseShape(it->value("shape", "cube"));
        v.height = it->value("height", v.height);
    }
}

void App::shutdown() {
    net_.disconnect();
    if (host_)
        host_->stop();
    host_.reset();
    if (initialized_)
        renderer_.shutdown();
}

void App::setScreen(Screen screen) {
    if (screen_ == Screen::Lan && screen != Screen::Lan)
        lan_.stop();
    if (screen_ == Screen::Address && screen != Screen::Address)
        SDL_StopTextInput(window_);
    if (screen == Screen::Lan)
        lan_.start();
    if (screen == Screen::Address)
        SDL_StartTextInput(window_);
    screen_ = screen;
}

void App::startHost() {
    try {
        hostMap_ = std::make_unique<Map>(Map::parse(loadText("data/maps/" + options_.map + ".json")));
    } catch (const std::exception& e) {
        status_ = e.what();
        return;
    }
    server::ServerConfig config;
    config.port = options_.port;
    config.mapName = options_.map;
    config.name = options_.name + "'s game";
    config.difficulty = options_.difficulty;
    host_ = std::make_unique<server::Server>(*data_, *hostMap_, config);
    if (!host_->start(now_)) {
        host_.reset();
        status_ = "cannot host: port " + std::to_string(options_.port) + " is busy";
        return;
    }
    startConnect(net::Address::loopback(host_->port()));
}

void App::startConnect(const net::Address& address) {
    status_.clear();
    if (!net_.connect(address, data_->hash(), options_.name, now_)) {
        status_ = net_.error();
        return;
    }
    status_ = "connecting to " + address.toString();
    setScreen(Screen::Connecting);
}

bool App::onConnected() {
    try {
        map_ = std::make_unique<Map>(Map::parse(loadText("data/maps/" + net_.mapName() + ".json")));
    } catch (const std::exception& e) {
        leaveGame(e.what());
        return false;
    }
    if (map_->hash() != net_.mapHash()) {
        leaveGame("map '" + net_.mapName() + "' differs from server");
        return false;
    }
    selected_.clear();
    effects_.clear();
    prediction_.clear();
    cameraPlaced_ = false;
    camera_.target = glm::vec2(map_->width(), map_->height()) * 0.5f;
    status_.clear();
    setScreen(Screen::Game);
    const std::string title = "Void Warzone - " + options_.name + " (P" + std::to_string(net_.player() + 1) + ")";
    SDL_SetWindowTitle(window_, title.c_str());
    return true;
}

void App::leaveGame(std::string status) {
    net_.disconnect();
    if (host_)
        host_->stop();
    host_.reset();
    hostMap_.reset();
    map_.reset();
    entities_.clear();
    selected_.clear();
    prediction_.clear();
    status_ = std::move(status);
    SDL_SetWindowTitle(window_, "Void Warzone");
    setScreen(Screen::Menu);
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        return;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer_.resize(e.window.data1, e.window.data2);
        pixelDensity_ = SDL_GetWindowPixelDensity(window_);
        return;
    case SDL_EVENT_MOUSE_MOTION:
        mouse_ = glm::vec2(e.motion.x, e.motion.y) * pixelDensity_;
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (screen_ == Screen::Address && addressInput_.size() < 64)
            addressInput_ += e.text.text;
        return;
    case SDL_EVENT_KEY_DOWN:
        if (screen_ != Screen::Game) {
            handleMenuKey(e.key.key);
            return;
        }
        break;
    default:
        break;
    }
    if (screen_ == Screen::Game)
        handleGameEvent(e);
}

void App::handleMenuKey(SDL_Keycode key) {
    switch (screen_) {
    case Screen::Menu:
        if (key == SDLK_1 || key == SDLK_H) {
            startHost();
        } else if (key == SDLK_2 || key == SDLK_L) {
            status_.clear();
            setScreen(Screen::Lan);
        } else if (key == SDLK_3 || key == SDLK_C) {
            status_.clear();
            setScreen(Screen::Address);
        } else if (key == SDLK_D) {
            options_.difficulty = static_cast<Difficulty>((static_cast<int>(options_.difficulty) + 1) % 4);
        } else if (key == SDLK_ESCAPE) {
            running_ = false;
        }
        break;
    case Screen::Lan:
        if (key == SDLK_ESCAPE) {
            setScreen(Screen::Menu);
        } else if (key >= SDLK_1 && key <= SDLK_9) {
            const auto index = static_cast<std::size_t>(key - SDLK_1);
            if (index < lan_.servers().size()) {
                const auto address = lan_.servers()[index].address;
                startConnect(address);
            }
        }
        break;
    case Screen::Address:
        if (key == SDLK_ESCAPE) {
            setScreen(Screen::Menu);
        } else if (key == SDLK_BACKSPACE && !addressInput_.empty()) {
            addressInput_.pop_back();
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (const auto addr = net::Address::parse(addressInput_, net::kGamePort))
                startConnect(*addr);
            else
                status_ = "bad address";
        }
        break;
    case Screen::Connecting:
        if (key == SDLK_ESCAPE)
            leaveGame("cancelled");
        break;
    case Screen::Game:
        break;
    }
}

void App::handleGameEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_WHEEL:
        camera_.distance = std::clamp(camera_.distance * (1.0f - e.wheel.y * 0.1f), kMinZoom, kMaxZoom);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const glm::vec2 p = glm::vec2(e.button.x, e.button.y) * pixelDensity_;
        mouse_ = p;
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (attackMoveArmed_) {
                issueCommand(p, true);
                attackMoveArmed_ = false;
            } else {
                dragging_ = true;
                dragStart_ = p;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            attackMoveArmed_ = false;
            issueCommand(p, false);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT && dragging_) {
            dragging_ = false;
            const glm::vec2 p = glm::vec2(e.button.x, e.button.y) * pixelDensity_;
            const bool add = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            if (glm::distance(p, dragStart_) < kDragThreshold)
                clickSelect(p, add);
            else
                boxSelect(dragStart_, p, add);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.key == SDLK_A && !selected_.empty()) {
            attackMoveArmed_ = true;
        } else if (e.key.key == SDLK_S && !selected_.empty()) {
            prediction_.cancel(selected_);
            net_.sendCommand(StopCommand{selectedEntities()});
        } else if (e.key.key == SDLK_ESCAPE) {
            if (attackMoveArmed_)
                attackMoveArmed_ = false;
            else
                selected_.clear();
        } else if (e.key.key == SDLK_SPACE) {
            pruneSelection();
            if (!selected_.empty())
                if (const auto* u = findEntity(selected_.front()))
                    camera_.target = u->pos;
        } else if (e.key.key == SDLK_F10) {
            leaveGame("left the game");
        } else if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
            const auto index = static_cast<std::size_t>(e.key.key - SDLK_1);
            if (index < data_->recruitable().size())
                net_.sendCommand(RecruitCommand{0, data_->recruitable()[index]});
        }
        break;
    default:
        break;
    }
}

const net::InterpolatedEntity* App::findEntity(std::uint32_t id) const {
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const auto& e) { return e.id == id; });
    return it != entities_.end() ? &*it : nullptr;
}

const net::InterpolatedEntity* App::entityAt(glm::vec2 p, float pad) const {
    const net::InterpolatedEntity* best = nullptr;
    float bestDist = pad;
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit)
            continue;
        const float d = glm::distance(e.pos, p) - data_->unit(e.def).radius;
        if (d <= bestDist) {
            best = &e;
            bestDist = d;
        }
    }
    return best;
}

std::vector<entt::entity> App::selectedEntities() const {
    std::vector<entt::entity> out;
    for (const auto id : selected_)
        out.push_back(static_cast<entt::entity>(id));
    return out;
}

void App::pruneSelection() {
    std::erase_if(selected_, [this](std::uint32_t id) {
        const auto* e = findEntity(id);
        return !e || !isOwn(*e);
    });
}

void App::clickSelect(glm::vec2 screen, bool add) {
    if (!add)
        selected_.clear();
    const auto ground = camera_.screenToGround(screen.x, screen.y);
    if (!ground)
        return;
    const auto* e = entityAt(*ground, 0.4f);
    if (!e || !isOwn(*e))
        return;
    if (const auto it = std::find(selected_.begin(), selected_.end(), e->id); it != selected_.end())
        selected_.erase(it);
    else
        selected_.push_back(e->id);
}

void App::boxSelect(glm::vec2 a, glm::vec2 b, bool add) {
    if (!add)
        selected_.clear();
    const glm::vec2 lo = glm::min(a, b);
    const glm::vec2 hi = glm::max(a, b);
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || !isOwn(e))
            continue;
        const glm::vec2 s = camera_.worldToScreen({e.pos.x, 0.5f, e.pos.y});
        if (s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y && std::find(selected_.begin(), selected_.end(), e.id) == selected_.end())
            selected_.push_back(e.id);
    }
}

void App::issueCommand(glm::vec2 screen, bool attackMove) {
    pruneSelection();
    const auto ground = camera_.screenToGround(screen.x, screen.y);
    if (selected_.empty() || !ground)
        return;
    std::uint8_t ownTeam = 0;
    if (const auto* first = findEntity(selected_.front()))
        ownTeam = first->team;

    const auto* target = entityAt(*ground, 0.4f);
    if (target && target->team != ownTeam) {
        prediction_.cancel(selected_);
        net_.sendCommand(AttackCommand{selectedEntities(), static_cast<entt::entity>(target->id)});
        addEffect(Mesh::Cylinder, {ground->x, 0.1f, ground->y}, {1.0f, 0.3f, 0.3f, 0.8f}, 0.4f, 1.4f, 0.35f);
        return;
    }

    const auto seq = net_.sendCommand(MoveCommand{selectedEntities(), *ground, attackMove});
    if (attackMove) {
        prediction_.cancel(selected_);
    } else {
        std::vector<net::InterpolatedEntity> units;
        for (const auto id : selected_)
            if (const auto* e = findEntity(id))
                units.push_back(*e);
        prediction_.onMove(units, *ground, seq, *data_, *map_);
    }
    const glm::vec4 color = attackMove ? glm::vec4{1.0f, 0.6f, 0.2f, 0.8f} : glm::vec4{0.35f, 1.0f, 0.55f, 0.8f};
    addEffect(Mesh::Cylinder, {ground->x, 0.1f, ground->y}, color, 0.4f, 1.4f, 0.35f);
}

void App::updateCamera(float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec2 move{};
    if (keys[SDL_SCANCODE_UP])
        move.y += 1.0f;
    if (keys[SDL_SCANCODE_DOWN])
        move.y -= 1.0f;
    if (keys[SDL_SCANCODE_RIGHT])
        move.x += 1.0f;
    if (keys[SDL_SCANCODE_LEFT])
        move.x -= 1.0f;

    if (SDL_GetMouseFocus() == window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) {
        constexpr float edge = 6.0f;
        if (mouse_.x <= edge)
            move.x -= 1.0f;
        if (mouse_.x >= static_cast<float>(renderer_.width()) - edge)
            move.x += 1.0f;
        if (mouse_.y <= edge)
            move.y += 1.0f;
        if (mouse_.y >= static_cast<float>(renderer_.height()) - edge)
            move.y -= 1.0f;
    }

    camera_.target += move * camera_.distance * 1.2f * dt;
    camera_.target = glm::clamp(camera_.target, glm::vec2(0.0f), glm::vec2(map_->width(), map_->height()));
}

void App::updateGame(float dt) {
    net_.interpolate(now_, entities_);
    prediction_.update(dt, now_, net_, *data_, *map_, entities_);

    if (!cameraPlaced_ && net_.hasSnapshot()) {
        glm::vec2 center{};
        int own = 0;
        for (const auto& e : entities_)
            if (e.kind == net::EntityKind::Unit && isOwn(e)) {
                center += e.pos;
                ++own;
            }
        if (own > 0) {
            camera_.target = center / static_cast<float>(own);
            cameraPlaced_ = true;
        }
    }

    for (const auto& ev : net_.takeEvents())
        handleNetEvent(ev);
    for (const auto& d : net_.takeDeaths())
        addEffect(Mesh::Sphere, {d.pos.x, 0.6f, d.pos.y}, {1.0f, 0.8f, 0.4f, 0.7f}, 1.0f, 3.5f, 0.45f);
    bannerTime_ = std::max(0.0f, bannerTime_ - dt);

    for (auto& fx : effects_)
        fx.t += dt;
    std::erase_if(effects_, [](const Effect& fx) { return fx.t >= fx.life; });

    pruneSelection();
    updateCamera(dt);
}

void App::frame(double dt) {
    now_ = nowSeconds();
    fps_ = fps_ * 0.95f + (dt > 0.0 ? static_cast<float>(1.0 / dt) : 0.0f) * 0.05f;

    if (host_)
        host_->update(now_);
    net_.update(now_);
    if (screen_ == Screen::Lan)
        lan_.update(now_);

    if (screen_ == Screen::Connecting) {
        if (net_.state() == net::ClientState::Connected)
            onConnected();
        else if (net_.state() == net::ClientState::Disconnected)
            leaveGame(net_.error().empty() ? "connection failed" : net_.error());
    } else if (screen_ == Screen::Game && net_.state() != net::ClientState::Connected) {
        leaveGame(net_.error().empty() ? "disconnected" : net_.error());
    }

    if (screen_ == Screen::Game)
        updateGame(static_cast<float>(dt));
    render();
}

glm::vec4 App::unitColor(const net::InterpolatedEntity& e) const {
    if (isOwn(e))
        return kOwnColor;
    if (e.owner != net::kNoOwner)
        return kAllyColor;
    glm::vec4 c = kTeamColors[e.team & 1];
    if (data_->unit(e.def).structure)
        c = glm::vec4(glm::vec3(c) * 0.7f, 1.0f);
    return c;
}

void App::render() {
    camera_.update(renderer_.width(), renderer_.height(), renderer_.homogeneousDepth());
    renderer_.begin(camera_);
    bgfx::dbgTextClear();
    if (screen_ == Screen::Game && map_) {
        renderWorld();
        renderHud();
        renderGameHud();
    } else {
        renderMenu();
    }
    renderer_.end();
}

void App::renderWorld() {
    const auto w = static_cast<float>(map_->width());
    const auto h = static_cast<float>(map_->height());
    renderer_.draw(Mesh::Cube, {w * 0.5f, -0.05f, h * 0.5f}, {w, 0.1f, h}, 0.0f, kGround);

    for (const auto& lane : map_->lanes())
        for (std::size_t i = 0; i + 1 < lane.points.size(); ++i) {
            const glm::vec2 a = lane.points[i];
            const glm::vec2 b = lane.points[i + 1];
            const glm::vec2 mid = (a + b) * 0.5f;
            const glm::vec2 d = b - a;
            renderer_.draw(Mesh::Cube, {mid.x, 0.005f, mid.y}, {glm::length(d) + 3.0f, 0.02f, 3.0f}, std::atan2(d.y, d.x), kLane);
        }

    for (const auto& r : map_->walls()) {
        const glm::vec3 size{static_cast<float>(r.w), 2.0f, static_cast<float>(r.h)};
        renderer_.draw(Mesh::Cube, {static_cast<float>(r.x) + size.x * 0.5f, 1.0f, static_cast<float>(r.y) + size.z * 0.5f}, size, 0.0f, kWall);
    }

    for (const auto& e : entities_) {
        if (e.kind == net::EntityKind::Projectile) {
            const glm::vec4 c = glm::mix(kTeamColors[e.team & 1], glm::vec4(1.0f), 0.4f);
            renderer_.draw(Mesh::Sphere, {e.pos.x, 0.8f, e.pos.y}, glm::vec3(0.3f), 0.0f, c, Unlit);
            continue;
        }
        const auto& def = data_->unit(e.def);
        const auto& vis = visuals_[e.def];
        const float d = def.radius * 2.0f;
        const glm::vec4 color = unitColor(e);
        renderer_.draw(vis.shape, {e.pos.x, vis.height * 0.5f, e.pos.y}, {d, vis.height, d}, e.facing, color);

        if (def.weapon && !def.structure) {
            const glm::vec2 f{std::cos(e.facing), std::sin(e.facing)};
            const glm::vec2 nose = e.pos + f * def.radius * 0.9f;
            renderer_.draw(Mesh::Cube, {nose.x, vis.height * 0.65f, nose.y}, {def.radius, 0.18f, 0.18f}, e.facing,
                           glm::vec4(glm::vec3(color) * 0.6f, 1.0f));
        }

        const bool selected = std::find(selected_.begin(), selected_.end(), e.id) != selected_.end();
        if (selected)
            renderer_.draw(Mesh::Cylinder, {e.pos.x, 0.03f, e.pos.y}, {d + 0.6f, 0.04f, d + 0.6f}, 0.0f, kSelectColor, Unlit | Alpha);

        const float maxHp = def.maxHp;
        if (selected || static_cast<float>(e.hp) < maxHp) {
            const float frac = std::clamp(static_cast<float>(e.hp) / maxHp, 0.0f, 1.0f);
            const float barW = std::max(1.0f, d);
            const float y = vis.height + 0.5f;
            renderer_.draw(Mesh::Cube, {e.pos.x, y, e.pos.y}, {barW, 0.15f, 0.08f}, 0.0f, {0.05f, 0.05f, 0.05f, 1.0f}, Unlit);
            const glm::vec4 hpColor = glm::mix(glm::vec4{1.0f, 0.2f, 0.2f, 1.0f}, glm::vec4{0.3f, 1.0f, 0.4f, 1.0f}, frac);
            renderer_.draw(Mesh::Cube, {e.pos.x - barW * 0.5f + barW * frac * 0.5f, y, e.pos.y - 0.05f}, {barW * frac, 0.17f, 0.1f}, 0.0f,
                           hpColor, Unlit);
        }
    }

    renderResourcePoints();

    for (const auto& fx : effects_) {
        const float k = fx.t / fx.life;
        glm::vec4 c = fx.color;
        c.a *= 1.0f - k * k;
        const float s = fx.from + (fx.to - fx.from) * k;
        if (fx.mesh == Mesh::Cylinder)
            renderer_.draw(Mesh::Cylinder, fx.pos, {s, 0.03f, s}, 0.0f, c, Unlit | Alpha);
        else
            renderer_.draw(fx.mesh, fx.pos, glm::vec3(s), 0.0f, c, Unlit | Alpha);
    }

    if (dragging_ && glm::distance(mouse_, dragStart_) >= kDragThreshold) {
        const glm::vec2 lo = glm::min(mouse_, dragStart_);
        const glm::vec2 hi = glm::max(mouse_, dragStart_);
        renderer_.drawScreenRect(lo, hi, {0.35f, 1.0f, 0.55f, 0.12f});
        const glm::vec4 border{0.35f, 1.0f, 0.55f, 0.8f};
        renderer_.drawScreenRect(lo, {hi.x, lo.y + 1.0f}, border);
        renderer_.drawScreenRect({lo.x, hi.y - 1.0f}, hi, border);
        renderer_.drawScreenRect(lo, {lo.x + 1.0f, hi.y}, border);
        renderer_.drawScreenRect({hi.x - 1.0f, lo.y}, hi, border);
    }
}

void App::renderHud() {
    int teams[2] = {0, 0};
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit)
            ++teams[e.team & 1];
    const auto stats = net_.stats();
    const auto* latest = net_.latest();

    text(1, 1, 0x0b, "Void Warzone %.*s | %s | %.0f fps", static_cast<int>(kVersion.size()), kVersion.data(), renderer_.name(), fps_);
    text(1, 2, 0x07, "%s P%d | tick %u | units %d vs %d | selected %d%s", host_ ? "host" : "client", net_.player() + 1,
                        latest ? latest->tick : 0u, teams[0], teams[1], static_cast<int>(selected_.size()),
                        attackMoveArmed_ ? " | ATTACK-MOVE" : "");
    text(1, 3, 0x07, "rtt %.0f ms | loss %.1f%% | in %.1f out %.1f kbps | interp %.0f ms jitter %.1f ms | snap %d B | predicted %d",
                        stats.rtt, stats.packetLoss, stats.receivedKbps, stats.sentKbps, net_.interpDelay() * 1000.0, net_.jitter() * 1000.0,
                        static_cast<int>(net_.lastSnapshotBytes()), static_cast<int>(prediction_.active()));
    text(1, 4, 0x08, "LMB select | RMB move/attack | A+LMB attack-move | S stop | Space focus | arrows/edges/wheel camera | F10 leave");
}

void App::addEffect(Mesh mesh, glm::vec3 pos, glm::vec4 color, float from, float to, float life) {
    effects_.push_back({mesh, pos, color, from, to, life});
}

void App::showBanner(std::string text, float seconds) {
    banner_ = std::move(text);
    bannerTime_ = seconds;
}

void App::handleNetEvent(const net::NetEvent& ev) {
    const glm::vec3 pos{net::dequantizePos(ev.x), 0.6f, net::dequantizePos(ev.y)};
    const float value = (static_cast<float>(ev.value) - 64.0f) / 64.0f;
    switch (static_cast<net::NetEventType>(ev.type)) {
    case net::NetEventType::Hit:
        addEffect(Mesh::Sphere, pos, {1.0f, 1.0f, 1.0f, 0.8f}, 0.2f, 0.7f, 0.12f);
        break;
    case net::NetEventType::SlamWarn:
        addEffect(Mesh::Cylinder, {pos.x, 0.08f, pos.z}, {1.0f, 0.15f, 0.1f, 0.35f}, 0.5f, value * 2.0f, 1.0f);
        addEffect(Mesh::Cylinder, {pos.x, 0.07f, pos.z}, {1.0f, 0.15f, 0.1f, 0.25f}, value * 2.0f, value * 2.0f, 1.0f);
        break;
    case net::NetEventType::Slam:
        addEffect(Mesh::Cylinder, {pos.x, 0.12f, pos.z}, {1.0f, 0.55f, 0.15f, 0.9f}, value * 1.2f, value * 2.2f, 0.4f);
        addEffect(Mesh::Sphere, pos, {1.0f, 0.6f, 0.2f, 0.6f}, 1.0f, value * 1.2f, 0.3f);
        break;
    case net::NetEventType::Summon:
        addEffect(Mesh::Sphere, pos, {0.7f, 0.2f, 1.0f, 0.6f}, 1.0f, 6.0f, 0.6f);
        break;
    case net::NetEventType::Enrage:
        addEffect(Mesh::Sphere, pos, {1.0f, 0.1f, 0.1f, 0.7f}, 2.0f, 8.0f, 0.8f);
        showBanner("THE BOSS IS ENRAGED", 2.5f);
        break;
    case net::NetEventType::WaveStart: {
        const auto* latest = net_.latest();
        const int total = latest ? latest->game.totalWaves : 0;
        const int wave = static_cast<int>(std::lround(value));
        showBanner(wave == total ? "FINAL WAVE" : "WAVE " + std::to_string(wave), 3.0f);
        break;
    }
    case net::NetEventType::PointCaptured: {
        const int owner = static_cast<int>(std::lround(value));
        const glm::vec4 c = owner == 0 ? kTeamColors[0] : owner == 1 ? kTeamColors[1] : glm::vec4{0.7f, 0.7f, 0.7f, 1.0f};
        addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.z}, {c.r, c.g, c.b, 0.8f}, 2.0f, 12.0f, 0.7f);
        break;
    }
    case net::NetEventType::MatchEnd:
        break;
    }
}

void App::renderResourcePoints() {
    const auto* latest = net_.latest();
    const auto& defs = map_->resourcePoints();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const auto& def = defs[i];
        const net::PointInfo info = latest && i < latest->game.points.size() ? latest->game.points[i] : net::PointInfo{};
        const float control = static_cast<float>(info.control) / 127.0f;
        const glm::vec4 neutral{0.55f, 0.55f, 0.6f, 1.0f};
        const glm::vec4 ownerColor = info.owner == 0 ? kTeamColors[0] : info.owner == 1 ? kTeamColors[1] : neutral;
        const float d = def.radius * 2.0f;
        renderer_.draw(Mesh::Cylinder, {def.pos.x, 0.02f, def.pos.y}, {d, 0.03f, d}, 0.0f, glm::vec4(glm::vec3(ownerColor) * 0.35f, 1.0f));
        const glm::vec4 progressColor = control >= 0.0f ? kTeamColors[0] : kTeamColors[1];
        const float p = std::abs(control) * d;
        if (p > 0.05f)
            renderer_.draw(Mesh::Cylinder, {def.pos.x, 0.04f, def.pos.y}, {p, 0.03f, p}, 0.0f, glm::vec4(glm::vec3(progressColor), 0.55f), Unlit | Alpha);
        renderer_.draw(Mesh::Cylinder, {def.pos.x, 1.2f, def.pos.y}, {0.35f, 2.4f, 0.35f}, 0.0f, ownerColor);
        renderer_.draw(Mesh::Sphere, {def.pos.x, 2.6f, def.pos.y}, glm::vec3(0.7f), 0.0f, ownerColor, Unlit);
    }
}

void App::renderGameHud() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    const auto& game = latest->game;
    const int cols = renderer_.width() / 8;
    const int rows = renderer_.height() / 16;

    const char* difficulty = difficultyName(static_cast<Difficulty>(std::min<int>(game.difficulty, 3)));
    if (game.wave < game.totalWaves)
        text(1, 6, 0x0e, "Wave %d/%d | next in %.0fs | %s", game.wave, game.totalWaves, game.nextWaveIn / 10.0, difficulty);
    else
        text(1, 6, 0x0c, "Wave %d/%d | final wave - destroy the portal or clear the void | %s", game.wave, game.totalWaves, difficulty);

    float income = map_->defense().baseIncome;
    std::string points;
    for (std::size_t i = 0; i < game.points.size() && i < map_->resourcePoints().size(); ++i) {
        const auto& def = map_->resourcePoints()[i];
        const char mark = game.points[i].owner == 0 ? '+' : game.points[i].owner == 1 ? 'x' : '-';
        points += def.name + mark + " ";
        if (game.points[i].owner == 0)
            income += def.income;
    }
    text(1, 7, 0x0a, "Energy %u | team income %.0f/s | points %s", latest->energy, income, points.c_str());

    std::string recruit;
    for (std::size_t i = 0; i < data_->recruitable().size() && i < 9; ++i) {
        const auto& def = data_->unit(data_->recruitable()[i]);
        recruit += "[" + std::to_string(i + 1) + "] " + def.name + " " + std::to_string(static_cast<int>(def.cost)) + "  ";
    }
    text(1, 8, 0x07, "Recruit: %s", recruit.c_str());

    if (game.boss != net::kNoEntity)
        if (const auto* boss = findEntity(game.boss)) {
            const auto& def = data_->unit(boss->def);
            const float frac = std::clamp(static_cast<float>(boss->hp) / def.maxHp, 0.0f, 1.0f);
            constexpr int barWidth = 50;
            std::string bar(barWidth, '-');
            for (int i = 0; i < static_cast<int>(frac * barWidth); ++i)
                bar[static_cast<std::size_t>(i)] = '#';
            const std::string title = def.boss ? def.boss->title : def.name;
            const int x = std::max(0, (cols - barWidth - 2) / 2);
            text(std::max(0, (cols - static_cast<int>(title.size())) / 2), 2, 0x0c, "%s", title.c_str());
            text(x, 3, 0x0c, "[%s]", bar.c_str());
        }

    const auto state = static_cast<MatchState>(game.state);
    if (state != MatchState::Playing) {
        const char* msg = state == MatchState::Victory ? "VICTORY - THE VOID IS SEALED" : "DEFEAT - THE CORE HAS FALLEN";
        text(std::max(0, (cols - static_cast<int>(std::strlen(msg))) / 2), rows / 2 - 1, state == MatchState::Victory ? 0x0a : 0x0c, "%s", msg);
        const char* hint = "F10 - back to menu";
        text(std::max(0, (cols - static_cast<int>(std::strlen(hint))) / 2), rows / 2 + 1, 0x07, "%s", hint);
    } else if (bannerTime_ > 0.0f && !banner_.empty()) {
        text(std::max(0, (cols - static_cast<int>(banner_.size())) / 2), rows / 3, 0x0e, "%s", banner_.c_str());
    }
}

void App::renderMenu() {
    text(2, 2, 0x0b, "VOID WARZONE %.*s", static_cast<int>(kVersion.size()), kVersion.data());
    int row = 5;
    switch (screen_) {
    case Screen::Menu:
        text(2, row++, 0x0f, "[1] Host game (map %s, port %u)", options_.map.c_str(), static_cast<unsigned>(options_.port));
        text(2, row++, 0x0f, "[D] Difficulty: %s", difficultyName(options_.difficulty));
        text(2, row++, 0x0f, "[2] Find LAN games");
        text(2, row++, 0x0f, "[3] Connect to address");
        text(2, row++, 0x0f, "[Esc] Quit");
        break;
    case Screen::Lan: {
        text(2, row++, 0x0f, "LAN games (searching...)");
        ++row;
        int i = 1;
        for (const auto& s : lan_.servers())
            text(2, row++, 0x0f, "[%d] %s | %s | %d/%d | %s", i++, s.name.c_str(), s.map.c_str(), s.players, s.maxPlayers,
                                s.address.toString().c_str());
        if (lan_.servers().empty())
            text(2, row++, 0x08, "no games found");
        ++row;
        text(2, row++, 0x08, "[Esc] Back");
        break;
    }
    case Screen::Address:
        text(2, row++, 0x0f, "Address (host[:port]), Enter to connect, Esc to go back:");
        text(2, row++, 0x0e, "> %s_", addressInput_.c_str());
        break;
    case Screen::Connecting:
        text(2, row++, 0x0f, "%s ...", status_.c_str());
        text(2, row++, 0x08, "[Esc] Cancel");
        break;
    case Screen::Game:
        break;
    }
    if (!status_.empty() && screen_ != Screen::Connecting)
        text(2, row + 1, 0x0c, "%s", status_.c_str());
    text(2, row + 3, 0x08, "player name: %s", options_.name.c_str());
}

}
