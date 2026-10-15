#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL3/SDL.h>
#include <bx/math.h>
#include <glm/geometric.hpp>

#include "core/World.h"
#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

constexpr SDL_Keycode kBuildKeys[] = {SDLK_D, SDLK_F, SDLK_G, SDLK_J, SDLK_K};

}

int App::buildKeyIndex(SDL_Keycode key) const {
    for (int i = 0; i < 5; ++i)
        if (kBuildKeys[i] == key)
            return i;
    return -1;
}

std::vector<entt::entity> App::selectedBuilders() const {
    std::vector<entt::entity> builders;
    for (const auto id : selected_)
        if (const auto* e = findEntity(id); e && isOwn(*e) && data_->unit(e->def).builder)
            builders.push_back(static_cast<entt::entity>(id));
    return builders;
}

std::vector<entt::entity> App::selectedTransports() const {
    std::vector<entt::entity> out;
    for (const auto id : selected_)
        if (const auto* e = findEntity(id); e && isOwn(*e) && data_->unit(e->def).transport > 0)
            out.push_back(static_cast<entt::entity>(id));
    return out;
}

float App::liftOf(const net::InterpolatedEntity& e) const {
    if (!data_->unit(e.def).air)
        return 0.0f;
    const auto it = airLift_.find(e.id);
    return it != airLift_.end() ? it->second : 3.0f;
}

void App::updateAirLift(float dt) {
    constexpr float kCruise = 3.0f;
    constexpr float kClearance = 1.4f;
    std::erase_if(airLift_, [&](const auto& kv) { return findEntity(kv.first) == nullptr; });
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || !data_->unit(e.def).air)
            continue;
        const float r = data_->unit(e.def).radius;
        float top = 0.0f;
        for (const glm::vec2 at : {e.pos, e.pos + e.vel * 0.8f}) {
            for (const glm::vec2 o : {glm::vec2{0.0f, 0.0f}, glm::vec2{r, 0.0f}, glm::vec2{-r, 0.0f}, glm::vec2{0.0f, r}, glm::vec2{0.0f, -r}})
                top = std::max(top, mapVisuals_.heightAt(at.x + o.x, at.y + o.y));
            for (const auto& s : entities_) {
                if (s.kind != net::EntityKind::Unit || !data_->unit(s.def).structure)
                    continue;
                if (glm::distance(s.pos, at) < r + data_->unit(s.def).radius + 0.3f)
                    top = std::max(top, visuals_[s.def].height);
            }
        }
        const float target = std::max(kCruise, top + kClearance);
        auto [it, fresh] = airLift_.try_emplace(e.id, target);
        if (!fresh) {
            const float rate = target > it->second ? 5.0f : 1.5f;
            it->second += (target - it->second) * (1.0f - std::exp(-dt * rate));
        }
    }
}

std::vector<glm::vec2> App::buildLine(glm::vec2 from, glm::vec2 to) const {
    std::vector<glm::vec2> out;
    if (buildArmed_ < 0)
        return out;
    const auto& def = data_->unit(static_cast<UnitDefId>(buildArmed_));
    const float step = def.wall ? 1.0f : std::ceil(def.radius * 2.0f + kStructureGap);
    const glm::vec2 a = snapBuild(from);
    glm::vec2 d = snapBuild(to) - a;
    if (std::abs(d.x) >= std::abs(d.y) * 2.0f)
        d.y = 0.0f;
    else if (std::abs(d.y) >= std::abs(d.x) * 2.0f)
        d.x = 0.0f;
    const float len = std::max(std::abs(d.x), std::abs(d.y));
    const int count = std::min(40, static_cast<int>(len / step) + 1);
    for (int i = 0; i < count; ++i) {
        const float t = count > 1 ? static_cast<float>(i) * step / len : 0.0f;
        out.push_back(snapBuild(a + d * std::min(t, 1.0f)));
    }
    return out;
}

