#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <imgui.h>

#include <icons_font_awesome.h>

#include "core/Version.h"

namespace vw::client {

namespace {

constexpr ImU32 kAccent = IM_COL32(61, 184, 255, 255);
constexpr ImU32 kVoid = IM_COL32(255, 64, 140, 255);
constexpr ImU32 kEnergy = IM_COL32(255, 210, 80, 255);
constexpr ImU32 kShard = IM_COL32(190, 130, 255, 255);
constexpr ImU32 kGood = IM_COL32(90, 230, 120, 255);
constexpr ImU32 kBad = IM_COL32(255, 80, 80, 255);
constexpr ImU32 kDim = IM_COL32(125, 135, 158, 255);
constexpr ImGuiWindowFlags kHudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
constexpr const char* kRankNames[] = {"Recruit", "Veteran", "Elite", "Hero"};
constexpr ImU32 kRankColors[] = {IM_COL32(170, 178, 195, 255), IM_COL32(205, 215, 235, 255), IM_COL32(170, 120, 255, 255), IM_COL32(255, 205, 70, 255)};
constexpr ImU32 kRarityColors[] = {IM_COL32(210, 215, 225, 255), IM_COL32(80, 160, 255, 255), IM_COL32(190, 110, 255, 255), IM_COL32(255, 170, 40, 255)};
constexpr const char* kRarityNames[] = {"Common", "Rare", "Epic", "Legendary"};
constexpr const char* kSkillKeys[] = {"Q", "W", "E", "R"};

ImVec4 toVec(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

void bar(float fraction, ImVec2 size, ImU32 fill, const char* label) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = size.y * 0.35f;
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, IM_COL32(10, 12, 20, 230), r);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        dl->AddRectFilled(p, {p.x + size.x * f, p.y + size.y}, fill, r);
    dl->AddRect(p, {p.x + size.x, p.y + size.y}, IM_COL32(255, 255, 255, 28), r);
    if (label && *label) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 tp{p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f};
        dl->AddText({tp.x + 1.0f, tp.y + 1.0f}, IM_COL32(0, 0, 0, 200), label);
        dl->AddText(tp, IM_COL32(255, 255, 255, 240), label);
    }
    ImGui::Dummy(size);
}

ImU32 hpColor(float frac) {
    const ImVec4 a{1.0f, 0.25f, 0.2f, 1.0f};
    const ImVec4 b{0.3f, 0.9f, 0.4f, 1.0f};
    const float t = std::clamp(frac, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f});
}

bool accentButton(const char* label, ImVec2 size, ImU32 color = kAccent) {
    const ImVec4 c = toVec(color);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{c.x * 0.55f, c.y * 0.55f, c.z * 0.55f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{c.x * 0.75f, c.y * 0.75f, c.z * 0.75f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool toggleButton(const char* label, bool active, ImVec2 size) {
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, toVec(IM_COL32(40, 110, 160, 255)));
    const bool pressed = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor();
    return pressed;
}

void centeredText(const char* text, ImU32 color) {
    const float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f + ImGui::GetCursorPosX()));
    ImGui::TextColored(toVec(color), "%s", text);
}

std::string effectText(const SkillEffect& fx, int level) {
    const float scale = 1.0f + 0.25f * static_cast<float>(level - 1);
    char buf[96];
    switch (fx.type) {
    case EffectType::Damage:
        std::snprintf(buf, sizeof(buf), "Deals %.0f damage in %.1f radius", fx.amount * scale, fx.radius);
        break;
    case EffectType::Heal:
        std::snprintf(buf, sizeof(buf), "Heals %.0f in %.1f radius", fx.amount * scale, fx.radius);
        break;
    case EffectType::Buff:
        std::snprintf(buf, sizeof(buf), "Buff for %.0fs", fx.duration);
        break;
    case EffectType::Dash:
        std::snprintf(buf, sizeof(buf), "Dash up to %.0f cells", fx.amount);
        break;
    case EffectType::Summon:
        std::snprintf(buf, sizeof(buf), "Summons %d allies", fx.count);
        break;
    case EffectType::Aura:
        std::snprintf(buf, sizeof(buf), "Aura in %.1f radius", fx.radius);
        break;
    }
    return buf;
}

