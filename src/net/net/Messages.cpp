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
            if constexpr (std::is_same_v<T, RecruitCommand>) {
                w.u16(c.unit);
                w.u32(entt::to_integral(c.factory));
                return;
            } else if constexpr (std::is_same_v<T, StructureCommand>) {
                w.u32(entt::to_integral(c.target));
                w.u8(static_cast<std::uint8_t>(c.op));
                w.u8(c.slot);
                w.f32(c.point.x);
                w.f32(c.point.y);
                return;
            } else if constexpr (std::is_same_v<T, PromoteCommand>) {
                w.u32(entt::to_integral(c.unit));
                return;
            } else if constexpr (std::is_same_v<T, CastCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u8(c.slot);
                w.f32(c.point.x);
                w.f32(c.point.y);
                return;
            } else if constexpr (std::is_same_v<T, LearnCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u16(c.skill);
                return;
            } else if constexpr (std::is_same_v<T, BuyCommand>) {
                w.u16(c.item);
                return;
            } else if constexpr (std::is_same_v<T, EquipCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u8(c.stashIndex);
                return;
            } else if constexpr (std::is_same_v<T, UnequipCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u8(c.slot);
                return;
            } else if constexpr (std::is_same_v<T, RefitCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u16(c.variant);
                return;
            } else if constexpr (std::is_same_v<T, ModuleCommand>) {
                w.u32(entt::to_integral(c.unit));
                w.u16(c.module);
                return;
            } else if constexpr (std::is_same_v<T, DeployCommand>) {
                writeUnits(w, c.targets);
                return;
            } else if constexpr (std::is_same_v<T, CheatCommand>) {
                w.u8(static_cast<std::uint8_t>(c.op));
                w.u16(c.unit);
                w.u8(c.team);
                w.u8(c.count);
                w.f32(c.point.x);
                w.f32(c.point.y);
                writeUnits(w, c.targets);
                return;
            } else {
                writeUnits(w, c.units);
            }
            if constexpr (std::is_same_v<T, MoveCommand>) {
                w.f32(c.point.x);
                w.f32(c.point.y);
                w.u8((c.attack ? 1 : 0) | (c.queue ? 2 : 0) | (c.patrol ? 4 : 0));
            } else if constexpr (std::is_same_v<T, AttackCommand>) {
                w.u32(entt::to_integral(c.target));
                w.u8(c.queue ? 1 : 0);
            } else if constexpr (std::is_same_v<T, StopCommand>) {
                w.u8(c.hold ? 1 : 0);
            } else if constexpr (std::is_same_v<T, BuildCommand>) {
                w.u16(c.unit);
                w.f32(c.pos.x);
                w.f32(c.pos.y);
                w.u8(c.queue ? 1 : 0);
            } else if constexpr (std::is_same_v<T, UnloadCommand>) {
                w.f32(c.point.x);
                w.f32(c.point.y);
                w.u8(c.queue ? 1 : 0);
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
        const auto flags = r.u8();
        c.attack = (flags & 1) != 0;
        c.queue = (flags & 2) != 0;
        c.patrol = (flags & 4) != 0;
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
        c.queue = r.u8() != 0;
        command = std::move(c);
        break;
    }
    case 2: {
        StopCommand c;
        if (!readUnits(r, c.units))
            return false;
        c.hold = r.u8() != 0;
        command = std::move(c);
        break;
    }
    case 3: {
        RecruitCommand c;
        c.unit = r.u16();
        c.factory = static_cast<entt::entity>(r.u32());
        command = c;
        break;
    }
    case 15: {
        StructureCommand c;
        c.target = static_cast<entt::entity>(r.u32());
        c.op = static_cast<StructureOp>(std::min<int>(r.u8(), 3));
        c.slot = r.u8();
        c.point = {r.f32(), r.f32()};
        if (!std::isfinite(c.point.x) || !std::isfinite(c.point.y))
            return false;
        command = c;
        break;
    }
    case 4: {
        PromoteCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        command = c;
        break;
    }
    case 5: {
        CastCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.slot = r.u8();
        c.point = {r.f32(), r.f32()};
        if (!std::isfinite(c.point.x) || !std::isfinite(c.point.y))
            return false;
        command = c;
        break;
    }
    case 6: {
        LearnCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.skill = r.u16();
        command = c;
        break;
    }
    case 7: {
        BuyCommand c;
        c.item = r.u16();
        command = c;
        break;
    }
    case 8: {
        EquipCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.stashIndex = r.u8();
        command = c;
        break;
    }
    case 9: {
        UnequipCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.slot = r.u8();
        command = c;
        break;
    }
    case 16: {
        CheatCommand c;
        const auto op = r.u8();
        if (op >= static_cast<std::uint8_t>(CheatOp::Count))
            return false;
        c.op = static_cast<CheatOp>(op);
        c.unit = r.u16();
        c.team = std::min<std::uint8_t>(r.u8(), 1);
        c.count = r.u8();
        c.point = {r.f32(), r.f32()};
        if (!std::isfinite(c.point.x) || !std::isfinite(c.point.y) || !readUnits(r, c.targets))
            return false;
        command = std::move(c);
        break;
    }
    case 14: {
        DeployCommand c;
        if (!readUnits(r, c.targets))
            return false;
        command = std::move(c);
        break;
    }
    case 13: {
        ModuleCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.module = r.u16();
        command = c;
        break;
    }
    case 12: {
        UnloadCommand c;
        if (!readUnits(r, c.units))
            return false;
        c.point = {r.f32(), r.f32()};
        c.queue = r.u8() != 0;
        if (!std::isfinite(c.point.x) || !std::isfinite(c.point.y))
            return false;
        command = std::move(c);
        break;
    }
    case 11: {
        RefitCommand c;
        c.unit = static_cast<entt::entity>(r.u32());
        c.variant = r.u16();
        command = c;
        break;
    }
    case 10: {
        BuildCommand c;
        if (!readUnits(r, c.units))
            return false;
        c.unit = r.u16();
        c.pos = {r.f32(), r.f32()};
        c.queue = r.u8() != 0;
        if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y))
            return false;
        command = c;
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
