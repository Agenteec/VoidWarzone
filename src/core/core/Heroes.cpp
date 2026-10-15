#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

#include "core/Movement.h"
#include "core/World.h"

namespace vw {

namespace {

constexpr float kXpRadius = 10.0f;
constexpr float kAuraDuration = 0.6f;
constexpr int kAuraPeriod = kTickRate / 2;

StatMods rankMods(const Progression& p) {
    StatMods m;
    switch (p.rank) {
    case Rank::Recruit:
        break;
    case Rank::Veteran:
        m.hpMul = 1.15f;
        m.damageMul = 1.15f;
        break;
    case Rank::Elite:
        m.hpMul = 1.3f;
        m.damageMul = 1.3f;
        m.armor = 1.0f;
        m.regen = 1.0f;
        break;
    case Rank::Hero: {
        const float lv = static_cast<float>(std::max(0, p.level - 1));
        m.hpMul = 1.5f * (1.0f + 0.06f * lv);
        m.damageMul = 1.4f * (1.0f + 0.05f * lv);
        m.armor = 2.0f + 0.3f * lv;
        m.regen = 2.0f + 0.5f * lv;
        break;
    }
    }
    return m;
}

float levelScale(std::uint8_t level) {
    return 1.0f + 0.5f * static_cast<float>(level - 1);
}

}

int itemSlots(Rank rank) {
    switch (rank) {
    case Rank::Elite:
        return 1;
    case Rank::Hero:
        return 6;
    default:
        return 0;
    }
}

float xpForNextLevel(int level) {
    return 100.0f * static_cast<float>(level);
}

void World::devBoost(std::uint8_t player) {
    if (player >= kMaxPlayers)
        return;
    players_[player].energy += 5000.0f;
    players_[player].shards += 20;
    for (auto [e, owner, prog] : registry_.view<const Owner, Progression>().each())
        if (owner.player == player)
            prog.xp = std::max(prog.xp, kHeroXp);
}

int World::heroCount(std::uint8_t player) const {
    int n = 0;
    for (auto [e, owner, prog] : registry_.view<const Owner, const Progression>().each())
        n += owner.player == player && prog.rank == Rank::Hero;
    for (const auto& r : respawns_)
        n += r.player == player;
    return n;
}

void World::refreshStats(entt::entity e) {
    const auto* base = registry_.try_get<BaseStats>(e);
    if (!base)
        return;

    StatMods mods;
    if (const auto* prog = registry_.try_get<Progression>(e)) {
        mods.add(rankMods(*prog));
        for (const auto item : prog->items)
            mods.add(data_->item(item).mods);
    }
    float shieldMax = data_->unit(registry_.get<Unit>(e).def).shield;
    if (const auto* modules = registry_.try_get<Modules>(e))
        for (const auto m : modules->slots)
            if (m < data_->modules().size()) {
                mods.add(data_->module(m).mods);
                shieldMax += data_->module(m).shield;
            }
    if (const auto* buffs = registry_.try_get<Buffs>(e))
        for (const auto& b : buffs->list)
            mods.add(b.mods);
    if (const auto* boss = registry_.try_get<Boss>(e); boss && boss->enraged) {
        const auto& def = data_->unit(registry_.get<Unit>(e).def);
        if (def.boss && def.boss->enrage) {
            mods.speedMul *= def.boss->enrage->speed;
            mods.damageMul *= def.boss->enrage->damage;
            mods.attackSpeedMul /= std::max(0.05f, def.boss->enrage->cooldown);
        }
    }

    auto& health = registry_.get<Health>(e);
    const float ratio = health.max > 0.0f ? health.hp / health.max : 1.0f;
    health.max = std::max(1.0f, (base->maxHp + mods.hp) * mods.hpMul);
    health.hp = std::min(health.max, std::max(health.hp > 0.0f ? 1.0f : 0.0f, ratio * health.max));
    health.armor = base->armor + mods.armor;
    if (shieldMax != health.shieldMax) {
        health.shield = std::min(health.shield + std::max(0.0f, shieldMax - health.shieldMax), shieldMax);
        health.shieldMax = shieldMax;
    }

    registry_.get<Unit>(e).speed = base->speed * std::max(0.1f, mods.speedMul);
    if (auto* w = registry_.try_get<Weapon>(e)) {
        w->def.damage = base->damage * mods.damageMul;
        w->def.cooldown = base->cooldown / std::max(0.1f, mods.attackSpeedMul);
        w->def.range = base->range + mods.range;
    }
    if (mods.regen > 0.0f)
        registry_.emplace_or_replace<Regen>(e, mods.regen);
    else
        registry_.remove<Regen>(e);
}

void World::grantXp(entt::entity e, float amount) {
    auto* prog = registry_.try_get<Progression>(e);
    if (!prog)
        return;
    prog->xp += amount;
    bool changed = false;
    if (prog->rank == Rank::Recruit && prog->xp >= kVeteranXp) {
        prog->rank = Rank::Veteran;
        changed = true;
        events_.push_back({EventType::Promote, e, entt::null, registry_.get<Position>(e).value, registry_.get<Position>(e).value, static_cast<float>(Rank::Veteran)});
    }
    while (prog->rank == Rank::Hero && prog->level < kMaxHeroLevel && prog->xp >= xpForNextLevel(prog->level)) {
        prog->xp -= xpForNextLevel(prog->level);
        ++prog->level;
        ++prog->skillPoints;
        changed = true;
        events_.push_back({EventType::LevelUp, e, entt::null, registry_.get<Position>(e).value, registry_.get<Position>(e).value, static_cast<float>(prog->level)});
    }
    if (changed)
        refreshStats(e);
}

std::vector<entt::entity> World::unitsInRadius(glm::vec2 center, float radius, std::uint8_t team, bool sameTeam) const {
    std::vector<entt::entity> out;
    for (auto [e, pos, unit, t, health] : registry_.view<const Position, const Unit, const Team, const Health>(entt::exclude<Structure>).each())
        if (health.hp > 0.0f && (t.id == team) == sameTeam && glm::distance(pos.value, center) - unit.radius <= radius)
            out.push_back(e);
    return out;
}

void World::onEnemyKilled(const UnitDef& def, glm::vec2 pos, int lastHitBy) {
    const bool byPlayer = lastHitBy >= 0 && players_[static_cast<std::size_t>(lastHitBy)].active;
    if (def.bounty > 0.0f) {
        if (byPlayer) {
            auto& p = players_[static_cast<std::size_t>(lastHitBy)];
            p.energy += def.bounty;
            ++p.kills;
        } else {
            grantTeamEnergy(def.bounty * 0.5f);
        }
    }

    int receiver = byPlayer ? lastHitBy : -1;
    if (receiver < 0 && (def.shards > 0 || !def.loot.empty())) {
        int best = std::numeric_limits<int>::max();
        for (int i = 0; i < kMaxPlayers; ++i)
            if (players_[static_cast<std::size_t>(i)].active && players_[static_cast<std::size_t>(i)].shards < best) {
                best = players_[static_cast<std::size_t>(i)].shards;
                receiver = i;
            }
    }
    if (receiver >= 0) {
        auto& p = players_[static_cast<std::size_t>(receiver)];
        p.shards += def.shards;
        if (!def.loot.empty() && p.stash.size() < kStashSize) {
            const ItemId item = def.loot[static_cast<std::size_t>(tick_ % def.loot.size())];
            p.stash.push_back(item);
            events_.push_back({EventType::Loot, entt::null, entt::null, pos, pos, static_cast<float>(item)});
        }
    }

    const float xp = 8.0f + def.bounty * 1.5f;
    for (auto [e, position, prog] : registry_.view<const Position, Progression>().each())
        if (glm::distance(position.value, pos) <= kXpRadius && alive(e))
            grantXp(e, xp);
}

Progression* World::ownedProgression(std::uint8_t player, entt::entity e) {
    if (!alive(e))
        return nullptr;
    const auto* owner = registry_.try_get<Owner>(e);
    if (!owner || owner->player != player)
        return nullptr;
    return registry_.try_get<Progression>(e);
}

void World::handle(const PromoteCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || cmd.player >= kMaxPlayers)
        return;
    auto& player = players_[cmd.player];
    if (prog->rank == Rank::Recruit || prog->rank == Rank::Veteran) {
        if (prog->xp < kEliteXp || player.energy < kEliteCost)
            return;
        player.energy -= kEliteCost;
        prog->rank = Rank::Elite;
    } else if (prog->rank == Rank::Elite) {
        if (prog->xp < kHeroXp || player.shards < kHeroShards || heroCount(cmd.player) >= kMaxHeroes ||
            data_->unit(registry_.get<Unit>(cmd.unit).def).skills.empty())
            return;
        player.shards -= kHeroShards;
        prog->rank = Rank::Hero;
        prog->level = 1;
        prog->xp = 0.0f;
        prog->skillPoints = 1;
    } else {
        return;
    }
    refreshStats(cmd.unit);
    auto& health = registry_.get<Health>(cmd.unit);
    health.hp = health.max;
    events_.push_back({EventType::Promote, cmd.unit, entt::null, registry_.get<Position>(cmd.unit).value, registry_.get<Position>(cmd.unit).value, static_cast<float>(prog->rank)});
}

