#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <bx/math.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "core/World.h"

namespace vw::client {

namespace {

constexpr const char* kShadowNames[] = {"off", "low", "medium", "high"};
constexpr int kShadowSizes[] = {0, 1024, 2048, 4096};
constexpr const char* kLightNames[] = {"day", "dusk", "night"};
constexpr glm::vec4 kTeamColors[] = {{0.25f, 0.75f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.55f, 1.0f}};
constexpr glm::vec4 kOwnColor{0.35f, 1.0f, 0.55f, 1.0f};
constexpr glm::vec4 kAllyColor{0.95f, 0.85f, 0.3f, 1.0f};
constexpr glm::vec4 kSelectColor{0.35f, 1.0f, 0.55f, 0.55f};
constexpr float kMinZoom = 12.0f;
constexpr float kMaxZoom = 90.0f;
constexpr float kDragThreshold = 5.0f;
constexpr bgfx::ViewId kUiView = 3;

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

double nowSeconds() {
    return static_cast<double>(SDL_GetTicksNS()) * 1e-9;
}

}

const char* shadowQualityName(int quality) {
    return kShadowNames[std::clamp(quality, 0, 3)];
}

const char* lightPresetName(int preset) {
    return kLightNames[std::clamp(preset, 0, 2)];
}

int parseShadowQuality(std::string_view text) {
    for (int i = 0; i < 4; ++i)
        if (text == kShadowNames[i])
            return i;
    return -1;
}

int parseLightPreset(std::string_view text) {
    for (int i = 0; i < 3; ++i)
        if (text == kLightNames[i])
            return i;
    return -1;
}

std::string settingsPath() {
    char* dir = SDL_GetPrefPath("Agenteec", "VoidWarzone");
    if (!dir)
        return {};
    std::string path = std::string(dir) + "settings.json";
    SDL_free(dir);
    return path;
}

void App::loadSettings() {
#if defined(SDL_PLATFORM_ANDROID)
    shadowQuality_ = 1;
    touchUi_ = true;
#endif
    const std::string path = settingsPath();
    std::size_t size = 0;
    if (void* buf = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &size)) {
        const std::string text(static_cast<const char*>(buf), size);
        SDL_free(buf);
        const auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_object()) {
            shadowQuality_ = std::clamp(j.value("shadows", shadowQuality_), 0, 3);
            lightPreset_ = std::clamp(j.value("light", lightPreset_), 0, 2);
            showStats_ = j.value("stats", showStats_);
            if (options_.name == LaunchOptions{}.name)
                options_.name = j.value("name", options_.name);
            if (options_.master == LaunchOptions{}.master)
                options_.master = j.value("master", options_.master);
            if (options_.difficulty == LaunchOptions{}.difficulty)
                options_.difficulty = static_cast<Difficulty>(std::clamp(j.value("difficulty", 1), 0, 3));
            if (options_.map == LaunchOptions{}.map)
                options_.map = j.value("map", options_.map);
            if (options_.mode == LaunchOptions{}.mode)
                options_.mode = static_cast<GameMode>(std::clamp(j.value("mode", 0), 0, 2));
            windowMode_ = std::clamp(j.value("window", windowMode_), 0, 2);
            resWidth_ = j.value("resWidth", resWidth_);
            resHeight_ = j.value("resHeight", resHeight_);
            vsync_ = j.value("vsync", vsync_);
            msaa_ = j.value("msaa", msaa_);
            uiScaleUser_ = std::clamp(j.value("uiScale", uiScaleUser_), 0.6f, 2.0f);
            renderScale_ = std::clamp(j.value("renderScale", renderScale_), 25, 100);
            layout_ = std::clamp(j.value("layout", layout_), 0, 2);
            preferredColor_ = std::clamp(j.value("color", preferredColor_), -1, 7);
            upnp_ = j.value("upnp", upnp_);
            fpsLimit_ = std::clamp(j.value("fpsLimit", fpsLimit_), 0, 1000);
            limitBackground_ = j.value("limitBackground", limitBackground_);
        }
    }
    if (options_.shadows >= 0)
        shadowQuality_ = options_.shadows;
    if (options_.light >= 0)
        lightPreset_ = options_.light;
    if (options_.windowMode >= 0)
        windowMode_ = options_.windowMode;
    std::snprintf(nameBuf_.data(), nameBuf_.size(), "%s", options_.name.c_str());
    std::snprintf(masterBuf_.data(), masterBuf_.size(), "%s", options_.master.c_str());
}

void App::saveSettings() const {
    const std::string path = settingsPath();
    if (path.empty())
        return;
    const std::string text = nlohmann::json{{"shadows", shadowQuality_},
                                            {"light", lightPreset_},
                                            {"stats", showStats_},
                                            {"name", options_.name},
                                            {"master", options_.master},
                                            {"difficulty", static_cast<int>(options_.difficulty)},
                                            {"map", options_.map},
                                            {"mode", static_cast<int>(options_.mode)},
                                            {"window", windowMode_},
                                            {"resWidth", resWidth_},
                                            {"resHeight", resHeight_},
                                            {"vsync", vsync_},
                                            {"msaa", msaa_},
                                            {"uiScale", uiScaleUser_},
                                            {"renderScale", renderScale_},
                                            {"layout", layout_},
                                            {"color", preferredColor_},
                                            {"upnp", upnp_},
                                            {"fpsLimit", fpsLimit_},
                                            {"limitBackground", limitBackground_}}
                                 .dump(2);
    SDL_SaveFile(path.c_str(), text.data(), text.size());
}

void App::applyGraphics() {
    Lighting l;
    switch (lightPreset_) {
    case 1:
        l.sunDir = {-0.8f, 0.42f, 0.3f};
        l.sun = {1.0f, 0.62f, 0.36f};
        l.sky = {0.34f, 0.3f, 0.42f};
        l.clear = 0x140c10ff;
        break;
    case 2:
        l.sunDir = {0.35f, 0.75f, 0.45f};
        l.sun = {0.32f, 0.42f, 0.68f};
        l.sky = {0.14f, 0.16f, 0.27f};
        l.clear = 0x04050aff;
        break;
    default:
        break;
    }
    renderer_.setLighting(l);
    renderer_.setShadowSize(kShadowSizes[std::clamp(shadowQuality_, 0, 3)]);
}

