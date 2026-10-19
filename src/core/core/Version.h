#pragma once

#include <cstdint>
#include <string_view>

namespace vw {

inline constexpr std::string_view kVersion = VW_VERSION;
inline constexpr std::uint32_t kProtocolVersion = 16;

}
