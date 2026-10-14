#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "core/World.h"

namespace vw {

namespace {

struct Scaling {
    float hp;
    float damage;
    float count;
};

constexpr Scaling kScaling[] = {
    {0.7f, 0.7f, 0.75f},
    {1.0f, 1.0f, 1.0f},
    {1.35f, 1.25f, 1.25f},
    {1.8f, 1.5f, 1.5f},
};

constexpr float kCaptureRate = 0.05f;
constexpr int kCaptureCap = 4;

const Scaling& scaling(Difficulty d) {
    return kScaling[static_cast<std::size_t>(d)];
}

}

const char* difficultyName(Difficulty d) {
    switch (d) {
    case Difficulty::Easy:
        return "easy";
    case Difficulty::Normal:
        return "normal";
    case Difficulty::Hard:
        return "hard";
    case Difficulty::Nightmare:
        return "nightmare";
    }
    return "normal";
}

bool parseDifficulty(std::string_view text, Difficulty& out) {
    for (const auto d : {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard, Difficulty::Nightmare})
        if (text == difficultyName(d)) {
            out = d;
            return true;
        }
    return false;
}

const char* gameModeName(GameMode m) {
    return m == GameMode::Endless ? "endless" : "defense";
}

bool parseGameMode(std::string_view text, GameMode& out) {
    for (const auto m : {GameMode::Defense, GameMode::Endless})
        if (text == gameModeName(m)) {
            out = m;
            return true;
        }
    return false;
}

int World::activePlayers() const {
    return static_cast<int>(std::count_if(players_.begin(), players_.end(), [](const PlayerState& p) { return p.active; }));
}

float World::endlessCycle() const {
    const auto waves = map_ ? static_cast<int>(map_->defense().waves.size()) : 0;
    return mode_ == GameMode::Endless && waves > 0 ? static_cast<float>(std::max(0, defense_.wave - 1) / waves) : 0.0f;
}

float World::enemyHpScale() const {
    return scaling(difficulty_).hp * (1.0f + 0.1f * static_cast<float>(std::max(0, activePlayers() - 1))) * (1.0f + 0.4f * endlessCycle());
}

float World::enemyDamageScale() const {
    return scaling(difficulty_).damage * (1.0f + 0.25f * endlessCycle());
}

float World::enemyCountScale() const {
    return scaling(difficulty_).count * (1.0f + 0.25f * static_cast<float>(std::max(0, activePlayers() - 1))) * (1.0f + 0.2f * endlessCycle());
}

void World::grantTeamEnergy(float amount) {
    const int n = activePlayers();
    if (n == 0)
        return;
    const float share = amount / static_cast<float>(n);
    for (auto& p : players_)
        if (p.active)
            p.energy += share;
}

void World::handle(const RecruitCommand& cmd) {
    if (!map_ || cmd.player >= kMaxPlayers || cmd.unit >= data_->units().size())
        return;
    auto& player = players_[cmd.player];
    const auto& def = data_->unit(cmd.unit);
    if (!player.active || def.cost <= 0.0f || player.energy < def.cost || defense_.state != MatchState::Playing)
        return;
    player.energy -= def.cost;
    const int slot = recruitCount_++ % 9;
    const glm::vec2 offset{static_cast<float>(slot % 3 - 1) * 1.5f, static_cast<float>(slot / 3 - 1) * 1.5f};
    const glm::vec2 pos = Map::center(nearestFree(*map_, Map::cellOf(playerBase(cmd.player) + offset)));
    spawn(cmd.unit, 0, pos, cmd.player);
}

void World::updateBosses() {
    struct Summon {
        UnitDefId unit;
        int count;
        glm::vec2 pos;
        std::uint8_t team;
        LaneFollower lane;
        bool hasLane;
    };
    std::vector<Summon> summons;
    std::vector<entt::entity> enraged;

    for (auto [e, pos, boss, health, unit, team, vel] : registry_.view<const Position, Boss, const Health, const Unit, const Team, Velocity>().each()) {
        const auto& def = *data_->unit(unit.def).boss;

        if (!boss.enraged && def.enrage && health.hp < health.max * def.enrage->hpBelow) {
            boss.enraged = true;
            enraged.push_back(e);
            events_.push_back({EventType::Enrage, e, entt::null, pos.value, pos.value});
        }

        if (boss.casting >= 0) {
            vel.value = {};
            boss.castTimer -= kTickDt;
            if (boss.castTimer > 0.0f)
                continue;
            const auto& ability = def.abilities[static_cast<std::size_t>(boss.casting)];
            const float dmg = ability.damage * enemyDamageScale() * (boss.enraged && def.enrage ? def.enrage->damage : 1.0f);
            std::vector<entt::entity> hits;
            queryAll(boss.castPos, ability.radius, [&](const SpatialHash::Item& item) {
                if (item.team == team.id || !alive(item.entity))
                    return;
                if (glm::distance(item.pos, boss.castPos) - item.radius <= ability.radius)
                    hits.push_back(item.entity);
            });
            for (const auto h : hits)
                damage(h, dmg, -1, e);
            events_.push_back({EventType::Slam, e, entt::null, boss.castPos, boss.castPos, ability.radius});
            boss.casting = -1;
            continue;
        }

        for (std::size_t i = 0; i < def.abilities.size(); ++i) {
            auto& cd = boss.cooldowns[i];
            cd -= kTickDt;
            if (cd > 0.0f)
                continue;
            const auto& ability = def.abilities[i];
            if (ability.type == AbilityType::Slam) {
                if (findEnemy(pos.value, team.id, ability.radius * 0.8f) == entt::null)
                    continue;
                boss.casting = static_cast<int>(i);
                boss.castTimer = ability.windup;
                boss.castPos = pos.value;
                vel.value = {};
                cd = ability.cooldown;
                events_.push_back({EventType::SlamWarn, e, entt::null, pos.value, pos.value, ability.radius});
                break;
            }
            if (findEnemy(pos.value, team.id, unit.sight) == entt::null)
                continue;
            const auto* lane = registry_.try_get<LaneFollower>(e);
            summons.push_back({ability.unitId, ability.count, pos.value, team.id, lane ? *lane : LaneFollower{}, lane != nullptr});
            cd = ability.cooldown;
            events_.push_back({EventType::Summon, e, entt::null, pos.value, pos.value});
        }
    }

    for (const auto e : enraged)
        refreshStats(e);

    for (const auto& s : summons)
        for (int i = 0; i < s.count; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(std::max(1, s.count)) * 6.2831853f;
            const glm::vec2 p = s.pos + glm::vec2(std::cos(a), std::sin(a)) * 2.5f;
            const auto u = spawn(s.unit, s.team, map_ ? Map::center(nearestFree(*map_, Map::cellOf(p))) : p);
            if (s.hasLane)
                registry_.emplace<LaneFollower>(u, s.lane);
        }
}

void World::updateResourcePoints() {
    if (!map_)
        return;
    const auto& defs = map_->resourcePoints();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const auto& def = defs[i];
        auto& st = points_[i];
        int counts[2] = {0, 0};
        spatial_.query(def.pos, def.radius, [&](const SpatialHash::Item& item) {
            if (glm::distance(item.pos, def.pos) <= def.radius && alive(item.entity))
                ++counts[item.team & 1];
        });

        if (counts[0] > 0 && counts[1] == 0)
            st.control += kCaptureRate * static_cast<float>(std::min(counts[0], kCaptureCap)) * kTickDt;
        else if (counts[1] > 0 && counts[0] == 0)
            st.control -= kCaptureRate * static_cast<float>(std::min(counts[1], kCaptureCap)) * kTickDt;
        st.control = std::clamp(st.control, -1.0f, 1.0f);

        int owner = st.owner;
        if (st.control >= 1.0f)
            owner = 0;
        else if (st.control <= -1.0f)
            owner = 1;
        else if ((st.owner == 0 && st.control <= 0.0f) || (st.owner == 1 && st.control >= 0.0f))
            owner = -1;
        if (owner != st.owner) {
            st.owner = owner;
            events_.push_back({EventType::PointCaptured, entt::null, entt::null, def.pos, def.pos, static_cast<float>(owner)});
        }

        if (st.owner == 0)
            grantTeamEnergy(def.income * kTickDt);
    }
}

