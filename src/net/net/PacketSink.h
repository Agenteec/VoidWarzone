#pragma once

#include <cstddef>

#include "net/Address.h"

namespace vw::net {

class PacketSink {
public:
    virtual ~PacketSink() = default;
    virtual bool send(const Address& to, const void* data, std::size_t size) = 0;
};

}
