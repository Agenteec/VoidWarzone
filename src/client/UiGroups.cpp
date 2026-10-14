#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>

#include "core/World.h"
#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

constexpr SDL_Keycode kBuildKeys[] = {SDLK_D, SDLK_F, SDLK_G, SDLK_J, SDLK_K};

}

bool App::arenaMode() const {
    return net_.lobby().mode == static_cast<std::uint8_t>(GameMode::Arena);
}

int App::buildKeyIndex(SDL_Keycode key) const {
    for (int i = 0; i < 5; ++i)
        if (kBuildKeys[i] == key)
            return i;
    return -1;
}

void App::armBuild(std::size_t index) {
    const auto& list = data_->buildable();
    if (!arenaMode() || index >= list.size())
        return;
    const int def = static_cast<int>(list[index]);
    buildArmed_ = buildArmed_ == def ? -1 : def;
    orderArm_ = Arm::None;
    armedSkill_ = -1;
}

bool App::buildValid(UnitDefId id, glm::vec2 pos) const {
    if (!map_)
        return false;
    const auto& def = data_->unit(id);
    const float r = std::max(def.radius - 0.25f, 0.5f);
    const glm::ivec2 c = Map::cellOf(pos);
    const int span = static_cast<int>(std::ceil(r)) + 1;
    for (int y = c.y - span; y <= c.y + span; ++y)
        for (int x = c.x - span; x <= c.x + span; ++x)
            if ((glm::ivec2(x, y) == c || glm::distance(Map::center({x, y}), pos) < r) && map_->blocked(x, y))
                return false;
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit && e.hp > 0 && glm::distance(e.pos, pos) < def.radius + data_->unit(e.def).radius - 0.1f)
            return false;
    return true;
}

void App::placeBuild(glm::vec2 screen, bool keep) {
    if (buildArmed_ < 0)
        return;
    if (const auto ground = camera_.screenToGround(screen.x, screen.y)) {
        const glm::vec2 pos = World::snapBuild(*ground);
        const auto def = static_cast<UnitDefId>(buildArmed_);
        if (buildValid(def, pos)) {
            net_.sendCommand(BuildCommand{0, def, pos});
            addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.y}, {0.35f, 1.0f, 0.55f, 0.8f}, 0.4f, data_->unit(def).radius * 2.5f, 0.35f);
        }
    }
    if (!keep)
        buildArmed_ = -1;
}

void App::renderBuildGhost() {
    if (buildArmed_ < 0 || dragging_)
        return;
    const auto ground = camera_.screenToGround(mouse_.x, mouse_.y);
    if (!ground)
        return;
    const auto def = static_cast<UnitDefId>(buildArmed_);
    const glm::vec2 pos = World::snapBuild(*ground);
    const float d = data_->unit(def).radius * 2.0f;
    const float h = std::max(visuals_[def].height, 0.8f);
    const glm::vec4 color = buildValid(def, pos) ? glm::vec4{0.35f, 1.0f, 0.55f, 0.35f} : glm::vec4{1.0f, 0.25f, 0.25f, 0.35f};
    renderer_.draw(Mesh::Cube, {pos.x, h * 0.5f, pos.y}, {d, h, d}, 0.0f, color, Unlit | Alpha);
    if (const auto& w = data_->unit(def).weapon)
        renderer_.draw(Mesh::Cylinder, {pos.x, 0.03f, pos.y}, {w->range * 2.0f, 0.03f, w->range * 2.0f}, 0.0f, glm::vec4{1.0f, 1.0f, 1.0f, 0.08f},
                       Unlit | Alpha);
}

void App::controlGroup(int n, bool assign, bool add) {
    auto& group = groups_[static_cast<std::size_t>(n)];
    std::erase_if(group, [this](std::uint32_t id) {
        const auto* e = findEntity(id);
        return !e || !isOwn(*e) || e->hp <= 0;
    });
    if (assign) {
        pruneSelection();
        if (!add)
            group.clear();
        for (const auto id : selected_)
            if (std::find(group.begin(), group.end(), id) == group.end())
                group.push_back(id);
        lastGroup_ = -1;
        return;
    }
    if (group.empty())
        return;
    if (!add)
        selected_.clear();
    for (const auto id : group)
        if (std::find(selected_.begin(), selected_.end(), id) == selected_.end())
            selected_.push_back(id);
    if (lastGroup_ == n && now_ - lastGroupTime_ < 0.4) {
        glm::vec2 center{};
        for (const auto id : group)
            if (const auto* e = findEntity(id))
                center += e->pos;
        camera_.target = center / static_cast<float>(group.size());
    }
    lastGroup_ = n;
    lastGroupTime_ = now_;
}

