#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <SDL3/SDL.h>

#include "core/Version.h"
#include "ui/Widgets.h"

namespace vw::client {

using namespace ui;

namespace {

constexpr const char* kColorNames[] = {"Green", "Blue",  "Yellow", "Orange", "Purple", "Cyan", "Pink", "White",
                                       "Red",   "Navy",  "Lime",   "Brown",  "Teal",   "Magenta", "Gold", "Grey"};

ImVec4 colorVec(std::size_t i, float alpha) {
    const std::uint32_t c = net::kPlayerColors[i];
    return {static_cast<float>((c >> 16) & 0xff) / 255.0f, static_cast<float>((c >> 8) & 0xff) / 255.0f, static_cast<float>(c & 0xff) / 255.0f,
            alpha};
}

}

namespace {

constexpr const char* kDifficultyNames[] = {"Easy", "Normal", "Hard", "Nightmare"};

void spinner(float width, float s, double now) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c{p.x + width * 0.5f, p.y + 26.0f * s};
    const float a0 = static_cast<float>(now) * 6.0f;
    dl->PathArcTo(c, 18.0f * s, a0, a0 + 4.2f, 32);
    dl->PathStroke(kAccent, 0, 4.0f * s);
    ImGui::Dummy({width, 56.0f * s});
}

bool cardSelectable(const char* id, bool selected, const std::string& title, const std::string& badge, const std::string& text, float s) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float wrap = width - 20.0f * s;
    const ImVec2 textSize = font->CalcTextSizeA(size * 0.9f, FLT_MAX, wrap, text.c_str());
    const float height = size + textSize.y + 20.0f * s;
    const bool pressed = ImGui::Selectable(id, selected, 0, {width, height});
    const ImVec2 a = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected)
        dl->AddRectFilled(a, {a.x + 3.0f, a.y + height}, kAccent);
    dl->AddText({a.x + 10.0f * s, a.y + 6.0f * s}, selected ? kAccent : kText, title.c_str());
    if (!badge.empty()) {
        const float bw = ImGui::CalcTextSize(badge.c_str()).x;
        dl->AddText({a.x + width - bw - 10.0f * s, a.y + 6.0f * s}, kDim, badge.c_str());
    }
    dl->AddText(font, size * 0.9f, {a.x + 10.0f * s, a.y + 10.0f * s + size}, kDim, text.c_str(), nullptr, wrap);
    return pressed;
}

}