void skillTooltip(const SkillDef& skill, int level) {
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::TextColored(toVec(kAccent), "%s", skill.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(skill.passive ? "passive" : "cooldown %.0fs", skill.cooldown);
    for (const auto& fx : skill.effects)
        ImGui::BulletText("%s", effectText(fx, std::max(level, 1)).c_str());
    ImGui::EndTooltip();
}

}

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
    return 1.0f;
#else
    return std::max(1.0f, SDL_GetWindowDisplayScale(window_)) * (touchUi_ ? 1.15f : 1.0f);
#endif
}

void App::drawUi() {
    uiRects_.clear();
    if (screen_ == Screen::Game && map_) {
        drawHud();
    } else {
        drawMenu();
    }
}

void App::drawMenu() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor({0.0f, 0.0f}, display, IM_COL32(10, 14, 28, 255), IM_COL32(16, 10, 30, 255), IM_COL32(4, 4, 8, 255),
                                IM_COL32(4, 6, 12, 255));
    const float t = static_cast<float>(now_);
    for (int i = 0; i < 3; ++i) {
        const float a = t * 0.07f + static_cast<float>(i) * 2.1f;
        const ImVec2 c{display.x * (0.5f + 0.35f * std::cos(a)), display.y * (0.5f + 0.3f * std::sin(a * 1.3f))};
        bg->AddCircleFilled(c, display.y * 0.45f, i == 1 ? IM_COL32(255, 40, 120, 10) : IM_COL32(40, 150, 255, 10), 64);
    }

    const float width = (screen_ == Screen::Online || screen_ == Screen::Lan ? 680.0f : 440.0f) * s;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {width, display.y * 0.95f});
    ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    const float full = ImGui::GetContentRegionAvail().x;
    const ImVec2 big{full, 38.0f * s};

    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 2.4f);
    centeredText("VOID WARZONE", kAccent);
    ImGui::PopFont();
    char version[64];
    std::snprintf(version, sizeof(version), "co-op defense  |  v%.*s", static_cast<int>(kVersion.size()), kVersion.data());
    centeredText(version, kDim);
    ImGui::Spacing();

    auto back = [&] {
        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_CHEVRON_LEFT "  Back", {full, 36.0f * s})) {
            if (screen_ == Screen::Online)
                lobby_.close();
            status_.clear();
            settingsOpen_ = false;
            setScreen(Screen::Menu);
        }
    };

    if (settingsOpen_ && screen_ == Screen::Menu) {
        ImGui::SeparatorText("Settings");
        drawSettings();
        back();
    } else {
        switch (screen_) {
        case Screen::Menu: {
            ImGui::SetNextItemWidth(full);
            if (ImGui::InputTextWithHint("##name", "Commander name", nameBuf_.data(), nameBuf_.size()))
                options_.name = nameBuf_.data();
            if (ImGui::IsItemDeactivatedAfterEdit())
                saveSettings();
            ImGui::Spacing();
            ImGui::TextDisabled("Difficulty");
            const float seg = (full - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
            static constexpr const char* kDiff[] = {"Easy", "Normal", "Hard", "Nightmare"};
            for (int i = 0; i < 4; ++i) {
                if (i > 0)
                    ImGui::SameLine();
                if (toggleButton(kDiff[i], static_cast<int>(options_.difficulty) == i, {seg, 32.0f * s})) {
                    options_.difficulty = static_cast<Difficulty>(i);
                    saveSettings();
                }
            }
            ImGui::Spacing();
            if (accentButton(ICON_FA_PLAY "   PLAY", {full, 48.0f * s}))
                startHost();
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_KEY "   Join by room code", big)) {
                status_.clear();
                setScreen(Screen::Code);
            }
            if (ImGui::Button(ICON_FA_GLOBE "   Online games", big)) {
                status_.clear();
                setScreen(Screen::Online);
            }
            if (ImGui::Button(ICON_FA_WIFI "   LAN games", big)) {
                status_.clear();
                setScreen(Screen::Lan);
            }
            if (ImGui::Button(ICON_FA_PLUG "   Connect by IP", big)) {
                status_.clear();
                setScreen(Screen::Address);
            }
            const float half = (full - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button(ICON_FA_COG "  Settings", {half, 36.0f * s}))
                settingsOpen_ = true;
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_POWER_OFF "  Quit", {half, 36.0f * s}))
                running_ = false;
            break;
        }
        case Screen::Code:
        case Screen::Address: {
            const bool code = screen_ == Screen::Code;
            ImGui::SeparatorText(code ? "Join by room code" : "Connect by IP");
            ImGui::TextDisabled(code ? "Ask the host for the code shown in their game" : "Host address, port optional (default 27450)");
            ImGui::SetNextItemWidth(full);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            char* buf = code ? codeBuf_.data() : addressBuf_.data();
            const std::size_t bufSize = code ? codeBuf_.size() : addressBuf_.size();
            const bool enter = ImGui::InputTextWithHint("##input", code ? "VOID-XXXX" : "192.168.1.10:27450", buf, bufSize,
                                                        ImGuiInputTextFlags_EnterReturnsTrue | (code ? ImGuiInputTextFlags_CharsUppercase : 0));
            ImGui::Spacing();
            ImGui::BeginDisabled(buf[0] == '\0');
            if (accentButton(code ? ICON_FA_KEY "   Join" : ICON_FA_PLUG "   Connect", big) || (enter && buf[0] != '\0')) {
                if (code) {
                    startJoin(buf);
                } else if (const auto addr = net::Address::parse(buf, net::kGamePort)) {
                    startConnect(*addr);
                } else {
                    status_ = "bad address";
                }
            }
            ImGui::EndDisabled();
            back();
            break;
        }
        case Screen::Online:
        case Screen::Lan: {
            const bool online = screen_ == Screen::Online;
            const bool loading = online && lobby_.state() == net::MasterClient::State::Connecting;
            ImGui::SeparatorText(online ? "Online games" : "LAN games");
            ImGui::TextDisabled("%s", online ? (loading ? "refreshing..." : "games registered on the master server")
                                             : "searching the local network...");
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
            int rows = 0;
            if (ImGui::BeginTable("games", 5, flags)) {
                ImGui::TableSetupColumn(online ? "Code" : "Address", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableHeadersRow();
                auto row = [&](const std::string& id, const std::string& name, const std::string& mapName, int players, int maxPlayers) {
                    ImGui::TableNextRow(0, 40.0f * s);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(toVec(kAccent), "%s", id.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", mapName.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text(ICON_FA_USERS " %d/%d", players, maxPlayers);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushID(rows++);
                    const bool join = accentButton("Join", {80.0f * s, 0.0f});
                    ImGui::PopID();
                    return join;
                };
                if (online) {
                    for (const auto& room : lobby_.rooms())
                        if (row(room.code, room.name, room.map, room.players, room.maxPlayers)) {
                            startJoin(room.code);
                            break;
                        }
                } else {
                    for (const auto& srv : lan_.servers())
                        if (row(srv.address.toString(), srv.name, srv.map, srv.players, srv.maxPlayers)) {
                            startConnect(srv.address);
                            break;
                        }
                }
                ImGui::EndTable();
            }
            if (rows == 0) {
                ImGui::Spacing();
                const bool failed = online && lobby_.state() == net::MasterClient::State::Failed;
                centeredText(failed ? lobby_.error().c_str() : "No games found yet - host one with PLAY", failed ? kBad : kDim);
            }
            back();
            break;
        }
        case Screen::Joining:
        case Screen::Connecting: {
            ImGui::Spacing();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 c{p.x + full * 0.5f, p.y + 26.0f * s};
            const float a0 = static_cast<float>(now_) * 6.0f;
            dl->PathArcTo(c, 18.0f * s, a0, a0 + 4.2f, 32);
            dl->PathStroke(kAccent, 0, 4.0f * s);
            ImGui::Dummy({full, 56.0f * s});
            centeredText(status_.empty() ? "connecting" : status_.c_str(), IM_COL32(220, 228, 240, 255));
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_TIMES "  Cancel", {full, 38.0f * s}))
                leaveGame("cancelled");
            break;
        }
        case Screen::Game:
            break;
        }
    }

    if (!status_.empty() && screen_ != Screen::Connecting && screen_ != Screen::Joining) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(toVec(kBad), ICON_FA_TIMES " %s", status_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void App::drawSettings() {
    const float full = ImGui::GetContentRegionAvail().x;
    static constexpr const char* kShadows[] = {"Off", "Low", "Medium", "High"};
    static constexpr const char* kLights[] = {"Day", "Dusk", "Night"};
    ImGui::BeginDisabled(!renderer_.shadowsSupported());
    ImGui::SetNextItemWidth(full * 0.55f);
    if (ImGui::Combo("Shadows", &shadowQuality_, kShadows, 4)) {
        applyGraphics();
        saveSettings();
    }
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(full * 0.55f);
    if (ImGui::Combo("Lighting", &lightPreset_, kLights, 3)) {
        applyGraphics();
        saveSettings();
    }
    if (ImGui::Checkbox("Performance stats (F3)", &showStats_))
        saveSettings();
    if (screen_ != Screen::Game) {
        ImGui::SetNextItemWidth(full * 0.55f);
        if (ImGui::InputText("Master server", masterBuf_.data(), masterBuf_.size()))
            options_.master = masterBuf_.data();
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveSettings();
    }
}

void App::drawHud() {
    const auto* latest = net_.latest();
    if (!latest)
        return;
    drawTopBar();
    drawBossBar();
    drawUnitPanel();
    drawCommandBar();
    drawSidePanel();
    if (showStats_)
        drawStats();

    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const auto state = static_cast<MatchState>(latest->game.state);
    if (state != MatchState::Playing) {
        drawEndScreen();
    } else if (bannerTime_ > 0.0f && !banner_.empty()) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float size = 46.0f * s;
        const float alpha = std::clamp(bannerTime_ / 0.6f, 0.0f, 1.0f);
        const ImVec2 ts = ui_.bold()->CalcTextSizeA(size, FLT_MAX, 0.0f, banner_.c_str());
        const ImVec2 p{(display.x - ts.x) * 0.5f, display.y * 0.27f};
        fg->AddText(ui_.bold(), size, {p.x + 3.0f, p.y + 3.0f}, IM_COL32(0, 0, 0, static_cast<int>(170 * alpha)), banner_.c_str());
        fg->AddText(ui_.bold(), size, p, IM_COL32(255, 214, 90, static_cast<int>(255 * alpha)), banner_.c_str());
    }

    if (attackMoveArmed_ || armedSkill_ >= 0) {
        ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.7f}, ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::Begin("##armed", nullptr, kHudFlags);
        ImGui::TextColored(toVec(kEnergy), ICON_FA_CROSSHAIRS "  %s", attackMoveArmed_ ? "Pick a point to attack-move" : "Pick the skill target");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
            attackMoveArmed_ = false;
            armedSkill_ = -1;
        }
        trackWindow();
        ImGui::End();
    }
    if (pauseOpen_)
        drawPauseMenu();
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
    ImGui::PushFont(ui_.bold(), 0.0f);
    if (game.wave < game.totalWaves)
        ImGui::Text(ICON_FA_FLAG "  WAVE %d / %d", game.wave, game.totalWaves);
    else
        ImGui::TextColored(toVec(kVoid), ICON_FA_FLAG "  FINAL WAVE");
    ImGui::PopFont();
    if (game.wave < game.totalWaves) {
        const auto& next = defense.waves[static_cast<std::size_t>(std::min<int>(game.wave, static_cast<int>(defense.waves.size()) - 1))];
        const float interval = game.wave == 0 ? defense.firstWaveDelay : (next.delay > 0.0f ? next.delay : defense.waveInterval);
        const float left = static_cast<float>(game.nextWaveIn) / 10.0f;
        char label[32];
        std::snprintf(label, sizeof(label), "next in %.0fs", left);
        bar(1.0f - left / std::max(interval, 1.0f), {150.0f * s, 14.0f * s}, IM_COL32(255, 170, 60, 220), label);
    } else {
        ImGui::TextDisabled("destroy the portal");
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, 28.0f * s);
    ImGui::BeginGroup();
    const auto& points = map_->resourcePoints();
    for (std::size_t i = 0; i < points.size() && i < game.points.size(); ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, 6.0f * s);
        const auto& info = game.points[i];
        const ImU32 color = info.owner == 0 ? kAccent : info.owner == 1 ? kVoid : IM_COL32(120, 125, 140, 255);
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
            ImGui::Text("Point %s  +%.0f energy/s", points[i].name.c_str(), points[i].income);
            ImGui::TextDisabled("%s", info.owner == 0 ? "held by your team" : info.owner == 1 ? "held by the Void" : "neutral - stand on it to capture");
            ImGui::EndTooltip();
        }
    }
    ImGui::EndGroup();

    float income = defense.baseIncome;
    for (std::size_t i = 0; i < points.size() && i < game.points.size(); ++i)
        if (game.points[i].owner == 0)
            income += points[i].income;
    for (const auto& e : entities_)
        if (e.kind == net::EntityKind::Unit && e.team == 0 && e.hp > 0)
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
        ImGui::SetItemTooltip("Room code - click to copy. Friends join via \"Join by room code\".");
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
    ImGui::SetItemTooltip("Shards - drop from bosses, used to raise heroes and buy rare items");
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
    ImGui::SetNextWindowPos({display.x * 0.5f, topBarHeight_ + 8.0f * s}, ImGuiCond_Always, {0.5f, 0.0f});
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
        ImGui::TextDisabled("lvl %d", own->level);
    }
    if (selected_.size() > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("+%d more", static_cast<int>(selected_.size()) - 1);
    }

    const float w = ImGui::GetContentRegionAvail().x;
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
        std::snprintf(label, sizeof(label), ICON_FA_ARROW_UP "  Promote to Elite   " ICON_FA_BOLT "%.0f", kEliteCost);
        if (ImGui::Button(label, {w, 0.0f}))
            pressKey(SDLK_P, false);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Needs %.0f XP and %.0f energy. Elites get item slots and more power. [P]", kEliteXp, kEliteCost);
    } else if (rank == Rank::Elite && def.skills.empty()) {
        ImGui::TextDisabled("This unit cannot become a hero");
    } else if (rank == Rank::Elite) {
        ImGui::BeginDisabled(static_cast<float>(own->xp) < kHeroXp || latest->view.shards < kHeroShards);
        std::snprintf(label, sizeof(label), ICON_FA_TROPHY "  Raise a Hero   " ICON_FA_DIAMOND "%d", kHeroShards);
        if (accentButton(label, {w, 0.0f}, IM_COL32(255, 190, 60, 255)))
            pressKey(SDLK_P, false);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Needs %.0f XP and %d shards, max %d heroes. Heroes learn skills. [P]", kHeroXp, kHeroShards, kMaxHeroes);
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
                ImGui::SetItemTooltip("Empty slot - learn a skill");
                ImGui::PopID();
                continue;
            }
            const auto& slot = own->skills[i];
            const auto& skill = data_->skill(slot.skill);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool armed = armedSkill_ == static_cast<int>(i);
            std::string name = skill.name.substr(0, 9);
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
            const ImVec2 ns = ImGui::CalcTextSize(name.c_str());
            dl->AddText({p.x + (box - ns.x) * 0.5f, q.y - ns.y - 4.0f * s}, skill.passive ? kDim : IM_COL32(235, 240, 250, 255), name.c_str());
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
            std::snprintf(label, sizeof(label), ICON_FA_BOOK "  %d skill point%s - learn", own->skillPoints, own->skillPoints > 1 ? "s" : "");
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
                    ImGui::TextDisabled("click to move back to the stash");
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button(ICON_FA_CUBE, {box, box});
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("Empty item slot - equip from the stash");
            }
            ImGui::PopID();
        }
    }
    trackWindow();
    ImGui::End();
}

