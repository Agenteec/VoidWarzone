#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include <SDL3/SDL.h>
#include <glm/common.hpp>

#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

void App::drawDesktopHud() {
    const auto* latest = net_.latest();
    const auto& game = latest->game;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;

    ImGui::SetNextWindowPos({pad, pad});
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f * s, 6.0f * s});
    ImGui::Begin("##wave", nullptr, kHudFlags);
    ImGui::PopStyleVar();
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
    if (game.totalWaves == 0 || game.wave < game.totalWaves) {
        const auto& defense = map_->defense();
        const float interval = game.wave == 0 ? defense.firstWaveDelay : defense.waveInterval;
        const float left = static_cast<float>(game.nextWaveIn) / 10.0f;
        char label[16];
        std::snprintf(label, sizeof(label), "%d:%02d", static_cast<int>(left) / 60, static_cast<int>(left) % 60);
        bar(1.0f - left / std::max(interval, 1.0f), {110.0f * s, 8.0f * s}, IM_COL32(255, 170, 60, 220), nullptr);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f * s);
        ImGui::TextDisabled(tr("next %s"), label);
    } else {
        const int anchors = anchorsAlive();
        if (anchors > 0)
            ImGui::TextColored({0.8f, 0.5f, 1.0f, 1.0f}, tr("destroy the pylons (%d)"), anchors);
        else
            ImGui::TextDisabled(tr("destroy the portal"));
    }
    }
    trackWindow();
    ImGui::End();

    drawResourceBar();
    drawBossBar();

    const float panelH = 170.0f * s;
    hudBottom_ = panelH + pad;
    drawMinimap(panelH - 16.0f * s);
    drawDock();
    drawGroupBar();
    drawInspect();
    drawSelection();
    drawSidePanel();
    drawChat();
}

void App::drawResourceBar() {
    const auto* latest = net_.latest();
    const auto& game = latest->game;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;
    const auto& points = map_->resourcePoints();
    float income = map_->defense().baseIncome;
    for (std::size_t i = 0; i < points.size() && i < game.points.size(); ++i)
        if (game.points[i].owner == myTeam())
            income += points[i].income;
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit && (game.mode == static_cast<std::uint8_t>(GameMode::Skirmish) ? isOwn(e) : e.team == 0) && e.hp > 0)
            income += data_->unit(e.def).income;

    ImGui::SetNextWindowPos({display.x - pad, pad}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f * s, 6.0f * s});
    ImGui::Begin("##resources", nullptr, kHudFlags);
    ImGui::PopStyleVar();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ui_.bold(), 0.0f);
    ImGui::TextColored(toVec(kEnergy), ICON_FA_BOLT " %u", latest->view.energy);
    ImGui::PopFont();
    ImGui::SetItemTooltip(tr("Energy - recruit units, promote, buy items"));
    ImGui::SameLine();
    ImGui::TextDisabled("+%.1f/s", income);
    ImGui::SameLine(0.0f, 16.0f * s);
    ImGui::PushFont(ui_.bold(), 0.0f);
    ImGui::TextColored(toVec(kShard), ICON_FA_DIAMOND " %u", latest->view.shards);
    ImGui::PopFont();
    ImGui::SetItemTooltip(tr("Shards - drop from bosses, raise heroes and buy rare items"));
    if (host_ && lobby_.state() == net::MasterClient::State::Hosting) {
        ImGui::SameLine(0.0f, 16.0f * s);
        if (ImGui::SmallButton(lobby_.code().c_str()))
            SDL_SetClipboardText(lobby_.code().c_str());
        ImGui::SetItemTooltip(tr("Room code - click to copy"));
    }
    ImGui::SameLine(0.0f, 16.0f * s);
    if (ImGui::Button(ICON_FA_BARS))
        pauseOpen_ = !pauseOpen_;
    ImGui::SetItemTooltip(tr("Menu [F10]"));
    trackWindow();
    topBarHeight_ = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    ImGui::End();
}