void App::applyVideo() {
    renderer_.setVideo(vsync_, msaa_);
#if defined(SDL_PLATFORM_ANDROID)
    renderer_.setScale(100.0f / static_cast<float>(std::clamp(renderScale_, 25, 100)));
    updateInputScale();
#endif
}

void App::applyWindow() {
#if !defined(SDL_PLATFORM_ANDROID)
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (windowMode_ == 0) {
        SDL_SetWindowFullscreen(window_, false);
        if (resWidth_ > 0 && resHeight_ > 0) {
            SDL_SetWindowSize(window_, resWidth_, resHeight_);
            SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        }
    } else if (windowMode_ == 1) {
        SDL_SetWindowFullscreenMode(window_, nullptr);
        SDL_SetWindowFullscreen(window_, true);
    } else {
        SDL_DisplayMode mode{};
        const int w = resWidth_ > 0 ? resWidth_ : 1920;
        const int h = resHeight_ > 0 ? resHeight_ : 1080;
        if (SDL_GetClosestFullscreenDisplayMode(display, w, h, 0.0f, false, &mode))
            SDL_SetWindowFullscreenMode(window_, &mode);
        SDL_SetWindowFullscreen(window_, true);
    }
    SDL_SyncWindow(window_);
#endif
}

bool App::touchLayout() const {
    return layout_ == 2 || (layout_ == 0 && touchUi_);
}

void App::cycleShadows() {
    shadowQuality_ = (shadowQuality_ + 1) % 4;
    applyGraphics();
    saveSettings();
}

void App::cycleLight() {
    lightPreset_ = (lightPreset_ + 1) % 3;
    applyGraphics();
    saveSettings();
}

bool App::init(SDL_Window* window, LaunchOptions options) {
    window_ = window;
    options_ = std::move(options);
    now_ = nowSeconds();
    const double start = now_;
    touchUi_ = options_.touch;
    loadSettings();
    float scale = options_.uiScale;
#if defined(SDL_PLATFORM_ANDROID)
    touchUi_ = true;
    if (scale <= 0.0f)
        scale = 100.0f / static_cast<float>(renderScale_);
#endif
    if (!netLibrary_.ok() || !renderer_.init(window, std::max(1.0f, scale), options_.renderer))
        return false;
    initialized_ = true;
    applyWindow();
    applyVideo();
    updateInputScale();
    applyGraphics();
    if (!ui_.init(window_, loadText))
        return false;

    try {
        loadContent();
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return false;
    }
    now_ = nowSeconds();
    spdlog::info("Void Warzone {} | {} | content loaded in {:.2f}s", kVersion, renderer_.name(), now_ - start);

    startUpnp();
    if (options_.host) {
        autoStart_ = true;
        startHost(true);
    } else if (!options_.join.empty()) {
        autoStart_ = true;
        startJoin(options_.join);
    } else if (!options_.connect.empty()) {
        if (const auto addr = net::Address::parse(options_.connect, net::kGamePort))
            startConnect(*addr);
        else
            status_ = "bad address: " + options_.connect;
    }
    return true;
}

void App::startUpnp() {
    lobby_.setPortMapper(&mapper_);
    if (!upnp_)
        return;
    std::vector<std::uint16_t> ports;
    for (std::uint16_t i = 0; i < net::kIcePortCount; ++i)
        ports.push_back(static_cast<std::uint16_t>(net::kIcePortBase + i));
    ports.push_back(options_.port);
    mapper_.start(std::move(ports));
}

double App::frameBudget() const {
    int limit = fpsLimit_;
    if (limitBackground_ && window_ && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS))
        limit = limit > 0 ? std::min(limit, 30) : 30;
    return limit > 0 ? 1.0 / static_cast<double>(limit) : 0.0;
}

void App::loadContent() {
    const std::string unitsText = loadText("data/units.json");
    data_ = std::make_unique<GameData>(GameData::parse(unitsText, loadText("data/skills.json"), loadText("data/items.json")));

    models_ = std::make_unique<ModelLibrary>(renderer_, loadText);
    visuals_ = loadUnitVisuals(unitsText, *data_, *models_);
    const auto catalog = nlohmann::json::parse(loadText("data/maps/maps.json"));
    for (const char* key : {"maps", "modes"}) {
        auto& list = std::string_view(key) == "maps" ? maps_ : modes_;
        list.clear();
        for (const auto& m : catalog.at(key))
            list.push_back({m.at("id").get<std::string>(), m.value("name", ""), m.value("players", ""), m.value("description", "")});
    }
}

void App::shutdown() {
    net_.disconnect();
    if (host_)
        host_->stop();
    host_.reset();
    mapper_.stop();
    ui_.shutdown();
    if (initialized_)
        renderer_.shutdown();
}

void App::setScreen(Screen screen) {
    if (screen_ == Screen::Lan && screen != Screen::Lan)
        lan_.stop();
    if (screen == Screen::Lan)
        lan_.start();
    pauseOpen_ = false;
    if (screen == Screen::Online) {
        lobby_.browse(options_.master, now_);
        lobbyRefresh_ = now_;
    }
    screen_ = screen;
}

void App::startJoin(const std::string& code) {
    status_.clear();
    if (!lobby_.join(options_.master, code, now_)) {
        status_ = lobby_.error();
        spdlog::warn("join failed: {}", status_);
        return;
    }
    status_ = "joining " + code;
    setScreen(Screen::Joining);
}

void App::updateLobby() {
    lobby_.update(now_);
    if (screen_ == Screen::Joining) {
        if (lobby_.state() == net::MasterClient::State::Ready) {
            spdlog::info("ice connected via {} | {}", lobby_.agent()->selectedPath(), lobby_.agent()->selectedPair());
            if (net_.connectVia(lobby_.agent(), data_->hash(), options_.name, now_)) {
                status_ = "connecting (" + net_.path() + ")";
                setScreen(Screen::Connecting);
            } else {
                status_ = net_.error();
                lobby_.close();
                setScreen(Screen::Menu);
            }
        } else if (lobby_.state() == net::MasterClient::State::Failed) {
            spdlog::warn("join failed: {}", lobby_.error());
            status_ = lobby_.error();
            setScreen(Screen::Menu);
        }
    }
    if (screen_ == Screen::Online && lobby_.state() != net::MasterClient::State::Connecting && now_ - lobbyRefresh_ > 3.0) {
        lobby_.browse(options_.master, now_);
        lobbyRefresh_ = now_;
    }
    if (host_)
        lobby_.setPlayers(host_->playerCount());
}

