#include "App.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace vw::client {

namespace {

#if defined(SDL_PLATFORM_ANDROID)
constexpr float kFxDensity = 0.5f;
#else
constexpr float kFxDensity = 1.0f;
#endif
constexpr std::size_t kMaxParticles = 4000;

const glm::vec4 kSideColors[] = {{0.25f, 0.75f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.55f, 1.0f}};
const glm::vec4 kFrostCore{0.9f, 0.97f, 1.0f, 1.0f};
const glm::vec4 kFrostHalo{0.25f, 0.65f, 1.0f, 1.0f};

}

float App::fxRandom(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(fxRng_);
}

glm::vec3 App::fxDirection(float up) {
    const float a = fxRandom(0.0f, 2.0f * std::numbers::pi_v<float>);
    const float y = fxRandom(up, 1.0f);
    const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    return {std::cos(a) * r, y, std::sin(a) * r};
}

void App::addParticle(const Particle& p) {
    if (particles_.size() < kMaxParticles)
        particles_.push_back(p);
}

int App::fxCount(float n) const {
    return std::max(1, static_cast<int>(n * kFxDensity));
}

void App::spawnExplosion(glm::vec3 at, float size, bool structure, bool voidSide, glm::vec4 hull) {
    const float s = std::max(size, 0.6f) * (structure ? 1.4f : 1.0f);
    const glm::vec4 fire = voidSide ? glm::vec4{0.32f, 0.08f, 0.45f, 1.0f} : glm::vec4{0.5f, 0.2f, 0.04f, 1.0f};
    const glm::vec4 hot = voidSide ? glm::vec4{0.8f, 0.55f, 0.9f, 1.0f} : glm::vec4{0.95f, 0.75f, 0.4f, 1.0f};
    const glm::vec3 center = at + glm::vec3(0.0f, 0.4f * s, 0.0f);

    addParticle({center, {}, hot * 0.6f, s * 1.8f, 0.0f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow});
    for (int i = 0; i < fxCount(4.0f + 6.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.1f);
        const float delay = structure ? fxRandom(0.0f, 0.5f) : fxRandom(0.0f, 0.12f);
        addParticle({center + dir * fxRandom(0.0f, 0.6f * s), dir * fxRandom(0.6f, 2.4f) + glm::vec3(0.0f, 0.8f, 0.0f), fire,
                     s * fxRandom(0.3f, 0.5f), s * fxRandom(0.7f, 1.1f), fxRandom(0.35f, 0.7f), -delay, 0.0f, 2.5f, 0.0f, ParticleKind::Glow});
    }
    for (int i = 0; i < fxCount(18.0f + 18.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.15f);
        addParticle({center, dir * fxRandom(5.0f, 13.0f) * std::sqrt(s), hot, fxRandom(0.08f, 0.16f), 0.02f, fxRandom(0.35f, 0.9f), 0.0f, 12.0f, 0.6f,
                     0.0f, ParticleKind::Glow});
    }
    const glm::vec4 metal = glm::vec4(glm::vec3(hull) * 0.12f + glm::vec3(0.16f, 0.16f, 0.17f), 1.0f);
    for (int i = 0; i < fxCount(5.0f + 7.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.35f);
        const float chunk = std::min(0.35f, s * fxRandom(0.05f, 0.13f));
        addParticle({center, dir * fxRandom(3.0f, 8.0f), i % 3 == 0 ? glm::vec4(0.08f, 0.07f, 0.06f, 1.0f) : metal, chunk, chunk, fxRandom(1.4f, 2.4f), 0.0f,
                     16.0f, 0.2f, fxRandom(-9.0f, 9.0f), ParticleKind::Debris});
    }
    for (int i = 0; i < fxCount(5.0f + 5.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.4f);
        const float grey = fxRandom(0.12f, 0.28f);
        const glm::vec4 smoke = voidSide ? glm::vec4(grey * 1.2f, grey * 0.8f, grey * 1.5f, 0.32f) : glm::vec4(grey, grey, grey * 1.05f, 0.32f);
        addParticle({center + dir * (0.3f * s), dir * fxRandom(0.4f, 1.2f) + glm::vec3(0.0f, fxRandom(0.8f, 1.6f), 0.0f), smoke, s * 0.35f,
                     s * fxRandom(1.0f, 1.5f), fxRandom(2.2f, 4.0f), -fxRandom(0.1f, 0.4f), -0.15f, 0.8f, 0.0f, ParticleKind::Smoke});
    }
    addEffect(Mesh::Cylinder, {at.x, 0.1f, at.z}, glm::vec4(glm::vec3(hot), 0.7f), 0.4f * s, 4.2f * s, 0.45f);
    addParticle({{at.x, 0.02f, at.z}, {}, {0.03f, 0.025f, 0.02f, 0.35f}, s * 1.0f, s * 1.1f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Decal});
}