void App::drawMinimap(float size) {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;
    const float mapW = static_cast<float>(map_->width());
    const float mapH = static_cast<float>(map_->height());
    const float aspect = mapW / mapH;
    const ImVec2 imageSize = aspect >= 1.0f ? ImVec2{size * std::min(aspect, 2.0f), size * std::min(aspect, 2.0f) / aspect} : ImVec2{size * aspect, size};
    const bool touch = touchLayout();

    if (touch)
        ImGui::SetNextWindowPos({pad, topBarHeight_ + pad});
    else
        ImGui::SetNextWindowPos({pad, display.y - pad}, ImGuiCond_Always, {0.0f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f * s, 6.0f * s});
    ImGui::Begin("##minimap", nullptr, kHudFlags);
    ImGui::PopStyleVar();
    ImGui::InvisibleButton("##mm", imageSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    auto toMap = [&](ImVec2 p) { return glm::vec2{(p.x - a.x) / imageSize.x * mapW, (p.y - a.y) / imageSize.y * mapH}; };
    auto toMini = [&](glm::vec2 p) { return ImVec2{a.x + p.x / mapW * imageSize.x, a.y + p.y / mapH * imageSize.y}; };
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        camera_.target = glm::clamp(toMap(ImGui::GetIO().MousePos), glm::vec2(0.0f), glm::vec2(mapW, mapH));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
        issueCommandAt(toMap(ImGui::GetIO().MousePos), orderArm_ == Arm::Attack, orderArm_ == Arm::Patrol);
        orderArm_ = Arm::None;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bgfx::TextureHandle tex = renderer_.textureHandle(minimap_);
    if (bgfx::isValid(tex))
        dl->AddImage(static_cast<ImTextureID>(tex.idx) + 1, a, b);
    if (const bgfx::TextureHandle fow = renderer_.textureHandle(minimapFow_); fowActive() && minimapFowSize_.x > 0 && bgfx::isValid(fow))
        dl->AddImage(static_cast<ImTextureID>(fow.idx) + 1, a, b);
    dl->PushClipRect(a, b, true);
    const auto* latest = net_.latest();
    const auto& defs = map_->resourcePoints();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const auto owner = latest && i < latest->game.points.size() ? latest->game.points[i].owner : 0xff;
        const ImU32 c = owner == 0xff ? IM_COL32(170, 170, 180, 255) : owner == myTeam() ? kAccent : owner == 1 ? kVoid
                                                                                          : ImGui::ColorConvertFloat4ToU32(ImVec4{teamColor(owner).r, teamColor(owner).g, teamColor(owner).b, 1.0f});
        const ImVec2 p = toMini(defs[i].pos);
        dl->AddCircleFilled(p, 4.0f * s, scaleColor(c, 0.5f, 0.9f));
        dl->AddCircle(p, 4.0f * s, c, 0, 1.5f);
    }
    const float unitR = std::max(1.5f, 1.6f * s);
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || hidden(e))
            continue;
        const bool structure = data_->unit(e.def).structure;
        ImU32 c = e.team == 1 ? kVoid : IM_COL32(70, 160, 255, 255);
        if (e.owner != net::kNoOwner) {
            const glm::vec4 pc = playerColor(e.owner);
            c = IM_COL32(static_cast<int>(pc.r * 255.0f), static_cast<int>(pc.g * 255.0f), static_cast<int>(pc.b * 255.0f), 255);
        }
        const ImVec2 p = toMini(e.pos);
        const float r = structure ? unitR * 1.8f : unitR;
        dl->AddRectFilled({p.x - r, p.y - r}, {p.x + r, p.y + r}, c);
    }
    const float w = static_cast<float>(renderer_.width());
    const float h = static_cast<float>(renderer_.height());
    const auto c0 = camera_.screenToGround(0.0f, 0.0f);
    const auto c1 = camera_.screenToGround(w, 0.0f);
    const auto c2 = camera_.screenToGround(w, h);
    const auto c3 = camera_.screenToGround(0.0f, h);
    if (c0 && c1 && c2 && c3)
        dl->AddQuad(toMini(*c0), toMini(*c1), toMini(*c2), toMini(*c3), IM_COL32(255, 255, 255, 200), 1.5f);
    dl->PopClipRect();
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 40));
    if (ImGui::IsItemHovered() && !touch)
        ImGui::SetTooltip(tr("Left: move camera  |  Right: send selected units"));
    trackWindow();
    minimapRight_ = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
    ImGui::End();
}