void App::armBuild(std::size_t index) {
    const auto& list = data_->buildable();
    if (index >= list.size() || selectedBuilders().empty())
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
    std::vector<SiteBlocker> around;
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || e.hp == 0 || glm::distance(e.pos, pos) > def.radius + 12.0f + kEnemyBuildGap)
            continue;
        const auto& other = data_->unit(e.def);
        if (!other.structure && isOwn(e) && other.builder)
            continue;
        around.push_back({e.pos, other.radius, other.structure, other.wall, e.team});
    }
    std::vector<PendingBuild> phantoms;
    collectPhantoms(phantoms);
    for (const auto& ph : phantoms) {
        if (ph.def == id && glm::distance(ph.pos, pos) < 0.3f)
            continue;
        const auto& other = data_->unit(ph.def);
        around.push_back({ph.pos, other.radius, true, other.wall, 0});
    }
    const SiteStatus status = checkSite(*map_, def, pos, 0, around);
    return status == SiteStatus::Ok || status == SiteStatus::Occupied;
}

void App::placeBuild(glm::vec2 screen, bool keep) {
    if (buildArmed_ < 0)
        return;
    if (lineBuild_) {
        lineBuild_ = false;
        if (const auto ground = camera_.screenToGround(screen.x, screen.y)) {
            const auto def = static_cast<UnitDefId>(buildArmed_);
            for (const auto p : buildLine(lineStart_, *ground))
                if (buildValid(def, p)) {
                    net_.sendCommand(BuildCommand{selectedBuilders(), def, p, true});
                    pendingBuilds_.push_back({def, p, now_});
                }
        }
        if (!keep)
            buildArmed_ = -1;
        return;
    }
    if (const auto ground = camera_.screenToGround(screen.x, screen.y)) {
        const glm::vec2 pos = snapBuild(*ground);
        const auto def = static_cast<UnitDefId>(buildArmed_);
        if (buildValid(def, pos)) {
            const bool queue = !touchLayout() && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            net_.sendCommand(BuildCommand{selectedBuilders(), def, pos, queue});
            pendingBuilds_.push_back({def, pos, now_});
            addEffect(Mesh::Cylinder, {pos.x, 0.1f, pos.y}, {0.35f, 1.0f, 0.55f, 0.8f}, 0.4f, data_->unit(def).radius * 2.5f, 0.35f);
        }
    }
    if (!keep)
        buildArmed_ = -1;
}

void App::collectPhantoms(std::vector<PendingBuild>& out) const {
    std::erase_if(pendingBuilds_, [&](const PendingBuild& p) { return now_ - p.time > 1.0; });
    out = pendingBuilds_;
    const auto* latest = net_.latest();
    if (!latest)
        return;
    for (const auto& u : latest->view.units)
        for (const auto& o : u.orders)
            if (static_cast<OrderType>(o.type) == OrderType::Build && o.unit < data_->units().size()) {
                const glm::vec2 p{net::dequantizePos(o.x), net::dequantizePos(o.y)};
                const bool built = std::any_of(entities_.begin(), entities_.end(), [&](const auto& e) {
                    return e.kind == net::EntityKind::Unit && e.def == o.unit && glm::distance(e.pos, p) < 0.3f;
                });
                const bool dup = std::any_of(out.begin(), out.end(), [&](const auto& q) { return q.def == o.unit && glm::distance(q.pos, p) < 0.3f; });
                if (!built && !dup)
                    out.push_back({static_cast<UnitDefId>(o.unit), p, now_});
            }
}

void App::renderPhantoms() {
    std::vector<PendingBuild> phantoms;
    collectPhantoms(phantoms);
    for (const auto& ph : phantoms) {
        const float d = data_->unit(ph.def).radius * 2.0f;
        const float h = std::max(visuals_[ph.def].height, 0.8f) * 0.6f;
        renderer_.draw(Mesh::Cube, {ph.pos.x, h * 0.5f, ph.pos.y}, {d, h, d}, 0.0f, glm::vec4{0.85f, 0.55f, 1.0f, 0.22f}, Unlit | Alpha);
    }
}

