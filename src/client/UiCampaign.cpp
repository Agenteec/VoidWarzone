#include "App.h"

#include <set>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "ui/Widgets.h"

namespace vw::client {

using nlohmann::json;
using namespace ui;

namespace {

std::string progressPath() {
    return prefDir() + "campaign.json";
}

}

void App::loadCampaign() {
    campaign_.clear();
    try {
        const json doc = json::parse(readData("data/campaign/campaign.json"));
        for (const auto& ch : doc.at("chapters"))
            for (const auto& m : ch.at("missions"))
                campaign_.push_back({m.at("id").get<std::string>(), m.at("map").get<std::string>(), m.value("name", ""), m.value("place", ""),
                                     ch.value("name", ""), ch.value("intro", ""), m.value("kind", "base") == "sortie", m.value("transport", 8)});
    } catch (const std::exception& e) {
        spdlog::warn("campaign: {}", e.what());
    }
    campaignDone_.clear();
    campaignRoster_.clear();
    campaignResearch_.clear();
    campaignBase_.clear();
    campaignEnergy_ = -1.0f;
    try {
        std::ifstream f(progressPath(), std::ios::binary);
        if (f) {
            const json p = json::parse(f);
            campaignDone_ = p.value("completed", std::vector<std::string>{});
            campaignRoster_ = p.value("roster", std::string{});
            campaignResearch_ = p.value("research", std::string{});
            campaignBase_ = p.value("base", std::string{});
            campaignEnergy_ = p.value("energy", -1.0f);
            if (campaignBase_.empty() && !campaignRoster_.empty())
                campaignBase_ = campaignRoster_;
        }
    } catch (const std::exception& e) {
        spdlog::warn("campaign progress: {}", e.what());
    }
}

void App::saveCampaign() {
    const json p = {{"completed", campaignDone_}, {"research", campaignResearch_}, {"base", campaignBase_}, {"energy", campaignEnergy_}};
    std::ofstream f(progressPath(), std::ios::binary);
    f << p.dump(1);
}

bool App::campaignUnlocked(std::size_t index) const {
    if (index == 0)
        return true;
    return std::find(campaignDone_.begin(), campaignDone_.end(), campaign_[index - 1].id) != campaignDone_.end();
}

int App::rosterSize() const {
    return static_cast<int>(baseUnitIndices().size());
}

std::vector<int> App::baseUnitIndices() const {
    std::vector<int> out;
    try {
        if (campaignBase_.empty())
            return out;
        const json base = json::parse(campaignBase_);
        for (std::size_t i = 0; i < base.size(); ++i) {
            const UnitDefId def = data_->find(base[i].value("def", ""));
            if (def != kInvalidUnitDef && !data_->unit(def).structure)
                out.push_back(static_cast<int>(i));
        }
    } catch (const std::exception&) {
    }
    return out;
}

void App::startMission(std::size_t index, bool coop) {
    if (index >= campaign_.size())
        return;
    campaignMission_ = campaign_[index].id;
    campaignRecorded_ = false;
    matchBase_.clear();
    matchEnergy_ = -1.0f;
    sortieSent_.clear();
    preSortieBase_ = campaignBase_;
    if (campaign_[index].sortie) {
        // the strike group: picked units from the base, placed at the sortie start
        try {
            const json base = campaignBase_.empty() ? json::array() : json::parse(campaignBase_);
            json team = json::array();
            for (const int i : sortieTeam_)
                if (i >= 0 && static_cast<std::size_t>(i) < base.size()) {
                    json u = base[static_cast<std::size_t>(i)];
                    u.erase("pos");
                    u.erase("tag");
                    team.push_back(std::move(u));
                    sortieSent_.push_back(i);
                }
            if (!team.empty())
                matchBase_ = team.dump();
        } catch (const std::exception& e) {
            spdlog::warn("sortie team: {}", e.what());
        }
    } else {
        matchBase_ = campaignBase_;
        matchEnergy_ = campaignEnergy_;
    }
    options_.map = campaign_[index].map;
    options_.mode = GameMode::Defense;
    autoStart_ = !coop;
    startHost(coop);
}

void App::recordMission(bool victory) {
    if (campaignMission_.empty() || campaignRecorded_)
        return;
    campaignRecorded_ = true;
    if (!victory || !host_ || !host_->world())
        return;
    if (std::find(campaignDone_.begin(), campaignDone_.end(), campaignMission_) == campaignDone_.end())
        campaignDone_.push_back(campaignMission_);
    storeCampaignBase();
}

void App::storeCampaignBase() {
    if (campaignMission_.empty() || !host_ || !host_->world() || host_->world()->defense().state != MatchState::Victory)
        return;
    const auto it = std::find_if(campaign_.begin(), campaign_.end(), [&](const auto& m) { return m.id == campaignMission_; });
    if (it == campaign_.end())
        return;
    const World& world = *host_->world();
    try {
        campaignResearch_ = world.exportResearch(net_.player());
        if (it->sortie) {
            // survivors come home: the base minus the units sent out, plus whoever made it back (placed by the HQ)
            json base = preSortieBase_.empty() ? json::array() : json::parse(preSortieBase_);
            json kept = json::array();
            for (std::size_t i = 0; i < base.size(); ++i)
                if (std::find(sortieSent_.begin(), sortieSent_.end(), static_cast<int>(i)) == sortieSent_.end())
                    kept.push_back(base[i]);
            for (auto u : json::parse(world.exportRoster(net_.player(), 64))) {
                u.erase("pos");
                kept.push_back(std::move(u));
            }
            campaignBase_ = kept.dump();
        } else {
            campaignBase_ = world.exportBase(net_.player());
            campaignEnergy_ = world.player(net_.player()).energy;
        }
    } catch (const std::exception& e) {
        spdlog::warn("campaign base export: {}", e.what());
    }
    saveCampaign();
    spdlog::info("mission {} recorded, base units {}", campaignMission_, rosterSize());
}

void App::drawCampaign() {
    const float s = uiScale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * 0.5f, display.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(display.x - 40.0f * s, 980.0f * s), std::min(display.y - 40.0f * s, 620.0f * s)});
    ImGui::Begin("##campaign", nullptr, kHudFlags & ~ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextColored(toVec(kAccent), ICON_FA_FLAG "  CAMPAIGN");
    ImGui::PopFont();
    ImGui::Separator();

    const float listW = 300.0f * s;
    ImGui::BeginChild("##missions", {listW, -50.0f * s}, ImGuiChildFlags_Borders);
    std::string chapter;
    for (std::size_t i = 0; i < campaign_.size(); ++i) {
        const auto& m = campaign_[i];
        if (m.chapter != chapter) {
            chapter = m.chapter;
            ImGui::PushFont(ui_.bold(), 0.0f);
            ImGui::TextUnformatted(chapter.c_str());
            ImGui::PopFont();
        }
        const bool done = std::find(campaignDone_.begin(), campaignDone_.end(), m.id) != campaignDone_.end();
        const bool open = campaignUnlocked(i);
        char label[96];
        std::snprintf(label, sizeof(label), "%s  %d. %s%s", done ? ICON_FA_CHECK : open ? ICON_FA_PLAY : ICON_FA_LOCK, static_cast<int>(i + 1), m.name.c_str(),
                      m.sortie ? "  " ICON_FA_TRUCK : "  " ICON_FA_HOME);
        ImGui::BeginDisabled(!open);
        if (ImGui::Selectable(label, campaignChoice_ == static_cast<int>(i), 0, {0.0f, 28.0f * s}))
            campaignChoice_ = static_cast<int>(i);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##brief", {0.0f, -50.0f * s}, ImGuiChildFlags_Borders);
    if (!campaign_.empty()) {
        campaignChoice_ = std::clamp(campaignChoice_, 0, static_cast<int>(campaign_.size()) - 1);
        const auto& m = campaign_[static_cast<std::size_t>(campaignChoice_)];
        ImGui::PushFont(ui_.bold(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted(m.name.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", m.place.c_str());
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        if (campaignChoice_ == 0 || campaign_[static_cast<std::size_t>(campaignChoice_) - 1].chapter != m.chapter) {
            ImGui::TextColored(toVec(kDim), "%s", m.intro.c_str());
            ImGui::Spacing();
        }
        try {
            const auto map = Map::parse(readMap(m.map));
            ImGui::TextUnformatted(map.mission().briefing.c_str());
            ImGui::Spacing();
            ImGui::PushFont(ui_.bold(), 0.0f);
            ImGui::TextUnformatted("Objectives");
            ImGui::PopFont();
            for (const auto& o : map.mission().objectives)
                if (!o.hidden)
                    ImGui::BulletText("%s", o.text.c_str());
            if (std::any_of(map.mission().objectives.begin(), map.mission().objectives.end(), [](const auto& o) { return o.hidden; }))
                ImGui::BulletText("...more objectives will appear during the mission");
        } catch (const std::exception& e) {
            ImGui::TextColored(toVec(kBad), "%s", e.what());
        }
        ImGui::Spacing();
        const auto units = baseUnitIndices();
        int structures = 0;
        json base = json::array();
        try {
            if (!campaignBase_.empty())
                base = json::parse(campaignBase_);
        } catch (const std::exception&) {
        }
        structures = static_cast<int>(base.size()) - static_cast<int>(units.size());
        if (!m.sortie) {
            if (!base.empty())
                ImGui::TextColored(toVec(kGood), ICON_FA_HOME "  Base mission: your base returns - %d buildings, %d vehicles", structures,
                                   static_cast<int>(units.size()));
            else
                ImGui::TextDisabled(ICON_FA_HOME "  Base mission: the base you build here stays for the next missions");
        } else {
            ImGui::TextColored(toVec(kAccent), ICON_FA_TRUCK "  Sortie: pick up to %d vehicles from the base", m.transport);
            if (sortieFor_ != campaignChoice_) {
                // default team: the most experienced units first
                sortieFor_ = campaignChoice_;
                sortieTeam_.clear();
                std::vector<int> sorted = units;
                auto score = [&](int i) {
                    const auto& u = base[static_cast<std::size_t>(i)];
                    const auto p = u.find("progression");
                    return p == u.end() ? 0.0f : static_cast<float>(p->value("rank", 0)) * 10000.0f + p->value("xp", 0.0f);
                };
                std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) { return score(a) > score(b); });
                for (const int i : sorted) {
                    const UnitDefId def = data_->find(base[static_cast<std::size_t>(i)].value("def", ""));
                    if (static_cast<int>(sortieTeam_.size()) < m.transport && def != kInvalidUnitDef && !data_->unit(def).builder &&
                        data_->unit(def).deploy == kInvalidUnitDef)
                        sortieTeam_.push_back(i);
                }
            }
            if (units.empty()) {
                ImGui::TextDisabled("No vehicles at the base yet - a default column will go.");
            } else {
                ImGui::TextDisabled("%d / %d selected. Units left behind stay at the base.", static_cast<int>(sortieTeam_.size()), m.transport);
                ImGui::BeginChild("##team", {0.0f, 150.0f * s}, ImGuiChildFlags_Borders);
                for (const int i : units) {
                    const auto& u = base[static_cast<std::size_t>(i)];
                    const UnitDefId def = data_->find(u.value("def", ""));
                    bool picked = std::find(sortieTeam_.begin(), sortieTeam_.end(), i) != sortieTeam_.end();
                    const auto p = u.find("progression");
                    static constexpr const char* kRanks[] = {"Recruit", "Veteran", "Elite", "Hero"};
                    const int rank = p == u.end() ? 0 : std::clamp(p->value("rank", 0), 0, 3);
                    char label[96];
                    std::snprintf(label, sizeof(label), "%s  (%s)##%d", data_->unit(def).name.c_str(), kRanks[rank], i);
                    ImGui::BeginDisabled(!picked && static_cast<int>(sortieTeam_.size()) >= m.transport);
                    if (ImGui::Checkbox(label, &picked)) {
                        if (picked)
                            sortieTeam_.push_back(i);
                        else
                            std::erase(sortieTeam_, i);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
            }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    if (ImGui::Button(ICON_FA_CHEVRON_LEFT "  Back", {130.0f * s, 40.0f * s}))
        setScreen(Screen::Menu);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_REFRESH "  Reset progress", {180.0f * s, 40.0f * s}))
        ImGui::OpenPopup("##reset");
    if (ImGui::BeginPopup("##reset")) {
        ImGui::TextUnformatted("Forget completed missions and veterans?");
        if (ImGui::Button("Reset")) {
            campaignDone_.clear();
            campaignRoster_.clear();
            campaignBase_.clear();
            campaignEnergy_ = -1.0f;
            sortieFor_ = -1;
            saveCampaign();
            campaignChoice_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    const float buttonW = 200.0f * s;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - buttonW * 2.0f - 8.0f * s);
    const bool open = !campaign_.empty() && campaignUnlocked(static_cast<std::size_t>(campaignChoice_));
    ImGui::BeginDisabled(!open);
    if (ImGui::Button(ICON_FA_USERS "  Host co-op", {buttonW, 40.0f * s}))
        startMission(static_cast<std::size_t>(campaignChoice_), true);
    ImGui::SameLine();
    if (accentButton(ICON_FA_PLAY "  Start mission", {buttonW, 40.0f * s}))
        startMission(static_cast<std::size_t>(campaignChoice_), false);
    ImGui::EndDisabled();
    trackWindow();
    ImGui::End();
}

bool App::drawObjectives() {
    if (const auto* latest = net_.latest(); latest && latest->game.mode == static_cast<std::uint8_t>(GameMode::Skirmish)) {
        ImGui::PushFont(ui_.bold(), 0.0f);
        ImGui::TextColored(toVec(kAccent), ICON_FA_FLAG "  SKIRMISH");
        ImGui::PopFont();
        ImGui::TextDisabled("%d sides left  -  destroy every enemy base", static_cast<int>(latest->game.sides));
        return true;
    }
    if (!map_ || !map_->mission().enabled)
        return false;
    const auto* latest = net_.latest();
    const auto& mission = map_->mission();
    ImGui::PushFont(ui_.bold(), 0.0f);
    ImGui::TextColored(toVec(kAccent), ICON_FA_FLAG "  %s", mission.title.empty() ? "MISSION" : mission.title.c_str());
    ImGui::PopFont();
    for (std::size_t i = 0; i < mission.objectives.size(); ++i) {
        const std::uint8_t state = latest && i < latest->game.objectives.size() ? latest->game.objectives[i] : (mission.objectives[i].hidden ? 0 : 1);
        if (state == 0)
            continue;
        const auto& o = mission.objectives[i];
        if (o.type == "survive" && o.optional && o.time > 9000.0f)
            continue;
        const char* icon = state == 2 ? ICON_FA_CHECK_SQUARE_O : state == 3 ? ICON_FA_TIMES : ICON_FA_SQUARE_O;
        const ImVec4 color = state == 2 ? toVec(kGood) : state == 3 ? toVec(kBad) : o.optional ? toVec(kDim) : toVec(kText);
        if (o.type == "survive" && state == 1) {
            const float left = std::max(0.0f, o.time - static_cast<float>(latest ? latest->tick : 0) / static_cast<float>(kTickRate));
            ImGui::TextColored(color, "%s  %s  %d:%02d", icon, o.text.c_str(), static_cast<int>(left) / 60, static_cast<int>(left) % 60);
        } else {
            ImGui::TextColored(color, "%s  %s", icon, o.text.c_str());
        }
    }
    return true;
}

}