void App::startHost(bool multiplayer) {
    simMs_ = 0.0;
    simTotal_ = 0.0;
    simSteps_ = 0;
    try {
        hostMap_ = std::make_unique<Map>(Map::parse(loadText("data/maps/" + options_.map + ".json")));
    } catch (const std::exception& e) {
        status_ = e.what();
        return;
    }
    server::ServerConfig config;
    config.port = multiplayer ? options_.port : 0;
    config.mapName = options_.map;
    config.name = options_.name + "'s game";
    config.difficulty = options_.difficulty;
    config.mode = options_.mode;
    config.cheats = options_.cheats;
    config.lobby = true;
    config.maxPlayers = multiplayer ? 8 : 1;
    config.loadMap = [](const std::string& name) {
        if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            throw std::runtime_error("bad map name");
        return loadText("data/maps/" + name + ".json");
    };
    host_ = std::make_unique<server::Server>(*data_, *hostMap_, config);
    if (!host_->start(now_)) {
        host_.reset();
        status_ = "cannot host: port " + std::to_string(options_.port) + " is busy";
        return;
    }
    if (multiplayer)
        lobby_.host(options_.master, net::RoomInfo{"", config.name, options_.map, 0, static_cast<std::uint8_t>(config.maxPlayers), true},
                    host_->iceHub(), now_);
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
    status_.clear();
    if (!host_)
        lobby_.close();
    chatLog_.clear();
    setupSent_ = false;
    if (net_.inLobby()) {
        setScreen(Screen::Lobby);
        return true;
    }
    enterGame();
    return screen_ == Screen::Game;
}

void App::enterGame() {
    try {
        map_ = std::make_unique<Map>(Map::parse(loadText("data/maps/" + net_.mapName() + ".json")));
    } catch (const std::exception& e) {
        leaveGame(e.what());
        return;
    }
    if (map_->hash() != net_.mapHash()) {
        leaveGame("map '" + net_.mapName() + "' differs from server");
        return;
    }
    if (mapVisualsHash_ != map_->hash()) {
        mapVisuals_ = buildMapVisuals(*map_, *data_, renderer_, *models_);
        mapVisualsHash_ = map_->hash();
    }
    buildMinimap();
    selected_.clear();
    effects_.clear();
    shells_.clear();
    prediction_.clear();
    panel_ = Panel::None;
    pauseOpen_ = false;
    bannerTime_ = 0.0f;
    cameraPlaced_ = false;
    camera_.target = glm::vec2(map_->width(), map_->height()) * 0.5f;
    setScreen(Screen::Game);
    const std::string title = "Void Warzone - " + options_.name + " (P" + std::to_string(net_.player() + 1) + ")";
    SDL_SetWindowTitle(window_, title.c_str());
}

void App::returnToLobby() {
    selected_.clear();
    effects_.clear();
    shells_.clear();
    entities_.clear();
    prediction_.clear();
    panel_ = Panel::None;
    orderArm_ = Arm::None;
    armedSkill_ = -1;
    buildArmed_ = -1;
    for (auto& g : groups_)
        g.clear();
    setScreen(Screen::Lobby);
}

void App::buildMinimap() {
    if (minimapHash_ == map_->hash())
        return;
    const int w = map_->width();
    const int h = map_->height();
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w * h));
    auto nearLane = [&](glm::vec2 p) {
        for (const auto& lane : map_->lanes())
            for (std::size_t k = 0; k + 1 < lane.points.size(); ++k) {
                const glm::vec2 a = lane.points[k];
                const glm::vec2 ab = lane.points[k + 1] - a;
                const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
                if (glm::distance(p, a + ab * t) < 3.0f)
                    return true;
            }
        return false;
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const glm::vec2 p = Map::center({x, y});
            std::uint32_t c = 0xff4f7ca0u;
            if (map_->blocked(x, y))
                c = ((x * 7 + y * 13) % 5 == 0) ? 0xff2e4537u : 0xff283a30u;
            else if (nearLane(p))
                c = 0xff6e7880u;
            pixels[static_cast<std::size_t>(y * w + x)] = c;
        }
    minimap_ = renderer_.createTextureRgba(w, h, pixels.data());
    minimapHash_ = map_->hash();
}

void App::leaveGame(std::string status) {
    net_.disconnect();
    lobby_.close();
    if (host_)
        host_->stop();
    host_.reset();
    hostMap_.reset();
    map_.reset();
    entities_.clear();
    selected_.clear();
    prediction_.clear();
    status_ = std::move(status);
    if (!status_.empty())
        spdlog::warn("{}", status_);
    SDL_SetWindowTitle(window_, "Void Warzone");
    setScreen(Screen::Menu);
}

void App::updateInputScale() {
    pixelDensity_ = SDL_GetWindowPixelDensity(window_) / renderer_.scale();
}

void App::handleEvent(const SDL_Event& e) {
    if (initialized_)
        ui_.processEvent(e, pixelDensity_, renderer_.width(), renderer_.height());
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        return;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        paused_ = true;
        return;
    case SDL_EVENT_DID_ENTER_FOREGROUND: {
        paused_ = false;
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        renderer_.resize(w, h);
        return;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer_.resize(e.window.data1, e.window.data2);
        updateInputScale();
        return;
    case SDL_EVENT_FINGER_DOWN: {
        touchUi_ = true;
        const glm::vec2 p{e.tfinger.x * static_cast<float>(renderer_.width()), e.tfinger.y * static_cast<float>(renderer_.height())};
        if (screen_ != Screen::Game || overUi(p) || pauseOpen_) {
            uiFingers_.insert(e.tfinger.fingerID);
            return;
        }
        handleFinger(e);
        return;
    }
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
        if (uiFingers_.contains(e.tfinger.fingerID)) {
            if (e.type == SDL_EVENT_FINGER_UP)
                uiFingers_.erase(e.tfinger.fingerID);
            return;
        }
        handleFinger(e);
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.which == SDL_TOUCH_MOUSEID)
            return;
        if (ui_.wantsMouse() || overUi(mouse_) || pauseOpen_)
            return;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouse_ = glm::vec2(e.motion.x, e.motion.y) * pixelDensity_;
        break;
    case SDL_EVENT_TEXT_INPUT:
        return;
    case SDL_EVENT_KEY_DOWN:
        if (ui_.wantsText())
            return;
        pressKey(e.key.key == SDLK_AC_BACK ? SDLK_ESCAPE : e.key.key, (e.key.mod & SDL_KMOD_SHIFT) != 0);
        return;
    default:
        break;
    }
    if (screen_ == Screen::Game)
        handleGameEvent(e);
}

