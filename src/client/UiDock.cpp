#include "App.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <SDL3/SDL.h>

#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

bool cardButton(const char* id, const char* label, const char* sub, const char* key, bool enabled, bool active, ImVec2 size) {
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, toVec(IM_COL32(40, 110, 160, 255)));
    const bool pressed = ImGui::Button("##card", size);
    if (active)
        ImGui::PopStyleColor();
    ImGui::EndDisabled();
    const ImVec2 a = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text = enabled ? kText : kDim;
    const ImVec2 ls = ImGui::CalcTextSize(label);
    const float labelY = sub && *sub ? a.y + size.y * 0.42f - ls.y * 0.5f : a.y + (size.y - ls.y) * 0.5f;
    dl->AddText({a.x + (size.x - ls.x) * 0.5f, labelY}, text, label);
    if (sub && *sub) {
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 0.78f;
        const ImVec2 ss = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, sub);
        dl->AddText(font, fs, {a.x + (size.x - ss.x) * 0.5f, a.y + size.y - ss.y - 3.0f}, enabled ? kEnergy : kDim, sub);
    }
    if (key && *key) {
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, ImGui::GetFontSize() * 0.72f, {a.x + 4.0f, a.y + 2.0f}, enabled ? IM_COL32(255, 210, 80, 200) : kDim, key);
    }
    ImGui::PopID();
    return pressed && enabled;
}

void unitTooltip(const UnitDef& def, const char* action, const char* key) {
    if (!ImGui::BeginItemTooltip())
        return;
    if (key && *key)
        ImGui::Text("%s %s  [%s]", action, def.name.c_str(), key);
    else
        ImGui::Text("%s %s", action, def.name.c_str());
    ImGui::TextDisabled("HP %.0f  %s armor %.0f", def.maxHp, armorTypeName(def.armorType), def.armor);
    if (def.speed > 0.0f)
        ImGui::TextDisabled("speed %.1f", def.speed);
    if (def.weapon)
        ImGui::TextDisabled("%s damage %.0f%s  range %.1f  every %.1fs", damageTypeName(def.weapon->type), def.weapon->damage,
                                def.weapon->splash > 0.0f ? " (area)" : "", def.weapon->range, def.weapon->cooldown);
    if (def.income > 0.0f)
        ImGui::TextDisabled("+%.1f energy/s", def.income);
    ImGui::EndTooltip();
}

}

