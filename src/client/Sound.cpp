#include "App.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

namespace vw::client {

void App::loadSounds() {
    if (!audio_.ready() && !audio_.init())
        return;
    std::string index;
    try {
        index = readData("data/sfx/index.txt");
    } catch (const std::exception& e) {
        spdlog::warn("sounds: {}", e.what());
        return;
    }
    int loaded = 0;
    std::size_t start = 0;
    while (start < index.size()) {
        const auto end = index.find('\n', start);
        std::string name = index.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? index.size() : end + 1;
        if (!name.empty() && name.back() == '\r')
            name.pop_back();
        if (name.empty())
            continue;
        try {
            const std::string bytes = readData("data/sfx/" + name + ".wav");
            loaded += audio_.load(name, bytes.data(), bytes.size()) ? 1 : 0;
        } catch (const std::exception&) {
        }
    }
    spdlog::info("{} sounds loaded", loaded);
    applyAudio();
}

void App::applyAudio() {
    audio_.setMaster(volMaster_);
    audio_.setVolume(SoundBus::Effects, volEffects_);
    audio_.setVolume(SoundBus::Interface, volUi_);
    audio_.setVolume(SoundBus::Ambient, volAmbient_);
}

float App::listenerGain(glm::vec2 pos, float* pan) const {
    const float d = glm::distance(pos, camera_.target);
    const float radius = camera_.distance * 1.1f + 14.0f;
    const float att = std::clamp(1.0f - d / radius, 0.0f, 1.0f);
    if (pan)
        *pan = std::clamp((pos.x - camera_.target.x) / std::max(camera_.distance * 0.9f, 6.0f), -1.0f, 1.0f);
    return att * att * std::clamp(1.35f - camera_.distance / 90.0f, 0.5f, 1.0f);
}

void App::playAt(const std::string& name, glm::vec2 pos, float gain, float pitch) {
    if (!audio_.ready())
        return;
    float pan = 0.0f;
    const float g = gain * listenerGain(pos, &pan);
    if (g < 0.01f)
        return;
    const std::string key = name + ":" + std::to_string(static_cast<int>(pos.x / 3.0f)) + "," + std::to_string(static_cast<int>(pos.y / 3.0f));
    auto& last = soundThrottle_[key];
    if (now_ - last < 0.06)
        return;
    last = now_;
    if (soundThrottle_.size() > 512)
        std::erase_if(soundThrottle_, [&](const auto& kv) { return now_ - kv.second > 1.0; });
    audio_.play(name, SoundBus::Effects, g, pan, pitch * fxRandom(0.94f, 1.06f));
}

void App::playUi(const std::string& name, float gain) {
    if (audio_.ready())
        audio_.play(name, SoundBus::Interface, gain);
}

void App::playShot(const net::InterpolatedEntity& shooter) {
    const auto& def = data_->unit(shooter.def);
    const auto& vis = visuals_[shooter.def];
    const float damage = def.weapon ? def.weapon->damage : 10.0f;
    const float gain = std::clamp(0.35f + damage / 150.0f, 0.35f, 1.0f);
    std::string name = "shot_heavy";
    if (vis.crystalShot) {
        name = "shot_void";
    } else if (vis.frostShot) {
        name = "shot_frost";
    } else {
        switch (shotKind(shooter.def)) {
        case ShotKind::Bullet:
            name = damage < 12.0f ? "shot_mg" : "shot_heavy";
            break;
        case ShotKind::Shell:
            name = damage < 40.0f ? "shot_auto" : "shot_cannon";
            break;
        case ShotKind::Artillery:
        case ShotKind::Ballistic:
            name = "shot_artillery";
            break;
        case ShotKind::Rocket:
            name = "shot_rocket";
            break;
        case ShotKind::Missile:
            name = "shot_missile";
            break;
        case ShotKind::Mortar:
            name = "shot_mortar";
            break;
        case ShotKind::Auto:
            break;
        }
    }
    playAt(name, shooter.pos, gain);
}

void App::playImpact(glm::vec2 at, ShotKind kind, float splash) {
    switch (kind) {
    case ShotKind::Bullet:
        if (fxRandom(0.0f, 1.0f) < 0.35f)
            playAt("hit_metal", at, 0.3f);
        break;
    case ShotKind::Shell:
        playAt("expl_small", at, 0.55f);
        break;
    case ShotKind::Artillery:
    case ShotKind::Ballistic:
        playAt(splash >= 2.5f ? "expl_large" : "expl_medium", at, 0.9f);
        break;
    default:
        playAt(splash >= 1.5f ? "expl_medium" : "expl_small", at, 0.7f);
        break;
    }
}

void App::playDeath(glm::vec2 at, const UnitDef& def, bool voidSide) {
    if (voidSide) {
        playAt("void_death", at, 0.8f);
        return;
    }
    const char* name = def.structure || def.radius >= 1.2f ? "expl_large" : def.radius >= 0.65f || def.air ? "expl_medium" : "expl_small";
    playAt(name, at, 1.0f);
}

void App::updateAudio() {
    if (!audio_.ready())
        return;
    const bool game = screen_ == Screen::Game && map_;
    float engine = 0.0f, enginePan = 0.0f, rotor = 0.0f, rotorPan = 0.0f;
    if (game) {
        for (const auto& e : entities_) {
            if (e.kind != net::EntityKind::Unit || (e.flags & net::kStateCarried))
                continue;
            const auto& def = data_->unit(e.def);
            if (def.structure)
                continue;
            float pan = 0.0f;
            const float g = listenerGain(e.pos, &pan);
            if (g <= 0.0f)
                continue;
            if (!visuals_[e.def].spinners.empty()) {
                rotor += g;
                rotorPan += pan * g;
            } else if (glm::length(e.vel) > 0.4f) {
                engine += g;
                enginePan += pan * g;
            }
        }
    }
    audio_.loop("loop_engine", SoundBus::Effects, std::min(0.28f, 0.07f * std::sqrt(engine)), engine > 0.0f ? enginePan / engine : 0.0f,
                0.9f + std::min(engine, 6.0f) * 0.03f);
    audio_.loop("loop_rotor", SoundBus::Effects, std::min(0.75f, 0.4f * std::sqrt(rotor)), rotor > 0.0f ? rotorPan / rotor : 0.0f);
    const bool cold = map_ && (map_->theme() == "winter" || map_->theme() == "autumn");
    audio_.loop("loop_wind", SoundBus::Ambient, game ? (cold ? 0.4f : 0.18f) : 0.0f);

    if (game) {
        if (const auto* latest = net_.latest()) {
            const auto state = static_cast<MatchState>(latest->game.state);
            if (state != lastMatchState_ && lastMatchState_ == MatchState::Playing && (state == MatchState::Victory || state == MatchState::Defeat))
                playUi(wonMatch() ? "victory" : "defeat", 0.9f);
            lastMatchState_ = state;
        }
        const bool changed = selected_ != lastSelection_;
        if (changed && !selected_.empty() && selected_.size() >= lastSelection_.size())
            playUi("ui_select", 0.5f);
        lastSelection_ = selected_;
    } else {
        lastMatchState_ = MatchState::Playing;
        lastSelection_.clear();
    }
}

}
