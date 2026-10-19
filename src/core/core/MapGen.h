#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vw {

// Random skirmish maps are named "random_<seed>" and generated with integer-only maths, so every client and the
// server build the same map (and map hash) from the name alone.
inline constexpr std::string_view kRandomMapPrefix = "random_";

std::string generateMap(std::uint32_t seed);
std::optional<std::string> generatedMap(std::string_view name);

}
