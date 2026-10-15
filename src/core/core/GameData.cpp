#include "core/GameData.h"

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace vw {

using nlohmann::json;

namespace {

constexpr const char* kDamageNames[] = {"kinetic", "piercing", "explosive", "energy", "emp"};
constexpr const char* kArmorNames[] = {"light", "heavy", "structure", "crystal"};

DamageType parseDamage(const std::string& name) {
    for (int i = 0; i < static_cast<int>(DamageType::Count); ++i)
        if (name == kDamageNames[i])
            return static_cast<DamageType>(i);
    throw std::runtime_error("unknown damage type: " + name);
}

ArmorType parseArmor(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ArmorType::Count); ++i)
        if (name == kArmorNames[i])
            return static_cast<ArmorType>(i);
    throw std::runtime_error("unknown armor type: " + name);
}

WeaponDef parseWeapon(const json& w, bool structure) {
    WeaponDef wd;
    wd.damage = w.value("damage", wd.damage);
    wd.range = w.value("range", wd.range);
    wd.cooldown = w.value("cooldown", wd.cooldown);
    wd.projectileSpeed = w.value("projectileSpeed", wd.projectileSpeed);
    wd.turnRate = w.value("turnRate", structure ? 3.5f : 6.0f);
    wd.type = parseDamage(w.value("type", "kinetic"));
    wd.splash = w.value("splash", wd.splash);
    const std::string targets = w.value("targets", "ground");
    wd.targets = targets == "both" ? 3 : targets == "air" ? 2 : 1;
    return wd;
}

StatMods parseMods(const json& j) {
    StatMods m;
    m.hp = j.value("hp", m.hp);
    m.hpMul = j.value("hpMul", m.hpMul);
    m.damageMul = j.value("damageMul", m.damageMul);
    m.attackSpeedMul = j.value("attackSpeedMul", m.attackSpeedMul);
    m.speedMul = j.value("speedMul", m.speedMul);
    m.armor = j.value("armor", m.armor);
    m.range = j.value("range", m.range);
    m.regen = j.value("regen", m.regen);
    return m;
}

EffectType parseEffectType(const std::string& t) {
    if (t == "damage")
        return EffectType::Damage;
    if (t == "heal")
        return EffectType::Heal;
    if (t == "buff")
        return EffectType::Buff;
    if (t == "dash")
        return EffectType::Dash;
    if (t == "summon")
        return EffectType::Summon;
    if (t == "aura")
        return EffectType::Aura;
    throw std::runtime_error("unknown effect: " + t);
}

EffectWho parseWho(const std::string& w) {
    if (w == "allies")
        return EffectWho::Allies;
    if (w == "enemies")
        return EffectWho::Enemies;
    return EffectWho::Self;
}

Rarity parseRarity(const std::string& r) {
    if (r == "rare")
        return Rarity::Rare;
    if (r == "epic")
        return Rarity::Epic;
    if (r == "legendary")
        return Rarity::Legendary;
    return Rarity::Common;
}

}

void StatMods::add(const StatMods& o, float scale) {
    hp += o.hp * scale;
    hpMul *= 1.0f + (o.hpMul - 1.0f) * scale;
    damageMul *= 1.0f + (o.damageMul - 1.0f) * scale;
    attackSpeedMul *= 1.0f + (o.attackSpeedMul - 1.0f) * scale;
    speedMul *= 1.0f + (o.speedMul - 1.0f) * scale;
    armor += o.armor * scale;
    range += o.range * scale;
    regen += o.regen * scale;
}

