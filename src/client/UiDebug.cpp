#include "App.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

constexpr glm::vec4 kSideColors[] = {{0.25f, 0.75f, 1.0f, 1.0f}, {1.0f, 0.25f, 0.55f, 1.0f}};

}

std::uint8_t App::myTeam() const {
    const auto* latest = net_.latest();
    if (!latest || net_.player() >= latest->game.teams.size() || latest->game.teams[net_.player()] == 0xff)
        return 0;
    return latest->game.teams[net_.player()];
}

glm::vec4 App::teamColor(std::uint8_t team) const {
    if (team == 0xff)
        return {0.55f, 0.55f, 0.6f, 1.0f};
    if (team == myTeam())
        return team < 2 ? kSideColors[0] : playerColor(net_.player());
    if (team < 2)
        return kSideColors[team];
    if (const auto* latest = net_.latest())
        for (std::size_t p = 0; p < latest->game.teams.size(); ++p)
            if (latest->game.teams[p] == team)
                return playerColor(static_cast<std::uint8_t>(p));
    return {0.55f, 0.55f, 0.6f, 1.0f};
}

bool App::wonMatch() const {
    const auto* latest = net_.latest();
    if (!latest || static_cast<MatchState>(latest->game.state) != MatchState::Victory)
        return false;
    return latest->game.mode != static_cast<std::uint8_t>(GameMode::Skirmish) || latest->game.winner == myTeam();
}

bool App::cheatsOn() const {
    const auto* latest = net_.latest();
    return latest && (latest->view.flags & net::kViewCheats) != 0;
}

void App::sendCheat(CheatOp op, std::vector<entt::entity> targets, glm::vec2 point) {
    CheatCommand c;
    c.op = op;
    c.unit = static_cast<std::uint16_t>(cheatUnit_);
    c.team = static_cast<std::uint8_t>(cheatTeam_);
    c.count = static_cast<std::uint8_t>(cheatCount_);
    c.point = point;
    c.targets = std::move(targets);
    net_.sendCommand(c);
}

bool App::cheatClick(glm::vec2 screen) {
    if (cheatArm_ == CheatArm::None)
        return false;
    const auto ground = camera_.screenToGround(screen.x, screen.y);
    if (!ground)
        return true;
    if (cheatArm_ == CheatArm::Spawn) {
        sendCheat(CheatOp::Spawn, {}, *ground);
        addEffect(Mesh::Cylinder, {ground->x, 0.1f, ground->y}, {1.0f, 0.4f, 1.0f, 0.8f}, 0.3f, 2.0f, 0.4f);
    } else if (const auto* e = entityAt(*ground, 0.6f)) {
        sendCheat(CheatOp::Kill, {static_cast<entt::entity>(e->id)});
    }
    if ((SDL_GetModState() & SDL_KMOD_SHIFT) == 0)
        cheatArm_ = CheatArm::None;
    return true;
}

