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

ShotKind App::shotKind(UnitDefId def) const {
    if (def >= visuals_.size() || def >= data_->units().size())
        return ShotKind::Bullet;
    if (visuals_[def].shot != ShotKind::Auto)
        return visuals_[def].shot;
    const auto& d = data_->unit(def);
    if (!d.weapon)
        return ShotKind::Bullet;
    if (d.weapon->guided > 0.0f)
        return ShotKind::Missile;
    if (smallCalibre(d))
        return ShotKind::Bullet;
    if (d.weapon->type == DamageType::Explosive && d.weapon->splash >= 1.5f && d.weapon->projectileSpeed <= 16.0f)
        return ShotKind::Mortar;
    return ShotKind::Shell;
}

void App::smokePuff(glm::vec3 at, glm::vec3 vel, float size0, float size1, float life, glm::vec4 color) {
    Particle p{at, vel, color, size0, size1, life, 0.0f, -0.12f, 0.9f, fxRandom(-0.8f, 0.8f), ParticleKind::Smoke};
    p.sprite = static_cast<Sprite>(static_cast<int>(fxRandom(0.0f, 3.99f)));
    addParticle(p);
}

void App::spawnExplosion(glm::vec3 at, float size, bool structure, bool voidSide, glm::vec4 hull) {
    const float s = std::max(size, 0.6f) * (structure ? 1.4f : 1.0f);
    const glm::vec4 fire = voidSide ? glm::vec4{0.55f, 0.2f, 0.85f, 1.0f} : glm::vec4{1.0f, 0.45f, 0.12f, 1.0f};
    const glm::vec4 hot = voidSide ? glm::vec4{0.9f, 0.7f, 1.0f, 1.0f} : glm::vec4{1.0f, 0.85f, 0.55f, 1.0f};
    const glm::vec3 center = at + glm::vec3(0.0f, 0.4f * s, 0.0f);

    Particle flash{center, {}, hot, s * 3.2f, s * 1.6f, 0.16f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow};
    flash.sprite = Sprite::Flare;
    addParticle(flash);
    flash.sprite = Sprite::Burst1;
    flash.size0 = s * 2.4f;
    flash.size1 = s * 3.4f;
    flash.life = 0.22f;
    flash.spin = fxRandom(-2.0f, 2.0f);
    addParticle(flash);
    for (int i = 0; i < fxCount(5.0f + 6.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.1f);
        const float delay = structure ? fxRandom(0.0f, 0.5f) : fxRandom(0.0f, 0.1f);
        Particle f{center + dir * fxRandom(0.0f, 0.6f * s), dir * fxRandom(0.6f, 2.4f) + glm::vec3(0.0f, 1.0f, 0.0f), fire, s * fxRandom(0.7f, 1.0f),
                   s * fxRandom(1.4f, 2.0f), fxRandom(0.4f, 0.8f), -delay, -0.5f, 2.0f, fxRandom(-1.5f, 1.5f), ParticleKind::Fire};
        f.sprite = fxRandom(0.0f, 1.0f) < 0.5f ? Sprite::Fire0 : Sprite::Fire1;
        addParticle(f);
    }
    for (int i = 0; i < fxCount(14.0f + 14.0f * s); ++i) {
        const glm::vec3 dir = fxDirection(0.15f);
        addParticle({center, dir * fxRandom(6.0f, 15.0f) * std::sqrt(s), hot, fxRandom(0.05f, 0.09f), 0.03f, fxRandom(0.3f, 0.8f), 0.0f, 12.0f, 0.6f,
                     0.0f, ParticleKind::Spark});
    }
    Particle ring{{at.x, 0.06f, at.z}, {}, voidSide ? glm::vec4{0.5f, 0.35f, 0.6f, 0.5f} : glm::vec4{0.62f, 0.58f, 0.54f, 0.55f}, s * 1.0f, s * 5.0f, 0.7f,
                  0.0f, 0.0f, 0.0f, fxRandom(0.0f, 6.0f), ParticleKind::Ring};
    ring.sprite = Sprite::SmokeRing;
    addParticle(ring);
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
        const glm::vec4 smoke = voidSide ? glm::vec4(grey * 1.2f, grey * 0.8f, grey * 1.5f, 0.55f) : glm::vec4(grey, grey, grey * 1.05f, 0.55f);
        smokePuff(center + dir * (0.3f * s), dir * fxRandom(0.4f, 1.2f) + glm::vec3(0.0f, fxRandom(0.8f, 1.6f), 0.0f), s * 0.9f, s * fxRandom(2.2f, 3.2f),
                  fxRandom(2.5f, 4.5f), smoke);
    }
    Particle scorch{{at.x, 0.03f, at.z}, {}, {0.02f, 0.018f, 0.016f, 0.45f}, s * 1.4f, s * 1.5f, 20.0f, 0.0f, 0.0f, 0.0f, fxRandom(0.0f, 6.0f),
                    ParticleKind::Decal};
    scorch.sprite = Sprite::Dirt1;
    addParticle(scorch);
}

