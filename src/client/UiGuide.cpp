#include "App.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "core/World.h"
#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

const char* targetsName(std::uint8_t t) {
    return t == 3 ? "ground+air" : t == 2 ? "air" : "ground";
}

ImVec4 multColor(float v) {
    if (v >= 1.2f)
        return {0.35f, 1.0f, 0.5f, 1.0f};
    if (v > 1.0f)
        return {0.7f, 1.0f, 0.6f, 1.0f};
    if (v >= 0.95f)
        return {0.85f, 0.88f, 0.92f, 1.0f};
    if (v >= 0.7f)
        return {1.0f, 0.75f, 0.4f, 1.0f};
    return {1.0f, 0.4f, 0.35f, 1.0f};
}

}

void App::drawGuide() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(display.x - 20.0f * s, 1180.0f * s), std::min(display.y - 20.0f * s, 760.0f * s)});
    ImGui::SetNextWindowBgAlpha(0.96f);
    bool open = true;
    ImGui::Begin(ICON_FA_BOOK "  Guide###guide", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                                         ImGuiWindowFlags_NoResize);
    if (!open)
        guideOpen_ = false;
    const auto& units = data_->units();
    const auto& combat = data_->combat();

    auto unitTable = [&](const char* id, const std::function<bool(const UnitDef&)>& filter) {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                                          ImGuiTableFlags_Sortable;
        if (!ImGui::BeginTable(id, 11, flags))
            return;
        ImGui::TableSetupScrollFreeze(1, 1);
        for (const char* h : {"Unit", "Cost", "HP", "Armor", "Shield", "Speed", "Weapon", "Damage", "DPS", "Range", "Notes"})
            ImGui::TableSetupColumn(h, std::string_view(h) == "Notes" ? ImGuiTableColumnFlags_WidthStretch : 0);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < units.size(); ++i) {
            const auto& u = units[i];
            if (u.design || !filter(u))
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(u.name.c_str());
            if (const int tier = data_->refitTier(static_cast<UnitDefId>(i)); tier > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("T%d", tier);
            }
            ImGui::TableNextColumn();
            if (u.cost > 0.0f)
                ImGui::Text("%.0f", u.cost);
            else if (u.refitFrom != kInvalidUnitDef)
                ImGui::TextDisabled("refit %.0f", u.refitCost);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", u.maxHp);
            ImGui::TableNextColumn();
            ImGui::Text("%s %.0f", armorTypeName(u.armorType), u.armor);
            ImGui::TableNextColumn();
            if (u.shield > 0.0f)
                ImGui::TextColored({0.35f, 0.85f, 1.0f, 1.0f}, "%.0f", u.shield);
            ImGui::TableNextColumn();
            if (u.speed > 0.0f)
                ImGui::Text("%.1f%s", u.speed, u.air ? " air" : "");
            ImGui::TableNextColumn();
            if (u.weapon) {
                ImGui::Text("%s%s", damageTypeName(u.weapon->type), u.weapon->splash > 0.0f ? " area" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%.0f / %.1fs", u.weapon->damage, u.weapon->cooldown);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", u.weapon->damage / std::max(u.weapon->cooldown, 0.05f));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f %s", u.weapon->range, targetsName(u.weapon->targets));
            } else {
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
            }
            ImGui::TableNextColumn();
            std::string notes;
            auto add = [&](const std::string& t) { notes += (notes.empty() ? "" : ", ") + t; };
            if (u.builder)
                add("builds");
            if (u.transport > 0)
                add("carries " + std::to_string(u.transport));
            if (u.repair > 0.0f)
                add("repairs " + std::to_string(static_cast<int>(u.repair)) + " hp/s");
            for (const auto a : u.auras)
                add("aura: " + data_->skill(a).name);
            if (u.income > 0.0f) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "+%.1f energy/s", u.income);
                add(buf);
            }
            if (u.produces != kInvalidUnitDef)
                add("makes " + data_->unit(u.produces).name);
            if (u.boss)
                add("boss");
            ImGui::TextDisabled("%s", notes.c_str());
        }
        ImGui::EndTable();
    };

    if (ImGui::BeginTabBar("##guidetabs")) {
        int tab = 0;
        auto item = [&](const char* name) {
            const bool select = guideTab_ == tab++;
            return ImGui::BeginTabItem(name, nullptr, select ? ImGuiTabItemFlags_SetSelected : 0);
        };
        if (item("Vehicles")) {
            ImGui::TextDisabled("Your units and their refit classes. DPS = damage per second before armor. T1/T2 = refit tier.");
            unitTable("##vehicles", [](const UnitDef& u) { return !u.structure && u.id.rfind("void_", 0) != 0 && u.id.rfind("creep_", 0) != 0; });
            ImGui::EndTabItem();
        }
        if (item("The Void")) {
            ImGui::TextDisabled("Enemy units and allied lane creeps. Crystal armor resists energy; shields regenerate after 3 s without damage.");
            unitTable("##void", [](const UnitDef& u) { return !u.structure && (u.id.rfind("void_", 0) == 0 || u.id.rfind("creep_", 0) == 0); });
            ImGui::EndTabItem();
        }
        if (item("Structures")) {
            ImGui::TextDisabled("Buildable structures need an engineer; others belong to the map.");
            unitTable("##structures", [](const UnitDef& u) { return u.structure; });
            ImGui::EndTabItem();
        }
        if (item("Damage & armor")) {
            ImGui::TextWrapped("Damage is first taken by the energy shield (shield column), then by the hull: the hull multiplier for the armor "
                               "type is applied and flat armor is subtracted (at least 1 damage, except EMP). Area weapons also hit everything "
                               "nearby on the same layer (ground or air).");
            ImGui::Spacing();
            if (ImGui::BeginTable("##combat", static_cast<int>(ArmorType::Count) + 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Damage \\ armor");
                for (int a = 0; a < static_cast<int>(ArmorType::Count); ++a)
                    ImGui::TableSetupColumn(armorTypeName(static_cast<ArmorType>(a)));
                ImGui::TableSetupColumn("shield");
                ImGui::TableHeadersRow();
                for (int dmg = 0; dmg < static_cast<int>(DamageType::Count); ++dmg) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(damageTypeName(static_cast<DamageType>(dmg)));
                    for (int a = 0; a < static_cast<int>(ArmorType::Count); ++a) {
                        ImGui::TableNextColumn();
                        const float v = combat.vsHull(static_cast<DamageType>(dmg), static_cast<ArmorType>(a));
                        ImGui::TextColored(multColor(v), "x%.2f", v);
                    }
                    ImGui::TableNextColumn();
                    const float v = combat.vsShield(static_cast<DamageType>(dmg));
                    ImGui::TextColored(multColor(v), "x%.2f", v);
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::TextDisabled("kinetic: machine guns - shreds light vehicles, weak against tanks and buildings");
            ImGui::TextDisabled("piercing: cannons - the answer to heavy armor");
            ImGui::TextDisabled("explosive: rockets, mortars, artillery - area damage, best against buildings");
            ImGui::TextDisabled("energy: Void plasma - strong against shields, weak against crystal armor");
            ImGui::TextDisabled("emp: almost no hull damage, but strips shields three times faster");
            ImGui::TextDisabled("Most guns hit only ground targets; aircraft need machine-gun towers, scouts, missiles or flak.");
            ImGui::EndTabItem();
        }
        if (item("Modules")) {
            ImGui::TextWrapped("Each vehicle has three module slots. Armor opens at Veteran, system and weapon at Elite. A new module replaces "
                               "the old one in its slot. Modules are visible on the vehicle.");
            for (int slot = 0; slot < kModuleSlots; ++slot) {
                ImGui::SeparatorText(moduleSlotName(static_cast<ModuleSlot>(slot)));
                for (const auto& m : data_->modules()) {
                    if (static_cast<int>(m.slot) != slot)
                        continue;
                    ImGui::Text("%s", m.name.c_str());
                    ImGui::SameLine(220.0f * s);
                    ImGui::TextColored(toVec(kEnergy), ICON_FA_BOLT "%.0f", m.cost);
                    ImGui::SameLine(300.0f * s);
                    std::string effect = modsText(m.mods);
                    if (m.shield > 0.0f)
                        effect += (effect.empty() ? "" : ", ") + std::string("+") + std::to_string(static_cast<int>(m.shield)) + " shield";
                    if (m.weapon) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "%s%s gun: %.0f dmg / %.2fs, range %.1f, %s", effect.empty() ? "" : ", ",
                                      damageTypeName(m.weapon->type), m.weapon->damage, m.weapon->cooldown, m.weapon->range,
                                      targetsName(m.weapon->targets));
                        effect += buf;
                    }
                    ImGui::TextDisabled("%s", effect.c_str());
                }
            }
            ImGui::EndTabItem();
        }
        if (item("Branches")) {
            ImGui::TextWrapped("Refits change a vehicle's class and keep its rank, experience, items and modules. Tier 1 needs Elite, tier 2 "
                               "needs Hero. You can switch between classes of the same tier or move down the branch.");
            std::function<void(UnitDefId)> node = [&](UnitDefId id) {
                const auto& u = data_->unit(id);
                const auto children = data_->refits(id);
                char label[160];
                if (u.refitFrom == kInvalidUnitDef)
                    std::snprintf(label, sizeof(label), "%s", u.name.c_str());
                else
                    std::snprintf(label, sizeof(label), "%s   [%s, %.0f energy]", u.name.c_str(), u.refitRank >= 3 ? "Hero" : "Elite", u.refitCost);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (children.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf;
                const bool openNode = ImGui::TreeNodeEx(u.id.c_str(), flags, "%s", label);
                ImGui::SameLine(460.0f * s);
                if (u.weapon)
                    ImGui::TextDisabled("HP %.0f  %s %.0f  %s %.0f dps, range %.1f, %s", u.maxHp, armorTypeName(u.armorType), u.armor,
                                        damageTypeName(u.weapon->type), u.weapon->damage / std::max(u.weapon->cooldown, 0.05f), u.weapon->range,
                                        targetsName(u.weapon->targets));
                else
                    ImGui::TextDisabled("HP %.0f  %s %.0f", u.maxHp, armorTypeName(u.armorType), u.armor);
                if (openNode) {
                    for (const auto c : children)
                        node(c);
                    ImGui::TreePop();
                }
            };
            for (std::size_t i = 0; i < data_->units().size(); ++i)
                if (data_->unit(static_cast<UnitDefId>(i)).refitFrom == kInvalidUnitDef && !data_->refits(static_cast<UnitDefId>(i)).empty())
                    node(static_cast<UnitDefId>(i));
            ImGui::SeparatorText("Hero ultimates");
            for (const auto& sk : data_->skills())
                if (sk.ultimate)
                    ImGui::BulletText("%s  (hero level %d, cooldown %.0fs)", sk.name.c_str(), sk.minLevel, sk.cooldown);
            ImGui::EndTabItem();
        }
        if (item("Ranks")) {
            if (ImGui::BeginTable("##ranks", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 110.0f * s);
                ImGui::TableSetupColumn("How", ImGuiTableColumnFlags_WidthFixed, 260.0f * s);
                ImGui::TableSetupColumn("Gives");
                ImGui::TableHeadersRow();
                auto row = [&](const char* rank, const std::string& how, const char* gives) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(rank);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(how.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", gives);
                };
                row("Recruit", "new unit", "base stats");
                row("Veteran", std::to_string(static_cast<int>(kVeteranXp)) + " XP (automatic)", "+15% HP and damage, armor module slot");
                row("Elite", std::to_string(static_cast<int>(kEliteXp)) + " XP + " + std::to_string(static_cast<int>(kEliteCost)) + " energy",
                    "+30% HP and damage, +1 armor, regeneration, 1 item slot, system and weapon module slots, tier 1 refits");
                row("Hero", std::to_string(static_cast<int>(kHeroXp)) + " XP + " + std::to_string(kHeroShards) + " shards, max " +
                                std::to_string(kMaxHeroes) + " per player",
                    "+50% HP, +40% damage and more per level, 6 item slots, skill points, 4 skills, ultimate from level 3, tier 2 refits");
                ImGui::EndTable();
            }
            ImGui::TextDisabled("XP comes from damage dealt and kills nearby. Hero levels go up to %d.", kMaxHeroLevel);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        guideTab_ = -1;
    }
    trackWindow();
    ImGui::End();
}

}