void World::spawnWave(const WaveDef& wave) {
    const float countScale = enemyCountScale();
    for (const auto& group : wave.groups) {
        const UnitDefId id = data_->find(group.unit);
        if (id == kInvalidUnitDef)
            continue;
        const int count = std::max(1, static_cast<int>(std::lround(static_cast<float>(group.count) * countScale)));
        if (!group.raid) {
            for (const int lane : group.lanes)
                spawnOnLane(id, 1, lane, count, 5.0f);
            continue;
        }

        const auto& defs = map_->resourcePoints();
        if (defs.empty() || !alive(portal_))
            continue;
        const glm::vec2 from = registry_.get<Position>(portal_).value;
        std::size_t target = static_cast<std::size_t>(defense_.wave) % defs.size();
        float best = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < defs.size(); ++i)
            if (points_[i].owner == 0 && glm::distance(defs[i].pos, from) < best) {
                best = glm::distance(defs[i].pos, from);
                target = i;
            }
        const glm::vec2 dir = glm::normalize(defs[target].pos - from);
        const float startOffset = registry_.get<Unit>(portal_).radius + 2.0f;
        std::vector<entt::entity> raiders;
        for (int i = 0; i < count; ++i) {
            const glm::vec2 side(-dir.y, dir.x);
            const glm::vec2 p = from + dir * (startOffset + static_cast<float>(i / 4)) + side * (static_cast<float>(i % 4) - 1.5f);
            raiders.push_back(spawn(id, 1, Map::center(nearestFree(*map_, Map::cellOf(p)))));
        }
        apply(MoveCommand{raiders, defs[target].pos, true});
    }

    if (!wave.boss.empty()) {
        const UnitDefId id = data_->find(wave.boss);
        if (id != kInvalidUnitDef) {
            const auto spawned = spawnOnLane(id, 1, wave.bossLane, 1, 6.0f);
            defense_.boss = spawned.front();
        }
    }
}

