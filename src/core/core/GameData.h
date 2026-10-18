#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vw {

using UnitDefId = std::uint16_t;
using SkillId = std::uint16_t;
using ItemId = std::uint16_t;
using ResearchId = std::uint16_t;
inline constexpr UnitDefId kInvalidUnitDef = 0xffff;
inline constexpr std::uint16_t kInvalidId = 0xffff;

enum class DamageType : std::uint8_t { Kinetic, Piercing, Explosive, Energy, Emp, Count };
enum class ArmorType : std::uint8_t { Light, Heavy, Structure, Crystal, Count };

struct CombatTable {
    float hull[static_cast<int>(DamageType::Count)][static_cast<int>(ArmorType::Count)] = {
        {1.25f, 0.6f, 0.5f, 1.0f}, {0.8f, 1.3f, 0.9f, 0.9f}, {1.1f, 0.8f, 1.5f, 1.0f}, {1.0f, 1.0f, 0.8f, 0.7f}, {0.3f, 0.3f, 0.2f, 0.3f}};
    float shield[static_cast<int>(DamageType::Count)] = {1.0f, 0.8f, 0.8f, 1.4f, 3.0f};

    float vsHull(DamageType d, ArmorType a) const { return hull[static_cast<int>(d)][static_cast<int>(a)]; }
    float vsShield(DamageType d) const { return shield[static_cast<int>(d)]; }
};

const char* damageTypeName(DamageType t);
const char* armorTypeName(ArmorType t);

struct StatMods {
    float hp = 0.0f;
    float hpMul = 1.0f;
    float damageMul = 1.0f;
    float attackSpeedMul = 1.0f;
    float speedMul = 1.0f;
    float armor = 0.0f;
    float range = 0.0f;
    float regen = 0.0f;

    void add(const StatMods& o, float scale = 1.0f);
};

struct WeaponDef {
    DamageType type = DamageType::Kinetic;
    float splash = 0.0f;
    std::uint8_t targets = 1;
    float slowMove = 1.0f;
    float slowAttack = 1.0f;
    float slowTime = 0.0f;
    float damage = 0.0f;
    float range = 0.0f;
    float cooldown = 1.0f;
    float projectileSpeed = 0.0f;
    float turnRate = 0.0f;
};

enum class AbilityType : std::uint8_t { Slam, Summon };

struct AbilityDef {
    AbilityType type = AbilityType::Slam;
    float cooldown = 10.0f;
    float radius = 4.0f;
    float damage = 0.0f;
    float windup = 0.8f;
    std::string unit;
    UnitDefId unitId = kInvalidUnitDef;
    int count = 0;
};

struct EnrageDef {
    float hpBelow = 0.5f;
    float speed = 1.0f;
    float damage = 1.0f;
    float cooldown = 1.0f;
};

struct BossDef {
    std::string title;
    std::vector<AbilityDef> abilities;
    std::optional<EnrageDef> enrage;
};

struct UnitDef {
    std::string id;
    std::string name;
    float cost = 0.0f;
    float bounty = 0.0f;
    int shards = 0;
    float maxHp = 100.0f;
    float armor = 0.0f;
    float radius = 0.5f;
    float speed = 0.0f;
    float sight = 8.0f;
    float accel = 0.0f;
    bool air = false;
    bool anchor = false;
    int transport = 0;
    UnitDefId refitFrom = kInvalidUnitDef;
    float refitCost = 0.0f;
    std::uint8_t refitRank = 2;
    std::vector<SkillId> auras;
    float repair = 0.0f;
    float repairRange = 0.0f;
    ArmorType armorType = ArmorType::Light;
    float shield = 0.0f;
    float shieldRegen = 0.0f;
    float turnRate = 8.0f;
    float income = 0.0f;
    bool structure = false;
    bool buildable = false;
    bool builder = false;
    bool wall = false;
    float buildTime = 10.0f;
    float priority = 1.0f;
    UnitDefId produces = kInvalidUnitDef;
    UnitDefId deploy = kInvalidUnitDef;
    UnitDefId undeploy = kInvalidUnitDef;
    bool deployOnly = false;
    std::vector<UnitDefId> producedAt;
    UnitDefId needs = kInvalidUnitDef;
    bool factory = false;
    bool lab = false;
    ResearchId research = 0xffff;
    float produceTime = 0.0f;
    float produceInterval = 20.0f;
    int produceMax = 4;
    std::optional<WeaponDef> weapon;
    std::optional<BossDef> boss;
    std::vector<SkillId> skills;
    std::vector<ItemId> loot;
};

enum class SkillTarget : std::uint8_t { Self, Point };
enum class EffectType : std::uint8_t { Damage, Heal, Buff, Dash, Summon, Aura };
enum class EffectWho : std::uint8_t { Self, Allies, Enemies };