void App::drawMenu() {
    if (screen_ == Screen::Lobby) {
        drawLobby();
        return;
    }
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor({0.0f, 0.0f}, display, IM_COL32(10, 14, 26, 255), IM_COL32(18, 10, 28, 255), IM_COL32(4, 4, 8, 255),
                                IM_COL32(4, 6, 12, 255));
    const float t = static_cast<float>(now_);
    for (int i = 0; i < 3; ++i) {
        const float a = t * 0.06f + static_cast<float>(i) * 2.1f;
        const ImVec2 c{display.x * (0.62f + 0.3f * std::cos(a)), display.y * (0.5f + 0.3f * std::sin(a * 1.3f))};
        bg->AddCircleFilled(c, display.y * 0.45f, i == 1 ? IM_COL32(255, 40, 120, 9) : IM_COL32(40, 150, 255, 9), 64);
    }

    const float panelW = std::min(330.0f * s, display.x * 0.42f);
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize({panelW, display.y});
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{22.0f * s, 22.0f * s});
    ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 2.0f);
    ImGui::TextColored(toVec(kAccent), "VOID");
    ImGui::TextColored(toVec(kText), "WARZONE");
    ImGui::PopFont();
    ImGui::TextDisabled("co-op defense  v%.*s", static_cast<int>(kVersion.size()), kVersion.data());
    const bool busy = screen_ == Screen::Connecting || screen_ == Screen::Joining;
    if (!status_.empty() && !busy) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(toVec(kBad), "%s", status_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy({0.0f, 18.0f * s});

    const float h = 38.0f * s;
    ImGui::BeginDisabled(busy);
    if (menuButton(ICON_FA_FLAG "   Campaign", false, full, h)) {
        settingsOpen_ = false;
        loadCampaign();
        setScreen(Screen::Campaign);
    }
    if (menuButton(ICON_FA_PLAY "   Single Player", false, full, h)) {
        settingsOpen_ = false;
        campaignMission_.clear();
        startHost(false);
    }
    if (menuButton(ICON_FA_USERS "   Host Multiplayer", false, full, h)) {
        settingsOpen_ = false;
        campaignMission_.clear();
        startHost(true);
    }
    ImGui::Dummy({0.0f, 6.0f * s});
    if (menuButton(ICON_FA_GLOBE "   Server Browser", !settingsOpen_ && (screen_ == Screen::Online || screen_ == Screen::Lan), full, h)) {
        settingsOpen_ = false;
        status_.clear();
        setScreen(Screen::Online);
    }
    if (menuButton(ICON_FA_KEY "   Join by Code", !settingsOpen_ && screen_ == Screen::Code, full, h)) {
        settingsOpen_ = false;
        status_.clear();
        setScreen(Screen::Code);
    }
    if (menuButton(ICON_FA_PLUG "   Direct Connect", !settingsOpen_ && screen_ == Screen::Address, full, h)) {
        settingsOpen_ = false;
        status_.clear();
        setScreen(Screen::Address);
    }
    ImGui::Dummy({0.0f, 6.0f * s});
    if (menuButton(ICON_FA_MAP "   Map Editor", false, full, h)) {
        settingsOpen_ = false;
        openEditor(options_.map);
    }
    if (menuButton(ICON_FA_COG "   Settings", settingsOpen_, full, h)) {
        settingsOpen_ = !settingsOpen_;
        if (settingsOpen_ && screen_ != Screen::Menu)
            handleMenuKey(SDLK_ESCAPE);
    }
    if (menuButton(ICON_FA_BOOK "   Guide", guideOpen_, full, h))
        guideOpen_ = !guideOpen_;
    if (menuButton(ICON_FA_POWER_OFF "   Quit", false, full, h))
        running_ = false;
    ImGui::EndDisabled();


    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 10.0f * s, ImGui::GetWindowHeight() - 80.0f * s));
    ImGui::TextDisabled("Commander");
    ImGui::SetNextItemWidth(full);
    if (ImGui::InputTextWithHint("##name", "your name", nameBuf_.data(), nameBuf_.size()))
        options_.name = nameBuf_.data();
    if (ImGui::IsItemDeactivatedAfterEdit())
        saveSettings();
    ImGui::End();

    if (settingsOpen_ || screen_ != Screen::Menu) {
        const float pad = 24.0f * s;
        const float x = panelW + pad;
        const float w = std::min(display.x - x - pad, (screen_ == Screen::Online || screen_ == Screen::Lan) && !settingsOpen_ ? 760.0f * s : 560.0f * s);
        ImGui::SetNextWindowPos({x + (display.x - x - pad - w) * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.0f, 0.5f});
        ImGui::SetNextWindowSizeConstraints({w, 0.0f}, {w, display.y - pad * 2.0f});
        ImGui::Begin("##page", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        drawMenuPage();
        ImGui::End();
    }
    if (guideOpen_)
        drawGuide();
}

void App::drawMenuPage() {
    const float s = uiScale();
    const float full = ImGui::GetContentRegionAvail().x;
    if (settingsOpen_) {
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted("Settings");
        ImGui::PopFont();
        drawSettings();
        return;
    }
    switch (screen_) {
    case Screen::Code:
    case Screen::Address: {
        const bool code = screen_ == Screen::Code;
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted(code ? "Join by room code" : "Direct connect");
        ImGui::PopFont();
        ImGui::TextDisabled(code ? "Ask the host for the code shown in their lobby" : "Host address, port optional (default 27450)");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(full);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        char* buf = code ? codeBuf_.data() : addressBuf_.data();
        const std::size_t bufSize = code ? codeBuf_.size() : addressBuf_.size();
        const bool enter = ImGui::InputTextWithHint("##input", code ? "VOID-XXXX" : "192.168.1.10:27450", buf, bufSize,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | (code ? ImGuiInputTextFlags_CharsUppercase : 0));
        ImGui::Spacing();
        ImGui::BeginDisabled(buf[0] == '\0');
        if (accentButton(code ? "Join" : "Connect", {full, 40.0f * s}) || (enter && buf[0] != '\0')) {
            if (code) {
                startJoin(buf);
            } else if (const auto addr = net::Address::parse(buf, net::kGamePort)) {
                startConnect(*addr);
            } else {
                status_ = "bad address";
            }
        }
        ImGui::EndDisabled();
        break;
    }
    case Screen::Online:
    case Screen::Lan:
        drawBrowser();
        break;
    case Screen::Joining:
    case Screen::Connecting:
        spinner(full, s, now_);
        centeredText(status_.empty() ? "connecting" : status_.c_str(), kText);
        ImGui::Spacing();
        if (ImGui::Button("Cancel", {full, 36.0f * s}))
            leaveGame("cancelled");
        break;
    default:
        break;
    }
}