void App::renderBuildGhost() {
    renderPhantoms();
    if (buildArmed_ < 0 || dragging_)
        return;
    const auto ground = camera_.screenToGround(mouse_.x, mouse_.y);
    if (!ground)
        return;
    const auto def = static_cast<UnitDefId>(buildArmed_);
    if (lineBuild_) {
        const float d = data_->unit(def).radius * 2.0f;
        const float h = std::max(visuals_[def].height, 0.8f);
        for (const auto p : buildLine(lineStart_, *ground)) {
            const glm::vec4 c = buildValid(def, p) ? glm::vec4{0.35f, 1.0f, 0.55f, 0.35f} : glm::vec4{1.0f, 0.25f, 0.25f, 0.35f};
            renderer_.draw(Mesh::Cube, {p.x, h * 0.5f, p.y}, {d, h, d}, 0.0f, c, Unlit | Alpha);
        }
        return;
    }
    const glm::vec2 pos = snapBuild(*ground);
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
        if (std::any_of(group.begin(), group.end(), [&](std::uint32_t id) { return unseen_.contains(id); }))
            redDot(ImGui::GetWindowDrawList(), {ImGui::GetItemRectMax().x - 4.0f * s, ImGui::GetItemRectMin().y + 4.0f * s}, 4.0f * s);
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

glm::vec4 App::orderColor(std::uint8_t type) {
    switch (static_cast<OrderType>(type)) {
    case OrderType::AttackMove:
    case OrderType::Attack:
        return {1.0f, 0.35f, 0.25f, 0.6f};
    case OrderType::Patrol:
        return {0.35f, 0.65f, 1.0f, 0.6f};
    case OrderType::Follow:
        return {1.0f, 0.85f, 0.25f, 0.6f};
    case OrderType::Build:
        return {0.85f, 0.55f, 1.0f, 0.6f};
    default:
        return {0.35f, 1.0f, 0.55f, 0.6f};
    }
}

void App::renderWaypoints() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    for (const auto id : selected_) {
        const auto* own = latest->view.unit(id);
        const auto* e = findEntity(id);
        if (!own || !e || own->orders.empty())
            continue;
        glm::vec2 from = e->pos;
        for (std::size_t i = 0; i < own->orders.size(); ++i) {
            const auto& o = own->orders[i];
            const glm::vec2 p{net::dequantizePos(o.x), net::dequantizePos(o.y)};
            const glm::vec4 color = orderColor(o.type);
            const float len = glm::distance(from, p);
            for (float t = 0.8f; t < len; t += 0.8f) {
                const glm::vec2 q = from + (p - from) * (t / len);
                renderer_.draw(Mesh::Sphere, {q.x, 0.12f, q.y}, glm::vec3(0.12f), 0.0f, color, Unlit | Alpha);
            }
            renderer_.draw(Mesh::Cylinder, {p.x, 0.04f, p.y}, {0.7f, 0.04f, 0.7f}, 0.0f, color, Unlit | Alpha);
            if (static_cast<OrderType>(o.type) == OrderType::Patrol && i + 1 < own->orders.size()) {
                const auto& back = own->orders[i + 1];
                const glm::vec2 b{net::dequantizePos(back.x), net::dequantizePos(back.y)};
                const float l2 = glm::distance(p, b);
                for (float t = 0.8f; t < l2; t += 1.6f) {
                    const glm::vec2 q = p + (b - p) * (t / l2);
                    renderer_.draw(Mesh::Sphere, {q.x, 0.12f, q.y}, glm::vec3(0.09f), 0.0f, color, Unlit | Alpha);
                }
                renderer_.draw(Mesh::Cylinder, {b.x, 0.04f, b.y}, {0.7f, 0.04f, 0.7f}, 0.0f, color, Unlit | Alpha);
                from = b;
                ++i;
                continue;
            }
            from = p;
        }
    }
}

}