void App::spawnImpact(glm::vec3 at, ShotKind kind, float splash, bool voidSide) {
    const glm::vec4 hot = voidSide ? glm::vec4{0.9f, 0.7f, 1.0f, 1.0f} : glm::vec4{1.0f, 0.85f, 0.55f, 1.0f};
    if (kind == ShotKind::Bullet) {
        Particle p{at, {}, hot * 0.8f, 0.5f, 0.2f, 0.06f, 0.0f, 0.0f, 0.0f, fxRandom(0.0f, 6.0f), ParticleKind::Glow};
        p.sprite = Sprite::Star;
        addParticle(p);
        for (int i = 0; i < fxCount(3.0f); ++i)
            addParticle({at, fxDirection(0.3f) * fxRandom(2.0f, 5.0f), hot, 0.04f, 0.02f, fxRandom(0.15f, 0.3f), 0.0f, 10.0f, 0.5f, 0.0f, ParticleKind::Spark});
        if (fxRandom(0.0f, 1.0f) < 0.4f)
            smokePuff({at.x, 0.2f, at.z}, {0.0f, 0.5f, 0.0f}, 0.2f, 0.7f, 0.6f, {0.6f, 0.58f, 0.55f, 0.35f});
        return;
    }
    const float s = kind == ShotKind::Ballistic ? 3.2f : kind == ShotKind::Shell ? 0.5f : std::max(0.6f, splash * 0.8f);
    const glm::vec3 ground{at.x, 0.0f, at.z};
    Particle flash{ground + glm::vec3(0.0f, 0.4f * s, 0.0f), {}, hot, s * 2.6f, s * 1.2f, 0.14f, 0.0f, 0.0f, 0.0f, fxRandom(-2.0f, 2.0f), ParticleKind::Glow};
    flash.sprite = Sprite::Burst0;
    addParticle(flash);
    for (int i = 0; i < fxCount(2.0f + 3.0f * s); ++i) {
        Particle f{ground + fxDirection(0.2f) * (0.3f * s), fxDirection(0.3f) * 1.5f + glm::vec3(0.0f, 1.2f, 0.0f), hot, s * 0.7f, s * 1.5f,
                   fxRandom(0.25f, 0.5f), 0.0f, 0.0f, 2.5f, fxRandom(-1.5f, 1.5f), ParticleKind::Fire};
        f.sprite = fxRandom(0.0f, 1.0f) < 0.5f ? Sprite::Fire0 : Sprite::Fire1;
        addParticle(f);
    }
    for (int i = 0; i < fxCount(3.0f + 3.0f * s); ++i) {
        Particle d{ground + glm::vec3(0.0f, 0.2f, 0.0f), fxDirection(0.6f) * fxRandom(3.0f, 7.0f) * std::sqrt(s), {0.36f, 0.32f, 0.28f, 0.8f}, s * 0.4f,
                   s * 0.9f, fxRandom(0.5f, 0.9f), 0.0f, 9.0f, 1.0f, fxRandom(-3.0f, 3.0f), ParticleKind::Smoke};
        d.sprite = Sprite::Dirt0;
        addParticle(d);
    }
    for (int i = 0; i < fxCount(2.0f + 2.0f * s); ++i)
        smokePuff(ground + fxDirection(0.0f) * (0.4f * s), fxDirection(0.5f) * 0.6f + glm::vec3(0.0f, 0.7f, 0.0f), s * 0.8f, s * 2.4f, fxRandom(1.8f, 3.2f),
                  {0.3f, 0.3f, 0.31f, 0.5f});
    Particle ring{{at.x, 0.06f, at.z}, {}, {0.65f, 0.6f, 0.56f, 0.45f}, s * 0.6f, s * 3.6f, 0.5f, 0.0f, 0.0f, 0.0f, fxRandom(0.0f, 6.0f), ParticleKind::Ring};
    ring.sprite = Sprite::SmokeRing;
    addParticle(ring);
    Particle scorch{{at.x, 0.03f, at.z}, {}, {0.03f, 0.025f, 0.02f, 0.4f}, s * 0.9f, s * 1.0f, 14.0f, 0.0f, 0.0f, 0.0f, fxRandom(0.0f, 6.0f), ParticleKind::Decal};
    scorch.sprite = Sprite::Dirt1;
    addParticle(scorch);
}