void World::updateDefense() {
    if (!map_ || !map_->defense().enabled || defense_.state != MatchState::Playing)
        return;
    const auto& def = map_->defense();

    if (defense_.boss != entt::null && !alive(defense_.boss))
        defense_.boss = entt::null;

    if (core_ != entt::null && !alive(core_)) {
        defense_.state = MatchState::Defeat;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 0.0f});
        return;
    }

    if (portal_ != entt::null && !alive(portal_)) {
        defense_.state = MatchState::Victory;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 1.0f});
        return;
    }

    float income = def.baseIncome;
    for (auto [e, unit, team] : registry_.view<const Unit, const Team, const Structure>().each())
        if (team.id == 0 && alive(e))
            income += data_->unit(unit.def).income;
    grantTeamEnergy(income * kTickDt);

    if (mode_ == GameMode::Endless && !def.waves.empty()) {
        defense_.nextWaveIn -= kTickDt;
        if (defense_.nextWaveIn <= 0.0f) {
            const auto index = static_cast<std::size_t>(defense_.wave) % def.waves.size();
            ++defense_.wave;
            spawnWave(def.waves[index]);
            events_.push_back({EventType::WaveStart, entt::null, entt::null, {}, {}, static_cast<float>(defense_.wave)});
            const float delay = def.waves[(index + 1) % def.waves.size()].delay;
            defense_.nextWaveIn = delay > 0.0f ? delay : def.waveInterval;
        }
        return;
    }

    if (defense_.wave < defense_.totalWaves) {
        defense_.nextWaveIn -= kTickDt;
        if (defense_.nextWaveIn <= 0.0f) {
            spawnWave(def.waves[static_cast<std::size_t>(defense_.wave)]);
            ++defense_.wave;
            events_.push_back({EventType::WaveStart, entt::null, entt::null, {}, {}, static_cast<float>(defense_.wave)});
            if (defense_.wave == defense_.totalWaves && portal_ != entt::null)
                registry_.remove<Invulnerable>(portal_);
            if (defense_.wave < defense_.totalWaves) {
                const float delay = def.waves[static_cast<std::size_t>(defense_.wave)].delay;
                defense_.nextWaveIn = delay > 0.0f ? delay : def.waveInterval;
            } else {
                defense_.nextWaveIn = 0.0f;
            }
        }
        return;
    }

    bool enemiesLeft = false;
    for (auto [e, team] : registry_.view<const Team>(entt::exclude<Structure>).each())
        if (team.id == 1 && alive(e)) {
            enemiesLeft = true;
            break;
        }
    if (!enemiesLeft) {
        defense_.state = MatchState::Victory;
        events_.push_back({EventType::MatchEnd, entt::null, entt::null, {}, {}, 1.0f});
    }
}

}