void App::spawnFrostBurst(glm::vec3 at, float scale) {
    addParticle({at, {}, kFrostCore * 0.7f, 1.1f * scale, 0.0f, 0.14f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow});
    for (int i = 0; i < fxCount(10.0f * scale + 4.0f); ++i) {
        const glm::vec3 dir = fxDirection(-0.2f);
        addParticle({at, dir * fxRandom(2.0f, 6.0f) * scale, i % 2 ? kFrostCore : kFrostHalo, fxRandom(0.08f, 0.18f) * scale, 0.02f,
                     fxRandom(0.35f, 0.7f), 0.0f, 9.0f, 1.0f, fxRandom(-12.0f, 12.0f), ParticleKind::Shard});
    }
    for (int i = 0; i < fxCount(3.0f * scale + 1.0f); ++i)
        addParticle({at + fxDirection(0.0f) * 0.2f, fxDirection(0.2f) * 0.6f, kFrostHalo * 0.18f, 0.4f * scale, 1.2f * scale, fxRandom(0.5f, 0.8f), 0.0f,
                     -0.2f, 1.5f, 0.0f, ParticleKind::Glow});
    addEffect(Mesh::Cylinder, {at.x, 0.08f, at.z}, {0.55f, 0.85f, 1.0f, 0.7f}, 0.3f, 2.4f * scale, 0.35f);
}

void App::renderFrostShot(const net::InterpolatedEntity& e, glm::vec3 p, float scale) {
    const float now = static_cast<float>(now_);
    const float pulse = 0.85f + 0.15f * std::sin(now * 26.0f + static_cast<float>(e.id));
    glm::vec2 dir2 = e.target - e.pos;
    dir2 = glm::length(dir2) > 1e-3f ? glm::normalize(dir2) : glm::vec2(1.0f, 0.0f);
    const glm::vec3 fwd{dir2.x, 0.0f, dir2.y};
    const glm::vec3 side{-dir2.y, 0.0f, dir2.x};
    renderer_.drawGlow(Mesh::Sphere, p, glm::vec3(0.85f * pulse * scale), kFrostHalo * 0.35f);
    renderer_.drawGlow(Mesh::Sphere, p, glm::vec3(0.3f * scale), kFrostCore * 0.85f);
    for (int k = 0; k < 4; ++k) {
        const float a = now * 11.0f + static_cast<float>(k) * 1.5708f + static_cast<float>(e.id);
        const glm::vec3 orbit = (side * std::cos(a) + glm::vec3(0.0f, std::sin(a), 0.0f)) * (0.42f * scale);
        renderer_.drawGlow(Mesh::Cube, p + orbit - fwd * 0.05f, glm::vec3(0.09f * scale, 0.09f * scale, 0.2f * scale), kFrostCore * 0.9f);
    }
    for (int k = 1; k <= 4; ++k)
        renderer_.drawGlow(Mesh::Sphere, p - fwd * (0.28f * static_cast<float>(k) * scale), glm::vec3((0.4f - 0.07f * static_cast<float>(k)) * scale),
                           kFrostHalo * (0.3f - 0.06f * static_cast<float>(k)));
    if (fxRandom(0.0f, 1.0f) < frameDt_ * 18.0f * kFxDensity)
        addParticle({p + fxDirection(-1.0f) * (0.2f * scale), fxDirection(-1.0f) * 0.4f - fwd * 0.8f, kFrostHalo * 0.16f, 0.25f * scale,
                     0.6f * scale, fxRandom(0.3f, 0.6f), 0.0f, -0.1f, 2.0f, 0.0f, ParticleKind::Glow});
}

void App::updateParticles(float dt) {
    for (auto& p : particles_) {
        p.t += dt;
        if (p.t < 0.0f)
            continue;
        p.vel.y -= p.gravity * dt;
        p.vel *= std::exp(-p.drag * dt);
        p.pos += p.vel * dt;
        if ((p.kind == ParticleKind::Debris || p.kind == ParticleKind::Shard) && p.pos.y < p.size0 * 0.5f) {
            p.pos.y = p.size0 * 0.5f;
            if (p.vel.y < 0.0f)
                p.vel.y = -p.vel.y * 0.3f;
            p.vel.x *= 0.55f;
            p.vel.z *= 0.55f;
            p.spin *= 0.6f;
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.t >= p.life; });
}