namespace vw::client {

void App::drawInspect() {
    if (!selected_.empty())
        inspected_ = 0;
    if (inspected_ == 0)
        return;
    const auto* e = findEntity(inspected_);
    if (!e || e->hp == 0) {
        inspected_ = 0;
        return;
    }
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto& def = data_->unit(e->def);
    const float width = 300.0f * s;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y - hudBottom_ * 0.2f - 8.0f * s}, ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {width, display.y});
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##inspect", nullptr, kHudFlags);
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(def.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (e->owner != net::kNoOwner) {
        const glm::vec4 c = playerColor(e->owner);
        std::string owner = "player";
        for (const auto& p : net_.lobby().players)
            if (p.id == e->owner)
                owner = p.name;
        ImGui::TextColored({c.r, c.g, c.b, 1.0f}, "%s%s", owner.c_str(), e->owner == net_.player() ? " (you)" : "");
    } else {
        ImGui::TextColored(toVec(e->team == 1 ? kVoid : kAccent), "%s", e->team == 1 ? "the Void" : "allied");
    }
    if (e->rank > 1) {
        const int rank = std::min(e->rank - 1, 3);
        ImGui::SameLine();
        ImGui::TextColored(toVec(kRankColors[rank]), "%s", kRankNames[rank]);
    }
    const float w = ImGui::GetContentRegionAvail().x;
    char label[48];
    const float frac = static_cast<float>(e->hp) / static_cast<float>(std::max<std::uint16_t>(e->maxHp, 1));
    std::snprintf(label, sizeof(label), "%u / %u", e->hp, e->maxHp);
    bar(frac, {w, 16.0f * s}, hpColor(frac), label);
    if (def.weapon)
        ImGui::TextDisabled("%s damage %.0f%s  range %.1f  every %.1fs", damageTypeName(def.weapon->type), def.weapon->damage,
                                def.weapon->splash > 0.0f ? " (area)" : "", def.weapon->range, def.weapon->cooldown);
    ImGui::TextDisabled("%s armor %.0f", armorTypeName(def.armorType), def.armor);
    if (def.shield > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored({0.35f, 0.85f, 1.0f, 1.0f}, "shield %u / %.0f", e->shield, def.shield);
    }
    if (e->flags & net::kStateBuilding)
        ImGui::TextColored(toVec(kEnergy), "under construction - right click with an engineer to help");
    if (def.speed > 0.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("speed %.1f", def.speed);
    }
    if (def.income > 0.0f)
        ImGui::TextDisabled("+%.1f energy/s", def.income);
    trackWindow();
    ImGui::End();
}

}

namespace vw::client {

void App::renderModules(const net::InterpolatedEntity& e, const UnitVisual& vis, glm::vec3 base, float grow, const glm::vec4& color) {
    const float d = data_->unit(e.def).radius * 2.0f * grow;
    const float hullYaw = e.facing + vis.yaw;
    for (const auto m : e.modules) {
        if (m >= data_->modules().size())
            continue;
        const auto& def = data_->module(m);
        if (def.look == "plates") {
            const glm::vec2 side{-std::sin(e.facing), std::cos(e.facing)};
            const float h = vis.height * grow * 0.4f;
            for (const float s : {-1.0f, 1.0f}) {
                const glm::vec2 p = glm::vec2(base.x, base.z) + side * (s * d * 0.56f);
                renderer_.draw(Mesh::Cube, {p.x, base.y + h * 0.95f, p.y}, {d * 0.8f, h, 0.16f * grow}, e.facing,
                               glm::vec4(glm::vec3(0.42f, 0.44f, 0.4f) + glm::vec3(color) * 0.15f, 1.0f));
            }
        } else if (def.look == "bubble" && e.shield > 0) {
            renderer_.draw(Mesh::Sphere, {base.x, base.y + vis.height * grow * 0.45f, base.z}, glm::vec3(d * 1.35f), 0.0f,
                           glm::vec4{0.35f, 0.85f, 1.0f, 0.13f}, Unlit | Alpha);
        } else if (def.look == "booster") {
            const glm::vec2 back = glm::vec2(base.x, base.z) - glm::vec2(std::cos(e.facing), std::sin(e.facing)) * d * 0.5f;
            const float glow = 0.6f + 0.4f * std::sin(static_cast<float>(now_) * 18.0f + static_cast<float>(e.id));
            renderer_.draw(Mesh::Sphere, {back.x, base.y + vis.height * grow * 0.4f, back.y}, glm::vec3(0.22f * grow * glow), 0.0f,
                           glm::vec4{1.0f, 0.6f, 0.2f, 0.85f}, Unlit | Alpha);
        } else if (m < moduleLooks_.size() && moduleLooks_[m].weapon) {
            constexpr float kSmall = 0.7f;
            const glm::vec3 pivot{vis.pivot.x, vis.pivot.y, vis.pivot.z + (vis.hi.z - vis.pivot.z) * 0.55f};
            float mtx[16];
            for (const Model* part : {moduleLooks_[m].mount, moduleLooks_[m].weapon}) {
                if (!part)
                    continue;
                makeTurretMatrix(mtx, base, hullYaw, pivot / kSmall, e.aim + kModelYaw, part == moduleLooks_[m].weapon ? e.pitch : 0.0f,
                                 vis.scale * grow * kSmall);
                renderer_.drawMesh(part->mesh, part->texture, part->mask, mtx, color, part->glow ? Tint | Glow : Tint);
            }
        }
    }
}

void App::renderSkillPreview() {
    if (armedSkill_ < 0)
        return;
    const auto* own = focusedOwn();
    const auto* caster = selected_.empty() ? nullptr : findEntity(selected_.front());
    if (!own || !caster || static_cast<std::size_t>(armedSkill_) >= own->skills.size()) {
        armedSkill_ = -1;
        return;
    }
    const auto& skill = data_->skill(own->skills[static_cast<std::size_t>(armedSkill_)].skill);
    const auto ground = camera_.screenToGround(mouse_.x, mouse_.y);
    if (skill.range > 0.0f)
        renderer_.draw(Mesh::Cylinder, {caster->pos.x, 0.02f, caster->pos.y}, {skill.range * 2.0f, 0.02f, skill.range * 2.0f}, 0.0f,
                       glm::vec4{1.0f, 1.0f, 1.0f, 0.07f}, Unlit | Alpha);
    if (!ground)
        return;
    float radius = 0.6f;
    for (const auto& fx : skill.effects)
        radius = std::max(radius, fx.radius);
    const bool inRange = skill.range <= 0.0f || glm::distance(*ground, caster->pos) <= skill.range + 0.5f;
    const glm::vec4 color = inRange ? glm::vec4{1.0f, 0.7f, 0.2f, 0.3f} : glm::vec4{1.0f, 0.2f, 0.2f, 0.3f};
    renderer_.draw(Mesh::Cylinder, {ground->x, 0.04f, ground->y}, {radius * 2.0f, 0.04f, radius * 2.0f}, 0.0f, color, Unlit | Alpha);
}

}

