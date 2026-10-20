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
    const auto* factory = selectedFactory();
    const auto* lab = selectedLab();
    const bool buildings = structuresOnly();
    if (factory && !hadFactory_)
        dock_ = Dock::Production;
    hadFactory_ = factory != nullptr;
    if (lab && !hadLab_)
        dock_ = Dock::Research;
    hadLab_ = lab != nullptr;
    if (buildings && dock_ != Dock::None && dock_ != Dock::Orders && !(dock_ == Dock::Production && factory) && !(dock_ == Dock::Research && lab))
        dock_ = factory ? Dock::Production : lab ? Dock::Research : Dock::Orders;
    if (builders && !hadBuilders_)
        dock_ = Dock::Build;
    else if (!builders && hadBuilders_ && dock_ == Dock::Build)
        dock_ = Dock::None;
    hadBuilders_ = builders;

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

    if (dock_ == Dock::Orders && buildings) {
        const auto* first = findEntity(selected_.front());
        ImGui::TextDisabled("%s%s", first ? data_->unit(first->def).name.c_str() : "", selected_.size() > 1 ? "  (+ more)" : "");
        if (factory) {
            next();
            if (cardButton("rally", ICON_FA_FLAG, "Rally", "", true, orderArm_ == Arm::Rally, cell))
                armOrder(Arm::Rally);
            ImGui::SetItemTooltip("Set where new vehicles drive to (or right click the ground)");
        }
        if (const auto deployable = selectedDeployable(); !deployable.empty()) {
            next();
            if (cardButton("deploy", ICON_FA_TRUCK, "Pack up", "Y", true, false, cell))
                pressKey(SDLK_Y, false);
            ImGui::SetItemTooltip("Pack the HQ back into the KShM to move it [Y]");
        }
        if (lab) {
            next();
            if (cardButton("tree", ICON_FA_SITEMAP, "Tree", "F2", true, researchOpen_, cell))
                researchOpen_ = !researchOpen_;
            ImGui::SetItemTooltip("Open the research tree [F2]");
        }
        const bool canDemolish = first && !data_->unit(first->def).deployOnly;
        const bool confirm = now_ - demolishArmed_ < 3.0;
        next();
        if (cardButton("demolish", ICON_FA_TRASH, confirm ? "Confirm" : "Demolish", "", canDemolish, confirm, cell)) {
            if (confirm) {
                for (const auto e : selectedOwnStructures())
                    net_.sendCommand(StructureCommand{0, e, StructureOp::Demolish, 0, {}});
                demolishArmed_ = -10.0;
                selected_.clear();
            } else {
                demolishArmed_ = now_;
            }
        }
        ImGui::SetItemTooltip("Demolish the selected buildings, refunds half of their cost (click twice)");
    } else if (dock_ == Dock::Orders) {
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
        if (!selectedTransports().empty()) {
            next();
            if (cardButton("unload", ICON_FA_ARROW_DOWN, "Unload", "U", true, orderArm_ == Arm::Unload, cell))
                armOrder(Arm::Unload);
            ImGui::SetItemTooltip("Unload passengers at a point [U]. Right click a transport with ground units to board it.");
        }
        if (const auto deployable = selectedDeployable(); !deployable.empty()) {
            const auto* first = findEntity(entt::to_integral(deployable.front()));
            const bool pack = first && data_->unit(first->def).undeploy != kInvalidUnitDef;
            next();
            if (cardButton("deploy", pack ? ICON_FA_TRUCK : ICON_FA_HOME, pack ? "Pack up" : "Deploy", "Y", true, false, cell))
                pressKey(SDLK_Y, false);
            ImGui::SetItemTooltip(pack ? "Pack the HQ back into the KShM to move it [Y]" : "Deploy the KShM into a field HQ here [Y]. New buildings must stand near your base.");
        }
        if (const auto structures = selectedOwnStructures(); !structures.empty()) {
            const auto* first = findEntity(entt::to_integral(structures.front()));
            const bool canDemolish = first && !data_->unit(first->def).deployOnly;
            const bool confirm = now_ - demolishArmed_ < 3.0;
            next();
            if (cardButton("demolish", ICON_FA_TRASH, confirm ? "Confirm" : "Demolish", "", canDemolish, confirm, cell)) {
                if (confirm) {
                    for (const auto e : structures)
                        net_.sendCommand(StructureCommand{0, e, StructureOp::Demolish, 0, {}});
                    demolishArmed_ = -10.0;
                    selected_.clear();
                } else {
                    demolishArmed_ = now_;
                }
            }
            ImGui::SetItemTooltip("Demolish the selected buildings, refunds half of their cost (click twice)");
        }
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
        std::vector<UnitDefId> list;
        const net::OwnFactory* queue = factory ? latest->view.factory(factory->id) : nullptr;
        if (factory) {
            ImGui::TextDisabled("%s", data_->unit(factory->def).name.c_str());
            for (const auto id : data_->recruitable())
                if (const auto& at = data_->unit(id).producedAt; std::find(at.begin(), at.end(), factory->def) != at.end() && !hiddenDesign(id))
                    list.push_back(id);
        } else {
            list = productionList();
        }
        for (std::size_t i = 0; i < list.size() && i < 20; ++i) {
            const auto& def = data_->unit(list[i]);
            next();
            char cost[16];
            std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.cost);
            const char* key = i < 5 ? kRecruitKeyNames[i] : "";
            const char* missing = missingFor(def);
            const bool full = queue && queue->queue.size() >= World::kQueueSize;
            if (cardButton(def.id.c_str(), def.name.substr(0, 7).c_str(), cost, key, energy >= def.cost && !missing && !full, false, cell))
                net_.sendCommand(RecruitCommand{0, list[i], factory ? static_cast<entt::entity>(factory->id) : entt::entity{entt::null}});
            unitTooltip(def, "Recruit", key);
            if (missing && ImGui::BeginItemTooltip()) {
                ImGui::TextColored(toVec(kBad), "needs a %s", missing);
                ImGui::EndTooltip();
            }
            if (!def.producedAt.empty() && ImGui::BeginItemTooltip()) {
                ImGui::TextDisabled("build time %.0f s", def.produceTime);
                ImGui::EndTooltip();
            }
        }
        if (!data_->designBodies().empty() && (factory || (map_ && map_->mission().enabled) ||
                                               (latest && latest->game.mode == static_cast<std::uint8_t>(GameMode::Skirmish)))) {
            next();
            if (cardButton("designer", ICON_FA_WRENCH, "Design", "F4", true, designerOpen_, cell))
                designerOpen_ = !designerOpen_;
            ImGui::SetItemTooltip("Vehicle designer [F4]: combine a hull, running gear and weapon into your own designs");
        }
        if (queue) {
            column = 0;
            ImGui::Spacing();
            if (queue->queue.empty())
                ImGui::TextDisabled("queue empty - right click the ground to set a rally point");
            for (std::size_t q = 0; q < queue->queue.size(); ++q) {
                if (q > 0)
                    ImGui::SameLine();
                const auto& def = data_->unit(queue->queue[q]);
                ImGui::PushID(static_cast<int>(q) + 1000);
                const ImVec2 size{cell.x * 0.78f, cell.y * 0.62f};
                if (ImGui::Button(def.name.substr(0, 6).c_str(), size))
                    net_.sendCommand(StructureCommand{0, static_cast<entt::entity>(factory->id), StructureOp::Cancel, static_cast<std::uint8_t>(q), {}});
                if (q == 0) {
                    const ImVec2 a = ImGui::GetItemRectMin();
                    const ImVec2 b = ImGui::GetItemRectMax();
                    const float k = static_cast<float>(queue->progress) / 255.0f;
                    ImGui::GetWindowDrawList()->AddRectFilled({a.x, b.y - 4.0f * s}, {a.x + (b.x - a.x) * k, b.y}, kGood, 2.0f);
                }
                ImGui::SetItemTooltip("%s - click to cancel and refund", def.name.c_str());
                ImGui::PopID();
            }
        }
    } else if (dock_ == Dock::Research) {
        researchDot_ = false;
        const net::OwnLab* state = lab ? latest->view.lab(lab->id) : nullptr;
        int busy = 0;
        for (const auto& l : latest->view.labs)
            busy += l.topic != kInvalidId;
        int done = 0;
        for (const auto r : latest->view.research)
            done += r != 0;
        if (lab)
            ImGui::TextDisabled("%s  -  %d / %d researched", data_->unit(lab->def).name.c_str(), done, static_cast<int>(data_->research().size()));
        else
            ImGui::TextDisabled("%d labs, %d busy  -  %d / %d researched", static_cast<int>(latest->view.labs.size()), busy, done,
                                static_cast<int>(data_->research().size()));
        if (state && state->topic < data_->research().size()) {
            const auto& r = data_->research(state->topic);
            next();
            if (cardButton("current", r.name.substr(0, 8).c_str(), "cancel", "", true, true, cell))
                net_.sendCommand(StructureCommand{0, static_cast<entt::entity>(lab->id), StructureOp::Cancel, 0, {}});
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled({a.x, b.y - 4.0f * s}, {a.x + (b.x - a.x) * static_cast<float>(state->progress) / 255.0f, b.y}, kGood, 2.0f);
            researchTooltip(state->topic);
        } else if (lab) {
            int shown = 0;
            for (std::size_t i = 0; i < data_->research().size() && shown < 11; ++i) {
                const auto t = static_cast<ResearchId>(i);
                if (!researchReady(t))
                    continue;
                const auto& r = data_->research(t);
                next();
                ++shown;
                char cost[24];
                std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", r.cost);
                if (cardButton(r.id.c_str(), r.name.substr(0, 8).c_str(), cost, "", energy >= r.cost, false, cell))
                    net_.sendCommand(StructureCommand{0, static_cast<entt::entity>(lab->id), StructureOp::Research, static_cast<std::uint8_t>(t), {}});
                researchTooltip(t);
            }
            if (shown == 0)
                ImGui::TextDisabled("nothing left to research here");
        } else {
            for (const auto& l : latest->view.labs)
                if (l.topic < data_->research().size())
                    ImGui::TextDisabled(ICON_FA_FLASK " %s  %d%%", data_->research(l.topic).name.c_str(), l.progress * 100 / 255);
            if (latest->view.labs.empty())
                ImGui::TextDisabled("build a Laboratory to research upgrades");
        }
        next();
        if (cardButton("tree", ICON_FA_SITEMAP, "Tree", "F2", true, researchOpen_, cell))
            researchOpen_ = !researchOpen_;
    } else if (dock_ == Dock::Refit) {
        const auto* own = focusedOwn();
        const auto* entity = !selected_.empty() ? findEntity(selected_.front()) : nullptr;
        if (!own || !entity) {
            ImGui::TextDisabled("Select an own unit to refit it");
        } else {
            const UnitDefId current = entity->def;
            const auto variants = data_->refitOptions(current);
            ImGui::TextDisabled("%s  -  tier %d", data_->unit(current).name.c_str(), data_->refitTier(current));
            if (variants.empty())
                ImGui::TextDisabled("end of the branch");
            for (const auto v : variants) {
                next();
                const auto& def = data_->unit(v);
                const bool rankOk = own->rank >= def.refitRank;
                const bool advance = def.refitFrom == current;
                char cost[24];
                std::snprintf(cost, sizeof(cost), "%s" ICON_FA_BOLT "%.0f", advance ? ICON_FA_ARROW_UP : "", def.refitCost);
                std::string name = def.name;
                if (const auto space = name.find(' '); space != std::string::npos)
                    name = name.substr(0, space);
                if (cardButton(def.id.c_str(), name.substr(0, 8).c_str(), cost, "", rankOk && energy >= def.refitCost, false, cell))
                    for (const auto id : selected_)
                        if (const auto* e = findEntity(id); e && isOwn(*e) && e->def == current)
                            net_.sendCommand(RefitCommand{0, static_cast<entt::entity>(id), v});
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("%s %s", advance ? "Upgrade to" : "Switch to", def.name.c_str());
                    ImGui::TextDisabled("HP %.0f  %s armor %.0f  speed %.1f", def.maxHp, armorTypeName(def.armorType), def.armor, def.speed);
                    if (def.weapon)
                        ImGui::TextDisabled("%s damage %.0f%s  range %.1f  every %.1fs  (%s)", damageTypeName(def.weapon->type), def.weapon->damage,
                                            def.weapon->splash > 0.0f ? " (area)" : "", def.weapon->range, def.weapon->cooldown,
                                            def.weapon->targets == 3 ? "ground+air" : def.weapon->targets == 2 ? "air" : "ground");
                    for (const auto a : def.auras)
                        ImGui::TextColored(toVec(kGood), "aura: %s", data_->skill(a).name.c_str());
                    if (def.repair > 0.0f)
                        ImGui::TextColored(toVec(kGood), "repairs nearby vehicles and buildings, %.0f hp/s", def.repair);
                    if (const auto children = data_->refits(v); !children.empty()) {
                        std::string branch;
                        for (const auto n : children)
                            branch += (branch.empty() ? "" : ", ") + data_->unit(n).name;
                        ImGui::TextDisabled("leads to: %s", branch.c_str());
                    }
                    if (!rankOk)
                        ImGui::TextColored(toVec(kBad), "needs %s rank", def.refitRank >= 3 ? "Hero" : "Elite");
                    ImGui::EndTooltip();
                }
            }
        }
    } else if (dock_ == Dock::Modules) {
        const auto* own = focusedOwn();
        const auto* entity = !selected_.empty() ? findEntity(selected_.front()) : nullptr;
        if (!own || !entity) {
            ImGui::TextDisabled("Select an own unit to fit modules");
        } else {
            for (int slot = 0; slot < kModuleSlots; ++slot) {
                const auto need = slot == static_cast<int>(ModuleSlot::Armor) ? Rank::Veteran : Rank::Elite;
                const bool unlocked = own->rank >= static_cast<std::uint8_t>(need);
                column = 0;
                ImGui::TextDisabled("%s%s", moduleSlotName(static_cast<ModuleSlot>(slot)), unlocked ? "" : need == Rank::Veteran ? "  (Veteran)" : "  (Elite)");
                for (std::size_t i = 0; i < data_->modules().size(); ++i) {
                    const auto& def = data_->module(static_cast<ModuleId>(i));
                    if (static_cast<int>(def.slot) != slot)
                        continue;
                    next();
                    const bool fitted = entity->modules[static_cast<std::size_t>(slot)] == i;
                    char cost[16];
                    std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.cost);
                    std::string name = def.name;
                    if (const auto space = name.find(' '); space != std::string::npos && name.size() > 9)
                        name = name.substr(0, space);
                    if (cardButton(def.id.c_str(), name.substr(0, 9).c_str(), cost, "", unlocked && !fitted && energy >= def.cost, fitted, cell))
                        for (const auto id : selected_)
                            if (const auto* e = findEntity(id); e && isOwn(*e) && e->modules[static_cast<std::size_t>(slot)] != i)
                                net_.sendCommand(ModuleCommand{0, static_cast<entt::entity>(id), static_cast<std::uint16_t>(i)});
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::Text("%s  (%s slot)", def.name.c_str(), moduleSlotName(def.slot));
                        const std::string mods = modsText(def.mods);
                        if (!mods.empty())
                            ImGui::TextUnformatted(mods.c_str());
                        if (def.shield > 0.0f)
                            ImGui::TextColored({0.35f, 0.85f, 1.0f, 1.0f}, "+%.0f energy shield", def.shield);
                        if (def.weapon)
                            ImGui::TextDisabled("second weapon: %s %.0f dmg, range %.1f, every %.1fs, hits %s", damageTypeName(def.weapon->type),
                                                def.weapon->damage, def.weapon->range, def.weapon->cooldown,
                                                def.weapon->targets == 3 ? "ground and air" : def.weapon->targets == 2 ? "air" : "ground");
                        ImGui::TextDisabled("replaces the module in this slot");
                        ImGui::EndTooltip();
                    }
                }
            }
        }
    } else if (dock_ == Dock::Build) {
        const auto& list = data_->buildable();
        for (std::size_t i = 0; i < list.size() && i < 16; ++i) {
            next();
            const auto& def = data_->unit(list[i]);
            char cost[16];
            std::snprintf(cost, sizeof(cost), ICON_FA_BOLT "%.0f", def.cost);
            const char* key = i < 5 ? kBuildKeyNames[i] : "";
            const bool armed = buildArmed_ == static_cast<int>(list[i]);
            const char* missing = missingFor(def);
            if (cardButton(def.id.c_str(), def.name.substr(0, 8).c_str(), cost, touch ? "" : key, builders && !missing && (energy >= def.cost || armed), armed, cell))
                armBuild(i);
            unitTooltip(def, "Build", key);
            if (missing && ImGui::BeginItemTooltip()) {
                ImGui::TextColored(toVec(kBad), "needs a %s", missing);
                ImGui::EndTooltip();
            }
            if (def.produces != kInvalidUnitDef && ImGui::BeginItemTooltip()) {
                ImGui::TextDisabled("makes a %s every %.0fs (up to %d)", data_->unit(def.produces).name.c_str(), def.produceInterval, def.produceMax);
                ImGui::EndTooltip();
            }
        }
    }
    if (dock_ != Dock::None)
        ImGui::Separator();

    bool skillDot = false;
    for (const auto& u : latest->view.units)
        skillDot |= u.skillPoints > 0;
    if (dock_ == Dock::Refit)
        refitDot_ = false;
    if (dock_ == Dock::Modules)
        modulesDot_ = false;
    bool dot = false;
    auto tabButton = [&](const char* icon, const char* tip, bool active) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? toVec(IM_COL32(40, 110, 160, 255)) : ImVec4{0.14f, 0.16f, 0.2f, 0.9f});
        const bool pressed = ImGui::Button(icon, tab);
        ImGui::PopStyleColor();
        if (dot && !active)
            redDot(ImGui::GetWindowDrawList(), {ImGui::GetItemRectMax().x - 4.0f * s, ImGui::GetItemRectMin().y + 4.0f * s}, 4.5f * s);
        dot = false;
        ImGui::SetItemTooltip("%s", tip);
        ImGui::SameLine();
        return pressed;
    };
    auto toggleDock = [&](Dock d) { dock_ = dock_ == d ? Dock::None : d; };
    ImGui::PushID("tabs");
    if (tabButton(buildings ? ICON_FA_BUILDING "##o" : ICON_FA_CROSSHAIRS "##o", buildings ? "Building actions" : "Orders: move, attack, patrol, hold",
                  dock_ == Dock::Orders))
        toggleDock(Dock::Orders);
    if ((!buildings || factory) && tabButton(ICON_FA_INDUSTRY "##p", "Production: recruit units", dock_ == Dock::Production))
        toggleDock(Dock::Production);
    dot = researchDot_;
    if ((!buildings || lab) && tabButton(ICON_FA_FLASK "##q", "Research: upgrades and new vehicles (select a laboratory)", dock_ == Dock::Research))
        toggleDock(Dock::Research);
    if (buildings) {
        ImGui::NewLine();
        ImGui::PopID();
        trackWindow();
        commandCardLeft_ = ImGui::GetWindowPos().x;
        ImGui::End();
        return;
    }
    dot = refitDot_;
    if (tabButton(ICON_FA_COGS "##r", "Refit: change the class of an Elite vehicle", dock_ == Dock::Refit))
        toggleDock(Dock::Refit);
    dot = modulesDot_;
    if (tabButton(ICON_FA_CUBE "##m", "Modules: armor (Veteran), system and weapon (Elite)", dock_ == Dock::Modules))
        toggleDock(Dock::Modules);
    dot = skillDot;
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