void App::renderShell(const net::InterpolatedEntity& e, Shell& shell, glm::vec3 p) {
    const glm::vec3 d = p - shell.last;
    if (glm::dot(d, d) > 1e-6f)
        shell.dir = glm::normalize(d);
    shell.last = p;
    const glm::vec3 dir = shell.dir;
    const bool enemy = e.team != 0;
    switch (shell.kind) {
    case ShotKind::Bullet:
    case ShotKind::Auto: {
        const glm::vec4 tracer = enemy ? glm::vec4{1.0f, 0.42f, 0.25f, 1.0f} : glm::vec4{1.0f, 0.8f, 0.4f, 1.0f};
        renderer_.drawStreak(Sprite::Trace0, p - dir * 1.4f, p, 0.16f, tracer, SpriteBlend::Add);
        renderer_.drawSprite(Sprite::Dot, p, 0.25f, 0.0f, tracer * 0.8f, SpriteBlend::Add);
        break;
    }
    case ShotKind::Shell:
        renderer_.drawStreak(Sprite::Trace1, p - dir * 0.9f, p, 0.22f, {1.0f, 0.65f, 0.3f, 1.0f}, SpriteBlend::Add);
        renderer_.drawSprite(Sprite::Flare, p, 0.6f, 0.0f, {1.0f, 0.9f, 0.7f, 1.0f}, SpriteBlend::Add);
        break;
    case ShotKind::Mortar:
        renderer_.draw(Mesh::Sphere, p, glm::vec3(0.16f), 0.0f, {0.15f, 0.15f, 0.14f, 1.0f}, Lit);
        shell.trail += frameDt_;
        if (shell.trail > 0.05f) {
            shell.trail = 0.0f;
            smokePuff(p, {0.0f, 0.1f, 0.0f}, 0.12f, 0.45f, 0.7f, {0.7f, 0.7f, 0.7f, 0.25f});
        }
        break;
    case ShotKind::Rocket:
    case ShotKind::Missile:
    case ShotKind::Ballistic: {
        const bool big = shell.kind == ShotKind::Ballistic;
        const Model* model = models_->model(big || shell.kind == ShotKind::Missile ? "proj_missile:hull" : "proj_rocket:hull");
        const float scale = big ? 1.4f : 0.7f;
        if (model) {
            const glm::vec3 up = std::abs(dir.y) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(dir, up));
            const glm::vec3 up2 = glm::cross(right, dir);
            const float m[16] = {right.x * scale, right.y * scale, right.z * scale, 0.0f, up2.x * scale, up2.y * scale, up2.z * scale, 0.0f,
                                 -dir.x * scale, -dir.y * scale, -dir.z * scale, 0.0f, p.x, p.y, p.z, 1.0f};
            renderer_.drawMesh(model->mesh, model->texture, model->mask, m, {1.0f, 1.0f, 1.0f, 1.0f}, Glow);
        }
        const glm::vec3 tail = p - dir * (big ? 1.2f : 0.45f);
        const float flicker = 0.8f + 0.2f * std::sin(static_cast<float>(now_) * 60.0f + static_cast<float>(e.id));
        renderer_.drawStreak(Sprite::Flame0, tail - dir * (big ? 1.6f : 0.7f) * flicker, tail, big ? 0.7f : 0.3f, {1.0f, 0.7f, 0.3f, 1.0f}, SpriteBlend::Add);
        renderer_.drawSprite(Sprite::Flare, tail, big ? 2.4f : 1.0f, 0.0f, {1.0f, 0.8f, 0.5f, 1.0f}, SpriteBlend::Add);
        shell.trail += frameDt_;
        const float every = big ? 0.02f : 0.03f;
        while (shell.trail > every) {
            shell.trail -= every;
            const glm::vec4 c = shell.kind == ShotKind::Missile ? glm::vec4{0.85f, 0.85f, 0.85f, 0.35f} : glm::vec4{0.62f, 0.6f, 0.58f, 0.45f};
            smokePuff(tail + fxDirection(-1.0f) * 0.05f, fxDirection(-1.0f) * 0.25f, big ? 0.6f : 0.25f, big ? 2.6f : 1.1f,
                      big ? fxRandom(3.0f, 5.0f) : fxRandom(1.4f, 2.4f), c);
        }
        break;
    }
    }
}

