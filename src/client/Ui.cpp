#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <imgui.h>

#include "core/Version.h"
#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

void App::trackWindow() {
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    uiRects_.push_back({p.x, p.y, p.x + s.x, p.y + s.y});
}

bool App::overUi(glm::vec2 p) const {
    for (const auto& r : uiRects_)
        if (p.x >= r.x && p.y >= r.y && p.x <= r.z && p.y <= r.w)
            return true;
    return false;
}

float App::uiScale() const {
#if defined(SDL_PLATFORM_ANDROID)
    return 2.2f / renderer_.scale() * uiScaleUser_;
#else
    return std::max(1.0f, SDL_GetWindowDisplayScale(window_)) * uiScaleUser_ * (touchLayout() ? 1.15f : 1.0f);
#endif
}

void App::drawUi() {
    uiRects_.clear();
    if (screen_ == Screen::Game && map_) {
        drawHud();
    } else if (screen_ == Screen::Editor && editor_) {
        drawEditor();
    } else if (screen_ == Screen::Campaign) {
        drawCampaign();
    } else {
        drawMenu();
    }
}

void App::drawHud() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    if (touchLayout())
        drawTouchHud();
    else
        drawDesktopHud();
    if (showStats_)
        drawStats();
    drawFloaters();
    drawLevelUps();
    if (guideOpen_)
        drawGuide();
    drawResearchTree();
    drawDesigner();
    drawDebug();

    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto state = static_cast<MatchState>(latest->game.state);
    if (state == MatchState::Playing)
        endDismissed_ = false;
    if (state != MatchState::Playing && !endDismissed_) {
        drawEndScreen();
    } else if (state != MatchState::Playing) {
        ImGui::SetNextWindowPos({display.x * 0.5f, 8.0f * s}, ImGuiCond_Always, {0.5f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##ended", nullptr, kHudFlags);
        const bool victory = wonMatch();
        ImGui::TextColored(toVec(victory ? kGood : kBad), "%s", victory ? "VICTORY" : "DEFEAT");
        ImGui::SameLine();
        ImGui::TextDisabled(tr("free play"));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Results")))
            endDismissed_ = false;
        trackWindow();
        ImGui::End();
    } else if (bannerTime_ > 0.0f && !banner_.empty()) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float size = 46.0f * s;
        const float alpha = std::clamp(bannerTime_ / 0.6f, 0.0f, 1.0f);
        const ImVec2 ts = ui_.bold()->CalcTextSizeA(size, FLT_MAX, 0.0f, banner_.c_str());
        const ImVec2 p{(display.x - ts.x) * 0.5f, display.y * 0.27f};
        fg->AddText(ui_.bold(), size, {p.x + 3.0f, p.y + 3.0f}, IM_COL32(0, 0, 0, static_cast<int>(170 * alpha)), banner_.c_str());
        fg->AddText(ui_.bold(), size, p, IM_COL32(255, 214, 90, static_cast<int>(255 * alpha)), banner_.c_str());
    }

    if (buildArmed_ >= 0) {
        ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.7f}, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::Begin("##armedBuild", nullptr, kHudFlags);
        ImGui::TextColored(toVec(kEnergy), tr(ICON_FA_WRENCH "  Place %s"), data_->unit(static_cast<UnitDefId>(buildArmed_)).name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Cancel")))
            buildArmed_ = -1;
        trackWindow();
        ImGui::End();
    } else if ((orderArm_ != Arm::None) || armedSkill_ >= 0) {
        ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.7f}, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::Begin("##armed", nullptr, kHudFlags);
        ImGui::TextColored(toVec(kEnergy), ICON_FA_CROSSHAIRS "  %s", orderArm_ == Arm::Attack   ? "Pick a point to attack-move"
                           : orderArm_ == Arm::Patrol ? "Pick the patrol point"
                           : orderArm_ == Arm::Move   ? "Pick a point to move"
                                                      : "Pick the skill target");
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Cancel"))) {
            orderArm_ = Arm::None;
            armedSkill_ = -1;
        }
        trackWindow();
        ImGui::End();
    }
    if (pauseOpen_)
        drawPauseMenu();
}

void App::drawTouchHud() {
    drawTopBar();
    drawBossBar();
    hudBottom_ = 82.0f * uiScale();
    drawMinimap(110.0f * uiScale());
    drawUnitPanel();
    drawDock();
    drawSidePanel();
    drawGroupBar();
    drawInspect();
    drawChat();
}