void World::handle(const RefitCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || cmd.player >= kMaxPlayers || cmd.variant >= data_->units().size() || prog->rank < Rank::Elite)
        return;
    auto& unit = registry_.get<Unit>(cmd.unit);
    const auto& variant = data_->unit(cmd.variant);
    if (cmd.variant == unit.def || variant.refitFrom == kInvalidUnitDef || variant.refitFrom != data_->baseOf(unit.def))
        return;
    auto& player = players_[cmd.player];
    if (player.energy < variant.refitCost)
        return;
    player.energy -= variant.refitCost;

    unit.def = cmd.variant;
    unit.radius = variant.radius;
    unit.sight = variant.sight;
    auto& base = registry_.get<BaseStats>(cmd.unit);
    base = {variant.maxHp, variant.armor, variant.speed};
    if (variant.weapon) {
        base.damage = variant.weapon->damage;
        base.cooldown = variant.weapon->cooldown;
        base.range = variant.weapon->range;
        registry_.emplace_or_replace<Weapon>(cmd.unit, *variant.weapon, 0.0f);
        if (!registry_.all_of<Turret>(cmd.unit))
            registry_.emplace<Turret>(cmd.unit, Turret{registry_.get<Facing>(cmd.unit).angle, 0.0f});
    } else {
        registry_.remove<Weapon>(cmd.unit);
        registry_.remove<Turret>(cmd.unit);
    }
    auto& health = registry_.get<Health>(cmd.unit);
    health.shieldMax = variant.shield;
    health.shield = std::min(health.shield, health.shieldMax);
    refreshStats(cmd.unit);
    const glm::vec2 pos = registry_.get<Position>(cmd.unit).value;
    events_.push_back({EventType::Promote, cmd.unit, entt::null, pos, pos, static_cast<float>(prog->rank)});
}