namespace vw::client {

void App::redDot(ImDrawList* dl, ImVec2 at, float radius) {
    dl->AddCircleFilled(at, radius + 1.5f, IM_COL32(20, 10, 10, 220));
    dl->AddCircleFilled(at, radius, IM_COL32(235, 45, 45, 255));
}

void App::levelUpEffect(glm::vec2 pos, const std::string& label) {
    for (int i = 0; i < 3; ++i) {
        Effect ring{Mesh::Cylinder, {pos.x, 0.15f, pos.y}, {1.0f, 0.82f, 0.25f, 0.85f}, 0.6f, 2.6f, 0.9f};
        ring.rise = 2.2f;
        ring.delay = 0.18f * static_cast<float>(i);
        effects_.push_back(ring);
    }
    Effect beam{Mesh::Cube, {pos.x, 1.6f, pos.y}, {1.0f, 0.9f, 0.45f, 0.55f}, 3.0f, 3.4f, 0.7f};
    effects_.push_back(beam);
    addEffect(Mesh::Sphere, {pos.x, 0.8f, pos.y}, {1.0f, 0.85f, 0.3f, 0.6f}, 0.4f, 2.6f, 0.5f);
    floaters_.push_back({{pos.x, 2.2f, pos.y}, label, IM_COL32(255, 214, 90, 255)});
}

void App::trackLevelUps() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    std::set<std::uint32_t> present;
    for (const auto& u : latest->view.units) {
        present.insert(u.id);
        const auto it = known_.find(u.id);
        if (it != known_.end() && (u.rank > it->second.rank || (u.rank == it->second.rank && u.level > it->second.level))) {
            if (const auto* e = findEntity(u.id)) {
                static constexpr const char* kRanks[] = {"RECRUIT", "VETERAN", "ELITE", "HERO"};
                const std::string label = u.rank > it->second.rank ? kRanks[std::min<int>(u.rank, 3)] : "LEVEL " + std::to_string(u.level);
                levelUpEffect(e->pos, ICON_FA_ARROW_UP " " + label);
            }
            if (std::find(selected_.begin(), selected_.end(), u.id) == selected_.end())
                unseen_.insert(u.id);
            if (u.rank > it->second.rank && u.rank >= static_cast<std::uint8_t>(Rank::Veteran))
                modulesDot_ = true;
            if (u.rank > it->second.rank && u.rank >= static_cast<std::uint8_t>(Rank::Elite) && !data_->refitOptions(findEntity(u.id) ? findEntity(u.id)->def : 0).empty())
                refitDot_ = true;
        }
        known_[u.id] = {u.rank, u.level};
    }
    std::erase_if(known_, [&](const auto& kv) { return !present.contains(kv.first); });
    std::erase_if(unseen_, [&](std::uint32_t id) {
        return !present.contains(id) || std::find(selected_.begin(), selected_.end(), id) != selected_.end();
    });
}

