#pragma once

#include <cstddef>
#include <cstdint>

namespace vw::net {

inline constexpr std::size_t kMaxPacketSize = 1200;
inline constexpr std::uint16_t kGamePort = 27450;
inline constexpr std::uint16_t kMasterPort = 27400;
inline constexpr std::uint16_t kStunPort = 3478;

}
