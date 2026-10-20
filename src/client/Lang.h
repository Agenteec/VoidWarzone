#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace vw::client {

struct Language {
    const char* code;
    const char* name;
};

// Languages with a dictionary in data/lang/<code>.json ({"strings": {"English text": "translation"}}); English is the source.
inline constexpr Language kLanguages[] = {{"en", "English"}, {"ru", "Русский"}, {"uk", "Українська"}, {"de", "Deutsch"}, {"es", "Español"}};

void setDictionary(std::string_view code, std::string_view json);
const std::string& languageCode();
// First preferred system locale that has a dictionary, else "en".
std::string systemLanguage();

// Translates UI text. Keys are the English strings without leading Font Awesome icons and spaces or a trailing ImGui "##id";
// those parts are kept around the translation. The returned pointer stays valid for the program's lifetime.
const char* tr(const char* text);
const char* tr(const std::string& text);
std::string trs(std::string_view text);

template <std::size_t N>
std::array<const char*, N> trList(const char* const (&in)[N]) {
    std::array<const char*, N> out{};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = tr(in[i]);
    return out;
}

}