struct SkillEffect {
    EffectType type = EffectType::Damage;
    EffectWho who = EffectWho::Self;
    float radius = 0.0f;
    float amount = 0.0f;
    float duration = 0.0f;
    StatMods mods;
    UnitDefId unit = kInvalidUnitDef;
    int count = 0;
    float delay = 0.0f;
    DamageType damageType = DamageType::Kinetic;
};

struct SkillDef {
    std::string id;
    std::string name;
    SkillTarget target = SkillTarget::Self;
    bool passive = false;
    bool ultimate = false;
    int minLevel = 1;
    float range = 0.0f;
    float cooldown = 10.0f;
    std::vector<SkillEffect> effects;
};

enum class Rarity : std::uint8_t { Common, Rare, Epic, Legendary };

struct ItemDef {
    std::string id;
    std::string name;
    Rarity rarity = Rarity::Common;
    float cost = 0.0f;
    int shards = 0;
    StatMods mods;
};

enum class ModuleSlot : std::uint8_t { Armor, System, Weapon, Count };
inline constexpr int kModuleSlots = static_cast<int>(ModuleSlot::Count);
using ModuleId = std::uint16_t;

struct ModuleDef {
    std::string id;
    std::string name;
    ModuleSlot slot = ModuleSlot::Armor;
    float cost = 0.0f;
    StatMods mods;
    float shield = 0.0f;
    std::optional<WeaponDef> weapon;
    std::string look;
};

const char* moduleSlotName(ModuleSlot s);

enum class ResearchScope : std::uint8_t { All, Vehicles, Structures };

struct ResearchDef {
    std::string id;
    std::string name;
    std::string desc;
    float cost = 100.0f;
    float time = 30.0f;
    std::vector<ResearchId> after;
    std::vector<UnitDefId> unlocks;
    StatMods mods;
    ResearchScope scope = ResearchScope::All;
    int weapon = -1;
    int tier = 0;

    bool applies(const UnitDef& def) const {
        if (scope == ResearchScope::Vehicles && def.structure)
            return false;
        if (scope == ResearchScope::Structures && !def.structure)
            return false;
        return weapon < 0 || (def.weapon && static_cast<int>(def.weapon->type) == weapon);
    }
};

class GameData {
public:
    static GameData parse(std::string_view units, std::string_view skills = {}, std::string_view items = {}, std::string_view research = {});

    const UnitDef& unit(UnitDefId id) const { return units_[id]; }
    UnitDefId find(std::string_view id) const;
    const std::vector<UnitDef>& units() const { return units_; }
    const std::vector<UnitDefId>& recruitable() const { return recruitable_; }
    const std::vector<UnitDefId>& buildable() const { return buildable_; }
    std::vector<UnitDefId> refits(UnitDefId base) const;
    UnitDefId baseOf(UnitDefId id) const { return units_[id].refitFrom != kInvalidUnitDef ? units_[id].refitFrom : id; }
    UnitDefId rootOf(UnitDefId id) const {
        while (units_[id].refitFrom != kInvalidUnitDef)
            id = units_[id].refitFrom;
        return id;
    }
    int refitTier(UnitDefId id) const { return units_[id].refitFrom == kInvalidUnitDef ? 0 : 1 + refitTier(units_[id].refitFrom); }
    std::vector<UnitDefId> refitOptions(UnitDefId current) const;

    const SkillDef& skill(SkillId id) const { return skills_[id]; }
    const std::vector<SkillDef>& skills() const { return skills_; }
    SkillId findSkill(std::string_view id) const;

    const ItemDef& item(ItemId id) const { return items_[id]; }
    const std::vector<ItemDef>& items() const { return items_; }
    ItemId findItem(std::string_view id) const;
    const std::vector<ItemId>& shop() const { return shop_; }
    const ModuleDef& module(ModuleId id) const { return modules_[id]; }
    const std::vector<ModuleDef>& modules() const { return modules_; }
    ModuleId findModule(std::string_view id) const;
    const ResearchDef& research(ResearchId id) const { return research_[id]; }
    const std::vector<ResearchDef>& research() const { return research_; }
    ResearchId findResearch(std::string_view id) const;

    float maxRadius() const { return maxRadius_; }
    const CombatTable& combat() const { return combat_; }
    std::uint64_t hash() const { return hash_; }

private:
    std::vector<UnitDef> units_;
    std::unordered_map<std::string, UnitDefId> index_;
    std::vector<UnitDefId> recruitable_;
    std::vector<UnitDefId> buildable_;
    std::vector<SkillDef> skills_;
    std::vector<ItemDef> items_;
    std::vector<ItemId> shop_;
    std::vector<ModuleDef> modules_;
    std::vector<ResearchDef> research_;
    float maxRadius_ = 0.0f;
    CombatTable combat_;
    std::uint64_t hash_ = 0;
};

std::uint64_t fnv1a(std::string_view data, std::uint64_t seed = 14695981039346656037ull);

}