void App::drawDebug() {
    if (!debugOpen_)
        return;
    const auto* latest = net_.latest();
    const float s = uiScale();
    ImGui::SetNextWindowPos({20.0f * s, topBarHeight_ + 20.0f * s}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360.0f * s, 0.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_BUG "  Debug  [F9]###debug", &debugOpen_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        trackWindow();
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##dbg")) {
        if (ImGui::BeginTabItem("Info")) {
            ImGui::Text("%.0f fps  %.2f ms  %s", fps_, frameMs_, renderer_.name());
            int units = 0, structures = 0, shells = 0, own = 0;
            for (const auto& e : entities_) {
                if (e.kind == net::EntityKind::Projectile) {
                    ++shells;
                    continue;
                }
                (data_->unit(e.def).structure ? structures : units)++;
                own += isOwn(e);
            }
            ImGui::Text("units %d  structures %d  shells %d  own %d", units, structures, shells, own);
            ImGui::Text("particles %d  effects %d", static_cast<int>(particles_.size()), static_cast<int>(effects_.size()));
            if (latest)
                ImGui::Text("tick %u  player %d  energy %u", latest->tick, net_.player(), latest->view.energy);
            if (host_ && host_->world())
                ImGui::Text("sim %.2f ms/tick  (embedded host)", simMs_);
            if (map_)
                ImGui::Text("map %s  %dx%d", options_.map.c_str(), map_->width(), map_->height());
            ImGui::Text("camera %.1f, %.1f  zoom %.0f", camera_.target.x, camera_.target.y, camera_.distance);
            if (const auto g = camera_.screenToGround(mouse_.x, mouse_.y))
                ImGui::Text("cursor %.1f, %.1f", g->x, g->y);
            if (!selected_.empty())
                if (const auto* e = findEntity(selected_.front()))
                    ImGui::Text("selected #%u %s  hp %d/%d", e->id, data_->unit(e->def).id.c_str(), static_cast<int>(e->hp), static_cast<int>(e->maxHp));
            ImGui::Checkbox("Stats overlay [F3]", &showStats_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Overlays")) {
            ImGui::Checkbox("Weapon ranges", &dbgRanges_);
            ImGui::Checkbox("Blocked nav cells", &dbgNav_);
            ImGui::Checkbox("Entity labels", &dbgLabels_);
            ImGui::Checkbox("Health bars for everything", &dbgBars_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cheats")) {
            const bool leader = net_.lobby().leader == net_.player();
            if (!cheatsOn()) {
                ImGui::TextWrapped("Cheats are off for this match.");
                ImGui::BeginDisabled(!leader);
                if (accentButton(ICON_FA_MAGIC "  Enable cheats", {-1.0f, 34.0f * s}))
                    net_.sendCheats(true);
                ImGui::EndDisabled();
                if (!leader)
                    ImGui::TextDisabled("only the host can enable them");
            } else {
                const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const ImVec2 b{w, 30.0f * s};
                if (ImGui::Button(ICON_FA_BOLT " +1000 energy", b))
                    sendCheat(CheatOp::Energy);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_HEART " Heal all", b))
                    sendCheat(CheatOp::Heal);
                if (ImGui::Button(ICON_FA_FORWARD " Finish all", b))
                    sendCheat(CheatOp::Finish);
                ImGui::SetItemTooltip("Completes construction, the head of every factory queue and running research");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FLASK " All research", b))
                    sendCheat(CheatOp::Research);
                if (ImGui::Button(ICON_FA_TROPHY " Hero XP", b))
                    sendCheat(CheatOp::Xp, selectedEntities());
                ImGui::SetItemTooltip("Gives the selected units enough XP to become heroes");
                ImGui::SameLine();
                const bool god = latest && (latest->view.flags & net::kViewGod) != 0;
                if (toggleButton(god ? ICON_FA_SHIELD " God: on" : ICON_FA_SHIELD " God: off", god, b))
                    sendCheat(CheatOp::God);
                if (ImGui::Button(ICON_FA_STEP_FORWARD " Next wave", b))
                    sendCheat(CheatOp::Wave);
                ImGui::SameLine();
                std::vector<entt::entity> kill = selectedEntities();
                if (inspected_)
                    kill.push_back(static_cast<entt::entity>(inspected_));
                ImGui::BeginDisabled(kill.empty());
                if (ImGui::Button(ICON_FA_TIMES " Kill selected", b)) {
                    sendCheat(CheatOp::Kill, kill);
                    selected_.clear();
                    inspected_ = 0;
                }
                ImGui::EndDisabled();
                if (ImGui::Button(ICON_FA_FLAG " Win", b))
                    sendCheat(CheatOp::Win);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FLAG_O " Lose", b))
                    sendCheat(CheatOp::Lose);
                if (toggleButton(ICON_FA_CROSSHAIRS " Kill on click", cheatArm_ == CheatArm::Kill, {-1.0f, 30.0f * s}))
                    cheatArm_ = cheatArm_ == CheatArm::Kill ? CheatArm::None : CheatArm::Kill;

                ImGui::SeparatorText("Spawn");
                cheatUnit_ = std::clamp(cheatUnit_, 0, static_cast<int>(data_->units().size()) - 1);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##unit", data_->unit(static_cast<UnitDefId>(cheatUnit_)).name.c_str(), ImGuiComboFlags_HeightLarge)) {
                    for (int i = 0; i < static_cast<int>(data_->units().size()); ++i) {
                        const auto& def = data_->unit(static_cast<UnitDefId>(i));
                        const std::string label = def.name + "  (" + def.id + ")";
                        if (ImGui::Selectable(label.c_str(), i == cheatUnit_))
                            cheatUnit_ = i;
                    }
                    ImGui::EndCombo();
                }
                if (toggleButton("Ours", cheatTeam_ == 0, {w * 0.48f, 0.0f}))
                    cheatTeam_ = 0;
                ImGui::SameLine();
                if (toggleButton("Enemy", cheatTeam_ == 1, {w * 0.48f, 0.0f}))
                    cheatTeam_ = 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderInt("##count", &cheatCount_, 1, 10, "x%d");
                if (toggleButton(ICON_FA_MAP_MARKER " Place on map (click, Shift keeps)", cheatArm_ == CheatArm::Spawn, {-1.0f, 30.0f * s}))
                    cheatArm_ = cheatArm_ == CheatArm::Spawn ? CheatArm::None : CheatArm::Spawn;
                if (leader) {
                    ImGui::Spacing();
                    if (ImGui::SmallButton("Disable cheats"))
                        net_.sendCheats(false);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    trackWindow();
    ImGui::End();

    if (dbgLabels_) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const glm::vec4 view = visibleRect(2.0f);
        for (const auto& e : entities_) {
            if (e.kind == net::EntityKind::Projectile || e.pos.x < view.x || e.pos.y < view.y || e.pos.x > view.z || e.pos.y > view.w)
                continue;
            const glm::vec2 p = camera_.worldToScreen({e.pos.x, visuals_[e.def].height + 0.9f, e.pos.y});
            char text[64];
            std::snprintf(text, sizeof(text), "#%u %s %d", e.id, data_->unit(e.def).id.c_str(), static_cast<int>(e.hp));
            dl->AddText({p.x - ImGui::CalcTextSize(text).x * 0.5f, p.y}, e.team == 0 ? IM_COL32(170, 255, 190, 230) : IM_COL32(255, 170, 170, 230), text);
        }
    }
}

