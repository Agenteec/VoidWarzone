#include "net/Messages.h"

#include <cmath>
#include <type_traits>
#include <variant>

#include "net/Protocol.h"

namespace vw::net {

namespace {

constexpr std::size_t kMaxUnits = 256;

void writeUnits(Writer& w, const std::vector<entt::entity>& units) {
    w.varu(units.size());
    for (const auto e : units)
        w.u32(entt::to_integral(e));
}

bool readUnits(Reader& r, std::vector<entt::entity>& units) {
    const auto n = r.varu();
    if (n > kMaxUnits)
        return false;
    units.clear();
    for (std::uint64_t i = 0; i < n && r.ok(); ++i)
        units.push_back(static_cast<entt::entity>(r.u32()));
    return r.ok();
}

}

void writeCommand(Writer& w, std::uint32_t seq, const Command& command) {
    w.u8(static_cast<std::uint8_t>(MessageType::Command));
    w.u32(seq);
    w.u8(static_cast<std::uint8_t>(command.index()));
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            writeUnits(w, c.units);
            if constexpr (std::is_same_v<T, MoveCommand>) {
                w.f32(c.point.x);
                w.f32(c.point.y);
                w.u8(c.attack ? 1 : 0);
            } else if constexpr (std::is_same_v<T, AttackCommand>) {
                w.u32(entt::to_integral(c.target));
            }
        },
        command);
}

bool readCommand(Reader& r, std::uint32_t& seq, Command& command) {
    seq = r.u32();
    const auto type = r.u8();
    switch (type) {
    case 0: {
        MoveCommand c;
        if (!readUnits(r, c.units))
            return false;
        c.point = {r.f32(), r.f32()};
        c.attack = r.u8() != 0;
        if (!std::isfinite(c.point.x) || !std::isfinite(c.point.y))
            return false;
        command = std::move(c);
        break;
    }
    case 1: {
        AttackCommand c;
        if (!readUnits(r, c.units))
            return false;
        c.target = static_cast<entt::entity>(r.u32());
        command = std::move(c);
        break;
    }
    case 2: {
        StopCommand c;
        if (!readUnits(r, c.units))
            return false;
        command = std::move(c);
        break;
    }
    default:
        return false;
    }
    return r.ok();
}

const char* denyReasonText(DenyReason reason) {
    switch (reason) {
    case DenyReason::Version:
        return "version mismatch";
    case DenyReason::Data:
        return "game data mismatch";
    case DenyReason::Full:
        return "server is full";
    }
    return "denied";
}

}