void App::muzzleFx(const net::InterpolatedEntity& shooter) {
    const auto& def = data_->unit(shooter.def);
    const glm::vec3 m = muzzleOf(shooter);
    const glm::vec3 fwd{std::cos(shooter.aim), 0.0f, std::sin(shooter.aim)};
    const ShotKind kind = shotKind(shooter.def);
    Particle flash{m, fwd, {1.0f, 0.82f, 0.45f, 1.0f}, 0.7f, 0.7f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Flash};
    switch (kind) {
    case ShotKind::Bullet:
    case ShotKind::Auto: {
        flash.sprite = fxRandom(0.0f, 1.0f) < 0.5f ? Sprite::Muzzle0 : Sprite::Muzzle1;
        addParticle(flash);
        const glm::vec3 side{-fwd.z, 0.0f, fwd.x};
        addParticle({m - fwd * 0.2f, side * fxRandom(1.0f, 2.0f) + glm::vec3(0.0f, fxRandom(1.0f, 2.0f), 0.0f), {0.9f, 0.7f, 0.3f, 1.0f}, 0.05f, 0.04f, 0.5f,
                     0.0f, 9.0f, 0.5f, 6.0f, ParticleKind::Debris});
        break;
    }
    case ShotKind::Shell: {
        flash.sprite = Sprite::Muzzle2;
        flash.size0 = flash.size1 = 1.6f;
        flash.life = 0.08f;
        addParticle(flash);
        Particle glow{m + fwd * 0.3f, {}, {1.0f, 0.8f, 0.4f, 1.0f}, 1.6f, 0.6f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow};
        glow.sprite = Sprite::Flare;
        addParticle(glow);
        for (int i = 0; i < fxCount(4.0f); ++i)
            smokePuff(m + fwd * 0.4f, fwd * fxRandom(1.0f, 2.5f) + fxDirection(0.2f) * 0.5f, 0.3f, fxRandom(1.0f, 1.6f), fxRandom(1.0f, 1.8f),
                      {0.62f, 0.6f, 0.58f, 0.5f});
        if (!def.structure) {
            Particle ring{{shooter.pos.x, 0.06f, shooter.pos.y}, {}, {0.7f, 0.66f, 0.6f, 0.35f}, def.radius, def.radius * 3.5f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f,
                          ParticleKind::Ring};
            ring.sprite = Sprite::SmokeRing;
            addParticle(ring);
        }
        break;
    }
    case ShotKind::Mortar: {
        Particle glow{m, {}, {1.0f, 0.8f, 0.4f, 1.0f}, 1.0f, 0.4f, 0.08f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow};
        glow.sprite = Sprite::Flare;
        addParticle(glow);
        for (int i = 0; i < fxCount(3.0f); ++i)
            smokePuff(m, glm::vec3(0.0f, fxRandom(1.0f, 2.0f), 0.0f) + fxDirection(0.0f) * 0.3f, 0.3f, 1.2f, fxRandom(1.0f, 1.6f), {0.7f, 0.7f, 0.68f, 0.45f});
        break;
    }
    case ShotKind::Rocket:
    case ShotKind::Missile:
    case ShotKind::Ballistic: {
        const bool big = kind == ShotKind::Ballistic;
        const glm::vec3 back = big ? glm::vec3(0.0f, -1.0f, 0.0f) : -fwd;
        Particle glow{m, {}, {1.0f, 0.75f, 0.4f, 1.0f}, big ? 4.0f : 1.4f, 0.5f, big ? 0.3f : 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, ParticleKind::Glow};
        glow.sprite = Sprite::Flare;
        addParticle(glow);
        for (int i = 0; i < fxCount(big ? 22.0f : 5.0f); ++i) {
            const glm::vec3 v = big ? fxDirection(-0.05f) * fxRandom(2.0f, 5.0f) * glm::vec3(1.0f, 0.15f, 1.0f) + glm::vec3(0.0f, fxRandom(0.3f, 3.0f), 0.0f)
                                    : back * fxRandom(1.5f, 3.5f) + fxDirection(0.0f) * 0.6f;
            smokePuff(big ? glm::vec3{shooter.pos.x, 0.5f, shooter.pos.y} : m, v, big ? 1.0f : 0.35f, big ? fxRandom(3.0f, 5.0f) : fxRandom(1.2f, 1.8f),
                      big ? fxRandom(3.0f, 6.0f) : fxRandom(1.0f, 1.8f), {0.75f, 0.74f, 0.72f, 0.5f});
        }
        break;
    }
    }
    recoil_[shooter.id] = 1.0f;
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
        if (p.t < 0.0f || p.kind == ParticleKind::Flash)
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
            if (p.sprite == Sprite::Dot && p.size0 < 0.3f)
                renderer_.drawGlow(Mesh::Sphere, p.pos, glm::vec3(size), p.color * (1.0f - k * k));
            else
                renderer_.drawSprite(p.sprite, p.pos, size * 1.2f, p.spin * p.t, p.color * (1.0f - k * k), SpriteBlend::Add);
            break;
        case ParticleKind::Fire: {
            const glm::vec4 c = glm::mix(glm::vec4(1.0f, 0.9f, 0.6f, 1.0f), p.color, std::min(1.0f, k * 1.6f)) * (1.0f - k);
            renderer_.drawSprite(p.sprite, p.pos, size, p.spin * p.t, c, SpriteBlend::Add);
            break;
        }
        case ParticleKind::Spark: {
            const glm::vec3 v = p.vel * 0.035f;
            renderer_.drawStreak(Sprite::Trace0, p.pos - v, p.pos, size * 2.0f, p.color * (1.0f - k), SpriteBlend::Add);
            break;
        }
        case ParticleKind::Flash:
            renderer_.drawStreak(p.sprite, p.pos - p.vel * (size * 0.15f), p.pos + p.vel * size, size * 0.5f, p.color * (1.0f - k * 0.5f), SpriteBlend::Add);
            break;
        case ParticleKind::Ring: {
            glm::vec4 c = p.color;
            c.a *= 1.0f - k;
            renderer_.drawDecal(p.sprite, p.pos, size, p.spin, c, SpriteBlend::Alpha);
            break;
        }
        case ParticleKind::Shard:
            renderer_.drawGlow(Mesh::Cube, p.pos, glm::vec3(size), p.color * (1.0f - k));
            break;
        case ParticleKind::Smoke: {
            glm::vec4 c = p.color;
            c.a *= (1.0f - k) * std::min(1.0f, p.t * 6.0f);
            renderer_.drawSprite(p.sprite, p.pos, size * 1.4f, p.spin * p.t + static_cast<float>(&p - particles_.data()) * 0.7f, c, SpriteBlend::Alpha);
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
            renderer_.drawDecal(p.sprite == Sprite::Dot ? Sprite::Dirt1 : p.sprite, p.pos, size * 1.4f, p.spin, c, SpriteBlend::Alpha);
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
    const bool voidSide = def.armorType == ArmorType::Crystal || def.id.rfind("void", 0) == 0;
    spawnExplosion({d.pos.x, lift, d.pos.y}, def.radius * 2.0f, def.structure, voidSide, hull);
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

void App::updateFocus() {
    std::vector<glm::vec4> points;
    if (!map_ || map_->buildings().empty()) {
        renderer_.setFocus(points);
        return;
    }
    if (const auto ground = camera_.screenToGround(mouse_.x, mouse_.y))
        points.push_back({ground->x, 0.5f, ground->y, 3.0f});
    std::vector<std::pair<float, glm::vec4>> units;
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || data_->unit(e.def).structure || hidden(e))
            continue;
        const bool mine = isOwn(e);
        const float d = glm::distance(e.pos, camera_.target) - (mine ? 1000.0f : 0.0f) - (std::find(selected_.begin(), selected_.end(), e.id) != selected_.end() ? 2000.0f : 0.0f);
        units.push_back({d, glm::vec4(e.pos.x, 0.6f + liftOf(e), e.pos.y, 1.6f + data_->unit(e.def).radius)});
    }
    std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [d, p] : units) {
        if (points.size() >= static_cast<std::size_t>(Renderer::kFocusPoints))
            break;
        points.push_back(p);
    }
    renderer_.setFocus(points);
}

}