void App::pressKey(SDL_Keycode key, bool shift) {
    if (screen_ == Screen::Game)
        handleGameKey(key, shift);
    else
        handleMenuKey(key);
}

void App::handleFinger(const SDL_Event& e) {
    const glm::vec2 p{e.tfinger.x * static_cast<float>(renderer_.width()), e.tfinger.y * static_cast<float>(renderer_.height())};
    const SDL_FingerID id = e.tfinger.fingerID;

    auto spreadAndCenter = [this](float& spread, glm::vec2& center) {
        const auto& a = fingers_.begin()->second.pos;
        const auto& b = std::next(fingers_.begin())->second.pos;
        spread = glm::distance(a, b);
        center = (a + b) * 0.5f;
    };

    if (e.type == SDL_EVENT_FINGER_DOWN) {
        fingers_[id] = {p, p};
        if (fingers_.size() >= 2) {
            gesture_ = true;
            dragging_ = false;
            spreadAndCenter(gestureSpread_, gestureCenter_);
        }
        return;
    }

    const auto it = fingers_.find(id);
    if (it == fingers_.end())
        return;

    if (e.type == SDL_EVENT_FINGER_MOTION) {
        it->second.pos = p;
        if (gesture_ && fingers_.size() >= 2 && screen_ == Screen::Game) {
            float spread = 0.0f;
            glm::vec2 center{};
            spreadAndCenter(spread, center);
            const auto before = camera_.screenToGround(gestureCenter_.x, gestureCenter_.y);
            const auto after = camera_.screenToGround(center.x, center.y);
            if (before && after)
                camera_.target += *before - *after;
            if (spread > 10.0f && gestureSpread_ > 10.0f)
                camera_.distance = std::clamp(camera_.distance * gestureSpread_ / spread, kMinZoom, kMaxZoom);
            if (map_)
                camera_.target = glm::clamp(camera_.target, glm::vec2(0.0f), glm::vec2(map_->width(), map_->height()));
            gestureSpread_ = spread;
            gestureCenter_ = center;
        } else if (!gesture_ && screen_ == Screen::Game && glm::distance(it->second.start, p) > 16.0f) {
            dragging_ = true;
            dragStart_ = it->second.start;
            mouse_ = p;
        }
        return;
    }

    const Finger finger = it->second;
    fingers_.erase(it);
    if (gesture_) {
        if (fingers_.empty())
            gesture_ = false;
        return;
    }
    if (dragging_) {
        dragging_ = false;
        boxSelect(finger.start, p, false);
        return;
    }
    tap(p);
}

void App::tap(glm::vec2 p) {
    if (screen_ != Screen::Game)
        return;
    const auto ground = camera_.screenToGround(p.x, p.y);
    if (buildArmed_ >= 0) {
        placeBuild(p, false);
        return;
    }
    if (armedSkill_ >= 0) {
        castSlot(static_cast<std::uint8_t>(armedSkill_), ground);
        armedSkill_ = -1;
        return;
    }
    if (orderArm_ != Arm::None) {
        issueArmed(p);
        return;
    }
    if (ground)
        if (const auto* e = entityAt(*ground, 0.6f); e && isOwn(*e)) {
            selected_ = {e->id};
            inspected_ = 0;
            return;
        } else if (e && selected_.empty()) {
            inspected_ = e->id;
            return;
        }
    if (!selected_.empty())
        issueCommand(p, false);
}

void App::castSlot(std::uint8_t slot, std::optional<glm::vec2> ground) {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    for (const auto id : selected_) {
        const auto* own = latest->view.unit(id);
        if (!own || own->rank != static_cast<std::uint8_t>(Rank::Hero) || slot >= own->skills.size())
            continue;
        const auto& skill = data_->skill(own->skills[slot].skill);
        if (skill.passive)
            continue;
        glm::vec2 point{};
        if (skill.target == SkillTarget::Self) {
            if (const auto* e = findEntity(id))
                point = e->pos;
        } else if (ground) {
            point = *ground;
        } else {
            return;
        }
        if (own->skills[slot].cooldown == 0) {
            prediction_.cancel({id});
            net_.sendCommand(CastCommand{0, static_cast<entt::entity>(id), slot, point});
        }
        return;
    }
}

