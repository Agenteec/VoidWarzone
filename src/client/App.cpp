#include "App.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "core/Version.h"

namespace vw::client {

namespace {

constexpr glm::vec4 kGround{0.06f, 0.06f, 0.10f, 1.0f};
constexpr glm::vec4 kLane{0.10f, 0.10f, 0.17f, 1.0f};
constexpr glm::vec4 kWall{0.22f, 0.20f, 0.32f, 1.0f};
constexpr glm::vec4 kTeamColors[] = {{0.25f, 0.75f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.55f, 1.0f}};
constexpr glm::vec4 kOwnColor{0.35f, 1.0f, 0.55f, 1.0f};
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

}

bool App::init(SDL_Window* window) {
    window_ = window;
    pixelDensity_ = SDL_GetWindowPixelDensity(window);
    if (!renderer_.init(window))
        return false;
    initialized_ = true;

    try {
        loadContent();
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return false;
    }

    glm::vec2 center{};
    int own = 0;
    for (auto [e, pos, owner] : world_->registry().view<const Position, const Owner>().each())
        if (owner.player == localPlayer_) {
            center += pos.value;
            ++own;
        }
    camera_.target = own ? center / static_cast<float>(own) : glm::vec2(map_->width(), map_->height()) * 0.5f;

    spdlog::info("Void Warzone {} | {} | map {}", kVersion, renderer_.name(), map_->name());
    return true;
}

void App::loadContent() {
    const std::string unitsText = loadText("data/units.json");
    data_ = std::make_unique<GameData>(GameData::parse(unitsText));
    map_ = std::make_unique<Map>(Map::parse(loadText("data/maps/siege_test.json")));
    world_ = std::make_unique<World>(*data_, *map_);

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
    world_.reset();
    if (initialized_)
        renderer_.shutdown();
}

bool App::isOwn(entt::entity e) const {
    const auto* owner = world_->registry().try_get<Owner>(e);
    return owner && owner->player == localPlayer_;
}

void App::pruneSelection() {
    std::erase_if(selected_, [this](entt::entity e) { return !world_->alive(e); });
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer_.resize(e.window.data1, e.window.data2);
        pixelDensity_ = SDL_GetWindowPixelDensity(window_);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouse_ = glm::vec2(e.motion.x, e.motion.y) * pixelDensity_;
        break;
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
        } else if (e.key.key == SDLK_S) {
            world_->apply(StopCommand{selected_});
        } else if (e.key.key == SDLK_ESCAPE) {
            if (attackMoveArmed_)
                attackMoveArmed_ = false;
            else
                selected_.clear();
        } else if (e.key.key == SDLK_SPACE && !selected_.empty()) {
            pruneSelection();
            if (!selected_.empty())
                camera_.target = world_->registry().get<Position>(selected_.front()).value;
        }
        break;
    default:
        break;
    }
}

void App::clickSelect(glm::vec2 screen, bool add) {
    if (!add)
        selected_.clear();
    const auto ground = camera_.screenToGround(screen.x, screen.y);
    if (!ground)
        return;
    const auto e = world_->unitAt(*ground, 0.4f);
    if (e == entt::null || !isOwn(e))
        return;
    if (const auto it = std::find(selected_.begin(), selected_.end(), e); it != selected_.end())
        selected_.erase(it);
    else
        selected_.push_back(e);
}

void App::boxSelect(glm::vec2 a, glm::vec2 b, bool add) {
    if (!add)
        selected_.clear();
    const glm::vec2 lo = glm::min(a, b);
    const glm::vec2 hi = glm::max(a, b);
    for (auto [e, pos, owner] : world_->registry().view<const Position, const Owner>().each()) {
        if (owner.player != localPlayer_)
            continue;
        const glm::vec2 s = camera_.worldToScreen({pos.value.x, 0.5f, pos.value.y});
        if (s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y && std::find(selected_.begin(), selected_.end(), e) == selected_.end())
            selected_.push_back(e);
    }
}

void App::issueCommand(glm::vec2 screen, bool attackMove) {
    pruneSelection();
    const auto ground = camera_.screenToGround(screen.x, screen.y);
    if (selected_.empty() || !ground)
        return;
    const auto target = world_->unitAt(*ground, 0.4f);
    const bool enemy = target != entt::null && world_->registry().get<Team>(target).id != localTeam_;
    if (enemy) {
        world_->apply(AttackCommand{selected_, target});
        effects_.push_back({{ground->x, 0.1f, ground->y}, {1.0f, 0.3f, 0.3f, 0.8f}, 1.0f, 0.0f, 0.35f});
    } else {
        world_->apply(MoveCommand{selected_, *ground, attackMove});
        const glm::vec4 color = attackMove ? glm::vec4{1.0f, 0.6f, 0.2f, 0.8f} : glm::vec4{0.35f, 1.0f, 0.55f, 0.8f};
        effects_.push_back({{ground->x, 0.1f, ground->y}, color, 1.0f, 0.0f, 0.35f});
    }
}

