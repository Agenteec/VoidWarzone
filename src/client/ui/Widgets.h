#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <icons_font_awesome.h>

#include "core/GameData.h"

namespace vw::client::ui {

inline constexpr ImU32 kAccent = IM_COL32(61, 184, 255, 255);
inline constexpr ImU32 kVoid = IM_COL32(255, 64, 140, 255);
inline constexpr ImU32 kEnergy = IM_COL32(255, 210, 80, 255);
inline constexpr ImU32 kShard = IM_COL32(190, 130, 255, 255);
inline constexpr ImU32 kGood = IM_COL32(90, 230, 120, 255);
inline constexpr ImU32 kBad = IM_COL32(255, 80, 80, 255);
inline constexpr ImU32 kDim = IM_COL32(125, 135, 158, 255);
inline constexpr ImU32 kText = IM_COL32(220, 228, 240, 255);
inline constexpr ImGuiWindowFlags kHudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
inline constexpr const char* kRankNames[] = {"Recruit", "Veteran", "Elite", "Hero"};
inline constexpr ImU32 kRankColors[] = {IM_COL32(170, 178, 195, 255), IM_COL32(205, 215, 235, 255), IM_COL32(170, 120, 255, 255),
                                        IM_COL32(255, 205, 70, 255)};
inline constexpr ImU32 kRarityColors[] = {IM_COL32(210, 215, 225, 255), IM_COL32(80, 160, 255, 255), IM_COL32(190, 110, 255, 255),
                                          IM_COL32(255, 170, 40, 255)};
inline constexpr const char* kRarityNames[] = {"Common", "Rare", "Epic", "Legendary"};
inline constexpr const char* kSkillKeys[] = {"Q", "W", "E", "R"};
inline constexpr const char* kBuildKeyNames[] = {"D", "F", "G", "J", "K"};
inline constexpr const char* kRecruitKeyNames[] = {"Z", "X", "C", "V", "N"};

inline ImVec4 toVec(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

inline ImU32 scaleColor(ImU32 c, float k, float alpha = 1.0f) {
    const ImVec4 v = toVec(c);
    return ImGui::ColorConvertFloat4ToU32({v.x * k, v.y * k, v.z * k, alpha});
}

inline void bar(float fraction, ImVec2 size, ImU32 fill, const char* label) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = size.y * 0.3f;
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, IM_COL32(8, 10, 16, 230), r);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        dl->AddRectFilled(p, {p.x + size.x * f, p.y + size.y}, fill, r);
    dl->AddRect(p, {p.x + size.x, p.y + size.y}, IM_COL32(255, 255, 255, 24), r);
    if (label && *label) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        if (ts.y <= size.y + 2.0f) {
            const ImVec2 tp{p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f};
            dl->AddText({tp.x + 1.0f, tp.y + 1.0f}, IM_COL32(0, 0, 0, 200), label);
            dl->AddText(tp, IM_COL32(255, 255, 255, 240), label);
        }
    }
    ImGui::Dummy(size);
}

inline ImU32 hpColor(float frac) {
    const float t = std::clamp(frac, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32({1.0f + (0.3f - 1.0f) * t, 0.25f + (0.9f - 0.25f) * t, 0.2f + (0.4f - 0.2f) * t, 1.0f});
}

inline bool accentButton(const char* label, ImVec2 size, ImU32 color = kAccent) {
    const ImVec4 c = toVec(color);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{c.x * 0.5f, c.y * 0.5f, c.z * 0.5f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{c.x * 0.7f, c.y * 0.7f, c.z * 0.7f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

// Draws `text` centred in the box [a, b]: one line if it fits, otherwise two lines and/or a smaller font, an ellipsis as the last resort.
inline void fitText(ImDrawList* dl, std::string_view text, ImVec2 a, ImVec2 b, ImU32 color, float maxScale = 1.0f) {
    ImFont* font = ImGui::GetFont();
    const float base = ImGui::GetFontSize() * maxScale;
    const float w = b.x - a.x - 4.0f;
    const float h = b.y - a.y;
    const std::string s(text);
    auto width = [&](const std::string& t, float size) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str()).x; };
    std::vector<std::string> lines{s};
    float size = base;
    for (float k = 1.0f; k >= 0.62f; k -= 0.06f) {
        size = base * k;
        if (width(s, size) <= w) {
            lines = {s};
            break;
        }
        // best two-line split at a space
        std::string bestA, bestB;
        float best = 1e9f;
        for (std::size_t i = s.find(' '); i != std::string::npos; i = s.find(' ', i + 1)) {
            const std::string l = s.substr(0, i);
            const std::string r = s.substr(i + 1);
            const float m = std::max(width(l, size), width(r, size));
            if (m < best) {
                best = m;
                bestA = l;
                bestB = r;
            }
        }
        if (best <= w && size * 2.1f <= h) {
            lines = {bestA, bestB};
            break;
        }
        lines = {s};
    }
    if (lines.size() == 1 && width(lines[0], size) > w) {
        std::string t = lines[0];
        while (t.size() > 1 && width(t + "..", size) > w) {
            // drop one whole UTF-8 code point: its continuation bytes, then the lead byte
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80)
                t.pop_back();
            if (!t.empty())
                t.pop_back();
        }
        lines = {t + ".."};
    }
    const float total = size * static_cast<float>(lines.size());
    float y = a.y + (h - total) * 0.5f;
    for (const auto& l : lines) {
        dl->AddText(font, size, {a.x + (b.x - a.x - width(l, size)) * 0.5f, y}, color, l.c_str());
        y += size;
    }
}

inline bool toggleButton(const char* label, bool active, ImVec2 size) {
    if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, toVec(IM_COL32(40, 110, 160, 255)));
    const bool pressed = ImGui::Button(label, size);
    if (active)
        ImGui::PopStyleColor();
    return pressed;
}