void App::handleMenuKey(SDL_Keycode key) {
    if (key != SDLK_ESCAPE)
        return;
    switch (screen_) {
    case Screen::Menu:
        settingsOpen_ = false;
        break;
    case Screen::Online:
        lobby_.close();
        setScreen(Screen::Menu);
        break;
    case Screen::Lan:
    case Screen::Address:
    case Screen::Code:
        status_.clear();
        setScreen(Screen::Menu);
        break;
    case Screen::Joining:
    case Screen::Connecting:
        leaveGame("cancelled");
        break;
    case Screen::Lobby:
        leaveGame("");
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
            if (buildArmed_ >= 0) {
                placeBuild(p, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
            } else if (orderArm_ != Arm::None) {
                issueArmed(p);
            } else {
                dragging_ = true;
                dragStart_ = p;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            orderArm_ = Arm::None;
            if (buildArmed_ >= 0)
                buildArmed_ = -1;
            else
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
    default:
        break;
    }
}

void App::handleGameKey(SDL_Keycode key, bool shift) {
    const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (key == SDLK_A && !selected_.empty()) {
        armOrder(Arm::Attack);
    } else if (key == SDLK_M && !selected_.empty()) {
        armOrder(Arm::Move);
    } else if (key == SDLK_T && !selected_.empty()) {
        armOrder(Arm::Patrol);
    } else if ((key == SDLK_S || key == SDLK_H) && !selected_.empty()) {
        prediction_.cancel(selected_);
        net_.sendCommand(StopCommand{selectedEntities(), key == SDLK_H});
    } else if (key == SDLK_ESCAPE) {
        if (pauseOpen_) {
            pauseOpen_ = false;
        } else if ((orderArm_ != Arm::None) || armedSkill_ >= 0 || buildArmed_ >= 0) {
            orderArm_ = Arm::None;
            armedSkill_ = -1;
            buildArmed_ = -1;
        } else if (panel_ != Panel::None) {
            panel_ = Panel::None;
        } else if (!selected_.empty()) {
            selected_.clear();
        } else {
            pauseOpen_ = true;
        }
    } else if (key == SDLK_SPACE) {
        pruneSelection();
        if (!selected_.empty())
            if (const auto* u = findEntity(selected_.front()))
                camera_.target = u->pos;
    } else if (key == SDLK_F10) {
        pauseOpen_ = !pauseOpen_;
    } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !touchLayout()) {
        chatOpen_ = true;
    } else if (key == SDLK_F3) {
        showStats_ = !showStats_;
        saveSettings();
    } else if (key == SDLK_F5) {
        cycleShadows();
    } else if (key == SDLK_F6) {
        cycleLight();
    } else if (key >= SDLK_0 && key <= SDLK_9 && (ctrl || panel_ == Panel::None)) {
        controlGroup(static_cast<int>(key - SDLK_0), ctrl, shift);
    } else if (panel_ != Panel::None && ((key >= SDLK_1 && key <= SDLK_9) || key == SDLK_0)) {
        handlePanelKey(key, shift);
    } else if (key == SDLK_Z || key == SDLK_X || key == SDLK_C) {
        const auto index = static_cast<std::size_t>(key == SDLK_Z ? 0 : key == SDLK_X ? 1 : 2);
        if (index < data_->recruitable().size())
            net_.sendCommand(RecruitCommand{0, data_->recruitable()[index]});
    } else if (buildKeyIndex(key) >= 0) {
        armBuild(static_cast<std::size_t>(buildKeyIndex(key)));
    } else if (key == SDLK_B || key == SDLK_I || key == SDLK_L) {
        const Panel p = key == SDLK_B ? Panel::Shop : key == SDLK_I ? Panel::Stash : Panel::Learn;
        panel_ = panel_ == p ? Panel::None : p;
    } else if (key == SDLK_P) {
        for (const auto id : selected_)
            net_.sendCommand(PromoteCommand{0, static_cast<entt::entity>(id)});
    } else if (key == SDLK_Q || key == SDLK_W || key == SDLK_E || key == SDLK_R) {
        const std::uint8_t slot = key == SDLK_Q ? 0 : key == SDLK_W ? 1 : key == SDLK_E ? 2 : 3;
        if (touchUi_) {
            bool self = false;
            if (const auto* own = focusedOwn(); own && slot < own->skills.size())
                self = data_->skill(own->skills[slot].skill).target == SkillTarget::Self;
            if (self)
                castSlot(slot, std::nullopt);
            else
                armedSkill_ = slot;
            orderArm_ = Arm::None;
        } else {
            castSlot(slot, camera_.screenToGround(mouse_.x, mouse_.y));
        }
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
    if (e && !isOwn(*e) && !add) {
        inspected_ = e->id;
        return;
    }
    inspected_ = 0;
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
    int best = 3;
    for (const auto id : selected_)
        if (const auto* e = findEntity(id))
            best = std::min(best, selectPriority(*e));
    std::erase_if(selected_, [&](std::uint32_t id) {
        const auto* e = findEntity(id);
        return !e || selectPriority(*e) > best;
    });
}

int App::selectPriority(const net::InterpolatedEntity& e) const {
    const auto& def = data_->unit(e.def);
    if (def.structure)
        return 3;
    if (e.rank >= 4 || def.weapon)
        return 1;
    return 2;
}

void App::armOrder(Arm arm) {
    orderArm_ = orderArm_ == arm ? Arm::None : arm;
    armedSkill_ = -1;
    buildArmed_ = -1;
}

void App::issueArmed(glm::vec2 screen) {
    const Arm arm = orderArm_;
    const bool keep = !touchLayout() && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (!keep)
        orderArm_ = Arm::None;
    issueCommand(screen, arm == Arm::Attack, arm == Arm::Patrol);
}

void App::issueCommand(glm::vec2 screen, bool attackMove, bool patrol) {
    if (const auto ground = camera_.screenToGround(screen.x, screen.y))
        issueCommandAt(*ground, attackMove, patrol);
}

void App::issueCommandAt(glm::vec2 point, bool attackMove, bool patrol) {
    const bool queue = !touchLayout() && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    pruneSelection();
    const std::optional<glm::vec2> ground = point;
    if (selected_.empty())
        return;
    std::uint8_t ownTeam = 0;
    if (const auto* first = findEntity(selected_.front()))
        ownTeam = first->team;

    const auto* target = entityAt(*ground, 0.4f);
    const bool followable = target && !data_->unit(target->def).structure &&
                            std::find(selected_.begin(), selected_.end(), target->id) == selected_.end();
    if (target && !patrol && (target->team != ownTeam || followable)) {
        const bool enemy = target->team != ownTeam;
        if (!queue)
            prediction_.cancel(selected_);
        net_.sendCommand(AttackCommand{selectedEntities(), static_cast<entt::entity>(target->id), queue});
        addEffect(Mesh::Cylinder, {target->pos.x, 0.1f, target->pos.y}, orderColor(static_cast<std::uint8_t>(enemy ? OrderType::Attack : OrderType::Follow)),
                  0.4f, data_->unit(target->def).radius * 2.0f + 1.0f, 0.35f);
        return;
    }
    const auto seq = net_.sendCommand(MoveCommand{selectedEntities(), *ground, attackMove, queue, patrol});
    if (queue) {
    } else if (attackMove || patrol) {
        prediction_.cancel(selected_);
    } else {
        std::vector<net::InterpolatedEntity> units;
        for (const auto id : selected_)
            if (const auto* e = findEntity(id))
                units.push_back(*e);
        prediction_.onMove(units, *ground, seq, *data_, *map_);
    }
    const glm::vec4 color = orderColor(static_cast<std::uint8_t>(patrol ? OrderType::Patrol : attackMove ? OrderType::AttackMove : OrderType::Move));
    addEffect(Mesh::Cylinder, {ground->x, 0.1f, ground->y}, color, 0.4f, 1.4f, 0.35f);
}

void App::updateCamera(float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec2 move{};
    if (keys[SDL_SCANCODE_UP])
        move.y -= 1.0f;
    if (keys[SDL_SCANCODE_DOWN])
        move.y += 1.0f;
    if (keys[SDL_SCANCODE_RIGHT])
        move.x += 1.0f;
    if (keys[SDL_SCANCODE_LEFT])
        move.x -= 1.0f;

    if (!touchUi_ && SDL_GetMouseFocus() == window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS)) {
        constexpr float edge = 6.0f;
        if (mouse_.x <= edge)
            move.x -= 1.0f;
        if (mouse_.x >= static_cast<float>(renderer_.width()) - edge)
            move.x += 1.0f;
        if (mouse_.y <= edge)
            move.y -= 1.0f;
        if (mouse_.y >= static_cast<float>(renderer_.height()) - edge)
            move.y += 1.0f;
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
            camera_.target = options_.camera.value_or(center / static_cast<float>(own));
            if (options_.zoom > 0.0f)
                camera_.distance = std::clamp(options_.zoom, kMinZoom, kMaxZoom);
            cameraPlaced_ = true;
        }
    }

    for (const auto& ev : net_.takeEvents())
        handleNetEvent(ev);
    for (const auto& d : net_.takeDeaths())
        addEffect(Mesh::Sphere, {d.pos.x, 0.6f, d.pos.y}, {1.0f, 0.8f, 0.4f, 0.7f}, 1.0f, 3.5f, 0.45f);
    bannerTime_ = std::max(0.0f, bannerTime_ - dt);

    for (auto& [id, s] : shells_)
        s.seen = false;
    for (auto& fx : effects_)
        fx.t += dt;
    std::erase_if(effects_, [](const Effect& fx) { return fx.t >= fx.life; });

    pruneSelection();
    updateCamera(dt);
}