void App::drawTopBar() {
    const auto* latest = net_.latest();
    const auto& game = latest->game;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize({display.x, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f * s, 6.0f * s});
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("##top", nullptr, kHudFlags & ~ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(2);

    const auto& defense = map_->defense();
    ImGui::BeginGroup();
    if (drawObjectives()) {
    } else {
    ImGui::PushFont(ui_.bold(), 0.0f);
    if (game.totalWaves == 0)
        ImGui::Text(tr(ICON_FA_FLAG "  WAVE %d"), game.wave);
    else if (game.wave < game.totalWaves)
        ImGui::Text(tr(ICON_FA_FLAG "  WAVE %d / %d"), game.wave, game.totalWaves);
    else
        ImGui::TextColored(toVec(kVoid), tr(ICON_FA_FLAG "  FINAL WAVE"));
    ImGui::PopFont();
    if ((game.totalWaves == 0 || game.wave < game.totalWaves) && !defense.waves.empty()) {
        const auto& next = defense.waves[static_cast<std::size_t>(game.wave) % defense.waves.size()];
        const float interval = game.wave == 0 ? defense.firstWaveDelay : (next.delay > 0.0f ? next.delay : defense.waveInterval);
        const float left = static_cast<float>(game.nextWaveIn) / 10.0f;
        char label[32];
        std::snprintf(label, sizeof(label), tr("next in %.0fs"), left);
        bar(1.0f - left / std::max(interval, 1.0f), {150.0f * s, 14.0f * s}, IM_COL32(255, 170, 60, 220), label);
    } else {
        const int anchors = anchorsAlive();
        if (anchors > 0)
            ImGui::TextColored({0.8f, 0.5f, 1.0f, 1.0f}, tr("destroy the pylons (%d)"), anchors);
        else
            ImGui::TextDisabled(tr("destroy the portal"));
    }
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 28.0f * s);
    ImGui::BeginGroup();
    const auto& points = map_->resourcePoints();
    for (std::size_t i = 0; i < points.size() && i < game.points.size(); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, 6.0f * s);
        const auto& info = game.points[i];
        const ImU32 color = info.owner == 0xff ? IM_COL32(120, 125, 140, 255) : info.owner == myTeam() ? kAccent : info.owner == 1 ? kVoid
                                                                                                        : ImGui::ColorConvertFloat4ToU32(ImVec4{teamColor(info.owner).r, teamColor(info.owner).g, teamColor(info.owner).b, 1.0f});
        const ImVec2 size{30.0f * s, 34.0f * s};
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 c = toVec(color);
        dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ImGui::ColorConvertFloat4ToU32({c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, 0.95f}), 5.0f * s);
        dl->AddRect(p, {p.x + size.x, p.y + size.y}, color, 5.0f * s, 0, 1.5f * s);
        const ImVec2 ts = ImGui::CalcTextSize(points[i].name.c_str());
        dl->AddText({p.x + (size.x - ts.x) * 0.5f, p.y + 4.0f * s}, IM_COL32(255, 255, 255, 235), points[i].name.c_str());
        const float control = static_cast<float>(info.control) / 127.0f;
        const float w = std::abs(control) * (size.x - 6.0f * s);
        if (w > 0.5f)
            dl->AddRectFilled({p.x + 3.0f * s, p.y + size.y - 7.0f * s}, {p.x + 3.0f * s + w, p.y + size.y - 3.0f * s}, control >= 0.0f ? kAccent : kVoid,
                              2.0f);
        ImGui::Dummy(size);
        if (ImGui::BeginItemTooltip()) {
            ImGui::Text(tr("Point %s  +%.0f energy/s"), points[i].name.c_str(), points[i].income);
            ImGui::TextDisabled("%s", info.owner == myTeam() ? "held by your team" : info.owner == 1 ? "held by the enemy" : info.owner == 0xff ? "neutral - stand on it to capture" : "held by another team");
            ImGui::EndTooltip();
        }
    }
    ImGui::EndGroup();

    float income = defense.baseIncome;
    for (std::size_t i = 0; i < points.size() && i < game.points.size(); ++i)
        if (game.points[i].owner == myTeam())
            income += points[i].income;
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit && (game.mode == static_cast<std::uint8_t>(GameMode::Skirmish) ? isOwn(e) : e.team == 0) && e.hp > 0)
            income += data_->unit(e.def).income;

    char energy[48];
    std::snprintf(energy, sizeof(energy), ICON_FA_BOLT " %u", latest->view.energy);
    char rate[24];
    std::snprintf(rate, sizeof(rate), "+%.1f/s", income);
    char shards[24];
    std::snprintf(shards, sizeof(shards), ICON_FA_DIAMOND " %u", latest->view.shards);
    std::string room;
    if (host_ && lobby_.state() == net::MasterClient::State::Hosting)
        room = std::string(ICON_FA_USERS " ") + lobby_.code();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float menuW = 44.0f * s;
    float right = ImGui::CalcTextSize(energy).x + ImGui::CalcTextSize(rate).x + ImGui::CalcTextSize(shards).x + menuW + spacing * 6.0f;
    if (!room.empty())
        right += ImGui::CalcTextSize(room.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + spacing * 2.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - right));
    const float y = ImGui::GetCursorPosY();
    if (!room.empty()) {
        ImGui::SetCursorPosY(y + 4.0f * s);
        if (ImGui::SmallButton(room.c_str()))
            SDL_SetClipboardText(lobby_.code().c_str());
        ImGui::SetItemTooltip(tr("Room code - click to copy. Friends join via \"Join by room code\"."));
        ImGui::SameLine(0.0f, spacing * 2.0f);
    }
    ImGui::SetCursorPosY(y + 6.0f * s);
    ImGui::PushFont(ui_.bold(), 0.0f);
    ImGui::TextColored(toVec(kEnergy), "%s", energy);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(y + 6.0f * s);
    ImGui::TextDisabled("%s", rate);
    ImGui::SameLine(0.0f, spacing * 2.0f);
    ImGui::SetCursorPosY(y + 6.0f * s);
    ImGui::PushFont(ui_.bold(), 0.0f);
    ImGui::TextColored(toVec(kShard), "%s", shards);
    ImGui::PopFont();
    ImGui::SetItemTooltip(tr("Shards - drop from bosses, used to raise heroes and buy rare items"));
    ImGui::SameLine(0.0f, spacing * 2.0f);
    ImGui::SetCursorPosY(y);
    if (ImGui::Button(ICON_FA_BARS, {menuW, 36.0f * s}))
        pauseOpen_ = !pauseOpen_;
    trackWindow();
    topBarHeight_ = ImGui::GetWindowSize().y;
    ImGui::End();
}