void App::drawGroupBar() {
    const float s = uiScale();
    const bool touch = touchLayout();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    int last = 0;
    for (int i = 1; i <= 9; ++i)
        if (!groups_[static_cast<std::size_t>(i)].empty())
            last = i;
    const int shown = touch ? std::max(last, 5) : last;
    if (shown == 0)
        return;

    ImGui::SetNextWindowPos({display.x * 0.5f, display.y - hudBottom_ - 6.0f * s}, ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f * s, 4.0f * s});
    ImGui::Begin("##groups", nullptr, kHudFlags);
    ImGui::PopStyleVar();
    const ImVec2 size{(touch ? 54.0f : 40.0f) * s, (touch ? 40.0f : 28.0f) * s};
    for (int i = 1; i <= shown; ++i) {
        auto& group = groups_[static_cast<std::size_t>(i)];
        std::erase_if(group, [this](std::uint32_t id) {
            const auto* e = findEntity(id);
            return !e || e->hp <= 0;
        });
        if (i > 1)
            ImGui::SameLine();
        char label[24];
        if (group.empty())
            std::snprintf(label, sizeof(label), "%d##g%d", i, i);
        else
            std::snprintf(label, sizeof(label), "%d:%d##g%d", i, static_cast<int>(group.size()), i);
        ImGui::PushStyleColor(ImGuiCol_Button, group.empty() ? ImVec4{0.12f, 0.13f, 0.16f, 0.7f} : ImVec4{0.16f, 0.32f, 0.45f, 0.9f});
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor();
        const bool hold = ImGui::IsItemActive() && ImGui::GetIO().MouseDownDuration[0] > 0.5f;
        if (hold && !groupHeld_) {
            groupHeld_ = true;
            controlGroup(i, true, false);
        } else if (clicked && !groupHeld_) {
            if (group.empty())
                controlGroup(i, true, false);
            else
                controlGroup(i, false, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        }
        if (touch)
            ImGui::SetItemTooltip("Tap: select, hold: assign the selection");
        else
            ImGui::SetItemTooltip("%d select, Ctrl+%d assign, Shift+%d add", i, i, i);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        groupHeld_ = false;
    trackWindow();
    ImGui::End();
}

}

namespace vw::client {

void App::renderWaypoints() {
    for (auto it = waypoints_.begin(); it != waypoints_.end();) {
        const auto* e = findEntity(it->first);
        auto& list = it->second;
        if (e && !list.empty() && glm::distance(e->pos, list.front()) < data_->unit(e->def).radius + 1.0f)
            list.erase(list.begin());
        if (!e || list.empty())
            it = waypoints_.erase(it);
        else
            ++it;
    }
    const glm::vec4 color{0.35f, 1.0f, 0.55f, 0.55f};
    for (const auto id : selected_) {
        const auto it = waypoints_.find(id);
        const auto* e = findEntity(id);
        if (it == waypoints_.end() || !e || it->second.size() < 2)
            continue;
        glm::vec2 from = e->pos;
        for (const auto& p : it->second) {
            const float len = glm::distance(from, p);
            for (float t = 0.8f; t < len; t += 0.8f) {
                const glm::vec2 q = from + (p - from) * (t / len);
                renderer_.draw(Mesh::Sphere, {q.x, 0.12f, q.y}, glm::vec3(0.12f), 0.0f, color, Unlit | Alpha);
            }
            renderer_.draw(Mesh::Cylinder, {p.x, 0.04f, p.y}, {0.7f, 0.04f, 0.7f}, 0.0f, color, Unlit | Alpha);
            from = p;
        }
    }
}

}