void App::drawSelectionTiles(float width, int perRow, bool horizontal) {
    const float s = uiScale();
    std::map<UnitDefId, int> counts;
    for (const auto id : selected_)
        if (const auto* e = findEntity(id))
            ++counts[e->def];
    ImGui::Text(tr("%d selected"), static_cast<int>(selected_.size()));
    for (const auto& [def, n] : counts) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s x%d", data_->unit(def).name.c_str(), n);
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float tile = std::min(48.0f * s, (width - spacing * static_cast<float>(perRow - 1) - (horizontal ? 0.0f : 14.0f * s)) /
                                               static_cast<float>(perRow));
    const float height = horizontal ? tile + ImGui::GetStyle().ScrollbarSize + 4.0f * s : ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("tiles", {width, height}, ImGuiChildFlags_None, horizontal ? ImGuiWindowFlags_HorizontalScrollbar : 0);
    int index = 0;
    std::uint32_t pick = 0;
    std::uint32_t drop = 0;
    UnitDefId type = kInvalidUnitDef;
    for (const auto id : selected_) {
        const auto* e = findEntity(id);
        if (!e)
            continue;
        if (horizontal ? index > 0 : index % perRow != 0)
            ImGui::SameLine();
        ++index;
        ImGui::PushID(static_cast<int>(id));
        const auto& def = data_->unit(e->def);
        const int rank = e->rank > 0 ? e->rank - 1 : 0;
        ImGui::PushStyleColor(ImGuiCol_Text, toVec(kRankColors[std::min(rank, 3)]));
        const bool clicked = ImGui::Button("##tile", {tile, tile});
        {
            std::string shortName = def.name.substr(0, def.name.find(" ("));
            fitText(ImGui::GetWindowDrawList(), shortName, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), kRankColors[std::min(rank, 3)], 0.8f);
        }
        if (clicked) {
            const SDL_Keymod mod = SDL_GetModState();
            if (mod & SDL_KMOD_SHIFT)
                drop = id;
            else if (mod & SDL_KMOD_CTRL)
                type = e->def;
            else
                pick = id;
        }
        ImGui::PopStyleColor();
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const float frac = static_cast<float>(e->hp) / static_cast<float>(std::max<std::uint16_t>(e->maxHp, 1));
        ImGui::GetWindowDrawList()->AddRectFilled({a.x + 3.0f, b.y - 5.0f}, {a.x + 3.0f + (b.x - a.x - 6.0f) * frac, b.y - 2.0f}, hpColor(frac));
        if (unseen_.contains(id))
            redDot(ImGui::GetWindowDrawList(), {b.x - 4.0f * s, a.y + 4.0f * s}, 4.0f * s);
        ImGui::SetItemTooltip(tr("%s  %u/%u\nclick: select only, shift: remove, ctrl: all of this type"), def.name.c_str(), e->hp, e->maxHp);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (pick != 0)
        selected_ = {pick};
    else if (drop != 0)
        std::erase(selected_, drop);
    else if (type != kInvalidUnitDef)
        std::erase_if(selected_, [&](std::uint32_t id) {
            const auto* e = findEntity(id);
            return !e || e->def != type;
        });
}

