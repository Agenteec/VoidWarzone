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
    if (!ImGui::Begin(tr(ICON_FA_BUG "  Debug  [F9]###debug"), &debugOpen_, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        trackWindow();
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##dbg")) {
        if (ImGui::BeginTabItem(tr("Info"))) {
            ImGui::Text(tr("%.0f fps  %.2f ms  %s"), fps_, frameMs_, renderer_.name());
            int units = 0, structures = 0, shells = 0, own = 0;
            for (const auto& e : entities_) {
                if (e.kind == net::EntityKind::Projectile) {
                    ++shells;
                    continue;
                }
                (data_->unit(e.def).structure ? structures : units)++;
                own += isOwn(e);
            }
            ImGui::Text(tr("units %d  structures %d  shells %d  own %d"), units, structures, shells, own);
            ImGui::Text(tr("particles %d  effects %d"), static_cast<int>(particles_.size()), static_cast<int>(effects_.size()));
            if (latest)
                ImGui::Text(tr("tick %u  player %d  energy %u"), latest->tick, net_.player(), latest->view.energy);
            if (host_ && host_->world())
                ImGui::Text(tr("sim %.2f ms/tick  (embedded host)"), simMs_);
            if (map_)
                ImGui::Text(tr("map %s  %dx%d"), options_.map.c_str(), map_->width(), map_->height());
            ImGui::Text(tr("camera %.1f, %.1f  zoom %.0f"), camera_.target.x, camera_.target.y, camera_.distance);
            if (const auto g = camera_.screenToGround(mouse_.x, mouse_.y))
                ImGui::Text(tr("cursor %.1f, %.1f"), g->x, g->y);
            if (!selected_.empty())
                if (const auto* e = findEntity(selected_.front()))
                    ImGui::Text(tr("selected #%u %s  hp %d/%d"), e->id, data_->unit(e->def).id.c_str(), static_cast<int>(e->hp), static_cast<int>(e->maxHp));
            ImGui::Checkbox(tr("Stats overlay [F3]"), &showStats_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Overlays"))) {
            ImGui::Checkbox(tr("Weapon ranges"), &dbgRanges_);
            ImGui::Checkbox(tr("Blocked nav cells"), &dbgNav_);
            ImGui::Checkbox(tr("Entity labels"), &dbgLabels_);
            ImGui::Checkbox(tr("Health bars for everything"), &dbgBars_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Cheats"))) {
            const bool leader = net_.lobby().leader == net_.player();
            if (!cheatsOn()) {
                ImGui::TextWrapped(tr("Cheats are off for this match."));
                ImGui::BeginDisabled(!leader);
                if (accentButton(tr(ICON_FA_MAGIC "  Enable cheats"), {-1.0f, 34.0f * s}))
                    net_.sendCheats(true);
                ImGui::EndDisabled();
                if (!leader)
                    ImGui::TextDisabled(tr("only the host can enable them"));
            } else {
                const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const ImVec2 b{w, 30.0f * s};
                if (ImGui::Button(tr(ICON_FA_BOLT " +1000 energy"), b))
                    sendCheat(CheatOp::Energy);
                ImGui::SameLine();
                if (ImGui::Button(tr(ICON_FA_HEART " Heal all"), b))
                    sendCheat(CheatOp::Heal);
                if (ImGui::Button(tr(ICON_FA_FORWARD " Finish all"), b))
                    sendCheat(CheatOp::Finish);
                ImGui::SetItemTooltip(tr("Completes construction, the head of every factory queue and running research"));
                ImGui::SameLine();
                if (ImGui::Button(tr(ICON_FA_FLASK " All research"), b))
                    sendCheat(CheatOp::Research);
                if (ImGui::Button(tr(ICON_FA_TROPHY " Hero XP"), b))
                    sendCheat(CheatOp::Xp, selectedEntities());
                ImGui::SetItemTooltip(tr("Gives the selected units enough XP to become heroes"));
                ImGui::SameLine();
                const bool god = latest && (latest->view.flags & net::kViewGod) != 0;
                if (toggleButton(god ? ICON_FA_SHIELD " God: on" : ICON_FA_SHIELD " God: off", god, b))
                    sendCheat(CheatOp::God);
                if (ImGui::Button(tr(ICON_FA_STEP_FORWARD " Next wave"), b))
                    sendCheat(CheatOp::Wave);
                ImGui::SameLine();
                std::vector<entt::entity> kill = selectedEntities();
                if (inspected_)
                    kill.push_back(static_cast<entt::entity>(inspected_));
                ImGui::BeginDisabled(kill.empty());
                if (ImGui::Button(tr(ICON_FA_TIMES " Kill selected"), b)) {
                    sendCheat(CheatOp::Kill, kill);
                    selected_.clear();
                    inspected_ = 0;
                }
                ImGui::EndDisabled();
                if (ImGui::Button(tr(ICON_FA_FLAG " Win"), b))
                    sendCheat(CheatOp::Win);
                ImGui::SameLine();
                if (ImGui::Button(tr(ICON_FA_FLAG_O " Lose"), b))
                    sendCheat(CheatOp::Lose);
                if (toggleButton(tr(ICON_FA_CROSSHAIRS " Kill on click"), cheatArm_ == CheatArm::Kill, {-1.0f, 30.0f * s}))
                    cheatArm_ = cheatArm_ == CheatArm::Kill ? CheatArm::None : CheatArm::Kill;

                ImGui::SeparatorText(tr("Spawn"));
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
                if (toggleButton(tr("Ours"), cheatTeam_ == 0, {w * 0.48f, 0.0f}))
                    cheatTeam_ = 0;
                ImGui::SameLine();
                if (toggleButton(tr("Enemy"), cheatTeam_ == 1, {w * 0.48f, 0.0f}))
                    cheatTeam_ = 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderInt("##count", &cheatCount_, 1, 10, "x%d");
                if (toggleButton(tr(ICON_FA_MAP_MARKER " Place on map (click, Shift keeps)"), cheatArm_ == CheatArm::Spawn, {-1.0f, 30.0f * s}))
                    cheatArm_ = cheatArm_ == CheatArm::Spawn ? CheatArm::None : CheatArm::Spawn;
                if (leader) {
                    ImGui::Spacing();
                    if (ImGui::SmallButton(tr("Disable cheats")))
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
            ImGui::TextColored(toVec(kGood), tr("%s(%s weapons, %s)"), mods.c_str(), damageTypeName(static_cast<DamageType>(r.weapon)), who);
        else
            ImGui::TextColored(toVec(kGood), "%s(%s)", mods.c_str(), who);
    }
    for (const auto u : r.unlocks)
        ImGui::TextColored(toVec(kEnergy), tr("unlocks %s"), data_->unit(u).name.c_str());
    for (const auto a : r.after)
        if (!latest || !latest->view.researched(a))
            ImGui::TextColored(toVec(kBad), tr("needs %s"), data_->research(a).name.c_str());
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
            showBanner(std::string(tr("RESEARCH COMPLETE:")) + " " + data_->research(static_cast<ResearchId>(i)).name, 3.0f);
            playUi("research", 0.8f);
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
    const auto& topics = data_->research();
    int tiers = 0;
    for (const auto& r : topics)
        tiers = std::max(tiers, r.tier + 1);

    // rows: one band per branch, topics of the same tier stacked inside the band
    static constexpr std::pair<const char*, const char*> kBranches[] = {{"weapons", "Weapons"},      {"armour", "Armour"},
                                                                        {"artillery", "Artillery & rockets"}, {"air", "Air & air defence"},
                                                                        {"defences", "Defences"},    {"recon", "Recon & support"}};
    const ImVec2 cell{150.0f * s, 44.0f * s};
    const float gapX = 30.0f * s, gapY = 6.0f * s, labelW = 120.0f * s, bandGap = 14.0f * s;
    std::vector<ImVec2> pos(topics.size(), ImVec2{-1.0f, -1.0f});
    std::vector<std::pair<std::string, float>> bands;
    float y = 0.0f;
    for (const auto& [key, label] : kBranches) {
        std::vector<int> stack(static_cast<std::size_t>(tiers), 0);
        bool any = false;
        for (std::size_t i = 0; i < topics.size(); ++i)
            if (topics[i].branch == key || (topics[i].branch.empty() && std::string_view(key) == "recon")) {
                auto& n = stack[static_cast<std::size_t>(topics[i].tier)];
                pos[i] = {labelW + static_cast<float>(topics[i].tier) * (cell.x + gapX), y + static_cast<float>(n) * (cell.y + gapY)};
                ++n;
                any = true;
            }
        if (!any)
            continue;
        bands.emplace_back(label, y);
        y += static_cast<float>(*std::max_element(stack.begin(), stack.end())) * (cell.y + gapY) + bandGap;
    }
    const ImVec2 canvas{labelW + static_cast<float>(tiers) * (cell.x + gapX), y};

    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.45f}, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(canvas.x + 40.0f * s, display.x - 40.0f * s), std::min(canvas.y + 110.0f * s, display.y * 0.86f)}, ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.05f, 0.06f, 0.09f, 0.98f});
    const bool open = ImGui::Begin(tr(ICON_FA_FLASK "  Research###research"), &researchOpen_, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    if (!open) {
        trackWindow();
        ImGui::End();
        return;
    }
    trackWindow();
    const std::uint32_t lab = idleLab();
    int done = 0;
    for (const auto r : latest->view.research)
        done += r != 0;
    ImGui::TextDisabled("%s", lab ? tr("Click a topic to start it in an idle laboratory.") : tr("Build a laboratory, or wait for one to finish its topic."));
    ImGui::SameLine();
    ImGui::TextColored(toVec(kAccent), "  %d / %d", done, static_cast<int>(topics.size()));
    ImGui::SameLine();
    ImGui::TextColored(toVec(kGood), "  " ICON_FA_CHECK);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextDisabled("%s", tr("done"));
    ImGui::SameLine();
    ImGui::TextColored(toVec(kAccent), "  " ICON_FA_FLASK);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextDisabled("%s", tr("available"));
    ImGui::SameLine();
    ImGui::TextColored(toVec(kDim), "  " ICON_FA_LOCK);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextDisabled("%s", tr("locked"));

    ImGui::BeginChild("##treecanvas", {0.0f, 0.0f}, 0, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int t = 0; t < tiers; ++t) {
        char head[24];
        std::snprintf(head, sizeof(head), "%s %d", tr("Tier"), t + 1);
        dl->AddText({origin.x + labelW + static_cast<float>(t) * (cell.x + gapX), origin.y - 2.0f * s}, kDim, head);
    }
    const float headH = ImGui::GetFontSize() + 4.0f * s;
    auto at = [&](std::size_t i) { return ImVec2{origin.x + pos[i].x, origin.y + headH + pos[i].y}; };
    for (std::size_t b = 0; b < bands.size(); ++b) {
        const float top = origin.y + headH + bands[b].second;
        if (b > 0)
            dl->AddLine({origin.x, top - bandGap * 0.5f}, {origin.x + canvas.x, top - bandGap * 0.5f}, IM_COL32(255, 255, 255, 18));
        dl->AddText(ui_.bold(), ImGui::GetFontSize(), {origin.x, top + 4.0f * s}, kAccent, tr(bands[b].first));
    }

    // hover chain: everything the hovered topic needs and everything that needs it
    std::vector<char> chain(topics.size(), 0);
    if (hoveredTopic_ >= 0 && static_cast<std::size_t>(hoveredTopic_) < topics.size()) {
        std::vector<int> work{hoveredTopic_};
        chain[static_cast<std::size_t>(hoveredTopic_)] = 1;
        while (!work.empty()) {
            const int k = work.back();
            work.pop_back();
            for (const auto a : topics[static_cast<std::size_t>(k)].after)
                if (!chain[a]) {
                    chain[a] = 1;
                    work.push_back(a);
                }
        }
        // topics only depend on earlier ones, so one forward pass collects every descendant
        std::vector<char> below(topics.size(), 0);
        below[static_cast<std::size_t>(hoveredTopic_)] = 1;
        for (std::size_t j = 0; j < topics.size(); ++j)
            for (const auto a : topics[j].after)
                if (below[a])
                    below[j] = 1;
        for (std::size_t j = 0; j < topics.size(); ++j)
            if (below[j] && !chain[j])
                chain[j] = 2;
    }
    for (std::size_t i = 0; i < topics.size(); ++i)
        for (const auto a : topics[i].after) {
            const ImVec2 from{at(a).x + cell.x, at(a).y + cell.y * 0.5f};
            const ImVec2 to{at(i).x, at(i).y + cell.y * 0.5f};
            const bool lit = hoveredTopic_ >= 0 && chain[i] && chain[a];
            const bool got = latest->view.researched(a);
            const ImU32 col = lit ? kAccent : got ? IM_COL32(90, 200, 120, 150) : IM_COL32(150, 160, 180, 70);
            dl->AddBezierCubic(from, {from.x + gapX * 0.8f, from.y}, {to.x - gapX * 0.8f, to.y}, to, col, lit ? 2.5f * s : 1.3f * s);
        }

    int hovered = -1;
    for (std::size_t i = 0; i < topics.size(); ++i) {
        const auto& r = topics[i];
        const auto id = static_cast<ResearchId>(i);
        const bool got = latest->view.researched(id);
        const net::OwnLab* running = nullptr;
        for (const auto& l : latest->view.labs)
            if (l.topic == id)
                running = &l;
        const bool ready = researchReady(id);
        const bool affordable = latest->view.energy >= r.cost;
        ImGui::SetCursorScreenPos(at(i));
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##t", cell);
        if (ImGui::IsItemHovered())
            hovered = static_cast<int>(i);
        if (clicked && ready && lab && affordable)
            startResearch(id);
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const bool lit = hoveredTopic_ >= 0 && chain[i];
        const ImU32 fill = got ? IM_COL32(34, 92, 52, 255) : running ? IM_COL32(32, 78, 122, 255) : ready ? IM_COL32(48, 54, 70, 255) : IM_COL32(26, 27, 32, 255);
        dl->AddRectFilled(a, b, fill, 4.0f * s);
        dl->AddRect(a, b, lit ? kAccent : ready && !got ? IM_COL32(110, 160, 220, 160) : IM_COL32(255, 255, 255, 30), 4.0f * s, 0, lit ? 2.0f * s : 1.0f);
        const char* icon = got ? ICON_FA_CHECK : running ? ICON_FA_COG : ready ? ICON_FA_FLASK : ICON_FA_LOCK;
        dl->AddText({a.x + 5.0f * s, a.y + 3.0f * s}, got ? kGood : ready || running ? kAccent : kDim, icon);
        fitText(dl, r.name, {a.x + 20.0f * s, a.y + 1.0f}, {b.x - 2.0f, b.y - ImGui::GetFontSize() * 0.95f}, got || ready || running ? kText : kDim, 0.92f);
        char sub[40];
        if (got)
            std::snprintf(sub, sizeof(sub), "%s", tr("done"));
        else if (running)
            std::snprintf(sub, sizeof(sub), "%d%%", running->progress * 100 / 255);
        else
            std::snprintf(sub, sizeof(sub), ICON_FA_BOLT "%.0f   %.0fs", r.cost, r.time);
        const float fs = ImGui::GetFontSize() * 0.8f;
        const ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, sub);
        dl->AddText(ImGui::GetFont(), fs, {a.x + (cell.x - ss.x) * 0.5f, b.y - ss.y - 3.0f * s}, got ? kGood : affordable ? kEnergy : kBad, sub);
        if (!r.unlocks.empty())
            dl->AddText(ImGui::GetFont(), fs, {b.x - 14.0f * s, a.y + 3.0f * s}, kEnergy, ICON_FA_STAR);
        if (running)
            dl->AddRectFilled({a.x, b.y - 3.0f * s}, {a.x + (b.x - a.x) * static_cast<float>(running->progress) / 255.0f, b.y}, kGood);
        researchTooltip(id);
        ImGui::PopID();
    }
    hoveredTopic_ = hovered;
    ImGui::SetCursorScreenPos({origin.x, origin.y + headH + canvas.y});
    ImGui::Dummy({canvas.x, 1.0f});
    ImGui::EndChild();
    ImGui::End();
}

}