void App::renderParticles() {
    for (const auto& p : particles_) {
        if (p.t < 0.0f)
            continue;
        const float k = std::clamp(p.t / p.life, 0.0f, 1.0f);
        const float size = p.size0 + (p.size1 - p.size0) * k;
        switch (p.kind) {
        case ParticleKind::Glow:
            renderer_.drawGlow(Mesh::Sphere, p.pos, glm::vec3(size), p.color * (1.0f - k * k));
            break;
        case ParticleKind::Shard:
            renderer_.drawGlow(Mesh::Cube, p.pos, glm::vec3(size), p.color * (1.0f - k));
            break;
        case ParticleKind::Smoke: {
            glm::vec4 c = p.color;
            c.a *= (1.0f - k) * std::min(1.0f, p.t * 6.0f);
            renderer_.draw(Mesh::Sphere, p.pos, glm::vec3(size), 0.0f, c, Alpha | NoShadow);
            break;
        }
        case ParticleKind::Debris: {
            glm::vec4 c = p.color;
            const float fade = std::clamp((p.life - p.t) / 0.6f, 0.0f, 1.0f);
            renderer_.draw(Mesh::Cube, p.pos, glm::vec3(size) * fade, p.spin * p.t, c, Lit);
            break;
        }
        case ParticleKind::Decal: {
            glm::vec4 c = p.color;
            c.a *= std::clamp((p.life - p.t) / 3.0f, 0.0f, 1.0f);
            renderer_.draw(Mesh::Cylinder, p.pos, {size, 0.02f, size}, 0.0f, c, Unlit | Alpha | NoShadow);
            break;
        }
        }
    }
}

void App::onUnitDeath(const net::UnitDeath& d) {
    if (d.flags & net::kStateCarried)
        return;
    if (d.def >= data_->units().size()) {
        spawnExplosion({d.pos.x, 0.0f, d.pos.y}, 1.0f, false, d.team == 1, {0.5f, 0.5f, 0.5f, 1.0f});
        return;
    }
    const auto& def = data_->unit(d.def);
    const float lift = def.air ? 3.0f : 0.0f;
    const glm::vec4 hull = kSideColors[d.team & 1];
    spawnExplosion({d.pos.x, lift, d.pos.y}, def.radius * 2.0f, def.structure, d.team == 1, hull);
}

void App::updateSnow(float dt) {
    if (!map_ || map_->theme() != "winter") {
        snow_.clear();
        return;
    }
    const float r = std::clamp(camera_.distance * 0.9f, 20.0f, 70.0f);
    const auto target = static_cast<std::size_t>(static_cast<float>(kFxDensity * 900.0f) * (r / 45.0f));
    const glm::vec2 c = camera_.target;
    while (snow_.size() < target)
        snow_.push_back({c.x + fxRandom(-r, r), fxRandom(0.0f, 20.0f), c.y + fxRandom(-r, r), fxRandom(1.2f, 2.4f)});
    if (snow_.size() > target)
        snow_.resize(target);
    const float t = static_cast<float>(now_);
    const glm::vec2 wind{0.9f + 0.5f * std::sin(t * 0.21f), 0.35f};
    for (auto& f : snow_) {
        f.y -= f.w * dt;
        f.x += (wind.x + std::sin(t * 1.3f + f.w * 7.0f) * 0.4f) * dt;
        f.z += (wind.y + std::cos(t * 1.1f + f.w * 5.0f) * 0.3f) * dt;
        if (f.y < 0.0f || std::abs(f.x - c.x) > r || std::abs(f.z - c.y) > r) {
            f.x = c.x + fxRandom(-r, r);
            f.z = c.y + fxRandom(-r, r);
            f.y = f.y < 0.0f ? 20.0f : fxRandom(0.0f, 20.0f);
        }
    }
}

void App::renderSnow() {
    for (const auto& f : snow_) {
        const float size = 0.05f + 0.035f * (f.w - 1.2f);
        renderer_.draw(Mesh::Cube, {f.x, f.y, f.z}, glm::vec3(size), f.w * 3.0f, {0.92f, 0.94f, 0.98f, 1.0f}, Unlit | NoShadow);
    }
}

}