void App::drawBrowser() {
    const float s = uiScale();
    const bool online = screen_ == Screen::Online;
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextUnformatted("Server browser");
    ImGui::PopFont();
    if (toggleButton(ICON_FA_GLOBE "  Online", online, {120.0f * s, 0.0f}) && !online)
        setScreen(Screen::Online);
    ImGui::SameLine();
    if (toggleButton(ICON_FA_WIFI "  LAN", !online, {120.0f * s, 0.0f}) && online) {
        lobby_.close();
        setScreen(Screen::Lan);
    }
    ImGui::SameLine();
    const bool loading = online && lobby_.state() == net::MasterClient::State::Connecting;
    ImGui::TextDisabled("%s", online ? (loading ? "refreshing..." : "games on the master server") : "searching the local network...");
    ImGui::Spacing();

    int rows = 0;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("games", 5, flags)) {
        ImGui::TableSetupColumn(online ? "Code" : "Address", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        auto row = [&](const std::string& id, const std::string& name, const std::string& mapName, int players, int maxPlayers) {
            ImGui::TableNextRow(0, 36.0f * s);
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
            ImGui::Text("%d/%d", players, maxPlayers);
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(rows++);
            const bool join = accentButton("Join", {70.0f * s, 0.0f});
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
        centeredText(failed ? lobby_.error().c_str() : "No games yet - host one with Host Multiplayer", failed ? kBad : kDim);
    }
}

void App::drawSettings() {
    const float s = uiScale();
    const float labelW = ImGui::GetContentRegionAvail().x * 0.55f;
    if (!ImGui::BeginTabBar("settings"))
        return;
    if (ImGui::BeginTabItem("Graphics")) {
#if !defined(SDL_PLATFORM_ANDROID)
        static constexpr const char* kWindowModes[] = {"Windowed", "Borderless fullscreen", "Fullscreen"};
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("Display mode", &windowMode_, kWindowModes, 3)) {
            applyWindow();
            saveSettings();
        }
        if (windowMode_ != 1) {
            std::vector<std::pair<int, int>> sizes;
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
            if (windowMode_ == 2) {
                int count = 0;
                if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count)) {
                    for (int i = 0; i < count; ++i)
                        sizes.emplace_back(modes[i]->w, modes[i]->h);
                    SDL_free(modes);
                }
            } else {
                SDL_Rect bounds{};
                SDL_GetDisplayUsableBounds(display, &bounds);
                for (const auto& [w, h] : {std::pair{1280, 720}, std::pair{1366, 768}, std::pair{1600, 900}, std::pair{1920, 1080},
                                           std::pair{2560, 1440}, std::pair{3840, 2160}})
                    if (w <= bounds.w && h <= bounds.h)
                        sizes.emplace_back(w, h);
            }
            std::sort(sizes.begin(), sizes.end(), std::greater<>());
            sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            int curW = resWidth_;
            int curH = resHeight_;
            if (curW <= 0)
                SDL_GetWindowSize(window_, &curW, &curH);
            char current[32];
            std::snprintf(current, sizeof(current), "%d x %d", curW, curH);
            ImGui::SetNextItemWidth(labelW);
            if (ImGui::BeginCombo("Resolution", current)) {
                for (const auto& [w, h] : sizes) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "%d x %d", w, h);
                    if (ImGui::Selectable(label, w == curW && h == curH)) {
                        resWidth_ = w;
                        resHeight_ = h;
                        applyWindow();
                        saveSettings();
                    }
                }
                ImGui::EndCombo();
            }
        }