void App::frame(double dt) {
    now_ = nowSeconds();
    frameDt_ = static_cast<float>(dt);
    frameMs_ = frameMs_ * 0.95f + static_cast<float>(dt * 1000.0) * 0.05f;
    fps_ = fps_ * 0.95f + (dt > 0.0 ? static_cast<float>(1.0 / dt) : 0.0f) * 0.05f;

    if (host_)
        host_->update(now_);
    net_.update(now_);
    if (autoStart_ && screen_ == Screen::Lobby && !net_.lobby().players.empty()) {
        autoStart_ = false;
        if (net_.lobby().leader == net_.player())
            net_.sendStart();
    }
    updateLobby();
    if (screen_ == Screen::Lan)
        lan_.update(now_);

    if (screen_ == Screen::Connecting) {
        if (net_.state() == net::ClientState::Connected)
            onConnected();
        else if (net_.state() == net::ClientState::Disconnected)
            leaveGame(net_.error().empty() ? "connection failed" : net_.error());
    } else if ((screen_ == Screen::Game || screen_ == Screen::Lobby) && net_.state() != net::ClientState::Connected) {
        leaveGame(net_.error().empty() ? "disconnected" : net_.error());
    } else if (screen_ == Screen::Lobby && !net_.inLobby()) {
        enterGame();
    } else if (screen_ == Screen::Game && net_.inLobby()) {
        returnToLobby();
    }
    for (auto& line : net_.takeChat()) {
        chatLog_.push_back({std::move(line), now_});
        if (chatLog_.size() > 60)
            chatLog_.erase(chatLog_.begin());
    }

    if (screen_ == Screen::Game)
        updateGame(static_cast<float>(dt));
    if (!paused_)
        render();
}

glm::vec4 App::playerColor(std::uint8_t owner) const {
    for (const auto& p : net_.lobby().players)
        if (p.id == owner) {
            const std::uint32_t c = net::kPlayerColors[p.color];
            return {static_cast<float>((c >> 16) & 0xff) / 255.0f, static_cast<float>((c >> 8) & 0xff) / 255.0f,
                    static_cast<float>(c & 0xff) / 255.0f, 1.0f};
        }
    return owner == net_.player() ? kOwnColor : kAllyColor;
}

glm::vec4 App::unitColor(const net::InterpolatedEntity& e) const {
    if (e.owner != net::kNoOwner)
        return playerColor(e.owner);
    glm::vec4 c = kTeamColors[e.team & 1];
    if (data_->unit(e.def).structure)
        c = glm::vec4(glm::vec3(c) * 0.7f, 1.0f);
    return c;
}

void App::render() {
    camera_.update(renderer_.width(), renderer_.height(), renderer_.homogeneousDepth());
    renderer_.begin(camera_);
    if (screen_ == Screen::Game && map_)
        renderWorld();
    ui_.beginFrame(frameDt_, renderer_.width(), renderer_.height(), uiScale());
    drawUi();
    ui_.endFrame(kUiView);
    renderer_.end();
}

glm::vec4 App::visibleRect(float margin) const {
    const auto w = static_cast<float>(renderer_.width());
    const auto h = static_cast<float>(renderer_.height());
    glm::vec2 lo{1e9f};
    glm::vec2 hi{-1e9f};
    for (const glm::vec2 corner : {glm::vec2{0.0f, 0.0f}, glm::vec2{w, 0.0f}, glm::vec2{0.0f, h}, glm::vec2{w, h}}) {
        const auto g = camera_.screenToGround(corner.x, corner.y);
        if (!g)
            return {-1e9f, -1e9f, 1e9f, 1e9f};
        lo = glm::min(lo, *g);
        hi = glm::max(hi, *g);
    }
    return {lo.x - margin, lo.y - margin, hi.x + margin, hi.y + margin};
}