void App::drawCommandBar() {
    const auto* latest = net_.latest();
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float pad = 10.0f * s;
    const ImVec2 cell{(touchUi_ ? 82.0f : 74.0f) * s, (touchUi_ ? 58.0f : 52.0f) * s};

    ImGui::SetNextWindowPos({display.x - pad, display.y - pad}, ImGuiCond_Always, {1.0f, 1.0f});
    ImGui::Begin("##commands", nullptr, kHudFlags);
    const auto& recruitable = data_->recruitable();
    for (std::size_t i = 0; i < recruitable.size() && i < 9; ++i) {
        const auto& def = data_->unit(recruitable[i]);
        ImGui::PushID(static_cast<int>(i));
        char label[64];
        std::snprintf(label, sizeof(label), "%s\n" ICON_FA_BOLT "%.0f", def.name.c_str(), def.cost);
        ImGui::BeginDisabled(static_cast<float>(latest->view.energy) < def.cost);
        if (ImGui::Button(label, cell))
            net_.sendCommand(RecruitCommand{0, recruitable[i]});
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::Text("Recruit %s  [%d]", def.name.c_str(), static_cast<int>(i) + 1);
            ImGui::TextDisabled("HP %.0f  armor %.0f  speed %.1f", def.maxHp, def.armor, def.speed);
            if (def.weapon)
                ImGui::TextDisabled("damage %.0f  range %.1f  every %.1fs", def.weapon->damage, def.weapon->range, def.weapon->cooldown);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::SameLine(0.0f, 14.0f * s);
    if (toggleButton(ICON_FA_SHOPPING_CART "\nShop", panel_ == Panel::Shop, cell))
        panel_ = panel_ == Panel::Shop ? Panel::None : Panel::Shop;
    ImGui::SetItemTooltip("Shop [B]");
    ImGui::SameLine();
    if (toggleButton(ICON_FA_ARCHIVE "\nStash", panel_ == Panel::Stash, cell))
        panel_ = panel_ == Panel::Stash ? Panel::None : Panel::Stash;
    ImGui::SetItemTooltip("Stash [I]");
    ImGui::SameLine();
    if (toggleButton(ICON_FA_BOOK "\nSkills", panel_ == Panel::Learn, cell))
        panel_ = panel_ == Panel::Learn ? Panel::None : Panel::Learn;
    ImGui::SetItemTooltip("Learn hero skills [L]");
    if (!selected_.empty()) {
        ImGui::SameLine(0.0f, 14.0f * s);
        if (toggleButton(ICON_FA_CROSSHAIRS "\nAttack", attackMoveArmed_, cell)) {
            attackMoveArmed_ = !attackMoveArmed_;
            armedSkill_ = -1;
        }
        ImGui::SetItemTooltip("Attack-move: units fight everything on the way [A]");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP "\nStop", cell))
            pressKey(SDLK_S, false);
        ImGui::SetItemTooltip("Stop [S]");
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
    ImGui::SetNextWindowSize({width, display.y * 0.62f - topBarHeight_}, ImGuiCond_Always);
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
        ImGui::TextDisabled("Bought items go to the stash (%d/%d)", static_cast<int>(latest->view.stash.size()), static_cast<int>(kStashSize));
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
            ImGui::TextDisabled("Select one of your units to equip items");
        else if (itemSlots(static_cast<Rank>(std::min<int>(own->rank, 3))) == 0)
            ImGui::TextDisabled("Only Elites and Heroes can carry items");
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
            ImGui::TextDisabled("Empty - buy items in the shop or defeat bosses");
        if (own && !own->items.empty()) {
            ImGui::SeparatorText("Equipped");
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
            ImGui::TextWrapped("Select a hero. Promote an Elite with shards to raise one.");
        } else {
            ImGui::Text("Skill points: %d", own->skillPoints);
            ImGui::TextDisabled("Up to %d skills, each up to level %d", static_cast<int>(kMaxSkills), static_cast<int>(kMaxSkillLevel));
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
                ImGui::TextDisabled("%s lvl %d/%d", skill.passive ? "passive" : "active", level, static_cast<int>(kMaxSkillLevel));
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - buttonW);
                for (const auto& fx : skill.effects)
                    ImGui::TextUnformatted(effectText(fx, std::max(level, 1)).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                const bool can = own->skillPoints > 0 && level < kMaxSkillLevel && (level > 0 || own->skills.size() < kMaxSkills);
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
        const auto& t = host_->world().systemTimes();
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
    ImGui::Text("%.0f fps  %.1f ms  %s", fps_, frameMs_, renderer_.name());
    ImGui::Text("render cpu %.1f gpu %.1f ms  draws %u", cpu, gpu, st->numDraw);
    if (host_)
        ImGui::Text("sim %.2f ms/tick  entities %d", simMs_, static_cast<int>(entities_.size()));
    ImGui::Text("rtt %.0f ms  loss %.1f%%  %s", stats.rtt, stats.packetLoss, net_.path().c_str());
    ImGui::Text("in %.1f out %.1f kbps  snap %d B", stats.receivedKbps, stats.sentKbps, static_cast<int>(net_.lastSnapshotBytes()));
    ImGui::Text("interp %.0f ms  jitter %.1f ms  predicted %d", net_.interpDelay() * 1000.0, net_.jitter() * 1000.0, static_cast<int>(prediction_.active()));
    ImGui::PopFont();
    ImGui::End();
}

void App::drawPauseMenu() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 110));
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({360.0f * s, 0.0f}, {360.0f * s, display.y});
    ImGui::Begin("##pause", nullptr, kHudFlags);
    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.5f);
    centeredText("MENU", kAccent);
    ImGui::PopFont();
    ImGui::TextDisabled("The match keeps running while this menu is open");
    if (host_ && lobby_.state() == net::MasterClient::State::Hosting) {
        ImGui::Text("Room code:");
        ImGui::SameLine();
        ImGui::TextColored(toVec(kAccent), "%s", lobby_.code().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
            SDL_SetClipboardText(lobby_.code().c_str());
    } else if (host_ && lobby_.state() == net::MasterClient::State::Failed) {
        ImGui::TextColored(toVec(kBad), "Online unavailable: %s", lobby_.error().c_str());
    }
    ImGui::Spacing();
    if (accentButton(ICON_FA_PLAY "  Resume", {full, 44.0f * s}))
        pauseOpen_ = false;
    ImGui::SeparatorText("Settings");
    drawSettings();
    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_SIGN_OUT "  Leave match", {full, 40.0f * s})) {
        pauseOpen_ = false;
        leaveGame("left the game");
    }
    trackWindow();
    ImGui::End();
}

void App::drawEndScreen() {
    const auto& game = net_.latest()->game;
    const bool victory = static_cast<MatchState>(game.state) == MatchState::Victory;
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, display, IM_COL32(0, 0, 0, 90));
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.45f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({420.0f * s, 0.0f}, {420.0f * s, display.y});
    ImGui::Begin("##end", nullptr, kHudFlags);
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 2.6f);
    centeredText(victory ? "VICTORY" : "DEFEAT", victory ? kGood : kBad);
    ImGui::PopFont();
    centeredText(victory ? "The Void portal is sealed" : "The core has fallen", IM_COL32(220, 228, 240, 255));
    char line[64];
    std::snprintf(line, sizeof(line), "Waves: %d / %d   |   %s", game.wave, game.totalWaves,
                  difficultyName(static_cast<Difficulty>(std::min<int>(game.difficulty, 3))));
    centeredText(line, kDim);
    ImGui::Spacing();
    if (accentButton(ICON_FA_SIGN_OUT "  Back to menu", {ImGui::GetContentRegionAvail().x, 46.0f * s}))
        leaveGame("");
    trackWindow();
    ImGui::End();
}

}
