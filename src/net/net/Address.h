#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace vw::net {

struct Address {
    std::uint32_t ip = 0;
    std::uint16_t port = 0;

    static std::optional<Address> parse(std::string_view text, std::uint16_t defaultPort);
    static Address loopback(std::uint16_t port) { return {0x7f000001u, port}; }
    static Address broadcast(std::uint16_t port) { return {0xffffffffu, port}; }

    bool valid() const { return port != 0; }
    std::string toString() const;
    bool operator==(const Address&) const = default;
};

struct AddressHash {
    std::size_t operator()(const Address& a) const noexcept { return std::hash<std::uint64_t>{}((std::uint64_t{a.ip} << 16) | a.port); }
};

}