inline bool menuButton(const char* label, bool active, float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? toVec(IM_COL32(30, 70, 105, 200)) : ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec(IM_COL32(30, 70, 105, 140)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec(IM_COL32(40, 110, 160, 255)));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
    const bool pressed = ImGui::Button(label, {width, height});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (active) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(a, {a.x + 3.0f, b.y}, kAccent);
    }
    return pressed;
}

inline void centeredText(const char* text, ImU32 color) {
    const float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f + ImGui::GetCursorPosX()));
    ImGui::TextColored(toVec(color), "%s", text);
}

inline std::string effectText(const SkillEffect& fx, int level) {
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

inline void skillTooltip(const SkillDef& skill, int level) {
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::TextColored(toVec(kAccent), "%s", skill.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(skill.passive ? "passive" : "cooldown %.0fs", skill.cooldown);
    for (const auto& fx : skill.effects)
        ImGui::BulletText("%s", effectText(fx, std::max(level, 1)).c_str());
    ImGui::EndTooltip();
}

inline void hotkeyBadge(const char* key) {
    if (!key || !*key)
        return;
    const ImVec2 a = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText({a.x + 4.0f, a.y + 2.0f}, kEnergy, key);
}

inline int utf8Encode(char32_t c, char* out) {
    if (c < 0x80) {
        out[0] = static_cast<char>(c);
        return 1;
    }
    if (c < 0x800) {
        out[0] = static_cast<char>(0xC0 | (c >> 6));
        out[1] = static_cast<char>(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (c >> 12));
        out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (c >> 18));
    out[1] = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (c & 0x3F));
    return 4;
}

inline constexpr char32_t kChatEmoji[] = {0x1F600, 0x1F602, 0x1F609, 0x1F60E, 0x1F914, 0x1F62E, 0x1F622, 0x1F621, 0x1F608, 0x1F480,
                                          0x1F44D, 0x1F44E, 0x1F44B, 0x1F44F, 0x1F64F, 0x1F4AA, 0x1F91D, 0x1F440, 0x2764,  0x2B50,
                                          0x1F525, 0x1F4A5, 0x1F4A3, 0x26A1,  0x2744,  0x1F3AF, 0x1F680, 0x1F681, 0x2708,  0x1F6E1,
                                          0x2694,  0x1F3C6, 0x26A0,  0x2705,  0x274C,  0x2753,  0x1F6A9, 0x1F4E2, 0x1F527, 0x1F319};

// keeps the caret at the end after an emoji was appended (InputText callback, UserData = bool*)
inline int caretToEnd(ImGuiInputTextCallbackData* d) {
    auto* pending = static_cast<bool*>(d->UserData);
    if (pending && *pending) {
        d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
        *pending = false;
    }
    return 0;
}

// emoji button + popup grid appending to buf; `open` tracks the popup, returns true the frame after it closed
inline bool emojiPicker(char* buf, std::size_t size, bool& open) {
    char label[16];
    const int n = utf8Encode(0x1F600, label);
    std::snprintf(label + n, sizeof(label) - n, "##emoji");
    if (ImGui::Button(label))
        ImGui::OpenPopup("##emojis");
    const bool was = open;
    open = false;
    if (ImGui::BeginPopup("##emojis")) {
        open = true;
        const float cell = ImGui::GetFrameHeight() * 1.25f;
        for (int i = 0; i < static_cast<int>(std::size(kChatEmoji)); ++i) {
            if (i % 10)
                ImGui::SameLine();
            char e[8] = {};
            const int len = utf8Encode(kChatEmoji[i], e);
            ImGui::PushID(i);
            if (ImGui::Button(e, {cell, cell})) {
                const std::size_t have = std::strlen(buf);
                if (have + len < size)
                    std::memcpy(buf + have, e, static_cast<std::size_t>(len) + 1);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return was && !open;
}

}