void App::renderDebugOverlay() {
    const glm::vec4 view = visibleRect(4.0f);
    if (dbgRanges_)
        for (const auto& e : entities_) {
            if (e.kind == net::EntityKind::Projectile || hidden(e) || e.pos.x < view.x || e.pos.y < view.y || e.pos.x > view.z || e.pos.y > view.w)
                continue;
            const auto& def = data_->unit(e.def);
            if (!def.weapon)
                continue;
            const float d = def.weapon->range * 2.0f;
            const glm::vec4 c = e.team == 0 ? glm::vec4{0.3f, 1.0f, 0.5f, 0.07f} : glm::vec4{1.0f, 0.3f, 0.3f, 0.07f};
            renderer_.draw(Mesh::Cylinder, {e.pos.x, 0.035f, e.pos.y}, {d, 0.02f, d}, 0.0f, c, Unlit | Alpha);
        }
    if (dbgNav_) {
        const Map* nav = host_ && host_->world() ? &host_->world()->navMap() : map_.get();
        if (nav) {
            const int x0 = std::max(0, static_cast<int>(view.x)), y0 = std::max(0, static_cast<int>(view.y));
            const int x1 = std::min(nav->width() - 1, static_cast<int>(view.z)), y1 = std::min(nav->height() - 1, static_cast<int>(view.w));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if (nav->blocked({x, y}))
                        renderer_.draw(Mesh::Cube, {x + 0.5f, 0.06f, y + 0.5f}, {0.9f, 0.04f, 0.9f}, 0.0f, {1.0f, 0.2f, 0.1f, 0.25f}, Unlit | Alpha);
        }
    }
}

const net::InterpolatedEntity* App::selectedLab() const {
    for (const auto id : selected_)
        if (const auto* e = findEntity(id); e && isOwn(*e) && data_->unit(e->def).lab && !(e->flags & net::kStateBuilding))
            return e;
    return nullptr;
}

bool App::structuresOnly() const {
    if (selected_.empty())
        return false;
    for (const auto id : selected_) {
        const auto* e = findEntity(id);
        if (!e || !isOwn(*e) || !data_->unit(e->def).structure)
            return false;
    }
    return true;
}

bool App::researchReady(ResearchId t) const {
    const auto* latest = net_.latest();
    if (!latest || t >= data_->research().size() || latest->view.researched(t))
        return false;
    for (const auto a : data_->research(t).after)
        if (!latest->view.researched(a))
            return false;
    for (const auto& l : latest->view.labs)
        if (l.topic == t)
            return false;
    return true;
}

std::uint32_t App::idleLab() const {
    const auto* latest = net_.latest();
    if (!latest)
        return 0;
    if (const auto* lab = selectedLab())
        if (const auto* l = latest->view.lab(lab->id); l && l->topic == kInvalidId)
            return lab->id;
    for (const auto& l : latest->view.labs)
        if (l.topic == kInvalidId)
            if (const auto* e = findEntity(l.id); e && !(e->flags & net::kStateBuilding))
                return l.id;
    return 0;
}

void App::startResearch(ResearchId t) {
    if (const std::uint32_t lab = idleLab())
        net_.sendCommand(StructureCommand{0, static_cast<entt::entity>(lab), StructureOp::Research, static_cast<std::uint8_t>(t), {}});
}

