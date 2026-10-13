#include "net/Lobby.h"

#include <cctype>

namespace vw::net {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr std::string_view kPrefix = "VOID-";

}

std::string makeRoomCode(std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> pick(0, kAlphabet.size() - 1);
    std::string code(kPrefix);
    for (std::size_t i = 0; i < kRoomCodeLength; ++i)
        code += kAlphabet[pick(rng)];
    return code;
}

std::string normalizeRoomCode(std::string_view text) {
    std::string raw;
    for (const char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)))
            raw += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (raw.size() == kRoomCodeLength + 4 && raw.rfind("VOID", 0) == 0)
        raw.erase(0, 4);
    if (raw.size() != kRoomCodeLength)
        return {};
    for (const char c : raw)
        if (kAlphabet.find(c) == std::string_view::npos)
            return {};
    return std::string(kPrefix) + raw;
}

void writeIceServers(Writer& w, const IceServers& s) {
    w.str(s.stunHost);
    w.u16(s.stunPort);
    w.str(s.turnHost);
    w.u16(s.turnPort);
    w.str(s.turnUser);
    w.str(s.turnPass);
}

IceServers readIceServers(Reader& r) {
    IceServers s;
    s.stunHost = r.str(128);
    s.stunPort = r.u16();
    s.turnHost = r.str(128);
    s.turnPort = r.u16();
    s.turnUser = r.str(128);
    s.turnPass = r.str(128);
    return s;
}

void writeRoom(Writer& w, const RoomInfo& room) {
    w.str(room.code);
    w.str(room.name);
    w.str(room.map);
    w.u8(room.players);
    w.u8(room.maxPlayers);
    w.u8(room.listed ? 1 : 0);
}

RoomInfo readRoom(Reader& r) {
    RoomInfo room;
    room.code = r.str(16);
    room.name = r.str(64);
    room.map = r.str(64);
    room.players = r.u8();
    room.maxPlayers = r.u8();
    room.listed = r.u8() != 0;
    return room;
}

}