void App::drawDock() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    const float s = uiScale();
    const bool touch = touchLayout();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;
    const ImVec2 cell = touch ? ImVec2{76.0f * s, 56.0f * s} : ImVec2{66.0f * s, 52.0f * s};
    const ImVec2 tab = touch ? ImVec2{52.0f * s, 46.0f * s} : ImVec2{40.0f * s, 34.0f * s};
    const float energy = static_cast<float>(latest->view.energy);
    const bool hasSelection = !selected_.empty();
    const bool builders = !selectedBuilders().empty();

    ImGui::SetNextWindowPos({display.x - pad, display.y - pad}, ImGuiCond_Always, {1.0f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f * s, 6.0f * s});
    ImGui::Begin("##dock", nullptr, kHudFlags);
    ImGui::PopStyleVar();

    int column = 0;
    auto next = [&] {
        if (column++ % 4 != 0)
            ImGui::SameLine();
    };

    if (dock_ == Dock::Orders) {
        const auto* own = focusedOwn();
        const auto* entity = hasSelection ? findEntity(selected_.front()) : nullptr;
        next();
        if (cardButton("move", ICON_FA_ARROWS, "Move", "M", hasSelection, orderArm_ == Arm::Move, cell))
            armOrder(Arm::Move);
        ImGui::SetItemTooltip("Move, ignoring enemies [M]. Shift queues orders.");
        next();
        if (cardButton("attack", ICON_FA_CROSSHAIRS, "Attack", "A", hasSelection, orderArm_ == Arm::Attack, cell))
            armOrder(Arm::Attack);
        ImGui::SetItemTooltip("Attack-move: fight everything on the way [A]");
        next();
        if (cardButton("patrol", ICON_FA_RETWEET, "Patrol", "T", hasSelection, orderArm_ == Arm::Patrol, cell))
            armOrder(Arm::Patrol);
        ImGui::SetItemTooltip("Patrol between here and the point, fighting on the way [T]");
        next();
        if (cardButton("hold", ICON_FA_HAND_PAPER_O, "Hold", "H", hasSelection, false, cell))
            pressKey(SDLK_H, false);
        ImGui::SetItemTooltip("Hold position: shoot only what is in range [H]");
        next();
        if (cardButton("stop", ICON_FA_STOP, "Stop", "S", hasSelection, false, cell))
            pressKey(SDLK_S, false);
        ImGui::SetItemTooltip("Stop and clear queued orders [S]");

        const auto rank = own ? static_cast<Rank>(std::min<int>(own->rank, 3)) : Rank::Recruit;
        bool canPromote = false;
        char promoteSub[24] = "Promote";
        const char* promoteTip = "Select a unit to promote it";
        if (own && entity) {
            if (rank == Rank::Recruit || rank == Rank::Veteran) {
                canPromote = static_cast<float>(own->xp) >= kEliteXp && energy >= kEliteCost;
                std::snprintf(promoteSub, sizeof(promoteSub), ICON_FA_BOLT "%.0f", kEliteCost);
                promoteTip = "Promote to Elite: 200 XP and energy. Elites carry items.";
            } else if (rank == Rank::Elite && !data_->unit(entity->def).skills.empty()) {
                canPromote = static_cast<float>(own->xp) >= kHeroXp && latest->view.shards >= kHeroShards;
                std::snprintf(promoteSub, sizeof(promoteSub), ICON_FA_DIAMOND "%d", kHeroShards);
                promoteTip = "Raise a Hero: 400 XP and shards. Heroes learn skills.";
            } else {
                promoteTip = rank == Rank::Hero ? "Already a hero" : "This unit cannot become a hero";
            }
        }
        next();
        if (cardButton("promote", rank == Rank::Elite ? ICON_FA_TROPHY : ICON_FA_ARROW_UP, promoteSub, "P", canPromote, false, cell))
            pressKey(SDLK_P, false);
        ImGui::SetItemTooltip("%s [P]", promoteTip);
    } else if (dock_ == Dock::Skills) {
        const auto* own = focusedOwn();
        const auto rank = own ? static_cast<Rank>(std::min<int>(own->rank, 3)) : Rank::Recruit;
        for (std::size_t i = 0; i < 4; ++i) {
            next();
            const bool has = own && rank == Rank::Hero && i < own->skills.size();
            if (!has) {
                cardButton(kSkillKeys[i], "", "", kSkillKeys[i], false, false, cell);
                ImGui::SetItemTooltip("Hero skill slot [%s]", kSkillKeys[i]);
                continue;
            }
            const auto& slot = own->skills[i];
            const auto& skill = data_->skill(slot.skill);
            const std::string shortName = skill.name.substr(0, 7);
            char lvl[8];
            std::snprintf(lvl, sizeof(lvl), "lvl %d", slot.level);
            if (cardButton(kSkillKeys[i], shortName.c_str(), lvl, kSkillKeys[i], !skill.passive, armedSkill_ == static_cast<int>(i), cell))
                pressKey(i == 0 ? SDLK_Q : i == 1 ? SDLK_W : i == 2 ? SDLK_E : SDLK_R, false);
            skillTooltip(skill, slot.level);
            if (slot.cooldown > 0) {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                const float frac = std::clamp(static_cast<float>(slot.cooldown) / 10.0f / std::max(skill.cooldown, 0.1f), 0.0f, 1.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled({a.x, b.y - (b.y - a.y) * frac}, b, IM_COL32(0, 0, 0, 160), 4.0f);
                char cd[8];
                std::snprintf(cd, sizeof(cd), "%d", (slot.cooldown + 9) / 10);
                const ImVec2 cs = ImGui::CalcTextSize(cd);
                dl->AddText({a.x + (b.x - a.x - cs.x) * 0.5f, a.y + (b.y - a.y - cs.y) * 0.5f}, IM_COL32(255, 255, 255, 255), cd);
            }
        }
        next();
        char points[16] = "Learn";
        if (own && own->skillPoints > 0)
            std::snprintf(points, sizeof(points), "%d pts", own->skillPoints);
        if (cardButton("learn", ICON_FA_BOOK, points, "L", true, panel_ == Panel::Learn, cell))
            panel_ = panel_ == Panel::Learn ? Panel::None : Panel::Learn;
        ImGui::SetItemTooltip("Learn hero skills [L]");
    } else if (dock_ == Dock::Production) {
        const auto& list = data_->recruitable();
        for (std::size_t i = 0; i < list.size() && i < 8; ++i) {
            next();
            const auto& def = data_->unit(list[i]);
            char cost[16];
            std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.cost);
            const char* key = i < 3 ? kRecruitKeyNames[i] : "";
            if (cardButton(def.id.c_str(), def.name.substr(0, 7).c_str(), cost, key, energy >= def.cost, false, cell))
                net_.sendCommand(RecruitCommand{0, list[i]});
            unitTooltip(def, "Recruit", key);
        }
    } else if (dock_ == Dock::Refit) {
        const auto* own = focusedOwn();
        const auto* entity = !selected_.empty() ? findEntity(selected_.front()) : nullptr;
        if (!own || !entity) {
            ImGui::TextDisabled("Select an own unit to refit it");
        } else {
            const UnitDefId current = entity->def;
            const UnitDefId base = data_->baseOf(current);
            const auto variants = data_->refits(base);
            const bool elite = own->rank >= static_cast<std::uint8_t>(Rank::Elite);
            if (variants.empty())
                ImGui::TextDisabled("%s has no refits", data_->unit(current).name.c_str());
            for (const auto v : variants) {
                next();
                const auto& def = data_->unit(v);
                char cost[16];
                std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.refitCost);
                std::string name = def.name;
                if (const auto space = name.find(' '); space != std::string::npos)
                    name = name.substr(0, space);
                if (cardButton(def.id.c_str(), name.substr(0, 8).c_str(), cost, "", elite && v != current && energy >= def.refitCost, v == current, cell))
                    for (const auto id : selected_)
                        if (const auto* e = findEntity(id); e && isOwn(*e) && data_->baseOf(e->def) == base && e->def != v)
                            net_.sendCommand(RefitCommand{0, static_cast<entt::entity>(id), v});
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("Refit to %s", def.name.c_str());
                    ImGui::TextDisabled("HP %.0f  %s armor %.0f  speed %.1f", def.maxHp, armorTypeName(def.armorType), def.armor, def.speed);
                    if (def.weapon)
                        ImGui::TextDisabled("%s damage %.0f%s  range %.1f  every %.1fs", damageTypeName(def.weapon->type), def.weapon->damage,
                                            def.weapon->splash > 0.0f ? " (area)" : "", def.weapon->range, def.weapon->cooldown);
                    for (const auto a : def.auras)
                        ImGui::TextColored(toVec(kGood), "aura: %s", data_->skill(a).name.c_str());
                    if (def.repair > 0.0f)
                        ImGui::TextColored(toVec(kGood), "repairs nearby vehicles and buildings, %.0f hp/s", def.repair);
                    if (!elite)
                        ImGui::TextColored(toVec(kBad), "needs Elite rank");
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (dock_ == Dock::Build) {
        const auto& list = data_->buildable();
        for (std::size_t i = 0; i < list.size() && i < 8; ++i) {
            next();
            const auto& def = data_->unit(list[i]);
            char cost[16];
            std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.cost);
            const char* key = i < 5 ? kBuildKeyNames[i] : "";
            const bool armed = buildArmed_ == static_cast<int>(list[i]);
            if (cardButton(def.id.c_str(), def.name.substr(0, 8).c_str(), cost, touch ? "" : key, builders && (energy >= def.cost || armed), armed, cell))
                armBuild(i);
            unitTooltip(def, "Build", key);
            if (def.produces != kInvalidUnitDef && ImGui::BeginItemTooltip()) {
                ImGui::TextDisabled("makes a %s every %.0fs (up to %d)", data_->unit(def.produces).name.c_str(), def.produceInterval, def.produceMax);
                ImGui::EndTooltip();
            }
        }
    }
    if (dock_ != Dock::None)
        ImGui::Separator();

    auto tabButton = [&](const char* icon, const char* tip, bool active) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? toVec(IM_COL32(40, 110, 160, 255)) : ImVec4{0.14f, 0.16f, 0.2f, 0.9f});
        const bool pressed = ImGui::Button(icon, tab);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tip);
        ImGui::SameLine();
        return pressed;
    };
    auto toggleDock = [&](Dock d) { dock_ = dock_ == d ? Dock::None : d; };
    ImGui::PushID("tabs");
    if (tabButton(ICON_FA_CROSSHAIRS "##o", "Orders: move, attack, patrol, hold", dock_ == Dock::Orders))
        toggleDock(Dock::Orders);
    if (tabButton(ICON_FA_INDUSTRY "##p", "Production: recruit units", dock_ == Dock::Production))
        toggleDock(Dock::Production);
    if (tabButton(ICON_FA_COGS "##r", "Refit: change the class of an Elite vehicle", dock_ == Dock::Refit))
        toggleDock(Dock::Refit);
    if (tabButton(ICON_FA_BOLT "##s", "Hero skills", dock_ == Dock::Skills))
        toggleDock(Dock::Skills);
    if (tabButton(ICON_FA_WRENCH "##b", "Build (select an engineer): walls, turrets, factories", dock_ == Dock::Build))
        toggleDock(Dock::Build);
    if (tabButton(ICON_FA_SHOPPING_CART "##shop", "Shop [B]", panel_ == Panel::Shop))
        panel_ = panel_ == Panel::Shop ? Panel::None : Panel::Shop;
    if (tabButton(ICON_FA_ARCHIVE "##stash", "Stash [I]", panel_ == Panel::Stash))
        panel_ = panel_ == Panel::Stash ? Panel::None : Panel::Stash;
    ImGui::NewLine();
    ImGui::PopID();

    trackWindow();
    commandCardLeft_ = ImGui::GetWindowPos().x;
    ImGui::End();
}

}
