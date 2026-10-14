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
inline constexpr UnitDefId kInvalidUnitDef = 0xffff;
inline constexpr std::uint16_t kInvalidId = 0xffff;

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
    float turnRate = 8.0f;
    float income = 0.0f;
    bool structure = false;
    bool buildable = false;
    UnitDefId produces = kInvalidUnitDef;
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
};

struct SkillDef {
    std::string id;
    std::string name;
    SkillTarget target = SkillTarget::Self;
    bool passive = false;
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

class GameData {
public:
    static GameData parse(std::string_view units, std::string_view skills = {}, std::string_view items = {});

    const UnitDef& unit(UnitDefId id) const { return units_[id]; }
    UnitDefId find(std::string_view id) const;
    const std::vector<UnitDef>& units() const { return units_; }
    const std::vector<UnitDefId>& recruitable() const { return recruitable_; }
    const std::vector<UnitDefId>& buildable() const { return buildable_; }

    const SkillDef& skill(SkillId id) const { return skills_[id]; }
    const std::vector<SkillDef>& skills() const { return skills_; }
    SkillId findSkill(std::string_view id) const;

    const ItemDef& item(ItemId id) const { return items_[id]; }
    const std::vector<ItemDef>& items() const { return items_; }
    ItemId findItem(std::string_view id) const;
    const std::vector<ItemId>& shop() const { return shop_; }

    float maxRadius() const { return maxRadius_; }
    std::uint64_t hash() const { return hash_; }

private:
    std::vector<UnitDef> units_;
    std::unordered_map<std::string, UnitDefId> index_;
    std::vector<UnitDefId> recruitable_;
    std::vector<UnitDefId> buildable_;
    std::vector<SkillDef> skills_;
    std::vector<ItemDef> items_;
    std::vector<ItemId> shop_;
    float maxRadius_ = 0.0f;
    std::uint64_t hash_ = 0;
};

std::uint64_t fnv1a(std::string_view data, std::uint64_t seed = 14695981039346656037ull);

}
