#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vw {

using UnitDefId = std::uint16_t;
inline constexpr UnitDefId kInvalidUnitDef = 0xffff;

struct WeaponDef {
    float damage = 0.0f;
    float range = 0.0f;
    float cooldown = 1.0f;
    float projectileSpeed = 0.0f;
};

enum class AbilityType : std::uint8_t { Slam, Summon };

struct AbilityDef {
    AbilityType type = AbilityType::Slam;
    float cooldown = 10.0f;
    float radius = 4.0f;
    float damage = 0.0f;
    float windup = 0.8f;
    std::string unit;
    UnitDefId unitId = 0xffff;
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
    float maxHp = 100.0f;
    float armor = 0.0f;
    float radius = 0.5f;
    float speed = 0.0f;
    float sight = 8.0f;
    bool structure = false;
    std::optional<WeaponDef> weapon;
    std::optional<BossDef> boss;
};

class GameData {
public:
    static GameData parse(std::string_view json);

    const UnitDef& unit(UnitDefId id) const { return units_[id]; }
    UnitDefId find(std::string_view id) const;
    const std::vector<UnitDef>& units() const { return units_; }
    const std::vector<UnitDefId>& recruitable() const { return recruitable_; }
    float maxRadius() const { return maxRadius_; }
    std::uint64_t hash() const { return hash_; }

private:
    std::vector<UnitDef> units_;
    std::unordered_map<std::string, UnitDefId> index_;
    std::vector<UnitDefId> recruitable_;
    float maxRadius_ = 0.0f;
    std::uint64_t hash_ = 0;
};

std::uint64_t fnv1a(std::string_view data);

}