std::uint64_t fnv1a(std::string_view data, std::uint64_t seed) {
    std::uint64_t h = seed;
    for (const char c : data) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<UnitDefId> GameData::refits(UnitDefId base) const {
    std::vector<UnitDefId> out;
    for (std::size_t i = 0; i < units_.size(); ++i)
        if (units_[i].refitFrom == base)
            out.push_back(static_cast<UnitDefId>(i));
    return out;
}

ModuleId GameData::findModule(std::string_view id) const {
    for (std::size_t i = 0; i < modules_.size(); ++i)
        if (modules_[i].id == id)
            return static_cast<ModuleId>(i);
    return kInvalidId;
}

const char* moduleSlotName(ModuleSlot s) {
    return s == ModuleSlot::Weapon ? "weapon" : s == ModuleSlot::System ? "system" : "armor";
}

std::vector<UnitDefId> GameData::refitOptions(UnitDefId current) const {
    auto out = refits(current);
    if (units_[current].refitFrom != kInvalidUnitDef)
        for (const auto s : refits(units_[current].refitFrom))
            if (s != current)
                out.push_back(s);
    return out;
}

const char* damageTypeName(DamageType t) {
    return kDamageNames[std::min(static_cast<int>(t), static_cast<int>(DamageType::Count) - 1)];
}

const char* armorTypeName(ArmorType t) {
    return kArmorNames[std::min(static_cast<int>(t), static_cast<int>(ArmorType::Count) - 1)];
}

GameData GameData::parse(std::string_view unitsText, std::string_view skillsText, std::string_view itemsText) {
    GameData d;

    std::unordered_map<std::string, SkillId> skillIndex;
    std::vector<json> skillJson;
    const json skillsJson = skillsText.empty() ? json::object({{"skills", json::array()}}) : json::parse(skillsText);
    for (const auto& s : skillsJson.at("skills")) {
            SkillDef def;
            def.id = s.at("id").get<std::string>();
            def.name = s.value("name", def.id);
            def.target = s.value("target", "self") == "point" ? SkillTarget::Point : SkillTarget::Self;
            def.passive = s.value("passive", false);
            def.ultimate = s.value("ultimate", false);
            def.minLevel = s.value("minLevel", def.ultimate ? 3 : 1);
            def.range = s.value("range", def.range);
            def.cooldown = s.value("cooldown", def.cooldown);
            if (skillIndex.contains(def.id))
                throw std::runtime_error("duplicate skill: " + def.id);
            skillIndex.emplace(def.id, static_cast<SkillId>(d.skills_.size()));
            d.skills_.push_back(std::move(def));
            skillJson.push_back(s);
        }

    std::unordered_map<std::string, ItemId> itemIndex;
    const json itemsJson = itemsText.empty() ? json::object({{"items", json::array()}}) : json::parse(itemsText);
    for (const auto& i : itemsJson.at("items")) {
            ItemDef def;
            def.id = i.at("id").get<std::string>();
            def.name = i.value("name", def.id);
            def.rarity = parseRarity(i.value("rarity", "common"));
            def.cost = i.value("cost", def.cost);
            def.shards = i.value("shards", def.shards);
            if (const auto m = i.find("mods"); m != i.end())
                def.mods = parseMods(*m);
            if (itemIndex.contains(def.id))
                throw std::runtime_error("duplicate item: " + def.id);
            const auto id = static_cast<ItemId>(d.items_.size());
            itemIndex.emplace(def.id, id);
            if (def.cost > 0.0f || def.shards > 0)
                d.shop_.push_back(id);
            d.items_.push_back(std::move(def));
        }

    for (const auto& m : itemsJson.value("modules", json::array())) {
        ModuleDef def;
        def.id = m.at("id").get<std::string>();
        def.name = m.value("name", def.id);
        const std::string slot = m.value("slot", "armor");
        def.slot = slot == "weapon" ? ModuleSlot::Weapon : slot == "system" ? ModuleSlot::System : ModuleSlot::Armor;
        def.cost = m.value("cost", def.cost);
        if (const auto mods = m.find("mods"); mods != m.end())
            def.mods = parseMods(*mods);
        def.shield = m.value("shield", def.shield);
        def.look = m.value("look", "");
        if (const auto w = m.find("weapon"); w != m.end())
            def.weapon = parseWeapon(*w, false);
        d.modules_.push_back(std::move(def));
    }

    const json unitsJson = json::parse(unitsText);
    if (const auto c = unitsJson.find("combat"); c != unitsJson.end()) {
        const json hull = c->value("hull", json::object());
        for (const auto& [dname, row] : hull.items())
            for (const auto& [aname, v] : row.items())
                d.combat_.hull[static_cast<int>(parseDamage(dname))][static_cast<int>(parseArmor(aname))] = v.get<float>();
        const json shield = c->value("shield", json::object());
        for (const auto& [dname, v] : shield.items())
            d.combat_.shield[static_cast<int>(parseDamage(dname))] = v.get<float>();
    }
    std::unordered_map<std::size_t, std::string> produceNames;
    std::unordered_map<std::size_t, std::string> refitNames;
    for (const auto& u : unitsJson.at("units")) {
        UnitDef def;
        def.id = u.at("id").get<std::string>();
        def.name = u.value("name", def.id);
        def.cost = u.value("cost", def.cost);
        def.bounty = u.value("bounty", def.bounty);
        def.shards = u.value("shards", def.shards);
        def.maxHp = u.value("maxHp", def.maxHp);
        def.armor = u.value("armor", def.armor);
        def.radius = u.value("radius", def.radius);
        def.speed = u.value("speed", def.speed);
        def.sight = u.value("sight", def.sight);
        def.accel = u.value("accel", std::max(def.speed * 4.0f, 1.0f));
        def.turnRate = u.value("turnRate", def.turnRate);
        def.structure = u.value("structure", def.structure);
        def.armorType = def.structure ? ArmorType::Structure : def.radius >= 0.65f ? ArmorType::Heavy : ArmorType::Light;
        if (const auto at = u.find("armorType"); at != u.end())
            def.armorType = parseArmor(at->get<std::string>());
        def.shield = u.value("shield", def.shield);
        def.shieldRegen = u.value("shieldRegen", def.shield * 0.05f);
        def.income = u.value("income", def.income);
        def.buildable = u.value("buildable", def.buildable);
        def.repair = u.value("repair", def.repair);
        def.air = u.value("air", def.air);
        def.anchor = u.value("anchor", def.anchor);
        def.transport = u.value("transport", def.transport);
        def.repairRange = u.value("repairRange", def.repairRange);
        if (const auto rf = u.find("refit"); rf != u.end()) {
            refitNames.emplace(d.units_.size(), rf->at("from").get<std::string>());
            def.refitCost = rf->value("cost", def.refitCost);
            def.refitRank = rf->value("rank", "elite") == "hero" ? 3 : 2;
        }
        for (const auto& a : u.value("auras", json::array())) {
            const auto it = skillIndex.find(a.get<std::string>());
            if (it == skillIndex.end())
                throw std::runtime_error("unknown aura: " + a.get<std::string>());
            def.auras.push_back(it->second);
        }
        def.builder = u.value("builder", def.builder);
        def.wall = u.value("wall", def.wall);
        def.buildTime = u.value("buildTime", def.buildTime);
        const bool armed = u.contains("weapon");
        def.priority = u.value("priority", armed ? (def.structure ? 2.0f : 3.0f) : (def.structure ? 1.0f : 2.0f));
        if (const auto pr = u.find("produce"); pr != u.end()) {
            produceNames.emplace(d.units_.size(), pr->at("unit").get<std::string>());
            def.produceInterval = pr->value("interval", def.produceInterval);
            def.produceMax = pr->value("max", def.produceMax);
        }
        if (const auto w = u.find("weapon"); w != u.end())
            def.weapon = parseWeapon(*w, def.structure);
        if (const auto b = u.find("boss"); b != u.end()) {
            BossDef bd;
            bd.title = b->value("title", def.name);
            for (const auto& a : b->value("abilities", json::array())) {
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
        for (const auto& s : u.value("skills", json::array())) {
            const auto it = skillIndex.find(s.get<std::string>());
            if (it == skillIndex.end())
                throw std::runtime_error("unknown skill: " + s.get<std::string>());
            def.skills.push_back(it->second);
        }
        for (const auto& l : u.value("loot", json::array())) {
            const auto it = itemIndex.find(l.get<std::string>());
            if (it == itemIndex.end())
                throw std::runtime_error("unknown item: " + l.get<std::string>());
            def.loot.push_back(it->second);
        }
        if (d.index_.contains(def.id))
            throw std::runtime_error("duplicate unit: " + def.id);
        d.maxRadius_ = std::max(d.maxRadius_, def.radius);
        d.index_.emplace(def.id, static_cast<UnitDefId>(d.units_.size()));
        d.units_.push_back(std::move(def));
    }

    for (const auto& [i, name] : refitNames) {
        d.units_[i].refitFrom = d.find(name);
        if (d.units_[i].refitFrom == kInvalidUnitDef)
            throw std::runtime_error("unknown refit base: " + name);
    }

    for (const auto& [i, name] : produceNames) {
        d.units_[i].produces = d.find(name);
        if (d.units_[i].produces == kInvalidUnitDef)
            throw std::runtime_error("unknown produced unit: " + name);
    }

    for (std::size_t i = 0; i < d.units_.size(); ++i) {
        auto& def = d.units_[i];
        if (def.buildable)
            d.buildable_.push_back(static_cast<UnitDefId>(i));
        else if (def.cost > 0.0f && !def.structure)
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

    for (std::size_t i = 0; i < d.skills_.size(); ++i) {
        for (const auto& e : skillJson[i].at("effects")) {
            SkillEffect fx;
            fx.type = parseEffectType(e.at("type").get<std::string>());
            fx.who = parseWho(e.value("who", fx.type == EffectType::Damage ? "enemies" : fx.type == EffectType::Heal ? "allies" : "self"));
            fx.radius = e.value("radius", fx.radius);
            fx.amount = e.value("amount", fx.amount);
            fx.duration = e.value("duration", fx.duration);
            fx.count = e.value("count", fx.count);
            fx.delay = e.value("delay", fx.delay);
            fx.damageType = parseDamage(e.value("damageType", "kinetic"));
            if (const auto m = e.find("mods"); m != e.end())
                fx.mods = parseMods(*m);
            if (fx.type == EffectType::Summon) {
                fx.unit = d.find(e.at("unit").get<std::string>());
                if (fx.unit == kInvalidUnitDef)
                    throw std::runtime_error("unknown summon unit in skill " + d.skills_[i].id);
            }
            d.skills_[i].effects.push_back(fx);
        }
    }

    d.hash_ = fnv1a(itemsText, fnv1a(skillsText, fnv1a(unitsText)));
    return d;
}

UnitDefId GameData::find(std::string_view id) const {
    const auto it = index_.find(std::string(id));
    return it == index_.end() ? kInvalidUnitDef : it->second;
}

SkillId GameData::findSkill(std::string_view id) const {
    for (std::size_t i = 0; i < skills_.size(); ++i)
        if (skills_[i].id == id)
            return static_cast<SkillId>(i);
    return kInvalidId;
}

ItemId GameData::findItem(std::string_view id) const {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].id == id)
            return static_cast<ItemId>(i);
    return kInvalidId;
}

}