void World::handle(const ModuleCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    auto* modules = registry_.try_get<Modules>(cmd.unit);
    if (!prog || !modules || cmd.player >= kMaxPlayers || cmd.module >= data_->modules().size())
        return;
    const auto& def = data_->module(cmd.module);
    const int slot = static_cast<int>(def.slot);
    const Rank need = def.slot == ModuleSlot::Armor ? Rank::Veteran : Rank::Elite;
    if (prog->rank < need || modules->slots[static_cast<std::size_t>(slot)] == cmd.module)
        return;
    auto& player = players_[cmd.player];
    if (player.energy < def.cost)
        return;
    player.energy -= def.cost;
    modules->slots[static_cast<std::size_t>(slot)] = cmd.module;
    if (def.slot == ModuleSlot::Weapon) {
        if (def.weapon)
            registry_.emplace_or_replace<SecondaryWeapon>(cmd.unit, SecondaryWeapon{*def.weapon, 0.0f, entt::null});
        else
            registry_.remove<SecondaryWeapon>(cmd.unit);
    }
    refreshStats(cmd.unit);
}

void World::updateStrikes() {
    for (auto& s : strikes_)
        s.timer -= kTickDt;
    std::vector<Strike> due;
    std::erase_if(strikes_, [&](const Strike& s) {
        if (s.timer > 0.0f)
            return false;
        due.push_back(s);
        return true;
    });
    for (const auto& s : due) {
        std::vector<entt::entity> hits;
        for (auto [e, pos, unit, t] : registry_.view<const Position, const Unit, const Team>(entt::exclude<Carried, Air>).each())
            if (t.id != s.team && alive(e) && !registry_.all_of<Invulnerable>(e) && glm::distance(pos.value, s.pos) - unit.radius <= s.radius)
                hits.push_back(e);
        for (const auto e : hits)
            damage(e, s.amount, s.owner, registry_.valid(s.source) ? s.source : entt::null, s.type);
        events_.push_back({EventType::Slam, entt::null, entt::null, s.pos, s.pos, s.radius});
    }
}

