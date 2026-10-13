#pragma once

#include <cstdint>

#include "core/Commands.h"
#include "net/Serialize.h"

namespace vw::net {

void writeCommand(Writer& w, std::uint32_t seq, const Command& command);
bool readCommand(Reader& r, std::uint32_t& seq, Command& command);

}