void App::drawFloaters() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float size = 22.0f * uiScale();
    for (const auto& f : floaters_) {
        const float k = f.t / f.life;
        const glm::vec2 p = camera_.worldToScreen({f.pos.x, f.pos.y + k * 2.5f, f.pos.z});
        const ImVec2 ts = ui_.bold()->CalcTextSizeA(size, FLT_MAX, 0.0f, f.text.c_str());
        const int alpha = static_cast<int>(255.0f * std::clamp(1.0f - k * k, 0.0f, 1.0f));
        const ImVec2 at{p.x - ts.x * 0.5f, p.y - ts.y};
        dl->AddText(ui_.bold(), size, {at.x + 2.0f, at.y + 2.0f}, IM_COL32(0, 0, 0, alpha * 2 / 3), f.text.c_str());
        dl->AddText(ui_.bold(), size, at, (f.color & 0x00ffffffu) | (static_cast<std::uint32_t>(alpha) << 24), f.text.c_str());
    }
}

void App::drawLevelUps() {
    if (unseen_.empty())
        return;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y - hudBottom_ - 52.0f * s}, ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##levelups", nullptr, kHudFlags);
    char label[64];
    std::snprintf(label, sizeof(label), ICON_FA_ARROW_UP "  %d unit%s promoted", static_cast<int>(unseen_.size()), unseen_.size() > 1 ? "s" : "");
    if (ImGui::Button(label)) {
        const std::uint32_t id = *unseen_.begin();
        selected_ = {id};
        if (const auto* e = findEntity(id))
            camera_.target = e->pos;
    }
    redDot(ImGui::GetWindowDrawList(), {ImGui::GetItemRectMax().x - 3.0f * s, ImGui::GetItemRectMin().y + 3.0f * s}, 5.0f * s);
    ImGui::SetItemTooltip("Click to select it - new modules, refits or skills may be available");
    trackWindow();
    ImGui::End();
}

}