void App::drawSelection() {
    if (selected_.empty())
        return;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;
    const float left = minimapRight_ + pad;
    const float right = commandCardLeft_ - pad;
    const float width = std::min(right - left, 460.0f * s);
    if (width < 120.0f * s)
        return;
    const float panelH = hudBottom_ - pad;
    ImGui::SetNextWindowPos({left + (right - left - width) * 0.5f, display.y - pad}, ImGuiCond_Always, {0.0f, 1.0f});
    ImGui::SetNextWindowSize({width, panelH});
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##selection", nullptr, kHudFlags & ~ImGuiWindowFlags_AlwaysAutoResize);
    const float w = ImGui::GetContentRegionAvail().x;
    const auto* own = focusedOwn();
    const auto* entity = findEntity(selected_.front());

    if (selected_.size() == 1 && own && entity) {
        const auto& def = data_->unit(entity->def);
        const auto rank = static_cast<Rank>(std::min<int>(own->rank, 3));
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.1f);
        ImGui::TextUnformatted(def.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(toVec(kRankColors[static_cast<int>(rank)]), "%s", kRankNames[static_cast<int>(rank)]);
        if (rank == Rank::Hero) {
            ImGui::SameLine();
            ImGui::TextDisabled(tr("level %d"), own->level);
        }
        char label[48];
        const float hpFrac = static_cast<float>(entity->hp) / static_cast<float>(std::max<std::uint16_t>(entity->maxHp, 1));
        std::snprintf(label, sizeof(label), "%u / %u", entity->hp, entity->maxHp);
        bar(hpFrac, {w, 16.0f * s}, hpColor(hpFrac), label);
        const float goal = rank == Rank::Hero ? xpForNextLevel(own->level) : rank == Rank::Elite ? kHeroXp : kEliteXp;
        std::snprintf(label, sizeof(label), "XP %u / %.0f", own->xp, goal);
        bar(static_cast<float>(own->xp) / std::max(goal, 1.0f), {w, 11.0f * s}, IM_COL32(150, 110, 255, 230), label);
        ImGui::TextDisabled(tr("dmg %.0f  range %.1f  armor %.0f"), def.weapon ? def.weapon->damage : 0.0f, def.weapon ? def.weapon->range : 0.0f, def.armor);
        const int slots = itemSlots(rank);
        if (slots > 0) {
            const float box = 34.0f * s;
            for (int i = 0; i < slots; ++i) {
                if (i > 0)
                    ImGui::SameLine();
                ImGui::PushID(i);
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
                        ImGui::TextDisabled(tr("click to move it back to the stash"));
                        ImGui::EndTooltip();
                    }
                } else {
                    ImGui::BeginDisabled();
                    ImGui::Button("##empty", {box, box});
                    ImGui::EndDisabled();
                    ImGui::SetItemTooltip(tr("Empty item slot - equip from the stash [I]"));
                }
                ImGui::PopID();
            }
        }
        if (own->skillPoints > 0) {
            ImGui::SameLine();
            ImGui::TextColored(toVec(kGood), tr("%d skill point%s [L]"), own->skillPoints, own->skillPoints > 1 ? "s" : "");
        }
    } else {
        drawSelectionTiles(w, 9, false);
    }
    trackWindow();
    ImGui::End();
}

void App::drawChat() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 8.0f * s;
    std::vector<const ChatEntry*> recent;
    for (const auto& entry : chatLog_)
        if (chatOpen_ || now_ - entry.time < 12.0)
            recent.push_back(&entry);
    if (recent.empty() && !chatOpen_)
        return;
    if (recent.size() > 6)
        recent.erase(recent.begin(), recent.end() - 6);
    ImGui::SetNextWindowPos({pad, display.y - hudBottom_ - pad}, ImGuiCond_Always, {0.0f, 1.0f});
    ImGui::SetNextWindowBgAlpha(chatOpen_ ? 0.75f : 0.0f);
    ImGui::Begin("##chat", nullptr, kHudFlags | (chatOpen_ ? 0 : ImGuiWindowFlags_NoInputs));
    for (const auto* entry : recent) {
        ImGui::TextColored(toVec(kAccent), "%s:", entry->line.name.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(entry->line.text.c_str());
    }
    if (chatOpen_) {
        ImGui::SetNextItemWidth(360.0f * s);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        if (ImGui::InputTextWithHint("##say", tr("Enter to send, Esc to close"), chatBuf_.data(), chatBuf_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (chatBuf_[0] != '\0')
                net_.sendChat(chatBuf_.data());
            chatBuf_[0] = '\0';
            chatOpen_ = false;
        } else if (ImGui::IsItemDeactivated()) {
            chatOpen_ = false;
        }
        trackWindow();
    }
    ImGui::End();
}

}
