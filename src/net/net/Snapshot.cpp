#include "net/Snapshot.h"

#include <cstddef>

namespace vw::net {

namespace {

enum Field : std::uint8_t {
    kStatic = 1,
    kPos = 2,
    kVel = 4,
    kFacing = 8,
    kHp = 16,
    kAim = 32,
    kTarget = 64,
};

constexpr std::uint8_t kAll = kStatic | kPos | kVel | kFacing | kHp | kAim | kTarget;

std::uint8_t diff(const EntityState& a, const EntityState& b) {
    std::uint8_t m = 0;
    if (a.kind != b.kind || a.def != b.def || a.team != b.team || a.owner != b.owner || a.rank != b.rank)
        m |= kStatic;
    if (a.x != b.x || a.y != b.y)
        m |= kPos;
    if (a.vx != b.vx || a.vy != b.vy)
        m |= kVel;
    if (a.facing != b.facing)
        m |= kFacing;
    if (a.hp != b.hp || a.maxHp != b.maxHp)
        m |= kHp;
    if (a.aim != b.aim || a.pitch != b.pitch)
        m |= kAim;
    if (a.tx != b.tx || a.ty != b.ty)
        m |= kTarget;
    return m;
}

bool byId(const EntityState& a, const EntityState& b) {
    return a.id < b.id;
}

}

void writeView(Writer& w, const ClientView& v) {
    w.u32(v.commandSeq);
    w.u32(v.energy);
    w.u16(v.shards);
    w.u8(static_cast<std::uint8_t>(v.stash.size()));
    for (const auto item : v.stash)
        w.u16(item);
    w.varu(v.units.size());
    for (const auto& u : v.units) {
        w.u32(u.id);
        w.u8(u.rank);
        w.u8(u.level);
        w.u16(u.xp);
        w.u8(u.skillPoints);
        w.u8(static_cast<std::uint8_t>(u.skills.size()));
        for (const auto& s : u.skills) {
            w.u16(s.skill);
            w.u8(s.level);
            w.u16(s.cooldown);
        }
        w.u8(static_cast<std::uint8_t>(u.items.size()));
        for (const auto item : u.items)
            w.u16(item);
    }
}

bool readView(Reader& r, ClientView& v) {
    v.commandSeq = r.u32();
    v.energy = r.u32();
    v.shards = r.u16();
    v.stash.resize(r.u8());
    for (auto& item : v.stash)
        item = r.u16();
    const auto count = r.varu();
    if (count > 4096)
        return false;
    v.units.resize(static_cast<std::size_t>(count));
    for (auto& u : v.units) {
        u.id = r.u32();
        u.rank = r.u8();
        u.level = r.u8();
        u.xp = r.u16();
        u.skillPoints = r.u8();
        u.skills.resize(r.u8());
        for (auto& s : u.skills) {
            s.skill = r.u16();
            s.level = r.u8();
            s.cooldown = r.u16();
        }
        u.items.resize(r.u8());
        for (auto& item : u.items)
            item = r.u16();
        if (!r.ok())
            return false;
    }
    return r.ok();
}

const OwnUnit* ClientView::unit(std::uint32_t id) const {
    for (const auto& u : units)
        if (u.id == id)
            return &u;
    return nullptr;
}

const EntityState* Snapshot::find(std::uint32_t id) const {
    EntityState key;
    key.id = id;
    const auto it = std::lower_bound(entities.begin(), entities.end(), key, byId);
    return it != entities.end() && it->id == id ? &*it : nullptr;
}

void encodeSnapshot(Writer& w, const Snapshot& cur, const Snapshot* base, const ClientView& view) {
    w.u32(cur.tick);
    w.u32(base ? base->tick : 0);
    writeView(w, view);

    w.u8(cur.game.state);
    w.u8(cur.game.wave);
    w.u8(cur.game.totalWaves);
    w.u8(cur.game.difficulty);
    w.u16(cur.game.nextWaveIn);
    w.u32(cur.game.boss);
    w.u8(static_cast<std::uint8_t>(cur.game.points.size()));
    for (const auto& p : cur.game.points) {
        w.u8(static_cast<std::uint8_t>(p.control));
        w.u8(p.owner);
    }

    std::vector<std::uint32_t> removed;
    Writer changed;
    std::size_t changedCount = 0;

    const auto& baseEntities = base ? base->entities : std::vector<EntityState>{};
    std::size_t bi = 0;
    for (const auto& e : cur.entities) {
        while (bi < baseEntities.size() && baseEntities[bi].id < e.id)
            removed.push_back(baseEntities[bi++].id);
        const EntityState* prev = bi < baseEntities.size() && baseEntities[bi].id == e.id ? &baseEntities[bi++] : nullptr;
        const std::uint8_t mask = prev ? diff(*prev, e) : kAll;
        if (!mask)
            continue;
        ++changedCount;
        changed.u32(e.id);
        changed.u8(mask);
        if (mask & kStatic) {
            changed.u8(static_cast<std::uint8_t>(e.kind));
            changed.u16(e.def);
            changed.u8(e.team);
            changed.u8(e.owner);
            changed.u8(e.rank);
        }
        if (mask & kPos) {
            changed.u16(e.x);
            changed.u16(e.y);
        }
        if (mask & kVel) {
            changed.i16(e.vx);
            changed.i16(e.vy);
        }
        if (mask & kFacing)
            changed.u8(e.facing);
        if (mask & kHp) {
            changed.u16(e.hp);
            changed.u16(e.maxHp);
        }
        if (mask & kAim) {
            changed.u8(e.aim);
            changed.u8(e.pitch);
        }
        if (mask & kTarget) {
            changed.u16(e.tx);
            changed.u16(e.ty);
        }
    }
    while (bi < baseEntities.size())
        removed.push_back(baseEntities[bi++].id);

    w.varu(removed.size());
    for (const auto id : removed)
        w.u32(id);
    w.varu(changedCount);
    w.bytes(changed.data().data(), changed.size());

    w.varu(cur.events.size());
    for (const auto& ev : cur.events) {
        w.u8(ev.type);
        w.u16(ev.x);
        w.u16(ev.y);
        w.u16(ev.value);
    }
}

bool decodeSnapshot(Reader& r, Snapshot& out, const BaselineLookup& baseline) {
    out.tick = r.u32();
    const std::uint32_t baseTick = r.u32();
    if (!readView(r, out.view))
        return false;
    out.game.state = r.u8();
    out.game.wave = r.u8();
    out.game.totalWaves = r.u8();
    out.game.difficulty = r.u8();
    out.game.nextWaveIn = r.u16();
    out.game.boss = r.u32();
    out.game.points.resize(r.u8());
    for (auto& p : out.game.points) {
        p.control = static_cast<std::int8_t>(r.u8());
        p.owner = r.u8();
    }
    if (!r.ok())
        return false;

    const Snapshot* base = nullptr;
    if (baseTick != 0) {
        base = baseline ? baseline(baseTick) : nullptr;
        if (!base)
            return false;
    }
    out.entities = base ? base->entities : std::vector<EntityState>{};

    const auto removedCount = r.varu();
    if (removedCount > 65536)
        return false;
    for (std::uint64_t i = 0; i < removedCount && r.ok(); ++i) {
        EntityState key;
        key.id = r.u32();
        const auto it = std::lower_bound(out.entities.begin(), out.entities.end(), key, byId);
        if (it != out.entities.end() && it->id == key.id)
            out.entities.erase(it);
    }

    const std::size_t sortedCount = out.entities.size();
    const auto changedCount = r.varu();
    if (changedCount > 65536)
        return false;
    for (std::uint64_t i = 0; i < changedCount && r.ok(); ++i) {
        EntityState key;
        key.id = r.u32();
        const std::uint8_t mask = r.u8();
        const auto end = out.entities.begin() + static_cast<std::ptrdiff_t>(sortedCount);
        auto it = std::lower_bound(out.entities.begin(), end, key, byId);
        EntityState* e = nullptr;
        if (it != end && it->id == key.id) {
            e = &*it;
        } else {
            if (!(mask & kStatic))
                return false;
            out.entities.push_back(key);
            e = &out.entities.back();
        }
        if (mask & kStatic) {
            e->kind = static_cast<EntityKind>(r.u8());
            e->def = r.u16();
            e->team = r.u8();
            e->owner = r.u8();
            e->rank = r.u8();
        }
        if (mask & kPos) {
            e->x = r.u16();
            e->y = r.u16();
        }
        if (mask & kVel) {
            e->vx = r.i16();
            e->vy = r.i16();
        }
        if (mask & kFacing)
            e->facing = r.u8();
        if (mask & kHp) {
            e->hp = r.u16();
            e->maxHp = r.u16();
        }
        if (mask & kAim) {
            e->aim = r.u8();
            e->pitch = r.u8();
        }
        if (mask & kTarget) {
            e->tx = r.u16();
            e->ty = r.u16();
        }
    }
    std::sort(out.entities.begin(), out.entities.end(), byId);

    const auto eventCount = r.varu();
    if (eventCount > 4096)
        return false;
    out.events.clear();
    for (std::uint64_t i = 0; i < eventCount && r.ok(); ++i) {
        NetEvent ev;
        ev.type = r.u8();
        ev.x = r.u16();
        ev.y = r.u16();
        ev.value = r.u16();
        out.events.push_back(ev);
    }
    return r.ok();
}

}