void App::renderWorld() {
    const glm::vec4 white{1.0f};
    const glm::vec4 view = visibleRect(10.0f);
    auto inView = [&](glm::vec2 lo, glm::vec2 hi) { return hi.x >= view.x && hi.y >= view.y && lo.x <= view.z && lo.y <= view.w; };
    float identity[16];
    bx::mtxIdentity(identity);
    for (const auto& chunk : mapVisuals_.chunks) {
        if (!inView(chunk.min, chunk.max))
            continue;
        renderer_.drawTerrain(chunk.terrain, mapVisuals_.terrainTextures);
        for (const auto& p : chunk.props)
            renderer_.drawMesh(p.mesh, p.texture, p.mask, identity, white);
    }

    for (const auto& e : entities_) {
        if (!inView(e.pos, e.pos))
            continue;
        if (e.kind == net::EntityKind::Projectile) {
            const glm::vec4 c = glm::mix(kTeamColors[e.team & 1], glm::vec4(1.0f, 0.9f, 0.6f, 1.0f), 0.6f);
            renderer_.draw(Mesh::Sphere, projectilePos(e), glm::vec3(0.28f), 0.0f, c, Unlit);
            continue;
        }
        const auto& def = data_->unit(e.def);
        const auto& vis = visuals_[e.def];
        const auto rank = e.rank > 0 ? static_cast<Rank>(e.rank - 1) : Rank::Recruit;
        const float grow = rank == Rank::Hero ? 1.25f : rank == Rank::Elite ? 1.1f : 1.0f;
        const float d = def.radius * 2.0f * grow;
        const float height = vis.height * grow;
        const glm::vec4 color = unitColor(e);
        if (vis.parts.empty()) {
            renderer_.draw(vis.shape, {e.pos.x, height * 0.5f, e.pos.y}, {d, height, d}, e.facing, color);
        } else {
            float mtx[16];
            const glm::vec3 base{e.pos.x, 0.0f, e.pos.y};
            const float hullYaw = e.facing + vis.yaw;
            for (const auto& part : vis.parts) {
                if (part.turret)
                    makeTurretMatrix(mtx, base, hullYaw, vis.pivot, e.aim + kModelYaw, part.pitch ? e.pitch : 0.0f, vis.scale * grow);
                else
                    makeModelMatrix(mtx, base, hullYaw, vis.scale * grow, part.offset);
                renderer_.drawMesh(part.model->mesh, part.model->texture, part.model->mask, mtx, color, Tint);
            }
        }
        if (e.rank > 0 && rank != Rank::Recruit) {
            const glm::vec4 gold{1.0f, 0.82f, 0.25f, 1.0f};
            const glm::vec4 silver{0.8f, 0.85f, 0.95f, 1.0f};
            if (rank == Rank::Veteran)
                renderer_.draw(Mesh::Cube, {e.pos.x, height + 0.25f, e.pos.y}, {0.5f, 0.08f, 0.12f}, 0.0f, gold, Unlit);
            if (rank == Rank::Elite)
                renderer_.draw(Mesh::Cylinder, {e.pos.x, 0.05f, e.pos.y}, {d + 0.9f, 0.05f, d + 0.9f}, 0.0f, glm::vec4(glm::vec3(silver), 0.6f),
                               Unlit | Alpha);
            if (rank == Rank::Hero) {
                renderer_.draw(Mesh::Cylinder, {e.pos.x, 0.05f, e.pos.y}, {d + 1.2f, 0.05f, d + 1.2f}, 0.0f, glm::vec4(glm::vec3(gold), 0.6f),
                               Unlit | Alpha);
                renderer_.draw(Mesh::Sphere, {e.pos.x, height + 0.9f, e.pos.y}, glm::vec3(0.35f), 0.0f, gold, Unlit);
            }
        }

        if (def.weapon && !def.structure && vis.parts.empty()) {
            const glm::vec2 f{std::cos(e.aim), std::sin(e.aim)};
            const glm::vec2 nose = e.pos + f * d * 0.45f;
            renderer_.draw(Mesh::Cube, {nose.x, height * 0.65f, nose.y}, {d * 0.5f, 0.18f, 0.18f}, e.aim,
                           glm::vec4(glm::vec3(color) * 0.6f, 1.0f));
        }

        const bool selected = std::find(selected_.begin(), selected_.end(), e.id) != selected_.end();
        if (selected)
            renderer_.draw(Mesh::Cylinder, {e.pos.x, 0.03f, e.pos.y}, {d + 0.6f, 0.04f, d + 0.6f}, 0.0f, kSelectColor, Unlit | Alpha);

        const float maxHp = static_cast<float>(e.maxHp);
        if (selected || static_cast<float>(e.hp) < maxHp) {
            const float frac = std::clamp(static_cast<float>(e.hp) / maxHp, 0.0f, 1.0f);
            const float barW = std::max(1.0f, d);
            const float y = height + 0.5f;
            renderer_.draw(Mesh::Cube, {e.pos.x, y, e.pos.y}, {barW, 0.15f, 0.08f}, 0.0f, {0.05f, 0.05f, 0.05f, 1.0f}, Unlit);
            const glm::vec4 hpColor = glm::mix(glm::vec4{1.0f, 0.2f, 0.2f, 1.0f}, glm::vec4{0.3f, 1.0f, 0.4f, 1.0f}, frac);
            renderer_.draw(Mesh::Cube, {e.pos.x - barW * 0.5f + barW * frac * 0.5f, y, e.pos.y - 0.05f}, {barW * frac, 0.17f, 0.1f}, 0.0f,
                           hpColor, Unlit);
        }
    }

    std::erase_if(shells_, [](const auto& kv) { return !kv.second.seen; });
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

    renderBuildGhost();
    renderWaypoints();

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

glm::vec3 App::muzzleOf(const net::InterpolatedEntity& e) const {
    const auto& vis = visuals_[e.def];
    const auto rank = e.rank > 0 ? static_cast<Rank>(e.rank - 1) : Rank::Recruit;
    const float grow = rank == Rank::Hero ? 1.25f : rank == Rank::Elite ? 1.1f : 1.0f;
    if (!vis.turret)
        return {e.pos.x + std::cos(e.aim) * data_->unit(e.def).radius, vis.height * grow * 0.65f, e.pos.y + std::sin(e.aim) * data_->unit(e.def).radius};
    return turretPoint({e.pos.x, 0.0f, e.pos.y}, e.facing + vis.yaw, vis.pivot, e.aim + kModelYaw, e.pitch, vis.scale * grow, vis.muzzle);
}

glm::vec3 App::projectilePos(const net::InterpolatedEntity& e) {
    auto [it, fresh] = shells_.try_emplace(e.id);
    Shell& s = it->second;
    s.seen = true;
    if (fresh) {
        s.start = e.pos;
        s.muzzle = {e.pos.x, 0.8f, e.pos.y};
        s.lob = 0.0f;
        float best = 9.0f;
        for (const auto& u : entities_) {
            if (u.kind != net::EntityKind::Unit || u.def != e.def || u.team != e.team)
                continue;
            const glm::vec2 d = u.pos - e.pos;
            const float d2 = glm::dot(d, d);
            if (d2 < best) {
                best = d2;
                s.muzzle = muzzleOf(u);
                s.lob = std::tan(std::clamp(u.pitch, 0.0f, 1.2f));
            }
        }
    }
    const float done = glm::distance(e.pos, s.start);
    const float left = glm::distance(e.target, e.pos);
    const float k = done + left > 1e-3f ? done / (done + left) : 1.0f;
    const glm::vec2 xz = e.pos + (glm::vec2(s.muzzle.x, s.muzzle.z) - s.start) * (1.0f - k);
    const float y = s.muzzle.y + (0.6f - s.muzzle.y) * k + s.lob * (done + left) * k * (1.0f - k);
    return {xz.x, y, xz.y};
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
    case net::NetEventType::Fire: {
        const glm::vec2 from{pos.x, pos.z};
        const net::InterpolatedEntity* shooter = nullptr;
        float best = 1.0f;
        for (const auto& u : entities_) {
            if (u.kind != net::EntityKind::Unit)
                continue;
            const glm::vec2 d = u.pos - from;
            if (glm::dot(d, d) < best) {
                best = glm::dot(d, d);
                shooter = &u;
            }
        }
        if (shooter)
            addEffect(Mesh::Sphere, muzzleOf(*shooter), {1.0f, 0.85f, 0.4f, 0.9f}, 0.25f, 0.7f, 0.09f);
        break;
    }
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
    case net::NetEventType::SkillCast: {
        const auto id = static_cast<SkillId>(std::lround(value));
        if (id >= data_->skills().size())
            break;
        for (const auto& fx : data_->skill(id).effects) {
            const float r = std::max(fx.radius, 1.0f) * 2.0f;
            switch (fx.type) {
            case EffectType::Damage:
                addEffect(Mesh::Sphere, pos, {1.0f, 0.5f, 0.1f, 0.8f}, 0.5f, r, 0.6f);
                addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.z}, {1.0f, 0.35f, 0.1f, 0.6f}, r * 0.5f, r, 0.8f);
                break;
            case EffectType::Heal:
                addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.z}, {0.3f, 1.0f, 0.4f, 0.6f}, 1.0f, r, 0.6f);
                break;
            case EffectType::Buff:
                addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.z}, {1.0f, 0.9f, 0.3f, 0.5f}, 1.0f, r, 0.5f);
                break;
            case EffectType::Dash:
                addEffect(Mesh::Sphere, pos, {0.5f, 0.9f, 1.0f, 0.8f}, 0.3f, 2.5f, 0.3f);
                break;
            case EffectType::Summon:
                addEffect(Mesh::Sphere, pos, {0.6f, 0.4f, 1.0f, 0.6f}, 0.5f, 3.0f, 0.5f);
                break;
            case EffectType::Aura:
                break;
            }
        }
        break;
    }
    case net::NetEventType::Promote:
        addEffect(Mesh::Sphere, pos, {1.0f, 0.85f, 0.3f, 0.7f}, 0.5f, 4.0f, 0.6f);
        if (static_cast<Rank>(std::lround(value)) == Rank::Hero)
            showBanner("A HERO RISES", 2.0f);
        break;
    case net::NetEventType::LevelUp:
        addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.z}, {1.0f, 0.85f, 0.3f, 0.7f}, 0.5f, 4.0f, 0.6f);
        break;
    case net::NetEventType::Loot: {
        const auto id = static_cast<ItemId>(std::lround(value));
        if (id < data_->items().size())
            showBanner("LOOT: " + data_->item(id).name, 3.0f);
        addEffect(Mesh::Sphere, pos, {0.9f, 0.6f, 1.0f, 0.7f}, 0.5f, 3.0f, 0.8f);
        break;
    }
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
        if (const Model* m = mapVisuals_.derrick) {
            float mtx[16];
            const float extent = std::max({m->max.x - m->min.x, m->max.z - m->min.z, 1.0f});
            makeModelMatrix(mtx, {def.pos.x, 0.0f, def.pos.y}, 0.0f, 2.6f / extent, {});
            renderer_.drawMesh(m->mesh, m->texture, m->mask, mtx, ownerColor, Tint);
        } else {
            renderer_.draw(Mesh::Cylinder, {def.pos.x, 1.2f, def.pos.y}, {0.35f, 2.4f, 0.35f}, 0.0f, ownerColor);
        }
        renderer_.draw(Mesh::Sphere, {def.pos.x, 3.4f, def.pos.y}, glm::vec3(0.5f), 0.0f, ownerColor, Unlit);
    }
}

