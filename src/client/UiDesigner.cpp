#include "App.h"

#include <algorithm>
#include <cstdio>

#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

bool App::designSaved(UnitDefId id) const {
    return std::find(designs_.begin(), designs_.end(), data_->unit(id).id) != designs_.end();
}

bool App::hiddenDesign(UnitDefId id) const {
    return data_->unit(id).design && !designSaved(id);
}

void App::drawDesigner() {
    if (!designerOpen_)
        return;
    const auto& bodies = data_->designBodies();
    const auto& props = data_->designPropulsion();
    const auto& weapons = data_->designWeapons();
    if (bodies.empty() || props.empty() || weapons.empty())
        return;
    designBody_ = std::clamp(designBody_, 0, static_cast<int>(bodies.size()) - 1);
    designProp_ = std::clamp(designProp_, 0, static_cast<int>(props.size()) - 1);
    designWeapon_ = std::clamp(designWeapon_, 0, static_cast<int>(weapons.size()) - 1);
    const auto* latest = net_.latest();
    auto researched = [&](ResearchId r) { return r == kInvalidId || !latest || latest->view.researched(r); };

    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.45f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::Begin(tr(ICON_FA_WRENCH "  Vehicle Designer###designer"), &designerOpen_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        trackWindow();
        ImGui::End();
        return;
    }
    trackWindow();
    ImGui::TextDisabled(tr("Pick a hull, a running gear and a weapon. Saved designs appear in the factories' Production tab."));

    auto column = [&](const char* title, const std::vector<DesignPart>& parts, int& selected) {
        ImGui::BeginGroup();
        ImGui::TextColored(toVec(kAccent), "%s", title);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const bool ok = researched(parts[i].research);
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? toVec(kText) : toVec(kDim));
            std::string label = (ok ? "" : ICON_FA_LOCK "  ") + parts[i].name;
            if (ImGui::Selectable(label.c_str(), selected == static_cast<int>(i), 0, {150.0f * s, 0.0f}))
                selected = static_cast<int>(i);
            ImGui::PopStyleColor();
            if (!ok && ImGui::BeginItemTooltip()) {
                ImGui::TextColored(toVec(kBad), tr("research: %s"), data_->research(parts[i].research).name.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::EndGroup();
    };
    column("HULL", bodies, designBody_);
    ImGui::SameLine(0.0f, 18.0f * s);
    column("RUNNING GEAR", props, designProp_);
    ImGui::SameLine(0.0f, 18.0f * s);
    column("WEAPON", weapons, designWeapon_);
    ImGui::SameLine(0.0f, 24.0f * s);

    const UnitDefId id = data_->designOf(bodies[static_cast<std::size_t>(designBody_)].id, props[static_cast<std::size_t>(designProp_)].id,
                                         weapons[static_cast<std::size_t>(designWeapon_)].id);
    ImGui::BeginGroup();
    if (id != kInvalidUnitDef) {
        const auto& def = data_->unit(id);
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.2f);
        ImGui::TextUnformatted(def.name.c_str());
        ImGui::PopFont();
        ImGui::Text(tr(ICON_FA_BOLT " %.0f   build %.0f s"), def.cost, def.produceTime);
        ImGui::Text(tr("Hull %.0f hp, armour %.0f (%s)"), def.maxHp, def.armor, def.armorType == ArmorType::Light ? "light" : "heavy");
        ImGui::Text(tr("Speed %.1f, turn %.1f"), def.speed, def.turnRate);
        if (def.weapon) {
            const auto& w = *def.weapon;
            const float dps = w.damage * static_cast<float>(std::max(1, w.burst)) / std::max(0.05f, w.cooldown);
            ImGui::Text(tr("%s %.0f x%d, range %.1f, every %.1f s (%.0f dps)"), damageTypeName(w.type), w.damage, std::max(1, w.burst), w.range, w.cooldown, dps);
            ImGui::TextDisabled(tr("hits %s%s"), w.targets == 3 ? "ground and air" : w.targets == 2 ? "air only" : "ground",
                                w.splash > 0.0f ? ", splash" : "");
        } else if (def.repair > 0.0f) {
            ImGui::Text(tr("Repairs %.0f hp/s within %.0f"), def.repair, def.repairRange);
        }
        std::string factories;
        for (const auto at : def.producedAt)
            factories += (factories.empty() ? "" : ", ") + data_->unit(at).name;
        ImGui::TextDisabled(tr("made at: %s"), factories.c_str());
        bool locked = false;
        for (const auto r : def.researchAll) {
            const bool ok = researched(r);
            locked = locked || !ok;
            ImGui::TextColored(ok ? toVec(kGood) : toVec(kBad), "%s %s", ok ? ICON_FA_CHECK : ICON_FA_TIMES, data_->research(r).name.c_str());
        }
        ImGui::Spacing();
        const bool saved = designSaved(id);
        ImGui::BeginDisabled(saved || designs_.size() >= 12);
        if (ImGui::Button(saved ? ICON_FA_CHECK "  Saved" : ICON_FA_PLUS "  Save design", {200.0f * s, 34.0f * s})) {
            designs_.push_back(def.id);
            saveSettings();
            playUi("ui_confirm");
        }
        ImGui::EndDisabled();
        if (locked)
            ImGui::TextDisabled(tr("can be saved now, built once researched"));
    }
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::TextDisabled(tr("SAVED DESIGNS  %d / 12"), static_cast<int>(designs_.size()));
    for (std::size_t i = 0; i < designs_.size(); ++i) {
        const UnitDefId d = data_->find(designs_[i]);
        if (d == kInvalidUnitDef)
            continue;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(ICON_FA_TIMES)) {
            designs_.erase(designs_.begin() + static_cast<std::ptrdiff_t>(i));
            saveSettings();
            ImGui::PopID();
            break;
        }
        ImGui::SetItemTooltip(tr("Remove this design"));
        ImGui::SameLine();
        ImGui::Text("%s", data_->unit(d).name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_BOLT "%.0f", data_->unit(d).cost);
        ImGui::PopID();
    }
    ImGui::End();
}

}