void App::researchTooltip(ResearchId t) {
    if (!ImGui::BeginItemTooltip())
        return;
    const auto* latest = net_.latest();
    const auto& r = data_->research(t);
    ImGui::Text("%s", r.name.c_str());
    ImGui::TextDisabled(ICON_FA_BOLT "%.0f   " ICON_FA_CLOCK_O " %.0f s", r.cost, r.time);
    if (!r.desc.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(r.desc.c_str());
        ImGui::PopTextWrapPos();
    }
    const std::string mods = modsText(r.mods);
    if (!mods.empty()) {
        const char* who = r.scope == ResearchScope::Vehicles ? "vehicles" : r.scope == ResearchScope::Structures ? "buildings" : "everything";
        if (r.weapon >= 0)
            ImGui::TextColored(toVec(kGood), "%s(%s weapons, %s)", mods.c_str(), damageTypeName(static_cast<DamageType>(r.weapon)), who);
        else
            ImGui::TextColored(toVec(kGood), "%s(%s)", mods.c_str(), who);
    }
    for (const auto u : r.unlocks)
        ImGui::TextColored(toVec(kEnergy), "unlocks %s", data_->unit(u).name.c_str());
    for (const auto a : r.after)
        if (!latest || !latest->view.researched(a))
            ImGui::TextColored(toVec(kBad), "needs %s", data_->research(a).name.c_str());
    ImGui::EndTooltip();
}

void App::trackResearch() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    const auto& now = latest->view.research;
    if (knownResearch_.size() != now.size()) {
        knownResearch_ = now;
        return;
    }
    for (std::size_t i = 0; i < now.size(); ++i)
        if (now[i] && !knownResearch_[i]) {
            showBanner("RESEARCH COMPLETE: " + data_->research(static_cast<ResearchId>(i)).name, 3.0f);
            researchDot_ = true;
        }
    knownResearch_ = now;
}

void App::drawResearchTree() {
    if (!researchOpen_)
        return;
    const auto* latest = net_.latest();
    if (!latest)
        return;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.45f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (!ImGui::Begin(ICON_FA_FLASK "  Research###research", &researchOpen_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        trackWindow();
        ImGui::End();
        return;
    }
    int tiers = 0;
    for (const auto& r : data_->research())
        tiers = std::max(tiers, r.tier + 1);
    const std::uint32_t lab = idleLab();
    ImGui::TextDisabled(lab ? "Click a topic to start it in an idle laboratory." : "Build a laboratory, or wait for one to finish its topic.");
    const ImVec2 cell{150.0f * s, 46.0f * s};
    ImGui::BeginTable("##tree", tiers, ImGuiTableFlags_SizingFixedFit);
    for (int t = 0; t < tiers; ++t)
        ImGui::TableSetupColumn(("Tier " + std::to_string(t + 1)).c_str(), ImGuiTableColumnFlags_WidthFixed, cell.x);
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();
    for (int t = 0; t < tiers; ++t) {
        ImGui::TableSetColumnIndex(t);
        for (std::size_t i = 0; i < data_->research().size(); ++i) {
            const auto& r = data_->research(static_cast<ResearchId>(i));
            if (r.tier != t)
                continue;
            const auto id = static_cast<ResearchId>(i);
            const bool done = latest->view.researched(id);
            const net::OwnLab* running = nullptr;
            for (const auto& l : latest->view.labs)
                if (l.topic == id)
                    running = &l;
            const bool ready = researchReady(id);
            ImGui::PushID(static_cast<int>(i));
            const ImVec4 color = done ? ImVec4{0.16f, 0.42f, 0.22f, 1.0f} : running ? ImVec4{0.16f, 0.36f, 0.56f, 1.0f}
                                 : ready ? ImVec4{0.22f, 0.24f, 0.3f, 1.0f} : ImVec4{0.12f, 0.12f, 0.14f, 1.0f};
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            const bool affordable = latest->view.energy >= r.cost;
            if (ImGui::Button("##t", cell) && ready && lab && affordable)
                startResearch(id);
            ImGui::PopStyleColor();
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText({a.x + 6.0f * s, a.y + 4.0f * s}, done || ready || running ? kText : kDim, r.name.c_str());
            char sub[40];
            if (done)
                std::snprintf(sub, sizeof(sub), ICON_FA_CHECK " done");
            else if (running)
                std::snprintf(sub, sizeof(sub), "%d%%", running->progress * 100 / 255);
            else
                std::snprintf(sub, sizeof(sub), ICON_FA_BOLT "%.0f  %.0fs", r.cost, r.time);
            dl->AddText({a.x + 6.0f * s, b.y - ImGui::GetFontSize() - 4.0f * s}, done ? kGood : affordable ? kEnergy : kBad, sub);
            if (running)
                dl->AddRectFilled({a.x, b.y - 3.0f * s}, {a.x + (b.x - a.x) * static_cast<float>(running->progress) / 255.0f, b.y}, kGood);
            researchTooltip(id);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    trackWindow();
    ImGui::End();
}

}