void App::drawBossBar() {
    const auto& game = net_.latest()->game;
    if (game.boss == net::kNoEntity)
        return;
    const auto* boss = findEntity(game.boss);
    if (!boss)
        return;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto& def = data_->unit(boss->def);
    const float frac = static_cast<float>(boss->hp) / static_cast<float>(std::max<std::uint16_t>(boss->maxHp, 1));
    ImGui::SetNextWindowPos({display.x * 0.5f, (touchLayout() ? topBarHeight_ : 0.0f) + 8.0f * s}, ImGuiCond_Always, {0.5f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##boss", nullptr, kHudFlags);
    ImGui::PushFont(ui_.bold(), 0.0f);
    centeredText((def.boss ? def.boss->title : def.name).c_str(), kVoid);
    ImGui::PopFont();
    char label[48];
    std::snprintf(label, sizeof(label), "%u / %u", boss->hp, boss->maxHp);
    bar(frac, {std::min(display.x * 0.4f, 520.0f * s), 18.0f * s}, IM_COL32(220, 40, 90, 255), label);
    trackWindow();
    ImGui::End();
}

void App::drawUnitPanel() {
    const auto* latest = net_.latest();
    const auto* own = focusedOwn();
    const auto* entity = selected_.empty() ? nullptr : findEntity(selected_.front());
    if (!own || !entity)
        return;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 10.0f * s;
    const float width = 300.0f * s;
    const auto& def = data_->unit(entity->def);
    const auto rank = static_cast<Rank>(std::min<int>(own->rank, 3));

    ImGui::SetNextWindowPos({pad, display.y - pad}, ImGuiCond_Always, {0.0f, 1.0f});
    ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {width, display.y});
    ImGui::Begin("##unit", nullptr, kHudFlags);
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(def.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(toVec(kRankColors[static_cast<int>(rank)]), ICON_FA_STAR " %s", kRankNames[static_cast<int>(rank)]);
    if (rank == Rank::Hero) {
        ImGui::SameLine();
        ImGui::TextDisabled(tr("lvl %d"), own->level);
    }
    const float w = ImGui::GetContentRegionAvail().x;
    if (selected_.size() > 1)
        drawSelectionTiles(w, 6, true);
    char label[64];
    const float hpFrac = static_cast<float>(entity->hp) / static_cast<float>(std::max<std::uint16_t>(entity->maxHp, 1));
    std::snprintf(label, sizeof(label), ICON_FA_HEART " %u / %u", entity->hp, entity->maxHp);
    bar(hpFrac, {w, 18.0f * s}, hpColor(hpFrac), label);

    float goal = kEliteXp;
    const char* next = "Elite";
    if (rank == Rank::Elite) {
        goal = kHeroXp;
        next = "Hero";
    } else if (rank == Rank::Hero) {
        goal = xpForNextLevel(own->level);
        next = "next level";
    }
    std::snprintf(label, sizeof(label), "XP %u / %.0f  (%s)", own->xp, goal, next);
    bar(static_cast<float>(own->xp) / std::max(goal, 1.0f), {w, 12.0f * s}, IM_COL32(150, 110, 255, 230), label);

    const float energy = static_cast<float>(latest->view.energy);
    if (rank == Rank::Recruit || rank == Rank::Veteran) {
        ImGui::BeginDisabled(static_cast<float>(own->xp) < kEliteXp || energy < kEliteCost);
        std::snprintf(label, sizeof(label), tr(ICON_FA_ARROW_UP "  Promote to Elite   " ICON_FA_BOLT "%.0f"), kEliteCost);
        if (ImGui::Button(label, {w, 0.0f}))
            pressKey(SDLK_P, false);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(tr("Needs %.0f XP and %.0f energy. Elites get item slots and more power. [P]"), kEliteXp, kEliteCost);
    } else if (rank == Rank::Elite && def.skills.empty()) {
        ImGui::TextDisabled(tr("This unit cannot become a hero"));
    } else if (rank == Rank::Elite) {
        ImGui::BeginDisabled(static_cast<float>(own->xp) < kHeroXp || latest->view.shards < kHeroShards);
        std::snprintf(label, sizeof(label), tr(ICON_FA_TROPHY "  Raise a Hero   " ICON_FA_DIAMOND "%d"), kHeroShards);
        if (accentButton(label, {w, 0.0f}, IM_COL32(255, 190, 60, 255)))
            pressKey(SDLK_P, false);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(tr("Needs %.0f XP and %d shards, max %d heroes. Heroes learn skills. [P]"), kHeroXp, kHeroShards, kMaxHeroes);
    }

    if (rank == Rank::Hero) {
        const float box = (w - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
        for (std::size_t i = 0; i < 4; ++i) {
            if (i > 0)
                ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            if (i >= own->skills.size()) {
                ImGui::BeginDisabled();
                ImGui::Button(kSkillKeys[i], {box, box * 0.8f});
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(tr("Empty slot - learn a skill"));
                ImGui::PopID();
                continue;
            }
            const auto& slot = own->skills[i];
            const auto& skill = data_->skill(slot.skill);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool armed = armedSkill_ == static_cast<int>(i);
            const std::string name = skill.name;
            if (armed)
                ImGui::PushStyleColor(ImGuiCol_Button, toVec(IM_COL32(170, 120, 20, 255)));
            ImGui::BeginDisabled(skill.passive);
            if (ImGui::Button(("##s" + std::to_string(i)).c_str(), {box, box * 0.8f}) && !skill.passive)
                pressKey(i == 0 ? SDLK_Q : i == 1 ? SDLK_W : i == 2 ? SDLK_E : SDLK_R, false);
            ImGui::EndDisabled();
            if (armed)
                ImGui::PopStyleColor();
            skillTooltip(skill, slot.level);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 q{p.x + box, p.y + box * 0.8f};
            dl->AddText({p.x + 5.0f * s, p.y + 3.0f * s}, kEnergy, kSkillKeys[i]);
            fitText(dl, name, {p.x + 2.0f, p.y + 14.0f * s}, {q.x - 2.0f, q.y - 2.0f}, skill.passive ? kDim : IM_COL32(235, 240, 250, 255), 0.85f);
            char lvl[8];
            std::snprintf(lvl, sizeof(lvl), "%d", slot.level);
            dl->AddText({q.x - ImGui::CalcTextSize(lvl).x - 5.0f * s, p.y + 3.0f * s}, kDim, lvl);
            if (slot.cooldown > 0) {
                const float frac = std::clamp(static_cast<float>(slot.cooldown) / 10.0f / std::max(skill.cooldown, 0.1f), 0.0f, 1.0f);
                dl->AddRectFilled({p.x, q.y - (q.y - p.y) * frac}, q, IM_COL32(0, 0, 0, 150), 5.0f * s);
                char cd[8];
                std::snprintf(cd, sizeof(cd), "%d", (slot.cooldown + 9) / 10);
                const ImVec2 cs = ImGui::CalcTextSize(cd);
                dl->AddText({p.x + (box - cs.x) * 0.5f, p.y + (box * 0.8f - cs.y) * 0.5f}, IM_COL32(255, 255, 255, 255), cd);
            }
            ImGui::PopID();
        }
        if (own->skillPoints > 0) {
            std::snprintf(label, sizeof(label), tr(ICON_FA_BOOK "  Skill points: %d - learn"), own->skillPoints);
            if (accentButton(label, {w, 0.0f}, kGood))
                panel_ = Panel::Learn;
        }
    }

    const int slots = itemSlots(rank);
    if (slots > 0) {
        const float box = std::min(44.0f * s, (w - ImGui::GetStyle().ItemSpacing.x * static_cast<float>(slots - 1)) / static_cast<float>(slots));
        for (int i = 0; i < slots; ++i) {
            if (i > 0)
                ImGui::SameLine();
            ImGui::PushID(100 + i);
            if (i < static_cast<int>(own->items.size())) {
                const auto& item = data_->item(own->items[static_cast<std::size_t>(i)]);
                const ImVec4 c = toVec(kRarityColors[static_cast<int>(item.rarity)]);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{c.x * 0.3f, c.y * 0.3f, c.z * 0.3f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                if (ImGui::Button(item.name.substr(0, 2).c_str(), {box, box}))
                    net_.sendCommand(UnequipCommand{0, static_cast<entt::entity>(selected_.front()), static_cast<std::uint8_t>(i)});
                ImGui::PopStyleColor(2);
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextColored(c, "%s", item.name.c_str());
                    ImGui::TextUnformatted(modsText(item.mods).c_str());
                    ImGui::TextDisabled(tr("click to move back to the stash"));
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button(ICON_FA_CUBE, {box, box});
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(tr("Empty item slot - equip from the stash"));
            }
            ImGui::PopID();
        }
    }
    trackWindow();
    ImGui::End();
}

void App::drawSidePanel() {
    if (panel_ == Panel::None)
        return;
    const auto* latest = net_.latest();
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 10.0f * s;
    const float width = std::min(380.0f * s, display.x * 0.45f);
    ImGui::SetNextWindowPos({display.x - pad, topBarHeight_ + pad}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowSize({width, std::max(160.0f * s, display.y - topBarHeight_ - hudBottom_ - pad * 3.0f)}, ImGuiCond_Always);
    const char* title = panel_ == Panel::Shop ? ICON_FA_SHOPPING_CART "  Shop###side" : panel_ == Panel::Stash ? ICON_FA_ARCHIVE "  Stash###side"
                                                                                                                : ICON_FA_BOOK "  Hero skills###side";
    bool open = true;
    ImGui::Begin(title, &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (!open)
        panel_ = Panel::None;

    const auto* own = focusedOwn();
    const auto focused = selected_.empty() ? entt::null : static_cast<entt::entity>(selected_.front());
    const float energy = static_cast<float>(latest->view.energy);
    const float buttonW = 86.0f * s;
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    auto itemHeader = [&](const ItemDef& item) {
        ImGui::TextColored(toVec(kRarityColors[static_cast<int>(item.rarity)]), "%s", item.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kRarityNames[static_cast<int>(item.rarity)]);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - buttonW);
        ImGui::TextUnformatted(modsText(item.mods).c_str());
        ImGui::PopTextWrapPos();
    };
    auto rightButton = [&](const char* label, bool enabled) {
        ImGui::SameLine(rightEdge - buttonW);
        ImGui::BeginDisabled(!enabled);
        const bool pressed = accentButton(label, {buttonW, 0.0f});
        ImGui::EndDisabled();
        return pressed;
    };

    if (panel_ == Panel::Shop) {
        ImGui::TextDisabled(tr("Bought items go to the stash (%d/%d)"), static_cast<int>(latest->view.stash.size()), static_cast<int>(kStashSize));
        ImGui::Separator();
        const auto& shop = data_->shop();
        for (std::size_t i = 0; i < shop.size(); ++i) {
            const auto& item = data_->item(shop[i]);
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            itemHeader(item);
            ImGui::EndGroup();
            std::string price;
            if (item.cost > 0.0f)
                price += ICON_FA_BOLT + std::to_string(static_cast<int>(item.cost)) + " ";
            if (item.shards > 0)
                price += ICON_FA_DIAMOND + std::to_string(item.shards);
            const bool afford = energy >= item.cost && static_cast<int>(latest->view.shards) >= item.shards &&
                                latest->view.stash.size() < kStashSize;
            if (rightButton(price.c_str(), afford))
                net_.sendCommand(BuyCommand{0, shop[i]});
            ImGui::Separator();
            ImGui::PopID();
        }
    } else if (panel_ == Panel::Stash) {
        const bool canEquip = own && static_cast<int>(own->items.size()) < itemSlots(static_cast<Rank>(std::min<int>(own->rank, 3)));
        if (!own)
            ImGui::TextDisabled(tr("Select one of your units to equip items"));
        else if (itemSlots(static_cast<Rank>(std::min<int>(own->rank, 3))) == 0)
            ImGui::TextDisabled(tr("Only Elites and Heroes can carry items"));
        ImGui::Separator();
        for (std::size_t i = 0; i < latest->view.stash.size(); ++i) {
            const auto& item = data_->item(latest->view.stash[i]);
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            itemHeader(item);
            ImGui::EndGroup();
            if (rightButton("Equip", canEquip))
                net_.sendCommand(EquipCommand{0, focused, static_cast<std::uint8_t>(i)});
            ImGui::Separator();
            ImGui::PopID();
        }
        if (latest->view.stash.empty())
            ImGui::TextDisabled(tr("Empty - buy items in the shop or defeat bosses"));
        if (own && !own->items.empty()) {
            ImGui::SeparatorText(tr("Equipped"));
            for (std::size_t i = 0; i < own->items.size(); ++i) {
                ImGui::PushID(200 + static_cast<int>(i));
                ImGui::BeginGroup();
                itemHeader(data_->item(own->items[i]));
                ImGui::EndGroup();
                if (rightButton("Unequip", true))
                    net_.sendCommand(UnequipCommand{0, focused, static_cast<std::uint8_t>(i)});
                ImGui::PopID();
            }
        }
    } else if (panel_ == Panel::Learn) {
        const auto* entity = selected_.empty() ? nullptr : findEntity(selected_.front());
        if (!own || !entity || own->rank != static_cast<std::uint8_t>(Rank::Hero)) {
            ImGui::TextWrapped(tr("Select a hero. Promote an Elite with shards to raise one."));
        } else {
            ImGui::Text(tr("Skill points: %d"), own->skillPoints);
            ImGui::TextDisabled(tr("Up to %d skills, each up to level %d"), static_cast<int>(kMaxSkills), static_cast<int>(kMaxSkillLevel));
            ImGui::Separator();
            const auto& pool = data_->unit(entity->def).skills;
            for (std::size_t i = 0; i < pool.size(); ++i) {
                const auto& skill = data_->skill(pool[i]);
                int level = 0;
                for (const auto& sl : own->skills)
                    if (sl.skill == pool[i])
                        level = sl.level;
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginGroup();
                ImGui::TextColored(toVec(level > 0 ? kGood : kAccent), "%s", skill.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(tr("%s lvl %d/%d"), skill.passive ? "passive" : "active", level, skill.ultimate ? 1 : static_cast<int>(kMaxSkillLevel));
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - buttonW);
                for (const auto& fx : skill.effects)
                    ImGui::TextUnformatted(effectText(fx, std::max(level, 1)).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                const int maxLevel = skill.ultimate ? 1 : kMaxSkillLevel;
                const bool can = own->skillPoints > 0 && level < maxLevel && own->level >= skill.minLevel &&
                                 (level > 0 || own->skills.size() < kMaxSkills);
                if (skill.ultimate)
                    ImGui::TextColored(toVec(own->level >= skill.minLevel ? kEnergy : kDim), tr(ICON_FA_STAR " ultimate - hero level %d"), skill.minLevel);
                if (rightButton(level > 0 ? "Upgrade" : "Learn", can))
                    net_.sendCommand(LearnCommand{0, focused, pool[i]});
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }
    trackWindow();
    ImGui::End();
}

void App::drawStats() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto stats = net_.stats();
    const bgfx::Stats* st = bgfx::getStats();
    const double cpu = st->cpuTimerFreq ? static_cast<double>(st->cpuTimeEnd - st->cpuTimeBegin) * 1000.0 / static_cast<double>(st->cpuTimerFreq) : 0.0;
    const double gpu = st->gpuTimerFreq ? static_cast<double>(st->gpuTimeEnd - st->gpuTimeBegin) * 1000.0 / static_cast<double>(st->gpuTimerFreq) : 0.0;
    if (host_) {
        const auto* world = host_->world();
        const auto& t = world ? world->systemTimes() : SystemTimes{};
        double total = 0.0;
        for (const double v : t.seconds)
            total += v;
        if (t.steps > simSteps_) {
            const double ms = (total - simTotal_) / static_cast<double>(t.steps - simSteps_) * 1000.0;
            simMs_ = simSteps_ == 0 ? ms : simMs_ * 0.9 + ms * 0.1;
            simTotal_ = total;
            simSteps_ = t.steps;
        }
    }
    ImGui::SetNextWindowPos({display.x - 10.0f * s, topBarHeight_ + 10.0f * s}, ImGuiCond_Always, {1.0f, 0.0f});
    if (panel_ != Panel::None)
        ImGui::SetNextWindowPos({10.0f * s, topBarHeight_ + 10.0f * s}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##stats", nullptr, kHudFlags | ImGuiWindowFlags_NoInputs);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    ImGui::Text(tr("%.0f fps  %.1f ms  %s"), fps_, frameMs_, renderer_.name());
    ImGui::Text(tr("render cpu %.1f gpu %.1f ms  draws %u"), cpu, gpu, st->numDraw);
    if (host_)
        ImGui::Text(tr("sim %.2f ms/tick  entities %d"), simMs_, static_cast<int>(entities_.size()));
    ImGui::Text(tr("ui %.0f%%  layout %s"), uiScale() * 100.0f, touchLayout() ? "touch" : "desktop");
    ImGui::Text(tr("rtt %.0f ms  loss %.1f%%  %s"), stats.rtt, stats.packetLoss, net_.path().c_str());
    ImGui::Text(tr("in %.1f out %.1f kbps  snap %d B"), stats.receivedKbps, stats.sentKbps, static_cast<int>(net_.lastSnapshotBytes()));
    ImGui::Text(tr("interp %.0f ms  jitter %.1f ms  predicted %d"), net_.interpDelay() * 1000.0, net_.jitter() * 1000.0, static_cast<int>(prediction_.active()));
    ImGui::PopFont();
    ImGui::End();
}

void App::drawPauseMenu() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 110));
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({420.0f * s, 0.0f}, {420.0f * s, display.y});
    ImGui::Begin("##pause", nullptr, kHudFlags);
    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.5f);
    centeredText(tr("MENU"), kAccent);
    ImGui::PopFont();
    ImGui::TextDisabled(tr("The match keeps running while this menu is open"));
    if (host_ && lobby_.state() == net::MasterClient::State::Hosting) {
        ImGui::Text(tr("Room code:"));
        ImGui::SameLine();
        ImGui::TextColored(toVec(kAccent), "%s", lobby_.code().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Copy")))
            SDL_SetClipboardText(lobby_.code().c_str());
    } else if (host_ && lobby_.state() == net::MasterClient::State::Failed) {
        ImGui::TextColored(toVec(kBad), tr("Online unavailable: %s"), tr(lobby_.error()));
    }
    ImGui::Spacing();
    if (accentButton(tr(ICON_FA_PLAY "  Resume"), {full, 44.0f * s}))
        pauseOpen_ = false;
    if (ImGui::Button(tr(ICON_FA_BOOK "  Guide  [F1]"), {full, 36.0f * s})) {
        pauseOpen_ = false;
        guideOpen_ = true;
    }
    if (ImGui::Button(tr(ICON_FA_BUG "  Debug & cheats  [F9]"), {full, 32.0f * s})) {
        pauseOpen_ = false;
        debugOpen_ = true;
    }
    ImGui::Spacing();
    drawSettings();
    ImGui::Spacing();
    if (net_.lobby().leader == net_.player()) {
        if (saveName_[0] == 0) {
            const std::time_t t = std::time(nullptr);
            std::strftime(saveName_, sizeof(saveName_), "save_%Y%m%d_%H%M", std::localtime(&t));
        }
        ImGui::SetNextItemWidth(full - 130.0f * s);
        ImGui::InputText("##savename", saveName_, sizeof(saveName_), ImGuiInputTextFlags_CharsNoBlank);
        ImGui::SameLine();
        if (ImGui::Button(tr(ICON_FA_FLOPPY_O "  Save"), {-1.0f, 0.0f})) {
            net_.sendSaveGame(saveName_);
            saveName_[0] = 0;
        }
        ImGui::TextDisabled(tr("Saves are written on the host (%s)"), host_ ? "this PC" : "server");
    }
    if (net_.lobby().leader == net_.player() && ImGui::Button(tr(ICON_FA_USERS "  Return to lobby"), {full, 40.0f * s})) {
        pauseOpen_ = false;
        net_.sendReturnToLobby();
    }
    if (ImGui::Button(tr(ICON_FA_SIGN_OUT "  Leave match"), {full, 40.0f * s})) {
        pauseOpen_ = false;
        leaveGame("");
    }
    trackWindow();
    ImGui::End();
}

void App::drawEndScreen() {
    const auto& game = net_.latest()->game;
    const bool victory = wonMatch();
    const float s = uiScale();
    const bool campaign = !campaignMission_.empty();
    if (campaign)
        recordMission(victory);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 90));
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.45f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({420.0f * s, 0.0f}, {420.0f * s, display.y});
    ImGui::Begin("##end", nullptr, kHudFlags);
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 2.6f);
    centeredText(campaign ? (victory ? "MISSION COMPLETE" : "MISSION FAILED") : victory ? "VICTORY" : "DEFEAT", victory ? kGood : kBad);
    ImGui::PopFont();
    if (campaign) {
        bool sortie = false;
        std::string debrief;
        for (const auto& m : campaign_)
            if (m.id == campaignMission_) {
                sortie = m.sortie;
                debrief = m.debrief;
            }
        if (victory && !debrief.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(toVec(kDim), "%s", tr(debrief));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
        centeredText(victory ? (sortie ? "Survivors return to the base with their experience" : "The base is saved for the next missions")
                             : (sortie ? "The strike group is lost - the base is unchanged" : "The base has fallen"),
                     kText);
        ImGui::Spacing();
        const float w = ImGui::GetContentRegionAvail().x;
        const std::string mission = campaignMission_;
        std::size_t index = 0;
        for (std::size_t i = 0; i < campaign_.size(); ++i)
            if (campaign_[i].id == mission)
                index = i;
        if (victory && accentButton(tr(ICON_FA_PLAY "  Continue campaign"), {w, 44.0f * s})) {
            storeCampaignBase();
            leaveGame("");
        } else if (!victory && accentButton(tr(ICON_FA_REFRESH "  Retry mission"), {w, 44.0f * s})) {
            leaveGame("");
            startMission(index, false);
        } else if (ImGui::Button(tr(ICON_FA_PLAY "  Keep playing"), {w, 38.0f * s})) {
            endDismissed_ = true;
        } else if (ImGui::Button(tr(ICON_FA_SIGN_OUT "  Back to campaign"), {w, 38.0f * s})) {
            leaveGame("");
        }
        trackWindow();
        ImGui::End();
        return;
    }
    centeredText(victory ? "The Void portal is sealed" : "The core has fallen", kText);
    char line[64];
    if (game.totalWaves == 0)
        std::snprintf(line, sizeof(line), tr("Survived %d waves   |   %s"), std::max(0, game.wave - 1),
                      difficultyName(static_cast<Difficulty>(std::min<int>(game.difficulty, 3))));
    else
        std::snprintf(line, sizeof(line), tr("Waves: %d / %d   |   %s"), game.wave, game.totalWaves,
                      difficultyName(static_cast<Difficulty>(std::min<int>(game.difficulty, 3))));
    centeredText(line, kDim);
    ImGui::Spacing();
    const float full = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(tr(ICON_FA_PLAY "  Keep playing"), {full, 40.0f * s}))
        endDismissed_ = true;
    ImGui::SetItemTooltip(tr("Stay on the map: units, energy, recruiting and building keep working"));
    if (net_.lobby().leader == net_.player()) {
        if (accentButton(tr(ICON_FA_USERS "  Return to lobby"), {full, 44.0f * s}))
            net_.sendReturnToLobby();
    } else {
        centeredText(tr("Waiting for the host..."), kDim);
    }
    if (ImGui::Button(tr(ICON_FA_SIGN_OUT "  Leave"), {full, 38.0f * s}))
        leaveGame("");
    trackWindow();
    ImGui::End();
}

}