void App::step() {
    world_->step();
    const auto& reg = world_->registry();
    for (const auto& ev : world_->events()) {
        if (ev.type == EventType::Death) {
            effects_.push_back({{ev.from.x, 0.6f, ev.from.y}, {1.0f, 0.8f, 0.4f, 0.7f}, 2.5f, 0.0f, 0.45f});
        } else if (ev.type == EventType::Shot && reg.valid(ev.entity)) {
            const auto* w = reg.try_get<Weapon>(ev.entity);
            if (w && w->def.projectileSpeed <= 0.0f)
                effects_.push_back({{ev.to.x, 0.6f, ev.to.y}, {1.0f, 1.0f, 1.0f, 0.8f}, 0.5f, 0.0f, 0.12f});
        }
    }
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

void App::frame(double dt) {
    fps_ = fps_ * 0.95f + (dt > 0.0 ? static_cast<float>(1.0 / dt) : 0.0f) * 0.05f;
    accumulator_ = std::min(accumulator_ + dt, 0.25);
    while (accumulator_ >= kTickDt) {
        step();
        accumulator_ -= kTickDt;
    }

    const auto fdt = static_cast<float>(dt);
    for (auto& fx : effects_)
        fx.t += fdt;
    std::erase_if(effects_, [](const Effect& fx) { return fx.t >= fx.life; });

    updateCamera(fdt);
    render(static_cast<float>(accumulator_ / kTickDt));
}

glm::vec4 App::unitColor(entt::entity e) const {
    if (isOwn(e))
        return kOwnColor;
    const auto team = world_->registry().get<Team>(e).id;
    glm::vec4 c = kTeamColors[team & 1];
    if (world_->registry().all_of<Structure>(e))
        c = glm::vec4(glm::vec3(c) * 0.7f, 1.0f);
    return c;
}

void App::render(float alpha) {
    camera_.update(renderer_.width(), renderer_.height(), renderer_.homogeneousDepth());
    renderer_.begin(camera_);

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

    pruneSelection();
    const auto& reg = world_->registry();
    for (auto [e, pos, prev, unit, facing, health] :
         reg.view<const Position, const PrevPosition, const Unit, const Facing, const Health>().each()) {
        const glm::vec2 p = glm::mix(prev.value, pos.value, alpha);
        const auto& vis = visuals_[unit.def];
        const float d = unit.radius * 2.0f;
        const glm::vec4 color = unitColor(e);
        renderer_.draw(vis.shape, {p.x, vis.height * 0.5f, p.y}, {d, vis.height, d}, facing.angle, color);

        if (reg.all_of<Weapon>(e) && !reg.all_of<Structure>(e)) {
            const glm::vec2 f{std::cos(facing.angle), std::sin(facing.angle)};
            const glm::vec2 nose = p + f * unit.radius * 0.9f;
            renderer_.draw(Mesh::Cube, {nose.x, vis.height * 0.65f, nose.y}, {unit.radius, 0.18f, 0.18f}, facing.angle,
                           glm::vec4(glm::vec3(color) * 0.6f, 1.0f));
        }

        const bool selected = std::find(selected_.begin(), selected_.end(), e) != selected_.end();
        if (selected)
            renderer_.draw(Mesh::Cylinder, {p.x, 0.03f, p.y}, {d + 0.6f, 0.04f, d + 0.6f}, 0.0f, kSelectColor, Unlit | Alpha);

        if (selected || health.hp < health.max) {
            const float frac = std::clamp(health.hp / health.max, 0.0f, 1.0f);
            const float barW = std::max(1.0f, d);
            const float y = vis.height + 0.5f;
            renderer_.draw(Mesh::Cube, {p.x, y, p.y}, {barW, 0.15f, 0.08f}, 0.0f, {0.05f, 0.05f, 0.05f, 1.0f}, Unlit);
            const glm::vec4 hpColor = glm::mix(glm::vec4{1.0f, 0.2f, 0.2f, 1.0f}, glm::vec4{0.3f, 1.0f, 0.4f, 1.0f}, frac);
            renderer_.draw(Mesh::Cube, {p.x - barW * 0.5f + barW * frac * 0.5f, y, p.y - 0.05f}, {barW * frac, 0.17f, 0.1f}, 0.0f, hpColor, Unlit);
        }
    }

    for (auto [e, pos, prev, proj] : reg.view<const Position, const PrevPosition, const Projectile>().each()) {
        const glm::vec2 p = glm::mix(prev.value, pos.value, alpha);
        glm::vec4 c = kTeamColors[proj.team & 1];
        c = glm::mix(c, glm::vec4(1.0f), 0.4f);
        renderer_.draw(Mesh::Sphere, {p.x, 0.8f, p.y}, glm::vec3(0.3f), 0.0f, c, Unlit);
    }

    for (const auto& fx : effects_) {
        const float k = fx.t / fx.life;
        glm::vec4 c = fx.color;
        c.a *= 1.0f - k;
        const float s = fx.size * (0.4f + k);
        if (fx.pos.y < 0.2f)
            renderer_.draw(Mesh::Cylinder, fx.pos, {s, 0.03f, s}, 0.0f, c, Unlit | Alpha);
        else
            renderer_.draw(Mesh::Sphere, fx.pos, glm::vec3(s), 0.0f, c, Unlit | Alpha);
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

    renderHud();
    renderer_.end();
}

void App::renderHud() {
    bgfx::dbgTextClear();
    int teams[2] = {0, 0};
    for (auto [e, team] : world_->registry().view<const Team>().each())
        ++teams[team.id & 1];
    bgfx::dbgTextPrintf(1, 1, 0x0b, "Void Warzone %.*s | %s | %.0f fps", static_cast<int>(kVersion.size()), kVersion.data(), renderer_.name(), fps_);
    bgfx::dbgTextPrintf(1, 2, 0x07, "tick %llu | units %d vs %d | selected %d%s", static_cast<unsigned long long>(world_->tick()), teams[0],
                        teams[1], static_cast<int>(selected_.size()), attackMoveArmed_ ? " | ATTACK-MOVE" : "");
    bgfx::dbgTextPrintf(1, 3, 0x08, "LMB select | RMB move/attack | A+LMB attack-move | S stop | Space focus | arrows/edges/wheel camera");
}

}