const net::OwnUnit* App::focusedOwn() const {
    const auto* latest = net_.latest();
    if (!latest || selected_.empty())
        return nullptr;
    return latest->view.unit(selected_.front());
}

std::string App::modsText(const StatMods& m) const {
    std::string s;
    char buf[48];
    auto add = [&](const char* fmt, float v) {
        std::snprintf(buf, sizeof(buf), fmt, v);
        s += buf;
    };
    if (m.hp != 0.0f)
        add("+%.0f hp ", m.hp);
    if (m.hpMul != 1.0f)
        add("+%.0f%% hp ", (m.hpMul - 1.0f) * 100.0f);
    if (m.damageMul != 1.0f)
        add("+%.0f%% dmg ", (m.damageMul - 1.0f) * 100.0f);
    if (m.attackSpeedMul != 1.0f)
        add("+%.0f%% atk spd ", (m.attackSpeedMul - 1.0f) * 100.0f);
    if (m.speedMul != 1.0f)
        add("%+.0f%% move ", (m.speedMul - 1.0f) * 100.0f);
    if (m.armor != 0.0f)
        add("+%.0f armor ", m.armor);
    if (m.range != 0.0f)
        add("+%.1f range ", m.range);
    if (m.regen != 0.0f)
        add("+%.0f regen ", m.regen);
    return s;
}

void App::handlePanelKey(SDL_Keycode key, bool shift) {
    const int digit = key == SDLK_0 ? 10 : static_cast<int>(key - SDLK_1) + 1;
    const auto index = static_cast<std::size_t>(digit - 1);
    const auto* latest = net_.latest();
    if (!latest)
        return;
    const auto focused = selected_.empty() ? entt::null : static_cast<entt::entity>(selected_.front());

    switch (panel_) {
    case Panel::Shop:
        if (index < data_->shop().size())
            net_.sendCommand(BuyCommand{0, data_->shop()[index]});
        break;
    case Panel::Stash:
        if (focused == entt::null)
            break;
        if (shift)
            net_.sendCommand(UnequipCommand{0, focused, static_cast<std::uint8_t>(index)});
        else if (index < latest->view.stash.size())
            net_.sendCommand(EquipCommand{0, focused, static_cast<std::uint8_t>(index)});
        break;
    case Panel::Learn:
        if (const auto* e = findEntity(selected_.empty() ? 0 : selected_.front())) {
            const auto& pool = data_->unit(e->def).skills;
            if (index < pool.size())
                net_.sendCommand(LearnCommand{0, focused, pool[index]});
        }
        break;
    case Panel::None:
        break;
    }
}

}
