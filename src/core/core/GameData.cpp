#include "core/GameData.h"

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace vw {

std::uint64_t fnv1a(std::string_view data) {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : data) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

GameData GameData::parse(std::string_view text) {
    const auto j = nlohmann::json::parse(text);
    GameData d;
    for (const auto& u : j.at("units")) {
        UnitDef def;
        def.id = u.at("id").get<std::string>();
        def.name = u.value("name", def.id);
        def.cost = u.value("cost", def.cost);
        def.bounty = u.value("bounty", def.bounty);
        def.maxHp = u.value("maxHp", def.maxHp);
        def.armor = u.value("armor", def.armor);
        def.radius = u.value("radius", def.radius);
        def.speed = u.value("speed", def.speed);
        def.sight = u.value("sight", def.sight);
        def.structure = u.value("structure", def.structure);
        if (const auto w = u.find("weapon"); w != u.end()) {
            WeaponDef wd;
            wd.damage = w->value("damage", wd.damage);
            wd.range = w->value("range", wd.range);
            wd.cooldown = w->value("cooldown", wd.cooldown);
            wd.projectileSpeed = w->value("projectileSpeed", wd.projectileSpeed);
            def.weapon = wd;
        }
        if (const auto b = u.find("boss"); b != u.end()) {
            BossDef bd;
            bd.title = b->value("title", def.name);
            for (const auto& a : b->value("abilities", nlohmann::json::array())) {
                AbilityDef ad;
                const auto type = a.at("type").get<std::string>();
                if (type == "slam")
                    ad.type = AbilityType::Slam;
                else if (type == "summon")
                    ad.type = AbilityType::Summon;
                else
                    throw std::runtime_error("unknown ability: " + type);
                ad.cooldown = a.value("cooldown", ad.cooldown);
                ad.radius = a.value("radius", ad.radius);
                ad.damage = a.value("damage", ad.damage);
                ad.windup = a.value("windup", ad.windup);
                ad.unit = a.value("unit", "");
                ad.count = a.value("count", ad.count);
                bd.abilities.push_back(std::move(ad));
            }
            if (const auto en = b->find("enrage"); en != b->end()) {
                EnrageDef ed;
                ed.hpBelow = en->value("hpBelow", ed.hpBelow);
                ed.speed = en->value("speed", ed.speed);
                ed.damage = en->value("damage", ed.damage);
                ed.cooldown = en->value("cooldown", ed.cooldown);
                bd.enrage = ed;
            }
            def.boss = std::move(bd);
        }
        if (d.index_.contains(def.id))
            throw std::runtime_error("duplicate unit: " + def.id);
        d.maxRadius_ = std::max(d.maxRadius_, def.radius);
        d.index_.emplace(def.id, static_cast<UnitDefId>(d.units_.size()));
        d.units_.push_back(std::move(def));
    }
    for (std::size_t i = 0; i < d.units_.size(); ++i) {
        auto& def = d.units_[i];
        if (def.cost > 0.0f)
            d.recruitable_.push_back(static_cast<UnitDefId>(i));
        if (!def.boss)
            continue;
        for (auto& a : def.boss->abilities) {
            if (a.type != AbilityType::Summon)
                continue;
            a.unitId = d.find(a.unit);
            if (a.unitId == kInvalidUnitDef)
                throw std::runtime_error("unknown summon unit: " + a.unit);
        }
    }
    d.hash_ = fnv1a(text);
    return d;
}

UnitDefId GameData::find(std::string_view id) const {
    const auto it = index_.find(std::string(id));
    return it == index_.end() ? kInvalidUnitDef : it->second;
}

}
