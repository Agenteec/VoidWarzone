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
        if (d.index_.contains(def.id))
            throw std::runtime_error("duplicate unit: " + def.id);
        d.maxRadius_ = std::max(d.maxRadius_, def.radius);
        d.index_.emplace(def.id, static_cast<UnitDefId>(d.units_.size()));
        d.units_.push_back(std::move(def));
    }
    d.hash_ = fnv1a(text);
    return d;
}

UnitDefId GameData::find(std::string_view id) const {
    const auto it = index_.find(std::string(id));
    return it == index_.end() ? kInvalidUnitDef : it->second;
}

}
