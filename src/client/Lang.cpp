#include "Lang.h"

#include <mutex>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace vw::client {

namespace {

std::string g_code = "en";
std::unordered_map<std::string, std::string> g_dict;
std::unordered_map<std::string, std::string> g_cache;
std::mutex g_mutex;

// Font Awesome 4 icons live in U+F000..U+F2FF: UTF-8 EF 80..8B xx
std::size_t iconPrefix(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == ' ') {
            ++i;
        } else if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0x80 && static_cast<unsigned char>(s[i + 1]) <= 0x8B) {
            i += 3;
        } else {
            break;
        }
    }
    return i;
}

std::string translate(std::string_view text) {
    const std::size_t pre = iconPrefix(text);
    std::size_t idPos = text.find("##", pre);
    if (idPos == std::string_view::npos)
        idPos = text.size();
    std::string_view core = text.substr(pre, idPos - pre);
    std::size_t trail = core.size();
    while (trail > 0 && core[trail - 1] == ' ')
        --trail;
    const std::string key(core.substr(0, trail));
    const auto it = g_dict.find(key);
    if (it == g_dict.end())
        return std::string(text);
    std::string out(text.substr(0, pre));
    out += it->second;
    out += core.substr(trail);
    out += text.substr(idPos);
    return out;
}

}

void setDictionary(std::string_view code, std::string_view json) {
    std::lock_guard lock(g_mutex);
    g_code = code.empty() ? "en" : std::string(code);
    g_dict.clear();
    g_cache.clear();
    if (json.empty())
        return;
    try {
        const auto doc = nlohmann::json::parse(json);
        const auto strings = doc.value("strings", nlohmann::json::object());
        for (const auto& [k, v] : strings.items())
            if (v.is_string() && !v.get<std::string>().empty())
                g_dict[k] = v.get<std::string>();
    } catch (const std::exception& e) {
        spdlog::warn("language {}: {}", g_code, e.what());
    }
    spdlog::info("language {}: {} strings", g_code, g_dict.size());
}

const std::string& languageCode() {
    return g_code;
}

std::string systemLanguage() {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    std::string found = "en";
    for (int i = 0; locales && i < count; ++i) {
        const std::string lang = locales[i]->language ? locales[i]->language : "";
        const auto it = std::find_if(std::begin(kLanguages), std::end(kLanguages), [&](const Language& l) { return lang == l.code; });
        if (it != std::end(kLanguages)) {
            found = it->code;
            break;
        }
    }
    SDL_free(locales);
    return found;
}

const char* tr(const char* text) {
    if (!text || !*text || g_dict.empty())
        return text;
    std::lock_guard lock(g_mutex);
    auto [it, inserted] = g_cache.try_emplace(text);
    if (inserted)
        it->second = translate(text);
    return it->second.c_str();
}

const char* tr(const std::string& text) {
    if (g_dict.empty())
        return text.c_str();
    return tr(text.c_str());
}

std::string trs(std::string_view text) {
    if (g_dict.empty())
        return std::string(text);
    std::lock_guard lock(g_mutex);
    return translate(text);
}

}
