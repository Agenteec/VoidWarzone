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

struct UnitDef {
    std::string id;
    float maxHp = 100.0f;
    float armor = 0.0f;
    float radius = 0.5f;
    float speed = 0.0f;
    float sight = 8.0f;
    bool structure = false;
    std::optional<WeaponDef> weapon;
};

class GameData {
public:
    static GameData parse(std::string_view json);

    const UnitDef& unit(UnitDefId id) const { return units_[id]; }
    UnitDefId find(std::string_view id) const;
    const std::vector<UnitDef>& units() const { return units_; }
    float maxRadius() const { return maxRadius_; }
    std::uint64_t hash() const { return hash_; }

private:
    std::vector<UnitDef> units_;
    std::unordered_map<std::string, UnitDefId> index_;
    float maxRadius_ = 0.0f;
    std::uint64_t hash_ = 0;
};

std::uint64_t fnv1a(std::string_view data);

}
