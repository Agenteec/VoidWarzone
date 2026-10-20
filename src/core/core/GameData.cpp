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
    wd.burst = std::max(1, w.value("burst", wd.burst));
    wd.burstGap = w.value("burstGap", wd.burstGap);
    wd.spread = w.value("spread", wd.spread);
    wd.guided = w.value("guided", wd.guided);
    wd.suicide = w.value("suicide", wd.suicide);
    const std::string targets = w.value("targets", "ground");
    wd.targets = targets == "both" ? 3 : targets == "air" ? 2 : 1;
    if (const auto slow = w.find("slow"); slow != w.end()) {
        wd.slowMove = slow->value("move", wd.slowMove);
        wd.slowAttack = slow->value("attack", wd.slowAttack);
        wd.slowTime = slow->value("duration", 2.0f);
    }
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

ResearchId GameData::findResearch(std::string_view id) const {
    for (std::size_t i = 0; i < research_.size(); ++i)
        if (research_[i].id == id)
            return static_cast<ResearchId>(i);
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

std::string expandDesigns(std::string_view unitsText) {
    const json doc = json::parse(unitsText);
    json out = json::array();
    const auto d = doc.find("design");
    if (d == doc.end())
        return out.dump();
    const json bodies = d->value("bodies", json::array());
    const json props = d->value("propulsion", json::array());
    const json weapons = d->value("weapons", json::array());
    for (const auto& b : bodies)
        for (const auto& p : props)
            for (const auto& w : weapons) {
                const std::string bid = b.at("id").get<std::string>();
                const std::string pid = p.at("id").get<std::string>();
                const std::string wid = w.at("id").get<std::string>();
                json u;
                u["id"] = "d_" + bid + "_" + pid + "_" + wid;
                u["name"] = b.value("name", bid) + " " + p.value("name", pid) + " " + w.value("name", wid);
                u["design"] = true;
                const float cost = (b.value("cost", 0.0f) + w.value("cost", 0.0f)) * p.value("costMul", 1.0f);
                u["cost"] = std::round(cost);
                u["produceTime"] = std::round(std::max(6.0f, cost / 12.0f));
                u["maxHp"] = std::round(b.value("maxHp", 300.0f) * p.value("hpMul", 1.0f));
                u["armor"] = b.value("armor", 0.0f);
                u["armorType"] = bid == "light" ? "light" : "heavy";
                u["radius"] = b.value("radius", 0.6f);
                u["speed"] = p.value("speed", 5.0f) * b.value("speed", 1.0f);
                u["accel"] = p.value("accel", 5.0f) * b.value("speed", 1.0f);
                u["turnRate"] = p.value("turnRate", 3.0f) * b.value("speed", 1.0f);
                u["producedAt"] = b.value("factory", json::array());
                json classes = json::array({"design"});
                for (const json* part : {&b, &p, &w})
                    for (const auto& c : part->value("classes", json::array()))
                        classes.push_back(c);
                u["classes"] = classes;
                json research = json::array();
                for (const json* part : {&b, &p, &w})
                    if (part->contains("research") && part->at("research").is_string())
                        research.push_back(part->at("research"));
                u["researchAll"] = research;
                float range = 0.0f;
                if (const auto weapon = w.find("weapon"); weapon != w.end()) {
                    u["weapon"] = *weapon;
                    range = weapon->value("range", 0.0f);
                }
                if (w.contains("repair")) {
                    u["repair"] = w.at("repair");
                    u["repairRange"] = w.value("repairRange", 4.0f);
                }
                u["sight"] = std::max({w.value("sight", 0.0f), range + 1.0f, 9.0f});
                json model = json::object();
                model["glb"] = "design_" + bid + "_" + pid;
                model["turret"] = w.value("turret", std::string("colony_uaz"));
                json visual = json::object();
                visual["shape"] = "cube";
                visual["height"] = 1.2;
                visual["model"] = model;
                if (w.contains("shot"))
                    visual["shot"] = w.at("shot");
                if (w.value("spin", false))
                    visual["spin"] = true;
                u["visual"] = visual;
                out.push_back(std::move(u));
            }
    return out.dump();
}

GameData GameData::parse(std::string_view unitsText, std::string_view skillsText, std::string_view itemsText, std::string_view researchText) {
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
    struct Links {
        std::string deploy, undeploy, requires_;
        std::vector<std::string> producedAt;
    };
    std::unordered_map<std::size_t, Links> links;
    json allUnits = unitsJson.at("units");
    for (auto& g : json::parse(expandDesigns(unitsText)))
        allUnits.push_back(std::move(g));
    std::unordered_map<std::size_t, std::vector<std::string>> researchAllNames;
    for (const auto& u : allUnits) {
        if (u.contains("researchAll"))
            researchAllNames[d.units_.size()] = u.at("researchAll").get<std::vector<std::string>>();
        UnitDef def;
        def.design = u.value("design", false);
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
        def.lab = u.value("lab", def.lab);
        def.classes = u.value("classes", std::vector<std::string>{});
        def.deployOnly = u.value("deployOnly", false);
        def.produceTime = u.value("produceTime", std::max(2.0f, def.cost / 10.0f));
        {
            Links l{u.value("deploy", ""), u.value("undeploy", ""), u.value("requires", ""), u.value("producedAt", std::vector<std::string>{})};
            if (!l.deploy.empty() || !l.undeploy.empty() || !l.requires_.empty() || !l.producedAt.empty())
                links.emplace(d.units_.size(), std::move(l));
        }
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

    for (const auto& [i, l] : links) {
        auto resolve = [&](const std::string& name) {
            const UnitDefId id = d.find(name);
            if (id == kInvalidUnitDef)
                throw std::runtime_error("unknown unit link: " + name);
            return id;
        };
        auto& def = d.units_[i];
        if (!l.deploy.empty())
            def.deploy = resolve(l.deploy);
        if (!l.undeploy.empty())
            def.undeploy = resolve(l.undeploy);
        if (!l.requires_.empty())
            def.needs = resolve(l.requires_);
        for (const auto& name : l.producedAt) {
            def.producedAt.push_back(resolve(name));
            d.units_[def.producedAt.back()].factory = true;
        }
    }
    for (const auto& [i, name] : produceNames) {
        d.units_[i].produces = d.find(name);
        if (d.units_[i].produces == kInvalidUnitDef)
            throw std::runtime_error("unknown produced unit: " + name);
    }

    for (std::size_t i = 0; i < d.units_.size(); ++i) {
        auto& def = d.units_[i];
        if (def.buildable && !def.deployOnly)
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

    if (!researchText.empty()) {
        const json rj = json::parse(researchText);
        std::unordered_map<std::string, ResearchId> index;
        std::vector<std::vector<std::string>> after;
        for (const auto& t : rj.at("topics")) {
            ResearchDef def;
            def.id = t.at("id").get<std::string>();
            def.name = t.value("name", def.id);
            def.desc = t.value("desc", "");
            def.cost = t.value("cost", def.cost);
            def.time = t.value("time", def.time);
            if (const auto m = t.find("mods"); m != t.end())
                def.mods = parseMods(*m);
            const std::string scope = t.value("applies", "all");
            def.scope = scope == "vehicles" ? ResearchScope::Vehicles : scope == "structures" ? ResearchScope::Structures : ResearchScope::All;
            if (const auto w = t.find("weapon"); w != t.end())
                def.weapon = static_cast<int>(parseDamage(w->get<std::string>()));
            def.classes = t.value("classes", std::vector<std::string>{});
            if (index.contains(def.id))
                throw std::runtime_error("duplicate research: " + def.id);
            const auto id = static_cast<ResearchId>(d.research_.size());
            index.emplace(def.id, id);
            for (const auto& u : t.value("unlocks", json::array())) {
                const UnitDefId unit = d.find(u.get<std::string>());
                if (unit == kInvalidUnitDef)
                    throw std::runtime_error("unknown research unlock: " + u.get<std::string>());
                def.unlocks.push_back(unit);
                d.units_[unit].research = id;
            }
            after.push_back(t.value("after", std::vector<std::string>{}));
            d.research_.push_back(std::move(def));
        }
        if (d.research_.size() > 255)
            throw std::runtime_error("too many research topics");
        for (const auto& [unit, names] : researchAllNames)
            for (const auto& name : names) {
                const auto it = index.find(name);
                if (it == index.end())
                    throw std::runtime_error("unknown design research: " + name);
                d.units_[unit].researchAll.push_back(it->second);
            }
        if (const auto design = unitsJson.find("design"); design != unitsJson.end()) {
            const char* keys[] = {"bodies", "propulsion", "weapons"};
            for (std::size_t k = 0; k < 3; ++k)
                for (const auto& part : design->value(keys[k], json::array())) {
                    DesignPart dp;
                    dp.id = part.at("id").get<std::string>();
                    dp.name = part.value("name", dp.id);
                    if (part.contains("research") && part.at("research").is_string())
                        if (const auto it = index.find(part.at("research").get<std::string>()); it != index.end())
                            dp.research = it->second;
                    d.designParts_[k].push_back(std::move(dp));
                }
        }
        for (std::size_t i = 0; i < d.research_.size(); ++i) {
            for (const auto& name : after[i]) {
                const auto it = index.find(name);
                if (it == index.end() || it->second >= i)
                    throw std::runtime_error("research '" + d.research_[i].id + "' must come after a known topic: " + name);
                d.research_[i].after.push_back(it->second);
                d.research_[i].tier = std::max(d.research_[i].tier, d.research_[it->second].tier + 1);
            }
        }
    }

    d.hash_ = fnv1a(researchText, fnv1a(itemsText, fnv1a(skillsText, fnv1a(unitsText))));
    return d;
}

UnitDefId GameData::designOf(std::string_view body, std::string_view prop, std::string_view weapon) const {
    return find("d_" + std::string(body) + "_" + std::string(prop) + "_" + std::string(weapon));
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