void World::updateRepair() {
    if (tick_ % kAuraPeriod != 0)
        return;
    for (auto [e, pos, unit, team] : registry_.view<const Position, const Unit, const Team>().each()) {
        const auto& def = data_->unit(unit.def);
        if (def.repair <= 0.0f || !alive(e))
            continue;
        entt::entity best = entt::null;
        float worst = 1.0f;
        for (auto [o, op, ou, ot, oh] : registry_.view<const Position, const Unit, const Team, const Health>(entt::exclude<Construction>).each()) {
            if (o == e || ot.id != team.id || oh.hp <= 0.0f || oh.hp >= oh.max ||
                glm::distance(op.value, pos.value) - ou.radius > def.repairRange + unit.radius)
                continue;
            if (oh.hp / oh.max < worst) {
                worst = oh.hp / oh.max;
                best = o;
            }
        }
        if (best == entt::null)
            continue;
        auto& h = registry_.get<Health>(best);
        h.hp = std::min(h.max, h.hp + def.repair * static_cast<float>(kAuraPeriod) * kTickDt);
    }
}

void World::handle(const LearnCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || prog->rank != Rank::Hero || prog->skillPoints <= 0)
        return;
    const auto& pool = data_->unit(registry_.get<Unit>(cmd.unit).def).skills;
    if (std::find(pool.begin(), pool.end(), cmd.skill) == pool.end())
        return;
    if (prog->level < data_->skill(cmd.skill).minLevel)
        return;
    auto it = std::find_if(prog->skills.begin(), prog->skills.end(), [&](const SkillSlot& s) { return s.skill == cmd.skill; });
    if (it != prog->skills.end()) {
        if (it->level >= (data_->skill(cmd.skill).ultimate ? 1 : kMaxSkillLevel))
            return;
        ++it->level;
    } else {
        if (prog->skills.size() >= kMaxSkills)
            return;
        prog->skills.push_back({cmd.skill, 1, 0.0f});
    }
    --prog->skillPoints;
}

void World::handle(const CastCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || cmd.slot >= prog->skills.size() || !std::isfinite(cmd.point.x) || !std::isfinite(cmd.point.y))
        return;
    auto& slot = prog->skills[cmd.slot];
    const auto& skill = data_->skill(slot.skill);
    if (skill.passive || slot.cooldown > 0.0f)
        return;

    const glm::vec2 pos = registry_.get<Position>(cmd.unit).value;
    glm::vec2 center = pos;
    if (skill.target == SkillTarget::Point) {
        const glm::vec2 d = cmd.point - pos;
        const float len = glm::length(d);
        center = len > skill.range && len > 1e-4f ? pos + d / len * skill.range : cmd.point;
    }

    slot.cooldown = skill.cooldown * (1.0f - 0.1f * static_cast<float>(slot.level - 1));
    const SkillId id = slot.skill;
    const std::uint8_t level = slot.level;
    for (const auto& fx : skill.effects)
        applySkillEffect(cmd.unit, fx, level, center, id);
    events_.push_back({EventType::SkillCast, cmd.unit, entt::null, pos, center, static_cast<float>(id)});
}