namespace vw::client {

float App::walkBob(const net::InterpolatedEntity& e) const {
    const float speed = std::max(data_->unit(e.def).speed, 0.1f);
    const float amp = std::clamp(glm::length(e.vel) / speed, 0.0f, 1.0f);
    const float t = static_cast<float>(now_) * 9.0f + static_cast<float>(e.id % 13u);
    return std::abs(std::sin(t)) * 0.08f * amp * data_->unit(e.def).radius;
}

void App::renderLegs(const net::InterpolatedEntity& e, const UnitVisual& vis, glm::vec3 base, float hullYaw, float scale, const glm::vec4& color) {
    if (vis.legs.empty())
        return;
    const float speed = std::max(data_->unit(e.def).speed, 0.1f);
    const float amp = std::clamp(glm::length(e.vel) / speed, 0.0f, 1.0f);
    const float t = static_cast<float>(now_) * (4.5f + 4.0f * amp) + static_cast<float>(e.id % 13u);
    float mtx[16];
    for (std::size_t i = 0; i < vis.legs.size(); ++i) {
        const Model* leg = vis.legs[i];
        const glm::vec3 hip = vis.hips[i];
        const float side = hip.x >= 0.0f ? 1.0f : -1.0f;
        const float phase = t + (i % 2 ? 3.14159f : 0.0f) + static_cast<float>(i / 2) * 1.5708f;
        const float idle = 0.04f * std::sin(static_cast<float>(now_) * 1.7f + static_cast<float>(i));
        const float swing = std::sin(phase) * 0.45f * amp + idle;
        const float lift = std::max(0.0f, std::cos(phase)) * 0.35f * amp * side;
        makeLegMatrix(mtx, base, hullYaw, scale, hip, swing, lift);
        renderer_.drawMesh(leg->mesh, leg->texture, leg->mask, mtx, color, leg->glow ? Tint | Glow : Tint);
    }
}

void App::crystalFire(const net::InterpolatedEntity& shooter) {
    const auto& vis = visuals_[shooter.def];
    const glm::vec3 muzzle = muzzleOf(shooter);
    const glm::vec3 base{shooter.pos.x, liftOf(shooter), shooter.pos.y};
    const float yaw = shooter.facing + vis.yaw;
    const glm::vec4 color{0.85f, 0.3f, 1.0f, 1.0f};
    for (const auto& tip : vis.tips)
        beams_.push_back({modelPoint(base, yaw, vis.scale, tip), muzzle, color, 0.09f, 0.28f});
    addEffect(Mesh::Sphere, muzzle, {0.9f, 0.5f, 1.0f, 0.8f}, 0.3f, 1.1f, 0.25f);
}

int App::anchorsAlive() const {
    int n = 0;
    for (const auto& e : entities_)
        n += e.kind == net::EntityKind::Unit && e.hp > 0 && data_->unit(e.def).anchor;
    return n;
}

void App::renderVoidFx() {
    const float now = static_cast<float>(now_);
    for (const auto& b : beams_) {
        const float k = b.t / b.life;
        const float w = b.width * (1.0f - k * 0.6f);
        renderer_.drawBeam(b.a, b.b, w, b.color * (1.0f - k));
        renderer_.drawBeam(b.a, b.b, w * 0.35f, glm::vec4(1.0f, 0.9f, 1.0f, 1.0f) * (1.0f - k));
    }

    const net::InterpolatedEntity* portal = nullptr;
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit && e.def < visuals_.size() && visuals_[e.def].gate.w > 0.0f)
            portal = &e;
    if (!portal)
        return;
    const auto& pv = visuals_[portal->def];
    const float yaw = portal->facing + pv.yaw;
    const glm::vec3 base{portal->pos.x, 0.0f, portal->pos.y};
    const int anchors = anchorsAlive();
    const glm::vec4 color = anchors > 0 ? glm::vec4(0.75f, 0.3f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.45f, 1.0f);
    float local[16];
    const float r = pv.gate.w;
    bx::mtxSRT(local, r, r, r, 0.0f, 0.0f, 0.0f, pv.gate.x, pv.gate.y, pv.gate.z);
    float model[16];
    makeModelMatrix(model, base, yaw, pv.scale, {});
    float mtx[16];
    bx::mtxMul(mtx, local, model);
    renderer_.drawPortal(mtx, color);
    const glm::vec3 center = modelPoint(base, yaw, pv.scale, glm::vec3(pv.gate));
    for (int k = 0; k < 14; ++k) {
        const float a = now * (0.9f + 0.13f * static_cast<float>(k % 4)) + static_cast<float>(k) * 0.4488f;
        const float rr = r * (1.05f - 0.35f * std::fmod(now * 0.35f + static_cast<float>(k) * 0.29f, 1.0f));
        const glm::vec3 spot{pv.gate.x + std::cos(a) * rr, pv.gate.y + std::sin(a) * rr, pv.gate.z + 0.1f};
        renderer_.drawGlow(Mesh::Sphere, modelPoint(base, yaw, pv.scale, spot), glm::vec3(0.22f), color * 0.7f);
    }
    renderer_.drawGlow(Mesh::Sphere, center, glm::vec3(r * pv.scale * 0.5f), color * 0.12f);

    int i = 0;
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || !data_->unit(e.def).anchor || e.hp == 0)
            continue;
        const auto& av = visuals_[e.def];
        const glm::vec3 from = av.tips.empty() ? glm::vec3(e.pos.x, av.height, e.pos.y)
                                               : modelPoint({e.pos.x, 0.0f, e.pos.y}, e.facing + av.yaw, av.scale, av.tips.front());
        const float pulse = 0.75f + 0.25f * std::sin(now * 5.0f + static_cast<float>(i));
        renderer_.drawBeam(from, center, 0.2f * pulse, glm::vec4(0.35f, 0.06f, 0.6f, 1.0f));
        renderer_.drawBeam(from, center, 0.05f, glm::vec4(0.75f, 0.45f, 0.95f, 1.0f) * 0.6f);
        for (int k = 0; k < 3; ++k) {
            const float f = std::fmod(now * 0.7f + static_cast<float>(k) / 3.0f + static_cast<float>(i) * 0.17f, 1.0f);
            renderer_.drawGlow(Mesh::Sphere, from + (center - from) * f, glm::vec3(0.3f), {0.5f, 0.2f, 0.8f, 1.0f});
        }
        renderer_.drawGlow(Mesh::Sphere, from, glm::vec3(0.6f * pulse), {0.4f, 0.1f, 0.65f, 1.0f});
        ++i;
    }
}

}