#else
        ImGui::SetNextItemWidth(labelW);
        ImGui::SliderInt("Render resolution", &renderScale_, 25, 100, "%d%%");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            applyVideo();
            saveSettings();
        }
#endif
        if (ImGui::Checkbox("Vertical sync", &vsync_)) {
            applyVideo();
            saveSettings();
        }
        static constexpr int kFpsValues[] = {0, 30, 60, 90, 120, 144, 165, 240};
        static constexpr const char* kFpsNames[] = {"Unlimited", "30", "60", "90", "120", "144", "165", "240"};
        int fpsIndex = 0;
        for (int i = 0; i < 8; ++i)
            if (kFpsValues[i] == fpsLimit_)
                fpsIndex = i;
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("FPS limit", &fpsIndex, kFpsNames, 8)) {
            fpsLimit_ = kFpsValues[fpsIndex];
            saveSettings();
        }
        ImGui::SetItemTooltip("Caps the frame rate (on top of vertical sync). Saves power and heat on laptops and phones.");
        if (ImGui::Checkbox("Limit to 30 FPS when the window is in the background", &limitBackground_))
            saveSettings();
        static constexpr const char* kAa[] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA"};
        int aa = msaa_ >= 8 ? 3 : msaa_ >= 4 ? 2 : msaa_ >= 2 ? 1 : 0;
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("Anti-aliasing", &aa, kAa, 4)) {
            msaa_ = aa == 0 ? 0 : 1 << aa;
            applyVideo();
            saveSettings();
        }
        static constexpr const char* kShadows[] = {"Off", "Low", "Medium", "High"};
        static constexpr const char* kLights[] = {"Day", "Dusk", "Night"};
        ImGui::BeginDisabled(!renderer_.shadowsSupported());
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("Shadows", &shadowQuality_, kShadows, 4)) {
            applyGraphics();
            saveSettings();
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("Lighting", &lightPreset_, kLights, 3)) {
            applyGraphics();
            saveSettings();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Interface")) {
        static constexpr const char* kLayouts[] = {"Auto", "Desktop (mouse)", "Touch"};
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::Combo("Layout", &layout_, kLayouts, 3))
            saveSettings();
        ImGui::SetItemTooltip("Auto picks the touch layout on phones and the desktop layout with a mouse");
        int percent = static_cast<int>(std::lround(uiScaleUser_ * 100.0f));
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::SliderInt("Interface scale", &percent, 60, 200, "%d%%"))
            uiScaleUser_ = static_cast<float>(percent) / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveSettings();
        if (ImGui::Checkbox("Performance stats (F3)", &showStats_))
            saveSettings();
        ImGui::EndTabItem();
    }
    if (screen_ != Screen::Game && screen_ != Screen::Lobby && ImGui::BeginTabItem("Network")) {
        ImGui::SetNextItemWidth(labelW);
        if (ImGui::InputText("Master server", masterBuf_.data(), masterBuf_.size()))
            options_.master = masterBuf_.data();
        if (ImGui::IsItemDeactivatedAfterEdit())
            saveSettings();
        ImGui::TextDisabled("Lobby and NAT relay server used for room codes");
        ImGui::Spacing();
        if (ImGui::Checkbox("UPnP port mapping", &upnp_)) {
            saveSettings();
            if (upnp_)
                startUpnp();
            else
                mapper_.stop();
        }
        ImGui::SetItemTooltip("Asks the router to open UDP ports so players can connect directly instead of through the relay");
        if (upnp_) {
            ImGui::SameLine();
            const auto st = mapper_.state();
            ImGui::TextColored(toVec(st == net::PortMapper::State::Ready ? kGood : st == net::PortMapper::State::Discovering ? kDim : kBad), "%s",
                               mapper_.status().c_str());
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::Dummy({0.0f, 4.0f * s});
}

void App::drawLobby() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor({0.0f, 0.0f}, display, IM_COL32(10, 14, 26, 255), IM_COL32(18, 10, 28, 255),
                                                            IM_COL32(4, 4, 8, 255), IM_COL32(4, 6, 12, 255));
    const auto& info = net_.lobby();
    const bool leader = info.leader == net_.player();
    const bool multiplayer = info.maxPlayers > 1;
    const float pad = 16.0f * s;
    ImGui::SetNextWindowPos({pad, pad});
    ImGui::SetNextWindowSize({display.x - pad * 2.0f, display.y - pad * 2.0f});
    ImGui::Begin("##lobby", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(multiplayer ? info.serverName.c_str() : "Single player");
    ImGui::PopFont();
    if (host_ && multiplayer)
        ImGui::SameLine();
    if (host_ && lobby_.state() == net::MasterClient::State::Hosting) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * s);
        ImGui::TextDisabled("room code");
        ImGui::SameLine();
        ImGui::TextColored(toVec(kAccent), "%s", lobby_.code().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
            SDL_SetClipboardText(lobby_.code().c_str());
    } else if (host_ && multiplayer && lobby_.state() == net::MasterClient::State::Failed) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * s);
        ImGui::TextColored(toVec(kBad), "online unavailable: %s (LAN and IP still work)", lobby_.error().c_str());
    } else if (host_ && multiplayer) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * s);
        ImGui::TextDisabled("registering room...");
    }
    if (host_ && multiplayer && upnp_) {
        ImGui::SameLine();
        if (const auto ext = mapper_.external(options_.port))
            ImGui::TextColored(toVec(kGood), "  UPnP: direct %s:%u", mapper_.externalIp().c_str(), static_cast<unsigned>(*ext));
        else
            ImGui::TextDisabled("  UPnP: %s", mapper_.status().c_str());
    }
    ImGui::Separator();

    const float footer = 52.0f * s;
    const float bodyH = ImGui::GetContentRegionAvail().y - footer;
    const float leftW = ImGui::GetContentRegionAvail().x * 0.46f;

    ImGui::BeginChild("players", {leftW, bodyH}, ImGuiChildFlags_Borders);
    const bool skirmish = info.mode == static_cast<std::uint8_t>(GameMode::Skirmish);
    ImGui::TextDisabled("PLAYERS  %d / %d", static_cast<int>(info.players.size()), info.maxPlayers);
    ImGui::SameLine();
    {
        std::array<int, net::kLobbyTeams> perTeam{};
        for (const auto& p : info.players)
            ++perTeam[std::min<std::size_t>(p.team, perTeam.size() - 1)];
        const int sides = static_cast<int>(std::count_if(perTeam.begin(), perTeam.end(), [](int n) { return n > 0; }));
        std::string summary;
        if (!skirmish)
            summary = "co-op: everyone fights the Void together";
        else if (sides <= 1)
            summary = "free-for-all: everyone fights everyone";
        else
            for (int t = 0; t < net::kLobbyTeams; ++t)
                if (perTeam[static_cast<std::size_t>(t)] > 0)
                    summary += (summary.empty() ? "teams: " : " vs ") + std::to_string(perTeam[static_cast<std::size_t>(t)]);
        ImGui::TextColored(toVec(kAccent), "  %s", summary.c_str());
    }
    const float rosterH = multiplayer ? std::max(150.0f * s, bodyH * 0.55f) : ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.0f;
    ImGui::BeginChild("roster", {0.0f, rosterH});
    if (ImGui::BeginTable("##roster", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, 86.0f * s);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 96.0f * s);
        ImGui::TableHeadersRow();
        for (const auto& p : info.players) {
            ImGui::PushID(p.id);
            ImGui::TableNextRow(0, ImGui::GetFrameHeight() + 4.0f * s);
            const bool me = p.id == net_.player();
            if (me && !setupSent_) {
                setupSent_ = true;
                if (preferredColor_ >= 0 && preferredColor_ != p.color)
                    net_.sendPlayerSetup(p.id, static_cast<std::uint8_t>(preferredColor_), p.team);
            }
            const bool editable = me || leader;
            ImGui::TableNextColumn();
            bool picked = lobbySelected_.contains(p.id);
            if (ImGui::Checkbox("##pick", &picked)) {
                if (picked)
                    lobbySelected_.insert(p.id);
                else
                    lobbySelected_.erase(p.id);
            }
            ImGui::SetItemTooltip("Select for group actions");

            ImGui::TableNextColumn();
            {
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetFrameHeight();
                ImGui::GetWindowDrawList()->AddRectFilled({a.x, a.y + 2.0f}, {a.x + 5.0f * s, a.y + h - 2.0f},
                                                          ImGui::ColorConvertFloat4ToU32(colorVec(p.color, 1.0f)), 2.0f);
                ImGui::Dummy({8.0f * s, h});
                ImGui::SameLine();
            }
            if (p.bot) {
                ImGui::TextColored(toVec(kAccent), ICON_FA_COGS);
                ImGui::SameLine();
            } else if (p.id == info.leader) {
                ImGui::TextColored(toVec(kEnergy), ICON_FA_STAR);
                ImGui::SetItemTooltip("Host: changes the settings and starts the match");
                ImGui::SameLine();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toVec(me ? kGood : kText), "%s%s", p.name.c_str(), me ? "  (you)" : "");

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!editable);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##color", kColorNames[p.color % std::size(kColorNames)])) {
                for (std::size_t i = 0; i < net::kPlayerColors.size(); ++i) {
                    const bool taken = std::any_of(info.players.begin(), info.players.end(),
                                                   [&](const auto& o) { return o.id != p.id && o.color == i; });
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::ColorButton("##c", colorVec(i, taken ? 0.3f : 1.0f), ImGuiColorEditFlags_NoTooltip,
                                       {ImGui::GetFontSize(), ImGui::GetFontSize()});
                    ImGui::SameLine();
                    if (ImGui::Selectable(kColorNames[i], p.color == i, taken ? ImGuiSelectableFlags_Disabled : 0)) {
                        if (me) {
                            preferredColor_ = static_cast<int>(i);
                            saveSettings();
                        }
                        net_.sendPlayerSetup(p.id, static_cast<std::uint8_t>(i), p.team);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            char teamLabel[16];
            std::snprintf(teamLabel, sizeof(teamLabel), "Team %d", p.team + 1);
            if (ImGui::BeginCombo("##team", teamLabel)) {
                for (int t = 0; t < net::kLobbyTeams; ++t) {
                    char label[16];
                    std::snprintf(label, sizeof(label), "Team %d", t + 1);
                    if (ImGui::Selectable(label, p.team == t))
                        net_.sendPlayerSetup(p.id, p.color, static_cast<std::uint8_t>(t));
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (p.bot) {
                ImGui::TextDisabled("computer");
                if (leader) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_TIMES "##rm"))
                        net_.sendBot(false, p.id);
                    ImGui::SetItemTooltip("Remove this AI player");
                }
            } else if (p.id == info.leader) {
                ImGui::TextColored(toVec(kEnergy), "host");
            } else {
                ImGui::TextColored(toVec(p.ready ? kGood : kBad), p.ready ? ICON_FA_CHECK " ready" : ICON_FA_TIMES " not ready");
            }
            ImGui::PopID();
        }
        const int open = info.maxPlayers - static_cast<int>(info.players.size());
        if (open > 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(ICON_FA_USERS "  %d open slot%s", open, open == 1 ? "" : "s");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (leader && skirmish) {
        ImGui::BeginDisabled(static_cast<int>(info.players.size()) >= info.maxPlayers);
        if (ImGui::Button(ICON_FA_PLUS "  Add AI opponent"))
            net_.sendBot(true);
        ImGui::SameLine();
        if (ImGui::Button("Fill with AI"))
            for (int i = static_cast<int>(info.players.size()); i < info.maxPlayers; ++i)
                net_.sendBot(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        ImGui::SetItemTooltip("Put everyone on the same team for free-for-all, or split players into teams.");
    }
    std::erase_if(lobbySelected_, [&](std::uint8_t id) {
        return std::none_of(info.players.begin(), info.players.end(), [&](const auto& p) { return p.id == id; });
    });
    if (!lobbySelected_.empty()) {
        ImGui::TextDisabled("%d selected:", static_cast<int>(lobbySelected_.size()));
        auto mayEdit = [&](std::uint8_t id) { return leader || id == net_.player(); };
        for (int t = 0; t < net::kLobbyTeams; ++t) {
            ImGui::SameLine();
            char label[16];
            std::snprintf(label, sizeof(label), "Team %d", t + 1);
            if (ImGui::SmallButton(label))
                for (const auto& p : info.players)
                    if (lobbySelected_.contains(p.id) && mayEdit(p.id))
                        net_.sendPlayerSetup(p.id, p.color, static_cast<std::uint8_t>(t));
        }
        if (leader) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec(kBad));
            if (ImGui::SmallButton(ICON_FA_USER_TIMES " Kick"))
                for (const auto id : lobbySelected_)
                    if (id != net_.player())
                        net_.sendKick(id);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
            lobbySelected_.clear();
    }
    if (multiplayer) {
        ImGui::Separator();
        ImGui::TextDisabled("CHAT");
        const float inputH = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("log", {0.0f, std::max(60.0f * s, ImGui::GetContentRegionAvail().y - inputH)});
        for (const auto& entry : chatLog_) {
            ImGui::TextColored(toVec(kAccent), "%s:", entry.line.name.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", entry.line.text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##chat", "say something...", chatBuf_.data(), chatBuf_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (chatBuf_[0] != '\0')
                net_.sendChat(chatBuf_.data());
            chatBuf_[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("settings", {0.0f, bodyH}, ImGuiChildFlags_Borders);
    auto send = [&](const std::string& map, int mode, int difficulty, int maxPlayers) {
        net_.sendSettings(map, static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(difficulty), static_cast<std::uint8_t>(maxPlayers));
        options_.map = map;
        options_.mode = static_cast<GameMode>(std::clamp(mode, 0, 3));
        options_.difficulty = static_cast<Difficulty>(difficulty);
        saveSettings();
    };
    if (!leader)
        ImGui::TextDisabled("Only the host can change the settings");
    {
        const auto it = std::find_if(maps_.begin(), maps_.end(), [&](const auto& m) {
            return m.id == info.map || (m.id == "random" && info.map.rfind("random_", 0) == 0);
        });
        const float size = std::min(ImGui::GetContentRegionAvail().x * 0.42f, 220.0f * s);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (const auto* preview = mapPreview(info.map)) {
            const float scale = size / static_cast<float>(std::max(preview->width, preview->height));
            const ImVec2 o{a.x + (size - preview->width * scale) * 0.5f, a.y + (size - preview->height * scale) * 0.5f};
            dl->AddImage(static_cast<ImTextureID>(renderer_.textureHandle(preview->texture).idx) + 1, o,
                         {o.x + preview->width * scale, o.y + preview->height * scale});
            for (const auto& pt : preview->points)
                dl->AddCircleFilled({o.x + pt.x * scale, o.y + pt.y * scale}, 2.5f * s, IM_COL32(250, 210, 40, 255));
            for (std::size_t i = 0; i < preview->slots.size(); ++i) {
                const ImVec2 c{o.x + preview->slots[i].x * scale, o.y + preview->slots[i].y * scale};
                const bool taken = i < info.players.size();
                const ImU32 col = taken ? ImGui::ColorConvertFloat4ToU32(colorVec(info.players[i].color, 1.0f)) : IM_COL32(230, 230, 230, 200);
                dl->AddCircleFilled(c, 6.0f * s, IM_COL32(10, 10, 14, 220));
                dl->AddCircle(c, 6.0f * s, col, 0, 2.0f * s);
                char n[4];
                std::snprintf(n, sizeof(n), "%d", static_cast<int>(i + 1));
                const ImVec2 ts = ImGui::CalcTextSize(n);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.75f, {c.x - ts.x * 0.375f, c.y - ts.y * 0.375f}, col, n);
            }
        } else {
            dl->AddRectFilled(a, {a.x + size, a.y + size}, IM_COL32(20, 22, 30, 255));
        }
        dl->AddRect(a, {a.x + size, a.y + size}, IM_COL32(90, 100, 120, 255));
        ImGui::Dummy({size, size});
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextUnformatted(it != maps_.end() ? it->name.c_str() : info.map.c_str());
        ImGui::PopFont();
        if (it != maps_.end()) {
            ImGui::TextColored(toVec(kAccent), "%s players", it->players.c_str());
            ImGui::TextDisabled("%s", it->description.c_str());
        }
        ImGui::Spacing();
        if (info.mode < modes_.size()) {
            ImGui::Text("%s  -  %s", modes_[info.mode].name.c_str(), kDifficultyNames[std::min<int>(info.difficulty, 3)]);
            ImGui::TextDisabled("%s", modes_[info.mode].description.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Separator();
    }
    ImGui::BeginDisabled(!leader);
    ImGui::TextDisabled("MAP");
    for (const auto& m : maps_) {
        ImGui::PushID(m.id.c_str());
        const bool random = m.id == "random";
        const bool chosen = random ? info.map.rfind("random_", 0) == 0 : info.map == m.id;
        const std::string title = random && chosen ? m.name + "  #" + info.map.substr(7) : m.name;
        if (cardSelectable("##map", chosen, title, m.players + " players", random && chosen ? m.description + " Click again for a new one." : m.description, s) &&
            (random || !chosen))
            send(m.id, info.mode, info.difficulty, info.maxPlayers);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("MODE");
    for (std::size_t i = 0; i < modes_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i) + 100);
        if (cardSelectable("##mode", info.mode == i, modes_[i].name, "", modes_[i].description, s) && info.mode != i)
            send(info.map, static_cast<int>(i), info.difficulty, info.maxPlayers);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("DIFFICULTY");
    const float seg = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (toggleButton(kDifficultyNames[i], info.difficulty == i, {seg, 0.0f}) && info.difficulty != i)
            send(info.map, info.mode, i, info.maxPlayers);
    }
    ImGui::Spacing();
    bool cheats = info.cheats;
    if (ImGui::Checkbox("Cheats (debug menu F9 in game)", &cheats))
        net_.sendCheats(cheats);
    if (multiplayer && host_) {
        ImGui::Spacing();
        int maxPlayers = info.maxPlayers;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::SliderInt("Max players", &maxPlayers, 2, 16);
        if (ImGui::IsItemDeactivatedAfterEdit())
            send(info.map, info.mode, info.difficulty, maxPlayers);
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_CHEVRON_LEFT "  Leave", {130.0f * s, 40.0f * s}))
        leaveGame("");
    const float buttonW = 220.0f * s;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonW);
    if (leader) {
        int ready = 0;
        for (const auto& p : info.players)
            ready += p.id != info.leader && p.ready;
        if (multiplayer && info.players.size() > 1) {
            char text[32];
            std::snprintf(text, sizeof(text), "%d / %d ready", ready, static_cast<int>(info.players.size()) - 1);
            const float tw = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - tw - 14.0f * s);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * s);
            ImGui::TextDisabled("%s", text);
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10.0f * s);
        }
        if (accentButton(ICON_FA_PLAY "  Start match", {buttonW, 40.0f * s}))
            net_.sendStart();
        if (host_) {
            if (now_ - savesListed_ > 2.0) {
                saves_ = listJson(savesDir());
                savesListed_ = now_;
            }
            if (!saves_.empty()) {
                saveChoice_ = std::clamp(saveChoice_, 0, static_cast<int>(saves_.size()) - 1);
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonW * 2.0f - 8.0f * s);
                ImGui::SetNextItemWidth(buttonW);
                if (ImGui::BeginCombo("##saves", saves_[static_cast<std::size_t>(saveChoice_)].c_str())) {
                    for (int i = 0; i < static_cast<int>(saves_.size()); ++i)
                        if (ImGui::Selectable(saves_[static_cast<std::size_t>(i)].c_str(), i == saveChoice_))
                            saveChoice_ = i;
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Load game", {buttonW, 0.0f}))
                    net_.sendLoadGame(saves_[static_cast<std::size_t>(saveChoice_)]);
            }
        }
    } else {
        bool ready = false;
        for (const auto& p : info.players)
            if (p.id == net_.player())
                ready = p.ready;
        if (accentButton(ready ? ICON_FA_TIMES "  Not ready" : ICON_FA_FLAG "  Ready", {buttonW, 40.0f * s}, ready ? kDim : kGood))
            net_.sendReady(!ready);
    }
    ImGui::End();
}

}