void World::applySkillEffect(entt::entity caster, const SkillEffect& fx, std::uint8_t level, glm::vec2 center, SkillId skill) {
    const std::uint8_t team = registry_.get<Team>(caster).id;
    const auto* owner = registry_.try_get<Owner>(caster);
    const int player = owner ? owner->player : -1;
    const float scale = levelScale(level);
    const auto* prog = registry_.try_get<Progression>(caster);
    const float power = 1.0f + 0.05f * static_cast<float>(prog ? std::max(0, prog->level - 1) : 0);

    auto targets = [&]() {
        if (fx.who == EffectWho::Self || fx.radius <= 0.0f)
            return std::vector<entt::entity>{caster};
        return unitsInRadius(center, fx.radius, team, fx.who == EffectWho::Allies);
    };

    switch (fx.type) {
    case EffectType::Damage:
        if (fx.delay > 0.0f) {
            strikes_.push_back({center, fx.radius, fx.amount * scale * power, fx.damageType, team, player, caster, fx.delay});
            events_.push_back({EventType::SlamWarn, caster, entt::null, center, center, fx.radius});
            break;
        }
        for (const auto t : unitsInRadius(center, fx.radius, team, false))
            if (!registry_.all_of<Invulnerable>(t))
                damage(t, fx.amount * scale * power, player, caster, fx.damageType);
        break;
    case EffectType::Heal:
        for (const auto t : unitsInRadius(center, fx.radius, team, true)) {
            auto& h = registry_.get<Health>(t);
            h.hp = std::min(h.max, h.hp + fx.amount * scale * power);
        }
        break;
    case EffectType::Buff:
        for (const auto t : targets()) {
            ActiveBuff buff;
            buff.mods.add(fx.mods, scale);
            buff.remaining = fx.duration * (1.0f + 0.25f * static_cast<float>(level - 1));
            buff.skill = skill;
            buff.source = caster;
            auto& list = registry_.get_or_emplace<Buffs>(t).list;
            std::erase_if(list, [&](const ActiveBuff& b) { return b.skill == skill && b.source == caster; });
            list.push_back(buff);
            refreshStats(t);
        }
        break;
    case EffectType::Dash: {
        const auto& unit = registry_.get<Unit>(caster);
        glm::vec2 to = center;
        if (map_)
            to = Map::center(nearestFree(*map_, Map::cellOf(center)));
        registry_.get<Position>(caster).value = to;
        if (map_)
            pushOutOfWalls(*map_, registry_.get<Position>(caster).value, unit.radius);
        registry_.remove<Navigator>(caster);
        if (auto* order = registry_.try_get<Order>(caster); order && order->type != OrderType::Attack && order->type != OrderType::Patrol &&
                                                            order->type != OrderType::Hold && order->type != OrderType::Follow &&
                                                            order->type != OrderType::Build)
            finishOrder(caster, *order);
        break;
    }
    case EffectType::Summon: {
        const glm::vec2 pos = registry_.get<Position>(caster).value;
        const int count = fx.count + (level - 1);
        for (int i = 0; i < count; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(std::max(1, count)) * 6.2831853f;
            glm::vec2 p = pos + glm::vec2(std::cos(a), std::sin(a)) * 1.5f;
            if (map_)
                p = Map::center(nearestFree(*map_, Map::cellOf(p)));
            const auto u = spawn(fx.unit, team, p, player);
            registry_.remove<Progression>(u);
            registry_.emplace<Summoned>(u, fx.duration * (1.0f + 0.25f * static_cast<float>(level - 1)));
        }
        break;
    }
    case EffectType::Aura:
        break;
    }
}

void World::handle(const BuyCommand& cmd) {
    if (cmd.player >= kMaxPlayers || cmd.item >= data_->items().size())
        return;
    const auto& shop = data_->shop();
    if (std::find(shop.begin(), shop.end(), cmd.item) == shop.end())
        return;
    auto& player = players_[cmd.player];
    const auto& item = data_->item(cmd.item);
    if (!player.active || player.stash.size() >= kStashSize || player.energy < item.cost || player.shards < item.shards)
        return;
    player.energy -= item.cost;
    player.shards -= item.shards;
    player.stash.push_back(cmd.item);
}

