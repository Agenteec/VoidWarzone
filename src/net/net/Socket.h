#pragma once

#include <cstddef>
#include <cstdint>

#include "net/Address.h"

namespace vw::net {

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(std::uint16_t port = 0, bool broadcast = false);
    void close();
    bool isOpen() const;
    std::uint16_t port() const { return port_; }

    bool send(const Address& to, const void* data, std::size_t size);
    int receive(Address& from, void* data, std::size_t capacity);

private:
    std::uintptr_t handle_ = ~std::uintptr_t{0};
    std::uint16_t port_ = 0;
};

}