void World::handle(const EquipCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || cmd.player >= kMaxPlayers)
        return;
    auto& stash = players_[cmd.player].stash;
    if (cmd.stashIndex >= stash.size() || static_cast<int>(prog->items.size()) >= itemSlots(prog->rank))
        return;
    prog->items.push_back(stash[cmd.stashIndex]);
    stash.erase(stash.begin() + cmd.stashIndex);
    refreshStats(cmd.unit);
}

void World::handle(const UnequipCommand& cmd) {
    auto* prog = ownedProgression(cmd.player, cmd.unit);
    if (!prog || cmd.player >= kMaxPlayers || cmd.slot >= prog->items.size())
        return;
    auto& stash = players_[cmd.player].stash;
    if (stash.size() >= kStashSize)
        return;
    stash.push_back(prog->items[cmd.slot]);
    prog->items.erase(prog->items.begin() + cmd.slot);
    refreshStats(cmd.unit);
}

void World::updateHeroes() {
    std::vector<entt::entity> dirty;

    for (auto [e, buffs] : registry_.view<Buffs>().each()) {
        const auto before = buffs.list.size();
        for (auto& b : buffs.list)
            b.remaining -= kTickDt;
        std::erase_if(buffs.list, [](const ActiveBuff& b) { return b.remaining <= 0.0f; });
        if (buffs.list.size() != before)
            dirty.push_back(e);
    }
    for (const auto e : dirty)
        refreshStats(e);

    for (auto [e, prog] : registry_.view<Progression>().each())
        for (auto& s : prog.skills)
            s.cooldown = std::max(0.0f, s.cooldown - kTickDt);

    if (tick_ % kAuraPeriod == 0) {
        struct Aura {
            entt::entity source;
            SkillId skill;
            std::uint8_t level;
            glm::vec2 pos;
        };
        std::vector<Aura> auras;
        for (auto [e, prog, pos] : registry_.view<const Progression, const Position>().each())
            for (const auto& s : prog.skills)
                if (data_->skill(s.skill).passive && alive(e))
                    auras.push_back({e, s.skill, s.level, pos.value});
        for (auto [e, unit, pos] : registry_.view<const Unit, const Position>().each())
            if (alive(e))
                for (const auto skill : data_->unit(unit.def).auras)
                    auras.push_back({e, skill, 1, pos.value});
        for (const auto& a : auras)
            for (const auto& fx : data_->skill(a.skill).effects) {
                if (fx.type != EffectType::Aura)
                    continue;
                SkillEffect buff = fx;
                buff.type = EffectType::Buff;
                buff.duration = kAuraDuration;
                applySkillEffect(a.source, buff, a.level, a.pos, a.skill);
            }
    }

    for (auto [e, regen, health] : registry_.view<const Regen, Health>().each())
        if (health.hp > 0.0f)
            health.hp = std::min(health.max, health.hp + regen.perSecond * kTickDt);

    for (auto [e, summoned, health] : registry_.view<Summoned, Health>().each()) {
        summoned.remaining -= kTickDt;
        if (summoned.remaining <= 0.0f)
            health.hp = 0.0f;
    }

    for (auto it = respawns_.begin(); it != respawns_.end();) {
        it->timer -= kTickDt;
        if (it->timer > 0.0f) {
            ++it;
            continue;
        }
        if (players_[it->player].active && map_) {
            const glm::vec2 pos = Map::center(nearestFree(*map_, Map::cellOf(playerBase(it->player))));
            const auto e = spawn(it->def, 0, pos, it->player);
            registry_.get<Progression>(e) = it->progression;
            refreshStats(e);
            auto& h = registry_.get<Health>(e);
            h.hp = h.max;
            events_.push_back({EventType::Promote, e, entt::null, pos, pos, static_cast<float>(Rank::Hero)});
        }
        it = respawns_.erase(it);
    }
}

}
